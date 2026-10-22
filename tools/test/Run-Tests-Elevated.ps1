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
    # The test is NOT executed when the capture cannot be confirmed as started (process alive + log file created).
    [Parameter(Mandatory=$false)]
    [switch]$CaptureDbgView,

    # Run -TestName through tools\test\Run-Tests-CI.ps1 in this Hardware* suite (build provenance, safety class,
    # adapter baseline and post-test state check apply) instead of the legacy dispatcher Run-Tests.ps1.
    # -TestName is then the runner entry, e.g. 'test_ptp_phc_stability --case UT-CORR-005' or 'ps1:tests/...'.
    [Parameter(Mandatory=$false)]
    [ValidateSet('', 'HardwareUnit', 'HardwareIntegration', 'HardwareMandatory', 'HardwareMutation')]
    [string]$Suite = '',

    # With -Suite: machine-readable results document of the run (Run-Tests-CI.ps1 -ResultsJson).
    [Parameter(Mandatory=$false)]
    [string]$ResultsJson = '',

    # Bound for the elevated child; on expiry it is terminated and the run reported as TIMEOUT.
    [Parameter(Mandatory=$false)]
    [int]$TimeoutSeconds = 1800
)

$scriptPath = Join-Path $PSScriptRoot 'Run-Tests.ps1'
$runCiPath  = Join-Path $PSScriptRoot 'Run-Tests-CI.ps1'
$repoRoot   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

function ConvertTo-AvbPsLiteral { param([string]$Text) return "'" + $Text.Replace("'", "''") + "'" }

# Command for the elevated child.  The exit code of the dispatcher is captured BEFORE Stop-Transcript: PowerShell
# -Command exits with the value of the last expression, and Stop-Transcript's return value would overwrite it.
# Captured via $global:LASTEXITCODE (reset first; no exit code = failure), then exit explicitly.
function New-AvbElevatedCommand {
    param([string]$Configuration, [string]$TestName, [string]$TestArgs, [string]$TestExecutable, [bool]$Full,
          [string]$LogFile, [string]$Suite, [string]$ResultsJson, [string]$LegacyScript, [string]$RunCiScript)
    $command = if ($LogFile) { "Start-Transcript -Path $(ConvertTo-AvbPsLiteral $LogFile) -Force | Out-Null; " } else { '' }
    $command += "`$_innerCode = 1; `$global:LASTEXITCODE = `$null; "
    if ($Suite) {
        $entry = if ($TestArgs) { "$TestName $TestArgs" } else { $TestName }
        $command += "try { & $(ConvertTo-AvbPsLiteral $RunCiScript) -Suite $Suite -Configuration $Configuration -Tests @($(ConvertTo-AvbPsLiteral $entry))"
        if ($ResultsJson) { $command += " -ResultsJson $(ConvertTo-AvbPsLiteral $ResultsJson)" }
    } else {
        $command += "try { & $(ConvertTo-AvbPsLiteral $LegacyScript) -Configuration $Configuration"
        if ($Full) { $command += " -Full" }
        if ($TestName) {
            # PS1 test scripts (e.g. Test-DebugLevelVerbosity) are dispatched directly; all other names get .exe appended if missing.
            $testExe = if ($TestName -match '\.ps1$') { $TestName } elseif ($TestName -notmatch '\.exe$') { "$TestName.exe" } else { $TestName }
            $command += " -TestExecutable $(ConvertTo-AvbPsLiteral $testExe)"
        }
        if ($TestArgs) { $command += " -TestArgs $(ConvertTo-AvbPsLiteral $TestArgs)" }
        if ($TestExecutable) { $command += " -TestExecutable $(ConvertTo-AvbPsLiteral $TestExecutable)" }
        if ($LogFile) { $command += " -CollectLogs" }   # always collect logs when logging to file
    }
    $command += "; `$_innerCode = `$global:LASTEXITCODE; if (`$null -eq `$_innerCode) { `$_innerCode = 1 } } catch { `$_innerCode = 1 } finally { "
    $command += $(if ($LogFile) { "Stop-Transcript | Out-Null" } else { "`$null" })
    $command += " }; exit `$_innerCode"
    return $command
}

