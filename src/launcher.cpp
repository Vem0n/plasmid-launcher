// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "applicationlocation.h"

#include <QApplication>
#include <QColor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QQuickItem>
#include <QLocale>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <utility>
#include <QtConcurrentRun>

namespace {
constexpr auto accelService = "org.kde.kglobalaccel";
constexpr auto accelPath = "/kglobalaccel";
constexpr auto accelInterface = "org.kde.KGlobalAccel";

QString configPath(const QString &file)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + '/' + file;
}

QString color(QSettings &settings, const QString &key, const QString &fallback)
{
    const QVariant value = settings.value(key);
    QStringList parts = value.toStringList();
    if (parts.size() == 1)
        parts = parts.first().split(',');
    if (parts.size() >= 3)
        return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt()).name();
    return fallback;
}

QString applicationPath(const QString &desktopId)
{
    if (desktopId.contains('/') || !desktopId.endsWith(".desktop"))
        return {};
    const auto path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
    if (path.isEmpty())
        return {};
    QSettings desktop(path, QSettings::IniFormat);
    desktop.beginGroup("Desktop Entry");
    if (desktop.value("Type").toString() != "Application"
        || desktop.value("Hidden", false).toBool() || desktop.value("NoDisplay", false).toBool())
        return {};
    return path;
}

int roleByName(QAbstractItemModel *model, const QByteArray &name)
{
    const auto roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name) return it.key();
    return -1;
}
}

Launcher::Launcher(QObject *parent, const QString &downloadsDirectory, const QString &indexFile, bool monitorApplications,
                   const std::optional<QList<LutrisSource>> &libraries) : QObject(parent)
{
    const auto directory = downloadsDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : downloadsDirectory;
    const auto cache = indexFile.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/downloads.json" : indexFile;
    m_downloads = new DownloadsModel(directory, cache, this);
    m_filteredDownloads = new DownloadsFilterModel(m_downloads, this);
    // Existing backend fixtures isolate persistence with an index path; do not
    // inspect the user's Lutris databases unless a fixture source is supplied.
    const auto sources = libraries.has_value() || indexFile.isEmpty() ? libraries
        : std::optional<QList<LutrisSource>>(QList<LutrisSource>{});
    m_applications = new ApplicationsModel(this, {}, {}, {}, sources);
    m_filteredApplications = new ApplicationsFilterModel(m_applications, this);
    readTheme();
    readFavorites();
    readPlaces();
    const auto groupFile = indexFile.isEmpty() ? configPath("plasmid/groups.json")
        : QFileInfo(indexFile).absolutePath() + "/groups.json";
    m_groups = new Groups(groupFile, m_favorites, this);
    connect(m_applications, &ApplicationsModel::refreshed, this, [this] {
        m_groups->observeApplications(m_applications->entries());
    });
    connect(m_groups, &Groups::notificationsEnabledChanged, this, [this] {
        m_applications->setWatching(m_groups->notificationsEnabled());
    });
    if (monitorApplications) m_applications->setWatching(m_groups->notificationsEnabled());
    connect(&m_libraryLaunch, &QFutureWatcher<std::optional<ApplicationEntry>>::finished, this, [this] {
        const auto current = m_libraryLaunch.result();
        m_libraryLaunching = false;
        if (m_window && m_window->isVisible()) {
            Q_EMIT launchError(tr("Lutris launch cancelled because Plasmid was reopened."));
            return;
        }
        if (!current) {
            m_applications->refresh();
            Show();
            Q_EMIT launchError(tr("This Lutris entry is no longer available, or its library cannot be read."));
            return;
        }
        const auto url = "lutris:rungameid/" + current->libraryId;
        const auto arguments = current->librarySource.flatpak
            ? QStringList{"run", "net.lutris.Lutris", url} : QStringList{url};
        queueExternal(current->librarySource.flatpak ? "flatpak" : "lutris", arguments, current->path);
    });
}

Launcher::~Launcher()
{
    m_libraryLaunch.waitForFinished();
    if (m_registered) {
        QDBusInterface iface(accelService, accelPath, accelInterface);
        iface.call("setInactive", m_actionId);
    }
}

