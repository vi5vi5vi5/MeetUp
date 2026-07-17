<#
.SYNOPSIS
  Build the MeetUp Win11 client. Configures first if needed.
.EXAMPLE
  scripts\build.ps1 -Config Debug
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $root "out/build/$($Config.ToLower())"

. (Join-Path $PSScriptRoot "_vsdevenv.ps1")
Enter-MsvcEnv

# Always (re)configure: cmake --preset is cheap/idempotent when nothing changed,
# but it's the only thing that picks up drift (new deps in vcpkg.json, edited
# CMakeLists.txt/presets) for a build dir that was configured earlier. Skipping
# this when build.ninja already exists left stale caches (e.g. missing the
# vcpkg toolchain after Opus was added) that ninja's own incremental
# "re-running CMake" step can't fix, since it just replays the old cache.
& (Join-Path $PSScriptRoot "configure.ps1") -Config $Config

Push-Location $root
try {
    Write-Host "==> Building ($Config)" -ForegroundColor Cyan
    cmake --build $binDir
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    Write-Host "==> Built -> $binDir/Win11Client.exe" -ForegroundColor Green
}
finally { Pop-Location }
