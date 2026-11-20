// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "applicationsmodel.h"
#include <QObject>
#include <QSet>
#include <QVariantList>

// Durable user choices and a small, explainable offline suggestion engine.
// Display names are never interpreted as commands or group classifications.
class Groups final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId WRITE select NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY notificationsEnabledChanged)
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    Groups(const QString &file, const QVariantList &favorites, QObject *parent = nullptr);
    QVariantList groups() const;
    QString selectedId() const { return m_selected; }
    QVariantList items() const;
    QVariantList suggestions() const { return m_suggestions; }
    QString error() const { return m_error; }
    bool notificationsEnabled() const { return m_enabled; }
    void select(const QString &id);
    void setNotificationsEnabled(bool enabled);
    Q_INVOKABLE QString create(const QString &name);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool add(const QString &id, const QVariantMap &item);
    Q_INVOKABLE bool contains(const QString &id, const QString &key) const;
    Q_INVOKABLE bool unpin(const QString &id, const QString &key);
    Q_INVOKABLE void respond(const QString &id, const QString &action);
    void observeApplications(const QList<ApplicationEntry> &entries);
    static QVariantMap applicationItem(const ApplicationEntry &entry);
    static QStringList rolesFor(const ApplicationEntry &entry);
Q_SIGNALS:
    void changed();
    void selectionChanged();
    void notificationsEnabledChanged();
    void suggestionReady(const QVariantMap &suggestion);
    void itemAdded(const QString &message);
private:
    int groupIndex(const QString &id) const;
    bool save();
    bool commitGroups(const QVariantList &groups);
    QString m_file, m_selected = "pinned", m_error;
    QVariantList m_groups, m_suggestions;
    QSet<QString> m_known;
    bool m_enabled = true, m_baseline = false, m_resetBaseline = false, m_readOnly = false;
};
