# NativeExit.ps1 - reliable native / child-script exit codes across PowerShell scope boundaries, and verified termination
# of a timed-out process tree (Stop-AvbProcessTree) (dot-source, no side effects)
#
# PowerShell writes the exit code of a native command (and of `exit` in a script invoked with &) ONLY to
# $global:LASTEXITCODE.  An unqualified `$LASTEXITCODE = ...` in any CALLER creates a scope-local variable that
# shadows the automatic one for every script that caller invokes (dynamic scoping), so an unqualified read can
# return the caller's stale value - e.g. $null after a successful MSBuild - and `exit $null` ends the process with 0.
# Reproduced from FullSuite_20261021_095005.log by tests/unit/build/test_native_exit_codes.ps1.
#
# Contract: 0 = verified success; any other value = failure; no observed exit code ($null) = failure.
# Usage around every native call:
#     Reset-AvbExitCode
#     & $tool @args
#     $rc = Get-AvbExitCode
#     if (-not (Test-AvbExitSuccess $rc)) { "... exit code $(Format-AvbExitCode $rc)"; exit (Get-AvbFailureExitCode $rc) }

# Clears the global exit code so a value left by an EARLIER command can never be read as this command's result.
function Reset-AvbExitCode { $global:LASTEXITCODE = $null }

# Exit code of the most recent native command / script `exit`; $null when none ran since Reset-AvbExitCode.
function Get-AvbExitCode {
    $c = $global:LASTEXITCODE
    if ($null -eq $c) { return $null }
    return [int]$c
}

function Test-AvbExitSuccess { param($Code) return ($null -ne $Code -and [int]$Code -eq 0) }

function Format-AvbExitCode {
    param($Code)
    if ($null -eq $Code) { return '<none: no exit code observed>' }
    return [string]$Code
}

# Process exit code for a failure: the observed nonzero code, else 1 (never 0, never $null).
function Get-AvbFailureExitCode {
    param($Code)
    if ($null -eq $Code -or [int]$Code -eq 0) { return 1 }
    return [int]$Code
}

# Kills a timed-out test and every descendant process (PowerShell 5.1 Process.Kill() ends only the root) and waits for
# each to exit.  A process has exited only after all of its I/O requests completed or were cancelled, so Ok = every
# process of the tree gone = no outstanding driver operation of the test.  @{ Ok; Detail }.
# Shared by Run-Tests-CI.ps1 (per-test timeout) and Run-Tests-Elevated.ps1 (elevated child timeout; 2026-10-24 #328:
# killing only the root left the test's installer chain running for two days).
function Stop-AvbProcessTree {
    param([System.Diagnostics.Process]$Process, [int]$WaitSeconds = 30)
    $tree = @(); $todo = @($Process.Id)
    while ($todo.Count -gt 0) {
        $next = @()
        foreach ($p in $todo) {
            foreach ($c in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$p" -ErrorAction SilentlyContinue)) {
                if ($tree -notcontains [int]$c.ProcessId) { $tree += [int]$c.ProcessId; $next += [int]$c.ProcessId }
            }
        }
        $todo = $next
    }
    try { $Process.Kill() } catch { }
    foreach ($id in $tree) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
    $rootGone = $Process.WaitForExit($WaitSeconds * 1000)
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $alive = @($tree)
    while ($alive.Count -gt 0 -and (Get-Date) -lt $deadline) {
        $alive = @($alive | Where-Object { $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue) })
        if ($alive.Count -gt 0) { Start-Sleep -Milliseconds 200 }
    }
    $detail = "root pid $($Process.Id) $(if ($rootGone) { 'exited' } else { "still running after ${WaitSeconds}s" }); descendants: " +
              $(if ($tree.Count -eq 0) { 'none' } elseif ($alive.Count -eq 0) { "$($tree -join ',') terminated" } else { "STILL RUNNING $($alive -join ',')" })
    return @{ Ok = ($rootGone -and $alive.Count -eq 0); Detail = $detail }
}
