#include "ServerSupervisor.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
#include <limits>

namespace { int timeScale = 1000; }
void ServerSupervisor::setTimeScale(int msPerSecond) { timeScale = qMax(1, msPerSecond); }
qint64 ServerSupervisor::ms(int seconds) const { return qint64(seconds) * timeScale; }

ServerSupervisor::ServerSupervisor(const QString &id, const QString &path, QObject *parent)
    : QObject(parent), profileId(id), historyPath(path) {
    retryTimer.setSingleShot(true);
    connect(&retryTimer, &QTimer::timeout, this, &ServerSupervisor::retryNow);
    connect(&manager, &LlamaProcessManager::log, this, &ServerSupervisor::log);
    connect(&manager, &LlamaProcessManager::changed, this, &ServerSupervisor::onManagerChanged);
    connect(&health, &HealthMonitor::result, this, &ServerSupervisor::onHealth);
    loadHistory();
}
ServerSupervisor::~ServerSupervisor() {
    // The manager's destructor kills the process; nothing may react to it any more.
    disconnect(&manager, nullptr, this, nullptr);
    disconnect(&health, nullptr, this, nullptr);
    retryTimer.stop();
    health.stop();
}
void ServerSupervisor::configurePolicy() {
    const auto &w = profile.watchdog;
    policy.configure({w.maxRestarts, ms(w.windowSeconds), ms(w.cooldownSeconds), ms(w.maxBackoffSeconds)});
}
void ServerSupervisor::setState(const QString &value, const QString &error) {
    const bool changedState = value != current;
    current = value; lastError = error;
    if (changedState) emit log("agent", "[watchdog] " + value + (error.isEmpty() ? "\n" : ": " + error + "\n"));
    emit changed();
}

QString ServerSupervisor::start(const Profile &p) {
    if (active()) return "Server is already active";
    retryTimer.stop(); health.stop(); ++generation;
    profile = p; configurePolicy(); policy.reset(); attempt = 0; afterStop = AfterStop::None; lastPid = 0;
    const auto error = manager.start(profile);
    if (!error.isEmpty()) return error;
    setState("STARTING");
    return {};
}
QString ServerSupervisor::stop() {
    retryTimer.stop(); health.stop(); ++generation; policy.reset(); attempt = 0;
    if (pendingOutcome) resolvePending("cancelled by manual stop");
    if (manager.active()) {
        afterStop = AfterStop::Manual; setState("STOPPING");
        manager.stop();
    } else if (current != "STOPPED") {
        afterStop = AfterStop::None; setState("STOPPED");
    }
    return {};
}
QString ServerSupervisor::restart(const Profile &p) {
    if (!active()) return start(p);
    if (current != "RUNNING" && current != "UNHEALTHY" && current != "BACKOFF" && current != "RESTARTING")
        return "Wait until the server is running or stopped";
    if (const auto error = p.validate(); !error.isEmpty()) return error;
    retryTimer.stop(); health.stop(); ++generation; policy.reset(); attempt = 0;
    if (pendingOutcome) resolvePending("superseded by manual restart");
    profile = p; configurePolicy();
    if (manager.active()) {
        afterStop = AfterStop::ManualRestart; setState("RESTARTING");
        manager.stop();
        return {};
    }
    afterStop = AfterStop::None;
    const auto error = manager.start(profile);
    if (!error.isEmpty()) { setState("FAILED", error); return error; }
    setState("STARTING");
    return {};
}

