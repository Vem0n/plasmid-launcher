// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QLocale>
#include <QTranslator>

// Embedded, immutable catalogs used by the ordinary Qt tr()/qsTr() pipeline.
// No process-wide locale changes or dependency on build-time Linguist tools.
class Translations final : public QTranslator
{
public:
    explicit Translations(const QStringList &languages = QLocale::system().uiLanguages());
    bool isEmpty() const override { return m_contexts.isEmpty(); }
    QString translate(const char *context, const char *source, const char *disambiguation = nullptr, int n = -1) const override;
    QString selectedLanguage() const { return m_language; }
private:
    QString m_language;
    QJsonObject m_contexts;
};
