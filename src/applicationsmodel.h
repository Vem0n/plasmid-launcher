// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <optional>
#include "lutrislibrary.h"

struct ApplicationEntry {
    QString desktopId, path, name, icon, genericName, comment;
    QStringList categories, keywords;
    QString command, workingDirectory, flatpakId;
    QString libraryId;
    LutrisSource librarySource;
    bool operator==(const ApplicationEntry &) const = default;
};

struct ApplicationsSnapshot {
    QList<ApplicationEntry> entries, libraryEntries;
    QStringList libraryWatchPaths;
    QString error;
};

// Desktop and library metadata are independent of presentation and ranking.
// Command metadata is inspected for shortcut deduplication, never executed.
class ApplicationsModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool watching READ watching NOTIFY watchingChanged)
    Q_PROPERTY(QString watchError READ watchError NOTIFY watchErrorChanged)
    Q_PROPERTY(QString libraryError READ libraryError NOTIFY libraryErrorChanged)
public:
    enum Role { DesktopIdRole = Qt::UserRole + 1, NameRole, IconRole, GenericNameRole,
                CommentRole, CategoriesRole, KeywordsRole };
    explicit ApplicationsModel(QObject *parent = nullptr, const QStringList &directories = {},
                               const QString &locale = {}, const QStringList &desktops = {},
                               const std::optional<QList<LutrisSource>> &libraries = std::nullopt);
    ~ApplicationsModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool scanning() const { return m_scanning; }
    bool watching() const { return m_watching; }
    QString watchError() const { return m_watchError; }
    QString libraryError() const { return m_libraryError; }
    void setWatching(bool enabled);
    QList<ApplicationEntry> entries() const;
    std::optional<ApplicationEntry> validatedEntry(const QString &desktopId) const;
    Q_INVOKABLE void refresh();
Q_SIGNALS:
    void countChanged();
    void scanningChanged();
    void refreshed();
    void watchingChanged();
    void watchErrorChanged();
    void libraryErrorChanged();
private:
    QStringList m_directories, m_desktops;
    QString m_locale;
    QList<ApplicationEntry> m_entries;
    QList<ApplicationEntry> m_libraryEntries;
    QFutureWatcher<ApplicationsSnapshot> m_scan;
    std::optional<QList<LutrisSource>> m_libraries;
    QStringList m_libraryWatchPaths;
    QString m_libraryError;
    bool m_scanning = false;
    bool m_dirty = false;
    bool m_watching = false;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_watchError;
    void updateWatches();
};

class ApplicationsFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit ApplicationsFilterModel(ApplicationsModel *source, QObject *parent = nullptr);
    QString query() const { return m_query; }
    QString category() const { return m_category; }
    void setQuery(const QString &query);
    void setCategory(const QString &category);
    Q_INVOKABLE QString desktopIdAt(int row) const;
Q_SIGNALS:
    void queryChanged();
    void categoryChanged();
    void countChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
private:
    int matchRank(const QModelIndex &index) const;
    QString m_query, m_category;
};
