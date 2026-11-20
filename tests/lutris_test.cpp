// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "applicationlocation.h"
#include "launchfixture.h"
#include <QApplication>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>
#include <QUuid>

class LutrisTest : public QObject
{
    Q_OBJECT
    static bool sql(const QString &path, const QStringList &statements)
    {
        const auto connection = QUuid::createUuid().toString();
        bool ok = true;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            ok = db.open();
            for (const auto &statement : statements) {
                QSqlQuery query(db);
                if (!ok || !query.exec(statement)) { ok = false; break; }
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return ok;
    }
    static LutrisSource source(const QString &root, bool flatpak = false)
    {
        QDir().mkpath(root);
        return {root + "/pga.db", root, root + "/lutris.conf", flatpak};
    }
    static bool populate(const LutrisSource &s)
    {
        return sql(s.database, {
            "CREATE TABLE games(id INTEGER PRIMARY KEY, name TEXT, slug TEXT, configpath TEXT, directory TEXT, runner TEXT, installed INTEGER)",
            "INSERT INTO games VALUES(7, 'Installed fixture', 'fixture', 'fixture-config', '', 'wine', 1)",
            "INSERT INTO games VALUES(8, 'Not installed', 'absent', 'absent-config', '', 'wine', 0)",
            "INSERT INTO games VALUES(9, '', 'empty', '', '', '', 1)"});
    }
    static QByteArray contents(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private Q_SLOTS:
    void missingCorruptAndUnsupportedAreOptional()
    {
        QTemporaryDir temp;
        const auto s = source(temp.path() + "/missing");
        auto snapshot = readLutrisLibraries({s});
        QVERIFY(snapshot.entries.isEmpty());
        QVERIFY(snapshot.error.isEmpty());
        QVERIFY(!QFileInfo::exists(s.database)); // A read must never initialize a database.
        const auto watches = lutrisWatchPaths({{QDir::homePath() + "/absent-parent/absent/pga.db", {}, {}, false}});
        QVERIFY(!watches.contains(QDir::homePath()));
        QVERIFY(!watches.contains("/"));
        QVERIFY(LaunchFixture::write(s.database, "cloud fork metadata, not sqlite"));
        const auto corrupt = contents(s.database);
        snapshot = readLutrisLibraries({s});
        QVERIFY(snapshot.entries.isEmpty());
        QVERIFY(!snapshot.error.isEmpty());
        QCOMPARE(contents(s.database), corrupt);
        const auto desktops = temp.path() + "/applications";
        QVERIFY(QDir().mkpath(desktops));
        QVERIFY(LaunchFixture::write(desktops + "/ordinary.desktop",
            "[Desktop Entry]\nType=Application\nName=Ordinary application\nExec=never-execute\n"));
        ApplicationsModel model(nullptr, {desktops}, {}, {}, QList<LutrisSource>{s});
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.validatedEntry("ordinary.desktop"));
        QVERIFY(!model.libraryError().isEmpty());
        QVERIFY(QFile::remove(s.database));
        QVERIFY(sql(s.database, {"CREATE TABLE games(unrelated TEXT)"}));
        const auto unsupported = contents(s.database);
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!model.libraryError().isEmpty());
        QCOMPARE(contents(s.database), unsupported);
        QVERIFY(QFile::remove(s.database));
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.libraryError().isEmpty());
    }

    void installedIdentityDedupAndReadOnly()
    {
        QTemporaryDir temp;
        const auto native = source(temp.path() + "/native");
        const auto flatpak = source(temp.path() + "/flatpak", true);
        QVERIFY(populate(native));
        QVERIFY(populate(flatpak));
        const auto before = contents(native.database);
        const auto modified = QFileInfo(native.database).lastModified();
        const auto snapshot = readLutrisLibraries({native, flatpak});
        QVERIFY(snapshot.error.isEmpty());
        QCOMPARE(snapshot.entries.size(), 2);
        const auto game = snapshot.entries.first();
        const auto other = snapshot.entries.last();
        QVERIFY(game.desktopId != other.desktopId);
        auto sameDatabase = native;
        sameDatabase.flatpak = true;
        QVERIFY(readLutrisLibraries({sameDatabase}).entries.first().desktopId != game.desktopId);
        QCOMPARE(game.libraryId, QString("7"));
        QVERIFY(game.command.isEmpty());
        QCOMPARE(contents(native.database), before);
        QCOMPARE(QFileInfo(native.database).lastModified(), modified);
        const auto desktops = temp.path() + "/applications";
        QVERIFY(QDir().mkpath(desktops));
        ApplicationsModel model(nullptr, {desktops}, {}, {}, QList<LutrisSource>{native, flatpak});
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(LaunchFixture::write(desktops + "/shortcut.desktop",
            "[Desktop Entry]\nType=Application\nName=Shortcut\nExec=lutris lutris:rungameid/7\n"));
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 2); // Real shortcut + Flatpak import, no duplicate native row.
        QVERIFY(model.validatedEntry(game.desktopId)); // Old pins still work after creating a shortcut.
        QCOMPARE(model.entries().size(), 3); // Includes the non-display alias for persisted group membership.
        QVERIFY(sql(native.database, {"UPDATE games SET slug='different', configpath='replacement' WHERE id=7"}));
        QVERIFY(!validateLutrisEntry(game)); // A reused numeric ID cannot launch a different game.
        QVERIFY(validateLutrisEntry(other));
    }

    void configuredDatabasePath()
    {
        const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/lutris";
        QVERIFY(QDir().mkpath(config));
        QTemporaryDir temp;
        const auto custom = temp.path() + "/custom library.db";
        QVERIFY(LaunchFixture::write(config + "/lutris.conf", "[lutris]\npga_path=" + custom.toUtf8() + "\n"));
        QCOMPARE(discoverLutrisSources().first().database, custom);
        QVERIFY(!QFileInfo::exists(custom));
        QVERIFY(QFile::remove(config + "/lutris.conf"));
    }

    void temporaryLockKeepsRowsButBlocksStaleLaunch()
    {
        QTemporaryDir temp;
        const auto s = source(temp.path() + "/library");
        QVERIFY(populate(s));
        const auto desktops = temp.path() + "/applications";
        QVERIFY(QDir().mkpath(desktops));
        ApplicationsModel model(nullptr, {desktops}, {}, {}, QList<LutrisSource>{s});
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 1);
        const auto game = model.entries().first();
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(s.database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("BEGIN EXCLUSIVE"));
            model.refresh();
            QTRY_VERIFY(!model.scanning());
            QCOMPARE(model.rowCount(), 1);
            QVERIFY(!model.libraryError().isEmpty());
            QVERIFY(!validateLutrisEntry(game));
            QVERIFY(query.exec("ROLLBACK"));
        }
        QSqlDatabase::removeDatabase(connection);
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.libraryError().isEmpty());
        QVERIFY(validateLutrisEntry(game));
        QVERIFY(QFile::remove(s.database));
        model.refresh();
        QTRY_VERIFY(!model.scanning());
        QCOMPARE(model.rowCount(), 0); // Absence removes old rows instead of retaining ghosts.
    }

    void launchesThroughMatchingLutrisAndRejectsRemovedGame()
    {
        for (const bool sandboxed : {false, true}) {
            QTemporaryDir temp;
            const auto s = source(temp.path() + "/library", sandboxed);
            QVERIFY(populate(s));
            const auto install = temp.path() + "/Game folder";
            QVERIFY(QDir().mkpath(install));
            auto escaped = install;
            escaped.replace('\'', "''");
            QVERIFY(sql(s.database, {"UPDATE games SET directory='" + escaped + "' WHERE id=7"}));
            Launcher backend(nullptr, temp.path(), temp.path() + "/cache.json", false, QList<LutrisSource>{s});
            backend.applications()->refresh();
            QTRY_VERIFY(!backend.applications()->scanning());
            const auto game = readLutrisLibraries({s}).entries.first();
            QCOMPARE(resolveApplicationLocation(game, {}).path, install);
            const auto group = backend.groups()->create("Games");
            const auto item = backend.applicationItem(game.desktopId);
            QVERIFY(!item.isEmpty());
            QVERIFY(backend.groups()->add(group, item));
            QQuickWindow window;
            backend.setWindow(&window);
            connect(&backend, &Launcher::externalClosing, &window, &QWindow::hide);
            QSignalSpy commands(&backend, &Launcher::externalCommandRequested);
            QSignalSpy errors(&backend, &Launcher::launchError);
            window.show();
            QVERIFY(backend.launchGroupItem(group, item.value("key").toString()));
            QVERIFY(!window.isVisible());
            QTRY_COMPARE(commands.count(), 1);
            const auto command = commands.takeFirst();
            QCOMPARE(command[0].toString(), sandboxed ? QString("flatpak") : QString("lutris"));
            QCOMPARE(command[1].toStringList(), sandboxed
                ? (QStringList{"run", "net.lutris.Lutris", "lutris:rungameid/7"}) : (QStringList{"lutris:rungameid/7"}));
            QVERIFY(sql(s.database, {"UPDATE games SET installed=0 WHERE id=7"}));
            window.show();
            QVERIFY(backend.launchApplication(game.desktopId)); // Cached row revalidated asynchronously.
            QTRY_COMPARE(errors.count(), 1);
            QCOMPARE(commands.count(), 0);
            QVERIFY(resolveApplicationLocation(game, {}).path.isEmpty());
        }
    }

    void walEventsRefreshWithoutPollingAndRespectBell()
    {
        QTemporaryDir temp;
        const auto s = source(temp.path() + "/library");
        QVERIFY(populate(s));
        const auto desktops = temp.path() + "/applications";
        QVERIFY(QDir().mkpath(desktops));
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(s.database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("PRAGMA journal_mode=WAL"));
            query.finish();
            ApplicationsModel model(nullptr, {desktops}, {}, {}, QList<LutrisSource>{s});
            QSignalSpy scans(&model, &ApplicationsModel::refreshed);
            model.setWatching(true);
            QTRY_COMPARE(model.rowCount(), 1);
            QTRY_VERIFY(!model.scanning());
            QVERIFY(query.exec("UPDATE games SET name='Updated via WAL' WHERE id=7"));
            query.finish();
            QTRY_COMPARE(model.index(0, 0).data(ApplicationsModel::NameRole).toString(), QString("Updated via WAL"));
            // Let coalesced filesystem notifications settle, then idle must stay idle.
            QTest::qWait(700);
            QTRY_VERIFY(!model.scanning());
            const auto count = scans.count();
            QTest::qWait(700);
            QCOMPARE(scans.count(), count);
            model.setWatching(false);
            QVERIFY(query.exec("UPDATE games SET name='Bell off' WHERE id=7"));
            query.finish();
            QTest::qWait(700);
            QCOMPARE(scans.count(), count);
            model.refresh(); // Browsing still works with background suggestions disabled.
            QTRY_COMPARE(model.index(0, 0).data(ApplicationsModel::NameRole).toString(), QString("Bell off"));
        }
        QSqlDatabase::removeDatabase(connection);
    }
};

int main(int argc, char **argv)
{
    LaunchFixture fixture;
    if (!fixture.setup()) return 1;
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    LutrisTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "lutris_test.moc"
