# Phase 3 — Dashboard

Status: implemented and tested on Linux with fakes (see testing.md). **Not yet built or run on Windows.** Multiple profiles and multiple server instances were already delivered with the multi-model work; this phase adds resource monitoring, charts, and the improved log viewer.

## Components

- `core/SystemMonitor` — system CPU and RAM, and CPU/RAM of each managed llama-server PID. Windows: `GetSystemTimes`, `GlobalMemoryStatusEx`, `GetProcessTimes`, `GetProcessMemoryInfo`. Linux (development/tests): `/proc`. CPU values need two samples, so the first sample of the system or of a new PID is `-1`.
- `core/GpuMonitor` — NVIDIA GPUs through NVML, **loaded at runtime** with QLibrary (`nvml.dll` from the system path, then `%ProgramW6432%\NVIDIA Corporation\NVSMI\nvml.dll`; `libnvidia-ml.so.1` on Linux). Nothing is linked at build time and the NVIDIA SDK is not needed. Without NVML, without an NVIDIA GPU, or when NVML fails to initialize, the GPU is reported as unavailable with a reason and everything else keeps working.
- `core/MetricsCollector` — runs both monitors on a dedicated low-priority thread every `metricsIntervalSeconds`, so slow system or NVML calls never block the Agent's IPC thread. A new sample is never queued behind a slow one. Keeps the last 5 minutes (at most 600 points) of compact history.
- `core/AppSettings` / `SettingsStore` — `settings.json` (`{"version":1,"settings":{...}}`) next to `profiles.json`, written atomically with QSaveFile. An invalid file falls back to defaults with a warning and is not overwritten until settings are changed.
- `ipc/LogModel` — the GUI console as a list model: per-line level, search, level filters, pause, copy, export.

## What the numbers mean

| Value | Meaning |
| --- | --- |
| System CPU | All processes, all logical processors |
| System RAM | Physical memory in use (total − available) |
| Model CPU | That llama-server process; 100% = all logical processors busy (same scale as Task Manager) |
| Model RAM | That process's working set (resident memory). With mmap, model pages that are mapped in count here |
| GPU utilization, VRAM, temperature, power | **Whole GPU, all processes** (NVML device values) |
| Model VRAM | That process's own GPU memory, only when the driver reports it; `n/a` otherwise |

Per spec 8, whole-card VRAM is never presented as one model's usage. On Windows the usual WDDM driver mode does not report per-process GPU memory, so model cards typically show `VRAM n/a` while the dashboard shows the whole-card total. Values are `-1` in the protocol when not available. `gpuMemoryBytes` is `0` when NVML works and the process is not using any GPU.

Only the direct llama-server process is measured, not other members of its Job Object.

## Settings

| Field | Default | Range |
| --- | --- | --- |
| metricsIntervalSeconds | 2 | 1–10 |

Changed from the dashboard ("Refresh every") through `updateSettings`; takes effect immediately and survives Agent restarts.

## GUI

- The model grid starts with a **System resources** panel: CPU, RAM, GPU, and VRAM tiles, each with the current value and a 5-minute chart. With several GPUs a selector picks the GPU shown. Charts are drawn with a Canvas (no Qt Charts dependency); gaps in the data (Agent offline, values unavailable) are drawn as gaps.
- Model cards and the model page show the process's CPU, RAM, and (when reported) VRAM.
- **Console**: search (case-insensitive), Errors / Warnings / Info toggles with counts, Pause / Resume (new lines are held and shown on resume), auto-scroll, Copy and Export (all lines, or only matching ones), Clear view (never deletes files on disk). Lines are colored by level.

llama.cpp writes nearly everything to stderr without structured levels, so the level is inferred from the text: lines starting with `E ` or containing words such as `error`, `failed`, `fatal`, `exception` are errors; `W `, `warning`, `deprecated`, `unhealthy`, `backoff` are warnings; everything else is info. This is a display aid, not a parser of llama.cpp internals.

The GUI keeps at most 5,000 lines per model; the Agent still keeps its own bounded replay and rotated files (see README).

## Not implemented

- Charts of a single model's CPU/RAM over time (the history keeps system and GPU values only).
- Measuring every process in a model's Job Object.
- GPUs from other vendors (AMD, Intel) — only NVML is supported.
