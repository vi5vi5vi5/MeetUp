<#
.SYNOPSIS
  Configure the MeetUp Win11 client with CMake (MSVC / Ninja preset).
.EXAMPLE
  scripts\configure.ps1 -Config Debug
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$preset = "$Config-x64"

. (Join-Path $PSScriptRoot "_vsdevenv.ps1")
Enter-MsvcEnv

Push-Location $root
try {
    Write-Host "==> Configuring preset '$preset' (Qt: $env:QTDIR)" -ForegroundColor Cyan
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
    Write-Host "==> Configured -> out/build/$($Config.ToLower())" -ForegroundColor Green
}
finally { Pop-Location }
