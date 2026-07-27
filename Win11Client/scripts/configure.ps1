<#
.SYNOPSIS
  Configure the MeetUp Win11 client with CMake (MSVC / Ninja preset).
.EXAMPLE
  scripts\configure.ps1 -Config Debug
#>
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$preset = "$Config-x64"

. (Join-Path $PSScriptRoot "_vsdevenv.ps1")
Enter-MsvcEnv

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

    $line = (Select-String -Path $rules -Pattern '^msvc_deps_prefix\s*=\s*(.+)$' |
             Select-Object -First 1)
    if (-not $line) { return }   # не MSVC/Ninja — проверять нечего
    $stored = $line.Matches[0].Groups[1].Value.TrimEnd()

    # Проба: что компилятор печатает на самом деле.
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("depscheck_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force $tmp | Out-Null
    try {
        Set-Content (Join-Path $tmp "probe.cpp") -Encoding ascii `
            -Value "#include <stdio.h>`nint main(){return 0;}"
        Push-Location $tmp
        cl /nologo /showIncludes /c probe.cpp > probe.log 2>&1
        Pop-Location
        $first = (Get-Content (Join-Path $tmp "probe.log") | Where-Object { $_ -match ':' } |
                  Select-Object -First 2 | Select-Object -Last 1)
        if (-not $first) { return }
        $actual = ($first -replace '^\s+', '')

        if (-not $actual.StartsWith($stored)) {
            Write-Host ""
            Write-Host "!!! Ninja НЕ БУДЕТ отслеживать зависимости от заголовков." -ForegroundColor Red
            Write-Host "    запомнено CMake : '$stored'" -ForegroundColor Red
            Write-Host "    печатает cl.exe : '$actual'" -ForegroundColor Red
            Write-Host "    Правка .h не пересоберёт .cpp — сборка будет собрана из разных" -ForegroundColor Red
            Write-Host "    версий классов и упадёт в непредсказуемом месте." -ForegroundColor Red
            Write-Host "    Лечение: удалить out/build/$($Config.ToLower()) и сконфигурировать заново" -ForegroundColor Yellow
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
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
    Test-HeaderDeps (Join-Path $root "out/build/$($Config.ToLower())")
    Write-Host "==> Configured -> out/build/$($Config.ToLower())" -ForegroundColor Green
}
finally { Pop-Location }