void ServerSupervisor::onManagerChanged() {
    const auto state = manager.status()["state"].toString();
    if (state == managerState) { emit changed(); return; }
    managerState = state;
    if (state == "STARTING") {
        if (current == "STOPPED" || current == "FAILED") setState("STARTING");
        else emit changed();
    } else if (state == "RUNNING") {
        processStarted = true; lastPid = manager.status()["pid"].toInteger();
        processClock.start(); consecutiveFailures = 0; ++generation;
        if (profile.watchdog.healthCheck) {
            healthSummary = "Waiting for /health";
            // Poll more often while loading so readiness is noticed quickly.
            const int interval = int(qMin(ms(profile.watchdog.intervalSeconds), ms(1)));
            health.start(generation, profile.host, profile.port, interval, int(ms(profile.watchdog.timeoutSeconds)));
            setState("STARTING");
        } else {
            healthSummary = "Disabled";
            setState("RUNNING");
            if (pendingOutcome) resolvePending("recovered");
        }
    } else if (state == "STOPPED" || state == "FAILED") {
        health.stop(); ++generation;
        const bool wasRunning = processStarted; processStarted = false;
        const auto reason = afterStop;
        afterStop = AfterStop::None;
        switch (reason) {
        case AfterStop::Manual: setState("STOPPED"); return;
        case AfterStop::GiveUp: emit changed(); return; // stays FAILED
        case AfterStop::AutoRestart: beginRetry(); return;
        case AfterStop::ManualRestart: {
            const auto error = manager.start(profile);
            if (error.isEmpty()) setState("STARTING"); else setState("FAILED", error);
            return;
        }
        case AfterStop::None: break;
        }
        if (state == "STOPPED") { setState("STOPPED"); return; }
        fail(wasRunning ? Failure::Crash : Failure::Startup, manager.status()["error"].toString());
    } else emit changed();
}

void ServerSupervisor::onHealth(quint64 gen, HealthMonitor::Outcome outcome, int, const QString &detail) {
    if (gen != generation) return; // result from an older process or check cycle
    const auto &w = profile.watchdog;
    if (outcome == HealthMonitor::Outcome::Healthy) {
        consecutiveFailures = 0; healthSummary = "Healthy";
        if (current == "STARTING") {
            health.setInterval(int(ms(w.intervalSeconds)));
            emit log("agent", QString("[watchdog] /health returned 200 after %1 s; model is ready\n").arg(processClock.elapsed() / 1000.0, 0, 'f', 1));
            setState("RUNNING");
            if (pendingOutcome) resolvePending("recovered");
        } else if (current == "UNHEALTHY") {
            emit log("agent", "[watchdog] Health checks recovered\n");
            setState("RUNNING");
        } else emit changed();
        return;
    }
    if (current == "STARTING") {
        // Loading-time 503s and refused connections are expected until the grace period ends.
        healthSummary = outcome == HealthMonitor::Outcome::Loading ? "Loading model (HTTP 503)" : "Waiting for /health: " + detail;
        if (processClock.elapsed() >= ms(w.startupGraceSeconds))
            fail(Failure::Startup, QString("Model did not become healthy within %1 s (last check: %2)").arg(w.startupGraceSeconds).arg(detail));
        else emit changed();
        return;
    }
    if (current != "RUNNING" && current != "UNHEALTHY") return;
    ++consecutiveFailures;
    healthSummary = QString("Failing %1/%2: %3").arg(consecutiveFailures).arg(w.failureThreshold).arg(detail);
    emit log("agent", QString("[watchdog] Health check failed (%1/%2): %3\n").arg(consecutiveFailures).arg(w.failureThreshold).arg(detail));
    if (current == "RUNNING" && consecutiveFailures >= w.failureThreshold) {
        const auto reason = QString("%1 consecutive health checks failed (last: %2)").arg(consecutiveFailures).arg(detail);
        if (w.autoRestart) fail(Failure::Health, reason);
        else { record(Failure::Health, reason, 0, "auto restart disabled"); setState("UNHEALTHY", reason); }
    } else emit changed();
}

