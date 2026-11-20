// SPDX-License-Identifier: GPL-3.0-or-later

#include "applicationsmodel.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace {
QString unescape(const QString &value)
{
    QString result;
    bool escaped = false;
    for (const auto character : value) {
        if (escaped) {
            switch (character.unicode()) {
            case 's': result += ' '; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: result += character; break;
            }
            escaped = false;
        } else if (character == '\\') escaped = true;
        else result += character;
    }
    return result;
}

QStringList stringList(const QString &value)
{
    QStringList result;
    QString part;
    bool escaped = false;
    for (const auto character : value) {
        if (!escaped && character == ';') {
            if (!part.isEmpty()) result.append(unescape(part));
            part.clear();
        } else {
            part += character;
        }
        if (escaped) escaped = false;
        else escaped = character == '\\';
    }
    if (!part.isEmpty()) result.append(unescape(part));
    result.removeDuplicates();
    return result;
}

QStringList localeKeys(QString locale)
{
    const auto modifier = locale.section('@', 1);
    locale = locale.section('@', 0, 0).section('.', 0, 0);
    const auto language = locale.section('_', 0, 0);
    QStringList keys;
    if (!modifier.isEmpty()) keys.append(locale + '@' + modifier);
    keys.append(locale);
    if (!modifier.isEmpty()) keys.append(language + '@' + modifier);
    keys.append(language);
    keys.removeDuplicates();
    return keys;
}

QString currentLocale()
{
    for (const auto *variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const auto value = qEnvironmentVariable(variable);
        if (!value.isEmpty()) return value;
    }
    return QLocale().name();
}

std::optional<ApplicationEntry> readEntry(const QString &path, const QString &id,
                                         const QString &locale, const QStringList &desktops)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream stream(&file);
    QHash<QString, QString> values;
    bool inEntry = false;
    while (!stream.atEnd()) {
        const auto line = stream.readLine().trimmed();
        if (line.startsWith('[')) { inEntry = line == "[Desktop Entry]"; continue; }
        if (!inEntry || line.startsWith('#')) continue;
        const auto separator = line.indexOf('=');
        if (separator > 0) values.insert(line.left(separator).trimmed(), line.mid(separator + 1).trimmed());
    }
    const auto localized = [&](const QString &key) {
        for (const auto &suffix : localeKeys(locale)) {
            const auto translated = key + '[' + suffix + ']';
            if (values.contains(translated)) return values.value(translated);
        }
        return values.value(key);
    };
    if (values.value("Type") != "Application" || values.value("Hidden") == "true"
        || values.value("NoDisplay") == "true" || values.value("Name").isEmpty()
        || (values.value("Exec").isEmpty() && values.value("DBusActivatable") != "true")) return {};
    const auto only = stringList(values.value("OnlyShowIn"));
    const auto excluded = stringList(values.value("NotShowIn"));
    bool show = !values.contains("OnlyShowIn");
    for (const auto &desktop : desktops) {
        if (only.contains(desktop)) { show = true; break; }
        if (excluded.contains(desktop)) { show = false; break; }
    }
    if (!show) return {};
    const auto executable = unescape(values.value("TryExec"));
    if (!executable.isEmpty() && QStandardPaths::findExecutable(executable).isEmpty()) return {};
    auto icon = unescape(localized("Icon"));
    if (icon.isEmpty()) icon = "application-x-executable";
    return ApplicationEntry{id, path, unescape(localized("Name")), icon,
                            unescape(localized("GenericName")), unescape(localized("Comment")),
                            stringList(values.value("Categories")), stringList(localized("Keywords")),
                            unescape(values.value("Exec")), unescape(values.value("Path")), unescape(values.value("X-Flatpak")), {}, {}};
}