# Capture is confirmed only when the DbgView process is alive AND its log file exists.  @{ Ok; Error }
function Wait-AvbDbgViewStarted {
    param($Process, [string]$LogFile, [int]$TimeoutSeconds = 10)
    if ($null -eq $Process) { return @{ Ok = $false; Error = 'DbgView was not started (start script returned no process; Dbgview.exe missing or launch refused)' } }
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        if ($Process.HasExited) { return @{ Ok = $false; Error = "DbgView PID $($Process.Id) exited during start-up (another kernel capture active?)" } }
        if (Test-Path -LiteralPath $LogFile) { return @{ Ok = $true; Error = '' } }
        if ((Get-Date) -ge $deadline) { return @{ Ok = $false; Error = "DbgView PID $($Process.Id) is running but created no log file '$LogFile' within ${TimeoutSeconds}s" } }
        Start-Sleep -Milliseconds 250
    }
}

# After the stop: CAPTURED (non-empty log) | EMPTY | MISSING.  Never claims a capture without a log.
function Get-AvbDbgViewCaptureResult {
    param([string]$LogFile)
    if (-not (Test-Path -LiteralPath $LogFile)) { return @{ Status = 'MISSING'; Bytes = 0 } }
    $bytes = (Get-Item -LiteralPath $LogFile).Length
    return @{ Status = $(if ($bytes -gt 0) { 'CAPTURED' } else { 'EMPTY' }); Bytes = $bytes }
}

# Default log file if not specified - use logs directory
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logsDir = Join-Path $repoRoot "logs"
if (-not $LogFile) {
    if ($TestName -or $Full) {
        if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir -Force | Out-Null }
        if ($TestName) {
            $LogFile = Join-Path $logsDir (("{0}_{1}.log" -f $TestName, $timestamp) -replace '[^\w.\-\\:]', '_')
        } elseif ($Full) {
            $LogFile = Join-Path $logsDir "full_test_suite_$timestamp.log"
        }
    }
}
if ($Suite -and -not $TestName) { Write-Host "[FAIL] -Suite requires -TestName (the runner entry)" -ForegroundColor Red; exit 1 }

