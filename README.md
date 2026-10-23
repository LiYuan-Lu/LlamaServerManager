# Llama Server Manager

A Windows x64 manager for multiple llama-server instances, built with Qt 6, QML, C++17, and CMake. The GUI and Agent are separate executables. Closing or crashing the GUI does not stop the Agent or model. Each model has a watchdog with HTTP health checks and automatic restarts (Phase 2). The dashboard shows system CPU/RAM, NVIDIA GPU/VRAM, per-model resource usage, and 5-minute charts, and the console has search, level filters, pause, and export (Phase 3). Phase 4 features, including the system tray and Windows Service support, are not implemented.

The Phase 1 implementation passed all seven tests in both Release and Debug builds on Windows. Phase 2 and Phase 3 were tested on Linux only so far; see [docs/testing.md](docs/testing.md). A real 9B GGUF model was also used to verify loading, GUI reconnection, and shutdown. After completing the build and deployment steps below, launch `deploy/llama-manager.exe`.

## Build

Install Qt 6.5 or later with the MSVC x64 packages for Core, Network, Quick, Quick Controls, Test, and Linguist Tools (included with the standard Qt installation); CMake; the Visual Studio 2026 C++ desktop development workload; and a Windows SDK. The Qt package name `msvc2022_64` does not require the Visual Studio 2022 IDE: this project was built with VS 2026 MSVC. The VS 2026 generator requires a CMake version that supports it.

Run these commands in PowerShell from the project root. Enter your own Qt MSVC x64 package directory when prompted:

```powershell
$qtRoot = Read-Host "Qt MSVC x64 package directory"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_PREFIX_PATH=$qtRoot"
cmake --build build --config Release -- /m:1 /nr:false
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
& .\build\Release\llama-manager.exe
```

`/m:1 /nr:false` avoids MSBuild worker communication stalls in restricted execution environments. In a normal environment, you can also use `--parallel`. CMake locates Visual Studio automatically; use `-DCMAKE_GENERATOR_INSTANCE` to select an installation if necessary. For VS 2022, use the `Visual Studio 17 2022` generator. Replace `Release` with `Debug` for a debug build.

## Usage

The GUI is available in English and Traditional Chinese. On first launch, it follows the Windows display language; use the EN / 中文 button in the top-right corner to switch at any time. The choice is saved per user in `HKEY_CURRENT_USER\Software\LlamaServerManager\llama-manager`.

1. Launch `llama-manager.exe`. The GUI connects asynchronously and starts `llama-manager-agent.exe` from the same directory if no Agent is available.
2. The main window is a grid of model cards. Click the **+** card to add a model, or click a card to open its details. Each card has one Start/Stop toggle button and a Restart button, so models can be controlled straight from the grid. In the editor, give the model a name and use the browse buttons to select your `llama-server.exe` and `.gguf` file. Configure the working directory, host, port, context size, GPU layers, threads, parallel slots, and metrics as needed.
3. Enter extra arguments as a JSON string array, such as `["--verbose"]`. Each element is one argument. Enter environment variables as a JSON object, such as `{"CUDA_VISIBLE_DEVICES":"0"}`. No shell is involved.
4. Save the profile (or click **Add model** for a new one), then start the server. The Agent first inspects the executable's `--help` output with a 10-second timeout. It rejects startup if required flags are missing. The selected llama.cpp version validates additional arguments; errors appear on stderr.
5. Inspect the PID, port, uptime, health, stdout, and stderr. With health checks on (the default), a model stays `STARTING` while it loads and becomes `RUNNING` once `GET /health` returns 200. With health checks off, `RUNNING` only means the process has started.
6. The **System resources** panel at the top of the model grid shows CPU, RAM, GPU, and VRAM with 5-minute charts; GPU values are for the whole card. Each running model card also shows its own CPU, RAM, and VRAM (when the driver reports it). Choose the refresh interval (1–10 s) in the panel. Without an NVIDIA GPU or driver, the GPU tiles show why they are unavailable. See `docs/phase3.md`.
7. The watchdog restarts a model automatically after a crash, a startup failure, or repeated failed health checks, waiting 5 s, then 10 s, 20 s, … up to 3 times in 10 minutes before giving up (`FAILED`). The **Watchdog** section of each model configures this, and the **Restart history** tab lists every failure and its outcome. Stop first calls terminate, then forcibly ends the owned process if it is still running after three seconds; it always cancels pending automatic restarts.
8. The **Console** tab of a model shows its stdout/stderr. Search, show or hide errors, warnings, and info lines, pause the view, copy, or export to a file. Clear view only clears the screen; log files stay on disk.
9. Close the GUI without stopping the Agent or model. Reopening it restores the current state and recent logs retained by the Agent.

The Agent can also run independently as `llama-manager-agent.exe`. Both executables accept `--data-dir <absolute-path>` for isolated testing. Use the same directory for both processes. Do not mix data directories during normal use. Phase 1 does not expose an Agent shutdown action in the GUI. For maintenance, stop the model first, then end the corresponding Agent PID in Task Manager. Do not terminate processes in bulk by executable name.

## Data and limitations

