// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLibrary>
#include <QDebug>

inline void preloadQmlTranslationRuntime()
{
    // KF6 6.32's KLocalizedQmlContext installs a static QObject as an app
    // event filter. If its library first loads in the QML type-loader thread,
    // that QObject has the wrong thread affinity. Load the runtime on the GUI
    // thread before QML imports; no private symbols or KDE headers are used.
    static QLibrary translations(QStringLiteral("KF6I18nQml"), 6);
    translations.setLoadHints(QLibrary::PreventUnloadHint);
    if (!translations.load())
        qWarning().noquote() << "Could not preload KDE translations:" << translations.errorString();
}
