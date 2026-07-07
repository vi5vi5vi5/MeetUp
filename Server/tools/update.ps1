# ============================================================
#  MeetUp — обновление с GitHub и пересборка (docker compose, Windows)
#  Использование:
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Force
#  Переопределить порты хоста:
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -HttpsPort 8443 -HttpPort 8081
# ============================================================
param(
    [switch]$Force,
    [int]$HttpPort = 0,
    [int]$HttpsPort = 0
)
$ErrorActionPreference = 'Stop'

# Порты пробрасываются в docker-compose.yml через переменные окружения.
if ($HttpPort -gt 0)  { $env:HTTP_PORT  = $HttpPort }
if ($HttpsPort -gt 0) { $env:HTTPS_PORT = $HttpsPort }

# Скрипт лежит в tools/; все операции идут из корня репозитория — на уровень выше.
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

Write-Host ""
Write-Host "=== 1/3 Получение новой версии из GitHub ===" -ForegroundColor Cyan
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
Write-Host "=== 2/3 Пересборка и перезапуск (docker compose) ===" -ForegroundColor Cyan
# up -d --build сам пересоберёт изменившиеся образы и перезапустит
# только те контейнеры, которые поменялись.
docker compose up -d --build
if ($LASTEXITCODE -ne 0) { throw "docker compose up failed" }

Write-Host ""
Write-Host "=== 3/3 Проверка ===" -ForegroundColor Cyan
docker compose ps

Write-Host ""
Write-Host "Готово. HEAD = $newRev" -ForegroundColor Green
$portSuffix = ""
if ($HttpsPort -gt 0 -and $HttpsPort -ne 443) { $portSuffix = ":$HttpsPort" }
Write-Host "Веб-клиент: https://<IP-сервера>$portSuffix/"
Write-Host "Сертификат самоподписанный — браузер предупредит; это ожидаемо."
