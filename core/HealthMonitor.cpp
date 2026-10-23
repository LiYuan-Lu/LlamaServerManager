#include "HealthMonitor.h"
#include <QHostAddress>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>

HealthMonitor::HealthMonitor(QObject *parent) : QObject(parent) {
    // Health checks always target the local server directly, never a system proxy.
    network.setProxy(QNetworkProxy::NoProxy);
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &HealthMonitor::check);
}
HealthMonitor::~HealthMonitor() { stop(); }
QString HealthMonitor::checkHost(const QString &host) {
    const QHostAddress address(host);
    if (address == QHostAddress::AnyIPv4) return "127.0.0.1";
    if (address == QHostAddress::AnyIPv6) return "::1";
    return host;
}
void HealthMonitor::start(quint64 gen, const QString &host, int port, int intervalMs, int timeoutMs, int firstDelayMs) {
    stop();
    generation = gen; interval = intervalMs; timeout = timeoutMs; active = true;
    url = QUrl(); url.setScheme("http"); url.setHost(checkHost(host)); url.setPort(port); url.setPath("/health");
    timer.start(qMax(0, firstDelayMs));
}
void HealthMonitor::stop() {
    active = false;
    timer.stop();
    if (reply) {
        // Disconnect first: abort() emits finished() synchronously.
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
    }
}
void HealthMonitor::check() {
    if (!active || reply) return;
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    reply = network.get(request);
    const quint64 gen = generation;
    connect(reply, &QNetworkReply::finished, this, [this, gen] {
        auto *finished = reply; reply = nullptr;
        const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Outcome outcome = Outcome::Failure; QString detail;
        if (status == 200) { outcome = Outcome::Healthy; detail = "HTTP 200"; }
        else if (status == 503) { outcome = Outcome::Loading; detail = "HTTP 503"; }
        else if (status > 0) detail = QString("HTTP %1").arg(status);
        else if (finished->error() == QNetworkReply::OperationCanceledError || finished->error() == QNetworkReply::TimeoutError)
            detail = QString("timed out after %1 ms").arg(timeout);
        else if (finished->error() == QNetworkReply::ConnectionRefusedError) detail = "connection refused";
        else detail = finished->errorString();
        finished->deleteLater();
        if (active) timer.start(interval);
        emit result(gen, outcome, status, detail);
    });
}
