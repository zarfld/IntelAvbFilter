# test_hil_preflight.ps1
# TEST-HILPF-*: the trusted HIL preflight (tools/hil-trusted/Invoke-HilPreflight.ps1 + HilPreflight.Policy.ps1) must
# authorize ONLY a first-attempt workflow_dispatch of hil.yml on refs/heads/master in zarfld/IntelAvbFilter by an
# allow-listed actor, from a protected non-job location, on a non-elevated runner with a verified machine baseline -
# and must fail closed (exit 1) on every missing input, query error, unparseable file, exception and timeout.
#
# Offline.  The policy library is dot-sourced (functions only).  The entry script is executed in a child process
# exactly as the runner executes .ps1 hooks (`powershell -command ". '<path>'"`) with synthetic job environments that
# are denied before any machine query; machine checks are exercised with injected probes.  No device, service,
# adapter, event-log or runner state is read or changed by the denial paths.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$PfDir     = Join-Path $RepoRoot 'tools\hil-trusted'
$PfEntry   = Join-Path $PfDir 'Invoke-HilPreflight.ps1'
$PfLib     = Join-Path $PfDir 'HilPreflight.Policy.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILPF: trusted HIL preflight / runner job-started hook (offline)" -ForegroundColor Cyan

if (-not (Test-Path -LiteralPath $PfEntry) -or -not (Test-Path -LiteralPath $PfLib)) {
    tc_fail 'TEST-HILPF-000-reference-files-present' "missing: $PfEntry or $PfLib"
    Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
    exit 1
}
tc_pass 'TEST-HILPF-000-reference-files-present'
. $PfLib
$Policy = Get-HilPolicy

# ---------------------------------------------------------------------------------------------------------
# Static invariants of the entry script (it is dot-sourced by the runner)
# ---------------------------------------------------------------------------------------------------------
$entryAst = [System.Management.Automation.Language.Parser]::ParseFile($PfEntry, [ref]$null, [ref]$null)
$exits   = @($entryAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.ExitStatementAst] }, $true))
$returns = @($entryAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.ReturnStatementAst] }, $false))
$topReturns = @($returns | Where-Object { $p = $_.Parent; while ($p -and -not ($p -is [System.Management.Automation.Language.ScriptBlockAst])) { $p = $p.Parent }; $p -eq $entryAst })
tc_check 'TEST-HILPF-001-entry-has-no-parameters-no-top-level-return-and-one-exit' `
    (($null -eq $entryAst.ParamBlock) -and ($topReturns.Count -eq 0) -and ($exits.Count -eq 1) -and ($exits[0].Pipeline.Extent.Text -eq '$hilExit')) `
    ("param={0} topReturns={1} exits={2}" -f ($null -ne $entryAst.ParamBlock), $topReturns.Count, $exits.Count)
