// SPDX-License-Identifier: GPL-3.0-or-later

#include "lutrislibrary.h"
#include "applicationsmodel.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>

namespace {
QString digest(const QString &value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}
QString identity(const LutrisSource &source, const QString &id, const QString &token) {
    // Keep persisted synthetic desktop IDs stable across the app rename.
    return "kshift-lutris-" + digest((source.flatpak ? "flatpak:" : "native:") + QDir::cleanPath(source.database))
        + '-' + id + '-' + digest(token) + ".desktop";
}
}

QList<LutrisSource> discoverLutrisSources()
{
    const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const auto flatpak = QDir::homePath() + "/.var/app/net.lutris.Lutris";
    QList<LutrisSource> sources;
    for (const bool sandboxed : {false, true}) {
        const auto directory = sandboxed ? flatpak + "/data/lutris" : data + "/lutris";
        auto configDirectory = sandboxed ? flatpak + "/config/lutris" : config + "/lutris";
        if (!QDir(configDirectory).exists()) configDirectory = directory;
        const auto configFile = configDirectory + "/lutris.conf";
        QSettings settings(configFile, QSettings::IniFormat);
        auto path = settings.value("lutris/pga_path").toString();
        if (path.isEmpty()) path = directory + "/pga.db";
        // Do not guess a cwd for relative/custom paths that Lutris owns.
        if (!QDir::isAbsolutePath(path)) continue;
        sources.append({QDir::cleanPath(path), directory, configFile, sandboxed});
    }
    return sources;
}

QStringList lutrisWatchPaths(const QList<LutrisSource> &sources)
{
    QStringList paths;
    for (const auto &source : sources) {
        QStringList files;
        if (!source.database.isEmpty()) files << source.database << source.database + "-wal" << source.database + "-shm";
        if (!source.configFile.isEmpty()) files << source.configFile;
        paths.append(files);
        for (const auto &file : files) {
            auto directory = QFileInfo(file).absolutePath();
            // Monitor the nearest existing parent for first-run/recreation,
            // without falling back to watching the entire home directory.
            while (!QDir(directory).exists() && directory != QDir::homePath() && directory != "/")
                directory = QFileInfo(directory).absolutePath();
            if (directory != QDir::homePath() && directory != "/") paths << directory;
            const auto parent = QFileInfo(directory).absolutePath();
            if (directory != QDir::homePath() && directory != "/"
                && QDir(directory).exists() && parent != QDir::homePath() && parent != "/")
                paths << parent;
        }
    }
    paths.removeDuplicates();
    return paths;
}

LutrisSnapshot readLutrisLibraries(const QList<LutrisSource> &sources)
{
    LutrisSnapshot result;
    for (const auto &source : sources) {
        if (!QFileInfo(source.database).isFile()) continue;
        if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
            result.failedDatabases.append(source.database);
            result.error = QCoreApplication::translate("LutrisLibrary", "Lutris library support needs the Qt SQLite driver. Applications remain available.");
            continue;
        }
        const auto connection = "plasmid-lutris-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool failed = false;
        QList<ApplicationEntry> entries;
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
            database.setDatabaseName(source.database);
            database.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=100");
            if (!database.open()) failed = true;
            else {
                QSqlQuery query(database);
                query.setForwardOnly(true);
                // SELECT only: never initialize, migrate or alter Lutris data.
                if (!query.exec("SELECT id, name, slug, configpath, directory, runner FROM games WHERE installed = 1")) failed = true;
                else {
                    while (query.next()) {
                        bool validId = false;
                        const auto number = query.value(0).toLongLong(&validId);
                        const auto name = query.value(1).toString().trimmed();
                        if (!validId || number <= 0 || name.isEmpty()) continue;
                        const auto id = QString::number(number);
                        const auto slug = query.value(2).toString();
                        const auto config = query.value(3).toString();
                        // Including the stable config/slug avoids launching a different
                        // game if SQLite reuses a deleted numeric ID.
                        const auto token = config.isEmpty() && slug.isEmpty() ? name : slug + '\n' + config;
                        ApplicationEntry entry;
                        entry.desktopId = identity(source, id, token);
                        entry.path = source.database;
                        entry.name = name;
                        entry.icon = "applications-games";
                        if (QRegularExpression("^[A-Za-z0-9_.-]+$").match(slug).hasMatch()) {
                            const auto icon = QFileInfo(source.dataDirectory).absolutePath() + "/icons/hicolor/128x128/apps/lutris_" + slug + ".png";
                            if (QFileInfo(icon).isFile()) entry.icon = icon;
                        }
                        entry.comment = QCoreApplication::translate("LutrisLibrary", "Installed in Lutris");
                        entry.categories = {"Game"};
                        entry.keywords = {"Lutris", query.value(5).toString()};
                        const auto directory = query.value(4).toString();
                        if (QDir::isAbsolutePath(directory)) entry.workingDirectory = QDir::cleanPath(directory);
                        entry.libraryId = id;
                        entry.librarySource = source;
                        entries.append(entry);
                    }
                    if (query.lastError().isValid()) failed = true;
                }
            }
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);
        if (failed) {
            result.failedDatabases.append(source.database);
            result.error = QCoreApplication::translate("LutrisLibrary", "Some Lutris library entries could not be read. Applications remain available.");
        } else result.entries.append(entries);
    }
    return result;
}

std::optional<ApplicationEntry> validateLutrisEntry(const ApplicationEntry &entry)
{
    if (entry.libraryId.isEmpty()) return {};
    const auto snapshot = readLutrisLibraries({entry.librarySource});
    for (const auto &current : snapshot.entries)
        if (current.desktopId == entry.desktopId) return current;
    return {};
}

QString lutrisEntryKey(const ApplicationEntry &entry)
{
    return (entry.librarySource.flatpak ? "flatpak:" : "native:") + entry.libraryId;
}

QString lutrisShortcutKey(const ApplicationEntry &entry)
{
    const auto tokens = QProcess::splitCommand(entry.command);
    bool native = false, flatpak = false;
    for (const auto &token : tokens) {
        if (QFileInfo(token).fileName() == "lutris") native = true;
        if (token == "net.lutris.Lutris") flatpak = true;
    }
    if (!native && !flatpak) return {};
    for (auto token : tokens) {
        // Lutris's shortcut writer shell-quotes URLs with query strings.
        if (token.startsWith('\'') && token.endsWith('\'')) token = token.mid(1, token.size() - 2);
        const QUrl url(token);
        if (url.scheme() != "lutris") continue;
        const auto match = QRegularExpression("^rungameid/([0-9]+)$").match(url.path());
        if (match.hasMatch()) return (flatpak ? "flatpak:" : "native:") + match.captured(1);
    }
    return {};
}