$command = New-AvbElevatedCommand -Configuration $Configuration -TestName $TestName -TestArgs $TestArgs -TestExecutable $TestExecutable `
               -Full ([bool]$Full) -LogFile $LogFile -Suite $Suite -ResultsJson $ResultsJson -LegacyScript $scriptPath -RunCiScript $runCiPath
$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $command)

if ($LogFile) {
    Write-Host "Output will be logged to: $LogFile" -ForegroundColor Cyan
}

# -- Optional: DebugView kernel capture, confirmed BEFORE the test starts --------
$dbgViewProc = $null
$dbgLog = $null
$capture = $null
if ($CaptureDbgView) {
    $dbgViewScript = Join-Path $repoRoot '.github\skills\DbgView\Start-DbgViewCapture.ps1'
    $stem = if ($TestName) { "dbgview_$TestName" } elseif ($Full) { 'dbgview_full_test_suite' } else { 'dbgview_tests' }
    $dbgLog = Join-Path $logsDir ((("{0}_{1}" -f $stem, $timestamp) -replace '[^\w.\-]', '_') + '.log')
    $started = @{ Ok = $false; Error = "start script not found at '$dbgViewScript'" }
    if (Test-Path $dbgViewScript) {
        Write-Host "[DbgView] Starting kernel capture -> $dbgLog" -ForegroundColor Cyan
        try { $dbgViewProc = & $dbgViewScript -LogFile $dbgLog } catch { $dbgViewProc = $null; Write-Host "[DbgView] start script threw: $($_.Exception.Message)" -ForegroundColor Red }
        $started = Wait-AvbDbgViewStarted -Process $dbgViewProc -LogFile $dbgLog
    }
    if (-not $started.Ok) {
        Write-Host "[DbgView] CAPTURE NOT STARTED: $($started.Error) --- test NOT executed (a reproduction without its kernel log is not the requested evidence)" -ForegroundColor Red
        if ($dbgViewProc -and -not $dbgViewProc.HasExited) { Stop-Process -Id $dbgViewProc.Id -Force -ErrorAction SilentlyContinue }
        exit 1
    }
    Write-Host "[DbgView] Capture confirmed: PID=$($dbgViewProc.Id), log $dbgLog" -ForegroundColor Green
    $capture = [ordered]@{ testName = $TestName; testArgs = $TestArgs; configuration = $Configuration; suite = $Suite; dbgViewPid = $dbgViewProc.Id
                           dbgViewLog = $dbgLog; startedUtc = (Get-Date).ToUniversalTime().ToString('o'); transcript = $LogFile; resultsJson = $ResultsJson
                           installedDriverSha256 = $null; outcome = $null; testExitCode = $null; stoppedUtc = $null; stop = $null; logStatus = $null; logBytes = 0 }
    $drv = Join-Path $env:SystemRoot 'System32\drivers\IntelAvbFilter.sys'
    if (Test-Path -LiteralPath $drv) { $capture.installedDriverSha256 = (Get-FileHash -LiteralPath $drv -Algorithm SHA256).Hash }
}

# -- Launch elevated child (bounded) and capture its exit code -------------------
# -PassThru returns a Process object; touching .Handle keeps it so ExitCode is readable after the wait (P0 fix #328).
$outcome = 'COMPLETED'
$childProc = Start-Process powershell -Verb RunAs -ArgumentList $arguments -PassThru
$childExitCode = -1
if ($null -ne $childProc) {
    $null = $childProc.Handle
    if ($childProc.WaitForExit([Math]::Max(1, $TimeoutSeconds) * 1000)) {
        $childExitCode = $childProc.ExitCode
        if ($null -eq $childExitCode) { $childExitCode = 1; $outcome = 'NO_EXIT_CODE' }
    } else {
        $outcome = 'TIMEOUT'
        Write-Host "[TIMEOUT] elevated test process PID=$($childProc.Id) still running after ${TimeoutSeconds}s --- terminated; hardware state must be inspected" -ForegroundColor Red
        Stop-Process -Id $childProc.Id -Force -ErrorAction SilentlyContinue
        $childExitCode = 1
    }
} else {
    $outcome = 'NOT_STARTED'
}

# -- Stop exactly the DebugView instance started above, verify the log -----------
$captureFailed = $false
if ($capture) {
    $stopScript = Join-Path $repoRoot '.github\skills\DbgView\Stop-DbgViewCapture.ps1'
    try {
        if (Test-Path $stopScript) { & $stopScript -ProcessId $dbgViewProc.Id } else { Stop-Process -Id $dbgViewProc.Id -Force }
    } catch { Write-Host "[DbgView] stop threw: $($_.Exception.Message)" -ForegroundColor Red }
    $capture.stop = if ($dbgViewProc.WaitForExit(5000)) { 'STOPPED' } else { 'STOP_FAILED' }
    $res = Get-AvbDbgViewCaptureResult -LogFile $dbgLog
    $capture.logStatus = $res.Status; $capture.logBytes = $res.Bytes
    $capture.outcome = $outcome; $capture.testExitCode = $childExitCode; $capture.stoppedUtc = (Get-Date).ToUniversalTime().ToString('o')
    ($capture | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath "$dbgLog.capture.json" -Encoding UTF8
    if ($capture.stop -ne 'STOPPED') { Write-Host "[DbgView] CAPTURE STOP FAILED: PID=$($dbgViewProc.Id) still running (cleanup failure, distinct from the test result)" -ForegroundColor Red; $captureFailed = $true }
    if ($res.Status -ne 'CAPTURED') { Write-Host "[DbgView] CAPTURE $($res.Status): '$dbgLog' --- no kernel log for this run" -ForegroundColor Red; $captureFailed = $true }
    else { Write-Host "[DbgView] Captured $($res.Bytes) bytes -> $dbgLog (metadata: $dbgLog.capture.json)" -ForegroundColor Green }
}

if ($LogFile -and (Test-Path $LogFile)) {
    Write-Host "`nLog file created: $LogFile" -ForegroundColor Green
}

# -- Propagate child exit code -------------------------------------------------
# Exit codes from the dispatcher (and native test binaries):
#   0 = PASS          -- all tests passed
#   1 = FAIL/BLOCKED  -- at least one test failed, blocked, or CLEANUP_FAILED; also TIMEOUT / no exit code here
#   2 = SKIP_ONLY     -- tests ran but no hardware found; all SKIP_NO_TESTS
# A requested capture that failed never turns into success.
if ($outcome -eq 'NOT_STARTED') { Write-Host "[FAIL] elevated test process was not started" -ForegroundColor Red; exit 1 }
if ($captureFailed -and $childExitCode -eq 0) { exit 1 }
exit $childExitCode
