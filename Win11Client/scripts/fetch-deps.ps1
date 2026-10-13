<#
.SYNOPSIS
  Download the third-party sources that are NOT kept in this repository
  (RNNoise + its model weights, the Ferry core) into third_party/.
.EXAMPLE
  scripts\fetch-deps.ps1           # доложить недостающее и выйти
  scripts\fetch-deps.ps1 -Force    # скачать заново, даже если всё на месте
#>
param([switch]$Force)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot "_vsdevenv.ps1")   # ради Invoke-Native

# ---------------------------------------------------------------------------
# Почему RNNoise не лежит в репозитории.
#
# Веса модели — это ~75 МБ сгенерированного C (src/rnnoise_data.c). Исходники
# самого клиента весят пару мегабайт, то есть чужая таблица чисел была бы в
# сорок раз тяжелее всего проекта и навсегда осталась бы в истории git. Поэтому
# third_party/ целиком в .gitignore, а собирается он этим скриптом — его зовёт
# configure.ps1, так что отдельно про него помнить не нужно.
#
# Коммит прибит гвоздями, а не взят из главной ветки: сборка обязана давать
# один и тот же результат сегодня и через год. Обновление зависимости — это
# осознанная правка одной строки ниже.
$RnnoiseCommit = "70f1d256acd4b34a572f999a05c87bf00b67730d"

# Ferry — передача файлов в чате (github.com/vi5vi5vi5/Ferry). Из его
# репозитория берём ровно то, что собирает клиент: ядро core/ (чанки, манифест,
# ссылка, BLAKE3, JSON — тот же код, что у CLI и релея Ferry), выравнивание
# пароля и эталонный Argon2 из Cli/ и самотесты ядра. Сабмодуль git дал бы то
# же самое, но второй механизм зависимостей рядом с этим скриптом лишний.
#
# Номер коммита ложится рядом с кодом (third_party/ferry/COMMIT), и правка
# строки ниже сама приводит к перекачке — без -Force. Ядро Ferry будет
# обновляться заметно чаще RNNoise, и забытый -Force означал бы сборку со
# старым ядром при новом номере в скрипте.
$FerryRepo = "vi5vi5vi5/Ferry"
$FerryCommit = "13ac368e73f45acd47d530ae73440637453c7c2f"
$FerryParts = @(
    "core",
    "Cli/Password.h",
    "Cli/Password.cpp",
    "Cli/third_party/argon2",
    "tests/CoreTests.cpp"
)

$thirdParty = Join-Path $root "third_party"
$rnnoiseDir = Join-Path $thirdParty "rnnoise"
# По этим двум файлам судим, что зависимость на месте: первый — сам код,
# второй — веса, которые приезжают ОТДЕЛЬНО и которых в репозитории апстрима
# нет вовсе. Проверять только каталог нельзя: прерванная закачка оставила бы
# исходники без весов, и сборка падала бы на линковке вместо внятной жалобы.
$srcMarker = Join-Path $rnnoiseDir "src/denoise.c"
$weightsMarker = Join-Path $rnnoiseDir "src/rnnoise_data.c"

function Test-Ready {
    (Test-Path $srcMarker) -and (Test-Path $weightsMarker) -and
    ((Get-Item $weightsMarker).Length -gt 1MB)   # обрезанный файл — не файл
}

$ferryDir = Join-Path $thirdParty "ferry"
$ferryStamp = Join-Path $ferryDir "COMMIT"

# Метка пишется последней, после всех файлов: прерванная закачка оставит
# каталог без метки, и следующий запуск честно начнёт заново.
function Test-FerryReady {
    (Test-Path $ferryStamp) -and
    ((Get-Content $ferryStamp -Raw).Trim() -eq $FerryCommit) -and
    (Test-Path (Join-Path $ferryDir "core/Crypto.h"))
}

# curl и tar — штатные в Windows 10 1803+, ставить нечего.
# curl: -f роняет на HTTP-ошибке (иначе в файл ляжет HTML страницы 404),
#       -sS молчит про прогресс, но не про ошибки, -L идёт за редиректом.
function Get-Remote([string]$url, [string]$dest) {
    Write-Host "    $url" -ForegroundColor DarkGray
    Invoke-Native { curl.exe -fsSL --retry 3 -o $dest $url }
    if ($LASTEXITCODE -ne 0) { throw "не удалось скачать $url (curl $LASTEXITCODE)" }
}

function Expand-Tar([string]$archive, [string]$dest) {
    Invoke-Native { tar.exe -xzf $archive -C $dest }
    if ($LASTEXITCODE -ne 0) { throw "не удалось распаковать $archive (tar $LASTEXITCODE)" }
}

function New-TempDir([string]$name) {
    New-Item -ItemType Directory -Force $thirdParty | Out-Null
    $dir = Join-Path $thirdParty $name
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $dir
}

