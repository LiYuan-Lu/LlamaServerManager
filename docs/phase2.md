# Phase 2 — Watchdog

Status: implemented. Each model has its own watchdog; settings are stored per profile.

## Components

- `core/HealthMonitor` — asynchronous `GET http://host:port/health` through QNetworkAccessManager (no proxy). At most one request is in flight; the next check is scheduled after the previous one finishes. Each result carries a generation token. Wildcard hosts (`0.0.0.0`, `::`) are checked via loopback.
- `core/RestartPolicy` — pure decision logic with explicit timestamps (fake-clock testable): at most `maxRestarts` within `windowSeconds`; the delay starts at `cooldownSeconds` and doubles per attempt up to `maxBackoffSeconds`.
- `core/ServerSupervisor` — one per model. Owns LlamaProcessManager, HealthMonitor, RestartPolicy, and the restart history, and runs the state machine below.

## States

| State | Meaning |
| --- | --- |
| STOPPED | Not running. A manual stop always ends here and never restarts automatically. |
| STARTING | Preflight, process launch, and model loading. With health checks on, lasts until /health returns 200. HTTP 503 and refused connections are expected during the startup grace period. |
| RUNNING | Healthy (or process started, when health checks are off). Checked every `intervalSeconds`. |
| UNHEALTHY | `failureThreshold` consecutive checks failed. With auto restart off the model stays here (and returns to RUNNING if checks recover). |
| RESTARTING | First automatic restart: stopping the old process and/or waiting the cooldown, then launching. |
| BACKOFF | Second or later restart within the window: waiting an increased delay. |
| FAILED | Startup failed, crashed with auto restart off, or the restart limit was reached. Waits for manual action. A process that never became healthy is stopped. |
| STOPPING | Manual stop in progress. |

Failures that trigger the policy: unexpected process exit (crash), startup failure (preflight/launch error, or not healthy before `startupGraceSeconds`), and failed health checks (timeout, refused, non-200/503 status, or 503 after the model was ready).

Stale results are ignored: every process launch, stop, and failure increments a generation counter, and health results from an older generation are dropped. Stop cancels the pending restart timer, aborts the in-flight request, and resets the restart budget. A manual Start or Restart also starts with a fresh budget.

## Restart history

Every failure is recorded with time, profile ID, PID, kind (`crash`, `startup`, `health`), reason, attempt, and outcome (`pending`, `recovered`, `failed`, `gave up: restart limit reached`, `auto restart disabled`, `cancelled by manual stop`, `superseded by manual restart`, `interrupted by Agent restart`). The last 50 entries per model are kept in `logs/<profile-id>/restart-history.json` and survive Agent restarts.

## Settings (profile `watchdog` object)

| Field | Default | Range |
| --- | --- | --- |
| autoRestart | true | |
| healthCheck | true | |
| intervalSeconds | 30 | 1–3600 |
| timeoutSeconds | 5 | 1–300 |
| failureThreshold | 3 | 1–100 |
| startupGraceSeconds | 600 | 1–86400 |
| cooldownSeconds | 5 | 0–3600 |
| maxBackoffSeconds | 300 | ≥ cooldown, ≤ 86400 |
| maxRestarts | 3 | 0–100 |
| windowSeconds | 600 | 1–86400 |

Profiles saved before Phase 2 have no `watchdog` object and receive these defaults, so existing models get auto restart and health checks after upgrading.

## Not implemented

- Optional active inference tests (spec 6.3). They must stay opt-in and must not become a default restart condition.
- A global `settings.json`; watchdog settings are per profile.