ApplicationsSnapshot scanApplications(const QStringList &directories, const QString &locale, const QStringList &desktops,
                                     const QList<LutrisSource> &sources, const QList<ApplicationEntry> &previousLibrary)
{
    ApplicationsSnapshot snapshot;
    auto &result = snapshot.entries;
    QSet<QString> seen;
    for (const auto &directory : directories) {
        QDirIterator files(directory, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path = files.next();
            const auto id = QDir(directory).relativeFilePath(path).replace('/', '-');
            if (seen.contains(id)) continue;
            seen.insert(id); // Hidden/user overrides also mask lower-priority entries.
            if (auto entry = readEntry(path, id, locale, desktops)) result.append(*entry);
        }
    }
    auto library = readLutrisLibraries(sources);
    snapshot.error = library.error;
    snapshot.libraryEntries = library.entries;
    for (const auto &entry : previousLibrary)
        if (library.failedDatabases.contains(entry.librarySource.database)) snapshot.libraryEntries.append(entry);
    snapshot.libraryWatchPaths = lutrisWatchPaths(sources);
    QSet<QString> shortcuts;
    for (const auto &entry : result) {
        const auto key = lutrisShortcutKey(entry);
        if (!key.isEmpty()) shortcuts.insert(key);
    }
    for (const auto &entry : snapshot.libraryEntries)
        if (!shortcuts.contains(lutrisEntryKey(entry))) result.append(entry);
    QCollator collator{QLocale(locale)};
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(result.begin(), result.end(), [&collator](const auto &a, const auto &b) {
        const auto order = collator.compare(a.name, b.name);
        return order == 0 ? a.desktopId < b.desktopId : order < 0;
    });
    return snapshot;
}
}

ApplicationsModel::ApplicationsModel(QObject *parent, const QStringList &directories,
                                     const QString &locale, const QStringList &desktops,
                                     const std::optional<QList<LutrisSource>> &libraries)
    : QAbstractListModel(parent),
      m_directories(directories.isEmpty() ? QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation) : directories),
      m_desktops(desktops.isEmpty() ? qEnvironmentVariable("XDG_CURRENT_DESKTOP", "KDE").split(':', Qt::SkipEmptyParts) : desktops),
      m_locale(locale.isEmpty() ? currentLocale() : locale)
{
    // Explicit catalog fixture directories do not inspect the user's libraries.
    m_libraries = libraries.has_value() || directories.isEmpty() ? libraries : std::optional<QList<LutrisSource>>(QList<LutrisSource>{});
    m_directories.removeDuplicates();
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(450);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (m_watching) { updateWatches(); refresh(); }
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (m_watching) m_debounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        if (m_watching) m_debounce.start();
    });
    connect(&m_scan, &QFutureWatcher<ApplicationsSnapshot>::finished, this, [this] {
        const auto snapshot = m_scan.result();
        const auto &entries = snapshot.entries;
        m_libraryEntries = snapshot.libraryEntries;
        m_libraryWatchPaths = snapshot.libraryWatchPaths;
        if (snapshot.error != m_libraryError) { m_libraryError = snapshot.error; Q_EMIT libraryErrorChanged(); }
        if (entries != m_entries) {
            beginResetModel();
            m_entries = entries;
            endResetModel();
            Q_EMIT countChanged();
        }
        m_scanning = false;
        Q_EMIT scanningChanged();
        Q_EMIT refreshed();
        if (m_watching) updateWatches();
        if (std::exchange(m_dirty, false)) refresh();
    });
}

ApplicationsModel::~ApplicationsModel() { m_scan.waitForFinished(); }

void ApplicationsModel::setWatching(bool enabled)
{
    if (enabled == m_watching) return;
    m_watching = enabled;
    m_debounce.stop();
    if (!enabled) m_dirty = false;
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_watchError.isEmpty()) { m_watchError.clear(); Q_EMIT watchErrorChanged(); }
    if (enabled) { updateWatches(); refresh(); }
    Q_EMIT watchingChanged();
}

