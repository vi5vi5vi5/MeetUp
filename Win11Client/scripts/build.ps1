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

if (-not (Test-Path (Join-Path $binDir "build.ninja"))) {
    & (Join-Path $PSScriptRoot "configure.ps1") -Config $Config
}

Push-Location $root
try {
    Write-Host "==> Building ($Config)" -ForegroundColor Cyan
    cmake --build $binDir
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    Write-Host "==> Built -> $binDir/Win11Client.exe" -ForegroundColor Green
}
finally { Pop-Location }
