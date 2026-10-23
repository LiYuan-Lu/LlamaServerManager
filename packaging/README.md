# Packaging

Every platform starts from the same step: `cmake --install` produces a self-contained,
runnable folder with the executables and the Qt libraries, plugins, and QML modules they need
(Qt's deployment scripts run `windeployqt` on Windows and `macdeployqt` on macOS). Each
platform folder then turns that folder into an installer.

| Folder | Output | Status |
| --- | --- | --- |
| [`windows/`](windows) | `LlamaServerManager-<version>-windows-x64-setup.exe` (Inno Setup 6) and `LlamaServerManager-<version>-windows-x64.zip` | Available |
| `macos/` | `.dmg` with `Llama Server Manager.app` | Planned |
| `linux/` | AppImage or distribution packages | Planned |

## Windows

Requirements: Qt 6.5+ (MSVC x64), CMake, Visual Studio with the C++ desktop workload, and
[Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`).

```powershell
.\packaging\windows\build-installer.ps1 -QtRoot C:\Qt\6.8.0\msvc2022_64
```

The script picks the newest Visual Studio 2022 or 2026 with the C++ tools (or pass `-Generator`), and `-RunTests` builds and runs the test suite before packaging. It produces both files in `packaging\windows\out`.

The zip contains the same files the installer installs, plus the Visual C++ runtime DLLs next to the executables (app-local deployment), so it runs on a computer without the runtime. Profiles and logs still go to `%APPDATA%\LlamaServerManager`.

The installer:

- installs to `Program Files\Llama Server Manager` (the app itself is in `bin\`),
- installs the Microsoft Visual C++ runtime when it is missing,
- optionally starts the app at sign-in (on by default) and adds a desktop shortcut,
- before upgrading or uninstalling, runs `llama-manager.exe --quit` so the window, the Agent,
  and running models close cleanly,
- keeps profiles, settings, and logs in `%APPDATA%\LlamaServerManager` when uninstalled.

## Releasing

[`.github/workflows/release.yml`](../.github/workflows/release.yml) builds the Windows packages on GitHub Actions (Qt from aqtinstall, tests included):

1. Set the new version in `project(LlamaServerManager VERSION …)` in `CMakeLists.txt` and commit.
2. Tag the commit with the same version and push the tag:

   ```powershell
   git tag v0.1.0
   git push origin v0.1.0
   ```

   The workflow fails if the tag does not match the version in `CMakeLists.txt`.
3. The workflow attaches the installer, the zip, and `SHA256SUMS.txt` to a **draft** release with generated notes. Review the notes on GitHub and publish the release.

Running the workflow manually (Actions → Release → Run workflow) builds the same files and uploads them as a workflow artifact, without creating a release.

## Adding a platform

1. Make sure `cmake --install` produces a working folder on that platform (the app bundle
   properties for macOS are already set in `CMakeLists.txt`).
2. Add a `packaging/<platform>/` folder with the installer definition and a build script.
3. Platform-specific runtime behavior already has a backend per platform: starting at sign-in
   (`core/LoginStartup`) and the tray icon (`apps/gui/TrayIcon`, built on `QSystemTrayIcon`).
