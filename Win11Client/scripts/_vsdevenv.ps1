<#
  Shared helper: make sure the MSVC (cl.exe) toolchain is on PATH.
  The Ninja generator does not run vcvars itself, so when these scripts are
  launched from a plain PowerShell we enter the Visual Studio dev environment.
  Idempotent — a no-op if cl.exe is already available (e.g. Dev PowerShell).
#>
# Вызов ВНЕШНЕЙ программы (cmake, ninja, windeployqt) без риска, что скрипт
# оборвётся на её предупреждении.
#
# Windows PowerShell 5.1 заворачивает stderr родной программы в объекты-ошибки,
# как только вывод скрипта куда-то перенаправляют — в файл, в конвейер, в лог
# сборки. При $ErrorActionPreference = "Stop" ПЕРВАЯ же такая строка обрывает
# скрипт. А cmake пишет в stderr обычные предупреждения (например, про приватные
# модули Qt), и сборка падала «на пустом месте» — но только у того, кто
# догадался сохранить лог. Провал мы и так ловим по $LASTEXITCODE, поэтому на
# время вызова снимаем Stop, а потом возвращаем как было.
function Invoke-Native {
    param([Parameter(Mandatory)][scriptblock]$Command)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & $Command } finally { $ErrorActionPreference = $prev }
}

function Enter-MsvcEnv {
    # Просим у компилятора английские диагностики. Это не косметика: Ninja
    # определяет зависимости от заголовков, вылавливая из вывода cl /showIncludes
    # строки с префиксом «Note: including file:», а сам префикс CMake запоминает
    # при конфигурировании (msvc_deps_prefix в CMakeFiles/rules.ninja).
    # Локализованный префикс приносит вопрос кодировок — а он уже один раз стоил
    # нам вечера (см. проверку в configure.ps1). ASCII такого вопроса не имеет.
    # Работает только если установлен английский языковой пакет MSVC; если нет —
    # префикс останется русским, и правильность проверит configure.ps1.
    $env:VSLANG = "1033"
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Open a 'Developer PowerShell for VS 2022' and re-run."
    }

    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) { throw "No Visual Studio with the C++ toolset found." }

    $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null

    Write-Host "==> MSVC environment initialised ($vsPath)" -ForegroundColor DarkGray
}