function Sync-Rnnoise {
    if ((Test-Ready) -and -not $Force) {
        Write-Host "==> RNNoise на месте (third_party/rnnoise)" -ForegroundColor DarkGray
        return
    }

    $tmp = New-TempDir "_download"
    try {
        # ---- 1. Исходники ----
        if ($Force -or -not (Test-Path $srcMarker)) {
            Write-Host "==> Качаем RNNoise ($($RnnoiseCommit.Substring(0,8)))" -ForegroundColor Cyan
            $srcArchive = Join-Path $tmp "rnnoise-src.tar.gz"
            Get-Remote "https://codeload.github.com/xiph/rnnoise/tar.gz/$RnnoiseCommit" $srcArchive
            Expand-Tar $srcArchive $tmp

            # GitHub кладёт всё в каталог rnnoise-<полный sha>; нам нужно ровно
            # third_party/rnnoise, потому что этот путь зашит в CMakeLists.txt.
            $unpacked = Join-Path $tmp "rnnoise-$RnnoiseCommit"
            if (-not (Test-Path $unpacked)) { throw "в архиве нет каталога rnnoise-$RnnoiseCommit" }
            if (Test-Path $rnnoiseDir) { Remove-Item -Recurse -Force $rnnoiseDir }
            Move-Item $unpacked $rnnoiseDir
        }

        # ---- 2. Веса ----
        # Хэш лежит в самом дереве (model_version) и служит одновременно именем
        # файла и контрольной суммой — так это устроено у апстрима, и так же
        # проверяет их download_model.sh, который на Windows не запустить.
        if ($Force -or -not (Test-Path $weightsMarker)) {
            $modelVersion = (Get-Content (Join-Path $rnnoiseDir "model_version") -Raw).Trim()
            if (-not $modelVersion) { throw "model_version пуст — дерево RNNoise битое" }

            Write-Host "==> Качаем веса модели ($($modelVersion.Substring(0,8)), ~56 МБ)" -ForegroundColor Cyan
            $weightsArchive = Join-Path $tmp "rnnoise_data-$modelVersion.tar.gz"
            Get-Remote "https://media.xiph.org/rnnoise/models/rnnoise_data-$modelVersion.tar.gz" $weightsArchive

            $actual = (Get-FileHash $weightsArchive -Algorithm SHA256).Hash.ToLower()
            if ($actual -ne $modelVersion.ToLower()) {
                throw "контрольная сумма весов не сошлась`n  ожидали: $modelVersion`n  получили: $actual"
            }
            Write-Host "    контрольная сумма сошлась" -ForegroundColor DarkGray
            Expand-Tar $weightsArchive $rnnoiseDir

            if (-not (Test-Path $weightsMarker)) { throw "в архиве весов нет src/rnnoise_data.c" }

            # Вторая, уменьшенная модель из того же архива: CMakeLists её не
            # собирает, а на диске это лишние 30 МБ.
            Remove-Item -Force -ErrorAction SilentlyContinue `
                (Join-Path $rnnoiseDir "src/rnnoise_data_little.c"), `
                (Join-Path $rnnoiseDir "src/rnnoise_data_little.h")
        }
    }
    finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }

    if (-not (Test-Ready)) { throw "third_party/rnnoise собран не полностью" }

    $mb = [math]::Round((Get-ChildItem $rnnoiseDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
    Write-Host "==> Готово: third_party/rnnoise ($mb МБ)" -ForegroundColor Green
}

function Sync-Ferry {
    if ((Test-FerryReady) -and -not $Force) {
        Write-Host "==> Ferry на месте (third_party/ferry @ $($FerryCommit.Substring(0,8)))" -ForegroundColor DarkGray
        return
    }

    $tmp = New-TempDir "_download_ferry"
    try {
        Write-Host "==> Качаем ядро Ferry ($($FerryCommit.Substring(0,8)))" -ForegroundColor Cyan
        $archive = Join-Path $tmp "ferry.tar.gz"
        Get-Remote "https://codeload.github.com/$FerryRepo/tar.gz/$FerryCommit" $archive
        Expand-Tar $archive $tmp

        # Каталог в архиве GitHub — <имя репозитория>-<полный sha>.
        $unpacked = Join-Path $tmp "$(Split-Path -Leaf $FerryRepo)-$FerryCommit"
        if (-not (Test-Path $unpacked)) { throw "в архиве нет каталога $(Split-Path -Leaf $unpacked)" }

        if (Test-Path $ferryDir) { Remove-Item -Recurse -Force $ferryDir }
        foreach ($part in $FerryParts) {
            $src = Join-Path $unpacked $part
            if (-not (Test-Path $src)) { throw "в Ferry @ $FerryCommit нет $part" }
            # Раскладка повторяет репозиторий Ferry: его исходники включают
            # друг друга от корня («core/Crypto.h», «Cli/Password.h»).
            $dst = Join-Path $ferryDir $part
            New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
            Move-Item $src $dst
        }
        Set-Content $ferryStamp $FerryCommit -Encoding ascii
    }
    finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }

    if (-not (Test-FerryReady)) { throw "third_party/ferry собран не полностью" }
    Write-Host "==> Готово: third_party/ferry" -ForegroundColor Green
}

Sync-Rnnoise
Sync-Ferry
