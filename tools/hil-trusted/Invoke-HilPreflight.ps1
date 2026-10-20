# Invoke-HilPreflight.ps1 -- trusted HIL preflight entry point (REFERENCE COPY)
#
# This file is NOT executed from the repository checkout.  The operator reviews it and installs it, together with
# HilPreflight.Policy.ps1, into a runner-local folder outside the workspace and the runner application directory
# (C:\hil-trusted; writable only by SYSTEM/Administrators; the runner account must not be an elevated admin).
# The installed copy is used twice:
#   1. as the runner's job-started hook (actions-runner\.env: ACTIONS_RUNNER_HOOK_JOB_STARTED=<installed path>);
#      the runner DOT-SOURCES .ps1 hooks (`pwsh -command ". '<path>'"`), so this script has no parameters and no
#      early return - every invocation evaluates the full policy;
#   2. as the first step of .github/workflows/hil.yml (defence in depth).
# Exactly one exit: 0 only when every check passed; 1 on any denial, error, or timeout.  No state is kept between
# jobs, so a failure cannot leave the runner "authorized".  Nothing here changes the machine.

$ErrorActionPreference = 'Stop'
$hilExit = 1
try {
    $hilRoot = $PSScriptRoot
    if (-not $hilRoot) { throw 'cannot determine the installed preflight location' }
    $hilLib = Join-Path $hilRoot 'HilPreflight.Policy.ps1'
    if (-not (Test-Path -LiteralPath $hilLib)) { throw "policy library missing: $hilLib" }
    . $hilLib

    $hilPolicy  = Get-HilPolicy
    $hilJobEnv  = Get-HilJobEnv
    $hilReasons = @()
    $hilReasons += @((Test-HilScriptLocation -ScriptRoot $hilRoot -JobEnv $hilJobEnv).Reasons)
    foreach ($p in @($hilRoot) + @($hilPolicy.TrustedFiles | ForEach-Object { Join-Path $hilRoot $_ } | Where-Object { Test-Path -LiteralPath $_ })) {
        $hilReasons += @((Test-HilAclRules -Rules (Get-HilAclRules $p) -Label $p).Reasons)
        $hilReasons += @((Test-HilAclOwner -OwnerSid (Get-HilAclOwnerSid $p) -Label $p).Reasons)
    }
    $hilPrincipal = New-Object System.Security.Principal.WindowsPrincipal([System.Security.Principal.WindowsIdentity]::GetCurrent())
    $hilReasons += @((Test-HilRunnerContext -Policy $hilPolicy -IsElevatedAdmin $hilPrincipal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)).Reasons)
    $hilReasons += @((Test-HilJobAuthorization -JobEnv $hilJobEnv -Policy $hilPolicy -AllowedActors (Read-HilAllowedActors $hilRoot)).Reasons)

    $hilBaselineOk = $false   # authorization requires POSITIVE evidence: an explicit, sole 'OK' verdict
    if ($hilReasons.Count -eq 0) {
        $hilMb = Invoke-HilWithTimeout -Seconds $hilPolicy.TimeoutSeconds -ArgumentList @($hilLib, $hilRoot) -ScriptBlock {
            param($lib, $root)
            . $lib
            $res = Test-HilMachineBaseline -Policy (Get-HilPolicy) -Root $root
            if ($res.Ok) { 'OK' } else { $res.Reasons }
        }
        if ($hilMb.TimedOut -or $hilMb.Error) { $hilReasons += "machine baseline not completed: $($hilMb.Error)" }
        else {
            $out = @(@($hilMb.Result) | Where-Object { $null -ne $_ } | ForEach-Object { [string]$_ } | Where-Object { $_ -ne '' })
            if ($out.Count -eq 1 -and $out[0] -ceq 'OK') { $hilBaselineOk = $true }
            else {
                $hilReasons += 'machine baseline did not return an explicit OK verdict'
                $hilReasons += @($out | Where-Object { $_ -cne 'OK' })
            }
        }
    }

    if ($hilReasons.Count -eq 0 -and $hilBaselineOk) {
        Write-Host "[HIL-PREFLIGHT] AUTHORIZED: $($hilJobEnv['GITHUB_WORKFLOW_REF']) @ $($hilJobEnv['GITHUB_SHA']) by $($hilJobEnv['GITHUB_ACTOR'])" -ForegroundColor Green
        $hilExit = 0
    } else {
        Write-Host '[HIL-PREFLIGHT] DENIED:' -ForegroundColor Red
        foreach ($x in $hilReasons) { Write-Host "  - $x" -ForegroundColor Red }
    }
} catch {
    Write-Host "[HIL-PREFLIGHT] DENIED: preflight error: $($_.Exception.Message)" -ForegroundColor Red
    $hilExit = 1
}
exit $hilExit
