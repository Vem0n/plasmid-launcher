// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QQuickItem>
#include <QRegion>
#include "downloadsmodel.h"
#include "applicationsmodel.h"
#include "groups.h"

class QQuickWindow;

class Launcher final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.Vem0n.Plasmid")
    Q_PROPERTY(QVariantMap theme READ theme CONSTANT)
    Q_PROPERTY(QVariantList favorites READ favorites CONSTANT)
    Q_PROPERTY(QVariantList places READ places CONSTANT)
    Q_PROPERTY(Groups *groups READ groups CONSTANT)
    Q_PROPERTY(QString shortcutError READ shortcutError NOTIFY shortcutErrorChanged)
    Q_PROPERTY(bool shortcutEnabled READ shortcutEnabled NOTIFY shortcutEnabledChanged)
    Q_PROPERTY(DownloadsModel *downloads READ downloads CONSTANT)
    Q_PROPERTY(DownloadsFilterModel *filteredDownloads READ filteredDownloads CONSTANT)
    Q_PROPERTY(ApplicationsModel *applications READ applications CONSTANT)
    Q_PROPERTY(ApplicationsFilterModel *filteredApplications READ filteredApplications CONSTANT)
    Q_PROPERTY(bool searchExecuting READ searchExecuting NOTIFY searchExecutingChanged)

public:
    explicit Launcher(QObject *parent = nullptr, const QString &downloadsDirectory = {}, const QString &indexFile = {},
                      bool monitorApplications = true, const std::optional<QList<LutrisSource>> &libraries = std::nullopt);
    ~Launcher() override;
    QVariantMap theme() const { return m_theme; }
    QVariantList favorites() const { return m_favorites; }
    QVariantList places() const { return m_places; }
    Groups *groups() const { return m_groups; }
    QString shortcutError() const { return m_shortcutError; }
    bool shortcutEnabled() const { return m_shortcutEnabled; }
    DownloadsModel *downloads() const { return m_downloads; }
    DownloadsFilterModel *filteredDownloads() const { return m_filteredDownloads; }
    ApplicationsModel *applications() const { return m_applications; }
    ApplicationsFilterModel *filteredApplications() const { return m_filteredApplications; }
    bool searchExecuting() const { return m_searchExecuting; }
    void setWindow(QQuickWindow *window);
    Q_INVOKABLE void setInputArea(const QRectF &area, QQuickItem *overlay);
    QRegion inputRegion() const { return m_inputRegion; }
    bool registerShortcut();
    Q_INVOKABLE bool launchFavorite(const QString &desktopId);
    Q_INVOKABLE bool launchApplication(const QString &desktopId);
    Q_INVOKABLE void requestApplicationLocation(const QString &desktopId);
    Q_INVOKABLE QString searchResultPath(QAbstractItemModel *model, int row) const;
    Q_INVOKABLE QVariantMap searchResultItem(QAbstractItemModel *model, int row) const;
    Q_INVOKABLE QVariantMap applicationItem(const QString &desktopId) const;
    Q_INVOKABLE QVariantMap fileItem(const QString &path) const;
    Q_INVOKABLE bool launchGroupItem(const QString &groupId, const QString &key);
    Q_INVOKABLE bool revealPath(const QString &path);
    Q_INVOKABLE bool runSearchResult(QAbstractItemModel *model, int row, int action = -1);
    void finishSearchExecution(bool accepted);
    Q_INVOKABLE bool openPlace(const QString &placeId);
    Q_INVOKABLE bool openDownloads();
    Q_INVOKABLE bool openDownload(const QString &path);
    Q_INVOKABLE bool revealDownload(const QString &path);
    Q_INVOKABLE void dismiss();
    void reportExternalLaunchFailure();

public Q_SLOTS:
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    Q_SCRIPTABLE void Quit();
    Q_SCRIPTABLE bool IsVisible() const;
    Q_SCRIPTABLE bool EnableShortcut();

Q_SIGNALS:
    void opening();
    void closing();
    void shortcutErrorChanged();
    void shortcutEnabledChanged();
    void launchError(const QString &message);
    void externalClosing();
    void externalCommandRequested(const QString &program, const QStringList &arguments);
    void applicationLocationReady(const QString &desktopId, const QString &path, const QString &label);
    void searchExecutingChanged();
    void searchExecutionRequested(QAbstractItemModel *model, const QModelIndex &index, int action);
    void searchFinished();
    void searchResuming();

private Q_SLOTS:
    void shortcutPressed(const QString &component, const QString &action, qlonglong timestamp);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void readTheme();
    void readFavorites();
    void readPlaces();
    void shortcutFailed(const QString &message);
    bool queueExternal(const QString &program, const QStringList &arguments, const QString &path);
    bool validDownload(const QString &path);
    void dispatchExternal();
    void dispatchSearch();
    void watchPopupItems();
    void updateInputRegion();
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_popupOverlay;
    QRectF m_inputArea;
    QRegion m_inputRegion;
    QMetaObject::Connection m_overlayChildren;
    QList<QMetaObject::Connection> m_popupConnections;
    QVariantMap m_theme;
    QVariantList m_favorites;
    QVariantList m_places;
    DownloadsModel *m_downloads;
    DownloadsFilterModel *m_filteredDownloads;
    ApplicationsModel *m_applications;
    ApplicationsFilterModel *m_filteredApplications;
    Groups *m_groups;
    QString m_pendingProgram;
    QStringList m_pendingArguments;
    QString m_pendingPath;
    QString m_shortcutError;
    bool m_registered = false;
    bool m_shortcutEnabled = false;
    bool m_open = false;
    bool m_searchExecuting = false;
    bool m_searchDispatched = false;
    bool m_libraryLaunching = false;
    QFutureWatcher<std::optional<ApplicationEntry>> m_libraryLaunch;
    QPointer<QAbstractItemModel> m_searchModel;
    QPersistentModelIndex m_searchIndex;
    QString m_searchMatchId, m_searchQuery;
    int m_searchAction = -1;
    // Retain the pre-rename shortcut identity to preserve existing assignments
    // without stealing/conflicting with the user's previously registered chord.
    const QStringList m_actionId{"org.kshift.KShift", "Toggle", "Plasmid", "Toggle Plasmid"};
};
