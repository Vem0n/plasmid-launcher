// SPDX-License-Identifier: GPL-3.0-or-later

#include "groups.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QProcess>
#include <QUuid>
#include <algorithm>

namespace {
QVariantMap normalizedItem(QVariantMap item)
{
    const auto kind = item.value("kind").toString();
    if (kind == "app") {
        const auto id = item.value("desktopId").toString();
        if (id.isEmpty() || id.contains('/') || !id.endsWith(".desktop")) return {};
        item["key"] = "app:" + id;
    } else if (kind == "file") {
        const auto path = item.value("path").toString();
        if (!QDir::isAbsolutePath(path)) return {};
        item["path"] = QDir::cleanPath(path);
        item["key"] = "file:" + QDir::cleanPath(path);
    } else return {};
    // Persist only data used for presentation and stable identity.
    return {{"kind", kind}, {"key", item.value("key").toString()}, {"desktopId", item.value("desktopId").toString()},
            {"path", item.value("path").toString()}, {"name", item.value("name").toString().left(256)},
            {"icon", item.value("icon").toString()}};
}
}

Groups::Groups(const QString &file, const QVariantList &favorites, QObject *parent)
    : QObject(parent), m_file(file)
{
    QVariantList initial;
    for (auto favorite : favorites) {
        auto item = favorite.toMap();
        item["kind"] = "app";
        initial.append(normalizedItem(item));
    }
    m_groups = {QVariantMap{{"id", "pinned"}, {"name", "Pinned"}, {"items", initial}}};
    QFile input(file);
    if (!input.exists()) return; // Migrate favorites only when no group store exists.
    if (!input.open(QIODevice::ReadOnly)) {
        m_readOnly = true;
        m_error = tr("Cannot read saved groups; changes are disabled.");
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parseError);
    const auto root = document.object();
    auto loaded = root.value("groups").toArray().toVariantList();
    QSet<QString> ids;
    bool valid = parseError.error == QJsonParseError::NoError && root.value("version").toInt() == 1 && !loaded.isEmpty();
    for (auto &value : loaded) {
        auto group = value.toMap();
        const auto id = group.value("id").toString();
        valid = valid && !id.isEmpty() && !ids.contains(id) && !group.value("name").toString().trimmed().isEmpty();
        ids.insert(id);
        if (id == "pinned") group["name"] = "Pinned";
        QVariantList items;
        QSet<QString> keys;
        for (const auto &stored : group.value("items").toList()) {
            const auto item = normalizedItem(stored.toMap());
            const auto key = item.value("key").toString();
            valid = valid && !item.isEmpty();
            if (!item.isEmpty() && !keys.contains(key)) { items.append(item); keys.insert(key); }
        }
        group["items"] = items;
        value = group;
    }
    if (!valid || !ids.contains("pinned")) {
        m_readOnly = true;
        m_error = tr("Saved groups are invalid; the original file has been preserved.");
        return;
    }
    m_groups = loaded;
    m_enabled = root.value("notificationsEnabled").toBool(true);
    m_baseline = root.value("baseline").toBool();
    for (const auto &id : root.value("knownApps").toArray()) m_known.insert(id.toString());
    m_suggestions = root.value("suggestions").toArray().toVariantList();
}

int Groups::groupIndex(const QString &id) const
{
    for (int i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].toMap().value("id").toString() == id) return i;
    return -1;
}

QVariantList Groups::groups() const
{
    auto result = m_groups;
    for (auto &value : result) {
        auto group = value.toMap();
        if (group.value("id") == "pinned") {
            group["name"] = tr("Pinned");
            value = group;
        }
    }
    return result;
}

QVariantList Groups::items() const
{
    const auto index = groupIndex(m_selected);
    return index < 0 ? QVariantList{} : m_groups[index].toMap().value("items").toList();
}

void Groups::select(const QString &id)
{
    if (id == m_selected || groupIndex(id) < 0) return;
    m_selected = id;
    Q_EMIT selectionChanged();
    Q_EMIT changed();
}

bool Groups::save()
{
    if (m_readOnly) { Q_EMIT changed(); return false; }
    auto known = m_known.values();
    std::sort(known.begin(), known.end());
    const QJsonObject root{{"version", 1}, {"groups", QJsonArray::fromVariantList(m_groups)},
                           {"notificationsEnabled", m_enabled}, {"baseline", m_baseline},
                           {"knownApps", QJsonArray::fromStringList(known)},
                           {"suggestions", QJsonArray::fromVariantList(m_suggestions)}};
    const auto data = QJsonDocument(root).toJson();
    QSaveFile output(m_file);
    if (!QDir().mkpath(QFileInfo(m_file).absolutePath()) || !output.open(QIODevice::WriteOnly)
        || output.write(data) != data.size() || !output.commit()) {
        m_error = tr("Could not save groups. Check the configuration folder is writable.");
        Q_EMIT changed();
        return false;
    }
    m_error.clear();
    return true;
}

