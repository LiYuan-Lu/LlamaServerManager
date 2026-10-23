#pragma once
#include <QJsonObject>
#include <QString>

// Agent-wide settings stored in settings.json ({"version":1, ...}) next to profiles.json.
struct AppSettings {
    static constexpr int Version = 1;
    int metricsIntervalSeconds = 2; // resource sampling period, 1-10 seconds
    QJsonObject toJson() const;
    // Missing fields keep their defaults; wrong types or out-of-range values are errors.
    static bool fromJson(const QJsonObject&, AppSettings&, QString& error);
};

class SettingsStore {
public:
    explicit SettingsStore(QString directory);
    // Invalid or unreadable files yield defaults and a warning; the file is not overwritten.
    AppSettings load(QString& warning) const;
    bool save(const AppSettings&, QString& error);

private:
    QString path;
};
