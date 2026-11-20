// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

// Copy once, leaving the original installation's files intact. Never replace
// settings already saved under the new identity or proceed after a failed copy.
inline QString migrateLegacySettings(const QString &config, const QString &cache, const QString &appCache)
{
    const QList<QPair<QString, QString>> files{
        {config + "/kshiftrc", config + "/plasmidrc"},
        {config + "/kshift/groups.json", config + "/plasmid/groups.json"},
        {cache + "/KShift/kshift/downloads.json", appCache + "/downloads.json"},
    };
    for (const auto &[source, destination] : files) {
        if (!QFileInfo::exists(source) || QFileInfo::exists(destination)) continue;
        const auto failure = [&](const QString &reason) {
            return QCoreApplication::translate("LegacySettings", "Could not migrate %1 to %2: %3")
                .arg(source, destination, reason);
        };
        QFile input(source);
        if (!input.open(QIODevice::ReadOnly)) return failure(input.errorString());
        const auto contents = input.readAll();
        if (input.error() != QFileDevice::NoError) return failure(input.errorString());
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
            return failure(QCoreApplication::translate("LegacySettings", "Cannot create the settings directory."));
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() || !output.commit())
            return failure(output.errorString());
    }
    return {};
}
