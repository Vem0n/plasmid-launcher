// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QDBusConnection>
#include <QProcess>
#include <QTest>

class ExistingInstance : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.Vem0n.Plasmid")
public:
    QStringList calls;
    bool enabled = false;
public Q_SLOTS:
    Q_SCRIPTABLE bool EnableShortcut() { calls.append("enable"); enabled = true; return true; }
    Q_SCRIPTABLE void Toggle() { calls.append("toggle"); }
};

class InstanceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void normalInvocationEnablesExistingPreview()
    {
        // Never register a mock object on the user's real desktop bus.
        if (qEnvironmentVariable("PLASMID_PRIVATE_BUS_TEST") != "1")
            QSKIP("Run with CTest, which provides an isolated dbus-run-session bus.");
        auto bus = QDBusConnection::sessionBus();
        ExistingInstance instance;
        QVERIFY(bus.registerService("io.github.Vem0n.Plasmid"));
        QVERIFY(bus.registerObject("/Plasmid", &instance, QDBusConnection::ExportScriptableSlots));
        const auto program = QCoreApplication::applicationDirPath() + "/plasmid-launcher";
        QProcess preview;
        preview.start(program, {"--no-shortcut"});
        QTRY_COMPARE_WITH_TIMEOUT(preview.state(), QProcess::NotRunning, 5000);
        QCOMPARE(preview.exitCode(), 0);
        QCOMPARE(instance.calls, QStringList{"toggle"});
        QVERIFY(!instance.enabled);
        QVERIFY(preview.readAllStandardError().contains("--no-shortcut does not enable Alt+Shift"));

        instance.calls.clear();
        QProcess normal;
        normal.start(program);
        QTRY_COMPARE_WITH_TIMEOUT(normal.state(), QProcess::NotRunning, 5000);
        QCOMPARE(normal.exitCode(), 0);
        QCOMPARE(instance.calls, (QStringList{"enable", "toggle"}));
        QVERIFY(instance.enabled);
        QVERIFY(normal.readAllStandardError().contains("Alt+Shift is active"));
        bus.unregisterObject("/Plasmid");
        bus.unregisterService("io.github.Vem0n.Plasmid");
    }
};
QTEST_GUILESS_MAIN(InstanceTest)
#include "instance_test.moc"
