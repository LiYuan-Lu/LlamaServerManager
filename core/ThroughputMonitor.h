#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

// Generation speed from llama-server's slot counters. Each slot reports the task it serves
// (id_task) and how many tokens that task has generated (next_token.n_decoded); the growth
// between two samples is the number of tokens generated in between.
class ThroughputMeter {
public:
    // Adds one GET /slots reply taken at `timeMs`. The first sample only sets the baseline.
    void add(const QJsonArray& slotList, qint64 timeMs);
    void reset() { *this = ThroughputMeter(); }
    double tokensPerSecond() const { return rate; }
    // Average speed of the latest finished generation burst; 0 until one finished.
    double lastTokensPerSecond() const { return lastRate; }
    qint64 generatedTokens() const { return total; }
    int activeSlots() const { return active; }
    int slotCount() const { return count; }

private:
    struct Slot {
        qint64 task = -1, decoded = 0;
        bool pending = false; // task changed; decoded may still be the previous task's count
    };
    QHash<int, Slot> previous;
    qint64 lastTime = -1, total = 0, burstTokens = 0, burstMs = 0;
    double rate = 0, lastRate = 0;
    int active = 0, count = 0;
};

// Polls GET http://host:port/slots once per interval while started. At most one request is in
// flight. A server started with --no-slots (HTTP 501/404) stops the polling until the next start.
class ThroughputMonitor : public QObject {
    Q_OBJECT
public:
    explicit ThroughputMonitor(QObject* parent = nullptr);
    ~ThroughputMonitor() override;
    void start(const QString& host, int port, const QString& apiKey, int intervalMs = 1000, int timeoutMs = 5000);
    void stop();
    // {available, reason, tokensPerSecond, lastTokensPerSecond, generatedTokens, activeSlots, slots}
    QJsonObject status() const;
signals:
    void updated();

private:
    void poll();
    void finished();
    QNetworkAccessManager network;
    QTimer timer;
    QElapsedTimer clock;
    QNetworkReply* reply = nullptr;
    QUrl url;
    QByteArray authorization;
    ThroughputMeter meter;
    QString reason = "Not running";
    int timeout = 5000;
    bool active = false, available = false;
};
