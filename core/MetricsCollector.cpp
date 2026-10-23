#include "MetricsCollector.h"
#include "GpuMonitor.h"
#include "SystemMonitor.h"
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
double rounded(double value)
{
    return value < 0 ? -1 : std::round(value * 10) / 10;
}
}

// Lives on the worker thread; all monitor state stays on that thread.
class MetricsSampler : public QObject {
public:
    explicit MetricsSampler(QString nvmlLibrary)
        : nvmlLibrary(std::move(nvmlLibrary))
    {
    }
    QJsonObject sample(const QList<MetricsCollector::Target>& targets)
    {
        if (!gpu) {
            // NVML initialization can take a while; it runs here, never on the Agent thread.
            gpu = std::make_unique<GpuMonitor>(nvmlLibrary);
        }
        const auto system = monitor.sampleSystem();
        QList<qint64> pids;
        for (const auto& t : targets) {
            pids << t.pid;
        }
        const auto processes = monitor.sampleProcesses(pids);
        const auto gpus = gpu->sample();
        QJsonArray devices;
        for (const auto& d : gpus.devices) {
            devices.append(QJsonObject {
                { "index", d.index },
                { "name", d.name },
                { "utilizationPercent", d.utilizationPercent },
                { "memoryUsedBytes", d.memoryUsedBytes },
                { "memoryTotalBytes", d.memoryTotalBytes },
                { "temperatureC", d.temperatureC },
                { "powerWatts", rounded(d.powerWatts) },
            });
        }
        QJsonObject perProfile;
        for (int i = 0; i < targets.size(); ++i) {
            const auto& p = processes[i];
            qint64 gpuMemory = -1;
            if (gpus.available && p.running) {
                gpuMemory = gpus.processMemory.value(p.pid, 0); // absent: not using a GPU
            }
            perProfile[targets[i].profileId] = QJsonObject {
                { "pid", p.pid },
                { "running", p.running },
                { "cpuPercent", rounded(p.cpuPercent) },
                { "ramBytes", p.ramBytes },
                { "gpuMemoryBytes", gpuMemory },
            };
        }
        const auto now = QDateTime::currentDateTimeUtc();
        return {
            { "time", now.toString(Qt::ISODateWithMs) },
            { "timeMs", now.toMSecsSinceEpoch() },
            { "system", QJsonObject {
                            { "cpuPercent", rounded(system.cpuPercent) },
                            { "ramUsedBytes", system.ramUsedBytes },
                            { "ramTotalBytes", system.ramTotalBytes },
                            { "logicalProcessors", SystemMonitor::logicalProcessors() },
                        } },
            { "gpu", QJsonObject { { "available", gpus.available }, { "reason", gpus.reason }, { "devices", devices } } },
            { "processes", perProfile },
        };
    }

private:
    QString nvmlLibrary;
    SystemMonitor monitor;
    std::unique_ptr<GpuMonitor> gpu;
};

MetricsCollector::MetricsCollector(TargetProvider provider, const QString& nvmlLibrary, QObject* parent)
    : QObject(parent)
    , targets(std::move(provider))
    , sampler(new MetricsSampler(nvmlLibrary))
{
    thread.setObjectName("metrics");
    sampler->moveToThread(&thread);
    timer.setInterval(interval * scale);
    connect(&timer, &QTimer::timeout, this, &MetricsCollector::tick);
}

MetricsCollector::~MetricsCollector()
{
    timer.stop();
    thread.quit();
    thread.wait();
    // The worker thread has finished, so the sampler no longer receives events.
    delete sampler;
}

void MetricsCollector::start()
{
    if (!thread.isRunning()) {
        thread.start(QThread::LowPriority);
    }
    timer.start();
    tick(); // establishes the CPU baseline right away
}

void MetricsCollector::setIntervalSeconds(int seconds)
{
    interval = std::clamp(seconds, MinIntervalSeconds, MaxIntervalSeconds);
    timer.setInterval(interval * scale);
}

void MetricsCollector::setTimeScale(int millisecondsPerSecond)
{
    scale = std::max(1, millisecondsPerSecond);
    timer.setInterval(interval * scale);
}

void MetricsCollector::tick()
{
    if (inFlight || !thread.isRunning()) {
        return; // never queue overlapping samples behind a slow one
    }
    inFlight = true;
    const auto list = targets ? targets() : QList<Target> { };
    auto* worker = sampler;
    QMetaObject::invokeMethod(sampler, [this, worker, list] {
        const auto sample = worker->sample(list);
        QMetaObject::invokeMethod(this, [this, sample] { accept(sample); }, Qt::QueuedConnection); }, Qt::QueuedConnection);
}

QJsonObject MetricsCollector::compact(const QJsonObject& sample)
{
    const auto system = sample["system"].toObject();
    QJsonArray gpus;
    for (const auto& value : sample["gpu"].toObject()["devices"].toArray()) {
        const auto d = value.toObject();
        gpus.append(QJsonObject { { "util", d["utilizationPercent"] }, { "memUsed", d["memoryUsedBytes"] }, { "memTotal", d["memoryTotalBytes"] } });
    }
    return {
        { "time", sample["timeMs"].toInteger() },
        { "cpu", system["cpuPercent"] },
        { "ramUsed", system["ramUsedBytes"] },
        { "ramTotal", system["ramTotalBytes"] },
        { "gpus", gpus },
    };
}

void MetricsCollector::accept(const QJsonObject& sample)
{
    inFlight = false;
    last = sample;
    last["intervalSeconds"] = interval;
    points.append(compact(sample));
    prune(points, sample["timeMs"].toInteger(), [](const QJsonValue& v) { return v.toObject()["time"].toInteger(); });
    emit sampled(last);
}
