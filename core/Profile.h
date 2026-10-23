#pragma once
#include <QJsonObject>
#include <QStringList>

struct Profile {
    QString executable, model, workingDirectory;
    QString host = "127.0.0.1";
    int port = 8080, context = 4096, gpuLayers = 0, threads = 4, parallel = 1;
    bool metrics = false;
    QStringList extraArguments;
    QJsonObject environment;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &, Profile &, QString &error);
    QString validate(bool checkFiles = true) const;
    QStringList arguments() const;
};
