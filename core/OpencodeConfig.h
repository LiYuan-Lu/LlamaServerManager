#pragma once
#include "Profile.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QNetworkReply;

// Builds an opencode provider config for a running llama-server. fetch() reads the model IDs
// from GET /v1/models and the per-request context size from GET /props, then emits finished()
// once. Each object serves a single fetch.
class OpencodeConfig : public QObject {
    Q_OBJECT
public:
    explicit OpencodeConfig(QObject* parent = nullptr);
    void fetch(const Profile& profile, int timeoutMs = 5000);
    // The config for `models` (a /v1/models reply) and `props` (a /props reply, may be empty).
    // Returns an empty object and sets `error` when the reply lists no model ID.
    static QJsonObject build(const Profile& profile, const QJsonObject& models, const QJsonObject& props, QString& error);
    // http://host:port/v1, with wildcard hosts mapped to loopback.
    static QUrl baseUrl(const Profile& profile);
signals:
    void finished(const QString& profileId, const QJsonObject& config, const QString& error);

private:
    QNetworkReply* get(const QString& path, int timeoutMs);
    void done();
    QNetworkAccessManager network;
    Profile profile;
    QNetworkReply* modelsReply = nullptr;
    QNetworkReply* propsReply = nullptr;
    QJsonObject models, props;
    QString modelsError;
    int remaining = 0;
};
