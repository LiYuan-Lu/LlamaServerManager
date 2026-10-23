#pragma once
#include <QJsonObject>
#include <QStringList>

// Per-model watchdog settings. Durations are in seconds.
struct WatchdogSettings {
    bool autoRestart = true;     // restart after crashes, startup failures, and failed health checks
    bool healthCheck = true;     // poll GET /health; STARTING lasts until it returns 200
    int intervalSeconds = 30;    // health check interval while running
    int timeoutSeconds = 5;      // HTTP request timeout
    int failureThreshold = 3;    // consecutive failed checks before UNHEALTHY
    int startupGraceSeconds = 600; // time allowed for the model to load
    int cooldownSeconds = 5;     // delay before the first automatic restart; doubles after each
    int maxBackoffSeconds = 300; // upper bound for the restart delay
    int maxRestarts = 3;         // automatic restarts allowed within windowSeconds
    int windowSeconds = 600;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, WatchdogSettings &, QString &error);
    QString validate() const;
};

struct Profile {
    QString id, name;
    QString executable, model, workingDirectory;
    QString host = "127.0.0.1";
    int port = 8080, context = 4096, gpuLayers = 0, threads = 4, parallel = 1;
    bool metrics = false;
    QStringList extraArguments;
    QJsonObject environment;
    WatchdogSettings watchdog;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, Profile &, QString &error);
    QString validate(bool checkFiles = true) const;
    QStringList arguments() const;
    QString displayName() const;
    static QString newId();
    static bool validId(const QString &id);
};