void Launcher::readTheme()
{
    QSettings plasma(configPath("plasmarc"), QSettings::IniFormat);
    const QString themeName = plasma.value("Theme/name", "default").toString();
    QString colors = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                          "plasma/desktoptheme/" + themeName + "/colors");
    if (colors.isEmpty())
        colors = configPath("kdeglobals");
    QSettings palette(colors, QSettings::IniFormat);
    m_theme = {{"background", color(palette, "Colors:Window/BackgroundNormal", "#282828")},
               {"surface", color(palette, "Colors:View/BackgroundNormal", "#1e1e1e")},
               {"text", color(palette, "Colors:Window/ForegroundNormal", "#eaeaea")},
               {"muted", color(palette, "Colors:Window/ForegroundInactive", "#999999")},
               {"accent", color(palette, "Colors:Selection/BackgroundNormal", "#4bc8ff")},
               {"name", themeName}};
    QSettings global(configPath("kdeglobals"), QSettings::IniFormat);
    QStringList iconPaths = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, "icons", QStandardPaths::LocateDirectory);
    iconPaths.append(QIcon::themeSearchPaths());
    iconPaths.removeDuplicates();
    QIcon::setThemeSearchPaths(iconPaths);
    QIcon::setThemeName(global.value("Icons/Theme", QIcon::themeName()).toString());
}

void Launcher::readFavorites()
{
    QSettings config(configPath("plasmidrc"), QSettings::IniFormat);
    const QStringList defaults{"org.mozilla.firefox.desktop", "org.kde.dolphin.desktop",
                               "org.kde.konsole.desktop", "org.kde.kcalc.desktop",
                               "systemsettings.desktop", "steam.desktop"};
    const QStringList ids = config.value("Launcher/Favorites", defaults).toStringList();
    const QString locale = QLocale().name();
    for (const auto &id : ids) {
        const QString path = applicationPath(id);
        if (path.isEmpty())
            continue;
        QSettings desktop(path, QSettings::IniFormat);
        desktop.beginGroup("Desktop Entry");
        QString name = desktop.value("Name[" + locale + ']',
                         desktop.value("Name[" + locale.section('_', 0, 0) + ']',
                         desktop.value("Name", id))).toString();
        m_favorites.append(QVariantMap{{"name", name}, {"icon", desktop.value("Icon", "application-x-executable")},
                                       {"desktopId", id}});
    }
}

void Launcher::readPlaces()
{
    const auto add = [this](const QString &id, const QString &name, const QString &path, const QString &icon) {
        if (!path.isEmpty() && QDir(path).exists())
            m_places.append(QVariantMap{{"id", id}, {"name", name}, {"path", path}, {"icon", icon}});
    };
    add("home", tr("Home"), QDir::homePath(), "user-home");
    add("documents", tr("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "folder-documents");
}

void Launcher::setWindow(QQuickWindow *window)
{
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    window->installEventFilter(this);
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible)
            QTimer::singleShot(0, this, &Launcher::dispatchExternal);
    });
    connect(window, &QWindow::widthChanged, this, &Launcher::updateInputRegion);
    connect(window, &QWindow::heightChanged, this, &Launcher::updateInputRegion);
    updateInputRegion();
}

void Launcher::setInputArea(const QRectF &area, QQuickItem *overlay)
{
    m_inputArea = area;
    if (overlay != m_popupOverlay) {
        disconnect(m_overlayChildren);
        m_popupOverlay = overlay;
        if (overlay) m_overlayChildren = connect(overlay, &QQuickItem::childrenChanged, this, &Launcher::watchPopupItems);
        watchPopupItems();
    }
    updateInputRegion();
}

