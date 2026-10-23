# Phase 4 — Desktop integration (part 1)

Status: system tray, start at sign-in, single GUI instance, and the Windows installer are implemented. Windows Service support and Agent permission management are the next part. The design is cross-platform: Windows is the first complete target, and macOS and Linux have their own backends where the platforms differ.

## System tray

`apps/gui/TrayIcon` is built on `QSystemTrayIcon`, so the same code runs in the Windows notification area, the macOS menu bar, and Linux system trays.

- The icon shows a status dot: green (models running), amber (starting, unhealthy, or restarting), red (a model failed), gray (Agent not connected); no dot when everything is stopped. On macOS a monochrome template icon is used instead, as menu bar icons are tinted by the system.
- The tooltip shows how many models are running and how many failed.
- The menu: show/hide window, status, one submenu per model (Start/Stop, Restart; at most 20, then "more in the window"), open logs folder, start at sign-in, Stop Agent… (asks in the window) or Start Agent, and Exit. **Exit closes only the GUI**; the Agent and models keep running.
- Left click toggles the window on Windows and Linux; on macOS a click opens the menu, like every menu bar item.
- A notification appears when a model changes to FAILED while the GUI is running.
- The window's close button hides it to the tray. The first time, a notification explains that the app is still running. Where the desktop has no tray (`QSystemTrayIcon::isSystemTrayAvailable()` is false), closing the window exits the GUI as before.

## Start at sign-in

`core/LoginStartup` starts `llama-manager --minimized` (plus `--data-dir` when a non-default directory is used) when the user signs in. The GUI starts hidden in the tray and launches the Agent, which starts every model whose profile has **Start automatically when the Agent starts**. Each platform uses its own mechanism, and none needs administrator rights:

| Platform | Mechanism | Location |
| --- | --- | --- |
| Windows | Task Scheduler task, logon trigger for the current user only, normal priority, no time limit, 10 s delay | Task "Llama Server Manager - <user>" (created with `schtasks.exe /XML`) |
| macOS | LaunchAgent with `RunAtLoad` | `~/Library/LaunchAgents/llama-server-manager.login.plist` |
| Linux | XDG autostart entry | `~/.config/autostart/llama-server-manager.desktop` |

The Windows task uses priority 4 (normal) because a task's priority is inherited by the processes it starts, and the Task Scheduler default (below normal) would slow down every model. When the GUI starts, it reads the entry and rewrites it if it points at another copy of the program (for example after moving the installation). An entry turned off in the desktop's own startup settings (Linux `Hidden=true`, macOS `Disabled`) is shown as off.

Turn it on or off in the window's **⋯** menu or the tray menu, or from the command line with `llama-manager --set-autostart on|off`.

## One GUI per data directory

`apps/gui/SingleInstance` holds `gui.lock` in the data directory and listens on a local socket (`QLocalServer`, user-only access). A second launch, for example from the Start menu while the GUI sits in the tray, tells the first one to show its window and exits.

## Command line

| Option | Effect |
| --- | --- |
| `--minimized` | Start hidden in the tray (ignored when there is no tray) |
| `--set-autostart on\|off` | Turn starting at sign-in on or off, then exit (exit code 1 on failure) |
| `--quit` | Close the running GUI, stop the Agent and all models (like Stop Agent), wait up to 30 s, then exit. Used by installers before replacing files |
| `--data-dir <path>` | Use another data directory (all of the above apply to it) |

`--set-autostart` and `--quit` run without creating a window.

## Packaging

`cmake --install` produces a runnable folder on every platform; Qt's deployment script (Qt 6.5+) adds the Qt libraries, plugins, and QML modules. The license files are installed next to the program (inside the app bundle on macOS). The GUI is a macOS app bundle with its icon and the Agent built into `Contents/MacOS`; this is prepared but not yet packaged. See [packaging/README.md](../packaging/README.md).

The Windows installer (`packaging/windows/LlamaServerManager.iss`, Inno Setup 6) is built by `packaging/windows/build-installer.ps1`. It installs to Program Files, installs the Visual C++ runtime if needed, offers start at sign-in (on by default, created for the user who ran the installer) and a desktop shortcut, runs `--quit` before upgrading or uninstalling, removes the sign-in task on uninstall, and keeps the user's data. Both executables carry an icon and version information.

## Not implemented yet

- Windows Service mode for the Agent (`%PROGRAMDATA%` data directory, service account, pipe permissions for other users).
- macOS DMG and Linux packages.
- Code signing of the executables and installer.
