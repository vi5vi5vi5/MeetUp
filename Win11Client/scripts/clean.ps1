<#
.SYNOPSIS
  Remove build junk. By default wipes the intermediate build trees (out/); pass
  -All to also remove the deployed dist/ folders.
.EXAMPLE
  scripts\clean.ps1          # wipe out/
  scripts\clean.ps1 -All     # wipe out/ and dist/
#>
param([switch]$All)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$targets = @("out")
if ($All) { $targets += "dist" }

foreach ($t in $targets) {
    $p = Join-Path $root $t
    if (Test-Path $p) {
        Remove-Item -Recurse -Force $p
        Write-Host "removed $t/" -ForegroundColor Green
    } else {
        Write-Host "$t/ already clean" -ForegroundColor DarkGray
    }
}