bool Groups::commitGroups(const QVariantList &groups)
{
    const auto before = m_groups;
    m_groups = groups;
    if (!save()) { m_groups = before; Q_EMIT changed(); return false; }
    Q_EMIT changed();
    return true;
}

QString Groups::create(const QString &name)
{
    const auto label = name.trimmed().left(80);
    if (label.isEmpty()) return {};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto groups = m_groups;
    groups.append(QVariantMap{{"id", id}, {"name", label}, {"items", QVariantList{}}});
    if (!commitGroups(groups)) return {};
    select(id);
    return id;
}

bool Groups::rename(const QString &id, const QString &name)
{
    const auto index = groupIndex(id);
    const auto label = name.trimmed().left(80);
    if (id == "pinned" || index < 0 || label.isEmpty()) return false;
    auto groups = m_groups;
    auto group = groups[index].toMap();
    group["name"] = label;
    groups[index] = group;
    const auto proposals = m_suggestions;
    for (auto &value : m_suggestions) {
        auto proposal = value.toMap();
        if (proposal.value("groupId") == id) { proposal["groupName"] = label; value = proposal; }
    }
    if (commitGroups(groups)) return true;
    m_suggestions = proposals;
    Q_EMIT changed();
    return false;
}

bool Groups::remove(const QString &id)
{
    const auto index = groupIndex(id);
    if (id == "pinned" || index < 0) return false;
    auto groups = m_groups;
    groups.removeAt(index);
    const auto before = m_suggestions;
    m_suggestions.removeIf([&](const QVariant &item) { return item.toMap().value("groupId").toString() == id; });
    if (!commitGroups(groups)) { m_suggestions = before; return false; }
    if (m_selected == id) select("pinned");
    return true;
}

bool Groups::contains(const QString &id, const QString &key) const
{
    const auto index = groupIndex(id);
    if (index < 0 || key.isEmpty()) return false;
    for (const auto &item : m_groups[index].toMap().value("items").toList())
        if (item.toMap().value("key").toString() == key) return true;
    return false;
}

bool Groups::add(const QString &id, const QVariantMap &value)
{
    const auto index = groupIndex(id);
    const auto item = normalizedItem(value);
    if (index < 0 || item.isEmpty()) return false;
    if (contains(id, item.value("key").toString())) return true;
    auto groups = m_groups;
    auto group = groups[index].toMap();
    auto items = group.value("items").toList();
    items.append(item);
    group["items"] = items;
    groups[index] = group;
    const auto proposals = m_suggestions;
    m_suggestions.removeIf([&](const QVariant &s) {
        const auto proposal = s.toMap();
        return proposal.value("groupId") == id && proposal.value("item").toMap().value("key") == item.value("key");
    });
    if (commitGroups(groups)) {
        const auto name = id == "pinned" ? tr("Pinned") : group.value("name").toString();
        Q_EMIT itemAdded(tr("%1 added to %2 group").arg(item.value("name").toString(), name));
        return true;
    }
    m_suggestions = proposals;
    Q_EMIT changed();
    return false;
}

bool Groups::unpin(const QString &id, const QString &key)
{
    const auto index = groupIndex(id);
    if (index < 0) return false;
    auto groups = m_groups;
    auto group = groups[index].toMap();
    auto items = group.value("items").toList();
    items.removeIf([&](const QVariant &item) { return item.toMap().value("key").toString() == key; });
    group["items"] = items;
    groups[index] = group;
    return commitGroups(groups);
}

void Groups::setNotificationsEnabled(bool enabled)
{
    if (enabled == m_enabled) return;
    const auto before = m_enabled;
    m_enabled = enabled;
    if (!save() && enabled) { m_enabled = before; return; }
    // Enabling starts a fresh baseline; do not announce a backlog from the off period.
    m_resetBaseline = enabled;
    Q_EMIT notificationsEnabledChanged();
    Q_EMIT changed();
}

QVariantMap Groups::applicationItem(const ApplicationEntry &entry)
{
    return normalizedItem({{"kind", "app"}, {"desktopId", entry.desktopId},
                           {"name", entry.name}, {"icon", entry.icon}});
}

QStringList Groups::rolesFor(const ApplicationEntry &entry)
{
    // Exact known identities supplement optional desktop categories. In particular,
    // a Steam game shortcut must never inherit Steam's launcher role from Exec.
    const QSet<QString> launchers{"steam.desktop", "com.valvesoftware.Steam.desktop", "heroic.desktop",
        "com.heroicgameslauncher.hgl.desktop", "net.lutris.Lutris.desktop", "lutris.desktop"};
    QStringList roles;
    if (entry.categories.contains("LauncherStore") || launchers.contains(entry.desktopId)) roles.append("LauncherStore");
    const QSet<QString> windowsLaunchers{"eadesktop.exe", "ealauncher.exe", "battle.net.exe",
        "battle.net launcher.exe", "ubisoftconnect.exe", "upc.exe", "galaxyclient.exe"};
    for (auto token : QProcess::splitCommand(entry.command)) {
        token.replace('\\', '/');
        if (windowsLaunchers.contains(token.section('/', -1).toLower())) roles.append("LauncherStore");
    }
    // Wine/Lutris/Bottles can hide the executable behind an opaque shortcut ID.
    // Use only exact branded Windows app aliases; never substring matching.
    const QSet<QString> windowsNames{"ea app", "ea desktop", "battle.net", "ubisoft connect", "gog galaxy"};
    if (windowsNames.contains(entry.name.simplified().toCaseFolded())) roles.append("LauncherStore");
    const QSet<QString> precise{"WebBrowser", "Email", "IDE", "VectorGraphics", "RasterGraphics",
        "3DGraphics", "AudioVideoEditing", "Player", "WordProcessor", "Spreadsheet", "Presentation",
        "TerminalEmulator", "TextEditor", "FileManager", "Archiving"};
    for (const auto &category : entry.categories)
        if (precise.contains(category)) roles.append(category);
    roles.removeDuplicates();
    return roles;
}

