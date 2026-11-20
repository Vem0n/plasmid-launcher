// SPDX-License-Identifier: GPL-3.0-or-later

#include "translations.h"
#include <QFile>
#include <QJsonDocument>

Translations::Translations(const QStringList &languages)
{
    auto preferences = languages;
    preferences.append("en");
    for (auto language : preferences) {
        language.replace('_', '-');
        // Follow uiLanguages() order, including its regional/base fallbacks.
        // Only bundled base catalogs are candidates; never form arbitrary paths.
        if (!QStringList{"en", "pl", "de", "fr", "es"}.contains(language)) continue;
        QFile file(":/i18n/" + language + ".json");
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto contexts = QJsonDocument::fromJson(file.readAll()).object();
        if (contexts.isEmpty()) continue;
        m_language = language;
        m_contexts = contexts;
        break;
    }
}

QString Translations::translate(const char *context, const char *source, const char *disambiguation, int n) const
{
    if (!context || !source || (disambiguation && *disambiguation)) return {};
    const auto message = m_contexts.value(QString::fromUtf8(context)).toObject().value(QString::fromUtf8(source));
    if (message.isString()) return message.toString();
    if (!message.isObject() || n < 0) return {}; // Null lets Qt use other translators/source text.
    QString form = "other";
    if (m_language == "pl") {
        if (n == 1) form = "one";
        else if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14)) form = "few";
        else form = "many";
    } else if (n == 1 || (m_language == "fr" && n == 0)) form = "one";
    return message.toObject().value(form).toString(); // Qt substitutes %n afterward.
}
