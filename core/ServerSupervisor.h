#pragma once
#include "LlamaProcessManager.h"
#include "HealthMonitor.h"
#include "RestartPolicy.h"
#include <QJsonArray>
#include <QElapsedTimer>

// Watchdog for one model. Wraps LlamaProcessManager (process lifecycle) with HealthMonitor
// and RestartPolicy, and exposes the extended state machine:
//   STOPPED, STARTING, RUNNING, UNHEALTHY, RESTARTING, BACKOFF, FAILED, STOPPING.
// STARTING lasts until /health returns 200 when health checks are enabled.
// A manual stop always wins: it cancels pending restarts and stale health results.
class ServerSupervisor : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxHistory = 50;
    // historyPath may be empty (no persistence).
    ServerSupervisor(const QString &profileId, const QString &historyPath, QObject *parent = nullptr);
    ~ServerSupervisor() override;
    QString start(const Profile &profile);
    QString stop();
    QString restart(const Profile &profile);
    QJsonObject status() const;
    QJsonArray history() const { return entries; }
    QString state() const { return current; }
    // Everything except STOPPED and FAILED; profiles cannot be edited while active.
    bool active() const { return current != "STOPPED" && current != "FAILED"; }
    // Test hook: milliseconds per configured watchdog "second" (default 1000).
    static void setTimeScale(int millisecondsPerSecond);
signals:
    void changed();
    void log(const QString &channel, const QString &text);
    void historyChanged();
private:
    enum class Failure { Crash, Startup, Health };
    enum class AfterStop { None, Manual, ManualRestart, AutoRestart, GiveUp };
    void onManagerChanged();
    void onHealth(quint64 generation, HealthMonitor::Outcome outcome, int status, const QString &detail);
    void fail(Failure kind, const QString &reason);
    void beginRetry();
    void retryNow();
    void setState(const QString &state, const QString &error = {});
    void record(Failure kind, const QString &reason, int attempt, const QString &outcome);
    void resolvePending(const QString &outcome);
    void loadHistory();
    void saveHistory();
    void configurePolicy();
    qint64 ms(int seconds) const;
    LlamaProcessManager manager;
    HealthMonitor health;
    RestartPolicy policy;
    QTimer retryTimer;
    QElapsedTimer processClock;
    Profile profile;
    QString profileId, historyPath, current = "STOPPED", lastError, managerState = "STOPPED", healthSummary;
    QJsonArray entries;
    quint64 generation = 0;
    qint64 lastPid = 0, retryDelay = 0;
    int consecutiveFailures = 0, attempt = 0;
    bool processStarted = false, pendingOutcome = false;
    AfterStop afterStop = AfterStop::None;
};
