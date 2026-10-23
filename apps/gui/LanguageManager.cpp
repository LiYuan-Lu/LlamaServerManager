#include "LanguageManager.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>

namespace {
// GUI preferences are per user and independent of the Agent data directory.
QSettings guiSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, "LlamaServerManager", "llama-manager");
}
constexpr auto LanguageKey = "language";
}

LanguageManager::LanguageManager(QObject* parent)
    : QObject(parent)
{
    const QString saved = guiSettings().value(LanguageKey).toString();
    apply(saved.isEmpty() ? systemDefault() : normalized(saved));
}

QString LanguageManager::normalized(const QString& language)
{
    return language == "zh_TW" ? QStringLiteral("zh_TW") : QStringLiteral("en");
}

QString LanguageManager::systemDefault()
{
    const QLocale locale = QLocale::system();
    if (locale.language() != QLocale::Chinese) {
        return QStringLiteral("en");
    }
    const auto territory = locale.territory();
    const bool traditional = locale.script() == QLocale::TraditionalHanScript
        || territory == QLocale::Taiwan || territory == QLocale::HongKong || territory == QLocale::Macao;
    return traditional ? QStringLiteral("zh_TW") : QStringLiteral("en");
}

void LanguageManager::setLanguage(const QString& language)
{
    const QString next = normalized(language);
    if (next == m_language) {
        return;
    }
    guiSettings().setValue(LanguageKey, next);
    apply(next);
}

void LanguageManager::apply(const QString& language)
{
    QCoreApplication::removeTranslator(&m_appTranslator);
    QCoreApplication::removeTranslator(&m_qtTranslator);
    if (language != "en") {
        // Application strings are embedded by qt_add_translations under :/translations.
        if (m_appTranslator.load(":/translations/llama-manager_" + language)) {
            QCoreApplication::installTranslator(&m_appTranslator);
        }
        // Qt's own strings (dialogs, standard buttons) come from the Qt installation
        // or the translations folder that windeployqt places next to the executable.
        if (m_qtTranslator.load("qt_" + language, QLibraryInfo::path(QLibraryInfo::TranslationsPath))
            || m_qtTranslator.load("qt_" + language, QCoreApplication::applicationDirPath() + "/translations")) {
            QCoreApplication::installTranslator(&m_qtTranslator);
        }
    }
    m_language = language;
    if (m_engine) {
        m_engine->retranslate();
    }
    emit languageChanged();
}
