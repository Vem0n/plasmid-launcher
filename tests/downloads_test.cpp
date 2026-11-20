// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "externalprocess.h"
#include "launchfixture.h"

#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QQuickWindow>
#include <QApplication>
#include <QStandardPaths>
#include <cstdio>

class DownloadsTest : public QObject
{
    Q_OBJECT
    static void writeFile(const QString &path)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("fixture") > 0);
    }
private Q_SLOTS:
    void filenameFilterKeepsPathsAndLiveUpdates()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto root = sandbox.path() + "/Downloads";
        QVERIFY(QDir().mkdir(root));
        writeFile(root + "/first.zip");
        writeFile(root + "/Notes [1].TXT");
        DownloadsModel source(root, sandbox.path() + "/cache.json");
        DownloadsFilterModel filtered(&source);
        QTRY_COMPARE_WITH_TIMEOUT(filtered.count(), 2, 5000);
        filtered.setQuery("  NOTES  ");
        QCOMPARE(filtered.count(), 1);
        QCOMPARE(filtered.pathAt(0), root + "/Notes [1].TXT");
        filtered.setQuery("[1]"); // Literal text, not a regular expression.
        QCOMPARE(filtered.count(), 1);
        filtered.setQuery("not present");
        QCOMPARE(filtered.count(), 0);
        QCOMPARE(filtered.pathAt(0), QString());
        filtered.setQuery(".zip");
        writeFile(root + "/new.zip");
        QTRY_COMPARE_WITH_TIMEOUT(filtered.count(), 2, 5000);
        QCOMPARE(filtered.pathAt(0), root + "/new.zip");
        QVERIFY(QFile::remove(root + "/new.zip"));
        QTRY_COMPARE_WITH_TIMEOUT(filtered.count(), 1, 5000);
        QCOMPARE(filtered.pathAt(0), root + "/first.zip");
        filtered.setQuery("");
        QCOMPARE(filtered.count(), source.count());
        for (int row = 0; row < source.count(); ++row)
            QCOMPARE(filtered.pathAt(row), source.pathAt(row));
        QTRY_VERIFY(!source.scanning());
    }

    void detachedOutputGoesToItsOwnLog()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto log = sandbox.path() + "/external.log";
        const auto done = sandbox.path() + "/done";
        QVERIFY(startExternalApplication(QCoreApplication::applicationFilePath(), {"--external-log-probe", done}, log));
        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        QTRY_COMPARE_WITH_TIMEOUT(read(done), QByteArray("finished"), 5000);
        QVERIFY(read(log).contains("external stdout"));
        QVERIFY(read(log).contains("external stderr"));
    }

    void externalAppsDoNotInheritDropdownSettings()
    {
        QProcessEnvironment host;
        host.insert("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
        host.insert("QT_QPA_PLATFORM", "offscreen");
        host.insert("QT_QUICK_BACKEND", "software");
        host.insert("WAYLAND_DISPLAY", "wayland-test");
        host.insert("XDG_CURRENT_DESKTOP", "KDE");
        host.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/test-bus");
        const auto child = externalApplicationEnvironment(host);
        QVERIFY(!child.contains("QT_WAYLAND_SHELL_INTEGRATION"));
        QVERIFY(!child.contains("QT_QPA_PLATFORM"));
        QVERIFY(!child.contains("QT_QUICK_BACKEND"));
        QCOMPARE(child.value("WAYLAND_DISPLAY"), host.value("WAYLAND_DISPLAY"));
        QCOMPARE(child.value("XDG_CURRENT_DESKTOP"), host.value("XDG_CURRENT_DESKTOP"));
        QCOMPARE(child.value("DBUS_SESSION_BUS_ADDRESS"), host.value("DBUS_SESSION_BUS_ADDRESS"));
    }

    void arrivalsMovesAndPersistence()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto root = sandbox.path() + "/Downloads";
        const auto cache = sandbox.path() + "/index/downloads.json";
        QVERIFY(QDir().mkpath(root));
        const auto original = root + "/old.zip";
        writeFile(original);
        qint64 arrival = 0;
        {
            DownloadsModel model(root, cache);
            QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1, 5000);
            arrival = model.data(model.index(0), DownloadsModel::ArrivalRole).toLongLong();
            QSignalSpy changes(&model, &DownloadsModel::refreshed);
            // New incoming file, directory and hidden entry; no recursive traversal.
            QTest::qWait(5);
            const auto image = root + "/some App.Image.AppImage";
            writeFile(image);
            QVERIFY(QDir().mkdir(root + "/folder"));
            writeFile(root + "/folder/nested.txt");
            writeFile(root + "/.hidden");
            QTRY_COMPARE_WITH_TIMEOUT(model.count(), 4, 5000);
            QVERIFY(model.contains(image));
            QVERIFY(!model.contains(root + "/folder/nested.txt"));
            QCOMPARE(model.data(model.index(model.count() - 1), DownloadsModel::PathRole).toString(), original);
            QVERIFY(QFile::rename(image, root + "/renamed.AppImage"));
            QTRY_VERIFY_WITH_TIMEOUT(!model.contains(image) && model.contains(root + "/renamed.AppImage"), 5000);
            QVERIFY(QFile::rename(root + "/renamed.AppImage", sandbox.path() + "/moved.AppImage"));
            QVERIFY(QFile::remove(root + "/.hidden"));
            QVERIFY(QDir(root + "/folder").removeRecursively());
            QTRY_COMPARE_WITH_TIMEOUT(model.count(), 1, 5000);
            QCOMPARE(model.data(model.index(0), DownloadsModel::ArrivalRole).toLongLong(), arrival);
            // No periodic rescans while idle.
            QTRY_VERIFY(!model.scanning());
            QTest::qWait(450);
            const int settled = changes.count();
            QTest::qWait(500);
            QCOMPARE(changes.count(), settled);
            QVERIFY(QFile::exists(cache));
        }
        DownloadsModel restored(root, cache);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 5000);
        QCOMPARE(restored.data(restored.index(0), DownloadsModel::ArrivalRole).toLongLong(), arrival);
        QVERIFY(QDir(root).removeRecursively());
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 0, 5000);
        QVERIFY(QDir().mkpath(root));
        writeFile(root + "/back.rar");
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 5000);
        QVERIFY(restored.contains(root + "/back.rar"));
        QTRY_VERIFY(!restored.scanning());
    }

    void externalCommandsWaitForUnmappedWindow()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto root = sandbox.path() + "/Downloads";
        QVERIFY(QDir().mkpath(root));
        const auto file = root + "/-archive ; $(noop).zip";
        writeFile(file);
        Launcher backend(nullptr, root, sandbox.path() + "/cache.json");
        QQuickWindow window;
        backend.setWindow(&window);
        QSignalSpy requested(&backend, &Launcher::externalCommandRequested);
        QSignalSpy close(&backend, &Launcher::externalClosing);
        QSignalSpy errors(&backend, &Launcher::launchError);
        QTRY_COMPARE_WITH_TIMEOUT(backend.downloads()->count(), 1, 5000);
        window.show();
        QVERIFY(backend.openDownloads());
        QCOMPARE(close.count(), 1);
        QTest::qWait(50);
        QCOMPARE(requested.count(), 0); // A still-visible host cannot launch anything.
        window.hide();
        QTRY_COMPARE(requested.count(), 1);
        auto command = requested.takeFirst();
        QCOMPARE(command[0].toString(), QString("dolphin"));
        QCOMPARE(command[1].toStringList(), QStringList{QUrl::fromLocalFile(root).toString(QUrl::FullyEncoded)});

        QVERIFY(backend.openDownload(file));
        QTRY_COMPARE(requested.count(), 1);
        command = requested.takeFirst();
        QCOMPARE(command[0].toString(), QString("xdg-open"));
        QCOMPARE(command[1].toStringList(), QStringList{QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)});
        QVERIFY(backend.revealDownload(file));
        QTRY_COMPARE(requested.count(), 1);
        command = requested.takeFirst();
        QCOMPARE(command[0].toString(), QString("dolphin"));
        QCOMPARE(command[1].toStringList(), (QStringList{"--select", QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)}));

        // Membership and existence are checked at click time and again at dispatch.
        QVERIFY(!backend.openDownload(sandbox.path() + "/outside.txt"));
        QVERIFY(QFile::remove(file));
        QVERIFY(!backend.revealDownload(file));
        QVERIFY(errors.count() >= 2);
        QCOMPARE(requested.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(backend.downloads()->count(), 0, 5000);
        QTRY_VERIFY(!backend.downloads()->scanning());
    }

    void placesAndPinnedAppsUseExternalLaunchPath()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        QVERIFY(QDir().mkdir(sandbox.path() + "/Downloads"));
        Launcher backend(nullptr, sandbox.path() + "/Downloads", sandbox.path() + "/cache.json");
        QQuickWindow window;
        backend.setWindow(&window);
        QSignalSpy requested(&backend, &Launcher::externalCommandRequested);
        QSignalSpy close(&backend, &Launcher::externalClosing);
        QSignalSpy errors(&backend, &Launcher::launchError);
        const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QVERIFY(documents.endsWith("/Documents with spaces"));
        QCOMPARE(backend.favorites().size(), 1);
        QCOMPARE(backend.favorites().first().toMap().value("desktopId").toString(), QString("org.plasmid.Fixture.desktop"));

        // Capture commands, keeping the host mapped until explicitly hidden.
        for (const auto &place : {QString("home"), QString("documents")}) {
            window.show();
            QVERIFY(backend.openPlace(place));
            QTest::qWait(30);
            QCOMPARE(requested.count(), 0);
            QVERIFY(!backend.openPlace(place)); // Ignore a second pending click.
            window.hide();
            QTRY_COMPARE(requested.count(), 1);
            const auto command = requested.takeFirst();
            QCOMPARE(command[0].toString(), QString("dolphin"));
            const auto path = place == "home" ? QDir::homePath() : documents;
            QCOMPARE(command[1].toStringList(), QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)});
        }

        window.show();
        QVERIFY(backend.launchFavorite("org.plasmid.Fixture.desktop"));
        QTest::qWait(30);
        QCOMPARE(requested.count(), 0);
        window.hide();
        QTRY_COMPARE(requested.count(), 1);
        const auto command = requested.takeFirst();
        QCOMPARE(command[0].toString(), QString("kstart"));
        QCOMPARE(command[1].toStringList(), (QStringList{"--application", "org.plasmid.Fixture"}));
        QCOMPARE(close.count(), 3);

        QVERIFY(!backend.openPlace(documents)); // No arbitrary paths.
        QVERIFY(!backend.launchFavorite("org.plasmid.NotPinned.desktop"));
        QVERIFY(!backend.launchFavorite("../org.plasmid.Fixture.desktop"));
        const auto desktop = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "org.plasmid.Fixture.desktop");
        QVERIFY(!desktop.isEmpty());
        QVERIFY(LaunchFixture::write(desktop, "[Desktop Entry]\nType=Application\nHidden=true\nExec=never-run-this-fixture\n"));
        QVERIFY(!backend.launchFavorite("org.plasmid.Fixture.desktop"));
        QVERIFY(QFile::remove(desktop));
        QVERIFY(!backend.launchFavorite("org.plasmid.Fixture.desktop"));
        QVERIFY(QDir().rmdir(documents));
        QVERIFY(!backend.openPlace("documents"));
        QCOMPARE(errors.count(), 6);
        QCOMPARE(requested.count(), 0);
        QTRY_VERIFY(!backend.downloads()->scanning());
    }
};
int main(int argc, char **argv)
{
    // CLI-only child fixture: exercise detached standard streams without ever
    // starting a GUI application or opening a user's files.
    if (argc == 3 && QByteArray(argv[1]) == "--external-log-probe") {
        std::fputs("external stdout\n", stdout);
        std::fputs("external stderr\n", stderr);
        std::fflush(stdout);
        std::fflush(stderr);
        QFile marker(QString::fromLocal8Bit(argv[2]));
        if (!marker.open(QIODevice::WriteOnly))
            return 1;
        marker.write("finished");
        return 0;
    }
    LaunchFixture fixture;
    if (!fixture.setup())
        return 1;
    QApplication app(argc, argv);
    DownloadsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "downloads_test.moc"
