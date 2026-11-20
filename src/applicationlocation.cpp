// SPDX-License-Identifier: GPL-3.0-or-later

#include "applicationlocation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>
#include <utility>

namespace {
// Steam uses text KeyValues for libraryfolders.vdf and appmanifest_*.acf.
// Parse objects, quoted/unquoted strings and // comments; reject malformed data.
class KeyValues
{
public:
    explicit KeyValues(QString text) : m_text(std::move(text)) {}
    QVariantMap parse() { return object(false, 0); }
    bool valid() const { return m_valid; }
private:
    QString token()
    {
        while (m_pos < m_text.size()) {
            if (m_text[m_pos].isSpace() || m_text[m_pos] == QChar(0xfeff)) { ++m_pos; continue; }
            if (m_text.mid(m_pos, 2) == "//") {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
                continue;
            }
            break;
        }
        if (m_pos == m_text.size()) return {};
        const auto first = m_text[m_pos++];
        if (first == '{' || first == '}') return first;
        QString result = QStringLiteral("");
        if (first == '"') {
            while (m_pos < m_text.size()) {
                auto character = m_text[m_pos++];
                if (character == '"') return result;
                if (character == '\\' && m_pos < m_text.size()) {
                    const auto next = m_text[m_pos++];
                    if (next == '"' || next == '\\') result += next;
                    else { result += character; result += next; }
                } else result += character;
            }
            m_valid = false;
        } else {
            result += first;
            while (m_pos < m_text.size() && !m_text[m_pos].isSpace() && m_text[m_pos] != '{' && m_text[m_pos] != '}')
                result += m_text[m_pos++];
        }
        return result;
    }
    QVariantMap object(bool nested, int depth)
    {
        QVariantMap result;
        if (depth > 32) { m_valid = false; return {}; }
        while (m_valid) {
            const auto key = token();
            if (key.isNull()) { if (nested) m_valid = false; break; }
            if (key == "}") { if (!nested) m_valid = false; return result; }
            if (key == "{" || key.isEmpty()) { m_valid = false; break; }
            const auto value = token();
            if (value.isNull() || value == "}") { m_valid = false; break; }
            result.insert(key, value == "{" ? QVariant(object(true, depth + 1)) : QVariant(value));
        }
        return result;
    }
    QString m_text;
    qsizetype m_pos = 0;
    bool m_valid = true;
};

QVariantMap readKeyValues(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) return {};
    KeyValues parser(QString::fromUtf8(file.readAll()));
    const auto result = parser.parse();
    return parser.valid() ? result : QVariantMap();
}

QString gameDirectory(const QString &appId, const QStringList &roots)
{
    QStringList libraries;
    for (const auto &root : roots) {
        libraries.append(root);
        const auto entries = readKeyValues(root + "/steamapps/libraryfolders.vdf").value("libraryfolders").toMap();
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            bool numeric = false;
            it.key().toUInt(&numeric);
            if (!numeric) continue;
            libraries.append(it.value().metaType() == QMetaType::fromType<QVariantMap>()
                                 ? it.value().toMap().value("path").toString() : it.value().toString());
        }
    }
    QStringList seen;
    for (const auto &library : std::as_const(libraries)) {
        const auto canonical = QFileInfo(library).canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical)) continue;
        seen.append(canonical);
        const auto state = readKeyValues(canonical + "/steamapps/appmanifest_" + appId + ".acf").value("AppState").toMap();
        if (state.value("appid").toString() != appId) continue;
        const auto directory = state.value("installdir").toString();
        if (directory.isEmpty() || QDir::isAbsolutePath(directory) || directory.split('/').contains("..")) continue;
        const auto path = QDir::cleanPath(canonical + "/steamapps/common/" + directory);
        if (QFileInfo(path).isDir()) return path;
    }
    return {};
}
}

QStringList steamInstallationRoots()
{
    const auto home = QDir::homePath();
    return {QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Steam",
            home + "/.steam/steam", home + "/.steam/root",
            home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"};
}

ApplicationLocation resolveApplicationLocation(const ApplicationEntry &entry, const QStringList &steamRoots)
{
    if (!entry.libraryId.isEmpty()) {
        const auto current = validateLutrisEntry(entry);
        const auto directory = current ? current->workingDirectory : QString();
        return {QFileInfo(directory).isDir() ? directory : QString(),
                QObject::tr("Lutris installation folder")};
    }
    // These tokens are used solely to locate a file, never for execution.
    auto tokens = QProcess::splitCommand(entry.command);
    for (const auto &token : std::as_const(tokens)) {
        const QUrl url(token);
        if (url.scheme() != "steam" || (url.host() != "rungameid" && url.host() != "run")) continue;
        const auto appId = url.path().section('/', 1, 1);
        if (!QRegularExpression("^[0-9]+$").match(appId).hasMatch()) continue;
        const auto path = gameDirectory(appId, steamRoots);
        return {path, path.isEmpty() ? QObject::tr("Game folder unavailable") : QObject::tr("Game folder")};
    }
    if (QDir::isAbsolutePath(entry.workingDirectory) && QFileInfo(entry.workingDirectory).isDir())
        return {entry.workingDirectory, QObject::tr("Working folder")};
    if (!entry.flatpakId.isEmpty() && !entry.flatpakId.contains('/') && entry.flatpakId != "..") {
        const auto path = QDir::homePath() + "/.var/app/" + entry.flatpakId;
        if (QFileInfo(path).isDir()) return {path, QObject::tr("Application data")};
    }
    if (!tokens.isEmpty() && QFileInfo(tokens.first()).fileName() == "env") {
        tokens.removeFirst();
        while (!tokens.isEmpty() && tokens.first().contains('=') && !tokens.first().startsWith('-')) tokens.removeFirst();
    }
    if (!tokens.isEmpty()) {
        const auto command = tokens.first();
        // Opaque wrapper commands do not identify the application's own files.
        const QStringList wrappers{"env", "sh", "bash", "dash", "zsh", "flatpak", "steam", "wine", "wine64"};
        if (!wrappers.contains(QFileInfo(command).fileName())) {
            const auto executable = QStandardPaths::findExecutable(command);
            if (!executable.isEmpty()) return {executable, QObject::tr("Executable")};
        }
    }
    return {QFileInfo::exists(entry.path) ? entry.path : QString(), QObject::tr("Desktop entry")};
}