void Launcher::watchPopupItems()
{
    for (const auto &connection : std::as_const(m_popupConnections)) disconnect(connection);
    m_popupConnections.clear();
    if (m_popupOverlay) {
        m_popupConnections << connect(m_popupOverlay, &QQuickItem::visibleChanged, this, &Launcher::updateInputRegion);
        for (auto *item : m_popupOverlay->childItems()) {
            m_popupConnections << connect(item, &QQuickItem::xChanged, this, &Launcher::updateInputRegion)
                << connect(item, &QQuickItem::yChanged, this, &Launcher::updateInputRegion)
                << connect(item, &QQuickItem::widthChanged, this, &Launcher::updateInputRegion)
                << connect(item, &QQuickItem::heightChanged, this, &Launcher::updateInputRegion)
                << connect(item, &QQuickItem::visibleChanged, this, &Launcher::updateInputRegion)
                << connect(item, &QQuickItem::opacityChanged, this, &Launcher::updateInputRegion);
        }
    }
    updateInputRegion();
}

void Launcher::updateInputRegion()
{
    if (!m_window || m_inputArea.isEmpty()) return;
    QRegion region(m_inputArea.toAlignedRect());
    if (m_popupOverlay && m_popupOverlay->isVisible()) {
        for (auto *item : m_popupOverlay->childItems()) {
            if (item->isVisible() && item->opacity() > 0)
                region += item->mapRectToScene(item->boundingRect()).toAlignedRect();
        }
    }
    region &= QRect(0, 0, m_window->width(), m_window->height());
    if (region == m_inputRegion) return;
    m_inputRegion = region;
    // Offscreen/minimal render fixtures have no desktop input region support.
    const auto platform = QGuiApplication::platformName();
    if (platform != "offscreen" && platform != "minimal") m_window->setMask(region);
    m_window->update(); // Commit the Wayland input region with the next frame.
}

bool Launcher::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin)) {
        auto *field = m_window->activeFocusItem();
        const QStringList searchFields{"searchField", "applicationsSearchField", "downloadsSearchField"};
        if (field && searchFields.contains(field->objectName())) {
            QPointF point;
            if (event->type() == QEvent::MouseButtonPress) point = static_cast<QMouseEvent *>(event)->position();
            else {
                const auto points = static_cast<QTouchEvent *>(event)->points();
                if (points.isEmpty()) return QObject::eventFilter(watched, event);
                point = points.first().position();
            }
            if (!field->contains(field->mapFromScene(point))) {
                field->setFocus(false, Qt::MouseFocusReason);
                if (auto *panel = m_window->findChild<QQuickItem *>("launcherPanel"))
                    panel->forceActiveFocus(Qt::MouseFocusReason);
            }
        }
    }
    // Observe the press before delivery; do not consume button/menu clicks.
    return QObject::eventFilter(watched, event);
}

void Launcher::shortcutFailed(const QString &message)
{
    m_shortcutError = message;
    qWarning().noquote() << message;
    Q_EMIT shortcutErrorChanged();
}

bool Launcher::registerShortcut()
{
    if (m_shortcutEnabled)
        return true;
    // This is the session-bus protocol used by KF6 KGlobalAccel. Keep this small
    // adapter isolated so it can be replaced by the KF6 wrapper when adding the backend.
    qDBusRegisterMetaType<QList<int>>();
    QDBusInterface iface(accelService, accelPath, accelInterface);
    if (!iface.isValid()) {
        shortcutFailed(tr("Global shortcuts are unavailable. Start Plasmid again to open it."));
        return false;
    }
    // Modifier-only chords are encoded as modifier bits, with no final key.
    const int chord = int(Qt::AltModifier | Qt::ShiftModifier);
    const QDBusMessage registered = iface.call("doRegister", m_actionId);
    if (registered.type() == QDBusMessage::ErrorMessage) {
        shortcutFailed(registered.errorMessage());
        return false;
    }
    m_registered = true;
    // The daemon drops conflicting assignments; do not use setForeignShortcut
    // or otherwise steal another component's shortcut. Autoloading also keeps
    // a user's alternative binding (or disabled binding) on subsequent runs.
    // SetPresent (2), with autoloading (0): honor an existing user assignment.
    QDBusReply<QList<int>> assigned = iface.call("setShortcut", m_actionId,
                                                QVariant::fromValue(QList<int>{chord}), uint(2));
    if (!assigned.isValid() || assigned.value().isEmpty()) {
        shortcutFailed(tr("Plasmid has no global shortcut. Assign one in System Settings → Shortcuts."));
        return false;
    }
    QDBusReply<QDBusObjectPath> component = iface.call("getComponent", m_actionId[0]);
    if (!component.isValid() || !QDBusConnection::sessionBus().connect(
            accelService, component.value().path(), "org.kde.kglobalaccel.Component",
            "globalShortcutPressed", this, SLOT(shortcutPressed(QString,QString,qlonglong)))) {
        shortcutFailed(tr("Could not subscribe to Plasmid's global shortcut."));
        return false;
    }
    m_shortcutEnabled = true;
    m_shortcutError.clear();
    Q_EMIT shortcutEnabledChanged();
    Q_EMIT shortcutErrorChanged();
    return true;
}

