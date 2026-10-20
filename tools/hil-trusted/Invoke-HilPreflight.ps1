# Invoke-HilPreflight.ps1 -- trusted HIL preflight (REFERENCE COPY)
#
# This file is NOT executed from the repository checkout.  The operator reviews it and installs a copy to the
# runner-local, non-workspace path C:\hil-trusted\Invoke-HilPreflight.ps1 (writable by administrators only).
# The installed copy is used twice:
#   1. as the runner's job-started hook (actions-runner\.env: ACTIONS_RUNNER_HOOK_JOB_STARTED=<that path>),
#      so EVERY job sent to this runner is checked before any step - including jobs from modified workflow
#      files that omit the preflight step;
#   2. as the first step of .github/workflows/hil.yml (-Mode Step), defence in depth.
# Exit 0 = authorized; exit 1 = denied (the job fails before checkout / repository code).
#
# Policy (job authorization, environment only):
#   repository zarfld/IntelAvbFilter, event workflow_dispatch, ref refs/heads/master, workflow file
#   .github/workflows/hil.yml on refs/heads/master, no pull-request head/base ref; optional actor allow-list.
# Machine baseline (read-only; skipped with -PolicyOnly):
#   no operator HALT file; no unacknowledged bugcheck; IntelAvbFilter service running; installed driver SHA256
#   equals the operator-authorized value; the expected Intel adapters present, Up and PnP-OK.
# Nothing here changes the machine.  #328 trust boundary: investigations/hil-runner-trust-boundary-20261020.md

param(
    [ValidateSet('Hook', 'Step')]
    [string]$Mode = 'Hook',
    [string]$TrustedRoot = 'C:\hil-trusted',
    [switch]$PolicyOnly
)

$ErrorActionPreference = 'Stop'

$HilPolicy = @{
    Repository   = 'zarfld/IntelAvbFilter'
    EventName    = 'workflow_dispatch'
    Ref          = 'refs/heads/master'
    WorkflowRef  = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/master'
    AdapterDids  = @('125B', '1533', '15BB')
    DriverFile   = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
}

# Pure decision on the job environment: @{ Allowed = bool; Reasons = string[] }
function Test-HilJobAuthorization {
    param([hashtable]$JobEnv, [hashtable]$Policy, [string[]]$AllowedActors = @())
    $r = @()
    function V([string]$n) { if ($JobEnv.ContainsKey($n)) { return [string]$JobEnv[$n] } return '' }
    if ((V 'GITHUB_ACTIONS') -ne 'true')                 { $r += 'not running under GitHub Actions' }
    if ((V 'GITHUB_REPOSITORY') -ne $Policy.Repository)  { $r += "repository '$(V 'GITHUB_REPOSITORY')' is not '$($Policy.Repository)'" }
    if ((V 'GITHUB_EVENT_NAME') -ne $Policy.EventName)   { $r += "event '$(V 'GITHUB_EVENT_NAME')' is not '$($Policy.EventName)'" }
    if ((V 'GITHUB_REF') -ne $Policy.Ref)                { $r += "ref '$(V 'GITHUB_REF')' is not '$($Policy.Ref)'" }
    if ((V 'GITHUB_WORKFLOW_REF') -ne $Policy.WorkflowRef) { $r += "workflow '$(V 'GITHUB_WORKFLOW_REF')' is not '$($Policy.WorkflowRef)'" }
    if ((V 'GITHUB_HEAD_REF') -or (V 'GITHUB_BASE_REF')) { $r += 'pull-request context (GITHUB_HEAD_REF/GITHUB_BASE_REF set)' }
    if ($AllowedActors.Count -gt 0 -and $AllowedActors -notcontains (V 'GITHUB_ACTOR')) { $r += "actor '$(V 'GITHUB_ACTOR')' is not on the operator allow-list" }
    return @{ Allowed = ($r.Count -eq 0); Reasons = $r }
}

