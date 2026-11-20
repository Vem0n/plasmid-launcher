// SPDX-License-Identifier: GPL-3.0-or-later

#include "translations.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTest>
#include <QTextStream>

class TranslationsTest : public QObject
{
    Q_OBJECT
    static QStringList placeholders(const QString &text)
    {
        QStringList result;
        auto matches = QRegularExpression("%L?(?:n|[1-9][0-9]*)").globalMatch(text);
        while (matches.hasNext()) result.append(matches.next().captured());
        result.sort();
        return result;
    }
    static QJsonObject catalog(const QString &language)
    {
        QFile file(":/i18n/" + language + ".json");
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
private Q_SLOTS:
    void catalogsAreCompleteAndPreserveParameters()
    {
        const auto english = catalog("en");
        QVERIFY(!english.isEmpty());
        for (const auto &language : {"en", "pl", "de", "fr", "es"}) {
            const auto localized = catalog(language);
            QCOMPARE(localized.keys(), english.keys());
            for (const auto &context : english.keys()) {
                const auto messages = localized.value(context).toObject();
                QCOMPARE(messages.keys(), english.value(context).toObject().keys());
                for (auto it = messages.begin(); it != messages.end(); ++it) {
                    const auto expected = placeholders(it.key());
                    if (it.value().isObject()) {
                        const auto forms = it.value().toObject();
                        QCOMPARE(forms.keys(), QString(language) == "pl" ? (QStringList{"few", "many", "one"}) : (QStringList{"one", "other"}));
                        for (const auto &form : forms) QCOMPARE(placeholders(form.toString()), expected);
                    } else {
                        QVERIFY2(!it.value().toString().isEmpty(), qPrintable(context + ": " + it.key()));
                        QCOMPARE(placeholders(it.value().toString()), expected);
                    }
                }
            }
        }
    }

    void fallbackAndContextIsolation()
    {
        Translations unsupported({"ja-JP", "ja"});
        QCOMPARE(unsupported.selectedLanguage(), QString("en"));
        Translations englishFirst({"en", "pl"});
        QCOMPARE(englishFirst.selectedLanguage(), QString("en"));
        Translations preferred({"xx", "fr", "pl"});
        QCOMPARE(preferred.selectedLanguage(), QString("fr"));
        QVERIFY(preferred.translate("OtherApplication", "Applications").isNull());
        QVERIFY(preferred.translate("LauncherPanel", "Unknown string").isNull());
    }

    void polishPluralForms()
    {
        Translations polish(QLocale("pl_PL").uiLanguages());
        QVERIFY(qApp->installTranslator(&polish));
        const QList<QPair<int, QString>> cases{{0,"elementów"}, {1,"element"}, {2,"elementy"}, {4,"elementy"},
            {5,"elementów"}, {12,"elementów"}, {14,"elementów"}, {21,"elementów"}, {22,"elementy"}, {101,"elementów"}, {102,"elementy"}};
        for (const auto &[count, word] : cases)
            QCOMPARE(QCoreApplication::translate("GroupSection", "%n items", nullptr, count), QString::number(count) + ' ' + word);
        QCOMPARE(QCoreApplication::translate("ApplicationsView", "%n applications", nullptr, 22), QString("22 aplikacje"));
        QCOMPARE(QCoreApplication::translate("Groups", "%1 added to %2 group").arg("Steam", "Moje gry"), QString("Dodano Steam do grupy Moje gry"));
        qApp->removeTranslator(&polish);
    }

    void systemLanguage_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("languagePreferences");
        QTest::newRow("US English") << "en_US.UTF-8" << "en\nSystem Notifications" << QString();
        QTest::newRow("Polish") << "pl_PL.UTF-8" << "pl\nPowiadomienia systemowe" << QString();
        QTest::newRow("Swiss German") << "de_CH.UTF-8" << "de\nSystembenachrichtigungen" << QString();
        QTest::newRow("Canadian French") << "fr_CA.UTF-8" << "fr\nNotifications système" << QString();
        QTest::newRow("Mexican Spanish") << "es_MX.UTF-8" << "es\nNotificaciones del sistema" << QString();
        QTest::newRow("Unsupported Japanese") << "ja_JP.UTF-8" << "en\nSystem Notifications" << QString();
        QTest::newRow("C locale") << "C.UTF-8" << "en\nSystem Notifications" << QString();
        QTest::newRow("Preferred UI language") << "en_US.UTF-8" << "pl\nPowiadomienia systemowe" << "pl:en";
        QTest::newRow("Ordered language fallback") << "en_US.UTF-8" << "de\nSystembenachrichtigungen" << "ja:de:en";
    }
    void systemLanguage()
    {
        QFETCH(QString, locale);
        QFETCH(QString, expected);
        QFETCH(QString, languagePreferences);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LC_ALL", locale);
        environment.insert("LANG", locale);
        environment.insert("LC_MESSAGES", locale);
        environment.remove("LANGUAGE");
        if (!languagePreferences.isEmpty()) environment.insert("LANGUAGE", languagePreferences);
        environment.insert("PLASMID_TRANSLATION_PROBE", "1");
        QProcess child;
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath());
        QVERIFY(child.waitForFinished(3000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(child.readAllStandardOutput()), expected);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariable("PLASMID_TRANSLATION_PROBE") == "1") {
        Translations translations;
        app.installTranslator(&translations);
        QTextStream(stdout) << translations.selectedLanguage() << '\n'
            << QCoreApplication::translate("LauncherPanel", "System Notifications");
        return 0;
    }
    TranslationsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "translations_test.moc"
