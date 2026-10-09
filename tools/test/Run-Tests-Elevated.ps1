param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [Parameter(Mandatory=$false)]
    [switch]$Full,
    
    [Parameter(Mandatory=$false)]
    [string]$TestName,
    
    [Parameter(Mandatory=$false)]
    [string]$TestExecutable,
    
    [Parameter(Mandatory=$false)]
    [string]$TestArgs = "",

    [Parameter(Mandatory=$false)]
    [string]$LogFile,

    # Start DebugView kernel capture around the test run and save to a log file.
    # Requires .github\skills\DbgView\DebugView\Dbgview.exe (run tools\setup\Install-DbgView.ps1 once).
    [Parameter(Mandatory=$false)]
    [switch]$CaptureDbgView
)

$scriptPath = Join-Path $PSScriptRoot 'Run-Tests.ps1'
$repoRoot   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# Default log file if not specified - use logs directory
if (-not $LogFile) {
    if ($TestName -or $Full) {
        $logsDir = Join-Path $repoRoot "logs"
        if (-not (Test-Path $logsDir)) {
            New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
        }
        $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
        
        if ($TestName) {
            $LogFile = Join-Path $logsDir "$TestName`_$timestamp.log"
        } elseif ($Full) {
            $LogFile = Join-Path $logsDir "full_test_suite_$timestamp.log"
        }
    }
}

# Build command to run script with transcript logging
if ($LogFile) {
    $command = "Start-Transcript -Path '$LogFile' -Force; "
} else {
    $command = ""
}
$command += "& '$scriptPath' -Configuration $Configuration"
if ($Full) {
    $command += " -Full"
}
if ($TestName) {
    # PS1 test scripts (e.g. Test-DebugLevelVerbosity) are dispatched directly;
    # all other names get .exe appended if missing.
    $testExe = if ($TestName -match '\.ps1$') { $TestName }
               elseif ($TestName -notmatch '\.exe$') { "$TestName.exe" }
               else { $TestName }
    $command += " -TestExecutable '$testExe'"
}
if ($TestArgs) {
    $command += " -TestArgs '$TestArgs'"
}
if ($TestExecutable) {
    $command += " -TestExecutable '$TestExecutable'"
}
# Always collect logs when logging to file
if ($LogFile) {
    $command += " -CollectLogs"
}
if ($LogFile) {
    $command += "; Stop-Transcript"
}
#$command += "; Write-Host ''; Write-Host 'Press any key to close...' -ForegroundColor Yellow; `$null = `$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')"

$arguments = @(
    '-NoProfile'
    '-ExecutionPolicy'
    'Bypass'
    '-Command'
    $command
)

if ($LogFile) {
    Write-Host "Output will be logged to: $LogFile" -ForegroundColor Cyan
}

# ── Optional: start DebugView kernel capture ───────────────────────────────────
$dbgViewProc = $null
if ($CaptureDbgView) {
    $dbgViewScript = Join-Path $repoRoot '.github\skills\DbgView\Start-DbgViewCapture.ps1'
    if (Test-Path $dbgViewScript) {
        # Derive log stem from test name or suite label
        $dbgLogStem = if ($TestName) { "dbgview_$TestName" }
                      elseif ($Full)  { 'dbgview_full_test_suite' }
                      else            { 'dbgview_tests' }
        Write-Host "[DbgView] Starting kernel capture (stem: $dbgLogStem)..." -ForegroundColor Cyan
        $dbgViewProc = & $dbgViewScript -LogName $dbgLogStem
        if ($dbgViewProc) {
            Write-Host "[DbgView] Capturing on PID=$($dbgViewProc.Id)" -ForegroundColor Green
            Start-Sleep -Milliseconds 500   # give DbgView time to open the log file
        }
    } else {
        Write-Warning "[DbgView] Start script not found at '$dbgViewScript'. Skipping capture."
    }
}

# ── Launch elevated child and capture its exit code ───────────────────────────
# -PassThru returns a Process object so we can read ExitCode after -Wait.
# Without -PassThru the exit code is silently discarded (P0 fix #328).
$childProc = Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait -PassThru
$childExitCode = if ($null -ne $childProc) { $childProc.ExitCode } else { -1 }

# ── Stop DebugView if we started it ───────────────────────────────────────────
if ($dbgViewProc) {
    $stopScript = Join-Path $repoRoot '.github\skills\DbgView\Stop-DbgViewCapture.ps1'
    if (Test-Path $stopScript) {
        & $stopScript -ProcessId $dbgViewProc.Id
    } else {
        Stop-Process -Id $dbgViewProc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "[DbgView] Stopped PID=$($dbgViewProc.Id)" -ForegroundColor Green
    }
}

if ($LogFile -and (Test-Path $LogFile)) {
    Write-Host "`nLog file created: $LogFile" -ForegroundColor Green
}

# ── Propagate child exit code ─────────────────────────────────────────────────
# Exit codes from Run-Tests.ps1 (and native test binaries):
#   0 = PASS          — all tests passed
#   1 = FAIL/BLOCKED  — at least one test failed, blocked, or CLEANUP_FAILED
#   2 = SKIP_ONLY     — tests ran but no hardware found; all SKIP_NO_TESTS
# The elevated wrapper must not swallow these; callers (CI, scripts) depend on them.
exit $childExitCode