void ApplicationsModel::updateWatches()
{
    // Watch desktop-entry trees and the nearest parent of missing roots only.
    QSet<QString> desired;
    for (const auto &root : m_directories) {
        auto parent = root;
        while (!QDir(parent).exists()) {
            const auto above = QFileInfo(parent).absolutePath();
            if (above == parent) break;
            parent = above;
        }
        if (QDir(parent).exists()) desired.insert(parent);
        if (QDir(root).exists()) {
            desired.insert(QFileInfo(root).absolutePath());
            QDirIterator dirs(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (dirs.hasNext()) desired.insert(dirs.next());
        }
    }
    for (const auto &path : std::as_const(m_libraryWatchPaths))
        if (QFileInfo::exists(path)) desired.insert(path);
    const auto previous = m_watcher.directories() + m_watcher.files();
    QString error;
    for (const auto &path : previous) if (!desired.contains(path)) m_watcher.removePath(path);
    for (const auto &path : desired)
        if (!previous.contains(path) && !m_watcher.addPath(path)) error = tr("Some application locations could not be monitored.");
    if (error != m_watchError) { m_watchError = error; Q_EMIT watchErrorChanged(); }
}
int ApplicationsModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_entries.size(); }
QVariant ApplicationsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const auto &entry = m_entries.at(index.row());
    switch (role) {
    case DesktopIdRole: return entry.desktopId;
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case IconRole: return entry.icon;
    case GenericNameRole: return entry.genericName;
    case CommentRole: return entry.comment;
    case CategoriesRole: return entry.categories;
    case KeywordsRole: return entry.keywords;
    default: return {};
    }
}
QHash<int, QByteArray> ApplicationsModel::roleNames() const
{
    return {{DesktopIdRole, "desktopId"}, {NameRole, "appName"}, {IconRole, "appIcon"},
            {GenericNameRole, "genericName"}, {CommentRole, "comment"},
            {CategoriesRole, "categories"}, {KeywordsRole, "keywords"}};
}
void ApplicationsModel::refresh()
{
    if (m_scanning) { m_dirty = true; return; }
    m_scanning = true;
    Q_EMIT scanningChanged();
    const auto sources = m_libraries.has_value() ? *m_libraries : discoverLutrisSources();
    m_scan.setFuture(QtConcurrent::run(scanApplications, m_directories, m_locale, m_desktops, sources, m_libraryEntries));
}
QList<ApplicationEntry> ApplicationsModel::entries() const
{
    auto entries = m_entries;
    QSet<QString> seen;
    for (const auto &entry : entries) seen.insert(entry.desktopId);
    for (const auto &entry : m_libraryEntries) if (!seen.contains(entry.desktopId)) entries.append(entry);
    return entries;
}
std::optional<ApplicationEntry> ApplicationsModel::validatedEntry(const QString &desktopId) const
{
    for (const auto &entry : entries()) {
        if (entry.desktopId != desktopId) continue;
        // SQL revalidation belongs to the launch/location worker, never the UI.
        if (!entry.libraryId.isEmpty()) return QFileInfo(entry.path).isFile() ? std::optional(entry) : std::nullopt;
        return readEntry(entry.path, entry.desktopId, m_locale, m_desktops);
    }
    return {};
}

ApplicationsFilterModel::ApplicationsFilterModel(ApplicationsModel *source, QObject *parent) : QSortFilterProxyModel(parent)
{
    setSourceModel(source);
    sort(0);
    connect(this, &QAbstractItemModel::modelReset, this, &ApplicationsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &ApplicationsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ApplicationsFilterModel::countChanged);
}
void ApplicationsFilterModel::setQuery(const QString &query)
{
    if (query == m_query) return;
    m_query = query;
    // Ranking depends on the query too. Refresh both ordering and filtering.
    invalidate();
    Q_EMIT queryChanged();
}
void ApplicationsFilterModel::setCategory(const QString &category)
{
    if (category == m_category) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_category = category;
    endFilterChange(Direction::Rows);
#else
    m_category = category;
    invalidateFilter();
#endif
    Q_EMIT categoryChanged();
}
bool ApplicationsFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto index = sourceModel()->index(row, 0, parent);
    if (!m_category.isEmpty() && !index.data(ApplicationsModel::CategoriesRole).toStringList().contains(m_category)) return false;
    return matchRank(index) < 8;
}

int ApplicationsFilterModel::matchRank(const QModelIndex &index) const
{
    const auto query = m_query.trimmed().toCaseFolded();
    if (query.isEmpty()) return 0;
    const auto name = index.data(ApplicationsModel::NameRole).toString().toCaseFolded();
    if (name == query) return 0;
    if (name.startsWith(query)) return 1;
    const auto position = name.indexOf(query);
    if (position >= 0) return !name.at(position - 1).isLetterOrNumber() ? 2 : 3;
    const auto generic = index.data(ApplicationsModel::GenericNameRole).toString().toCaseFolded();
    if (generic == query) return 4;
    if (generic.contains(query)) return 5;
    for (const auto &keyword : index.data(ApplicationsModel::KeywordsRole).toStringList()) {
        if (keyword.toCaseFolded().contains(query)) return 6;
    }
    return index.data(ApplicationsModel::CommentRole).toString().toCaseFolded().contains(query) ? 7 : 8;
}

bool ApplicationsFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto leftRank = matchRank(left), rightRank = matchRank(right);
    // The source is already sorted by localized name and desktop ID. Preserve
    // that stable order for ties and the unfiltered applications browser.
    return leftRank != rightRank ? leftRank < rightRank : left.row() < right.row();
}
QString ApplicationsFilterModel::desktopIdAt(int row) const { return index(row, 0).data(ApplicationsModel::DesktopIdRole).toString(); }
