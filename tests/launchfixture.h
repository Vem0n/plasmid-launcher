// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// Set up isolated XDG configuration before QApplication/Qt caches any paths.
// The desktop entry is never executed: tests only capture launch requests.
class LaunchFixture
{
public:
    QTemporaryDir root;
    QString documents() const { return root.path() + "/Documents with spaces"; }
    QString applicationFile() const { return root.path() + "/data/applications/org.plasmid.Fixture.desktop"; }

    static bool write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    bool setup()
    {
        if (!root.isValid())
            return false;
        const auto config = root.path() + "/config";
        const auto data = root.path() + "/data";
        if (!QDir().mkpath(config) || !QDir().mkpath(data + "/applications") || !QDir().mkpath(documents()))
            return false;
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
        qputenv("XDG_DATA_HOME", data.toUtf8());
        return write(config + "/user-dirs.dirs", "XDG_DOCUMENTS_DIR=\"" + documents().toUtf8() + "\"\n")
            && write(config + "/plasmidrc", "[Launcher]\nFavorites=org.plasmid.Fixture.desktop\n")
            && write(applicationFile(), "[Desktop Entry]\nType=Application\nName=Fixture app\nIcon=utilities-terminal\n"
                                        "Exec=never-run-this-fixture --title \"space here\" %U\nTerminal=true\nPath=/tmp\n")
            && write(data + "/applications/org.plasmid.NotPinned.desktop",
                     "[Desktop Entry]\nType=Application\nName=Not pinned\nExec=never-run-this-fixture\n");
    }
};
