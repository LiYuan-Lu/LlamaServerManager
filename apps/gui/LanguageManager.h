#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

class QQmlEngine;

// Switches the GUI language at runtime using Qt's translation system.
// UI strings are marked with qsTr() in QML; translations live in
// translations/llama-manager_<lang>.ts and are compiled into the executable as .qm.
class LanguageManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)

public:
    explicit LanguageManager(QObject* parent = nullptr);

    QString language() const { return m_language; }
    void setEngine(QQmlEngine* engine) { m_engine = engine; }

    // Accepts "en" or "zh_TW"; persists the choice and retranslates QML.
    Q_INVOKABLE void setLanguage(const QString& language);

signals:
    void languageChanged();

private:
    void apply(const QString& language);
    static QString normalized(const QString& language);
    static QString systemDefault();

    QString m_language;
    QTranslator m_appTranslator;
    QTranslator m_qtTranslator;
    QQmlEngine* m_engine = nullptr;
};
