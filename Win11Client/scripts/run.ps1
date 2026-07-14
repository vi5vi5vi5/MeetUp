<#
.SYNOPSIS
  Build (if needed) and launch the MeetUp Win11 client.
.EXAMPLE
  scripts\run.ps1 -Config Debug
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$QtDir = $env:QTDIR
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot "build.ps1") -Config $Config

$exe = Join-Path $root "out/build/$($Config.ToLower())/Win11Client.exe"
if (-not (Test-Path $exe)) { throw "executable not found: $exe" }

# The build tree links against Qt but doesn't copy its DLLs; put the Qt kit's
# bin on PATH so the app finds Qt6*.dll when launched outside the IDE.
# (For a self-contained copy use scripts\deploy.ps1.)
if (-not $QtDir) { $QtDir = "P:/Qt/6.11.1/msvc2022_64" }
$qtBin = Join-Path $QtDir "bin"
if (Test-Path $qtBin) { $env:PATH = "$qtBin;$env:PATH" }

Write-Host "==> Launching $exe" -ForegroundColor Green
& $exe