$entryText = [System.IO.File]::ReadAllText($PfEntry)
tc_check 'TEST-HILPF-002-entry-defaults-to-deny-and-catches-every-error' `
    (($entryText -match '\$hilExit\s*=\s*1\s*\r?\n\s*try\s*\{') -and ($entryText -match '\}\s*catch\s*\{[^}]*\$hilExit\s*=\s*1') -and (@([regex]::Matches($entryText, '\$hilExit\s*=\s*0')).Count -eq 1) -and
     ($entryText -notmatch 'Invoke-Expression|iex\s|Start-Process|&\s*\$env:')) 'entry script does not default to deny / contains dynamic execution'

# ---------------------------------------------------------------------------------------------------------
# Job authorization (pure)
# ---------------------------------------------------------------------------------------------------------
$sha = '0123456789abcdef0123456789abcdef01234567'
$ok = @{ GITHUB_ACTIONS = 'true'; GITHUB_REPOSITORY = 'zarfld/IntelAvbFilter'; GITHUB_EVENT_NAME = 'workflow_dispatch'; GITHUB_REF = 'refs/heads/master'
         GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/master'; GITHUB_SHA = $sha; GITHUB_WORKFLOW_SHA = $sha
         GITHUB_RUN_ATTEMPT = '1'; GITHUB_ACTOR = 'zarfld'; GITHUB_TRIGGERING_ACTOR = 'zarfld' }
function Merge-JobEnv([hashtable]$Base, [hashtable]$Delta) { $h = @{}; foreach ($k in $Base.Keys) { $h[$k] = $Base[$k] }; foreach ($k in $Delta.Keys) { if ($null -eq $Delta[$k]) { $h.Remove($k) } else { $h[$k] = $Delta[$k] } }; return $h }
$actors = @('zarfld')
$authCases = [ordered]@{
    'authorized first-attempt master dispatch' = @{ Env = $ok; Allow = $true }
    'fork pull_request'                = @{ Env = (Merge-JobEnv $ok @{ GITHUB_EVENT_NAME = 'pull_request'; GITHUB_REF = 'refs/pull/7/merge'; GITHUB_HEAD_REF = 'evil'; GITHUB_BASE_REF = 'master'; GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/pull/7/merge' }); Allow = $false }
    'pull_request_target'              = @{ Env = (Merge-JobEnv $ok @{ GITHUB_EVENT_NAME = 'pull_request_target'; GITHUB_HEAD_REF = 'evil'; GITHUB_BASE_REF = 'master' }); Allow = $false }
    'push to master'                   = @{ Env = (Merge-JobEnv $ok @{ GITHUB_EVENT_NAME = 'push' }); Allow = $false }
    'schedule'                         = @{ Env = (Merge-JobEnv $ok @{ GITHUB_EVENT_NAME = 'schedule' }); Allow = $false }
    'dispatch from feature branch'     = @{ Env = (Merge-JobEnv $ok @{ GITHUB_REF = 'refs/heads/feature/x'; GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/feature/x' }); Allow = $false }
    'branch Master (case variant)'     = @{ Env = (Merge-JobEnv $ok @{ GITHUB_REF = 'refs/heads/Master'; GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/Master' }); Allow = $false }
    'workflow path case variant'       = @{ Env = (Merge-JobEnv $ok @{ GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/HIL.yml@refs/heads/master' }); Allow = $false }
    'repository case variant'          = @{ Env = (Merge-JobEnv $ok @{ GITHUB_REPOSITORY = 'Zarfld/IntelAvbFilter' }); Allow = $false }
    'other workflow on master'         = @{ Env = (Merge-JobEnv $ok @{ GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/ci-standards-compliance.yml@refs/heads/master' }); Allow = $false }
    'reusable call from another workflow' = @{ Env = (Merge-JobEnv $ok @{ GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/caller.yml@refs/heads/master' }); Allow = $false }
    'foreign repository'               = @{ Env = (Merge-JobEnv $ok @{ GITHUB_REPOSITORY = 'untrusted-fork/IntelAvbFilter'; GITHUB_WORKFLOW_REF = 'untrusted-fork/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/master' }); Allow = $false }
    're-run attempt 2'                 = @{ Env = (Merge-JobEnv $ok @{ GITHUB_RUN_ATTEMPT = '2' }); Allow = $false }
    'workflow revision differs from code' = @{ Env = (Merge-JobEnv $ok @{ GITHUB_WORKFLOW_SHA = ('f' * 40) }); Allow = $false }
    'malformed GITHUB_SHA'             = @{ Env = (Merge-JobEnv $ok @{ GITHUB_SHA = 'master'; GITHUB_WORKFLOW_SHA = 'master' }); Allow = $false }
    'workflow ref missing'             = @{ Env = (Merge-JobEnv $ok @{ GITHUB_WORKFLOW_REF = $null }); Allow = $false }
    'run attempt missing'              = @{ Env = (Merge-JobEnv $ok @{ GITHUB_RUN_ATTEMPT = $null }); Allow = $false }
    'outside GitHub Actions'           = @{ Env = (Merge-JobEnv $ok @{ GITHUB_ACTIONS = $null }); Allow = $false }
    'actor not allow-listed'           = @{ Env = (Merge-JobEnv $ok @{ GITHUB_ACTOR = 'someone-else' }); Allow = $false }
    'triggering actor not allow-listed' = @{ Env = (Merge-JobEnv $ok @{ GITHUB_TRIGGERING_ACTOR = 'someone-else' }); Allow = $false }
    'actor case variant'               = @{ Env = (Merge-JobEnv $ok @{ GITHUB_ACTOR = 'Zarfld' }); Allow = $false }
}
$authBad = @()
foreach ($n in $authCases.Keys) {
    $d = Test-HilJobAuthorization -JobEnv $authCases[$n].Env -Policy $Policy -AllowedActors $actors
    if ($d.Allowed -ne $authCases[$n].Allow) { $authBad += ("{0}: allowed={1} expected={2} ({3})" -f $n, $d.Allowed, $authCases[$n].Allow, ($d.Reasons -join '; ')) }
}
tc_check 'TEST-HILPF-010-authorization-exact-match-matrix' ($authBad.Count -eq 0) ($authBad -join ' || ')
$dNoList = Test-HilJobAuthorization -JobEnv $ok -Policy $Policy -AllowedActors @()
tc_check 'TEST-HILPF-011-missing-actor-allow-list-denies' ((-not $dNoList.Allowed) -and (($dNoList.Reasons -join ';') -match 'allow-list is missing')) ($dNoList.Reasons -join '; ')

# ---------------------------------------------------------------------------------------------------------
# Location, ACL, runner context (pure, synthetic inputs)
# ---------------------------------------------------------------------------------------------------------
$jobEnvPaths = @{ GITHUB_WORKSPACE = 'C:\actions-runner\_work\IntelAvbFilter\IntelAvbFilter'; RUNNER_WORKSPACE = 'C:\actions-runner\_work\IntelAvbFilter'; RUNNER_TEMP = 'C:\actions-runner\_work\_temp'; RUNNER_TOOL_CACHE = 'C:\actions-runner\_work\_tool' }
$l1 = Test-HilScriptLocation -ScriptRoot 'C:\hil-trusted' -JobEnv $jobEnvPaths
$l2 = Test-HilScriptLocation -ScriptRoot 'C:\actions-runner\_work\IntelAvbFilter\IntelAvbFilter\tools\hil-trusted' -JobEnv $jobEnvPaths
$l3 = Test-HilScriptLocation -ScriptRoot 'C:\Users\User\actions-runner\hooks' -JobEnv @{}
$l4 = Test-HilScriptLocation -ScriptRoot '' -JobEnv @{}
tc_check 'TEST-HILPF-020-location-outside-workspace-and-runner-directory' ($l1.Ok -and -not $l2.Ok -and -not $l3.Ok -and -not $l4.Ok) ("trusted={0} workspace={1} runnerdir={2} unknown={3}" -f $l1.Ok, $l2.Ok, $l3.Ok, $l4.Ok)
$aGood = Test-HilAclRules -Label 'x' -Rules @(@{ Sid = 'S-1-5-18'; Rights = 'FullControl'; Type = 'Allow' }, @{ Sid = 'S-1-5-32-544'; Rights = 'FullControl'; Type = 'Allow' }, @{ Sid = 'S-1-5-32-545'; Rights = 'ReadAndExecute, Synchronize'; Type = 'Allow' })
$aUsers = Test-HilAclRules -Label 'x' -Rules @(@{ Sid = 'S-1-5-32-545'; Rights = 'Modify, Synchronize'; Type = 'Allow' })
$aRunner = Test-HilAclRules -Label 'x' -Rules @(@{ Sid = 'S-1-5-21-1-2-3-1001'; Rights = 'Write'; Type = 'Allow' })
$aGeneric = Test-HilAclRules -Label 'x' -Rules @(@{ Sid = 'S-1-5-11'; Rights = '-1610612736'; Type = 'Allow' })
$aDeny = Test-HilAclRules -Label 'x' -Rules @(@{ Sid = 'S-1-1-0'; Rights = 'Write'; Type = 'Deny' })
tc_check 'TEST-HILPF-021-acl-only-system-admins-trustedinstaller-may-write' ($aGood.Ok -and -not $aUsers.Ok -and -not $aRunner.Ok -and -not $aGeneric.Ok -and $aDeny.Ok) `
    ("good={0} users={1} runner={2} generic={3} deny-rule={4}" -f $aGood.Ok, $aUsers.Ok, $aRunner.Ok, $aGeneric.Ok, $aDeny.Ok)
