<#
.SYNOPSIS
  Builds Llama Server Manager in Release mode and packages it for Windows.

.DESCRIPTION
  1. Configures and builds the project with CMake. With -RunTests the test suite is built and
     run before packaging; otherwise tests are skipped.
  2. Runs `cmake --install` into packaging\windows\out\stage. Qt's deployment script copies
     the Qt libraries, plugins, and QML modules next to the executables (windeployqt).
  3. Copies Qt's license texts from the Qt installation, or from -QtLicenseDir when the Qt
     installation has none (for example Qt installed by aqtinstall in CI).
  4. Finds vc_redist.x64.exe from Visual Studio so the installer can install the runtime.
  5. Packs the stage folder into a zip that runs without installing. The Visual C++ runtime
     DLLs are copied next to the executables (app-local deployment), so the zip also runs on
     a computer without the runtime.
  6. Compiles LlamaServerManager.iss with Inno Setup.

  Results in packaging\windows\out:
    LlamaServerManager-<version>-windows-x64-setup.exe
    LlamaServerManager-<version>-windows-x64.zip

.EXAMPLE
  .\packaging\windows\build-installer.ps1 -QtRoot C:\Qt\6.8.0\msvc2022_64

.EXAMPLE
  .\packaging\windows\build-installer.ps1 -QtRoot C:\Qt\6.8.0\msvc2022_64 -RunTests
#>
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    # Default: the newest Visual Studio with the C++ tools (2022 or 2026).
    [string]$Generator,
    [string]$BuildDir,
    [string]$InnoSetupCompiler,
    [string]$QtLicenseDir,
    [switch]$RunTests
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

# The newest Visual Studio installation with the C++ tools.
$vsPath = $null
$vsVersion = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsArgs = @("-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64")
    $vsPath = & $vswhere @vsArgs -property installationPath | Select-Object -First 1
    $vsVersion = & $vswhere @vsArgs -property installationVersion | Select-Object -First 1
}
if (-not $Generator) {
    if (-not $vsVersion) { throw "Visual Studio with the C++ desktop workload was not found. Install it or pass -Generator." }
    $Generator = switch ([int]($vsVersion.Split('.')[0])) {
        17 { "Visual Studio 17 2022" }
        18 { "Visual Studio 18 2026" }
        default { throw "Visual Studio $vsVersion is not supported by this script; pass -Generator." }
    }
}
Write-Host "Generator: $Generator"

$testing = if ($RunTests) { "ON" } else { "OFF" }
Invoke-Checked "CMake configure" { cmake -S $root -B $BuildDir -G $Generator -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" "-DBUILD_TESTING=$testing" }
Invoke-Checked "Build" { cmake --build $BuildDir --config Release --parallel }
if ($RunTests) {
    Invoke-Checked "Tests" { ctest --test-dir $BuildDir -C Release --output-on-failure }
}

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
Invoke-Checked "Install" { cmake --install $BuildDir --config Release --prefix $stage }
if (-not (Test-Path (Join-Path $stage "bin\llama-manager.exe"))) { throw "cmake --install did not produce bin\llama-manager.exe" }
if (-not (Test-Path (Join-Path $stage "bin\Qt6Core.dll"))) { throw "Qt was not deployed into $stage\bin (Qt 6.5 or later is required)" }

# Qt's LGPL obligations: ship its license texts. Prefer the Qt installation's own folder.
# Select-Object -First 1 always yields a single path (indexing a one-item pipeline result
# would index the characters of the string instead).
$licenseCandidates = @((Join-Path $QtRoot "..\..\Licenses"), (Join-Path $QtRoot "Licenses"))
if ($QtLicenseDir) { $licenseCandidates += $QtLicenseDir }
$licenseDir = $licenseCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -First 1
if ($licenseDir) {
    $licenseDir = (Resolve-Path -LiteralPath $licenseDir).Path
    $target = New-Item -ItemType Directory -Force (Join-Path $stage "qt-licenses")
    Get-ChildItem -LiteralPath $licenseDir -File | Where-Object { $_.Name -match 'GPL|LGPL|LICENSE' } | Copy-Item -Destination $target
    Write-Host "Copied Qt license texts from $licenseDir"
} else {
    Write-Warning "Qt license texts not found next to $QtRoot; add them to the package manually or pass -QtLicenseDir."
}

# The Visual C++ runtime installer (and, next to it, the runtime DLLs) from Visual Studio.
$vcRedist = $null
if ($env:VCToolsRedistDir -and (Test-Path (Join-Path $env:VCToolsRedistDir "vc_redist.x64.exe"))) {
    $vcRedist = Join-Path $env:VCToolsRedistDir "vc_redist.x64.exe"
} elseif ($vsPath) {
    $vcRedist = Get-ChildItem (Join-Path $vsPath "VC\Redist\MSVC\*\vc_redist.x64.exe") -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $vcRedist) { Write-Warning "vc_redist.x64.exe not found; the installer will not install the Visual C++ runtime and the zip will not contain it." }

# The zip: the stage folder plus the Visual C++ runtime DLLs next to the executables.
$packageName = "LlamaServerManager-$version-windows-x64"
$portable = Join-Path $out $packageName
$zip = Join-Path $out "$packageName.zip"
if (Test-Path $portable) { Remove-Item -Recurse -Force $portable }
if (Test-Path $zip) { Remove-Item -Force $zip }
Copy-Item -Recurse $stage $portable
if ($vcRedist) {
    $crtDlls = Get-ChildItem (Join-Path (Split-Path $vcRedist) "x64\Microsoft.VC*.CRT\*.dll") -ErrorAction SilentlyContinue
    if ($crtDlls) {
        $crtDlls | Copy-Item -Destination (Join-Path $portable "bin")
        Write-Host "Added $($crtDlls.Count) Visual C++ runtime DLLs to the zip"
    } else {
        Write-Warning "Visual C++ runtime DLLs not found next to $vcRedist; the zip needs the runtime installed."
    }
}
Compress-Archive -Path $portable -DestinationPath $zip
Remove-Item -Recurse -Force $portable
Write-Host "Zip: $zip" -ForegroundColor Green

if (-not $InnoSetupCompiler) {
    $InnoSetupCompiler = @(${env:ProgramFiles(x86)}, $env:ProgramFiles, (Join-Path $env:LOCALAPPDATA "Programs")) |
        Where-Object { $_ } |
        ForEach-Object { Get-ChildItem (Join-Path $_ "Inno Setup *\ISCC.exe") -ErrorAction SilentlyContinue } |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $InnoSetupCompiler) { throw "Inno Setup not found. Install it (e.g. winget install JRSoftware.InnoSetup) or pass -InnoSetupCompiler." }

$isccArgs = @("/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$out")
if ($vcRedist) { $isccArgs += "/DVcRedist=$vcRedist" }
Invoke-Checked "Inno Setup" { & $InnoSetupCompiler @isccArgs (Join-Path $PSScriptRoot "LlamaServerManager.iss") }

$setup = Join-Path $out "$packageName-setup.exe"
Write-Host "Installer: $setup" -ForegroundColor Green
