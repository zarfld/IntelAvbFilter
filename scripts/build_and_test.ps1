<#!
.SYNOPSIS
  Build driver + user tool and run basic tests (caps, ptp) as Administrator.
.DESCRIPTION
  Loads Visual Studio C++ environment, builds solution + nmake user-mode test tool,
  optionally restarts matching Intel NIC filter (requires devcon.exe if RestartAdapter switch used),
  then executes a small test sequence.
.PARAMETER Configuration
  Build configuration (Debug / Release). Default Debug.
.PARAMETER Platform
  Target platform (x64 / Win32 / ARM64). Default x64.
.PARAMETER Solution
  Path to solution file. Default: IntelAvbFilter.sln in script root.
.PARAMETER RestartAdapter
  RETIRED 2026-10-29: refused.  It ran "devcon restart *VEN_8086*" - every Intel device, including the NIC that may
  carry the runner connection, outside the machine lock and without recovery.  Adapter administration is the Unsafe
  class: attended runs only, through Run-Tests-Elevated.ps1 with AVB_HIL_AUTHORIZE_UNSAFE naming the exact entry.
.NOTES
  Gated since 2026-10-29 (TEST-HILPATH-012): builds through tools\build\Build-Driver.ps1 / Build-Tests.ps1 (machine
  build exclusion + test build manifest), tests through tools\test\Run-Tests-CI.ps1 (provenance, HIL safety gate,
  #328 hazard hold, adapter state check).  The four avb_test_um.c commands run as the entries
  'avb_test_project <command>'; they are not classified per command yet, so the dispatcher BLOCKs them (visible,
  non-passing) until each command is classified from source evidence.  x64 only (the test runner directory).
.PARAMETER SkipDriver
  Skip building the driver (only user tool).
.PARAMETER SkipUserTool
  Skip building the user-mode test tool.
.PARAMETER SelfTestOnly
  Only run selftest (skip other test commands).
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/build_and_test.ps1 -Configuration Debug -Platform x64
#>
[CmdletBinding()] param(
  [string]$Configuration = 'Debug',
  [string]$Platform = 'x64',
  [string]$Solution = 'IntelAvbFilter.sln',
  [switch]$RestartAdapter,
  [switch]$SkipDriver,
  [switch]$SkipUserTool,
  [switch]$SelfTestOnly
)

$ErrorActionPreference = 'Stop'
$script:TestExit = 0

function Write-Info($m){ Write-Host "[INFO] $m" -ForegroundColor Cyan }
function Write-Warn($m){ Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Write-Err($m){ Write-Host "[ERR ] $m" -ForegroundColor Red }

function Require-Admin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $pr = New-Object Security.Principal.WindowsPrincipal($id)
  if(-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    Write-Err 'Must run elevated (Administrator).'; exit 1
  }
}

function Load-VCTools {
  if(Get-Command cl -ErrorAction SilentlyContinue){ Write-Info 'MSVC environment already loaded.'; return }
  $vswhere = Join-Path ${Env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  if(!(Test-Path $vswhere)){ throw "vswhere.exe not found: $vswhere" }
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if(!$vs){ throw 'Visual Studio with C++ tools not found.' }
  $bat = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
  if($Platform -eq 'Win32'){ $bat = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat' }
  if(!(Test-Path $bat)){ throw "vcvars script not found: $bat" }
  Write-Info "Loading MSVC env: $bat"
  # Capture environment variables produced by vcvars* and import into current PowerShell session
  cmd /c "call `"$bat`" >nul && set" | ForEach-Object {
    if($_ -match '='){ $k,$v = $_.Split('=',2); try { Set-Item -Path Env:$k -Value $v -ErrorAction SilentlyContinue } catch {} }
  }
  if(-not (Get-Command cl -ErrorAction SilentlyContinue)){ throw 'cl.exe not available after vcvars' }
  Write-Info 'MSVC environment ready.'
}

function Build-Driver {
  param([string]$SolutionPath)
  if($SkipDriver){ Write-Info 'SkipDriver set - skipping driver build.'; return }
  Write-Info "Building driver through tools\build\Build-Driver.ps1 ($Configuration|$Platform; machine build exclusion)"
  & .\tools\build\Build-Driver.ps1 -Configuration $Configuration -Platform $Platform
  if($LASTEXITCODE -ne 0){ throw "Build-Driver.ps1 failed (exit $LASTEXITCODE)" }
}

function Build-UserTool {
  if($SkipUserTool){ Write-Info 'SkipUserTool set - skipping user tool build.'; return }
  Write-Info "Building avb_test_project (tests\integration\avb\avb_test.mak) through tools\build\Build-Tests.ps1 ($Configuration; manifest + build exclusion)"
  & .\tools\build\Build-Tests.ps1 -Configuration $Configuration -TestName avb_test_project
  if($LASTEXITCODE -ne 0){ throw "Build-Tests.ps1 failed (exit $LASTEXITCODE)" }
}

function Restart-IntelAdapter {
  if(-not $RestartAdapter){ return }
  throw ('-RestartAdapter is retired (2026-10-29): it restarted every Intel device (devcon restart *VEN_8086*) outside the ' +
         'machine lock and without recovery. Adapter administration runs only as an attended Unsafe entry through ' +
         'tools\test\Run-Tests-Elevated.ps1 with AVB_HIL_AUTHORIZE_UNSAFE naming the exact entry.')
}

function Run-Tests {
  if($Platform -ne 'x64'){ throw "Run-Tests-CI.ps1 runs x64 test builds only (requested $Platform)" }
  $cmds = if($SelfTestOnly){ @('selftest') } else { @('caps', 'ptp-probe', 'snapshot', 'selftest') }
  $entries = @($cmds | ForEach-Object { "avb_test_project $_" })
  Write-Info "Running $($entries -join ', ') through tools\test\Run-Tests-CI.ps1 (HardwareIntegration; gated)"
  & .\tools\test\Run-Tests-CI.ps1 -Suite HardwareIntegration -Configuration $Configuration -Tests $entries
  $script:TestExit = $LASTEXITCODE
  if($script:TestExit -ne 0){ Write-Err "Run-Tests-CI.ps1 exit $($script:TestExit) (blocked or failed entries are non-passing)" }
}

function Show-Summary {
  Write-Host "`n================ SUMMARY ================" -ForegroundColor Green
  Write-Host "Configuration : $Configuration"
  Write-Host "Platform      : $Platform"
  Write-Host "Driver Built  : $([bool](-not $SkipDriver))"
  Write-Host "User Tool Built: $([bool](-not $SkipUserTool))"
  Write-Host '=========================================='
}

# Main Flow
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location (Split-Path -Parent $scriptRoot)  # project root assumed one level up (scripts/)
try {
  Require-Admin
  Restart-IntelAdapter   # retired switch: refuses before anything is built
  Load-VCTools
  Build-Driver -SolutionPath $Solution
  Build-UserTool
  Run-Tests
  Show-Summary
  if($script:TestExit -ne 0){ exit $script:TestExit }
  Write-Info 'All done.'
} catch {
  Write-Err $_
  exit 1
} finally {
  Pop-Location
}
