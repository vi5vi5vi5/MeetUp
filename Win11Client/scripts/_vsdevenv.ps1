<#
  Shared helper: make sure the MSVC (cl.exe) toolchain is on PATH.
  The Ninja generator does not run vcvars itself, so when these scripts are
  launched from a plain PowerShell we enter the Visual Studio dev environment.
  Idempotent — a no-op if cl.exe is already available (e.g. Dev PowerShell).
#>
function Enter-MsvcEnv {
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
