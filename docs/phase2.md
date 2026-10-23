# Phase 2 backlog

No unfinished HealthMonitor or RestartPolicy placeholders were added. Phase 1 FAILED states and exit codes provide basic process monitoring without automatic recovery.

- Implement a separate HealthMonitor: asynchronous GET /health, a five-second request timeout, a default 30-second interval, and no overlapping requests. Include a 600-second startup grace period and loading-time HTTP 503 handling.
- Implement a separate RestartPolicy: distinguish manual stops from crashes, startup failures, and HTTP failures. Add consecutive-failure thresholds, cooldown, exponential backoff, and a restart limit within a 10-minute window.
- Extend the state machine with UNHEALTHY, RESTARTING, and BACKOFF. Prevent overlapping recovery and restart loops.
- Use process-generation tokens to ignore stale HTTP responses and timers from previous processes.
- Record bounded restart history: failure time, profile ID, PID, reason, retry count, and recovery outcome.
- Persist watchdog settings and expose GUI controls. Manual Stop must cancel all pending recovery.
- Add fake HTTP, injectable clocks, and policy tests for loading-time 503 responses, timeouts, repeated failures, retry limits, stale results, and manual stop.
- Optionally add active inference health tests; do not make them a default mandatory restart condition.

Phase 3 system/GPU monitoring, multiple profiles and instances, and advanced log viewing remain unimplemented. Phase 4 system tray, Task Scheduler, Windows Service, and installer work also remains deferred.