- All model profiles are stored in `%APPDATA%/LlamaServerManager/profiles.json`, using schema version 2 (`{"version":2,"profiles":[...]}`, at most 64 models). A Phase 1 version 1 file is migrated automatically on first Agent start: its single profile becomes one model with a generated ID, and the original file is kept as `.bak`. QSaveFile provides atomic writes. The previous valid version is retained as `.bak`; invalid data falls back to the backup or an empty list with a warning.
- Agent-wide settings (currently the resource refresh interval) are stored in `settings.json` in the same directory.
- Logs are stored per model in `logs/<profile-id>/server.jsonl` under the same data directory. Deleting a model deletes its log folder. Each file is limited to 4 MiB, with three rotated files retained, for approximately 16 MiB total. Recent replay is limited to 1,000 records or 256 KiB. GUI text is also bounded. Clearing the view does not delete disk logs.
- Automatic restarts follow each model's watchdog settings (see `docs/phase2.md`). A manual stop leaves the server stopped. Restart history is kept in `logs/<profile-id>/restart-history.json` (last 50 entries).
- The single-Agent lock is scoped to the data directory. The named pipe is restricted to the current user; it is not a security boundary against untrusted programs running as that user.
- A Windows Job Object manages the owned model process and subsequent descendants. If assignment is rejected, the Agent logs a warning and falls back to managing the direct child. See the architecture document for limitations.
- Starting a model is refused while another active model uses the same port (including wildcard hosts). Several models may run at once; each has its own process, Job Object, and log, and stopping one never affects another. The Agent does not check whether GPU memory is sufficient for all running models.
- A port preflight check reduces conflicts but cannot eliminate the race before the child binds the port. Actual failures appear through process termination and stderr.
- Resource monitoring loads NVIDIA NVML (`nvml.dll`, installed with the NVIDIA driver) at runtime; it is not needed to build or run. Per-model VRAM is usually not reported by Windows WDDM drivers and is then shown as n/a.
- Models and llama-server are not bundled. No Python runtime is required.

## Translations

The GUI uses Qt's standard translation workflow. QML strings are marked with `qsTr()`, and translations are stored in `translations/llama-manager_zh_TW.ts`. The build compiles them to `.qm` files with `lrelease` and embeds them in the executable. After adding or changing `qsTr()` strings, refresh the `.ts` file, translate the new entries in Qt Linguist, and rebuild:

```powershell
cmake --build build --target llama-manager_lupdate
& (Join-Path $qtRoot "bin\linguist.exe") translations\llama-manager_zh_TW.ts
```

To add a language, add its `.ts` file to `qt_add_translations` in `CMakeLists.txt` and list it in `apps/gui/LanguageManager.cpp`. Qt's own strings, such as standard dialog buttons, come from the Qt translation files that windeployqt copies into `deploy/translations`.

## Development deployment

Keep both executables in the same directory. These commands create a runnable local folder. Installers, CPack packaging, Task Scheduler integration, and Windows Service support remain Phase 4 work.

Use the `$qtRoot` value from the build instructions:

```powershell
New-Item -ItemType Directory -Force deploy | Out-Null
Copy-Item build\Release\llama-manager.exe,build\Release\llama-manager-agent.exe deploy
& (Join-Path $qtRoot "bin\windeployqt.exe") --release --qmldir apps\gui\qml deploy\llama-manager.exe
& (Join-Path $qtRoot "bin\windeployqt.exe") --release deploy\llama-manager-agent.exe
```

The target computer needs a compatible MSVC x64 Redistributable. Do not distribute the Debug CRT.

If windeployqt cannot locate Visual Studio, run it from a Visual Studio Developer PowerShell, or set `$env:VCINSTALLDIR` to your Visual Studio `VC` directory and add `--compiler-runtime`. The resulting redistributable installer is not run automatically.

## Project structure

```text
apps/agent/main.cpp           Agent entry point and single-instance lock
apps/gui/main.cpp             QML entry point and AgentClient injection
apps/gui/LanguageManager.*    Runtime language switching with QTranslator
apps/gui/qml/Main.qml         Model grid (cards + add button) and page navigation
apps/gui/qml/ModelCard.qml    One model's status with Start/Stop toggle and Restart
apps/gui/qml/ModelPage.qml    Model detail: controls, profile editor, logs, restart history, delete
apps/gui/qml/ProfileEditor.qml  Paths, arguments, and file dialogs
apps/gui/qml/LogView.qml       Console: search, level filters, pause, copy, export
apps/gui/qml/ResourcePanel.qml System resources dashboard (CPU, RAM, GPU, VRAM)
apps/gui/qml/MetricTile.qml    One dashboard tile with a chart
apps/gui/qml/LineChart.qml     Canvas time-series chart
apps/gui/qml/UsageLabel.qml    CPU/RAM/VRAM of one model process
core/Profile.*               Data types, validation, and argument arrays
core/ProfileStore.*          Atomic persistence and backup recovery
core/LlamaProcessManager.*    Asynchronous QProcess lifecycle and state
core/ServerSupervisor.*       Per-model watchdog state machine and restart history
core/HealthMonitor.*          Asynchronous GET /health polling
core/RestartPolicy.*          Restart limits, cooldown, and exponential backoff
core/LogStore.*               Bounded replay and disk rotation
core/SystemMonitor.*          System and per-process CPU/RAM
core/GpuMonitor.*             NVIDIA GPUs through runtime-loaded NVML
core/MetricsCollector.*       Sampling thread and 5-minute history
core/AppSettings.*            settings.json
ipc/Protocol.*               Framing, versioning, and size limits
ipc/AgentServer.*            Commands and status/log events
ipc/AgentClient.*            Asynchronous connection, retries, and timeouts
ipc/LogModel.*               Console lines: levels, filters, pause, export
tests/FakeServer.cpp         Controllable console child process
tests/FakeNvml.cpp           Stand-in NVML library for GPU tests
tests/Tests.cpp              Qt Test and cross-process integration tests
translations/                Qt Linguist translation sources (.ts)
docs/                       Architecture, protocol, testing, and roadmap
```

See [docs/testing.md](docs/testing.md) for recorded results and unverified areas, [docs/phase2.md](docs/phase2.md) for the watchdog, and [docs/phase3.md](docs/phase3.md) for the dashboard and console.
