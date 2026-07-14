<#
.SYNOPSIS
  Produce a clean, self-contained folder you can run by double-click or copy to
  another PC: just Win11Client.exe + the minimum Qt runtime it needs. All QML,
  fonts and icons are already baked into the .exe as resources, so nothing else
  is loose. No build junk (that stays in out/build).
.EXAMPLE
  scripts\deploy.ps1                 # Release -> dist\Release
  scripts\deploy.ps1 -Launch         # deploy, then run it
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Release",
    [string]$QtDir = $env:QTDIR,
    [switch]$Launch,
    # -Portable: bundle the MSVC runtime + D3D12 shader compiler so it runs on a
    # bare PC that lacks the VC++ redistributable. Default is lean (assumes the
    # runtime is present, which every up-to-date Win11 has).
    [switch]$Portable
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not $QtDir) { $QtDir = "P:/Qt/6.11.1/msvc2022_64" }
$windeployqt = Join-Path $QtDir "bin/windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found at $windeployqt. Pass -QtDir <path-to-qt-kit>."
}

# Fresh build.
& (Join-Path $PSScriptRoot "build.ps1") -Config $Config

$exe  = Join-Path $root "out/build/$($Config.ToLower())/Win11Client.exe"
$dist = Join-Path $root "dist/$Config"

Write-Host "==> Staging clean folder: $dist" -ForegroundColor Cyan
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item $exe $dist

# Slim deployment: drop things this app doesn't need.
$flags = @(
    "--qmldir", (Join-Path $root "qml"),  # scan our QML for the exact runtime imports
    "--no-translations",                  # no Qt .qm translation catalogs
    "--no-system-d3d-compiler",           # no d3dcompiler_47.dll
    "--no-opengl-sw"                      # no ~20 MB software-OpenGL fallback
)
if ($Portable) { $flags += "--compiler-runtime" }
if ($Config -eq "Release") { $flags += "--release" } else { $flags += "--debug" }

Write-Host "==> Running windeployqt" -ForegroundColor Cyan
& $windeployqt @flags (Join-Path $dist "Win11Client.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed ($LASTEXITCODE)" }

# Lean mode: drop bits this app never loads at runtime.
#   dxcompiler/dxil  - D3D12 shader compiler (~15 MB); default RHI is D3D11 with
#                      precompiled shaders.
#   vc_redist.x64    - the VC++ installer (only staged with -Portable anyway).
#   qmltooling/      - QML debugger/profiler plugins (release build never loads).
#   generic/         - touch (qtuiotouch) plugin; desktop uses mouse.
#   iconengines/     - QIcon SVG engine; we render SVG via Image, not QIcon.
# NOTE: Qt6Network.dll + networkinformation/ + tls/ are kept: Qt6Qml hard-links
# Qt6Network (QML XMLHttpRequest), and TLS is needed for wss:// from M1 on.
if (-not $Portable) {
    Remove-Item -Force -Recurse -ErrorAction SilentlyContinue `
        (Join-Path $dist "dxcompiler.dll"), (Join-Path $dist "dxil.dll"), (Join-Path $dist "vc_redist.x64.exe"), `
        (Join-Path $dist "qmltooling"), (Join-Path $dist "generic"), (Join-Path $dist "iconengines")
}

# Report footprint.
$size = (Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum
$mb = [math]::Round($size / 1MB, 1)
$count = (Get-ChildItem $dist -Recurse -File).Count
Write-Host ""
Write-Host "==> Done. $dist" -ForegroundColor Green
Write-Host "    $count files, $mb MB - run Win11Client.exe directly." -ForegroundColor Green

if ($Launch) { & (Join-Path $dist "Win11Client.exe") }