# Read-only machine baseline: @{ Ok = bool; Reasons = string[] }
function Test-HilMachineBaseline {
    param([hashtable]$Policy, [string]$Root)
    $r = @()
    if (Test-Path -LiteralPath (Join-Path $Root 'HALT')) { $r += "operator HALT file present: $(Join-Path $Root 'HALT')" }
    $ackFile = Join-Path $Root 'bugcheck-ack.txt'
    $ack = if (Test-Path -LiteralPath $ackFile) { [datetime]::Parse((Get-Content -LiteralPath $ackFile -Raw).Trim()) } else { [datetime]::MinValue }
    $bc = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 1001; ProviderName = 'Microsoft-Windows-WER-SystemErrorReporting'; StartTime = $ack.AddSeconds(1) } -ErrorAction SilentlyContinue)
    if ($bc.Count -gt 0) { $r += ("{0} unacknowledged bugcheck(s) since {1:o} (latest {2:o}); record acknowledgement in {3}" -f $bc.Count, $ack, $bc[0].TimeCreated, $ackFile) }
    $svc = Get-Service -Name 'IntelAvbFilter' -ErrorAction SilentlyContinue
    if ($null -eq $svc -or $svc.Status -ne 'Running') { $r += "IntelAvbFilter service is not running ($(if ($svc) { $svc.Status } else { 'absent' }))" }
    $authFile = Join-Path $Root 'authorized-driver.sha256'
    if (-not (Test-Path -LiteralPath $authFile)) { $r += "no operator-authorized driver identity: $authFile" }
    elseif (-not (Test-Path -LiteralPath $Policy.DriverFile)) { $r += "installed driver not found: $($Policy.DriverFile)" }
    else {
        $want = ((Get-Content -LiteralPath $authFile -Raw).Trim() -split '\s+')[0].ToUpperInvariant()
        $have = (Get-FileHash -LiteralPath $Policy.DriverFile -Algorithm SHA256).Hash
        if ($have -ne $want) { $r += "installed driver SHA256 $have is not the authorized $want" }
    }
    foreach ($did in $Policy.AdapterDids) {
        $dev = @(Get-PnpDevice -Class Net -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match "VEN_8086&DEV_$did" })
        if ($dev.Count -eq 0) { $r += "adapter DEV_$did not present"; continue }
        foreach ($d in $dev) { if ($d.Status -ne 'OK') { $r += "adapter DEV_$did PnP status $($d.Status)" } }
        $na = @(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.PnPDeviceID -match "VEN_8086&DEV_$did" })
        foreach ($n in $na) { if ($n.Status -ne 'Up') { $r += "adapter DEV_$did ($($n.Name)) is $($n.Status)" } }
    }
    return @{ Ok = ($r.Count -eq 0); Reasons = $r }
}

if ($MyInvocation.InvocationName -eq '.') { return }   # dot-sourced (offline tests): functions only

$jobEnv = @{}
foreach ($n in 'GITHUB_ACTIONS', 'GITHUB_REPOSITORY', 'GITHUB_EVENT_NAME', 'GITHUB_REF', 'GITHUB_WORKFLOW_REF',
               'GITHUB_HEAD_REF', 'GITHUB_BASE_REF', 'GITHUB_ACTOR') {
    $v = [Environment]::GetEnvironmentVariable($n); if ($null -ne $v) { $jobEnv[$n] = $v }
}
$actorsFile = Join-Path $TrustedRoot 'allowed-actors.txt'
$actors = if (Test-Path -LiteralPath $actorsFile) { @(Get-Content -LiteralPath $actorsFile | ForEach-Object { $_.Trim() } | Where-Object { $_ }) } else { @() }

$auth = Test-HilJobAuthorization -JobEnv $jobEnv -Policy $HilPolicy -AllowedActors $actors
$reasons = @($auth.Reasons)
if (-not $PolicyOnly) { $reasons += @((Test-HilMachineBaseline -Policy $HilPolicy -Root $TrustedRoot).Reasons) }

if ($reasons.Count -gt 0) {
    Write-Host "[HIL-PREFLIGHT] DENIED ($Mode):" -ForegroundColor Red
    foreach ($x in $reasons) { Write-Host "  - $x" -ForegroundColor Red }
    exit 1
}
Write-Host "[HIL-PREFLIGHT] AUTHORIZED ($Mode): $($jobEnv['GITHUB_WORKFLOW_REF']) by $($jobEnv['GITHUB_ACTOR'])" -ForegroundColor Green
exit 0
