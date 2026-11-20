// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "searchexecution.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class RecordingSearchModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString queryString MEMBER query)
public:
    QString query = "fixture", matchId = "match-one";
    QList<QUrl> urls;
    bool resultEnabled = true, accepted = true;
    int executions = 0, action = -2;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 1; }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "matchId"}, {Qt::UserRole + 1, "urls"},
        {Qt::UserRole + 2, "enabled"}, {Qt::UserRole + 3, "actions"}}; }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        if (role == Qt::UserRole) return matchId;
        if (role == Qt::UserRole + 1) return QVariant::fromValue(urls);
        if (role == Qt::UserRole + 2) return resultEnabled;
        if (role == Qt::UserRole + 3) return QVariantList{QVariantMap{{"text", "Fixture action"}}};
        return {};
    }
    Q_INVOKABLE bool run(const QModelIndex &index) { if (!index.isValid()) return false; ++executions; action = -1; return accepted; }
    Q_INVOKABLE bool runAction(const QModelIndex &index, int number) {
        if (!index.isValid()) return false;
        ++executions; action = number; return accepted;
    }
};

class SearchTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void nativeDispatchWaitsForHideAndRejectsChangedMatches()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        QVERIFY(QDir().mkdir(sandbox.path() + "/Downloads"));
        Launcher backend(nullptr, sandbox.path() + "/Downloads", sandbox.path() + "/cache.json");
        QQuickWindow window;
        backend.setWindow(&window);
        RecordingSearchModel model;
        QSignalSpy resumed(&backend, &Launcher::searchResuming);
        QSignalSpy errors(&backend, &Launcher::launchError);
        connect(&backend, &Launcher::searchExecutionRequested, this,
                [&](QAbstractItemModel *source, const QModelIndex &index, int action) {
            QVERIFY(!window.isVisible());
            QVERIFY(backend.searchExecuting());
            backend.finishSearchExecution(executeSearchModel(source, index, action));
        });
        window.show();
        QVERIFY(backend.runSearchResult(&model, 0));
        QVERIFY(backend.searchExecuting());
        QVERIFY(!backend.runSearchResult(&model, 0));
        QTest::qWait(30);
        QCOMPARE(model.executions, 0);
        window.hide();
        QTRY_COMPARE(model.executions, 1);
        QCOMPARE(model.action, -1);
        QVERIFY(!backend.searchExecuting());
        QCOMPARE(resumed.count(), 0);

        QVERIFY(backend.runSearchResult(&model, 0, 0));
        QTRY_COMPARE(model.executions, 2);
        QCOMPARE(model.action, 0);
        QVERIFY(!backend.runSearchResult(&model, 0, 1));
        model.resultEnabled = false;
        QVERIFY(!backend.runSearchResult(&model, 0));
        model.resultEnabled = true;

        window.show();
        QVERIFY(backend.runSearchResult(&model, 0));
        model.matchId = "replaced-row";
        window.hide();
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(model.executions, 2);
        QCOMPARE(resumed.count(), 1);
        window.show();
        QVERIFY(backend.runSearchResult(&model, 0));
        model.query = "different query";
        window.hide();
        QTRY_COMPARE(errors.count(), 2);
        QCOMPARE(model.executions, 2);
        model.accepted = false; // A native runner may refine the query instead.
        QVERIFY(backend.runSearchResult(&model, 0));
        QTRY_COMPARE(model.executions, 3);
        QCOMPARE(resumed.count(), 3);
        QCOMPARE(model.query, QString("different query"));
        QVERIFY(!backend.searchExecuting());
        QTRY_VERIFY(!backend.downloads()->scanning());
    }

    void revealUsesLocalUrlsAndRechecksExistence()
    {
        QTemporaryDir sandbox;
        QVERIFY(sandbox.isValid());
        QVERIFY(QDir().mkdir(sandbox.path() + "/Downloads"));
        const auto path = sandbox.path() + "/CBT ; $(noop).rar";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        Launcher backend(nullptr, sandbox.path() + "/Downloads", sandbox.path() + "/cache.json");
        RecordingSearchModel model;
        model.urls = {QUrl("https://example.org/remote.rar"), QUrl::fromLocalFile(path)};
        QCOMPARE(backend.searchResultPath(&model, 0), path);
        const auto pin = backend.searchResultItem(&model, 0);
        QCOMPARE(pin.value("kind").toString(), QString("file"));
        QCOMPARE(pin.value("path").toString(), path);
        QVERIFY(backend.groups()->add("pinned", pin));
        QSignalSpy commands(&backend, &Launcher::externalCommandRequested);
        QVERIFY(backend.revealPath(path));
        QTRY_COMPARE(commands.count(), 1);
        auto command = commands.takeFirst();
        QCOMPARE(command[0].toString(), QString("dolphin"));
        QCOMPARE(command[1].toStringList(), (QStringList{"--select", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}));
        QVERIFY(backend.revealPath(sandbox.path()));
        QTRY_COMPARE(commands.count(), 1);
        QCOMPARE(commands.takeFirst()[1].toStringList(), QStringList{QUrl::fromLocalFile(sandbox.path()).toString(QUrl::FullyEncoded)});
        QQuickWindow window;
        backend.setWindow(&window);
        connect(&backend, &Launcher::externalClosing, &window, &QQuickWindow::hide);
        window.show();
        QVERIFY(backend.launchGroupItem("pinned", pin.value("key").toString()));
        QVERIFY(!window.isVisible());
        QTRY_COMPARE(commands.count(), 1);
        command = commands.takeFirst();
        QCOMPARE(command[0].toString(), QString("xdg-open"));
        QCOMPARE(command[1].toStringList(), QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)});
        const auto folder = backend.fileItem(sandbox.path());
        QVERIFY(backend.groups()->add("pinned", folder));
        window.show();
        QVERIFY(backend.launchGroupItem("pinned", folder.value("key").toString()));
        QTRY_COMPARE(commands.count(), 1);
        command = commands.takeFirst();
        QCOMPARE(command[0].toString(), QString("dolphin"));
        QCOMPARE(command[1].toStringList(), QStringList{QUrl::fromLocalFile(sandbox.path()).toString(QUrl::FullyEncoded)});
        QVERIFY(QFile::remove(path));
        QCOMPARE(backend.searchResultPath(&model, 0), QString());
        QVERIFY(!backend.revealPath(path));
        QVERIFY(!backend.revealPath("relative.rar"));
        QVERIFY(backend.searchResultItem(&model, 0).isEmpty());
        QVERIFY(!backend.launchGroupItem("pinned", pin.value("key").toString()));
        QVERIFY(!backend.launchGroupItem("missing-group", folder.value("key").toString()));
        QVERIFY(commands.isEmpty());
        QTRY_VERIFY(!backend.downloads()->scanning());
    }
};
QTEST_MAIN(SearchTest)
#include "search_test.moc"