$c1 = Test-HilRunnerContext -Policy $Policy -IsElevatedAdmin $true
$c2 = Test-HilRunnerContext -Policy $Policy -IsElevatedAdmin $false
tc_check 'TEST-HILPF-022-elevated-admin-runner-is-denied' ((-not $c1.Ok) -and $c2.Ok) ($c1.Reasons -join '; ')

# ---------------------------------------------------------------------------------------------------------
# Machine baseline with injected probes: every error / absence denies
# ---------------------------------------------------------------------------------------------------------
$Tmp = Join-Path $env:TEMP ("hilpf_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null
$goodHash = ('AB' * 32)
Set-Content -LiteralPath (Join-Path $Tmp 'authorized-driver.sha256') -Value "$goodHash  IntelAvbFilter.sys" -Encoding ASCII
Set-Content -LiteralPath (Join-Path $Tmp 'bugcheck-ack.txt') -Value '2026-10-20T13:40:00.0000000+02:00' -Encoding ASCII
function New-Probes([hashtable]$Over = @{}) {
    $p = @{
        Bugchecks   = { param($s) @{ Ok = $true; Count = 0; Latest = $null; Error = '' } }
        Service     = { param($n) @{ Ok = $true; Status = 'Running'; Error = '' } }
        FileHash    = { param($f) @{ Ok = $true; Hash = ('AB' * 32); Error = '' } }
        PnpDevices  = { param($d) @{ Ok = $true; Items = @('OK'); Error = '' } }
        NetAdapters = { param($d) @{ Ok = $true; Items = @('Up'); Error = '' } }
    }
    foreach ($k in $Over.Keys) { $p[$k] = $Over[$k] }
    return $p
}
$mbCases = [ordered]@{
    'all good'                    = @{ Probes = (New-Probes); Ok = $true }
    'bugcheck query error'        = @{ Probes = (New-Probes @{ Bugchecks = { param($s) @{ Ok = $false; Count = -1; Latest = $null; Error = 'Access denied' } } }); Ok = $false }
    'unacknowledged bugcheck'     = @{ Probes = (New-Probes @{ Bugchecks = { param($s) @{ Ok = $true; Count = 1; Latest = (Get-Date); Error = '' } } }); Ok = $false }
    'service unreadable'          = @{ Probes = (New-Probes @{ Service = { param($n) @{ Ok = $false; Status = ''; Error = 'not found' } } }); Ok = $false }
    'service stopped'             = @{ Probes = (New-Probes @{ Service = { param($n) @{ Ok = $true; Status = 'Stopped'; Error = '' } } }); Ok = $false }
    'driver hash mismatch'        = @{ Probes = (New-Probes @{ FileHash = { param($f) @{ Ok = $true; Hash = ('CD' * 32); Error = '' } } }); Ok = $false }
    'driver unreadable'           = @{ Probes = (New-Probes @{ FileHash = { param($f) @{ Ok = $false; Hash = ''; Error = 'locked' } } }); Ok = $false }
    'pnp query error'             = @{ Probes = (New-Probes @{ PnpDevices = { param($d) @{ Ok = $false; Items = @(); Error = 'WMI failure' } } }); Ok = $false }
    'adapter absent'              = @{ Probes = (New-Probes @{ PnpDevices = { param($d) if ($d -eq '125B') { @{ Ok = $true; Items = @(); Error = '' } } else { @{ Ok = $true; Items = @('OK'); Error = '' } } } }); Ok = $false }
    'netadapter query error'      = @{ Probes = (New-Probes @{ NetAdapters = { param($d) @{ Ok = $false; Items = @(); Error = 'CIM failure' } } }); Ok = $false }
    'netadapter missing'          = @{ Probes = (New-Probes @{ NetAdapters = { param($d) @{ Ok = $true; Items = @(); Error = '' } } }); Ok = $false }
    'link down'                   = @{ Probes = (New-Probes @{ NetAdapters = { param($d) @{ Ok = $true; Items = @('Disconnected'); Error = '' } } }); Ok = $false }
}
$mbBad = @()
foreach ($n in $mbCases.Keys) {
    $r = Test-HilMachineBaseline -Policy $Policy -Root $Tmp -Probes $mbCases[$n].Probes
    if ($r.Ok -ne $mbCases[$n].Ok) { $mbBad += ("{0}: ok={1} expected={2} ({3})" -f $n, $r.Ok, $mbCases[$n].Ok, ($r.Reasons -join '; ')) }
}
tc_check 'TEST-HILPF-030-machine-baseline-every-probe-error-or-absence-denies' ($mbBad.Count -eq 0) ($mbBad -join ' || ')
Set-Content -LiteralPath (Join-Path $Tmp 'bugcheck-ack.txt') -Value '20.10.2026 13:40' -Encoding ASCII
$rAck = Test-HilMachineBaseline -Policy $Policy -Root $Tmp -Probes (New-Probes)
Set-Content -LiteralPath (Join-Path $Tmp 'bugcheck-ack.txt') -Value '2026-10-20T13:40:00.0000000+02:00' -Encoding ASCII
Set-Content -LiteralPath (Join-Path $Tmp 'authorized-driver.sha256') -Value 'not-a-hash' -Encoding ASCII
$rAuth = Test-HilMachineBaseline -Policy $Policy -Root $Tmp -Probes (New-Probes)
Set-Content -LiteralPath (Join-Path $Tmp 'authorized-driver.sha256') -Value "$goodHash" -Encoding ASCII
Set-Content -LiteralPath (Join-Path $Tmp 'HALT') -Value 'stop' -Encoding ASCII
$rHalt = Test-HilMachineBaseline -Policy $Policy -Root $Tmp -Probes (New-Probes)
Remove-Item -LiteralPath (Join-Path $Tmp 'HALT') -Force
tc_check 'TEST-HILPF-031-unparseable-ack-invalid-authorized-hash-and-halt-deny' ((-not $rAck.Ok) -and (-not $rAuth.Ok) -and (-not $rHalt.Ok)) `
    ("ack={0} auth={1} halt={2}" -f ($rAck.Reasons -join ';'), ($rAuth.Reasons -join ';'), ($rHalt.Reasons -join ';'))

# ---------------------------------------------------------------------------------------------------------
# Timeout (runner hooks have none of their own)
# ---------------------------------------------------------------------------------------------------------
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$t1 = Invoke-HilWithTimeout -Seconds 2 -ScriptBlock { Start-Sleep -Seconds 30; 'late' }
$sw.Stop()
$t2 = Invoke-HilWithTimeout -Seconds 10 -ArgumentList @(21) -ScriptBlock { param($x) $x * 2 }
$t3 = Invoke-HilWithTimeout -Seconds 10 -ScriptBlock { Write-Error 'probe failed' }
tc_check 'TEST-HILPF-040-timeout-and-error-are-reported-not-ignored' `
    ($t1.TimedOut -and ($sw.Elapsed.TotalSeconds -lt 10) -and (-not $t2.TimedOut) -and (@($t2.Result)[0] -eq 42) -and ($t3.Error -match 'probe failed')) `
    ("timeout={0} after {1:N1}s; value={2}; error='{3}'" -f $t1.TimedOut, $sw.Elapsed.TotalSeconds, @($t2.Result)[0], $t3.Error)

# ---------------------------------------------------------------------------------------------------------
# Entry script executed exactly as the runner executes .ps1 hooks: powershell -command ". '<path>'"
# ---------------------------------------------------------------------------------------------------------
$Inst = Join-Path $Tmp 'installed'
New-Item -ItemType Directory -Path $Inst -Force | Out-Null
Copy-Item $PfEntry, $PfLib $Inst
function Invoke-AsHook([hashtable]$JobEnv) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "". '$(Join-Path $Inst 'Invoke-HilPreflight.ps1')'"""
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    foreach ($k in @($psi.EnvironmentVariables.Keys)) { if ($k -like 'GITHUB_*' -or $k -like 'RUNNER_*') { $psi.EnvironmentVariables.Remove($k) } }
    foreach ($k in $JobEnv.Keys) { $psi.EnvironmentVariables[$k] = [string]$JobEnv[$k] }
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(120000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT' } }
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result) }
}
$hFork = Invoke-AsHook (Merge-JobEnv $ok @{ GITHUB_EVENT_NAME = 'pull_request'; GITHUB_HEAD_REF = 'evil'; GITHUB_BASE_REF = 'master'; GITHUB_REF = 'refs/pull/7/merge' })
tc_check 'TEST-HILPF-050-dot-sourced-hook-denies-fork-pr-with-exit-1' (($hFork.Exit -eq 1) -and ($hFork.Out -match 'DENIED') -and ($hFork.Out -notmatch 'AUTHORIZED')) ("exit={0}; {1}" -f $hFork.Exit, ($hFork.Out -replace '\s+', ' '))
$hAuth = Invoke-AsHook $ok
tc_check 'TEST-HILPF-051-dot-sourced-hook-in-unprotected-folder-without-allow-list-denies-before-machine-queries' `
    (($hAuth.Exit -eq 1) -and ($hAuth.Out -match 'grants') -and ($hAuth.Out -match 'allow-list is missing') -and ($hAuth.Out -notmatch 'machine baseline|bugcheck|PnP|service ')) `
    ("exit={0}; {1}" -f $hAuth.Exit, ($hAuth.Out -replace '\s+', ' '))
Remove-Item -LiteralPath (Join-Path $Inst 'HilPreflight.Policy.ps1') -Force
$hNoLib = Invoke-AsHook $ok
tc_check 'TEST-HILPF-052-dot-sourced-hook-with-missing-policy-library-denies' (($hNoLib.Exit -eq 1) -and ($hNoLib.Out -match 'policy library missing')) ("exit={0}; {1}" -f $hNoLib.Exit, ($hNoLib.Out -replace '\s+', ' '))
Set-Content -LiteralPath (Join-Path $Inst 'HilPreflight.Policy.ps1') -Value 'function Get-HilPolicy { throw "corrupt policy" }' -Encoding ASCII
$hCorrupt = Invoke-AsHook $ok
tc_check 'TEST-HILPF-053-dot-sourced-hook-with-throwing-policy-denies' (($hCorrupt.Exit -eq 1) -and ($hCorrupt.Out -match 'preflight error')) ("exit={0}; {1}" -f $hCorrupt.Exit, ($hCorrupt.Out -replace '\s+', ' '))

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILPF: trusted preflight is not fail-closed" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILPF: trusted preflight authorizes only the operator dispatch and fails closed" -ForegroundColor Green
exit 0
