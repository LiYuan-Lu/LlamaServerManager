#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <functional>

class MetricsSampler;

// Samples system CPU/RAM, NVIDIA GPUs, and the managed llama-server processes on a worker
// thread every intervalSeconds, so slow system or NVML calls never block the Agent's IPC.
// Keeps a bounded history of the last HistorySeconds for the GUI charts.
class MetricsCollector : public QObject {
    Q_OBJECT
public:
    static constexpr int HistorySeconds = 300;
    static constexpr int MaxHistoryPoints = 600;
    static constexpr int MinIntervalSeconds = 1;
    static constexpr int MaxIntervalSeconds = 10;
    struct Target {
        QString profileId;
        qint64 pid = 0;
    };
    // Returns the processes to sample; called on the collector's thread before each sample.
    using TargetProvider = std::function<QList<Target>()>;
    explicit MetricsCollector(TargetProvider targets, const QString& nvmlLibrary = { }, QObject* parent = nullptr);
    ~MetricsCollector() override;
    void start();
    void setIntervalSeconds(int seconds);
    int intervalSeconds() const { return interval; }
    // Test hook: sampling period in milliseconds per configured second (default 1000).
    void setTimeScale(int millisecondsPerSecond);
    QJsonObject latest() const { return last; }
    // Compact history: [{time, cpu, ramUsed, ramTotal, gpus: [{util, memUsed, memTotal}]}], oldest first.
    QJsonArray history() const { return points; }
    // Reduces a full sample to one history point.
    static QJsonObject compact(const QJsonObject& sample);
    // Removes points older than HistorySeconds before `nowMs` and keeps at most MaxHistoryPoints.
    template <typename List, typename Time>
    static void prune(List& list, qint64 nowMs, Time timeOf)
    {
        while (!list.isEmpty() && (list.size() > MaxHistoryPoints || nowMs - timeOf(list.first()) > HistorySeconds * 1000LL)) {
            list.removeFirst();
        }
    }
signals:
    // A complete sample: time, intervalSeconds, system, gpu, processes (keyed by profile ID).
    void sampled(const QJsonObject& sample);

private:
    void tick();
    void accept(const QJsonObject& sample);
    TargetProvider targets;
    QThread thread;
    MetricsSampler* sampler = nullptr;
    QTimer timer;
    QJsonObject last;
    QJsonArray points;
    int interval = 2, scale = 1000;
    bool inFlight = false;
};
