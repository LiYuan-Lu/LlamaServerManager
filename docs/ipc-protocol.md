# IPC v1

Transport uses QLocalSocket / QLocalServer. Each message is a UTF-8 JSON object terminated by LF; newlines inside strings use JSON escaping. The receiver buffers partial frames and handles multiple frames in one read. A frame is limited to 1 MiB. The endpoint name combines `LlamaServerManager-v1-` with the first 24 hexadecimal characters of the SHA-256 hash of the absolute data-directory path. The GUI and Agent must use the same absolute path.

Request (`id` must be a string containing 1–128 characters):

```json
{"version":1,"id":"uuid","command":"getStatus"}
```

Response:

```json
{"version":1,"id":"uuid","ok":true,"error":"","data":{"state":"RUNNING","pid":1234}}
```

Event:

```json
{"version":1,"event":"log","data":{"sequence":1,"time":"2026-10-23T02:00:00.000Z","channel":"stdout","text":"loading...\n"}}
```

## Commands

Commands that act on one model take a `profileId` string. A missing or unknown `profileId` returns ok=false.

| Command | Payload / response |
| --- | --- |
| getStatus | data: `instances` (array, see below) and `warning` (profile load warning, may be empty) |
| listProfiles | data.profiles: array of Profile objects |
| getLogs | request.profileId; data: `profileId`, `entries` (bounded array of recent log records for that model) |
| createProfile | request.profile: Profile object (`id` is ignored and generated); returns a full status snapshot plus `id` |
| updateProfile | request.profileId, request.profile; returns a full status snapshot; rejected while that model is active |
| deleteProfile | request.profileId; removes the profile and its log folder; rejected while that model is active |
| startServer | request.profileId; starts the saved profile; rejected if another active model uses the same port |
| stopServer | request.profileId; cancels a pending manual restart and stops that model's process; repeated stop is safe |
| restartServer | request.profileId; stops and restarts in RUNNING/UNHEALTHY/RESTARTING/BACKOFF; acts as start in STOPPED/FAILED |
| getRestartHistory | request.profileId; data: `profileId`, `entries` (up to 50 watchdog records, oldest first) |
| shutdownAgent | stops every model like stopServer, broadcasts a `shutdown` event, and exits the Agent once all models have stopped (at most about 15 s). While shutting down, only read commands are accepted |
| getMetrics | data: `intervalSeconds`, `historySeconds` (300), `latest` (last sample, see below; empty before the first one), `history` (compact points, oldest first) |
| getSettings | data.settings: `{metricsIntervalSeconds}` |
| updateSettings | request.settings: fields to change (unknown fields are rejected); validated, saved to settings.json, applied, and broadcast as a `settings` event; data.settings: the new settings |

Each element of `instances` contains: id, name, state, pid, port, model, uptimeSeconds, error, arguments, profile, and the watchdog fields health (summary text such as `Healthy`, `Loading model (HTTP 503)`, `Failing 2/3: ...`, `Disabled`, `Not running`), healthFailures, autoRestart, restartAttempt, maxRestarts, restartsInWindow, and nextRestartSeconds (0 unless a restart is scheduled).

`throughput` is the generation speed, sampled from the server's `GET /slots` once per second while its process runs: `{"available":true,"reason":"","tokensPerSecond":41.7,"lastTokensPerSecond":39.2,"generatedTokens":5120,"activeSlots":1,"slots":2}`. `tokensPerSecond` is the speed over the last second across all slots (prompt processing is not counted); `lastTokensPerSecond` is the average of the latest finished generation burst (0 until one finished); `generatedTokens` counts tokens since the process started. When `available` is false, `reason` says why (`Not running`, `Waiting for /slots`, or the endpoint is disabled because the server runs with `--no-slots`; then it is not polled again until the next start). The GUI keeps its own 5-minute history from the status updates.

`state` is one of STOPPED, STARTING, RUNNING, UNHEALTHY, RESTARTING, BACKOFF, FAILED, STOPPING. Profiles can be edited or deleted only in STOPPED or FAILED.

A restart history record contains time, profileId, pid, kind (`crash`, `startup`, `health`), reason, attempt, and outcome.

A `status` event contains a complete snapshot (same shape as getStatus). A `log` event contains one record plus its `profileId`. A `history` event contains `profileId` and the full `entries` array of that model whenever its restart history changes. A `warning` event contains a message (and `profileId` when it concerns one model), such as a disk logging failure. A `shutdown` event (empty data) announces that the Agent is stopping at a user's request; GUIs must not relaunch it automatically afterwards. A `metrics` event contains one sample (see below). A `settings` event contains `settings` after it changed. Unknown commands return ok=false. Invalid JSON, incompatible versions, oversized frames, or invalid IDs cause disconnection.

A successful response means the command was accepted, not that the model has finished loading. Observe status and logs for the outcome. Disconnecting does not stop the service. On reconnection, the GUI must request getStatus (and getLogs for the model it is showing) rather than automatically replaying start/restart commands.

## Metrics

A sample (`metrics` event, `getMetrics.latest`):

```json
{"time":"2026-10-23T06:48:44.056Z","timeMs":1792738124056,"intervalSeconds":2,
 "system":{"cpuPercent":3.8,"ramUsedBytes":576282624,"ramTotalBytes":8417529856,"logicalProcessors":16},
 "gpu":{"available":true,"reason":"","devices":[{"index":0,"name":"NVIDIA GeForce RTX 4090","utilizationPercent":75,
   "memoryUsedBytes":6442450944,"memoryTotalBytes":25769803776,"temperatureC":67,"powerWatts":215.5}]},
 "processes":{"<profileId>":{"pid":2951,"running":true,"cpuPercent":0.3,"ramBytes":18034688,"gpuMemoryBytes":-1}}}
```

`-1` means not available (first sample for CPU values, a value the driver does not report, or no GPU). `processes` lists models whose process has a PID. Device values are for the whole GPU; `gpuMemoryBytes` is that process's own GPU memory (0 = not using a GPU). When `gpu.available` is false, `reason` says why.

A history point: `{"time":<ms since epoch>,"cpu":<percent>,"ramUsed":<bytes>,"ramTotal":<bytes>,"gpus":[{"util":<percent>,"memUsed":<bytes>,"memTotal":<bytes>}]}`. The Agent keeps 5 minutes, at most 600 points.

## Profile fields

Defaults: id/name/executable/model/workingDirectory are empty strings; host=127.0.0.1, port=8080, context=4096, gpuLayers=0, threads=4, parallel=1, metrics=false, autoStart=false, aliasEnabled=false, alias="", extraArguments=[], environment={}.

`aliasEnabled` / `alias`: when enabled, the alias is passed as `--alias`, so `GET /v1/models` reports it as the model ID instead of the model file path. An enabled alias must not be empty, have leading or trailing spaces, or contain commas or control characters; `-a` and `--alias` cannot be set through extraArguments.

`autoStart`: when true, the Agent starts this model each time the Agent starts (after it begins listening). A failure, such as a port conflict, is added to the status `warning` and to the model's log; the Agent keeps running.

Startup requires existing absolute executable and GGUF paths. Host must be a numeric IP address; GPU layers accepts -1. Each extraArguments element is passed separately to QProcess without a shell. Overriding the main managed options is prohibited.

The `id` is generated by the Agent and matches `[A-Za-z0-9_-]{1,64}`; it is also the log folder name. `name` is the display name (at most 128 characters); when empty, the model file name is used. `watchdog` holds the per-model watchdog settings (see phase2.md); a profile without it gets the defaults.
