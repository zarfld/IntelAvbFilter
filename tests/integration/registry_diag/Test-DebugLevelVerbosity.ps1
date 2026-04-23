<#
.SYNOPSIS
    TC-DEBUG-REG-VERB-001..003: Verify that DebugLevel registry setting controls
    DbgPrint verbosity in the loaded driver.

.DESCRIPTION
    Verifies REQ-NF-DEBUG-REG-001 (#95) acceptance criterion:
      "Debug level controls log verbosity correctly (ERROR, WARNING, INFO, TRACE)"

    Implements TEST-DEBUG-REG-001 (#247) gap: behavioral verbosity verification
    using CaptureDbgView / DebugView log parsing.

    Strategy (no driver reload required):
      The driver emits "IntelAvbFilter: DebugLevel loaded from registry: N" at
      FilterReadDebugSettings() call time.  We parse the CURRENT DbgView log for
      that sentinel line to confirm which DebugLevel was actually applied.

      For level-change proof (TC-VERB-002 / TC-VERB-003), the test:
        1. Writes a new DebugLevel to the registry.
        2. Reloads the driver via netcfg (uninstall + reinstall) — elevated only.
        3. Captures the new DbgView log.
        4. Asserts the sentinel "DebugLevel loaded from registry: <N>" appears.
        5. Asserts presence/absence of DL_INFO-level "IntelAvbFilter:" messages.

    SKIP conditions (graceful):
      - DbgView binary not found (Install-DbgView.ps1 not run)
      - Driver not installed / device not present
      - Not elevated (registry write to HKLM\SYSTEM requires admin)
      - Release build of driver (DBG=0 — DEBUGP macro is a no-op)

.PARAMETER DbgViewLogFile
    Path to an existing DbgView log file to parse (offline mode).
    When provided, the script only runs the parse/assert steps (no reload needed).

.PARAMETER SkipReload
    Do not attempt driver reload.  Only parse the current DbgView log (if running)
    or -DbgViewLogFile.  Useful when running during VV-CORR-001 long-duration tests.

.PARAMETER InfPath
    Path to IntelAvbFilter.inf for driver reinstall.  Defaults to repo root.

.NOTES
    Traces to:  #95  (REQ-NF-DEBUG-REG-001: Registry-Based Debug Settings)
    Traces to:  #247 (TEST-DEBUG-REG-001: Verify Registry-Based Debug Settings)
    Closes gap: behavioral verbosity verification (CaptureDbgView path)

    Run via Run-Tests-Elevated.ps1 for automatic elevation + DbgView capture:
      .\tools\test\Run-Tests-Elevated.ps1 -TestName "Test-DebugLevelVerbosity" -CaptureDbgView
#>
param(
    [string] $DbgViewLogFile = "",
    [switch] $SkipReload,
    [string] $InfPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:PassCount = 0
$script:FailCount = 0
$script:SkipCount = 0

$repoRoot = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent

if (-not $InfPath) {
    $InfPath = Join-Path $repoRoot "IntelAvbFilter.inf"
}

$dbgViewScript   = Join-Path $repoRoot '.github\skills\DbgView\Start-DbgViewCapture.ps1'
$stopDbgScript   = Join-Path $repoRoot '.github\skills\DbgView\Stop-DbgViewCapture.ps1'
$installScript   = Join-Path $repoRoot 'tools\setup\Install-Driver.ps1'

$DEBUG_REG_KEY   = "HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter\Parameters"
$DEBUG_REG_VALUE = "DebugLevel"

# DL_* constants matching flt_dbg.h
$DL_WARN        = 4   # compiled-in default
$DL_INFO        = 6
$DL_LOUD        = 8

# Sentinel emitted by FilterReadDebugSettings():
#   "IntelAvbFilter: DebugLevel loaded from registry: N (path: ...)"
$SENTINEL_PATTERN = "IntelAvbFilter: DebugLevel loaded from registry:\s*(\d+)"

# DL_INFO messages visible at DL_INFO(6) or higher
# (from avb_bar0_discovery.c: "AvbIsSupportedIntelController: Checking adapter:")
$DL_INFO_PATTERN = "IntelAvbFilter:.*AvbIsSupportedIntelController"

# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

function Write-TestResult {
    param([string]$Status, [string]$Message)
    switch ($Status) {
        'PASS' { Write-Host "  [PASS] $Message" -ForegroundColor Green;  $script:PassCount++ }
        'FAIL' { Write-Host "  [FAIL] $Message" -ForegroundColor Red;    $script:FailCount++ }
        'SKIP' { Write-Host "  [SKIP] $Message" -ForegroundColor Yellow; $script:SkipCount++ }
        'INFO' { Write-Host "  [INFO] $Message" -ForegroundColor Cyan }
    }
}

function Test-IsElevated {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal($id)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-CurrentDebugLevel {
    try {
        $val = Get-ItemPropertyValue -Path $DEBUG_REG_KEY -Name $DEBUG_REG_VALUE -ErrorAction SilentlyContinue
        return $val
    } catch {
        return $null
    }
}

function Set-DebugLevel {
    param([int]$Level)
    if (-not (Test-Path $DEBUG_REG_KEY)) {
        New-Item -Path $DEBUG_REG_KEY -Force | Out-Null
    }
    Set-ItemProperty -Path $DEBUG_REG_KEY -Name $DEBUG_REG_VALUE -Value $Level -Type DWord
}

function Restore-DebugLevel {
    param($OriginalValue)
    if ($null -eq $OriginalValue) {
        try { Remove-ItemProperty -Path $DEBUG_REG_KEY -Name $DEBUG_REG_VALUE -ErrorAction SilentlyContinue } catch {}
    } else {
        Set-DebugLevel -Level $OriginalValue
    }
}

function Get-DbgViewLog {
    # Find the most recently modified DbgView log in logs\
    $logsDir = Join-Path $repoRoot "logs"
    if (-not (Test-Path $logsDir)) { return $null }
    $latest = Get-ChildItem -Path $logsDir -Filter "dbgview_*.log" -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending |
              Select-Object -First 1
    if ($latest) { return $latest.FullName }
    return $null
}

function Read-DbgViewLog {
    param([string]$LogPath)
    if (-not $LogPath -or -not (Test-Path $LogPath)) { return @() }
    # DbgView log format: "<line#>\t<timestamp>\t<message>"
    return Get-Content -Path $LogPath -Encoding UTF8 -ErrorAction SilentlyContinue
}

function Get-SentinelLevel {
    param([string[]]$Lines)
    foreach ($line in $Lines) {
        if ($line -match $SENTINEL_PATTERN) {
            return [int]$Matches[1]
        }
    }
    return $null
}

function Test-InfoMessagesPresent {
    param([string[]]$Lines)
    return ($Lines | Where-Object { $_ -match $DL_INFO_PATTERN }).Count -gt 0
}

# ─────────────────────────────────────────────────────────────────────────────
# TC-DEBUG-REG-VERB-001: Parse currently captured DbgView log for sentinel
# ─────────────────────────────────────────────────────────────────────────────
function Test-VerbTC001 {
    Write-Host "`n=== TC-DEBUG-REG-VERB-001: Sentinel in current DbgView log ===" -ForegroundColor White

    $logPath = if ($DbgViewLogFile) { $DbgViewLogFile } else { Get-DbgViewLog }

    if (-not $logPath) {
        Write-TestResult SKIP "No DbgView log found. Start test with -CaptureDbgView or supply -DbgViewLogFile."
        return
    }

    Write-TestResult INFO "Parsing log: $logPath"
    $lines = Read-DbgViewLog -LogPath $logPath

    $level = Get-SentinelLevel -Lines $lines
    if ($null -ne $level) {
        Write-TestResult PASS "Sentinel found: 'DebugLevel loaded from registry: $level'"
        Write-TestResult INFO "Driver was loaded with filterDebugLevel = $level (DL_WARN=4, DL_INFO=6, DL_LOUD=8)"
    } else {
        # Sentinel absent — either the log pre-dates this load, or release build
        $hasAnyAvbLine = ($lines | Where-Object { $_ -match "IntelAvbFilter:" }).Count -gt 0
        if ($hasAnyAvbLine) {
            Write-TestResult INFO "IntelAvbFilter messages found but no DebugLevel sentinel."
            Write-TestResult SKIP "Driver may be release build (DBG=0) or log predates current driver load."
        } else {
            Write-TestResult SKIP "No 'IntelAvbFilter:' messages in log. DbgView kernel capture may not be active."
        }
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# TC-DEBUG-REG-VERB-002: After reload with DL_LOUD — verbose messages appear
# Requires: elevated, debug driver installed, netcfg available
# ─────────────────────────────────────────────────────────────────────────────
function Test-VerbTC002 {
    Write-Host "`n=== TC-DEBUG-REG-VERB-002: Reload at DL_LOUD (8) — INFO messages present ===" -ForegroundColor White

    if ($SkipReload) {
        Write-TestResult SKIP "Skipped (SkipReload switch set — safe during VV-CORR-001)."
        return
    }
    if (-not (Test-IsElevated)) {
        Write-TestResult SKIP "Admin elevation required for driver reload. Run via Run-Tests-Elevated.ps1."
        return
    }
    if (-not (Test-Path $installScript)) {
        Write-TestResult SKIP "Install-Driver.ps1 not found at '$installScript'."
        return
    }
    if (-not (Test-Path $InfPath)) {
        Write-TestResult SKIP "IntelAvbFilter.inf not found at '$InfPath'."
        return
    }
    if (-not (Test-Path $dbgViewScript)) {
        Write-TestResult SKIP "Start-DbgViewCapture.ps1 not found. Run Install-DbgView.ps1 first."
        return
    }

    # Save original DebugLevel
    $originalLevel = Get-CurrentDebugLevel

    try {
        # 1. Write DL_LOUD to registry
        Set-DebugLevel -Level $DL_LOUD
        Write-TestResult INFO "DebugLevel set to DL_LOUD ($DL_LOUD) in registry."

        # 2. Start DbgView capture for this reload
        $logStem = "dbgview_verb002_loud"
        Write-TestResult INFO "Starting DbgView capture (stem: $logStem)..."
        $dbgProc = & $dbgViewScript -LogName $logStem
        if ($dbgProc) {
            Write-TestResult INFO "DbgView capturing on PID=$($dbgProc.Id)"
            Start-Sleep -Milliseconds 500
        } else {
            Write-TestResult SKIP "DbgView could not start (binary missing or no elevation). Skipping reload test."
            return
        }

        # 3. Reload driver: uninstall (netcfg -u) then reinstall (netcfg -l)
        Write-TestResult INFO "Reloading driver (netcfg uninstall + reinstall)..."
        try {
            # Uninstall
            $uninstOut = & netcfg.exe -u MS_IntelAvbFilter 2>&1 | Out-String
            Write-TestResult INFO "netcfg -u exit: $LASTEXITCODE"

            Start-Sleep -Seconds 3

            # Reinstall
            $reinstOut = & netcfg.exe -l $InfPath -c s -i MS_IntelAvbFilter 2>&1 | Out-String
            Write-TestResult INFO "netcfg -l exit: $LASTEXITCODE"

            Start-Sleep -Seconds 5   # let FilterAttach / FilterReadDebugSettings run
        } catch {
            Write-TestResult SKIP "Driver reload via netcfg failed: $_"
            if ($dbgProc) { & $stopDbgScript -ProcessId $dbgProc.Id -ErrorAction SilentlyContinue }
            return
        }

        # 4. Stop DbgView and read log
        $logPath = $null
        if ($dbgProc) {
            if (Test-Path $stopDbgScript) {
                & $stopDbgScript -ProcessId $dbgProc.Id
            } else {
                Stop-Process -Id $dbgProc.Id -Force -ErrorAction SilentlyContinue
            }
            # Find the log file we just created
            $logsDir = Join-Path $repoRoot "logs"
            $logPath = Get-ChildItem -Path $logsDir -Filter "${logStem}_*.log" -ErrorAction SilentlyContinue |
                       Sort-Object LastWriteTime -Descending |
                       Select-Object -First 1 |
                       Select-Object -ExpandProperty FullName
        }

        if (-not $logPath) {
            Write-TestResult FAIL "DbgView log file not found after reload."
            return
        }
        Write-TestResult INFO "Parsing log: $logPath"
        $lines = Read-DbgViewLog -LogPath $logPath

        # 5. Assert sentinel shows DL_LOUD
        $level = Get-SentinelLevel -Lines $lines
        if ($null -ne $level) {
            if ($level -eq $DL_LOUD) {
                Write-TestResult PASS "Sentinel: 'DebugLevel loaded from registry: $level' (DL_LOUD=$DL_LOUD) ✓"
            } else {
                Write-TestResult FAIL "Sentinel shows level $level, expected $DL_LOUD (DL_LOUD)."
            }
        } else {
            Write-TestResult FAIL "Sentinel 'DebugLevel loaded from registry' not found in log. Debug build required (DBG=1)."
        }

        # 6. Assert DL_INFO messages present (DL_LOUD >= DL_INFO so they should appear)
        if (Test-InfoMessagesPresent -Lines $lines) {
            Write-TestResult PASS "DL_INFO messages ('AvbIsSupportedIntelController') found — verbosity active at DL_LOUD."
        } else {
            Write-TestResult INFO "No DL_INFO messages found. Driver may not have attached to an adapter during test."
        }

    } finally {
        Restore-DebugLevel -OriginalValue $originalLevel
        Write-TestResult INFO "DebugLevel restored to $(if ($null -eq $originalLevel) { 'absent (default)' } else { $originalLevel })."
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# TC-DEBUG-REG-VERB-003: After reload with DL_WARN — DL_INFO messages absent
# ─────────────────────────────────────────────────────────────────────────────
function Test-VerbTC003 {
    Write-Host "`n=== TC-DEBUG-REG-VERB-003: Reload at DL_WARN (4) — INFO messages absent ===" -ForegroundColor White

    if ($SkipReload) {
        Write-TestResult SKIP "Skipped (SkipReload switch set — safe during VV-CORR-001)."
        return
    }
    if (-not (Test-IsElevated)) {
        Write-TestResult SKIP "Admin elevation required for driver reload. Run via Run-Tests-Elevated.ps1."
        return
    }
    if (-not (Test-Path $installScript)) {
        Write-TestResult SKIP "Install-Driver.ps1 not found at '$installScript'."
        return
    }
    if (-not (Test-Path $InfPath)) {
        Write-TestResult SKIP "IntelAvbFilter.inf not found at '$InfPath'."
        return
    }
    if (-not (Test-Path $dbgViewScript)) {
        Write-TestResult SKIP "Start-DbgViewCapture.ps1 not found. Run Install-DbgView.ps1 first."
        return
    }

    $originalLevel = Get-CurrentDebugLevel

    try {
        Set-DebugLevel -Level $DL_WARN
        Write-TestResult INFO "DebugLevel set to DL_WARN ($DL_WARN) in registry."

        $logStem = "dbgview_verb003_warn"
        Write-TestResult INFO "Starting DbgView capture (stem: $logStem)..."
        $dbgProc = & $dbgViewScript -LogName $logStem
        if ($dbgProc) {
            Write-TestResult INFO "DbgView capturing on PID=$($dbgProc.Id)"
            Start-Sleep -Milliseconds 500
        } else {
            Write-TestResult SKIP "DbgView could not start. Skipping reload test."
            return
        }

        Write-TestResult INFO "Reloading driver (netcfg uninstall + reinstall)..."
        try {
            & netcfg.exe -u MS_IntelAvbFilter 2>&1 | Out-Null
            Start-Sleep -Seconds 3
            & netcfg.exe -l $InfPath -c s -i MS_IntelAvbFilter 2>&1 | Out-Null
            Start-Sleep -Seconds 5
        } catch {
            Write-TestResult SKIP "Driver reload via netcfg failed: $_"
            if ($dbgProc) { & $stopDbgScript -ProcessId $dbgProc.Id -ErrorAction SilentlyContinue }
            return
        }

        $logPath = $null
        if ($dbgProc) {
            if (Test-Path $stopDbgScript) {
                & $stopDbgScript -ProcessId $dbgProc.Id
            } else {
                Stop-Process -Id $dbgProc.Id -Force -ErrorAction SilentlyContinue
            }
            $logsDir = Join-Path $repoRoot "logs"
            $logPath = Get-ChildItem -Path $logsDir -Filter "${logStem}_*.log" -ErrorAction SilentlyContinue |
                       Sort-Object LastWriteTime -Descending |
                       Select-Object -First 1 |
                       Select-Object -ExpandProperty FullName
        }

        if (-not $logPath) {
            Write-TestResult FAIL "DbgView log file not found after reload."
            return
        }
        Write-TestResult INFO "Parsing log: $logPath"
        $lines = Read-DbgViewLog -LogPath $logPath

        # Assert sentinel shows DL_WARN
        $level = Get-SentinelLevel -Lines $lines
        if ($null -ne $level) {
            if ($level -eq $DL_WARN) {
                Write-TestResult PASS "Sentinel: 'DebugLevel loaded from registry: $level' (DL_WARN=$DL_WARN) ✓"
            } else {
                Write-TestResult FAIL "Sentinel shows level $level, expected $DL_WARN (DL_WARN)."
            }
        } else {
            Write-TestResult FAIL "Sentinel 'DebugLevel loaded from registry' not found in log. Debug build required (DBG=1)."
        }

        # Assert DL_INFO messages ARE suppressed (DL_WARN < DL_INFO)
        if (Test-InfoMessagesPresent -Lines $lines) {
            Write-TestResult FAIL "DL_INFO messages found at DL_WARN level — verbosity NOT suppressed correctly."
        } else {
            Write-TestResult PASS "DL_INFO messages absent at DL_WARN level — verbosity suppressed correctly ✓"
        }

    } finally {
        Restore-DebugLevel -OriginalValue $originalLevel
        Write-TestResult INFO "DebugLevel restored to $(if ($null -eq $originalLevel) { 'absent (default)' } else { $originalLevel })."
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

Write-Host "========================================================================" -ForegroundColor White
Write-Host " Test-DebugLevelVerbosity.ps1" -ForegroundColor White
Write-Host " Verifies: #95 REQ-NF-DEBUG-REG-001 / #247 TEST-DEBUG-REG-001" -ForegroundColor White
Write-Host " Gap: behavioral verbosity verification via DbgView log parsing" -ForegroundColor White
Write-Host "========================================================================" -ForegroundColor White

if ($DbgViewLogFile) {
    Write-Host "[INFO] Offline mode: using log file '$DbgViewLogFile'" -ForegroundColor Cyan
}
if ($SkipReload) {
    Write-Host "[INFO] SkipReload: only log-parse tests will run (safe during VV-CORR-001)" -ForegroundColor Cyan
}

Test-VerbTC001   # always runs — parse existing log or DbgViewLogFile
Test-VerbTC002   # requires elevation + debug driver + DbgView binary + netcfg
Test-VerbTC003   # requires elevation + debug driver + DbgView binary + netcfg

Write-Host "`n========================================================================" -ForegroundColor White
Write-Host " Summary" -ForegroundColor White
Write-Host "========================================================================" -ForegroundColor White
Write-Host "  Pass: $($script:PassCount)" -ForegroundColor Green
Write-Host "  Fail: $($script:FailCount)" -ForegroundColor Red
Write-Host "  Skip: $($script:SkipCount)" -ForegroundColor Yellow
Write-Host "========================================================================" -ForegroundColor White

if ($script:FailCount -gt 0) {
    Write-Host "`nRESULT: FAILURE" -ForegroundColor Red
    exit 1
} else {
    Write-Host "`nRESULT: SUCCESS (Fail=0; skips are expected when debug driver absent)" -ForegroundColor Green
    exit 0
}
