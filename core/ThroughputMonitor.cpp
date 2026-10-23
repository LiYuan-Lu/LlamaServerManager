#include "ThroughputMonitor.h"
#include "HealthMonitor.h"
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <cmath>

void ThroughputMeter::add(const QJsonArray& slotList, qint64 timeMs)
{
    QHash<int, Slot> next;
    qint64 tokens = 0;
    active = 0;
    count = 0;
    for (const auto& value : slotList) {
        const auto o = value.toObject();
        const int id = o["id"].toInt(-1);
        if (id < 0) {
            continue;
        }
        ++count;
        // Older servers report next_token as a one-element array and the slot state as a number.
        const auto token = o["next_token"].isArray() ? o["next_token"].toArray().first().toObject() : o["next_token"].toObject();
        const bool processing = o.contains("is_processing") ? o["is_processing"].toBool() : o["state"].toInt() != 0;
        if (processing) {
            ++active;
        }
        Slot slot { o["id_task"].toInteger(-1), token["n_decoded"].toInteger() };
        const auto before = previous.constFind(id);
        if (before == previous.cend()) {
            next.insert(id, slot); // first sample: tokens generated before monitoring are not counted
            continue;
        }
        if (slot.task != before->task) {
            // n_decoded restarts at 0 only when the new task's prompt is processed; until then it
            // still holds the previous task's count. A smaller value is already the new count.
            if (slot.decoded < before->decoded) {
                tokens += slot.decoded;
            } else {
                slot.pending = true;
            }
        } else if (before->pending) {
            // A stale counter does not move, so any change means the new task is counting.
            if (slot.decoded == before->decoded) {
                slot.pending = true;
            } else {
                tokens += slot.decoded;
            }
        } else {
            tokens += slot.decoded >= before->decoded ? slot.decoded - before->decoded : slot.decoded;
        }
        next.insert(id, slot);
    }
    previous = next;
    const qint64 elapsed = lastTime < 0 ? 0 : timeMs - lastTime;
    lastTime = timeMs;
    rate = elapsed > 0 ? tokens * 1000.0 / elapsed : 0;
    total += tokens;
    // A burst lasts while tokens are being generated; its average becomes the "last" speed.
    if (tokens > 0 && elapsed > 0) {
        burstTokens += tokens;
        burstMs += elapsed;
    }
    if ((active == 0 || tokens == 0) && burstTokens > 0) {
        lastRate = burstTokens * 1000.0 / burstMs;
        burstTokens = 0;
        burstMs = 0;
    }
}

ThroughputMonitor::ThroughputMonitor(QObject* parent)
    : QObject(parent)
{
    // Always talk to the local server directly, never through a system proxy.
    network.setProxy(QNetworkProxy::NoProxy);
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &ThroughputMonitor::poll);
    clock.start();
}
ThroughputMonitor::~ThroughputMonitor() { stop(); }
void ThroughputMonitor::start(const QString& host, int port, const QString& apiKey, int intervalMs, int timeoutMs)
{
    stop();
    url = QUrl();
    url.setScheme("http");
    url.setHost(HealthMonitor::checkHost(host));
    url.setPort(port);
    url.setPath("/slots");
    authorization = apiKey.isEmpty() ? QByteArray() : "Bearer " + apiKey.toUtf8();
    timeout = timeoutMs;
    timer.setInterval(intervalMs);
    meter.reset();
    reason = "Waiting for /slots";
    available = false;
    active = true;
    timer.start();
    emit updated();
}
void ThroughputMonitor::stop()
{
    active = false;
    timer.stop();
    if (reply) {
        // Disconnect first: abort() emits finished() synchronously.
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
    }
    if (available || reason != "Not running") {
        available = false;
        reason = "Not running";
        meter.reset();
        emit updated();
    }
}
void ThroughputMonitor::poll()
{
    if (!active || reply) {
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (!authorization.isEmpty()) {
        request.setRawHeader("Authorization", authorization);
    }
    reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, &ThroughputMonitor::finished);
}
void ThroughputMonitor::finished()
{
    auto* done = reply;
    reply = nullptr;
    done->deleteLater();
    const int status = done->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto body = QJsonDocument::fromJson(done->readAll());
    if (status == 200 && body.isArray()) {
        meter.add(body.array(), clock.elapsed());
        available = true;
        reason.clear();
    } else if (status == 501 || status == 404) {
        // Started with --no-slots: nothing to poll until the next start.
        available = false;
        reason = "The server's /slots endpoint is disabled";
        meter.reset();
        active = false;
        emit updated();
        return;
    } else {
        // Loading (503), busy, or not reachable yet: try again. The next good sample covers
        // the whole gap, so no tokens are lost.
        available = false;
        reason = status > 0 ? QString("/slots returned HTTP %1").arg(status) : "Waiting for /slots: " + done->errorString();
    }
    emit updated();
    if (active) {
        timer.start();
    }
}
QJsonObject ThroughputMonitor::status() const
{
    const auto rounded = [](double value) { return std::round(value * 10) / 10; };
    return {
        { "available", available },
        { "reason", reason },
        { "tokensPerSecond", available ? rounded(meter.tokensPerSecond()) : 0.0 },
        { "lastTokensPerSecond", rounded(meter.lastTokensPerSecond()) },
        { "generatedTokens", meter.generatedTokens() },
        { "activeSlots", meter.activeSlots() },
        { "slots", meter.slotCount() },
    };
}
