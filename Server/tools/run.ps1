# Запуск собранного сервера. Аргументы пробрасываются в exe.
# Примеры:
#   powershell -ExecutionPolicy Bypass -File tools\run.ps1
#   powershell -ExecutionPolicy Bypass -File tools\run.ps1 --ws-port 9500 --http-port 8081
# Скрипт лежит в tools/; собранный exe — в build/ в корне репозитория (на уровень выше).
$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot 'build\MeetUpServer.exe'
if (-not (Test-Path $exe)) {
    Write-Host "Build the project first: tools\build.ps1" -ForegroundColor Yellow
    exit 1
}
& $exe @args
