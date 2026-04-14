# Start-DbgViewCapture.ps1
# Launches DebugView elevated, begins file logging immediately, and returns the process object.
#
# Usage:
#   IMPORTANT: always assign the return value to suppress process object output in the console.
#
#   # Auto-named log (logs\dbgview_<timestamp>.log):
#   $proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1
#
#   # Named log (logs\avb_i210_test_<timestamp>.log):
#   $proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -LogName "avb_i210_test"
#
#   # Explicit full log path:
#   $proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -LogFile "C:\Logs\my_test.log"
#
#   # Stop after tests (or use Stop-DbgViewCapture.ps1):
#   .\.github\skills\DbgView\Stop-DbgViewCapture.ps1 -ProcessId $proc.Id
#
# Or use the -WaitForProcess switch to block until DebugView exits
# (no need to capture return value in that mode):
#   .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -WaitForProcess
#
# Skill: dbgview-capture  (.github/skills/DbgView/SKILL.md)

param(
    [string] $DbgViewExe  = "$PSScriptRoot\DebugView\Dbgview.exe",

    # Simple log name stem (e.g. "avb_i210_test") — placed in logs\ with a timestamp suffix.
    # Takes precedence over -LogFile when both are provided.
    [string] $LogName     = "",

    # Explicit full destination path for the log file.
    # Auto-generated from -LogName (or default stem) when omitted.
    [string] $LogFile     = "",

    # Append to an existing log file instead of overwriting it.
    [switch] $Append,

    # Set DEFAULT Debug Print Filter mask (0xFFFFFFFF).
    # NOTE: takes effect only after a reboot.  Only needed once per machine.
    [switch] $SetDefaultMask,

    # Set IHVNETWORK Debug Print Filter mask (0xFFFFFFFF) for NDIS vendor drivers.
    # NOTE: takes effect only after a reboot.  Only needed once per machine.
    [switch] $SetIhvNetworkMask,

    # Block until the DebugView process exits (useful in sequential test wrappers).
    [switch] $WaitForProcess
)

$ErrorActionPreference = 'Stop'

# ── Locate binary ──────────────────────────────────────────────────────────────
if (-not (Test-Path $DbgViewExe)) {
    Write-Warning ("[DbgView] Binary not found at '$DbgViewExe'.`n" +
                   "  Run the install script to download it automatically:`n" +
                   "    .\tools\setup\Install-DbgView.ps1`n" +
                   "  Tests will continue WITHOUT kernel debug capture.")
    # Return $null so callers can check: if ($proc) { ... }
    return $null
}

# ── Resolve log file path ─────────────────────────────────────────────────────
$logsRoot = Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) "logs"
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

if ($LogName) {
    # Caller supplied a stem name → place in standard logs\ dir with timestamp
    $LogFile = Join-Path $logsRoot "${LogName}_$timestamp.log"
} elseif (-not $LogFile) {
    # Nothing supplied → auto-generate default name
    $LogFile = Join-Path $logsRoot "dbgview_$timestamp.log"
}
# else: caller supplied -LogFile full path → use as-is
$logDir = Split-Path -Parent $LogFile
if ($logDir -and -not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
}

# ── Optional: Debug Print Filter registry (one-time machine setup) ─────────────
$filterKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter"

if ($SetDefaultMask) {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
             [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Warning "Cannot set Debug Print Filter: not running as Administrator."
    } else {
        reg add ($filterKey -replace ':', '') /v DEFAULT /t REG_DWORD /d 0xFFFFFFFF /f | Out-Null
        Write-Host "[DbgView] DEFAULT filter mask set. Reboot required for it to take effect." -ForegroundColor Yellow
    }
}

if ($SetIhvNetworkMask) {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
             [Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Warning "Cannot set Debug Print Filter: not running as Administrator."
    } else {
        reg add ($filterKey -replace ':', '') /v IHVNETWORK /t REG_DWORD /d 0xFFFFFFFF /f | Out-Null
        Write-Host "[DbgView] IHVNETWORK filter mask set. Reboot required for it to take effect." -ForegroundColor Yellow
    }
}

# ── Build argument list ────────────────────────────────────────────────────────
$argList = @(
    "/f",           # skip filter-save confirmation on exit
    "/t",           # start minimised to tray
    "/l", $LogFile  # begin logging immediately
)
if ($Append) { $argList += "/a" }

# ── Launch elevated ────────────────────────────────────────────────────────────
$proc = Start-Process -FilePath $DbgViewExe `
                      -ArgumentList $argList `
                      -Verb RunAs `
                      -PassThru

Write-Host "[DbgView] Started PID=$($proc.Id)"
Write-Host "[DbgView] Logging to: $LogFile"

if ($WaitForProcess) {
    $proc.WaitForExit()
    Write-Host "[DbgView] Process exited."
    return
}

return $proc
