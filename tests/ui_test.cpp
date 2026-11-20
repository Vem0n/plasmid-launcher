// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "qmlruntime.h"
#include "translations.h"
#include <QScopeGuard>

#include <QAbstractItemModel>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QUrl>

class DropdownTest : public QObject
{
    Q_OBJECT
    static QQuickItem *findVisualItem(QQuickItem *root, const QString &name)
    {
        if (root->objectName() == name)
            return root;
        for (auto *child : root->childItems()) {
            if (auto *item = findVisualItem(child, name))
                return item;
        }
        return nullptr;
    }
private Q_SLOTS:
    void inputAndDismissal()
    {
        qApp->setQuitOnLastWindowClosed(false);
        qApp->setQuitLockEnabled(false);
        QTest::failOnWarning(QRegularExpression("QObject::installEventFilter.*"));
        preloadQmlTranslationRuntime();
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        QVERIFY(QDir().mkdir(sandbox.path() + "/Downloads"));
        QFile fixture(sandbox.path() + "/Downloads/Example archive.zip");
        QVERIFY(fixture.open(QIODevice::WriteOnly));
        fixture.close();
        QFile other(sandbox.path() + "/Downloads/Other notes.txt");
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();
        Launcher backend(nullptr, sandbox.path() + "/Downloads", sandbox.path() + "/cache.json");
        QSignalSpy commands(&backend, &Launcher::externalCommandRequested);
        QSignalSpy searchCommands(&backend, &Launcher::searchExecutionRequested);
        QQmlApplicationEngine engine;
        QStringList qmlErrors;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                qmlErrors.append(error.toString());
        });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        backend.setWindow(window);
        auto *search = window->findChild<QQuickItem *>("searchField");
        auto *searchLoader = window->findChild<QObject *>("searchLoader");
        auto *panel = window->findChild<QQuickItem *>("launcherPanel");
        auto *content = window->findChild<QQuickItem *>("panelContent");
        auto *skeleton = window->findChild<QQuickItem *>("resizeSkeleton");
        QVERIFY(search);
        QVERIFY(searchLoader);
        QVERIFY(panel && content && skeleton);
        QVERIFY(!searchLoader->property("active").toBool());
        QVERIFY(!backend.shortcutEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(backend.downloads()->count(), 2, 5000);

        backend.Show();
        QTRY_VERIFY(window->isVisible());
        QTRY_VERIFY(search->hasActiveFocus());
        const auto initialHeight = window->height();
        auto *expand = window->findChild<QQuickItem *>("expandDownloads");
        auto *back = window->findChild<QQuickItem *>("downloadsBack");
        QVERIFY(expand);
        QVERIFY(back);
        auto *files = window->findChild<QQuickItem *>("downloadsList");
        auto *downloadsSearch = window->findChild<QQuickItem *>("downloadsSearchField");
        QVERIFY(files);
        QVERIFY(downloadsSearch);
        auto *notificationStatus = window->findChild<QQuickItem *>("notificationStatus");
        QVERIFY(notificationStatus);
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        auto *groupTabs = findVisualItem(window->contentItem(), "groupTabs");
        auto *firstGroups = findVisualItem(window->contentItem(), "groupsFirst");
        auto *lastGroups = findVisualItem(window->contentItem(), "groupsLast");
        auto *positionBar = findVisualItem(window->contentItem(), "groupPositionBar");
        auto *createGroup = findVisualItem(window->contentItem(), "createGroup");
        auto *pinnedTab = findVisualItem(window->contentItem(), "groupTab_pinned");
        QVERIFY(groupTabs && firstGroups && lastGroups && positionBar && createGroup && pinnedTab);
        QVERIFY(!groupTabs->property("overflowing").toBool());
        QVERIFY(!firstGroups->isVisible() && !lastGroups->isVisible() && !positionBar->isVisible());
        QCOMPARE(createGroup->parentItem(), pinnedTab->parentItem());
        QCOMPARE(createGroup->x(), pinnedTab->x() + pinnedTab->width() + 3);
        const auto click = [window](QQuickItem *item) {
            QTRY_VERIFY(item->isVisible() && item->isEnabled());
            QSignalSpy painted(window, &QQuickWindow::frameSwapped);
            window->update();
            QTRY_VERIFY(painted.count() > 0);
            QTRY_VERIFY(QRectF(0, 0, window->width(), window->height()).contains(
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2))));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        // Clicking real delegates must hide the surface before requesting a
        // launch. No external-process connection is installed in this test.
        QStringList tiles;
        for (const auto &place : backend.places())
            tiles.append("place_" + place.toMap().value("id").toString());
        QVERIFY(!backend.favorites().isEmpty());
        tiles.append("favorite_" + backend.favorites().first().toMap().value("desktopId").toString());
        for (const auto &name : std::as_const(tiles)) {
            auto *tile = findVisualItem(window->contentItem(), name);
            QVERIFY2(tile, qPrintable(name));
            QVERIFY(tile->isEnabled());
            QVERIFY(window->grabWindow().save("main-groups.png"));
            click(tile);
            QTRY_VERIFY2(!window->isVisible(), qPrintable(name));
            QTRY_COMPARE(commands.count(), 1);
            const auto command = commands.takeFirst();
            QCOMPARE(command[0].toString(), name.startsWith("favorite_") ? QString("kstart") : QString("dolphin"));
            backend.Show();
            QTRY_VERIFY(search->hasActiveFocus());
        }

        auto *browse = window->findChild<QQuickItem *>("browseApplications");
        auto *appsView = window->findChild<QQuickItem *>("applicationsView");
        auto *apps = window->findChild<QQuickItem *>("applicationsGrid");
        auto *appsSearch = window->findChild<QQuickItem *>("applicationsSearchField");
        auto *listMode = findVisualItem(window->contentItem(), "applicationsListMode");
        auto *galleryMode = findVisualItem(window->contentItem(), "applicationsGalleryMode");
        QVERIFY(browse && appsView && apps && appsSearch && listMode && galleryMode);
        const auto initialWidth = window->width();
        QSignalSpy browserWidths(window, &QWindow::widthChanged);
        QSignalSpy browserHeights(window, &QWindow::heightChanged);
        click(browse);
        QVERIFY(skeleton->isVisible());
        QCOMPARE(skeleton->opacity(), 1.0);
        QCOMPARE(content->opacity(), 0.0);
        QVERIFY(!searchLoader->property("active").toBool());
        QTRY_VERIFY(window->width() > initialWidth + 50);
        QTRY_VERIFY(window->height() > initialHeight + 200);
        QTest::qWait(35);
        const auto gridSize = apps->size();
        QSignalSpy gridWidths(apps, &QQuickItem::widthChanged);
        QSignalSpy gridHeights(apps, &QQuickItem::heightChanged);
        QTest::qWait(250);
        QCOMPARE(apps->size(), gridSize);
        QCOMPARE(gridWidths.count(), 0);
        QCOMPARE(gridHeights.count(), 0);
        QCOMPARE(browserWidths.count(), 1);
        QCOMPARE(browserHeights.count(), 1);
        QTRY_VERIFY(appsSearch->hasActiveFocus());
        QTRY_VERIFY(!skeleton->isVisible());
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        QTRY_VERIFY_WITH_TIMEOUT(backend.filteredApplications()->rowCount() > 0, 5000);
        QVERIFY(!appsView->property("listMode").toBool());
        click(galleryMode); // Clicking the active mode must leave it selected.
        QVERIFY(galleryMode->property("checked").toBool());
        QVERIFY(!listMode->property("checked").toBool());
        QVERIFY(apps->property("cellWidth").toReal() < apps->width() / 2);
        QVERIFY(window->grabWindow().save("applications-gallery.png"));
        apps->setProperty("currentIndex", 1);
        const auto selectedId = backend.filteredApplications()->desktopIdAt(1);
        click(listMode);
        QTRY_VERIFY(appsView->property("listMode").toBool());
        QVERIFY(listMode->property("checked").toBool());
        QVERIFY(!galleryMode->property("checked").toBool());
        QTRY_COMPARE(apps->property("cellWidth").toReal(), apps->width());
        QCOMPARE(backend.filteredApplications()->desktopIdAt(apps->property("currentIndex").toInt()), selectedId);
        QVERIFY(window->grabWindow().save("applications-list.png"));
        click(galleryMode);
        QTRY_VERIFY(!appsView->property("listMode").toBool());
        QCOMPARE(backend.filteredApplications()->desktopIdAt(apps->property("currentIndex").toInt()), selectedId);
        browserWidths.clear();
        browserHeights.clear();
        click(back);
        QTest::qWait(35);
        QVERIFY(panel->property("returningToMain").toBool());
        QCOMPARE(panel->property("page").toString(), QString("applications"));
        QCOMPARE(apps->size(), gridSize); // Outgoing content does not reflow during collapse.
        QCOMPARE(skeleton->property("destination").toString(), QString("home"));
        QCOMPARE(skeleton->opacity(), 1.0);
        QCOMPARE(content->opacity(), 0.0); // The outgoing gallery is fully obscured.
        QVERIFY(window->grabWindow().save("return-skeleton.png"));
        QTRY_COMPARE(window->width(), initialWidth);
        QTRY_COMPARE(window->height(), initialHeight);
        QTRY_COMPARE(panel->property("page").toString(), QString("home"));
        QCOMPARE(browserWidths.count(), 1);
        QCOMPARE(browserHeights.count(), 1);
        QTRY_VERIFY(!panel->property("transitioning").toBool());
        QVERIFY(!skeleton->isVisible());
        QCOMPARE(content->opacity(), 1.0);

        click(browse);
        QTRY_VERIFY(appsSearch->hasActiveFocus());
        for (const char character : QByteArray("__plasmid_missing__")) QTest::keyClick(window, character);
        QTRY_COMPARE(backend.filteredApplications()->rowCount(), 0);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(18, 100));
        QTRY_VERIFY(!appsSearch->hasActiveFocus());
        QCOMPARE(appsSearch->property("text").toString(), QString("__plasmid_missing__"));
        QVERIFY(window->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(appsSearch->property("text").toString(), QString());
        QVERIFY(window->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        QTRY_COMPARE(window->width(), initialWidth);
        QTRY_COMPARE(window->height(), initialHeight);
        click(browse);
        QTRY_VERIFY(appsSearch->hasActiveFocus());
        QTest::qWait(280);
        apps->setProperty("currentIndex", 0);
        QTRY_VERIFY(apps->property("currentItem").value<QQuickItem *>());
        auto *appCell = apps->property("currentItem").value<QQuickItem *>();
        const auto launchId = appCell->property("desktopId").toString();
        const auto launchEntry = backend.applications()->validatedEntry(launchId);
        QVERIFY(launchEntry);
        auto *appCard = findVisualItem(appCell, "application_" + launchId);
        QVERIFY(appCard);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                         appCard->mapToScene(QPointF(20, 20)).toPoint());
        auto *appMenu = window->findChild<QObject *>("applicationContextMenu");
        auto *appReveal = window->findChild<QQuickItem *>("revealApplication");
        QVERIFY(appMenu && appReveal);
        QTRY_VERIFY(appMenu->property("visible").toBool());
        QTRY_VERIFY(!appMenu->property("busy").toBool());
        const auto appGroupId = backend.groups()->create("Application selection test");
        QVERIFY(!appGroupId.isEmpty());
        auto *addAppToGroup = appMenu->findChild<QQuickItem *>("addToGroup");
        auto *appGroupMenu = appMenu->findChild<QObject *>("groupTargetsMenu");
        QVERIFY(addAppToGroup && appGroupMenu);
        auto *pinRow = appMenu->findChild<QQuickItem *>("pinContextItem");
        QVERIFY(pinRow);
        auto *pinLabel = pinRow->property("contentItem").value<QQuickItem *>();
        auto *groupLabel = addAppToGroup->property("contentItem").value<QQuickItem *>();
        QVERIFY(pinLabel && groupLabel);
        QCOMPARE(groupLabel->mapToScene(QPointF(groupLabel->property("leftPadding").toReal(), 0)).x(),
                 pinLabel->mapToScene(QPointF(pinLabel->property("leftPadding").toReal(), 0)).x());
        click(addAppToGroup);
        QTRY_VERIFY(appGroupMenu->property("visible").toBool());
        QVERIFY(appGroupMenu->property("width").toReal() >= 200);
        auto *appGroupTarget = findVisualItem(window->contentItem(), "groupTarget_" + appGroupId);
        QVERIFY(appGroupTarget);
        QVERIFY(appGroupTarget->width() > 0);
        QCOMPARE(appMenu->property("pinItem").toMap().value("key").toString(), "app:" + launchId);
        QSignalSpy groupTriggered(appGroupTarget, SIGNAL(triggered()));
        QVERIFY(window->grabWindow().save("application-group-choice.png"));
        click(appGroupTarget);
        QTRY_COMPARE(groupTriggered.count(), 1);
        QTRY_VERIFY(backend.groups()->contains(appGroupId, "app:" + launchId));
        QVERIFY(window->isVisible());
        auto *toast = window->findChild<QQuickItem *>("groupActionToast");
        QVERIFY(toast);
        QTRY_VERIFY(toast->isVisible());
        QCOMPARE(toast->property("message").toString(), launchEntry->name + " added to Application selection test group");
        QVERIFY(!toast->hasActiveFocus());
        QTRY_COMPARE(toast->opacity(), 1.0);
        QVERIFY(window->grabWindow().save("group-added-toast.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!toast->property("showing").toBool(), 2500);
        QTRY_VERIFY(!toast->isVisible());
        QVERIFY(window->isVisible());
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                         appCard->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(appMenu->property("visible").toBool());
        QTRY_VERIFY(!appMenu->property("busy").toBool());
        click(addAppToGroup);
        QTRY_VERIFY(appGroupMenu->property("visible").toBool());
        auto *memberRow = findVisualItem(window->contentItem(), "groupTarget_" + appGroupId);
        auto *memberCheck = findVisualItem(window->contentItem(), "groupCheck_" + appGroupId);
        QVERIFY(memberRow && memberCheck);
        QVERIFY(memberRow->property("alreadyMember").toBool());
        QVERIFY(!memberRow->isEnabled());
        QVERIFY(memberCheck->isVisible());
        QVERIFY(window->grabWindow().save("group-membership-check.png"));
        QVERIFY(QMetaObject::invokeMethod(appGroupMenu, "close"));
        QTRY_VERIFY(!appGroupMenu->property("visible").toBool());
        const auto appPath = appMenu->property("targetPath").toString();
        QVERIFY(!appPath.isEmpty());
        QVERIFY(window->isVisible());
        QVERIFY(window->grabWindow().save("applications-file-context.png"));
        click(appReveal);
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(commands.count(), 1);
        auto revealCommand = commands.takeFirst();
        QCOMPARE(revealCommand[0].toString(), QString("dolphin"));
        QCOMPARE(revealCommand[1].toStringList().last(), QUrl::fromLocalFile(appPath).toString(QUrl::FullyEncoded));
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        click(browse);
        QTRY_VERIFY(appsSearch->hasActiveFocus());
        QTest::qWait(280);
        click(listMode);
        apps->setProperty("currentIndex", 0);
        QTRY_VERIFY(apps->property("currentItem").value<QQuickItem *>());
        appCell = apps->property("currentItem").value<QQuickItem *>();
        appCard = findVisualItem(appCell, "application_" + appCell->property("desktopId").toString());
        QVERIFY(appCard);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, appCard->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(appMenu->property("visible").toBool());
        QTRY_VERIFY(!appMenu->property("busy").toBool());
        const auto listGroupId = backend.groups()->create("List selection test");
        click(addAppToGroup);
        QTRY_VERIFY(appGroupMenu->property("visible").toBool());
        auto *listGroupTarget = findVisualItem(window->contentItem(), "groupTarget_" + listGroupId);
        QVERIFY(listGroupTarget);
        click(listGroupTarget);
        QTRY_VERIFY(backend.groups()->contains(listGroupId, "app:" + launchId));
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, appCard->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(appMenu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(appMenu, "close"));
        QTRY_VERIFY(!appMenu->property("visible").toBool());
        click(appCard);
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(commands.count(), 1);
        auto appCommand = commands.takeFirst();
        QCOMPARE(appCommand[0].toString(), QString("kioclient"));
        QCOMPARE(appCommand[1].toStringList(), (QStringList{"exec", QUrl::fromLocalFile(launchEntry->path).toString(QUrl::FullyEncoded)}));
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        QTRY_COMPARE(window->width(), initialWidth);
        QTRY_COMPARE(window->height(), initialHeight);

        // The instant reopen/reset must be presented before counting the next
        // transition, including the platform's queued compact-size notification.
        QSignalSpy presented(window, &QQuickWindow::frameSwapped);
        window->update();
        QTRY_VERIFY(presented.count() > 0);
        QSignalSpy nativeResizes(window, &QWindow::heightChanged);
        click(expand);
        QTRY_VERIFY(back->isVisible());
        QVERIFY(!searchLoader->property("active").toBool());
        QTRY_VERIFY(window->height() > initialHeight + 100);
        QTest::qWait(35);
        QCOMPARE(skeleton->opacity(), 1.0);
        QCOMPARE(content->opacity(), 0.0);
        const auto listHeight = files->height();
        const auto listY = files->y();
        QSignalSpy listResizes(files, &QQuickItem::heightChanged);
        QTest::qWait(250);
        QCOMPARE(files->height(), listHeight);
        QCOMPARE(files->y(), listY);
        QCOMPARE(listResizes.count(), 0);
        QCOMPARE(nativeResizes.count(), 1); // No native resize for each animation frame.
        QTRY_VERIFY(downloadsSearch->hasActiveFocus());
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        const auto expandedHeight = window->height();
        QVERIFY(window->grabWindow().save("downloads-expanded.png"));
        nativeResizes.clear();
        click(back);
        QTRY_VERIFY(!back->isVisible());
        QTRY_COMPARE(window->height(), initialHeight);
        QCOMPARE(nativeResizes.count(), 1);
        QVERIFY(expandedHeight > initialHeight);
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(window->grabWindow().save("downloads-collapsed.png"));
        click(expand);
        QTest::qWait(280);
        auto *menu = window->findChild<QObject *>("downloadContextMenu");
        auto *reveal = window->findChild<QQuickItem *>("revealDownload");
        QVERIFY(files);
        QVERIFY(menu);
        QVERIFY(reveal);
        QTRY_VERIFY(downloadsSearch->hasActiveFocus());
        for (const char character : QByteArray("missing")) QTest::keyClick(window, character);
        QTRY_COMPARE(backend.filteredDownloads()->count(), 0);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(18, 100));
        QTRY_VERIFY(!downloadsSearch->hasActiveFocus());
        QCOMPARE(downloadsSearch->property("text").toString(), QString("missing"));
        QVERIFY(window->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(downloadsSearch->property("text").toString(), QString());
        QVERIFY(window->isVisible());
        QTRY_COMPARE(backend.filteredDownloads()->count(), 2);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        click(expand);
        QTest::qWait(280);
        // Filtered row zero must reveal the filtered path, not source row zero.
        QTRY_VERIFY(downloadsSearch->hasActiveFocus());
        for (const char character : QByteArray("Other")) QTest::keyClick(window, character);
        QTRY_COMPARE(backend.filteredDownloads()->count(), 1);
        files->setProperty("currentIndex", 0);
        QTRY_VERIFY(files->property("currentItem").value<QQuickItem *>());
        auto *fileRow = files->property("currentItem").value<QQuickItem *>();
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                         fileRow->mapToScene(QPointF(120, 30)).toPoint());
        QTRY_VERIFY(menu->property("visible").toBool());
        QTest::qWait(180);
        QVERIFY(window->isVisible()); // Item popup must not trigger focus-loss dismissal.
        QVERIFY(window->grabWindow().save("downloads-context.png"));
        click(reveal);
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(commands.count(), 1);
        QCOMPARE(commands.first()[0].toString(), QString("dolphin"));
        QCOMPARE(commands.first()[1].toStringList().first(), QString("--select"));
        QVERIFY(commands.first()[1].toStringList().last().endsWith("Other%20notes.txt"));
        commands.clear();
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        const auto searchGroupId = backend.groups()->create("Search selection test");
        search->setProperty("text", "focus test");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(18, 100));
        QTRY_VERIFY(!search->hasActiveFocus());
        QCOMPARE(search->property("text").toString(), QString("focus test"));
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(search->property("text").toString(), QString());
        QVERIFY(window->isVisible());
        click(search);
        QTRY_VERIFY(search->hasActiveFocus());
        search->setProperty("text", launchEntry->name);
        QTRY_VERIFY(searchLoader->property("item").value<QObject *>());
        auto *appResults = searchLoader->property("item").value<QObject *>();
        auto *searchAppsModel = appResults->property("model").value<QAbstractItemModel *>();
        QVERIFY(searchAppsModel);
        const auto findAppRow = [&] {
            for (int row = 0; row < searchAppsModel->rowCount(); ++row)
                if (backend.searchResultItem(searchAppsModel, row).value("desktopId").toString() == launchId) return row;
            return -1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(findAppRow() >= 0, 12000);
        appResults->setProperty("currentIndex", findAppRow());
        QTRY_VERIFY(appResults->property("currentItem").value<QQuickItem *>());
        auto *searchAppRow = appResults->property("currentItem").value<QQuickItem *>();
        // Open near the right edge: the contained submenu must flip inside
        // the compact native window and accept a real pointer selection.
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                         searchAppRow->mapToScene(QPointF(searchAppRow->width() - 20, 20)).toPoint());
        auto *searchAppMenu = window->findChild<QObject *>("searchFileContextMenu");
        QVERIFY(searchAppMenu);
        QTRY_VERIFY(searchAppMenu->property("visible").toBool());
        QCOMPARE(searchAppMenu->property("pinItem").toMap().value("desktopId").toString(), launchId);
        auto *searchAddToGroup = searchAppMenu->findChild<QQuickItem *>("addToGroup");
        auto *searchGroupMenu = searchAppMenu->findChild<QObject *>("groupTargetsMenu");
        QVERIFY(searchAddToGroup && searchGroupMenu);
        click(searchAddToGroup);
        QTRY_VERIFY(searchGroupMenu->property("visible").toBool());
        auto *searchGroupTarget = findVisualItem(window->contentItem(), "groupTarget_" + searchGroupId);
        QVERIFY(searchGroupTarget && searchGroupTarget->width() > 0);
        QVERIFY(window->grabWindow().save("search-group-choice.png"));
        click(searchGroupTarget);
        QTRY_VERIFY(backend.groups()->contains(searchGroupId, "app:" + launchId));
        QVERIFY(window->isVisible());
        const auto searchFile = sandbox.path() + "/Downloads/Example archive.zip";
        search->setProperty("text", searchFile);
        QTRY_VERIFY(searchLoader->property("item").value<QObject *>());
        auto *fileResults = searchLoader->property("item").value<QObject *>();
        auto *fileModel = fileResults->property("model").value<QAbstractItemModel *>();
        QVERIFY(fileModel);
        const auto findFileRow = [&] {
            for (int row = 0; row < fileModel->rowCount(); ++row)
                if (backend.searchResultPath(fileModel, row) == searchFile) return row;
            return -1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(findFileRow() >= 0, 12000);
        fileResults->setProperty("currentIndex", findFileRow());
        QTRY_VERIFY(fileResults->property("currentItem").value<QQuickItem *>());
        auto *searchFileRow = fileResults->property("currentItem").value<QQuickItem *>();
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, searchFileRow->mapToScene(QPointF(80, 20)).toPoint());
        auto *searchFileMenu = window->findChild<QObject *>("searchFileContextMenu");
        auto *searchFileReveal = window->findChild<QQuickItem *>("revealSearchFile");
        QVERIFY(searchFileMenu && searchFileReveal);
        QTRY_VERIFY(searchFileMenu->property("visible").toBool());
        QCOMPARE(searchFileMenu->property("targetPath").toString(), searchFile);
        QVERIFY(window->isVisible());
        QVERIFY(window->grabWindow().save("search-file-context.png"));
        // Incoming results must not retarget an already-open context menu.
        search->setProperty("text", "2+2");
        QTRY_VERIFY_WITH_TIMEOUT(findFileRow() < 0, 12000);
        QVERIFY(searchFileMenu->property("visible").toBool());
        QCOMPARE(searchFileMenu->property("targetPath").toString(), searchFile);
        click(searchFileReveal);
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(commands.count(), 1);
        revealCommand = commands.takeFirst();
        QCOMPARE(revealCommand[0].toString(), QString("dolphin"));
        QCOMPARE(revealCommand[1].toStringList(), (QStringList{"--select", QUrl::fromLocalFile(searchFile).toString(QUrl::FullyEncoded)}));
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_2);
        QTest::keyClick(window, Qt::Key_Plus);
        QTest::keyClick(window, Qt::Key_2);
        QCOMPARE(search->property("text").toString(), QString("2+2"));
        QTRY_VERIFY(searchLoader->property("item").value<QObject *>());
        auto *results = searchLoader->property("item").value<QObject *>();
        QTRY_VERIFY_WITH_TIMEOUT(results->property("count").toInt() > 0, 12000);
        auto *model = results->property("model").value<QAbstractItemModel *>();
        QVERIFY(model);
        const auto hasFour = [model] {
            for (int row = 0; row < model->rowCount(); ++row) {
                if (model->data(model->index(row, 0)).toString() == "4")
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasFour(), 12000);
        QVERIFY(window->grabWindow().save("main-search.png"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(searchCommands.count(), 1);
        QVERIFY(backend.searchExecuting());
        QVERIFY(searchLoader->property("active").toBool());
        QCOMPARE(searchLoader->property("item").value<QObject *>(), results);
        backend.finishSearchExecution(true); // Capture only; never execute the real runner.
        QTRY_VERIFY(!searchLoader->property("active").toBool());
        backend.Show();
        QTRY_VERIFY(search->hasActiveFocus());
        search->setProperty("text", "2+2");
        QTRY_VERIFY(searchLoader->property("item").value<QObject *>());
        results = searchLoader->property("item").value<QObject *>();
        QTRY_VERIFY_WITH_TIMEOUT(results->property("count").toInt() > 0, 12000);
        auto *resultView = qobject_cast<QQuickItem *>(results);
        QVERIFY(resultView);
        QTRY_VERIFY(results->property("currentItem").value<QQuickItem *>());
        click(results->property("currentItem").value<QQuickItem *>());
        QTRY_VERIFY(!window->isVisible());
        QTRY_COMPARE(searchCommands.count(), 2);
        backend.finishSearchExecution(false); // Preserve a runner-refined query.
        QTRY_VERIFY(window->isVisible());
        QCOMPARE(search->property("text").toString(), QString("2+2"));
        QTest::keyClick(window, Qt::Key_Down);
        QVERIFY(search->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(window->isVisible());
        QCOMPARE(search->property("text").toString(), QString());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        QCOMPARE(search->property("text").toString(), QString());
        QVERIFY(!searchLoader->property("active").toBool());
        QVERIFY(!searchLoader->property("item").value<QObject *>());

        backend.Toggle();
        QTRY_VERIFY(window->isVisible());
        QTRY_VERIFY(search->hasActiveFocus());
        click(browse);
        QVERIFY(panel->property("transitioning").toBool());
        QTest::keyClick(window, Qt::Key_Escape); // Escape works while controls are masked.
        backend.Show(); // Cancel dismissal before this resize/reveal can finish.
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(!panel->property("transitioning").toBool());
        QVERIFY(!skeleton->isVisible());
        QCOMPARE(content->opacity(), 1.0);
        QTest::qWait(400); // No stale deferred callback may restore the overlay.
        QVERIFY(search->hasActiveFocus());
        QVERIFY(!skeleton->isVisible());
        backend.Toggle();
        QTRY_VERIFY(!window->isVisible());
        backend.Show();
        backend.Hide();
        backend.Toggle(); // Reopen while the closing animation is still running.
        QTest::qWait(200);
        QVERIFY(window->isVisible());

        // Create a real empty tab through the editor, then fill a fixture group
        // to exercise height-only expansion and Pin in the shared context menu.
        auto *groupName = window->findChild<QQuickItem *>("groupNameField");
        auto *groupGallery = window->findChild<QQuickItem *>("groupGallery");
        auto *expandGroup = window->findChild<QQuickItem *>("expandGroup");
        auto *bell = window->findChild<QQuickItem *>("notificationBell");
        QVERIFY(createGroup && groupName && groupGallery && expandGroup && bell);
        // Overflow includes the plus. Arrows animate to each end, while direct
        // dragging still works and never clicks the tab under the drag.
        for (int i = 1; i <= 6; ++i) QVERIFY(!backend.groups()->create("Group " + QString::number(i)).isEmpty());
        backend.groups()->select("pinned");
        QTRY_VERIFY(groupTabs->property("overflowing").toBool());
        QTRY_VERIFY(groupTabs->property("contentX").toReal() < 1);
        QVERIFY(lastGroups->isVisible() && !firstGroups->isVisible());
        QVERIFY(positionBar->isVisible());
        QTRY_VERIFY(createGroup->mapToItem(groupTabs, QPointF()).x() > groupTabs->width());
        QVERIFY(window->grabWindow().save("groups-overflow-first.png"));
        click(lastGroups);
        QTest::qWait(90);
        QVERIFY(groupTabs->property("contentX").toReal() > 0);
        QVERIFY(groupTabs->property("contentX").toReal() < groupTabs->property("maximumX").toReal() - 1);
        QTRY_VERIFY(!lastGroups->isVisible());
        QVERIFY(firstGroups->isVisible());
        QVERIFY(createGroup->mapToItem(groupTabs, QPointF(createGroup->width(), 0)).x() <= groupTabs->width() + 1);
        QVERIFY(positionBar->property("position").toReal() > 0);
        QVERIFY(window->grabWindow().save("groups-overflow-last.png"));
        click(firstGroups);
        QTest::qWait(90);
        QVERIFY(groupTabs->property("contentX").toReal() > 0);
        QTRY_VERIFY(!firstGroups->isVisible());
        const auto dragStart = groupTabs->mapToScene(QPointF(groupTabs->width() * 0.7, 14)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
        for (int i = 1; i <= 6; ++i) {
            QTest::mouseMove(window, dragStart - QPoint(20 * i, 0));
            QTest::qWait(12);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragStart - QPoint(120, 0));
        QTRY_VERIFY(groupTabs->property("contentX").toReal() > 10);
        QCOMPARE(backend.groups()->selectedId(), QString("pinned"));
        QVERIFY(QMetaObject::invokeMethod(groupTabs, "cancelFlick"));
        click(lastGroups);
        QTRY_VERIFY(!lastGroups->isVisible());
        click(createGroup);
        QTRY_VERIFY(groupName->hasActiveFocus());
        groupName->setProperty("text", "Launchers");
        QTest::keyClick(window, Qt::Key_Return);
        const auto groupId = backend.groups()->selectedId();
        QVERIFY(groupId != "pinned");
        QCOMPARE(backend.groups()->groups().last().toMap().value("name").toString(), QString("Launchers"));
        QVERIFY(backend.groups()->items().isEmpty());
        QVERIFY(!expandGroup->isVisible());
        QTRY_VERIFY(createGroup->mapToItem(groupTabs, QPointF(createGroup->width(), 0)).x() <= groupTabs->width() + 1);
        const auto lastGroupTab = findVisualItem(window->contentItem(), "groupTab_" + groupId);
        QVERIFY(lastGroupTab);
        QCOMPARE(createGroup->x(), lastGroupTab->x() + lastGroupTab->width() + 3);
        QVERIFY(window->grabWindow().save("group-empty.png"));
        for (int i = 0; i < 25; ++i) {
            const auto path = sandbox.path() + "/Downloads/Group file " + QString::number(i) + ".zip";
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            QVERIFY(backend.groups()->add(groupId, backend.fileItem(path)));
        }
        QSignalSpy groupWidths(window, &QWindow::widthChanged);
        QSignalSpy groupHeights(window, &QWindow::heightChanged);
        auto *galleryFrame = window->findChild<QQuickItem *>("groupGalleryFrame");
        QVERIFY(galleryFrame);
        const auto collapsedReveal = galleryFrame->height();
        const auto headerPosition = groupTabs->mapToScene(QPointF());
        click(expandGroup);
        QTest::qWait(35);
        QVERIFY(panel->property("resizing").toBool());
        QVERIFY(!panel->property("transitioning").toBool());
        QVERIFY(!skeleton->isVisible());
        QCOMPARE(content->opacity(), 1.0);
        const auto groupViewport = groupGallery->size();
        QSignalSpy galleryResizes(groupGallery, &QQuickItem::heightChanged);
        QTRY_VERIFY(!panel->property("resizing").toBool());
        QCOMPARE(panel->property("page").toString(), QString("home"));
        QVERIFY(window->height() > initialHeight + 90);
        QCOMPARE(window->width(), initialWidth);
        QCOMPARE(groupWidths.count(), 0);
        QCOMPARE(groupHeights.count(), 1);
        QCOMPARE(groupGallery->size(), groupViewport);
        QCOMPARE(galleryResizes.count(), 0);
        QVERIFY(groupGallery->property("contentHeight").toReal() > groupGallery->height());
        QVERIFY(window->grabWindow().save("group-expanded.png"));
        QVERIFY(galleryFrame->height() > collapsedReveal);
        QCOMPARE(groupTabs->mapToScene(QPointF()), headerPosition);
        groupGallery->setProperty("currentIndex", 0);
        QTRY_VERIFY(groupGallery->property("currentItem").value<QQuickItem *>());
        auto *groupTile = groupGallery->property("currentItem").value<QQuickItem *>();
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, groupTile->mapToScene(QPointF(20, 20)).toPoint());
        auto *groupItemMenu = window->findChild<QObject *>("groupItemMenu");
        QVERIFY(groupItemMenu);
        QTRY_VERIFY(groupItemMenu->property("visible").toBool());
        auto *pin = groupItemMenu->findChild<QQuickItem *>("pinContextItem");
        QVERIFY(pin);
        QVERIFY(pin->isEnabled());
        const auto itemKey = groupItemMenu->property("pinItem").toMap().value("key").toString();
        click(pin);
        QVERIFY(backend.groups()->contains("pinned", itemKey));
        QVERIFY(window->isVisible());
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, groupTile->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(groupItemMenu->property("visible").toBool());
        QTRY_VERIFY(!pin->isEnabled());
        QVERIFY(window->grabWindow().save("group-context.png"));
        QVERIFY(QMetaObject::invokeMethod(groupItemMenu, "close"));
        groupHeights.clear();
        const auto retainedHeight = window->height();
        const auto expandedViewport = groupGallery->size();
        const auto expandedReveal = galleryFrame->height();
        const auto scrollBottom = groupGallery->property("contentHeight").toReal() - groupGallery->height();
        QVERIFY(scrollBottom > 0);
        groupGallery->setProperty("contentY", scrollBottom);
        QSignalSpy collapseGalleryResizes(groupGallery, &QQuickItem::heightChanged);
        click(expandGroup);
        QTest::qWait(35);
        QVERIFY(!panel->property("transitioning").toBool());
        QVERIFY(!skeleton->isVisible());
        QCOMPARE(content->opacity(), 1.0);
        QCOMPARE(groupGallery->size(), expandedViewport);
        QVERIFY(galleryFrame->height() < expandedReveal);
        QVERIFY(galleryFrame->height() > collapsedReveal);
        QVERIFY(groupGallery->property("contentY").toReal() > 0);
        QVERIFY(groupGallery->property("contentY").toReal() < scrollBottom);
        QVERIFY(window->grabWindow().save("group-collapsing.png"));
        QTRY_COMPARE(panel->height(), panel->property("compactHomeHeight").toReal());
        QCOMPARE(window->height(), retainedHeight);
        QTRY_VERIFY(!panel->property("transitioning").toBool());
        QCOMPARE(groupWidths.count(), 0);
        QCOMPARE(groupHeights.count(), 0);
        QCOMPARE(groupGallery->size(), expandedViewport);
        QCOMPARE(collapseGalleryResizes.count(), 0);
        QCOMPARE(galleryFrame->height(), collapsedReveal);
        QTRY_VERIFY(groupGallery->property("contentY").toReal() < 1);
        QCOMPARE(groupTabs->mapToScene(QPointF()), headerPosition);
        auto *hiddenGroupTile = findVisualItem(window->contentItem(), "groupItem_6");
        QVERIFY(hiddenGroupTile && !hiddenGroupTile->isEnabled());
        QVERIFY(groupTile->isEnabled());
        // Reversing an in-progress expansion keeps the same content geometry.
        click(expandGroup);
        QTest::qWait(35);
        QVERIFY(panel->property("resizing").toBool());
        click(expandGroup);
        QTRY_VERIFY(!panel->property("resizing").toBool());
        QTRY_COMPARE(panel->height(), panel->property("compactHomeHeight").toReal());
        QCOMPARE(window->height(), retainedHeight);
        QVERIFY(!skeleton->isVisible());
        QCOMPARE(groupGallery->size(), expandedViewport);
        QCOMPARE(collapseGalleryResizes.count(), 0);
        QCOMPARE(groupTabs->mapToScene(QPointF()), headerPosition);
        // No native size commit may occur after the collapse finishes.
        QTest::qWait(120);
        QCOMPARE(groupHeights.count(), 0);
        const auto panelBottom = qCeil(panel->y() + panel->height());
        QVERIFY(backend.inputRegion().contains(QPoint(window->width() / 2, panelBottom - 10)));
        QVERIFY(!backend.inputRegion().contains(QPoint(window->width() / 2, panelBottom + 25)));
        const auto collapsedImage = window->grabWindow();
        const auto scale = window->devicePixelRatio();
        QCOMPARE(collapsedImage.pixelColor(qRound(window->width() / 2 * scale), qRound((panelBottom + 25) * scale)).alpha(), 0);
        // A popup extending into the retained area must still accept input.
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, groupTile->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(groupItemMenu->property("visible").toBool());
        groupItemMenu->setProperty("y", panelBottom - groupTabs->mapToScene(QPointF()).y() + 15);
        auto *popupContent = groupItemMenu->property("contentItem").value<QQuickItem *>();
        QVERIFY(popupContent);
        QTRY_VERIFY(popupContent->mapToScene(QPointF(20, 20)).y() > panelBottom);
        const auto popupPoint = popupContent->mapToScene(QPointF(20, 20)).toPoint();
        QTRY_VERIFY(backend.inputRegion().contains(popupPoint));
        QVERIFY(window->grabWindow().save("group-popup-retained-surface.png"));
        QVERIFY(QMetaObject::invokeMethod(groupItemMenu, "close"));
        QTRY_VERIFY(!backend.inputRegion().contains(popupPoint));
        QVERIFY(!backend.inputRegion().contains(QPoint(window->width() / 2, panelBottom + 25)));
        QVERIFY(backend.groups()->notificationsEnabled());
        click(bell);
        QVERIFY(!backend.groups()->notificationsEnabled());
        QVERIFY(!backend.applications()->watching());
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        click(browse);
        QTRY_VERIFY(appsSearch->hasActiveFocus());
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        click(back);
        QTRY_VERIFY(!panel->property("transitioning").toBool());
        click(expand);
        QTRY_VERIFY(downloadsSearch->hasActiveFocus());
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        click(back);
        QTRY_VERIFY(!panel->property("transitioning").toBool());
        click(bell);
        QVERIFY(backend.groups()->notificationsEnabled());
        QVERIFY(backend.applications()->watching());
        QCOMPARE(notificationStatus->property("text").toString(), QString("System Notifications"));
        backend.Hide();
        QTRY_VERIFY(!window->isVisible());
        QVERIFY2(qmlErrors.isEmpty(), qPrintable(qmlErrors.join('\n')));
        QTRY_VERIFY(!backend.downloads()->scanning());
    }

    void localizedMenus()
    {
        preloadQmlTranslationRuntime();
        const QStringList languages{"pl", "de", "fr", "es"};
        const QStringList labels{"Powiadomienia systemowe", "Systembenachrichtigungen", "Notifications système", "Notificaciones del sistema"};
        const QStringList pinnedLabels{"Przypięte", "Angeheftet", "Épinglés", "Fijados"};
        const QStringList homeLabels{"Katalog domowy", "Persönlicher Ordner", "Dossier personnel", "Carpeta personal"};
        const QStringList groupLabels{"Dodaj do grupy", "Zu Gruppe hinzufügen", "Ajouter au groupe", "Añadir a un grupo"};
        const QStringList searchLabels{"Filtruj aplikacje…", "Anwendungen filtern…", "Filtrer les applications…", "Filtrar aplicaciones…"};
        for (int i = 0; i < languages.size(); ++i) {
            QTemporaryDir sandbox;
            QVERIFY(QDir().mkdir(sandbox.path() + "/Downloads"));
            const auto groupFile = sandbox.path() + "/groups.json";
            QString customId;
            {
                Groups original(groupFile, {});
                customId = original.create("Moje gry / My games");
                QVERIFY(!customId.isEmpty());
            }
            QFile saved(groupFile);
            QVERIFY(saved.open(QIODevice::ReadOnly));
            const auto savedContents = saved.readAll();
            saved.close();
            Translations translations(QLocale(languages[i]).uiLanguages());
            QVERIFY(qApp->installTranslator(&translations));
            const auto removeTranslator = qScopeGuard([&] { qApp->removeTranslator(&translations); });
            Launcher backend(nullptr, sandbox.path() + "/Downloads", sandbox.path() + "/cache.json", false);
            QCOMPARE(backend.groups()->groups().first().toMap().value("name").toString(), pinnedLabels[i]);
            QCOMPARE(backend.groups()->groups().last().toMap().value("name").toString(), QString("Moje gry / My games"));
            QCOMPARE(backend.places().first().toMap().value("name").toString(), homeLabels[i]);
            QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(saved.readAll(), savedContents); // Translating the default group's display never rewrites user data.
            saved.close();
            const auto file = sandbox.path() + "/Example.zip";
            QFile fixture(file);
            QVERIFY(fixture.open(QIODevice::WriteOnly));
            fixture.close();
            QVERIFY(backend.groups()->add(customId, backend.fileItem(file)));
            backend.groups()->select(customId);
            QQmlApplicationEngine engine;
            QStringList errors;
            connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &warnings) {
                for (const auto &warning : warnings) errors.append(warning.toString());
            });
            engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
            engine.load(QUrl("qrc:/qml/Main.qml"));
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            QVERIFY(window);
            backend.setWindow(window);
            backend.Show();
            QTRY_VERIFY(window->isVisible());
            auto *status = window->findChild<QQuickItem *>("notificationStatus");
            QVERIFY(status);
            QCOMPARE(status->property("text").toString(), labels[i]);
            const auto click = [window](QQuickItem *item) {
                QTRY_VERIFY(item && item->isVisible() && item->isEnabled());
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            };
            auto *bell = window->findChild<QQuickItem *>("notificationBell");
            click(bell);
            QVERIFY(!backend.groups()->notificationsEnabled());
            QCOMPARE(status->property("text").toString(), labels[i]); // Static text, state belongs to the icon.
            auto *tile = findVisualItem(window->contentItem(), "groupItem_0");
            QTRY_VERIFY(tile = findVisualItem(window->contentItem(), "groupItem_0"));
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, tile->mapToScene(QPointF(20, 20)).toPoint());
            auto *menu = window->findChild<QObject *>("groupItemMenu");
            QVERIFY(menu);
            QTRY_VERIFY(menu->property("visible").toBool());
            auto *add = menu->findChild<QQuickItem *>("addToGroup");
            QVERIFY(add);
            QCOMPARE(add->property("text").toString(), groupLabels[i]);
            auto *reveal = menu->findChild<QQuickItem *>("revealLocation");
            QVERIFY(reveal);
            auto *revealLabel = reveal->property("contentItem").value<QQuickItem *>();
            QVERIFY(revealLabel);
            QVERIFY2(revealLabel->implicitWidth() <= revealLabel->width() + 1, qPrintable(languages[i] + " reveal text clips"));
            QVERIFY(window->grabWindow().save("translations-" + languages[i] + "-menu.png"));
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));
            auto *panel = window->findChild<QQuickItem *>("launcherPanel");
            auto *browse = window->findChild<QQuickItem *>("browseApplications");
            auto *back = window->findChild<QQuickItem *>("downloadsBack");
            QVERIFY(panel && browse && back);
            click(browse);
            QTRY_VERIFY(!panel->property("transitioning").toBool());
            QCOMPARE(status->property("text").toString(), labels[i]);
            auto *search = window->findChild<QQuickItem *>("applicationsSearchField");
            QVERIFY(search);
            QCOMPARE(search->property("placeholderText").toString(), searchLabels[i]);
            click(back);
            QTRY_VERIFY(!panel->property("transitioning").toBool());
            click(window->findChild<QQuickItem *>("expandDownloads"));
            QTRY_VERIFY(!panel->property("transitioning").toBool());
            QCOMPARE(status->property("text").toString(), labels[i]);
            backend.Hide();
            QTRY_VERIFY(!window->isVisible());
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        }
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Translations translations({"en"});
    app.installTranslator(&translations);
    DropdownTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_test.moc"
