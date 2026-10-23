# Llama Server Manager

**A Windows desktop app for running and supervising local [llama.cpp](https://github.com/ggml-org/llama.cpp) `llama-server` instances.**

Point it at your own `llama-server.exe` and GGUF models, then start, stop, and monitor any number of models from one window. A background Agent keeps them running when the window is closed, restarts crashed or unresponsive servers, and shows CPU, RAM, GPU, and VRAM usage live.

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-0078D6)
![Qt](https://img.shields.io/badge/Qt-6.5%2B-41CD52)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![CMake](https://img.shields.io/badge/CMake-3.24%2B-064F8C)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

![Llama Server Manager dashboard with five models and live CPU, RAM, GPU, and VRAM charts](docs/images/dashboard.png)

## Features

- **Many models, one window** — every model is a card with its state, address, PID, uptime, and one-click Start/Stop and Restart. Each model has its own profile: executable, GGUF file, context size, GPU layers, threads, parallel slots, extra arguments, and environment variables.
- **Keeps running without the GUI** — models are owned by a separate Agent process. Close the window (or let it crash) and your servers keep serving; reopen it and everything reconnects.
- **Watchdog** — HTTP health checks on `/health`, crash detection, and automatic restarts with exponential backoff and a restart limit, so a broken model does not restart forever. A manual Stop is always respected. Every failure is kept in a restart history.
- **Live resource dashboard** — system CPU and RAM plus NVIDIA GPU utilization, VRAM, temperature, and power, each with a 5-minute chart. Every model card also shows that server's own CPU, RAM, and VRAM.
- **Console** — live stdout/stderr per model with search, Error/Warning/Info filters, pause, copy, and export. Logs are also written to disk with size limits and rotation.
- **Convenience** — start chosen models automatically with the Agent, duplicate a profile to try other settings, and stop everything with one Stop Agent button.
- **English and Traditional Chinese** user interface, switchable at any time.
- **Nothing extra to install** — no Docker, Python, Ollama, or LM Studio. It uses the llama.cpp build and models you already have and never modifies them.

## Screenshots

| Console with level filters | 繁體中文介面 |
| --- | --- |
| ![A model's console with search, error/warning/info filters, and llama-server output](docs/images/console.png) | ![The dashboard in Traditional Chinese](docs/images/dashboard-zh-TW.png) |

The screenshots use sample data produced by the test doubles in [`tests/`](tests) (a stand-in server and NVML library), not a real GPU.

## How it works

```text
llama-manager.exe  ⇄  local named pipe (JSON)  ⇄  llama-manager-agent.exe  ─┬─▶ llama-server.exe  (model A)
   Qt Quick GUI                                     background Agent         ├─▶ llama-server.exe  (model B)
                                                                             └─▶ …
```

- The **GUI** only displays state and sends commands. It starts the Agent if none is running.
- The **Agent** owns every `llama-server` process (each in its own Windows Job Object), runs the watchdog and resource sampling, and stores profiles, settings, and logs. Only one Agent runs per data directory, and it never touches `llama-server` processes it did not start.
- Arguments are passed to `llama-server` as a list, never through a shell, so paths with spaces or non-ASCII characters work.

More detail: [architecture](docs/architecture.md) · [IPC protocol](docs/ipc-protocol.md)

## Requirements

- Windows 10 or 11, x64
- A `llama-server.exe` from [llama.cpp](https://github.com/ggml-org/llama.cpp) (any backend: CPU, CUDA, Vulkan, …) and one or more `.gguf` models
- Optional: an NVIDIA GPU and driver for GPU/VRAM monitoring (NVML ships with the driver). Without it, everything except the GPU tiles works as usual.
- To build: Qt 6.5+ (MSVC x64), CMake 3.24+, and Visual Studio 2022 or 2026 with the C++ desktop workload

There are no prebuilt releases yet; [build from source](#building-from-source) as shown below.

## Getting started

1. Run `llama-manager.exe`. It starts the Agent in the background.
2. Click the **+** card, give the model a name, and use **Browse…** to select your `llama-server.exe` and `.gguf` file. Adjust the port, context size, GPU layers, and other options, then click **Add model**.
3. Click **▶ Start** on the card. The Agent checks the executable's `--help` output, launches it, and keeps the model in `STARTING` until `GET /health` answers 200, then shows `RUNNING`.
4. Point your client at `http://127.0.0.1:<port>` (llama-server's OpenAI-compatible API).

Click a card to open its page with the full profile, the **Console**, and the **Restart history**.

### Everyday use

| You want to… | Do this |
| --- | --- |
| Stop one model | Its **■ Stop** button. Pending automatic restarts are cancelled. |
| Close the window, keep models running | Close the window. Reopen it any time. |
| Stop all models and the Agent | **Stop Agent** (top right), then confirm. Start it again with **Start Agent**. |
| Start a model whenever the Agent starts | Check **Start automatically when the Agent starts** in its profile. |
| Try different settings | **Duplicate** on the model page creates an unsaved copy on the next free port. |
| Change how often resources refresh | **Refresh every** (1–10 s) above the resource tiles. |
| Switch language | The **EN / 中文** button. |

Extra arguments are entered as a JSON array (`["--flash-attn", "on"]`) and environment variables as a JSON object (`{"CUDA_VISIBLE_DEVICES": "0"}`). Options the manager sets itself (`-m`, `-c`, `-ngl`, `-t`, `--host`, `--port`, `--parallel`, `--metrics`) cannot be overridden there.

### Watchdog defaults

With the defaults, `/health` is checked every 30 s with a 5 s timeout; 3 failures in a row mark a model `UNHEALTHY`. A model gets 600 s to load. After a crash, a failed start, or failed health checks, it is restarted after 5 s, then 10 s, 20 s, … at most 3 times in 10 minutes before it is left `FAILED`. All of this is configurable per model in its **Watchdog** section; see [docs/phase2.md](docs/phase2.md).

## Data and settings

Everything is stored per user in `%APPDATA%\LlamaServerManager\`:

| Path | Contents |
| --- | --- |
| `profiles.json` | Model profiles (up to 64), written atomically with a `.bak` of the previous version |
| `settings.json` | Agent settings, currently the resource refresh interval |
| `logs\<profile-id>\server.jsonl` | Console output, 4 MiB per file, 3 rotated files kept |
| `logs\<profile-id>\restart-history.json` | Last 50 watchdog events |

Deleting a model deletes its log folder; the model file itself is never touched. Clearing the console only clears the screen.

### Notes and limitations

- GPU values on the dashboard are for the **whole GPU**. A model's own VRAM is shown only when the driver reports it; Windows WDDM drivers usually do not, so it reads `VRAM n/a`.
- Only NVIDIA GPUs are monitored (through NVML). Other GPUs still work with llama.cpp; they are just not shown.
- The Agent refuses to start a model on a port another running model uses, but it does not check whether there is enough VRAM for all of them.
- The Agent's named pipe is limited to the current Windows user. It is not a security boundary against other programs running as that user.
- Log levels in the console are inferred from the text, since llama.cpp does not label its output.

## Building from source

Install Qt 6.5 or later with the **MSVC x64** components (Core, Network, Quick, Quick Controls, Test, Linguist Tools), CMake, and Visual Studio with the *Desktop development with C++* workload and a Windows SDK. In PowerShell, from the project root:

```powershell
$qtRoot = Read-Host "Qt MSVC x64 package directory"   # e.g. C:\Qt\6.8.0\msvc2022_64
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 "-DCMAKE_PREFIX_PATH=$qtRoot"
cmake --build build --config Release --parallel
```

Use the `Visual Studio 17 2022` generator for VS 2022. If MSBuild stalls in a restricted environment, build with `-- /m:1 /nr:false` instead of `--parallel`.

### Run the tests

```powershell
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
```

The tests need no GPU or model: they use a small fake `llama-server` and a fake NVML library. See [docs/testing.md](docs/testing.md) for what is covered, the recorded results, and how to run the optional test against a real model.

### Create a runnable folder

Keep both executables in the same folder and let `windeployqt` add the Qt runtime:

```powershell
New-Item -ItemType Directory -Force deploy | Out-Null
Copy-Item build\Release\llama-manager.exe, build\Release\llama-manager-agent.exe deploy
& (Join-Path $qtRoot "bin\windeployqt.exe") --release --qmldir apps\gui\qml deploy\llama-manager.exe
& (Join-Path $qtRoot "bin\windeployqt.exe") --release deploy\llama-manager-agent.exe
```

The target computer needs the Microsoft Visual C++ x64 Redistributable. If `windeployqt` cannot find Visual Studio, run it from a *Developer PowerShell for VS* or add `--compiler-runtime` with `$env:VCINSTALLDIR` set.

### Translations

UI strings use Qt's `qsTr()`; translations live in [`translations/`](translations). After changing strings, refresh and edit them, then rebuild:

```powershell
cmake --build build --target llama-manager_lupdate
& (Join-Path $qtRoot "bin\linguist.exe") translations\llama-manager_zh_TW.ts
```

To add a language, add its `.ts` file to `qt_add_translations` in `CMakeLists.txt` and list it in `apps/gui/LanguageManager.cpp`.

## Project layout

```text
apps/gui/       Qt Quick GUI (QML pages and components, language switching)
apps/agent/     Agent entry point and single-instance lock
core/           Profiles, process management, watchdog, health checks, resource monitors
ipc/            JSON protocol, Agent server, GUI client, console model
tests/          Qt Test suite, fake llama-server, fake NVML library
translations/   Qt Linguist sources
docs/           Design and test documentation
```

| Document | About |
| --- | --- |
| [SPEC.md](SPEC.md) | Original specification (Traditional Chinese) |
| [docs/architecture.md](docs/architecture.md) | Processes, states, ownership, and limits |
| [docs/ipc-protocol.md](docs/ipc-protocol.md) | GUI ⇄ Agent messages |
| [docs/phase2.md](docs/phase2.md) | Watchdog and restart policy |
| [docs/phase3.md](docs/phase3.md) | Resource monitoring and console |
| [docs/testing.md](docs/testing.md) | Test coverage and results |

## Roadmap

Done: GUI and Agent (Phase 1), watchdog (Phase 2), dashboard, multiple models, and console (Phase 3), plus profile duplication, auto start, and Stop Agent.

Next, Windows integration (Phase 4):

- System tray icon with status and Start/Stop/Restart
- Start the Agent at sign-in through Task Scheduler
- Optional Windows Service for the Agent
- Installer, uninstaller, and release packages

## Contributing

Issues and pull requests are welcome. Please:

- Format C++ with the repository's `.clang-format` (WebKit style) and **always use braces**, even for one-line `if` bodies — see [AGENTS.md](AGENTS.md).
- Write commit messages in [Conventional Commits](https://www.conventionalcommits.org/) style (`feat:`, `fix:`, `docs:` …).
- Run the test suite before opening a pull request, and add tests for new behavior.

## License

Llama Server Manager is released under the [MIT License](LICENSE).

It is built with [Qt](https://www.qt.io/), which is used under the GNU LGPL v3 and linked dynamically; release packages must include Qt's license notices. llama.cpp and model files are not included and keep their own licenses.
