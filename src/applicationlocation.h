// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "applicationsmodel.h"

struct ApplicationLocation {
    QString path;
    QString label;
};

// Metadata-only lookup. Never runs a command or searches game contents.
QStringList steamInstallationRoots();
ApplicationLocation resolveApplicationLocation(const ApplicationEntry &entry, const QStringList &steamRoots);
