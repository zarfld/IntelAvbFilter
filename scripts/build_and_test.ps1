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
  If set, attempts to restart Intel adapter via devcon (must be in PATH).
.PARAMETER SkipDriver
  Skip building the driver (only user tool).
.PARAMETER SkipUserTool
  Skip building the user-mode test tool.
.PARAMETER SelfTestOnly
  Only run selftest (skip other test commands).
.PARAMETER verbose
  Extra logging.
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
  [switch]$SelfTestOnly,
  [switch]$Verbose
)

$ErrorActionPreference = 'Stop'

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
  if(!(Test-Path $SolutionPath)){ throw "Solution not found: $SolutionPath" }
  Write-Info "Building driver: $SolutionPath ($Configuration|$Platform)"
  & msbuild $SolutionPath /m /p:Configuration=$Configuration /p:Platform=$Platform /warnaserror:false /nologo | Write-Host
}

function Build-UserTool {
  if($SkipUserTool){ Write-Info 'SkipUserTool set - skipping user tool build.'; return }
  $mk = 'tools\avb_test\avb_test.mak'
  if(!(Test-Path $mk)){ throw "Makefile not found: $mk" }
  Write-Info "Building user tool via nmake ($Configuration|$Platform)"
  & nmake /nologo -f $mk CFG=$Configuration PLATFORM=$Platform | Write-Host
}

function Restart-IntelAdapter {
  if(-not $RestartAdapter){ return }
  if(-not (Get-Command devcon -ErrorAction SilentlyContinue)) { Write-Warn 'devcon.exe not found in PATH; skipping adapter restart.'; return }
  Write-Info 'Attempting adapter restart (devcon restart *VEN_8086*)'
  try { & devcon restart *VEN_8086* | Write-Host } catch { Write-Warn "devcon restart failed: $_" }
}

function Run-Tests {
  $exe = Join-Path -Path (Join-Path build/tools/avb_test $Platform) -ChildPath "$Configuration/avb_test_i210.exe"
  if(!(Test-Path $exe)){ throw "Test executable missing: $exe" }
  Write-Info "Running tests using $exe"
  if($SelfTestOnly){
    & $exe selftest | Write-Host
    return
  }
  & $exe caps      | Write-Host
  & $exe ptp-probe | Write-Host
  & $exe snapshot  | Write-Host
  & $exe selftest  | Write-Host
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
  Load-VCTools
  Build-Driver -SolutionPath $Solution
  Build-UserTool
  Restart-IntelAdapter
  Run-Tests
  Show-Summary
  Write-Info 'All done.'
} catch {
  Write-Err $_
  exit 1
} finally {
  Pop-Location
}
