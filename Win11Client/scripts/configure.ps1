<#
.SYNOPSIS
  Configure the MeetUp Win11 client with CMake (MSVC / Ninja preset).
.EXAMPLE
  scripts\configure.ps1 -Config Debug
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$QtDir = $env:QTDIR
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$preset = "$Config-x64"

. (Join-Path $PSScriptRoot "_vsdevenv.ps1")
Enter-MsvcEnv

# Пресет подставляет $env:QTDIR в CMAKE_PREFIX_PATH. Пустой QTDIR для CMake не
# ошибка — он просто не найдёт Qt6, причём только на ЧИСТОМ каталоге сборки:
# в уже сконфигурированном путь лежит в кэше, и поломка всплывает позже и
# совсем в другом месте. Поэтому подставляем то же значение по умолчанию, что и
# deploy.ps1 для windeployqt, — и обе половины гарантированно берут ОДИН кит.
if (-not $QtDir) { $QtDir = "P:/Qt/6.11.1/msvc2022_64" }
if (-not (Test-Path (Join-Path $QtDir "lib/cmake/Qt6"))) {
    throw "Qt не найден: $QtDir. Передайте -QtDir <путь к киту> или задайте переменную QTDIR."
}
$env:QTDIR = $QtDir

# Третьи стороны, которых нет в репозитории (RNNoise с его весами). Зовём
# ЗДЕСЬ, а не в build.ps1: через configure проходят все дороги — и сборка, и
# deploy, — а cmake ниже читает список файлов RNNoise и без них не
# сконфигурируется вовсе. Скрипт быстрый и молчаливый, когда всё на месте.
& (Join-Path $PSScriptRoot "fetch-deps.ps1")

# ---------------------------------------------------------------------------
# Проверка, которой здесь не было и которая стоила нам вечера отладки.
#
# Ninja узнаёт, от каких заголовков зависит объектник, разбирая вывод
# cl /showIncludes: строки, начинающиеся с префикса «Note: including file:»
# (на локализованной MSVC — «Примечание: включение файла:»), он превращает в
# список зависимостей, остальное считает обычным выводом. Сам префикс CMake
# запоминает при конфигурировании — в msvc_deps_prefix (CMakeFiles/rules.ninja).
#
# Если запомненный префикс записан не в той кодировке, в какой компилятор его
# печатает, совпадения нет — и Ninja не видит НИ ОДНОЙ зависимости от
# заголовков. Ошибки при этом нет: сборка идёт, но правка .h не пересобирает
# .cpp, которые его включают. Половина объектников остаётся собранной по старым
# определениям классов — разъезжается sizeof, конструктор пишет за границу
# чужого объекта, и приложение падает в стороне от причины. Именно так
# «переставшая запускаться» сборка падала внутри setContextProperty.
#
# Проверяем честно: спрашиваем у компилятора байты его первой строки и сверяем
# с байтами, которые CMake положил в rules.ninja.
function Test-HeaderDeps([string]$buildDir) {
    $rules = Join-Path $buildDir "CMakeFiles/rules.ninja"
    if (-not (Test-Path $rules)) { return }

    # Сравниваем БАЙТЫ, а не строки. Ninja сопоставляет префикс с выводом
    # компилятора побайтово, и вся поломка была ровно в кодировке — значит и
    # проверять надо так же. Любая перекодировка по дороге (PS 5.1 читает
    # файлы как ANSI, cmd пишет лог в UTF-8) превратила бы проверку в лотерею:
    # на глаз обе строки выглядят одинаково при разных байтах.
    # latin1 отображает байт в символ один в один — сравнение строк в нём
    # и есть сравнение байтов.
    $raw = [System.Text.Encoding]::GetEncoding(28591)

    $rulesText = $raw.GetString([System.IO.File]::ReadAllBytes($rules))
    $m = [regex]::Match($rulesText, '(?m)^msvc_deps_prefix\s*=\s*(.+?)\r?$')
    if (-not $m.Success) { return }        # не MSVC/Ninja — проверять нечего
    $stored = $m.Groups[1].Value.TrimEnd()
    if (-not $stored) { return }

    # Проба: что компилятор печатает на самом деле.
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("depscheck_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force $tmp | Out-Null
    try {
        Set-Content (Join-Path $tmp "probe.cpp") -Encoding ascii `
            -Value "#include <stdio.h>`nint main(){return 0;}"
        # Перенаправление делает cmd, а не PowerShell: в PS 5.1 «2>&1» на родной
        # программе превращает её stderr в ошибки, а при ErrorActionPreference =
        # Stop любая строка оттуда обрывает скрипт. И никакого Push-Location:
        # упади здесь что-нибудь — finally попытался бы удалить каталог, в
        # котором мы стоим.
        $log = Join-Path $tmp "probe.log"
        cmd /c "cd /d `"$tmp`" && cl /nologo /showIncludes /c probe.cpp > `"$log`" 2>&1" | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $log)) {
            # Сломалась сама проба, а не сборка: молчим, но не притворяемся,
            # что проверили.
            Write-Host "==> Проверку зависимостей выполнить не удалось (пробный cl не собрался)" -ForegroundColor DarkYellow
            return
        }

        $logText = $raw.GetString([System.IO.File]::ReadAllBytes($log))
        $first = ($logText -split "`r?`n" | Where-Object { $_ -match ':' } | Select-Object -First 1)
        if (-not $first) { return }
        $actual = $first.TrimStart()

        if (-not $actual.StartsWith($stored)) {
            # Для человека печатаем в UTF-8: в latin1 это была бы каша.
            $utf8 = [System.Text.Encoding]::UTF8
            $show = { param($s) $utf8.GetString($raw.GetBytes($s)) }
            Write-Host ""
            Write-Host "!!! Ninja НЕ БУДЕТ отслеживать зависимости от заголовков." -ForegroundColor Red
            Write-Host "    запомнено CMake : '$(& $show $stored)'" -ForegroundColor Red
            Write-Host "    печатает cl.exe : '$(& $show $actual)'" -ForegroundColor Red
            Write-Host "    Правка .h не пересоберёт .cpp — сборка соберётся из разных" -ForegroundColor Red
            Write-Host "    версий классов и упадёт в непредсказуемом месте." -ForegroundColor Red
            # Путь берём из аргумента, а не из $Config снаружи: аварийная ветка
            # не должна зависеть от переменных вызывающего — иначе вместо
            # внятной жалобы получишь «You cannot call a method on a null».
            Write-Host "    Лечение: удалить $buildDir и сконфигурировать заново" -ForegroundColor Yellow
            Write-Host "    из этой же консоли (кодировки должны совпасть)." -ForegroundColor Yellow
            throw "msvc_deps_prefix не совпадает с выводом компилятора"
        }
        Write-Host "==> Зависимости от заголовков отслеживаются" -ForegroundColor DarkGray
    }
    finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
}

Push-Location $root
try {
    Write-Host "==> Configuring preset '$preset' (Qt: $env:QTDIR)" -ForegroundColor Cyan
    Invoke-Native { cmake --preset $preset }
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
    Test-HeaderDeps (Join-Path $root "out/build/$($Config.ToLower())")
    Write-Host "==> Configured -> out/build/$($Config.ToLower())" -ForegroundColor Green
}
finally { Pop-Location }
