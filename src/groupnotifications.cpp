// SPDX-License-Identifier: GPL-3.0-or-later

#include "groupnotifications.h"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {
constexpr auto service = "org.freedesktop.Notifications";
constexpr auto path = "/org/freedesktop/Notifications";
QDBusMessage notificationCall(const QString &method, const QList<QVariant> &arguments)
{
    auto message = QDBusMessage::createMethodCall(service, path, service, method);
    message.setArguments(arguments);
    return message;
}
}

GroupNotifications::GroupNotifications(Groups *groups, const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_groups(groups), m_bus(bus)
{
    m_bus.connect(service, path, service, "ActionInvoked", this, SLOT(actionInvoked(uint,QString)));
    m_bus.connect(service, path, service, "NotificationClosed", this, SLOT(notificationClosed(uint,uint)));
    connect(groups, &Groups::suggestionReady, this, &GroupNotifications::send);
    connect(groups, &Groups::notificationsEnabledChanged, this, [this] {
        if (!m_groups->notificationsEnabled()) closeAll();
    });
    connect(groups, &Groups::changed, this, [this] {
        // Remove popups whose proposals have been accepted/deleted in Plasmid.
        QSet<QString> pending;
        for (const auto &s : m_groups->suggestions())
            if (!s.toMap().value("deferred").toBool()) pending.insert(s.toMap().value("id").toString());
        const auto ids = m_ids;
        for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
            if (pending.contains(it.value())) continue;
            m_bus.asyncCall(notificationCall("CloseNotification", {it.key()}));
            m_ids.remove(it.key());
        }
    });
}

void GroupNotifications::send(const QVariantMap &suggestion)
{
    if (!m_groups->notificationsEnabled()) return;
    const auto item = suggestion.value("item").toMap();
    const auto body = tr("Would you like to add %1 to %2?")
        .arg(item.value("name").toString().toHtmlEscaped(), suggestion.value("groupName").toString().toHtmlEscaped());
    const QStringList actions{"yes", tr("Yes"), "later", tr("Later"), "never", tr("Never")};
    const QVariantMap hints{{"desktop-entry", "io.github.Vem0n.Plasmid"}, {"urgency", QVariant::fromValue<uchar>(1)}, {"suppress-sound", true}};
    auto *call = new QDBusPendingCallWatcher(m_bus.asyncCall(notificationCall("Notify", {QStringLiteral("Plasmid"), uint(0),
        item.value("icon").toString(), tr("%1 — new app detected").arg(item.value("name").toString()),
        body, actions, hints, -1})), this);
    const auto generation = m_generation;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, call, suggestion, generation] {
        QDBusPendingReply<uint> reply = *call;
        call->deleteLater();
        if (reply.isError()) return; // Persisted in-panel suggestions remain available.
        bool current = false;
        for (const auto &s : m_groups->suggestions())
            if (s.toMap().value("id") == suggestion.value("id") && !s.toMap().value("deferred").toBool()) current = true;
        if (generation != m_generation || !m_groups->notificationsEnabled() || !current) {
            m_bus.asyncCall(notificationCall("CloseNotification", {reply.value()}));
        } else m_ids.insert(reply.value(), suggestion.value("id").toString());
    });
}

void GroupNotifications::actionInvoked(uint id, const QString &action)
{
    if (!m_ids.contains(id) || !m_groups->notificationsEnabled()) return;
    const auto suggestionId = m_ids.value(id);
    m_bus.asyncCall(notificationCall("CloseNotification", {id}));
    m_ids.remove(id);
    m_groups->respond(suggestionId, action);
}

void GroupNotifications::notificationClosed(uint id, uint) { m_ids.remove(id); }

void GroupNotifications::closeAll()
{
    ++m_generation;
    const auto ids = m_ids.keys();
    m_ids.clear();
    for (const auto id : ids) {
        m_bus.asyncCall(notificationCall("CloseNotification", {id}));
    }
}
