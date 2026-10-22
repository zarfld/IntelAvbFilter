# NativeExit.ps1 - reliable native / child-script exit codes across PowerShell scope boundaries (dot-source, no side effects)
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
