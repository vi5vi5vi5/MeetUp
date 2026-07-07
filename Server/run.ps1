# Запуск собранного сервера. Аргументы пробрасываются в exe.
# Примеры:
#   powershell -ExecutionPolicy Bypass -File run.ps1
#   powershell -ExecutionPolicy Bypass -File run.ps1 --ws-port 9500 --http-port 8081
$exe = Join-Path $PSScriptRoot 'build\MeetUpServer.exe'
if (-not (Test-Path $exe)) {
    Write-Host "Build the project first: build.ps1" -ForegroundColor Yellow
    exit 1
}
& $exe @args
