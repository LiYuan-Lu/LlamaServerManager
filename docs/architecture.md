# Architecture

GUI (`QGuiApplication`) → AgentClient → per-user QLocalSocket → AgentServer (`QCoreApplication`) → one Instance per profile (ServerSupervisor + LogStore) → LlamaProcessManager → QProcess → llama-server.

AgentServer also owns a MetricsCollector (Phase 3), which samples system, GPU, and per-model process usage on its own thread and pushes `metrics` events; see phase3.md.

ServerSupervisor is the per-model watchdog: it combines LlamaProcessManager with HealthMonitor (GET /health) and RestartPolicy, keeps the restart history, and owns the user-visible state. See phase2.md.

Each saved profile is one model. The Agent creates an independent ServerSupervisor and LogStore (`logs/<profile-id>/`) for every profile, so models start, stop, restart, and fail independently. Profiles cannot be edited or deleted while their model is active. Before starting, the Agent rejects a port already used by another active model.

The GUI opens on a grid of model cards. Each card shows the state, model file, address, PID, and uptime, with one Start/Stop toggle button and a Restart button. The trailing + card opens an empty editor; saving it sends createProfile. Clicking a card opens its detail page with the controls, the profile editor, the model's console, and Delete.

The GUI never owns the llama-server QProcess. The Agent is launched with startDetached, so its lifetime is independent of the GUI socket and GUI shutdown. While the Agent is alive, it owns the model PID, state, launch arguments, and recent logs. Reconnecting the GUI does not restart the model. Stale PIDs on disk are not treated as live processes, and unknown processes are not adopted.

## States

Process level (LlamaProcessManager): STOPPED / FAILED → STARTING (asynchronous --help preflight and OS process launch) → RUNNING (the OS process exists); STARTING / RUNNING → STOPPING → STOPPED; unexpected exits and launch failures → FAILED.

Model level (ServerSupervisor, shown in the GUI): STOPPED, STARTING, RUNNING, UNHEALTHY, RESTARTING, BACKOFF, FAILED, STOPPING. With health checks enabled, STARTING lasts until /health returns 200. Crashes, startup failures, and failed health checks are handed to RestartPolicy. See phase2.md for the full table.

A manual restart is accepted in RUNNING, UNHEALTHY, RESTARTING, and BACKOFF; in STOPPED or FAILED it acts as start. Stop cancels any pending automatic restart. Both --help and OS process creation have 10-second timeouts; Stop falls back to kill after three seconds. Model loading is bounded only by the configurable startup grace period (default 600 seconds).

Profile is a data type; ProfileStore handles files. LlamaProcessManager, HealthMonitor, RestartPolicy, and ServerSupervisor do not depend on QML or IPC. AgentServer coordinates commands. Tests can instantiate the process manager directly or launch the complete Agent.

## Process ownership

Each data directory acquires a QLockFile before listening. GUIs using the same account and directory share an endpoint. QLocalServer uses UserAccessOption. QLockFile does not remove a live lock merely because of its age.

On Windows, the model is assigned to a Job Object with KILL_ON_JOB_CLOSE after the started signal. Only processes created and assigned by this Agent are affected. An Agent crash normally terminates the model in that job to avoid an orphan that cannot be adopted. Closing the GUI does not close the job because the GUI does not own it.

There is a short interval between process creation and job assignment. Descendants spawned immediately are not guaranteed to join the job. If assignment is blocked by an external job restriction, a warning is logged and only the direct child is managed. Standard llama-server runs as the direct process; reliable management of arbitrary wrapper process trees is not claimed. Supporting such wrappers would require a Windows-specific suspended launch, job assignment, and resume sequence. The Agent never scans for or terminates unrelated llama-server processes by name.

## Bounds and failure handling

Newline-delimited JSON frames are limited to 1 MiB, and each socket's pending output is limited to 2 MiB. Slow clients exceeding the limit are disconnected while the model continues running. At most 16 GUI connections are accepted. Process output is split into chunks of at most 4,096 bytes; streaming UTF-8 decoders preserve characters split across reads. Memory and disk retention are bounded. Disk write or rotation failures generate warnings without stopping the model.

Profiles are validated before atomic writes. Reading a corrupt file does not immediately overwrite it. The GUI retries connection asynchronously every 1.5 seconds and attempts to launch the Agent at most once every 10 seconds. Requests time out after 15 seconds. Commands that can change state are not retried automatically. The Agent pushes status every second and metrics every `metricsIntervalSeconds` (default 2).

The Qt main-thread event-driven QProcess/IPC paths do not use waitForStarted or waitForFinished. Bounded blocking cleanup is used only during Agent destruction; tests may also wait. Disk operations are short synchronous local JSON/log writes. This MVP does not have a background write queue for slow network storage; keep the production data directory on a local disk.