void Launcher::shortcutPressed(const QString &component, const QString &action, qlonglong)
{
    if (component == m_actionId[0] && action == m_actionId[1])
        Toggle();
}

void Launcher::Show()
{
    if (m_window) {
        m_open = true;
        Q_EMIT opening();
    }
}

void Launcher::Hide() { m_open = false; Q_EMIT closing(); }
void Launcher::dismiss() { Hide(); }
void Launcher::Toggle() { m_open ? Hide() : Show(); }
void Launcher::Quit() { QCoreApplication::quit(); }
bool Launcher::IsVisible() const { return m_window && m_window->isVisible(); }
bool Launcher::EnableShortcut() { return registerShortcut(); }

bool Launcher::queueExternal(const QString &program, const QStringList &arguments, const QString &path)
{
    if (!m_pendingProgram.isEmpty() || m_searchExecuting || m_libraryLaunching)
        return false;
    m_pendingProgram = program;
    m_pendingArguments = arguments;
    m_pendingPath = path;
    m_open = false;
    // The host unmaps its surface immediately for external actions. Only after
    // visibleChanged(false) has run may a process be launched.
    Q_EMIT externalClosing();
    QTimer::singleShot(0, this, &Launcher::dispatchExternal);
    return true;
}

void Launcher::dispatchExternal()
{
    dispatchSearch();
    if (m_pendingProgram.isEmpty() || (m_window && m_window->isVisible()))
        return;
    const auto program = std::exchange(m_pendingProgram, {});
    const auto arguments = std::exchange(m_pendingArguments, {});
    const auto path = std::exchange(m_pendingPath, {});
    if (!QFileInfo::exists(path)) {
        m_downloads->refresh();
        Show();
        Q_EMIT launchError(tr("This item is no longer available."));
        return;
    }
    Q_EMIT externalCommandRequested(program, arguments);
}

bool Launcher::runSearchResult(QAbstractItemModel *model, int row, int action)
{
    if (!model || m_searchExecuting || m_libraryLaunching || !m_pendingProgram.isEmpty() || action < -1) return false;
    const auto index = model->index(row, 0);
    const auto enabledRole = roleByName(model, "enabled");
    if (!index.isValid() || (enabledRole >= 0 && !index.data(enabledRole).toBool())) return false;
    const auto actionsRole = roleByName(model, "actions");
    if (action >= 0 && (actionsRole < 0 || action >= index.data(actionsRole).toList().size())) return false;
    m_searchModel = model;
    m_searchIndex = index;
    m_searchMatchId = index.data(roleByName(model, "matchId")).toString();
    m_searchQuery = model->property("queryString").toString();
    m_searchAction = action;
    m_searchDispatched = false;
    m_searchExecuting = true;
    m_open = false;
    Q_EMIT searchExecutingChanged(); // Keep the provider Loader alive while hidden.
    Q_EMIT externalClosing();
    QTimer::singleShot(0, this, &Launcher::dispatchSearch);
    return true;
}

