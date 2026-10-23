#include "OpencodeConfig.h"
#include "HealthMonitor.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <algorithm>

OpencodeConfig::OpencodeConfig(QObject* parent)
    : QObject(parent)
{
    // Always talk to the local server directly, never through a system proxy.
    network.setProxy(QNetworkProxy::NoProxy);
}
QUrl OpencodeConfig::baseUrl(const Profile& p)
{
    QUrl url;
    url.setScheme("http");
    url.setHost(HealthMonitor::checkHost(p.host));
    url.setPort(p.port);
    url.setPath("/v1");
    return url;
}
QString OpencodeConfig::apiKey(const Profile& p)
{
    for (int i = 0; i < p.extraArguments.size(); ++i) {
        const auto& arg = p.extraArguments[i];
        if (arg == "--api-key") {
            // llama-server accepts a comma-separated list; any of the keys works.
            return p.extraArguments.value(i + 1).section(',', 0, 0);
        }
        if (arg.startsWith("--api-key=")) {
            return arg.mid(int(qstrlen("--api-key="))).section(',', 0, 0);
        }
    }
    return { };
}
QJsonObject OpencodeConfig::build(const Profile& p, const QJsonObject& modelList, const QJsonObject& serverProps, QString& error)
{
    QStringList ids;
    for (const auto& v : modelList["data"].toArray()) {
        const auto id = v.toObject()["id"].toString();
        if (!id.isEmpty() && !ids.contains(id)) {
            ids << id;
        }
    }
    if (ids.isEmpty()) {
        error = "The server did not report a model ID on /v1/models";
        return { };
    }
    // Context available to one request: /props reports it per slot. Without /props, assume the
    // context is split evenly between the parallel slots.
    int context = serverProps["default_generation_settings"].toObject()["n_ctx"].toInt();
    if (context <= 0) {
        context = serverProps["n_ctx"].toInt();
    }
    if (context <= 0) {
        context = std::max(1, p.context / std::max(1, p.parallel));
    }
    // opencode keeps this many tokens free for the reply.
    const int output = std::max(1, std::min(8192, context / 2));
    const auto name = p.displayName();
    QJsonObject entries;
    for (const auto& id : ids) {
        entries[id] = QJsonObject {
            { "name", ids.size() == 1 ? name : QString("%1 (%2)").arg(name, id) },
            { "limit", QJsonObject { { "context", context }, { "output", output } } },
        };
    }
    QJsonObject options { { "baseURL", baseUrl(p).toString() } };
    if (const auto key = apiKey(p); !key.isEmpty()) {
        options["apiKey"] = key;
    }
    const QJsonObject provider {
        { "npm", "@ai-sdk/openai-compatible" },
        { "name", name },
        { "options", options },
        { "models", entries },
    };
    return {
        { "$schema", "https://opencode.ai/config.json" },
        { "provider", QJsonObject { { QString("llamacpp-%1").arg(p.port), provider } } },
    };
}
QNetworkReply* OpencodeConfig::get(const QString& path, int timeoutMs)
{
    QUrl url = baseUrl(profile);
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (const auto key = apiKey(profile); !key.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    }
    auto* reply = network.get(request);
    ++remaining;
    connect(reply, &QNetworkReply::finished, this, &OpencodeConfig::done);
    return reply;
}
void OpencodeConfig::fetch(const Profile& p, int timeoutMs)
{
    profile = p;
    modelsReply = get("/v1/models", timeoutMs);
    propsReply = get("/props", timeoutMs);
}
void OpencodeConfig::done()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto body = QJsonDocument::fromJson(reply->readAll()).object();
    if (reply == modelsReply) {
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            modelsError = "GET /v1/models failed: " + reply->errorString();
        } else if (status != 200) {
            modelsError = QString("GET /v1/models returned HTTP %1").arg(status);
        } else {
            models = body;
        }
    } else if (status == 200) {
        props = body; // optional: build() falls back to the profile's context
    }
    reply->deleteLater();
    if (--remaining > 0) {
        return;
    }
    QString error = modelsError;
    QJsonObject config;
    if (error.isEmpty()) {
        config = build(profile, models, props, error);
    }
    emit finished(profile.id, config, error);
}
