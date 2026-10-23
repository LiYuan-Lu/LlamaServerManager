#include "AppSettings.h"
#include "MetricsCollector.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

QJsonObject AppSettings::toJson() const
{
    return { { "metricsIntervalSeconds", metricsIntervalSeconds } };
}

bool AppSettings::fromJson(const QJsonObject& o, AppSettings& settings, QString& error)
{
    AppSettings candidate;
    if (o.contains("metricsIntervalSeconds")) {
        const auto value = o["metricsIntervalSeconds"];
        const double number = value.toDouble();
        if (!value.isDouble() || number != double(int(number)) || number < MetricsCollector::MinIntervalSeconds
            || number > MetricsCollector::MaxIntervalSeconds) {
            error = QString("metricsIntervalSeconds must be an integer between %1 and %2")
                        .arg(MetricsCollector::MinIntervalSeconds)
                        .arg(MetricsCollector::MaxIntervalSeconds);
            return false;
        }
        candidate.metricsIntervalSeconds = int(number);
    }
    settings = candidate;
    return true;
}

SettingsStore::SettingsStore(QString directory)
    : path(directory + "/settings.json")
{
    QDir().mkpath(directory);
}

AppSettings SettingsStore::load(QString& warning) const
{
    AppSettings settings;
    QFile file(path);
    if (!file.exists()) {
        return settings;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        warning = "Cannot read settings.json; using default settings";
        return settings;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    QString error;
    if (!doc.isObject() || doc.object()["version"].toInt() != AppSettings::Version
        || !AppSettings::fromJson(doc.object()["settings"].toObject(), settings, error)) {
        warning = "Invalid settings.json; using default settings" + (error.isEmpty() ? QString() : ": " + error);
        return AppSettings { };
    }
    return settings;
}

bool SettingsStore::save(const AppSettings& settings, QString& error)
{
    const auto bytes = QJsonDocument(QJsonObject { { "version", AppSettings::Version }, { "settings", settings.toJson() } }).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
