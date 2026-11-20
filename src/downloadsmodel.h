// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QSortFilterProxyModel>

struct DownloadEntry {
    QString name;
    QString path;
    QString icon;
    QString description;
    qint64 arrival = 0;
    bool operator==(const DownloadEntry &) const = default;
};

class DownloadsModel;

// In-memory filename filter; source arrival order and path identity are kept.
class DownloadsFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit DownloadsFilterModel(DownloadsModel *source, QObject *parent = nullptr);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int count() const { return rowCount(); }
    Q_INVOKABLE QString pathAt(int row) const;
Q_SIGNALS:
    void queryChanged();
    void countChanged();
private:
    QString m_query;
};

struct DownloadSnapshot {
    QList<DownloadEntry> entries;
    QString error;
};

// Metadata-only index of direct children. No recursive scans or file contents.
class DownloadsModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, IconRole, DescriptionRole, ArrivalRole };
    explicit DownloadsModel(const QString &directory, const QString &indexFile, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_entries.size(); }
    bool scanning() const { return m_scanning; }
    QString error() const { return m_error; }
    QString directory() const { return m_directory; }
    bool contains(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void countChanged();
    void scanningChanged();
    void errorChanged();
    void refreshed();

private:
    void updateWatches();
    void scan();
    const QString m_directory;
    const QString m_indexFile;
    QFileSystemWatcher m_watcher;
    QFutureWatcher<DownloadSnapshot> m_scan;
    QTimer m_debounce;
    QList<DownloadEntry> m_entries;
    QString m_error;
    QString m_watchError;
    bool m_scanning = false;
    bool m_dirty = false;
};
