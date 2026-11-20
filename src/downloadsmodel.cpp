// SPDX-License-Identifier: GPL-3.0-or-later

#include "downloadsmodel.h"
#include <QCoreApplication>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QtConcurrentRun>
#include <algorithm>
#include <sys/stat.h>

DownloadsFilterModel::DownloadsFilterModel(DownloadsModel *source, QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterRole(DownloadsModel::NameRole);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSourceModel(source);
    connect(this, &QAbstractItemModel::rowsInserted, this, &DownloadsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DownloadsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &DownloadsFilterModel::countChanged);
}

void DownloadsFilterModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    setFilterFixedString(query.trimmed());
    Q_EMIT queryChanged();
}

QString DownloadsFilterModel::pathAt(int row) const
{
    return data(index(row, 0), DownloadsModel::PathRole).toString();
}

namespace {
DownloadSnapshot readDirectory(const QString &directory, const QString &indexFile)
{
    DownloadSnapshot result;
    QDir dir(directory);
    if (!dir.exists() || !QFileInfo(directory).isReadable()) {
        result.error = QCoreApplication::translate("DownloadsModel", "Downloads is unavailable.");
        return result;
    }
    QJsonObject previous;
    bool hasHistory = false;
    QFile saved(indexFile);
    if (saved.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(saved.readAll()).object();
        if (document["directory"].toString() == directory) {
            previous = document["entries"].toObject();
            hasHistory = true;
        }
    }
    const bool baseline = !hasHistory;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    QJsonObject next;
    const auto files = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                        QDir::NoSort);
    for (const auto &file : files) {
        struct stat info;
        const auto encoded = QFile::encodeName(file.absoluteFilePath());
        if (::lstat(encoded.constData(), &info) != 0 || !file.exists())
            continue;
        // A replaced file with the same name is a new arrival. Birth time also
        // distinguishes inode reuse where the filesystem supplies it.
        const QString identity = QString::number(info.st_dev) + ':' + QString::number(info.st_ino)
            + ':' + QString::number(file.birthTime().toMSecsSinceEpoch());
        const auto old = previous[file.fileName()].toObject();
        qint64 arrival = qint64(old["arrival"].toDouble());
        if (old["identity"].toString() != identity || arrival <= 0) {
            const auto born = file.birthTime().isValid() ? file.birthTime() : file.lastModified();
            arrival = baseline ? qMin(born.toMSecsSinceEpoch(), now) : now;
        }
        next.insert(file.fileName(), QJsonObject{{"identity", identity}, {"arrival", double(arrival)}});
        result.entries.append({file.fileName(), file.absoluteFilePath(), {}, {}, arrival});
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto &a, const auto &b) {
        return a.arrival == b.arrival ? a.name < b.name : a.arrival > b.arrival;
    });
    // Keep all arrival metadata, but bound the UI and MIME work to 200 rows.
    if (result.entries.size() > 200)
        result.entries.resize(200);
    QMimeDatabase mime;
    for (auto &entry : result.entries) {
        const QFileInfo file(entry.path);
        const auto type = mime.mimeTypeForFile(entry.path, QMimeDatabase::MatchExtension);
        entry.icon = file.isDir() ? QStringLiteral("folder") : type.iconName();
        entry.description = file.isDir() ? QCoreApplication::translate("DownloadsModel", "Folder") : type.comment();
        entry.description += " · " + QLocale().toString(QDateTime::fromMSecsSinceEpoch(entry.arrival),
                                                      QLocale::ShortFormat);
    }
    const QByteArray serialized = QJsonDocument(QJsonObject{{"directory", directory}, {"entries", next}})
                                      .toJson(QJsonDocument::Compact);
    // Never rewrite the cache for an unchanged directory event.
    if (!indexFile.isEmpty() && (!hasHistory || next != previous)) {
        QDir().mkpath(QFileInfo(indexFile).absolutePath());
        QSaveFile output(indexFile);
        if (!output.open(QIODevice::WriteOnly) || output.write(serialized) != serialized.size() || !output.commit())
            result.error = QCoreApplication::translate("DownloadsModel", "Could not save download arrival history.");
    }
    return result;
}
}

DownloadsModel::DownloadsModel(const QString &directory, const QString &indexFile, QObject *parent)
    : QAbstractListModel(parent), m_directory(QDir::cleanPath(QFileInfo(directory).absoluteFilePath())),
      m_indexFile(indexFile)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, &DownloadsModel::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &DownloadsModel::refresh);
    connect(&m_scan, &QFutureWatcher<DownloadSnapshot>::finished, this, [this] {
        const auto result = m_scan.result();
        if (result.entries != m_entries) {
            beginResetModel();
            m_entries = result.entries;
            endResetModel();
            Q_EMIT countChanged();
        }
        updateWatches();
        const auto error = result.error.isEmpty() ? m_watchError : result.error;
        if (error != m_error) {
            m_error = error;
            Q_EMIT errorChanged();
        }
        m_scanning = false;
        Q_EMIT scanningChanged();
        Q_EMIT refreshed();
        if (m_dirty && !m_debounce.isActive())
            m_debounce.start();
    });
    updateWatches();
    refresh();
}

int DownloadsModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_entries.size(); }
QVariant DownloadsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries[index.row()];
    switch (role) {
    case NameRole: return entry.name;
    case PathRole: return entry.path;
    case IconRole: return entry.icon;
    case DescriptionRole: return entry.description;
    case ArrivalRole: return entry.arrival;
    default: return {};
    }
}
QHash<int, QByteArray> DownloadsModel::roleNames() const
{
    return {{NameRole, "fileName"}, {PathRole, "filePath"}, {IconRole, "fileIcon"},
            {DescriptionRole, "description"}, {ArrivalRole, "arrival"}};
}
bool DownloadsModel::contains(const QString &path) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&path](const auto &entry) { return entry.path == path; });
}
QString DownloadsModel::pathAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries[row].path : QString();
}
void DownloadsModel::refresh()
{
    m_dirty = true;
    // Coalesce bursts without starving updates during continuous arrivals.
    if (!m_debounce.isActive())
        m_debounce.start();
}
void DownloadsModel::scan()
{
    if (m_scanning)
        return; // finished() will schedule one more scan if dirty.
    m_dirty = false;
    m_scanning = true;
    Q_EMIT scanningChanged();
    const auto directory = m_directory;
    const auto cache = m_indexFile;
    m_scan.setFuture(QtConcurrent::run([directory, cache] { return readDirectory(directory, cache); }));
}
void DownloadsModel::updateWatches()
{
    QStringList desired;
    if (QDir(m_directory).exists())
        desired.append(m_directory);
    // Watch the nearest existing ancestor as well, so removing/recreating the
    // Downloads directory does not silently disable monitoring.
    auto parent = QFileInfo(m_directory).dir();
    while (!parent.exists()) {
        const auto ancestor = QFileInfo(parent.absolutePath()).dir();
        if (ancestor.absolutePath() == parent.absolutePath())
            break;
        parent = ancestor;
    }
    if (parent.exists() && !desired.contains(parent.absolutePath()))
        desired.append(parent.absolutePath());
    const auto current = m_watcher.directories();
    for (const auto &path : current) {
        if (!desired.contains(path))
            m_watcher.removePath(path);
    }
    m_watchError.clear();
    for (const auto &path : desired) {
        if (!m_watcher.directories().contains(path) && !m_watcher.addPath(path))
            m_watchError = tr("Could not watch Downloads for changes.");
    }
}