void Launcher::dispatchSearch()
{
    if (!m_searchExecuting || m_searchDispatched || (m_window && m_window->isVisible())) return;
    if (!m_searchModel || !m_searchIndex.isValid()
        || m_searchModel->property("queryString").toString() != m_searchQuery
        || m_searchIndex.data(roleByName(m_searchModel, "matchId")).toString() != m_searchMatchId) {
        finishSearchExecution(false);
        Q_EMIT launchError(tr("This search result changed. Please select it again."));
        return;
    }
    m_searchDispatched = true;
    Q_EMIT searchExecutionRequested(m_searchModel, m_searchIndex, m_searchAction);
}

void Launcher::finishSearchExecution(bool accepted)
{
    if (!m_searchExecuting) return;
    // Some runners request a refined query instead of launching. Restore that
    // query without the ordinary opening/reset path clearing it.
    if (!accepted) { m_open = true; Q_EMIT searchResuming(); }
    m_searchExecuting = false;
    m_searchModel.clear();
    m_searchIndex = QPersistentModelIndex();
    Q_EMIT searchExecutingChanged();
    if (accepted) Q_EMIT searchFinished();
}

QString Launcher::searchResultPath(QAbstractItemModel *model, int row) const
{
    if (!model) return {};
    const auto urls = model->index(row, 0).data(roleByName(model, "urls")).value<QList<QUrl>>();
    for (const auto &url : urls) {
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) return url.toLocalFile();
    }
    return {};
}

QVariantMap Launcher::applicationItem(const QString &desktopId) const
{
    const auto entry = m_applications->validatedEntry(desktopId);
    return entry ? Groups::applicationItem(*entry) : QVariantMap{};
}

QVariantMap Launcher::fileItem(const QString &path) const
{
    if (!QDir::isAbsolutePath(path) || !QFileInfo::exists(path)) return {};
    const QFileInfo info(QDir::cleanPath(path));
    const auto canonical = info.canonicalFilePath();
    for (const auto &entry : m_applications->entries())
        if (entry.libraryId.isEmpty() && (entry.path == info.absoluteFilePath() || (!canonical.isEmpty() && QFileInfo(entry.path).canonicalFilePath() == canonical)))
            return applicationItem(entry.desktopId);
    return {{"kind", "file"}, {"key", "file:" + info.absoluteFilePath()},
            {"path", info.absoluteFilePath()}, {"name", info.fileName()},
            {"icon", info.isDir() ? "folder" : QMimeDatabase().mimeTypeForFile(info, QMimeDatabase::MatchExtension).iconName()}};
}

QVariantMap Launcher::searchResultItem(QAbstractItemModel *model, int row) const
{
    if (!model) return {};
    const auto urls = model->index(row, 0).data(roleByName(model, "urls")).value<QList<QUrl>>();
    for (const auto &url : urls) {
        if (url.scheme() == "applications") {
            auto id = url.path();
            while (id.startsWith('/')) id.remove(0, 1);
            const auto item = applicationItem(id);
            if (!item.isEmpty()) return item;
        }
        if (url.isLocalFile()) {
            const auto item = fileItem(url.toLocalFile());
            if (!item.isEmpty()) return item;
        }
    }
    return {};
}

bool Launcher::launchGroupItem(const QString &groupId, const QString &key)
{
    for (const auto &group : m_groups->groups()) {
        const auto value = group.toMap();
        if (value.value("id") != groupId) continue;
        for (const auto &stored : value.value("items").toList()) {
            const auto item = stored.toMap();
            if (item.value("key") != key) continue;
            if (item.value("kind") == "app") {
                const auto id = item.value("desktopId").toString();
                for (const auto &favorite : m_favorites)
                    if (favorite.toMap().value("desktopId") == id) return launchFavorite(id);
                return launchApplication(id);
            }
            const auto path = item.value("path").toString();
            if (!QFileInfo::exists(path)) break;
            const auto url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
            return queueExternal(QFileInfo(path).isDir() ? "dolphin" : "xdg-open", {url}, path);
        }
    }
    Q_EMIT launchError(tr("This group item is no longer available."));
    return false;
}

