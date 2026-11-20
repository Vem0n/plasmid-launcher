// SPDX-License-Identifier: GPL-3.0-or-later

#include "launcher.h"
#include "externalprocess.h"
#include "qmlruntime.h"
#include "searchexecution.h"
#include "groupnotifications.h"
#include "translations.h"
#include "legacysettings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QFutureWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtConcurrentRun>
#include <QStandardPaths>
#include <QTextStream>
#include <QTemporaryDir>
#include <memory>

int main(int argc, char **argv)
{
    // Select the installed Wayland shell integration before creating QApplication.
    // Offscreen tests and an explicitly selected platform do not use layer shell.
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland"
        && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

    QApplication app(argc, argv);
    app.setApplicationName("plasmid-launcher");
    app.setApplicationDisplayName("Plasmid");
    app.setOrganizationName("Vem0n");
    app.setDesktopFileName("io.github.Vem0n.Plasmid");
    app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);
    // KIO jobs used by runners acquire/release quit locks. Completing a query
    // must not terminate this resident launcher when its window is hidden.
    app.setQuitLockEnabled(false);
    Translations translations;
    app.installTranslator(&translations);

    QCommandLineParser parser;
    parser.setApplicationDescription("A Plasma-themed dropdown with KRunner search");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"background", "Start hidden and wait for Alt+Shift."});
    parser.addOption({"no-shortcut", "Preview without Alt+Shift; run the command again to reopen."});
    parser.addOption({"quit", "Quit the running Plasmid instance."});
    parser.addOption({"smoke-test", "Check installed KRunner providers without launching results."});
    parser.addOption({"screenshot", "Save the launcher surface to a PNG, then exit (no shortcut).", "path"});
    parser.process(app);

    const bool smokeTest = parser.isSet("smoke-test");
    const bool screenshot = parser.isSet("screenshot");
    const bool isolated = smokeTest || screenshot;
    auto bus = QDBusConnection::sessionBus();
    if (!isolated) {
        if (parser.isSet("quit")) {
            QDBusInterface remote("io.github.Vem0n.Plasmid", "/Plasmid", "io.github.Vem0n.Plasmid", bus);
            return remote.call("Quit").type() == QDBusMessage::ErrorMessage ? 1 : 0;
        }
        if (!bus.isConnected()) {
            qCritical() << "A session D-Bus connection is required.";
            return 1;
        }
        if (!bus.registerService("io.github.Vem0n.Plasmid")) {
            QDBusInterface remote("io.github.Vem0n.Plasmid", "/Plasmid", "io.github.Vem0n.Plasmid", bus);
            if (!parser.isSet("no-shortcut")) {
                QDBusReply<bool> enabled = remote.call("EnableShortcut");
                if (!enabled.isValid() || !enabled.value()) {
                    QTextStream(stderr) << "Could not enable Alt+Shift on the existing instance. "
                        "If it is an older build, run './build/plasmid-launcher --quit', then './build/plasmid-launcher'.\n";
                    return 1;
                }
                QTextStream(stderr) << "Alt+Shift is active; toggling the running Plasmid instance.\n";
            } else {
                QTextStream(stderr) << "Toggling the running instance; --no-shortcut does not enable Alt+Shift.\n";
            }
            return remote.call("Toggle").type() == QDBusMessage::ErrorMessage ? 1 : 0;
        }
    }

    if (!isolated) {
        const auto migrationError = migrateLegacySettings(
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation),
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        if (!migrationError.isEmpty()) {
            qCritical().noquote() << migrationError;
            return 1;
        }
    }
    preloadQmlTranslationRuntime();
    std::unique_ptr<QTemporaryDir> previewStorage;
    QString previewDownloads, previewIndex;
    if (isolated) {
        previewStorage = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/plasmid-preview-XXXXXX");
        previewDownloads = previewStorage->path() + "/Downloads";
        if (!previewStorage->isValid() || !QDir().mkpath(previewDownloads)) {
            qCritical() << "Cannot create isolated preview storage.";
            return 1;
        }
        previewIndex = previewStorage->path() + "/downloads.json";
    }
    Launcher launcher(nullptr, previewDownloads, previewIndex, !isolated);
    // Never send native notifications from smoke tests or screenshot previews.
    std::unique_ptr<GroupNotifications> groupNotifications;
    if (!isolated) groupNotifications = std::make_unique<GroupNotifications>(launcher.groups(), bus);
    QObject::connect(&launcher, &Launcher::searchExecutionRequested, &launcher,
                     [&launcher](QAbstractItemModel *model, const QModelIndex &index, int action) {
        // The native surface and renderer are already initialized. Native
        // runners can start children asynchronously, so keep host-only Qt
        // overrides out of the process environment permanently from here on.
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
        qunsetenv("QT_QPA_PLATFORM");
        qunsetenv("QT_QUICK_BACKEND");
        launcher.finishSearchExecution(executeSearchModel(model, index, action));
    });
    QObject::connect(&launcher, &Launcher::externalCommandRequested, &launcher,
                     [&launcher](const QString &program, const QStringList &arguments) {
        auto *started = new QFutureWatcher<bool>(&launcher);
        QObject::connect(started, &QFutureWatcher<bool>::finished, &launcher, [&launcher, started] {
            if (!started->result())
                launcher.reportExternalLaunchFailure();
            started->deleteLater();
        });
        const auto state = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
        if (!QDir().mkpath(state)) {
            started->deleteLater();
            launcher.reportExternalLaunchFailure();
            return;
        }
        const auto logFile = state + "/external-apps.log";
        QTextStream(stderr) << "Opening external application; its diagnostics: " << logFile << '\n';
        started->setFuture(QtConcurrent::run([program, arguments, logFile] {
            return startExternalApplication(program, arguments, logFile);
        }));
    });
    QQmlApplicationEngine engine;
    bool smokeCompleted = false;
    QObject::connect(&engine, &QQmlEngine::exit, &app, [&](int code) {
        if (smokeTest && code == 0) {
            smokeCompleted = true;
            qInfo() << "PASS: calculator, application and location search providers";
        }
    });
    engine.setInitialProperties({{"backend", QVariant::fromValue(&launcher)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(smokeTest ? "qrc:/tests/SearchSmoke.qml" : "qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (smokeTest) {
        const int result = app.exec();
        return smokeCompleted ? result : 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;
    // QWaylandWindow construction initializes/caches the shell integration.
    // Clear the selection override before any lazy-loaded runner starts work.
    window->create();
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    launcher.setWindow(window);
    if (!isolated) {
        if (!bus.registerObject("/Plasmid", &launcher, QDBusConnection::ExportScriptableSlots)) {
            qCritical() << "Could not register the Plasmid control interface.";
            return 1;
        }
        if (!parser.isSet("no-shortcut")) {
            if (launcher.registerShortcut())
                QTextStream(stderr) << "Plasmid ready. Alt+Shift toggles the panel.\n";
        } else {
            QTextStream(stderr) << "Preview mode: Alt+Shift is disabled. Reopen with './build/plasmid-launcher --no-shortcut' "
                                  "or enable the hotkey with './build/plasmid-launcher'.\n";
        }
    }
    if (!parser.isSet("background") || screenshot)
        QTimer::singleShot(0, &launcher, &Launcher::Show);
    if (screenshot) {
        QTimer::singleShot(1500, &app, [&] {
            const bool saved = window->grabWindow().save(parser.value("screenshot"));
            app.exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