void ServerSupervisor::fail(Failure kind, const QString &reason) {
    health.stop(); ++generation;
    const QString label = kind == Failure::Crash ? "Process exited unexpectedly"
        : kind == Failure::Startup ? "Startup failed" : "Health check failed";
    const QString message = reason.startsWith(label) || reason.isEmpty() ? (reason.isEmpty() ? label : reason) : label + ": " + reason;
    if (pendingOutcome) resolvePending("failed");
    if (!profile.watchdog.autoRestart) {
        record(kind, message, 0, "auto restart disabled");
        setState("FAILED", message);
        if (manager.active()) { afterStop = AfterStop::GiveUp; manager.stop(); } // e.g. never became healthy
        return;
    }
    const auto decision = policy.onFailure(QDateTime::currentMSecsSinceEpoch());
    if (!decision.restart) {
        const auto limit = QString("%1. Restart limit reached (%2 within %3 s); waiting for manual action")
            .arg(message).arg(profile.watchdog.maxRestarts).arg(profile.watchdog.windowSeconds);
        record(kind, message, decision.attempt, "gave up: restart limit reached");
        setState("FAILED", limit);
        if (manager.active()) { afterStop = AfterStop::GiveUp; manager.stop(); }
        return;
    }
    attempt = decision.attempt; retryDelay = decision.delayMs;
    record(kind, message, attempt, "pending");
    pendingOutcome = true;
    setState(attempt > 1 ? "BACKOFF" : "RESTARTING", message);
    if (manager.active()) { afterStop = AfterStop::AutoRestart; manager.stop(); }
    else beginRetry();
}
void ServerSupervisor::beginRetry() {
    emit log("agent", QString("[watchdog] Restart attempt %1/%2 in %3 ms\n").arg(attempt).arg(profile.watchdog.maxRestarts).arg(retryDelay));
    retryTimer.start(int(qMin<qint64>(retryDelay, std::numeric_limits<int>::max())));
    emit changed();
}
void ServerSupervisor::retryNow() {
    setState("RESTARTING", lastError);
    lastPid = 0;
    const auto error = manager.start(profile);
    if (!error.isEmpty()) fail(Failure::Startup, error);
}

void ServerSupervisor::record(Failure kind, const QString &reason, int n, const QString &outcome) {
    entries.append(QJsonObject{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"profileId", profileId},
        {"pid", lastPid}, {"kind", kind == Failure::Crash ? "crash" : kind == Failure::Startup ? "startup" : "health"},
        {"reason", reason}, {"attempt", n}, {"outcome", outcome}});
    while (entries.size() > MaxHistory) entries.removeFirst();
    saveHistory();
}
void ServerSupervisor::resolvePending(const QString &outcome) {
    pendingOutcome = false;
    for (qsizetype i = entries.size() - 1; i >= 0; --i) {
        auto entry = entries[i].toObject();
        if (entry["outcome"].toString() != "pending") continue;
        entry["outcome"] = outcome; entries[i] = entry;
        break;
    }
    saveHistory();
}
void ServerSupervisor::loadHistory() {
    if (historyPath.isEmpty()) return;
    QFile file(historyPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object()["version"].toInt() != 1) return;
    for (const auto &v : doc.object()["entries"].toArray()) {
        if (!v.isObject()) continue;
        auto entry = v.toObject();
        // An Agent restart interrupts any recovery that was still pending.
        if (entry["outcome"].toString() == "pending") entry["outcome"] = "interrupted by Agent restart";
        entries.append(entry);
    }
    while (entries.size() > MaxHistory) entries.removeFirst();
}
void ServerSupervisor::saveHistory() {
    if (!historyPath.isEmpty()) {
        QDir().mkpath(QFileInfo(historyPath).absolutePath());
        QSaveFile file(historyPath);
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}}).toJson(QJsonDocument::Compact);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            emit log("agent", "[watchdog] Cannot save restart history: " + file.errorString() + "\n");
    }
    emit historyChanged();
}

QJsonObject ServerSupervisor::status() const {
    auto s = manager.status();
    const auto &w = profile.watchdog;
    s["state"] = current;
    s["error"] = lastError;
    s["health"] = !w.healthCheck ? QString("Disabled") : processStarted ? healthSummary : QString("Not running");
    s["healthFailures"] = consecutiveFailures;
    s["autoRestart"] = w.autoRestart;
    s["restartAttempt"] = attempt;
    s["maxRestarts"] = w.maxRestarts;
    s["restartsInWindow"] = policy.recentRestarts(QDateTime::currentMSecsSinceEpoch());
    s["nextRestartSeconds"] = retryTimer.isActive() ? int(std::ceil(retryTimer.remainingTime() / 1000.0)) : 0;
    return s;
}
