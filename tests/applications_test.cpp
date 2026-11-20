// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "launchfixture.h"
#include "applicationlocation.h"

#include <QApplication>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>

class ApplicationsTest : public QObject
{
    Q_OBJECT
    static void entry(const QString &path, const QByteArray &values)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QVERIFY(LaunchFixture::write(path, "[Desktop Entry]\nType=Application\nExec=never-run-this-fixture\n" + values));
    }
    static int rowFor(const ApplicationsModel &model, const QString &id)
    {
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.index(row, 0).data(ApplicationsModel::DesktopIdRole).toString() == id) return row;
        return -1;
    }
private Q_SLOTS:
    void searchRanksNamesBeforeMetadata()
    {
        QTemporaryDir sandbox;
        const auto root = sandbox.path();
        entry(root + "/steam.desktop", "Name=Steam\nCategories=Game;\n");
        entry(root + "/prefix.desktop", "Name=Steam Cleaner\nCategories=Utility;\n");
        entry(root + "/word.desktop", "Name=A Steam companion\nCategories=Utility;\n");
        entry(root + "/substring.desktop", "Name=PreSteam utility\nCategories=Utility;\n");
        entry(root + "/generic.desktop", "Name=A generic app\nGenericName=Steam\nCategories=Utility;\n");
        entry(root + "/keyword.desktop", "Name=A keyword app\nKeywords=Steam;\nCategories=Utility;\n");
        entry(root + "/game.desktop", "Name=Baldur's Gate 3\nComment=Installed through Steam\nCategories=Game;\n");
        entry(root + "/zeta.desktop", "Name=Zeta\nComment=Steam plugin\nCategories=Utility;\n");
        ApplicationsModel source(nullptr, {root}, "en_US", {"KDE"});
        ApplicationsFilterModel filtered(&source);
        filtered.setQuery(" sTeAm "); // Also rank correctly when the scan completes after typing.
        source.refresh();
        QTRY_COMPARE(filtered.rowCount(), 8);
        const QStringList expected{"steam.desktop", "prefix.desktop", "word.desktop", "substring.desktop",
            "generic.desktop", "keyword.desktop", "game.desktop", "zeta.desktop"};
        for (int i = 0; i < expected.size(); ++i) QCOMPARE(filtered.desktopIdAt(i), expected[i]);
        filtered.setCategory("Game");
        QCOMPARE(filtered.rowCount(), 2);
        QCOMPARE(filtered.desktopIdAt(0), QString("steam.desktop"));
        filtered.setCategory("");
        filtered.setQuery("zeta");
        QCOMPARE(filtered.rowCount(), 1);
        QCOMPARE(filtered.desktopIdAt(0), QString("zeta.desktop"));
        filtered.setQuery("steam");
        QCOMPARE(filtered.desktopIdAt(0), QString("steam.desktop"));
        filtered.setQuery("");
        QCOMPARE(filtered.rowCount(), source.rowCount());
        for (int i = 0; i < source.rowCount(); ++i)
            QCOMPARE(filtered.desktopIdAt(i), source.index(i, 0).data(ApplicationsModel::DesktopIdRole).toString());
        // Renaming a desktop entry invalidates a previous exact match on refresh.
        entry(root + "/steam.desktop", "Name=ZZZ Steam\nCategories=Game;\n");
        filtered.setQuery("steam");
        source.refresh();
        QTRY_VERIFY(!source.scanning());
        QCOMPARE(filtered.desktopIdAt(0), QString("prefix.desktop"));
    }

    void locationsUseGameLibrariesWorkingFoldersAndExecutables()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto root = sandbox.path() + "/Steam";
        const auto library = sandbox.path() + "/Other library";
        const auto game = library + "/steamapps/common/Game with spaces";
        QVERIFY(QDir().mkpath(root + "/steamapps"));
        QVERIFY(QDir().mkpath(game));
        const auto folders = root + "/steamapps/libraryfolders.vdf";
        QVERIFY(LaunchFixture::write(folders, "// modern layout\n\"libraryfolders\" { \"0\" { \"path\" \""
            + root.toUtf8() + "\" \"label\" \"\" } \"1\" { \"path\" \"" + library.toUtf8() + "\" } }"));
        const auto manifest = library + "/steamapps/appmanifest_123.acf";
        QVERIFY(LaunchFixture::write(manifest, "\"AppState\" { \"appid\" \"123\" \"installdir\" \"Game with spaces\" }"));
        ApplicationEntry app;
        app.command = "steam steam://rungameid/123";
        auto location = resolveApplicationLocation(app, {root});
        QCOMPARE(location.path, game);
        QCOMPARE(location.label, QString("Game folder"));
        QVERIFY(LaunchFixture::write(folders, "libraryfolders { 1 \"" + library.toUtf8() + "\" }")); // Older layout.
        QCOMPARE(resolveApplicationLocation(app, {root}).path, game);
        QVERIFY(LaunchFixture::write(manifest, "AppState { appid 123 installdir ../outside }"));
        QVERIFY(resolveApplicationLocation(app, {root}).path.isEmpty());
        QVERIFY(LaunchFixture::write(manifest, "AppState { appid 999 installdir \"Game with spaces\" }"));
        QVERIFY(resolveApplicationLocation(app, {root}).path.isEmpty());
        QVERIFY(LaunchFixture::write(manifest, "AppState { appid 123 installdir \"Game with spaces\"")); // Truncated.
        QVERIFY(resolveApplicationLocation(app, {root}).path.isEmpty());
        QVERIFY(QFile::remove(manifest));
        QVERIFY(resolveApplicationLocation(app, {root}).path.isEmpty());
        app.command = "opaque-wrapper --game";
        app.workingDirectory = game;
        QCOMPARE(resolveApplicationLocation(app, {}).path, game);
        app.workingDirectory.clear();
        const auto executable = sandbox.path() + "/Executable with spaces";
        QVERIFY(LaunchFixture::write(executable, "fixture"));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        app.command = "env FIXTURE=yes \"" + executable + "\" %U";
        QCOMPARE(resolveApplicationLocation(app, {}).path, executable);
        app.path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "org.plasmid.Fixture.desktop");
        app.command = "sh -c opaque-stuff";
        location = resolveApplicationLocation(app, {});
        QCOMPARE(location.path, app.path);
        QCOMPARE(location.label, QString("Desktop entry"));
    }

    void catalogPrecedenceVisibilityAndMetadata()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto user = sandbox.path() + "/user";
        const auto system = sandbox.path() + "/system";
        entry(system + "/hidden.desktop", "Name=Hidden system app\n");
        entry(user + "/hidden.desktop", "Hidden=true\n");
        entry(system + "/override.desktop", "Name=Old name\n");
        entry(user + "/override.desktop", "Name=User override\n");
        entry(system + "/browser.desktop", "Name=Browser\nName[pl]=Przegladarka\nGenericName=Web browser\n"
              "Comment=Browse the web\nCategories=Network;WebBrowser;\nKeywords=Internet;\nKeywords[pl]=Internet;WWW;\nOnlyShowIn=KDE;\n");
        entry(system + "/tools/editor.desktop", "Name=Editor\nName[pl_PL]=Edytor\nCategories=Utility;TextEditor;\n"
              "Keywords=Write\\;edit;Notes;\n[Desktop Action Extra]\nName=Wrong action name\n");
        entry(system + "/nodisplay.desktop", "Name=No display\nNoDisplay=true\n");
        entry(system + "/gnome.desktop", "Name=GNOME only\nOnlyShowIn=GNOME;\n");
        entry(system + "/notkde.desktop", "Name=Not KDE\nNotShowIn=KDE;\n");
        entry(system + "/missing.desktop", "Name=Missing executable\nTryExec=plasmid-fixture-command-not-installed\n");
        entry(system + "/link.desktop", "Name=Link\nType=Link\n");
        entry(system + "/unnamed.desktop", "Comment=No name\n");
        entry(system + "/noexec.desktop", "Name=No executable\nExec=\n");
        entry(system + "/dbus.desktop", "Name=D-Bus only\nExec=\nDBusActivatable=true\n");

        ApplicationsModel model(nullptr, {user, system}, "pl_PL.UTF-8@variant", {"KDE"});
        QSignalSpy scans(&model, &ApplicationsModel::refreshed);
        model.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 5000);
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(rowFor(model, "hidden.desktop"), -1);
        const auto browser = model.index(rowFor(model, "browser.desktop"), 0);
        QCOMPARE(browser.data(ApplicationsModel::NameRole).toString(), QString("Przegladarka"));
        QCOMPARE(browser.data(ApplicationsModel::CategoriesRole).toStringList(), (QStringList{"Network", "WebBrowser"}));
        QCOMPARE(browser.data(ApplicationsModel::IconRole).toString(), QString("application-x-executable"));
        const auto editor = model.index(rowFor(model, "tools-editor.desktop"), 0);
        QCOMPARE(editor.data(ApplicationsModel::NameRole).toString(), QString("Edytor"));
        QCOMPARE(editor.data(ApplicationsModel::KeywordsRole).toStringList(), (QStringList{"Write;edit", "Notes"}));
        QCOMPARE(model.validatedEntry("override.desktop")->name, QString("User override"));
        QVERIFY(!model.validatedEntry("../../browser.desktop"));

        ApplicationsFilterModel filtered(&model);
        filtered.setCategory("Network");
        QCOMPARE(filtered.rowCount(), 1);
        QCOMPARE(filtered.desktopIdAt(0), QString("browser.desktop"));
        filtered.setQuery("  wWw  ");
        QCOMPARE(filtered.rowCount(), 1);
        filtered.setQuery("notes");
        QCOMPARE(filtered.rowCount(), 0);
        filtered.setCategory("");
        QCOMPARE(filtered.rowCount(), 1);
        QCOMPARE(filtered.desktopIdAt(0), QString("tools-editor.desktop"));
        filtered.setQuery("browse the WEB");
        QCOMPARE(filtered.rowCount(), 1);
        filtered.setQuery("");
        filtered.setCategory("Net"); // Categories are exact IDs, not text matches.
        QCOMPARE(filtered.rowCount(), 0);
        filtered.setCategory("");
        QCOMPARE(filtered.rowCount(), 4);

        QVERIFY(QFile::remove(user + "/hidden.desktop"));
        QVERIFY(QFile::remove(system + "/tools/editor.desktop"));
        model.refresh();
        model.refresh(); // Coalesce requests during a scan.
        QTRY_VERIFY_WITH_TIMEOUT(!model.scanning(), 5000);
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(rowFor(model, "hidden.desktop") >= 0);
        QCOMPARE(rowFor(model, "tools-editor.desktop"), -1);
        const auto idleScans = scans.count();
        QTest::qWait(250);
        QCOMPARE(scans.count(), idleScans);
    }

    void browserLaunchesExactDesktopFileAfterHide()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        const auto desktop = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/tools/Nested App.desktop";
        entry(desktop, "Name=Nested fixture\nCategories=Utility;\n");
        QVERIFY(QDir().mkdir(sandbox.path() + "/Downloads"));
        Launcher backend(nullptr, sandbox.path() + "/Downloads", sandbox.path() + "/cache.json");
        backend.applications()->refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.applications()->scanning(), 5000);
        QVERIFY(backend.applications()->validatedEntry("tools-Nested App.desktop"));
        QQuickWindow window;
        backend.setWindow(&window);
        QSignalSpy commands(&backend, &Launcher::externalCommandRequested);
        QSignalSpy errors(&backend, &Launcher::launchError);
        window.show();
        QVERIFY(backend.launchApplication("tools-Nested App.desktop"));
        QTest::qWait(30);
        QCOMPARE(commands.count(), 0);
        window.hide();
        QTRY_COMPARE(commands.count(), 1);
        auto command = commands.takeFirst();
        QCOMPARE(command[0].toString(), QString("kioclient"));
        QCOMPARE(command[1].toStringList(), (QStringList{"exec", QUrl::fromLocalFile(desktop).toString(QUrl::FullyEncoded)}));

        QVERIFY(!backend.launchApplication(desktop));
        entry(desktop, "Name=Hidden now\nHidden=true\n");
        QVERIFY(!backend.launchApplication("tools-Nested App.desktop"));
        entry(desktop, "Name=Nested fixture\n");
        window.show();
        QVERIFY(backend.launchApplication("tools-Nested App.desktop"));
        QVERIFY(QFile::remove(desktop));
        window.hide();
        QTRY_COMPARE(errors.count(), 3); // Removed between click and dispatch.
        QCOMPARE(commands.count(), 0);
        QTRY_VERIFY(!backend.applications()->scanning());
        QTRY_VERIFY(!backend.downloads()->scanning());
    }
};

int main(int argc, char **argv)
{
    LaunchFixture fixture;
    if (!fixture.setup()) return 1;
    QApplication app(argc, argv);
    ApplicationsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "applications_test.moc"
