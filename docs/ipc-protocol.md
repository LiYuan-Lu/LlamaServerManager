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

## Phase 1 commands

| Command | Payload / response |
| --- | --- |
| getStatus | data: state, pid, port, model, uptimeSeconds, error, arguments, health, profile |
| getLogs | data.entries: bounded array of recent log records |
| updateProfile | request.profile: Profile object; returns a full status snapshot on success; rejected while active |
| startServer | Starts the saved profile; status updates asynchronously after acceptance |
| stopServer | Cancels pending manual restart and stops the owned process; repeated stop is safe |
| restartServer | Stops and restarts in RUNNING; acts as start in STOPPED/FAILED |

A `status` event contains a complete snapshot. A `log` event contains one record. A `warning` event contains a message, such as a disk logging failure. Unknown commands return ok=false. Invalid JSON, incompatible versions, oversized frames, or invalid IDs cause disconnection. Later-phase commands listProfiles, deleteProfile, getMetrics, and getRestartHistory are not implemented.

A successful response means the command was accepted, not that the model has finished loading. Observe status and logs for the outcome. Disconnecting does not stop the service. On reconnection, the GUI must request getStatus and getLogs rather than automatically replaying start/restart commands.

## Profile fields

Defaults: executable/model/workingDirectory are empty strings; host=127.0.0.1, port=8080, context=4096, gpuLayers=0, threads=4, parallel=1, metrics=false, extraArguments=[], environment={}.

Startup requires existing absolute executable and GGUF paths. Host must be a numeric IP address; GPU layers accepts -1. Each extraArguments element is passed separately to QProcess without a shell. Overriding the main managed options is prohibited.
