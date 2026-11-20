// SPDX-License-Identifier: GPL-3.0-or-later

#include "groups.h"
#include "groupnotifications.h"
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class NotificationService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    uint lastId = 0;
    QStringList actions;
    QList<uint> closed;
public Q_SLOTS:
    uint Notify(const QString &, uint, const QString &, const QString &, const QString &,
                const QStringList &buttons, const QVariantMap &, int) {
        actions = buttons;
        return ++lastId;
    }
    void CloseNotification(uint id) { closed.append(id); Q_EMIT NotificationClosed(id, 3); }
Q_SIGNALS:
    void ActionInvoked(uint id, const QString &action);
    void NotificationClosed(uint id, uint reason);
};

class GroupsTest : public QObject
{
    Q_OBJECT
    static ApplicationEntry app(const QString &id, const QString &command = {}, const QStringList &categories = {"Game"}) {
        ApplicationEntry entry;
        entry.desktopId = id;
        entry.name = id;
        entry.icon = "application-x-executable";
        entry.command = command;
        entry.categories = categories;
        return entry;
    }
    static QList<ApplicationEntry> seeds() {
        return {app("steam.desktop"), app("ea.desktop", "wine \"/Games/EA App/EADesktop.exe\""),
                app("battle.desktop", "env WINEPREFIX=/prefix wine \"/Games/Battle.net/Battle.net.exe\""),
                app("ubisoft.desktop", "wine /Games/UbisoftConnect.exe")};
    }
    static QString seededGroup(Groups &groups) {
        const auto id = groups.create("Launchers");
        for (const auto &entry : seeds()) groups.add(id, Groups::applicationItem(entry));
        groups.observeApplications(seeds());
        return id;
    }
    static void writeDesktop(const QString &path, const QString &name) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=" + name.toUtf8() + "\nExec=/bin/true\nCategories=Game;\n");
    }