void Groups::observeApplications(const QList<ApplicationEntry> &entries)
{
    QSet<QString> current;
    QHash<QString, ApplicationEntry> byId;
    for (const auto &entry : entries) { current.insert(entry.desktopId); byId.insert(entry.desktopId, entry); }
    if (!m_enabled || !m_baseline || m_resetBaseline) {
        const bool needsSave = current != m_known || !m_baseline || m_resetBaseline;
        m_known = current;
        m_baseline = true;
        m_resetBaseline = false;
        if (needsSave) save();
        return; // The bell gates all classification, not just notification display.
    }
    if (current == m_known) return; // Metadata updates are not installations.
    QHash<QString, QStringList> intents;
    for (const auto &value : m_groups) {
        const auto group = value.toMap();
        if (group.value("id") == "pinned") continue;
        QHash<QString, int> support;
        int seeds = 0;
        for (const auto &item : group.value("items").toList()) {
            const auto member = item.toMap();
            const auto memberId = member.value("desktopId").toString();
            if (member.value("kind") != "app" || !byId.contains(memberId)) continue;
            ++seeds;
            for (const auto &role : rolesFor(byId[memberId])) ++support[role];
        }
        for (auto it = support.cbegin(); it != support.cend(); ++it)
            if (it.value() >= 2 && it.value() * 3 >= seeds * 2)
                intents[group.value("id").toString()].append(it.key());
    }
    QVariantList newSuggestions;
    auto added = (current - m_known).values();
    std::sort(added.begin(), added.end());
    for (const auto &id : added) {
        const auto &entry = byId[id];
        const auto candidateRoles = rolesFor(entry);
        if (candidateRoles.isEmpty()) continue;
        for (const auto &value : m_groups) {
            const auto group = value.toMap();
            const auto groupId = group.value("id").toString();
            if (groupId == "pinned" || contains(groupId, "app:" + id)) continue;
            QString reason;
            // At least two independent examples, and a clear majority. Group
            // titles and broad Game/Utility categories never establish intent.
            for (const auto &role : candidateRoles)
                if (intents.value(groupId).contains(role)) { reason = role; break; }
            if (reason.isEmpty()) continue;
            const auto suggestionId = groupId + ':' + id;
            bool exists = false;
            for (const auto &s : m_suggestions) if (s.toMap().value("id") == suggestionId) { exists = true; break; }
            if (exists) continue;
            QVariantMap suggestion{{"id", suggestionId}, {"groupId", groupId}, {"groupName", group.value("name")},
                {"item", applicationItem(entry)}, {"reason", reason}, {"deferred", false}};
            m_suggestions.append(suggestion);
            newSuggestions.append(suggestion);
        }
    }
    m_known = current;
    // Stale proposals cannot add removed apps or resurrect deleted groups.
    m_suggestions.removeIf([&](const QVariant &value) {
        const auto s = value.toMap();
        return !current.contains(s.value("item").toMap().value("desktopId").toString()) || groupIndex(s.value("groupId").toString()) < 0;
    });
    if (!save()) return;
    Q_EMIT changed();
    for (const auto &suggestion : newSuggestions) Q_EMIT suggestionReady(suggestion.toMap());
}

void Groups::respond(const QString &id, const QString &action)
{
    if (action == "never") { setNotificationsEnabled(false); return; }
    if (!m_enabled || (action != "yes" && action != "later")) return;
    for (int i = 0; i < m_suggestions.size(); ++i) {
        auto suggestion = m_suggestions[i].toMap();
        if (suggestion.value("id") != id) continue;
        if (action == "yes") {
            if (!m_known.contains(suggestion.value("item").toMap().value("desktopId").toString())
                || !add(suggestion.value("groupId").toString(), suggestion.value("item").toMap())) return;
            const auto count = m_suggestions.size();
            m_suggestions.removeIf([&](const QVariant &s) { return s.toMap().value("id") == id; });
            if (m_suggestions.size() != count) { save(); Q_EMIT changed(); }
            return;
        } else {
            suggestion["deferred"] = true;
            m_suggestions.removeAt(i);
            m_suggestions.append(suggestion);
        }
        save();
        Q_EMIT changed();
        return;
    }
}
