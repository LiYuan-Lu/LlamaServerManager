<#
.SYNOPSIS
  Builds Llama Server Manager in Release mode and packages it as a Windows installer.

.DESCRIPTION
  1. Configures and builds the project with CMake (tests are skipped).
  2. Runs `cmake --install` into packaging\windows\out\stage. Qt's deployment script copies
     the Qt libraries, plugins, and QML modules next to the executables (windeployqt).
  3. Copies Qt's license texts, if found in the Qt installation.
  4. Finds vc_redist.x64.exe from Visual Studio so the installer can install the runtime.
  5. Compiles LlamaServerManager.iss with Inno Setup 6.

  The result is packaging\windows\out\LlamaServerManager-<version>-windows-x64-setup.exe.

.EXAMPLE
  .\packaging\windows\build-installer.ps1 -QtRoot C:\Qt\6.8.0\msvc2022_64
#>
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$Generator = "Visual Studio 18 2026",
    [string]$BuildDir,
    [string]$InnoSetupCompiler
)
$ErrorActionPreference = "Stop"

function Invoke-Checked([string]$what, [scriptblock]$command) {
    & $command
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $root "build-release" }
$out = Join-Path $PSScriptRoot "out"
$stage = Join-Path $out "stage"

$match = Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(LlamaServerManager VERSION ([0-9.]+)'
if (-not $match) { throw "Cannot read the version from CMakeLists.txt" }
$version = $match.Matches[0].Groups[1].Value
Write-Host "Building Llama Server Manager $version" -ForegroundColor Cyan

Invoke-Checked "CMake configure" { cmake -S $root -B $BuildDir -G $Generator -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" -DBUILD_TESTING=OFF }
Invoke-Checked "Build" { cmake --build $BuildDir --config Release --parallel }

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
Invoke-Checked "Install" { cmake --install $BuildDir --config Release --prefix $stage }
if (-not (Test-Path (Join-Path $stage "bin\llama-manager.exe"))) { throw "cmake --install did not produce bin\llama-manager.exe" }
if (-not (Test-Path (Join-Path $stage "bin\Qt6Core.dll"))) { throw "Qt was not deployed into $stage\bin (Qt 6.5 or later is required)" }

# Qt's LGPL obligations: ship its license texts when the Qt installation has them.
# Select-Object -First 1 always yields a single path (indexing a one-item pipeline result
# would index the characters of the string instead).
$licenseDir = @((Join-Path $QtRoot "..\..\Licenses"), (Join-Path $QtRoot "Licenses")) |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -First 1
if ($licenseDir) {
    $licenseDir = (Resolve-Path -LiteralPath $licenseDir).Path
    $target = New-Item -ItemType Directory -Force (Join-Path $stage "qt-licenses")
    Get-ChildItem -LiteralPath $licenseDir -File | Where-Object { $_.Name -match 'GPL|LGPL|LICENSE' } | Copy-Item -Destination $target
    Write-Host "Copied Qt license texts from $licenseDir"
} else {
    Write-Warning "Qt license texts not found next to $QtRoot; add them to the package manually."
}

# The Visual C++ runtime installer from the newest Visual Studio installation.
$vcRedist = $null
if ($env:VCToolsRedistDir -and (Test-Path (Join-Path $env:VCToolsRedistDir "vc_redist.x64.exe"))) {
    $vcRedist = Join-Path $env:VCToolsRedistDir "vc_redist.x64.exe"
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        if ($vs) {
            $vcRedist = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC\*\vc_redist.x64.exe") -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
        }
    }
}
if (-not $vcRedist) { Write-Warning "vc_redist.x64.exe not found; the installer will not install the Visual C++ runtime." }

if (-not $InnoSetupCompiler) {
    $InnoSetupCompiler = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $InnoSetupCompiler) { throw "Inno Setup 6 not found. Install it (e.g. winget install JRSoftware.InnoSetup) or pass -InnoSetupCompiler." }

$isccArgs = @("/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$out")
if ($vcRedist) { $isccArgs += "/DVcRedist=$vcRedist" }
Invoke-Checked "Inno Setup" { & $InnoSetupCompiler @isccArgs (Join-Path $PSScriptRoot "LlamaServerManager.iss") }

$setup = Join-Path $out "LlamaServerManager-$version-windows-x64-setup.exe"
Write-Host "Installer: $setup" -ForegroundColor Green
