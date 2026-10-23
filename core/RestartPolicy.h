#pragma once
#include <QList>
#include <QtGlobal>

// Decides whether and when to restart after a failure. Pure logic: callers pass the
// current time in milliseconds, so tests can drive it with a fake clock.
class RestartPolicy {
public:
    struct Settings {
        int maxRestarts = 3;          // restarts allowed within windowMs
        qint64 windowMs = 600000;
        qint64 cooldownMs = 5000;     // delay before the first restart in the window
        qint64 maxBackoffMs = 300000; // the delay doubles per attempt up to this bound
    };
    struct Decision {
        bool restart = false;
        qint64 delayMs = 0;
        int attempt = 0;              // 1-based attempt number within the window
    };
    RestartPolicy() = default;
    explicit RestartPolicy(const Settings &settings) : config(settings) {}
    void configure(const Settings &settings) { config = settings; }
    const Settings &settings() const { return config; }
    // Records the restart when one is granted.
    Decision onFailure(qint64 nowMs);
    int recentRestarts(qint64 nowMs) const;
    void reset() { restarts.clear(); }
private:
    void prune(qint64 nowMs);
    Settings config;
    QList<qint64> restarts;
};
