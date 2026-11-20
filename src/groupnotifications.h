// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "groups.h"
#include <QDBusConnection>
#include <QHash>

// Instantiated by the real host only. Tests use a private notification service.
class GroupNotifications final : public QObject
{
    Q_OBJECT
public:
    GroupNotifications(Groups *groups, const QDBusConnection &bus, QObject *parent = nullptr);
private Q_SLOTS:
    void actionInvoked(uint id, const QString &action);
    void notificationClosed(uint id, uint reason);
private:
    void send(const QVariantMap &suggestion);
    void closeAll();
    Groups *m_groups;
    QDBusConnection m_bus;
    QHash<uint, QString> m_ids;
    uint m_generation = 0;
};
