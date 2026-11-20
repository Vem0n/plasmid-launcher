// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <QList>
#include <QStringList>
#include <optional>

struct ApplicationEntry;
struct LutrisSource {
    QString database, dataDirectory, configFile;
    bool flatpak = false;
    bool operator==(const LutrisSource &) const = default;
};
struct LutrisSnapshot {
    QList<ApplicationEntry> entries;
    QStringList failedDatabases;
    QString error;
};

QList<LutrisSource> discoverLutrisSources();
QStringList lutrisWatchPaths(const QList<LutrisSource> &sources);
LutrisSnapshot readLutrisLibraries(const QList<LutrisSource> &sources);
std::optional<ApplicationEntry> validateLutrisEntry(const ApplicationEntry &entry);
QString lutrisShortcutKey(const ApplicationEntry &entry);
QString lutrisEntryKey(const ApplicationEntry &entry);
