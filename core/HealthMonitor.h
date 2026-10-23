#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// Polls GET http://host:port/health asynchronously. At most one request is in flight;
// the next check is scheduled only after the previous one finished. Every result carries
// the generation passed to start(), so callers can drop results that belong to an older process.
class HealthMonitor : public QObject {
    Q_OBJECT
public:
    enum class Outcome { Healthy, Loading, Failure };
    Q_ENUM(Outcome)
    explicit HealthMonitor(QObject *parent = nullptr);
    ~HealthMonitor() override;
    void start(quint64 generation, const QString &host, int port, int intervalMs, int timeoutMs, int firstDelayMs = 0);
    void setInterval(int intervalMs) { interval = intervalMs; }
    void stop();
    bool running() const { return active; }
    bool requestInFlight() const { return reply != nullptr; }
    static QString checkHost(const QString &host);
signals:
    void result(quint64 generation, HealthMonitor::Outcome outcome, int httpStatus, const QString &detail);
private:
    void check();
    QNetworkAccessManager network;
    QTimer timer;
    QNetworkReply *reply = nullptr;
    QUrl url;
    quint64 generation = 0;
    int interval = 30000, timeout = 5000;
    bool active = false;
};
