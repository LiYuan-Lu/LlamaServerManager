#include "RestartPolicy.h"
void RestartPolicy::prune(qint64 now)
{
    while (!restarts.isEmpty() && now - restarts.first() >= config.windowMs) {
        restarts.removeFirst();
    }
}
int RestartPolicy::recentRestarts(qint64 now) const
{
    int n = 0;
    for (const auto t : restarts) {
        if (now - t < config.windowMs) {
            ++n;
        }
    }
    return n;
}
RestartPolicy::Decision RestartPolicy::onFailure(qint64 now)
{
    prune(now);
    Decision d;
    d.attempt = int(restarts.size()) + 1;
    if (restarts.size() >= config.maxRestarts) {
        return d;
    }
    qint64 delay = config.cooldownMs;
    for (int i = 1; i < d.attempt && delay < config.maxBackoffMs; ++i) {
        delay *= 2;
    }
    d.delayMs = qMin(delay, config.maxBackoffMs);
    d.restart = true;
    restarts.append(now);
    return d;
}