private Q_SLOTS:
    void durableGroups() {
        QTemporaryDir temp;
        const auto path = temp.filePath("groups.json");
        const QVariantList favorites{QVariantMap{{"desktopId", "steam.desktop"}, {"name", "Steam"}, {"icon", "steam"}}};
        Groups groups(path, favorites);
        QCOMPARE(groups.items().size(), 1);
        QVERIFY(!groups.rename("pinned", "Changed"));
        QVERIFY(!groups.remove("pinned"));
        const auto id = groups.create("  My files  ");
        QVERIFY(!id.isEmpty());
        QCOMPARE(groups.selectedId(), id);
        QCOMPARE(groups.items().size(), 0);
        QVERIFY(groups.rename(id, "Mods & archives"));
        const auto file = temp.filePath("A literal $(name).zip");
        QFile fixture(file);
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.close();
        const QVariantMap item{{"kind", "file"}, {"path", file}, {"name", "Archive"}, {"icon", "application-zip"}};
        QSignalSpy added(&groups, &Groups::itemAdded);
        QVERIFY(groups.add(id, item));
        QCOMPARE(added.count(), 1);
        QCOMPARE(added.first().first().toString(), QString("Archive added to Mods & archives group"));
        QVERIFY(groups.add(id, item));
        QCOMPARE(added.count(), 1); // Duplicate requests must not report a new add.
        QCOMPARE(groups.items().size(), 1);
        QVERIFY(!groups.add(id, {{"kind", "file"}, {"path", "relative"}}));
        QVERIFY(!groups.add(id, {{"kind", "app"}, {"desktopId", "../evil.desktop"}}));
        Groups restored(path, {});
        restored.select(id);
        QCOMPARE(restored.items(), groups.items());
        QVERIFY(QFile::remove(file)); // Missing bookmarks retain identity and remain removable.
        QVERIFY(restored.unpin(id, "file:" + file));
        QVERIFY(restored.items().isEmpty());
        QVERIFY(restored.remove(id));
        QCOMPARE(restored.selectedId(), QString("pinned"));
        QVERIFY(restored.unpin("pinned", "app:steam.desktop"));
        Groups emptyPinned(path, favorites);
        QVERIFY(emptyPinned.items().isEmpty()); // Migration must not repopulate an emptied group.
        QFile corrupt(temp.filePath("corrupt.json"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("{broken");
        corrupt.close();
        Groups invalid(corrupt.fileName(), {});
        QSignalSpy failedAdd(&invalid, &Groups::itemAdded);
        QVERIFY(!invalid.error().isEmpty());
        QVERIFY(invalid.create("Do not overwrite").isEmpty());
        QVERIFY(!invalid.add("pinned", item));
        QCOMPARE(failedAdd.count(), 0); // No success toast when persistence fails.
        QVERIFY(corrupt.open(QIODevice::ReadOnly));
        QCOMPARE(corrupt.readAll(), QByteArray("{broken"));
    }

    void offlineSuggestionsAndBell() {
        QTemporaryDir temp;
        const auto path = temp.filePath("groups.json");
        Groups groups(path, {});
        const auto id = seededGroup(groups);
        QSignalSpy suggestions(&groups, &Groups::suggestionReady);
        auto entries = seeds();
        entries.append(app("com.heroicgameslauncher.hgl.desktop"));
        entries.append(app("steam_app_123.desktop", "steam steam://rungameid/123"));
        entries.append(app("unrelated-game.desktop"));
        groups.observeApplications(entries);
        QCOMPARE(suggestions.count(), 1);
        QCOMPARE(groups.suggestions().size(), 1);
        const auto proposal = groups.suggestions().first().toMap();
        QCOMPARE(proposal.value("reason").toString(), QString("LauncherStore"));
        QVERIFY(!groups.contains(id, "app:com.heroicgameslauncher.hgl.desktop"));
        groups.respond(proposal.value("id").toString(), "later");
        Groups restored(path, {});
        QSignalSpy repeat(&restored, &Groups::suggestionReady);
        restored.observeApplications(entries);
        QCOMPARE(repeat.count(), 0);
        QCOMPARE(restored.suggestions().size(), 1);
        restored.respond(proposal.value("id").toString(), "yes");
        QVERIFY(restored.contains(id, "app:com.heroicgameslauncher.hgl.desktop"));
        QVERIFY(restored.suggestions().isEmpty());
        entries.append(app("galaxy.desktop", "wine /Games/GalaxyClient.exe"));
        restored.observeApplications(entries);
        QCOMPARE(repeat.count(), 1);
        restored.respond(restored.suggestions().first().toMap().value("id").toString(), "never");
        QVERIFY(!restored.notificationsEnabled());
        Groups disabled(path, {});
        QVERIFY(!disabled.notificationsEnabled());
        QSignalSpy quiet(&disabled, &Groups::suggestionReady);
        entries.append(app("another.desktop", {}, {"Game", "LauncherStore"}));
        disabled.observeApplications(entries);
        QCOMPARE(quiet.count(), 0);
        disabled.setNotificationsEnabled(true);
        entries.append(app("off-period.desktop", {}, {"Game", "LauncherStore"}));
        disabled.observeApplications(entries); // Re-enable establishes a baseline, no backlog.
        QCOMPARE(quiet.count(), 0);
        entries.append(app("new-after-enable.desktop", {}, {"Game", "LauncherStore"}));
        disabled.observeApplications(entries);
        QCOMPARE(quiet.count(), 1);
        QVERIFY(disabled.remove(id));
        QVERIFY(disabled.suggestions().isEmpty());
    }

    void conservativeIntentAndRemovals() {
        QTemporaryDir temp;
        Groups groups(temp.filePath("groups.json"), {});
        const auto id = groups.create("Launchers");
        auto first = app("game-a.desktop");
        auto second = app("game-b.desktop");
        groups.add(id, Groups::applicationItem(first));
        groups.add(id, Groups::applicationItem(second));
        groups.observeApplications({first, second});
        QSignalSpy proposals(&groups, &Groups::suggestionReady);
        groups.observeApplications({first, second, app("heroic.desktop")});
        QCOMPARE(proposals.count(), 0); // A group name plus broad Game metadata proves nothing.
        auto opaque = app("opaque-wine-shortcut.desktop", "lutris lutris:rungameid/1");
        opaque.name = "EA App";
        QVERIFY(Groups::rolesFor(opaque).contains("LauncherStore"));
        opaque.name = "My EA App mods";
        QVERIFY(Groups::rolesFor(opaque).isEmpty());
        Groups known(temp.filePath("known.json"), {});
        const auto launchers = seededGroup(known);
        auto entries = seeds();
        entries.append(app("heroic.desktop"));
        known.observeApplications(entries);
        const auto pendingId = known.suggestions().first().toMap().value("id").toString();
        known.observeApplications(seeds());
        QVERIFY(known.suggestions().isEmpty());
        known.respond(pendingId, "yes");
        QVERIFY(!known.contains(launchers, "app:heroic.desktop"));
    }

    void eventDrivenCatalog() {
        QTemporaryDir temp;
        const auto root = temp.filePath("applications");
        QVERIFY(QDir().mkpath(root));
        writeDesktop(root + "/old.desktop", "Old app");
        ApplicationsModel catalog(nullptr, {root}, "en_US", {"KDE"});
        QSignalSpy refreshes(&catalog, &ApplicationsModel::refreshed);
        catalog.setWatching(true);
        QTRY_COMPARE(catalog.rowCount(), 1);
        writeDesktop(root + "/new.desktop", "New app");
        QTRY_COMPARE(catalog.rowCount(), 2);
        const auto settled = refreshes.count();
        QTest::qWait(700);
        QCOMPARE(refreshes.count(), settled); // Idle means no repeated scans.
        QVERIFY(QFile::remove(root + "/old.desktop"));
        QTRY_COMPARE(catalog.rowCount(), 1);
        catalog.setWatching(false);
        QTest::qWait(500);
        const auto paused = refreshes.count();
        writeDesktop(root + "/quiet.desktop", "While off");
        QTest::qWait(700);
        QCOMPARE(refreshes.count(), paused);
        QCOMPARE(catalog.rowCount(), 1);
        catalog.refresh(); // Browsing can refresh the catalog independently of suggestions.
        QTRY_COMPARE(catalog.rowCount(), 2);
        catalog.setWatching(true);
        QVERIFY(QDir(root).removeRecursively());
        QTRY_COMPARE(catalog.rowCount(), 0);
        QVERIFY(QDir().mkpath(root));
        writeDesktop(root + "/recreated.desktop", "Recreated");
        QTRY_COMPARE(catalog.rowCount(), 1);
    }

    void nativeActionsOnPrivateBus() {
        auto bus = QDBusConnection::sessionBus();
        NotificationService service;
        QVERIFY(bus.registerService("org.freedesktop.Notifications"));
        QVERIFY(bus.registerObject("/org/freedesktop/Notifications", &service,
                                  QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QTemporaryDir temp;
        Groups groups(temp.filePath("groups.json"), {});
        const auto id = seededGroup(groups);
        GroupNotifications bridge(&groups, bus);
        auto entries = seeds();
        entries.append(app("heroic.desktop"));
        groups.observeApplications(entries);
        QTRY_COMPARE(service.lastId, uint(1));
        QCOMPARE(service.actions, (QStringList{"yes", "Yes", "later", "Later", "never", "Never"}));
        QTest::qWait(30); // Allow the asynchronous Notify reply to map its action target.
        Q_EMIT service.ActionInvoked(1, "yes");
        QTRY_VERIFY(groups.contains(id, "app:heroic.desktop"));
        QVERIFY(groups.suggestions().isEmpty());
        entries.append(app("galaxy.desktop", "wine /Games/GalaxyClient.exe"));
        groups.observeApplications(entries);
        QTRY_COMPARE(service.lastId, uint(2));
        QTest::qWait(30);
        Q_EMIT service.ActionInvoked(2, "later");
        QTRY_VERIFY(groups.suggestions().first().toMap().value("deferred").toBool());
        QVERIFY(groups.notificationsEnabled());
        entries.append(app("new-launcher.desktop", {}, {"Game", "LauncherStore"}));
        groups.observeApplications(entries);
        QTRY_COMPARE(service.lastId, uint(3));
        QTest::qWait(30);
        Q_EMIT service.ActionInvoked(3, "never");
        QTRY_VERIFY(!groups.notificationsEnabled());
        QTRY_VERIFY(service.closed.contains(3));
        bus.unregisterObject("/org/freedesktop/Notifications");
        bus.unregisterService("org.freedesktop.Notifications");
    }
};

QTEST_GUILESS_MAIN(GroupsTest)
#include "groups_test.moc"
