# ============================================================
#  MeetUp — обновление с GitHub и пересборка Docker-контейнера (Windows)
#  Использование:
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Force
#  Переопределить HTTP-порт хоста:
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -HttpPort 8080
# ============================================================
param(
    [switch]$Force,
    [int]$HttpPort = 80
)
$ErrorActionPreference = 'Stop'

# --- Параметры ---
$Image     = 'meetup-server'
$Container = 'meetup-server'
# WS-порт жёстко зашит в web/index.html (ws://host:9000) — маппинг 9000:9000.
$WsPort    = 9000

# Скрипт лежит в tools/; все операции идут из корня репозитория — на уровень выше.
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

Write-Host ""
Write-Host "=== 1/4 Получение новой версии из GitHub ===" -ForegroundColor Cyan
$oldRev = (git rev-parse HEAD 2>$null); if (-not $oldRev) { $oldRev = 'none' }

git pull --ff-only
if ($LASTEXITCODE -ne 0) { throw "git pull failed" }

$newRev = (git rev-parse HEAD 2>$null); if (-not $newRev) { $newRev = 'none' }

if (-not $Force -and $oldRev -eq $newRev) {
    Write-Host "Новых коммитов нет (HEAD = $newRev)."
    Write-Host "Пересборка не требуется. Запустите с -Force, чтобы пересобрать принудительно." -ForegroundColor Yellow
    exit 0
}

Write-Host ""
Write-Host "=== 2/4 Сборка Docker-образа `"$Image`" ===" -ForegroundColor Cyan
docker build -t $Image .
if ($LASTEXITCODE -ne 0) { throw "docker build failed" }

Write-Host ""
Write-Host "=== 3/4 Перезапуск контейнера `"$Container`" ===" -ForegroundColor Cyan

# Снести старый контейнер, если он есть. Проверяем наличие ЗАРАНЕЕ:
# docker rm -f несуществующего контейнера пишет в stderr, а при
# $ErrorActionPreference='Stop' это валит скрипт (глушить 2>$null не помогает).
$existing = docker ps -aq --filter "name=^$Container$"
if ($existing) { docker rm -f $Container | Out-Null }

docker run -d `
  --name $Container `
  --restart unless-stopped `
  -p "$($HttpPort):80" `
  -p "$($WsPort):9000" `
  $Image
if ($LASTEXITCODE -ne 0) { throw "docker run failed" }

Write-Host ""
Write-Host "=== 4/4 Проверка ===" -ForegroundColor Cyan
docker ps --filter "name=$Container"
Write-Host ""
Write-Host "Готово. HEAD = $newRev" -ForegroundColor Green
Write-Host "Веб-клиент:  http://localhost:$HttpPort"
Write-Host "WebSocket:   ws://localhost:$WsPort"
