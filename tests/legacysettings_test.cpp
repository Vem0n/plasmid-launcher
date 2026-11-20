// SPDX-License-Identifier: GPL-3.0-or-later

#include "legacysettings.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>

class LegacySettingsTest : public QObject
{
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &contents)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private Q_SLOTS:
    void preservesGroupsPreferencesAndDownloadHistory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config = directory.path() + "/config";
        const auto cache = directory.path() + "/cache";
        const auto appCache = cache + "/Vem0n/plasmid-launcher";
        const QByteArray groups = R"({"version":1,"groups":[{"id":"pinned","name":"Pinned","items":[]},{"id":"custom","name":"Moje gry","items":[]}],"notificationsEnabled":false})";
        QVERIFY(write(config + "/kshift/groups.json", groups));
        QVERIFY(write(config + "/kshiftrc", "[Launcher]\nFavorites=custom.desktop\n"));
        QVERIFY(write(cache + "/KShift/kshift/downloads.json", "{\"arrival\":123}"));
        QCOMPARE(migrateLegacySettings(config, cache, appCache), QString());
        QCOMPARE(read(config + "/plasmid/groups.json"), groups);
        QCOMPARE(read(config + "/plasmidrc"), read(config + "/kshiftrc"));
        QCOMPARE(read(appCache + "/downloads.json"), read(cache + "/KShift/kshift/downloads.json"));
        QCOMPARE(read(config + "/kshift/groups.json"), groups);
        QVERIFY(write(config + "/plasmid/groups.json", "new groups"));
        QVERIFY(write(config + "/plasmidrc", "new preferences"));
        QVERIFY(write(appCache + "/downloads.json", "new history"));
        QCOMPARE(migrateLegacySettings(config, cache, appCache), QString());
        QCOMPARE(read(config + "/plasmid/groups.json"), QByteArray("new groups"));
        QCOMPARE(read(config + "/plasmidrc"), QByteArray("new preferences"));
        QCOMPARE(read(appCache + "/downloads.json"), QByteArray("new history"));
        QCOMPARE(read(config + "/kshift/groups.json"), groups);
    }
    void noLegacyFilesNeedsNoMigration()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QCOMPARE(migrateLegacySettings(directory.path() + "/config", directory.path() + "/cache", directory.path() + "/appCache"), QString());
        QVERIFY(QDir(directory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }
    void failurePreservesOriginal()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config = directory.path() + "/config";
        QVERIFY(write(config + "/kshift/groups.json", "original groups"));
        QVERIFY(write(config + "/plasmid", "blocks destination directory"));
        const auto error = migrateLegacySettings(config, directory.path() + "/cache", directory.path() + "/appCache");
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(config + "/kshift/groups.json"), QByteArray("original groups"));
        QCOMPARE(read(config + "/plasmid"), QByteArray("blocks destination directory"));
        QVERIFY(!QFileInfo::exists(config + "/plasmid/groups.json"));
    }
    void previewDoesNotMigrateOrReplaceSavedState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config = directory.path() + "/config";
        const auto cache = directory.path() + "/cache";
        const auto appCache = cache + "/Vem0n/plasmid-launcher";
        QVERIFY(write(config + "/kshift/groups.json", "legacy groups"));
        QVERIFY(write(config + "/kshiftrc", "[Launcher]\nFavorites=custom.desktop\n"));
        QVERIFY(write(cache + "/KShift/kshift/downloads.json", "legacy history"));
        QVERIFY(write(appCache + "/downloads.json", "existing history"));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_CONFIG_HOME", config);
        environment.insert("XDG_DATA_HOME", directory.path() + "/data");
        environment.insert("XDG_CACHE_HOME", cache);
        environment.insert("XDG_STATE_HOME", directory.path() + "/state");
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        QProcess preview;
        preview.setProcessEnvironment(environment);
        preview.start("dbus-run-session", {"--", QCoreApplication::applicationDirPath() + "/plasmid-launcher",
                                          "--screenshot", directory.path() + "/preview.png"});
        QVERIFY(preview.waitForFinished(10000));
        QCOMPARE(preview.exitStatus(), QProcess::NormalExit);
        QVERIFY2(preview.exitCode() == 0, preview.readAllStandardError().constData());
        QVERIFY(QFileInfo::exists(directory.path() + "/preview.png"));
        QVERIFY(!QFileInfo::exists(config + "/plasmid/groups.json"));
        QVERIFY(!QFileInfo::exists(config + "/plasmidrc"));
        QCOMPARE(read(config + "/kshift/groups.json"), QByteArray("legacy groups"));
        QCOMPARE(read(appCache + "/downloads.json"), QByteArray("existing history"));
        QCOMPARE(read(cache + "/KShift/kshift/downloads.json"), QByteArray("legacy history"));
    }
};
QTEST_GUILESS_MAIN(LegacySettingsTest)
#include "legacysettings_test.moc"
