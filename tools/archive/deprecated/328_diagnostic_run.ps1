# 328_diagnostic_run.ps1 -- operator-run #328 idle/display-sleep experiment (TC-LCY-IDLE-001)
#
# Run from an ELEVATED PowerShell at the repo root:
#     .\tools\archive\deprecated\328_diagnostic_run.ps1
#
# What it changes (all temporary, restored in `finally`):
#   * Microsoft-Windows-NDIS/Diagnostic: max size -> 64 MB, enabled -> true
# What it never does: install/reload the driver, touch NIC power settings, sleep the
# machine, restart services, or clear the channel (the evaluator filters by Phase B window).
#
# During Phase B (test window): lock the session (Win+L), let the display turn off,
# wake and unlock, then press Enter in the test window.  Timeout default: 1200 s.
param(
    [int]$TimeoutSeconds = 1200,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$channel  = 'Microsoft-Windows-NDIS/Diagnostic'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Set-Location $repoRoot

function Invoke-Wevtutil {
    # Native tools do not throw: fail loudly on a non-zero exit code.
    $out = & wevtutil @args 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "wevtutil $($args -join ' ') failed (exit $LASTEXITCODE): $out" }
    return $out
}

# Elevated = Administrators group (enum, NOT the string 'Administrator', which resolves to an
# account name) OR a High-integrity token (S-1-16-12288).
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin  = ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
            ($identity.Groups | Where-Object { $_.Value -eq 'S-1-16-12288' })
if (-not $isAdmin) {
    throw "Not elevated (user=$($identity.Name)): wevtutil sl needs an Administrator PowerShell. Nothing was changed."
}

$stamp   = Get-Date -Format 'yyyyMMdd_HHmmss'
$logFile = Join-Path $repoRoot "logs\test_device_idle_regression_$stamp.log"
$evtx    = Join-Path $repoRoot "logs\ndis_diag_$stamp.evtx"
$result  = Join-Path $repoRoot "logs\idle_eval_$stamp.json"
New-Item -ItemType Directory -Force -Path (Join-Path $repoRoot 'logs') | Out-Null

# Remember the original configuration so it can be restored exactly.
$cfg = Invoke-Wevtutil gl $channel
$origEnabled = ($cfg -match '(?m)^\s*enabled:\s*true')
if ($cfg -notmatch '(?m)^\s*maxSize:\s*(\d+)') { throw "Cannot parse maxSize from: $cfg" }
$origMaxSize = $Matches[1]
Write-Host "Original $channel : enabled=$origEnabled maxSize=$origMaxSize"

$evalExit = 4
try {
    # /q:true suppresses the "enabling an analytic log clears it" prompt.
    Invoke-Wevtutil sl $channel '/ms:67108864' | Out-Null
    Invoke-Wevtutil sl $channel '/e:true' '/q:true' | Out-Null
    Write-Host "Channel enabled (64 MB). Starting test in a NEW elevated window (accept UAC)."
    Write-Host "  In that window, after 'Phase B' appears: lock (Win+L), let the display turn off, wake, unlock,"
    Write-Host "  then PRESS ENTER in that window.  Without Enter the run only ends after $TimeoutSeconds s."

    & (Join-Path $repoRoot 'tools\test\Run-Tests-Elevated.ps1') -Configuration $Configuration `
        -TestName test_device_idle_regression -TestArgs "$TimeoutSeconds" -LogFile $logFile
    $testExit = $LASTEXITCODE
    # The runner reports the exe's observational exit 3 (INCONCLUSIVE) as exit 1 ("1 test failed").
    # That is NOT the #328 verdict; the evaluator below is.
    Write-Host "Run-Tests-Elevated exit code: $testExit (1 is normal for an observational exit 3; the evaluator decides)"

    # Export BEFORE restoring/disabling.  /ow:true overwrites a stale file of the same name.
    Invoke-Wevtutil epl $channel $evtx '/ow:true' | Out-Null
    if (-not (Test-Path -LiteralPath $evtx)) { throw "Export produced no file: $evtx" }
    Write-Host "Exported: $evtx"
}
finally {
    # Always restore the original channel configuration.
    # ORDER MATTERS: disable first, then shrink.  Shrinking an enabled log below its current
    # file size is refused (observed 2026-10-16: 18 MB live file vs 1 MB target).
    $enabledArg = "/e:$($origEnabled.ToString().ToLower())"
    try { Invoke-Wevtutil sl $channel $enabledArg '/q:true' | Out-Null }
    catch { Write-Host "RESTORE (enabled) FAILED -- run: wevtutil sl $channel $enabledArg ($_)" -ForegroundColor Red }
    try { Invoke-Wevtutil sl $channel "/ms:$origMaxSize" | Out-Null }
    catch {
        Write-Host ("RESTORE (maxSize) FAILED -- the live log file is larger than $origMaxSize bytes. " +
                    "The export is already saved ($evtx); after reviewing it, clear and shrink: " +
                    "wevtutil cl $channel ; wevtutil sl $channel /ms:$origMaxSize ($_)") -ForegroundColor Red
    }
    Write-Host 'Channel state after restore:'
    (& wevtutil gl $channel) | Select-String 'enabled|maxSize' | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
}

if (-not (Test-Path -LiteralPath $logFile)) { throw "Test log not found: $logFile" }

& (Join-Path $repoRoot 'tools\test\Evaluate-IdleTransition.ps1') `
    -LogFile $logFile -EventFile $evtx -ResultFile $result
$evalExit = $LASTEXITCODE
Write-Host "Evaluator exit code: $evalExit (0 PASS, 1 DEVICE_FAILURE_OBSERVED, 2 BASELINE_UNAVAILABLE, 3 INCONCLUSIVE, 4 evaluator error)"
Write-Host "Artifacts: $logFile | $evtx | $result"
exit $evalExit
