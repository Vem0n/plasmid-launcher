// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QProcessEnvironment>
#include <QProcess>

inline QProcessEnvironment externalApplicationEnvironment(QProcessEnvironment environment)
{
    // Keep desktop/session and MIME-handler state, but never pass Plasmid's
    // special window-host settings to Dolphin or a system file handler.
    environment.remove("QT_WAYLAND_SHELL_INTEGRATION");
    environment.remove("QT_QPA_PLATFORM");
    environment.remove("QT_QUICK_BACKEND");
    return environment;
}

inline bool startExternalApplication(const QString &program, const QStringList &arguments, const QString &logFile)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessEnvironment(externalApplicationEnvironment(QProcessEnvironment::systemEnvironment()));
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(logFile, QIODevice::Append);
    process.setStandardErrorFile(logFile, QIODevice::Append);
    return process.startDetached();
}
