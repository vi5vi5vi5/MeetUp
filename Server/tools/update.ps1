# ============================================================
#  MeetUp — обновление с GitHub и пересборка (docker compose, Windows)
#  Использование:
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Force
#  Переопределить порты хоста:
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -HttpsPort 8443 -HttpPort 8081
#  С доменом и Let's Encrypt (HttpPort переопределять нельзя):
#    powershell -ExecutionPolicy Bypass -File tools\update.ps1 -Domain meetup.linkpc.net -Email you@mail.com
#
#  Домен, почта и порты ЗАПОМИНАЮТСЯ в файле .env рядом с docker-compose.yml:
#  указали -Domain один раз — дальше хватает запуска без единого флага.
# ============================================================
param(
    [switch]$Force,
    [int]$HttpPort = 0,
    [int]$HttpsPort = 0,
    [string]$Domain = "",
    [string]$Email  = ""
)
$ErrorActionPreference = 'Stop'

# Порты и домен пробрасываются в docker-compose.yml через переменные окружения.
if ($HttpPort -gt 0)  { $env:HTTP_PORT  = $HttpPort }
if ($HttpsPort -gt 0) { $env:HTTPS_PORT = $HttpsPort }
if ($Domain)          { $env:DOMAIN            = $Domain }
if ($Email)           { $env:LETSENCRYPT_EMAIL = $Email }

# Скрипт лежит в tools/; все операции идут из корня репозитория — на уровень выше.
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

# ---- .env: настройки установки, которые незачем вводить каждый раз ----
# docker compose читает этот файл сам, поэтому запомненного домена хватает и
# для «голого» `docker compose up -d`. Окружение процесса сильнее файла — то
# есть флаг текущего запуска всегда побеждает запомненное.
$EnvFile = Join-Path $RepoRoot ".env"

function Get-EnvFileValue([string]$Key) {
    if (-not (Test-Path $EnvFile)) { return "" }
    $line = Get-Content $EnvFile -Encoding utf8 | Where-Object { $_.StartsWith("$Key=") } | Select-Object -Last 1
    if ($line) { return $line.Substring($Key.Length + 1) }
    return ""
}

# Переписываем файл целиком: в домене и почте попадаются символы, которые при
# правке строки на месте пришлось бы экранировать.
function Set-EnvFileValue([string]$Key, [string]$Value) {
    $lines = @()
    if (Test-Path $EnvFile) {
        $lines = @(Get-Content $EnvFile -Encoding utf8 | Where-Object { -not $_.StartsWith("$Key=") })
    }
    $lines += "$Key=$Value"
    Set-Content -Path $EnvFile -Value $lines -Encoding utf8
}

foreach ($var in @('DOMAIN', 'LETSENCRYPT_EMAIL', 'HTTP_PORT', 'HTTPS_PORT')) {
    $current = [Environment]::GetEnvironmentVariable($var)
    if ($current) {
        Set-EnvFileValue $var $current
    } else {
        $remembered = Get-EnvFileValue $var
        if ($remembered) { [Environment]::SetEnvironmentVariable($var, $remembered) }
    }
}

# Из какого коммита собираем: сервер отдаёт это в GET /api/config. Внутри
# образа гита нет и не будет (см. Dockerfile), поэтому считаем здесь и
# передаём аргументами сборки через docker-compose.yml.
#
# Смотрим только на Server/ (мы в ней и стоим): правки в клиенте не делают
# сборку сервера «изменённой». Untracked-файлы не считаем — заметка рядом с
# исходниками в бинарь не попадает, а CMakeLists, куда её пришлось бы вписать,
# отслеживается, и такое изменение мы увидим.
$env:GIT_COMMIT = (git rev-parse --short HEAD)
if (-not $env:GIT_COMMIT) { $env:GIT_COMMIT = "unknown" }
$dirty = (git status --porcelain -uno -- .)
if ($dirty) { $env:GIT_MODIFIED = "1" } else { $env:GIT_MODIFIED = "0" }

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
Write-Host "Готово. HEAD = $newRev (сборка $env:GIT_COMMIT)" -ForegroundColor Green
if ($env:DOMAIN) {
    Write-Host "Веб-клиент: https://$env:DOMAIN/"
    Write-Host "Сертификат Let's Encrypt (домен $env:DOMAIN); автопродление в контейнере proxy."
    if ($Domain) { Write-Host "Домен запомнен в .env — дальше можно запускать без флагов." }
} else {
    $portSuffix = ""
    if ($HttpsPort -gt 0 -and $HttpsPort -ne 443) { $portSuffix = ":$HttpsPort" }
    Write-Host "Веб-клиент: https://<IP-сервера>$portSuffix/"
    Write-Host "Сертификат самоподписанный — браузер предупредит; это ожидаемо."
}