bool Launcher::revealPath(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || !QFileInfo::exists(path)) {
        Q_EMIT launchError(tr("This location is no longer available."));
        return false;
    }
    const auto url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    return queueExternal("dolphin", QFileInfo(path).isDir() ? QStringList{url} : QStringList{"--select", url}, path);
}

void Launcher::requestApplicationLocation(const QString &desktopId)
{
    const auto entry = m_applications->validatedEntry(desktopId);
    if (!entry) { Q_EMIT applicationLocationReady(desktopId, {}, tr("Application unavailable")); return; }
    auto *watcher = new QFutureWatcher<ApplicationLocation>(this);
    connect(watcher, &QFutureWatcher<ApplicationLocation>::finished, this, [this, watcher, desktopId] {
        const auto location = watcher->result();
        Q_EMIT applicationLocationReady(desktopId, location.path, location.label);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(resolveApplicationLocation, *entry, steamInstallationRoots()));
}

bool Launcher::openDownloads()
{
    const auto path = m_downloads->directory();
    if (!QFileInfo(path).isDir()) {
        Q_EMIT launchError(tr("Downloads is unavailable."));
        return false;
    }
    return queueExternal("dolphin", {QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}, path);
}

bool Launcher::openPlace(const QString &placeId)
{
    // Only the two explicit folder buttons are accepted, never an arbitrary path.
    QString path;
    if (placeId == "home")
        path = QDir::homePath();
    else if (placeId == "documents")
        path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        Q_EMIT launchError(tr("This folder is unavailable."));
        return false;
    }
    return queueExternal("dolphin", {QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}, path);
}

bool Launcher::launchFavorite(const QString &desktopId)
{
    bool pinned = false;
    for (const auto &favorite : std::as_const(m_favorites)) {
        if (favorite.toMap().value("desktopId").toString() == desktopId) {
            pinned = true;
            break;
        }
    }
    const auto path = pinned ? applicationPath(desktopId) : QString();
    if (path.isEmpty()) {
        Q_EMIT launchError(tr("This pinned application is no longer available."));
        return false;
    }
    // KDE interprets Exec field codes, terminal/working-directory settings and
    // D-Bus activation. Do not parse or shell-execute desktop entry commands.
    return queueExternal("kstart", {"--application", desktopId.chopped(8)}, path);
}

bool Launcher::validDownload(const QString &path)
{
    if (!m_downloads->contains(path) || !QFileInfo::exists(path)) {
        m_downloads->refresh();
        Q_EMIT launchError(tr("This item is no longer available."));
        return false;
    }
    return true;
}

bool Launcher::launchApplication(const QString &desktopId)
{
    const auto entry = m_applications->validatedEntry(desktopId);
    if (!entry) {
        m_applications->refresh();
        Q_EMIT launchError(tr("This application is no longer available."));
        return false;
    }
    if (!entry->libraryId.isEmpty()) {
        if (m_libraryLaunching || m_searchExecuting || !m_pendingProgram.isEmpty()) return false;
        m_libraryLaunching = true;
        m_open = false;
        Q_EMIT externalClosing();
        m_libraryLaunch.setFuture(QtConcurrent::run(validateLutrisEntry, *entry));
        return true;
    }
    // Use the exact installed desktop file, including nested application IDs.
    // KDE handles Exec/Terminal/D-Bus activation; the catalog only reads metadata.
    return queueExternal("kioclient", {"exec", QUrl::fromLocalFile(entry->path).toString(QUrl::FullyEncoded)}, entry->path);
}

bool Launcher::openDownload(const QString &path)
{
    if (!validDownload(path))
        return false;
    // MIME/default-application handling is delegated to the desktop. Do not
    // execute AppImages, unpack archives, or parse desktop files ourselves.
    return queueExternal("xdg-open", {QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}, path);
}

bool Launcher::revealDownload(const QString &path)
{
    if (!validDownload(path))
        return false;
    return queueExternal("dolphin", {"--select", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}, path);
}

void Launcher::reportExternalLaunchFailure()
{
    Show();
    Q_EMIT launchError(tr("Could not start the external application. Check that Dolphin, kstart, kioclient and xdg-open are installed."));
}
