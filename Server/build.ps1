# Сборка MeetUpServer (Qt6 MinGW).
# Запуск:  powershell -ExecutionPolicy Bypass -File build.ps1
$ErrorActionPreference = 'Stop'

# --- Пути к комплекту Qt и инструментам (правь здесь, если Qt переедет) ---
$QtKit = 'P:\Qt\QTSHIT\6.11.0\mingw_64'
$MinGW = 'P:\Qt\QTSHIT\Tools\mingw1310_64'
$CMake = 'P:\Qt\QTSHIT\Tools\CMake_64\bin'
$Ninja = 'P:\Qt\QTSHIT\Tools\Ninja'

$src = $PSScriptRoot
$env:PATH = "$MinGW\bin;$CMake;$Ninja;$QtKit\bin;$env:PATH"

Write-Host "== Configure ==" -ForegroundColor Cyan
& "$CMake\cmake.exe" -G Ninja -S $src -B "$src\build" `
    -DCMAKE_CXX_COMPILER="$MinGW\bin\g++.exe" `
    -DCMAKE_PREFIX_PATH="$QtKit" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "== Build ==" -ForegroundColor Cyan
& "$CMake\cmake.exe" --build "$src\build"
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "== Deploy Qt DLLs ==" -ForegroundColor Cyan
& "$QtKit\bin\windeployqt.exe" --no-translations --compiler-runtime "$src\build\MeetUpServer.exe" | Out-Null

Write-Host "OK -> $src\build\MeetUpServer.exe" -ForegroundColor Green
