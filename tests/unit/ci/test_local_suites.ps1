# test_local_suites.ps1
# TEST-LOCALSUITES-*: operator-driven local pre-commit HIL orchestration (tools/archive/deprecated/LocalSuites.ps1)
# and its DbgView reproduction path (tools/test/Run-Tests-Elevated.ps1 -CaptureDbgView -Suite).
#
# Contract under test (offline; nothing is built, installed or executed against hardware):
#   - suite order HardwareUnit, HardwareIntegration, HardwareMutation, HardwareMandatory; results come from the
#     runner's results documents (never console Format-Table text); a missing or inconsistent document stops the run;
#   - failure inventory keeps FAILED / FAILED(TIMEOUT) / INFRA_FAILURE / INCOMPLETE / INCONCLUSIVE / CLEANUP_FAILED as
#     failures, BLOCKED(*) as not executed, SKIPPED / COVERAGE-GAP as non-failures; testcase + adapter + configuration survive;
#   - TIMEOUT / INFRA / CLEANUP / state halt stop the run;
#   - reproduction: operator-confirmed, this configuration only (its driver is installed), executed ReadOnly failures
#     without a target, deduplicated across suites;
#   - Run-Tests-Elevated -Suite routes through Run-Tests-CI.ps1 (all gates); a DbgView capture that cannot be confirmed
#     (process alive + log created) prevents the test; a capture is never claimed without a log.
# Functions are extracted by AST from the real scripts; the DbgView checks use a harmless sleeping powershell.exe
# process and temporary files - DebugView itself is never started.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$LocalPath = Join-Path $RepoRoot 'tools\archive\deprecated\LocalSuites.ps1'
$ElevPath  = Join-Path $RepoRoot 'tools\test\Run-Tests-Elevated.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-LOCALSUITES: local pre-commit HIL orchestration" -ForegroundColor Cyan

$Tmp = Join-Path $env:TEMP ("localsuites_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

function Import-Functions([string]$Path, [string[]]$Names) {
    $a = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$null)
    foreach ($n in $Names) {
        $f = $a.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $n }, $true)
        if ($null -eq $f) { throw "function $n not found in $Path" }
        . ([scriptblock]::Create($f.Extent.Text))
        Set-Item -Path "function:script:$n" -Value (Get-Item "function:$n").ScriptBlock
    }
}
$missing = ''
try {
    . (Join-Path $RepoRoot 'tools\lib\NativeExit.ps1')
    Import-Functions $LocalPath @('Read-AvbSuiteResults', 'Get-AvbFailureInventory', 'Get-AvbStopReason', 'Get-AvbReproductionQueue', 'Format-AvbStepResult', 'Get-AvbSafetyHalt', 'Get-AvbCleanupIncidentSummary',
                                  'Read-AvbTimedChoice', 'New-AvbUntrustedSuiteRow')
    Import-Functions $ElevPath @('ConvertTo-AvbPsLiteral', 'New-AvbElevatedCommand', 'Wait-AvbDbgViewStarted', 'Get-AvbDbgViewCaptureResult')
} catch { $missing = $_.Exception.Message }
tc_check 'TEST-LOCALSUITES-000-decision-functions-present' ($missing -eq '') $missing
if ($missing) { Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue; Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total); exit 1 }

# Synthetic results document in the runner's schema (avb-runner-results/1; produced for real by
# tests/unit/ci/test_runner_results_json.ps1 against the sandboxed runner).
function New-Rec([string]$Name, [string]$Verdict, [bool]$Executed, [string]$Class = 'ReadOnly', $Exit = $null, [object[]]$Tcs = @(), [string]$Target = $null, [bool]$Check = $false) {
    return [ordered]@{ name = "$Name.exe"; entry = $Name; suite = 'X'; configuration = 'X'; verdict = $Verdict; status = $Verdict; executed = $Executed; check = $Check
                       exitCode = $Exit; artifact = "C:\b\$Name.exe"; artifactSha256 = ('A' * 64); arguments = ''; safetyClass = $Class; targetDid = $Target
                       reason = "reason of $Name"; logPath = "C:\l\$Name.log"; testcases = @($Tcs) }
}
function New-Tc([string]$Id, [string]$Verdict, $Idx = $null, $Did = $null) { return [ordered]@{ Id = $Id; Verdict = $Verdict; AdapterIndex = $Idx; AdapterDid = $Did; AdapterLabel = $null; Detail = '' } }
function Write-Doc([string]$Path, [string]$Suite, [string]$Cfg, [int]$Exit, [object[]]$Records, [string]$Halt = '') {
    $d = [ordered]@{ schema = 'avb-runner-results/1'; suite = $Suite; configuration = $Cfg; exitCode = $Exit; overall = 'FAILED'; stateHalt = $Halt
                     executedCount = @($Records | Where-Object { $_.executed }).Count; records = @($Records) }
    ($d | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $Path -Encoding UTF8
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

# --- Results document acceptance ---------------------------------------------------------------------------
$pOk = Join-Path $Tmp 'ok.json'
$null = Write-Doc $pOk 'HardwareUnit' 'Debug' 1 @((New-Rec 't_a' 'FAILED' $true -Exit 3))
$a1 = Read-AvbSuiteResults -Path $pOk -Suite 'HardwareUnit' -Configuration 'Debug' -ObservedExit 1
$a2 = Read-AvbSuiteResults -Path (Join-Path $Tmp 'absent.json') -Suite 'HardwareUnit' -Configuration 'Debug' -ObservedExit $null
$a3 = Read-AvbSuiteResults -Path $pOk -Suite 'HardwareUnit' -Configuration 'Debug' -ObservedExit 0
$a4 = Read-AvbSuiteResults -Path $pOk -Suite 'HardwareUnit' -Configuration 'Release' -ObservedExit 1
$a5 = Read-AvbSuiteResults -Path $pOk -Suite 'HardwareUnit' -Configuration 'Debug' -ObservedExit $null
tc_check 'TEST-LOCALSUITES-001-missing-or-inconsistent-results-document-is-never-accepted' `
    ($a1.Ok -and -not $a2.Ok -and ($a2.Error -match 'no results document.*no exit code observed') -and -not $a3.Ok -and ($a3.Error -match 'disagrees') -and
     -not $a4.Ok -and ($a4.Error -match 'Release') -and -not $a5.Ok) ("ok={0} absent={1}:{2} exitMismatch={3} cfg={4} noExit={5}" -f $a1.Ok, $a2.Ok, $a2.Error, $a3.Ok, $a4.Ok, $a5.Ok)

# --- Inventory categories (addendum cases 10, 11, 12, 15) ---------------------------------------------------
$pInv = Join-Path $Tmp 'inv.json'
$doc = Write-Doc $pInv 'HardwareIntegration' 'Release' 1 @(
    (New-Rec 't_fail' 'FAILED' $true -Exit 3 -Tcs @((New-Tc 'TC-A-001' 'FAIL' 0 '0x125B'), (New-Tc 'TC-A-002' 'PASS' 1 '0x1533'), (New-Tc 'TC-A-003' 'FAIL' 1 $null))),
    (New-Rec 't_time' 'FAILED(TIMEOUT)' $true), (New-Rec 't_infra' 'INFRA_FAILURE' $false), (New-Rec 't_incomplete' 'INCOMPLETE' $true -Exit 0),
    (New-Rec 't_inconcl' 'INCONCLUSIVE' $true -Exit 0), (New-Rec 't_skip' 'SKIPPED' $true -Exit 2), (New-Rec 't_gap' 'COVERAGE-GAP' $false),
    (New-Rec 't_blk' 'BLOCKED(SAFETY)' $false -Class 'Mutating'), (New-Rec 't_prov' 'BLOCKED(PROVENANCE)' $false), (New-Rec 't_new' 'SOMETHING-NEW' $true),
    (New-Rec 't_pass' 'PASSED' $true -Exit 0))
$inv = Get-AvbFailureInventory -Doc $doc -Configuration 'Release' -Suite 'HardwareIntegration'
function Get-InvCategory([string]$n) { return @($inv | Where-Object { $_.Name -eq "$n.exe" })[0].Category }
tc_check 'TEST-LOCALSUITES-002-failed-timeout-infra-incomplete-inconclusive-and-unknown-are-failures' `
    (@(@('t_fail', 't_time', 't_infra', 't_incomplete', 't_inconcl', 't_new') | Where-Object { (Get-InvCategory $_) -ne 'Failure' }).Count -eq 0) `
    (($inv | ForEach-Object { "$($_.Name)=$($_.Category)" }) -join ' ')
# Requirement changed 2026-10-21 (P0 full HIL coverage): a coverage gap is a required test NOT executed - its own
# nonpassing category (never NotFailure, never conflated with an executed failure or a safety block).
tc_check 'TEST-LOCALSUITES-003-skipped-stays-nonfailure-coverage-gap-is-required-not-executed' (((Get-InvCategory 't_skip') -eq 'NotFailure') -and ((Get-InvCategory 't_gap') -eq 'NotExecuted') -and ((Get-InvCategory 't_pass') -eq 'NotFailure')) `
    ("skip={0} gap={1} pass={2}" -f (Get-InvCategory 't_skip'), (Get-InvCategory 't_gap'), (Get-InvCategory 't_pass'))
$rowF = @($inv | Where-Object { $_.Name -eq 't_fail.exe' })[0]
tc_check 'TEST-LOCALSUITES-004-testcase-adapter-configuration-and-exit-code-survive' `
    (($rowF.Testcases -eq 'TC-A-001@0x125B TC-A-003/adapter:1') -and ($rowF.Configuration -eq 'Release') -and ($rowF.Suite -eq 'HardwareIntegration') -and ($rowF.ExitCode -eq 3)) `
    ("tcs='{0}' cfg={1} exit={2}" -f $rowF.Testcases, $rowF.Configuration, $rowF.ExitCode)
$blocked = @($inv | Where-Object { $_.Category -eq 'Blocked' })
tc_check 'TEST-LOCALSUITES-005-blocked-items-are-reported-as-not-executed' `
    (($blocked.Count -eq 2) -and (@($blocked | Where-Object { $_.Executed }).Count -eq 0) -and (@($inv | Where-Object { $_.Executed }).Count -eq 7)) `
    ("blocked={0} executed={1}" -f $blocked.Count, @($inv | Where-Object { $_.Executed }).Count)
# An EXECUTED test whose testcases were BLOCKED without a functional FAIL (runner verdict BLOCKED(TESTCASE), exit 3
# confirmed by the lines; test_ptp_freq_complete in LocalSuites_20261029_005219 was listed as a failure): its own
# nonpassing category - neither an executed functional failure nor a not-executed block; the failing testcases survive.
$docBt = Write-Doc (Join-Path $Tmp 'inv-blktc.json') 'HardwareUnit' 'Debug' 1 @(
    (New-Rec 't_blktc' 'BLOCKED(TESTCASE)' $true -Class 'Mutating:Target' -Exit 3 -Target '0x125B' -Tcs @((New-Tc 'UT-FREQ-001' 'PASS'), (New-Tc 'IT-FREQ-002' 'BLOCKED'), (New-Tc 'VV-FREQ-001' 'INCONCLUSIVE'))),
    (New-Rec 't_fail2' 'FAILED' $true -Exit 1 -Tcs @((New-Tc 'TC-B-001' 'FAIL'))))
$invBt = Get-AvbFailureInventory -Doc $docBt -Configuration 'Debug' -Suite 'HardwareUnit'
$rowBt = @($invBt | Where-Object { $_.Name -eq 't_blktc.exe' })[0]
tc_check 'TEST-LOCALSUITES-023-executed-blocked-testcases-are-their-own-nonpassing-category-not-a-failure' `
    (($null -ne $rowBt) -and ($rowBt.Category -eq 'BlockedTestcases') -and $rowBt.Executed -and ($rowBt.Testcases -eq 'IT-FREQ-002 VV-FREQ-001') -and
     (@($invBt | Where-Object { $_.Category -eq 'Failure' }).Count -eq 1) -and (@($invBt | Where-Object { $_.Category -eq 'NotFailure' }).Count -eq 0)) `
    (($invBt | ForEach-Object { "$($_.Name)=$($_.Category) tcs='$($_.Testcases)'" }) -join ' | ')

# 017 (Decision B, Q 2026-10-22): two observations of ONE cleanup incident stay two visible failure rows, but the
# inventory carries the incident references and counts unique incidents separately from observations.
$docInc = Write-Doc (Join-Path $Tmp 'incidents.json') 'HardwareUnit' 'Debug' 1 @(
    (New-Rec 't_x[STATE]' 'CLEANUP_FAILED' $false -Check $true), (New-Rec 'HIL-SESSION[end]' 'CLEANUP_FAILED' $false -Check $true), (New-Rec 't_y[STATE]' 'CLEANUP_FAILED' $false -Check $true))
$j = Get-Content (Join-Path $Tmp 'incidents.json') -Raw | ConvertFrom-Json
$j.records[0] | Add-Member -NotePropertyName incidents -NotePropertyValue @([pscustomobject]@{ incidentId = 'inc-1'; role = 'opened'; key = 'k1' })
$j.records[1] | Add-Member -NotePropertyName incidents -NotePropertyValue @([pscustomobject]@{ incidentId = 'inc-1'; role = 'confirmed'; key = 'k1' })
$j.records[2] | Add-Member -NotePropertyName incidents -NotePropertyValue @([pscustomobject]@{ incidentId = 'inc-2'; role = 'opened'; key = 'k2' })
$invInc = Get-AvbFailureInventory -Doc $j -Configuration 'Debug' -Suite 'HardwareUnit'
$sumInc = Get-AvbCleanupIncidentSummary -Inventory $invInc
tc_check 'TEST-LOCALSUITES-017-cleanup-observations-stay-visible-incidents-counted-once' `
    (@($invInc | Where-Object { $_.Category -eq 'Failure' }).Count -eq 3 -and (@($invInc)[1].Incidents -eq 'inc-1:confirmed') -and $sumInc.Incidents -eq 2 -and $sumInc.Observations -eq 3) `
    ("rows={0} row2='{1}' incidents={2} observations={3}" -f @($invInc).Count, @($invInc)[1].Incidents, $sumInc.Incidents, $sumInc.Observations)

# --- Stop rules ---------------------------------------------------------------------------------------------
$stopT = Get-AvbStopReason $doc
$docClean = Write-Doc (Join-Path $Tmp 'clean.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_fail' 'FAILED' $true -Exit 3), (New-Rec 't_blk' 'BLOCKED(SAFETY)' $false))
$docHalt  = Write-Doc (Join-Path $Tmp 'halt.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_fail' 'FAILED' $true -Exit 3)) -Halt 'CLEANUP_FAILED after x'
$docClean2 = Write-Doc (Join-Path $Tmp 'cleanup.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_pass' 'PASSED' $true -Exit 0), (New-Rec 't_pass[STATE]' 'CLEANUP_FAILED' $false -Check $true))
# Contract changed 2026-10-21 (Q): a safety halt (TIMEOUT / CLEANUP_FAILED / INCONCLUSIVE_RESTORATION / stateHalt) no
# longer stops the run - later suites still execute their eligible ReadOnly tests while every Init/Mutating test stays
# BLOCKED (the halt is carried by Run-Tests-CI -PriorResultsDir <ReportDir>, same as hil.yml) and the run stays
# nonpassing.  Only global driver/device infrastructure loss (INFRA_FAILURE) stops it.
$docUnv = Write-Doc (Join-Path $Tmp 'unverified.json') 'HardwareMutation' 'Debug' 1 @((New-Rec 't_tgt' 'PASSED' $true -Exit 0), (New-Rec 't_tgt[STATE-UNVERIFIED]' 'INCONCLUSIVE_RESTORATION' $false -Check $true)) -Halt 't_tgt: restoration not verified'
$halts = [ordered]@{ timeout = (Get-AvbSafetyHalt $doc); halt = (Get-AvbSafetyHalt $docHalt); cleanup = (Get-AvbSafetyHalt $docClean2); unverified = (Get-AvbSafetyHalt $docUnv); clean = (Get-AvbSafetyHalt $docClean) }
tc_check 'TEST-LOCALSUITES-006-only-infrastructure-loss-stops-the-run-safety-halts-are-carried-not-stopping' `
    (($stopT -match 'INFRA_FAILURE') -and ($stopT -notmatch 'TIMEOUT') -and ((Get-AvbStopReason $docClean) -eq '') -and ((Get-AvbStopReason $docHalt) -eq '') -and
     ((Get-AvbStopReason $docClean2) -eq '') -and ((Get-AvbStopReason $docUnv) -eq '') -and
     # Q 2026-10-28: a CLEANUP_FAILED row alone (older runner) is a cleanup observation, not a halt (was: carried halt).
     ($halts.timeout -match 'FAILED\(TIMEOUT\)') -and ($halts.halt -match 'state-integrity halt') -and ($halts.cleanup -eq '') -and
     ($halts.unverified -match 'INCONCLUSIVE_RESTORATION|restoration not verified') -and ($halts.clean -eq '') -and
     ([System.IO.File]::ReadAllText($LocalPath) -match '-PriorResultsDir \$ReportDir')) `
    ("stop: infra/timeout='{0}' halt='{1}' cleanup='{2}' unverified='{3}'; carried: {4}" -f $stopT, (Get-AvbStopReason $docHalt), (Get-AvbStopReason $docClean2), (Get-AvbStopReason $docUnv), (($halts.GetEnumerator() | ForEach-Object { "$($_.Key)='$($_.Value)'" }) -join ' '))

# 022 (Q 2026-10-28, binding): an INCONCLUSIVE_RESTORATION record without a state halt - a test's own restoration verdict,
# or an older runner's restoration row - is missing evidence, not loss of control: no safety halt (same rule as
# Run-Tests-CI Get-AvbPriorHalt).  The record itself stays a non-passing inventory row.
$docUnvOnly = Write-Doc (Join-Path $Tmp 'unverified_only.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_rst' 'INCONCLUSIVE_RESTORATION' $true -Exit 1))
$hUnvOnly = Get-AvbSafetyHalt $docUnvOnly
$invUnvOnly = @(Get-AvbFailureInventory -Doc $docUnvOnly -Configuration 'Debug' -Suite 'HardwareUnit')
tc_check 'TEST-LOCALSUITES-022-inconclusive-restoration-record-alone-is-no-safety-halt-but-stays-nonpassing' `
    ($hUnvOnly -eq '' -and @($invUnvOnly | Where-Object { $_.Category -ne 'NotFailure' }).Count -eq 1) `
    ("halt='{0}' inventory categories=[{1}]" -f $hUnvOnly, (@($invUnvOnly | ForEach-Object { $_.Category }) -join ','))

# D2, revised (Q 2026-10-28): the same rule as Run-Tests-CI Get-AvbPriorHalt - a FAILED(TIMEOUT) whose process tree was
# terminated (timeoutDisposition TERMINATED_STATE_VERIFIED or TERMINATED, any class) is no safety halt; without a
# disposition (older runner, termination not evidenced) or with HALT (tree did not terminate) it still is.
function New-TimeoutRec([string]$Class, [string]$Disp) { $r = New-Rec 't_to' 'FAILED(TIMEOUT)' $true -Class $Class; if ($Disp) { $r.timeoutDisposition = $Disp }; return $r }
$toV = Get-AvbSafetyHalt (Write-Doc (Join-Path $Tmp 'to_v.json') 'HardwareUnit' 'Debug' 1 @(New-TimeoutRec 'ReadOnly' 'TERMINATED_STATE_VERIFIED'))
$toH = Get-AvbSafetyHalt (Write-Doc (Join-Path $Tmp 'to_h.json') 'HardwareUnit' 'Debug' 1 @(New-TimeoutRec 'ReadOnly' 'HALT'))
$toL = Get-AvbSafetyHalt (Write-Doc (Join-Path $Tmp 'to_l.json') 'HardwareUnit' 'Debug' 1 @(New-TimeoutRec 'ReadOnly' ''))
$toI = Get-AvbSafetyHalt (Write-Doc (Join-Path $Tmp 'to_i.json') 'HardwareUnit' 'Debug' 1 @(New-TimeoutRec 'Init' 'TERMINATED_STATE_VERIFIED'))
$toT = Get-AvbSafetyHalt (Write-Doc (Join-Path $Tmp 'to_t.json') 'HardwareUnit' 'Debug' 1 @(New-TimeoutRec 'Init' 'TERMINATED'))
$toIH = Get-AvbSafetyHalt (Write-Doc (Join-Path $Tmp 'to_ih.json') 'HardwareUnit' 'Debug' 1 @(New-TimeoutRec 'Init' 'HALT'))
tc_check 'TEST-LOCALSUITES-018-only-a-timeout-without-evidenced-termination-is-a-safety-halt' `
    ($toV -eq '' -and $toI -eq '' -and $toT -eq '' -and $toH -match 'FAILED\(TIMEOUT\)' -and $toL -match 'FAILED\(TIMEOUT\)' -and $toIH -match 'FAILED\(TIMEOUT\)') `
    ("verified='{0}' halt='{1}' legacy='{2}' init-verified='{3}' init-terminated='{4}' init-halt='{5}'" -f $toV, $toH, $toL, $toI, $toT, $toIH)

# --- Reproduction queue (addendum case 13 + Debug/Release separation) ----------------------------------------
$dU = Write-Doc (Join-Path $Tmp 'u.json') 'HardwareUnit' 'Debug' 1 @(
    (New-Rec 't_ro' 'FAILED' $true -Exit 3), (New-Rec 't_mut' 'FAILED' $true -Class 'Mutating:Target' -Exit 3 -Target '0x125B'),
    (New-Rec 't_init' 'FAILED' $true -Class 'Init' -Exit 3), (New-Rec 't_time' 'FAILED(TIMEOUT)' $true), (New-Rec 't_blk' 'BLOCKED(SAFETY)' $false),
    (New-Rec 't_incomplete' 'INCOMPLETE' $true -Exit 0), (New-Rec 't_ro[STATE]' 'CLEANUP_FAILED' $false -Check $true))
$dM = Write-Doc (Join-Path $Tmp 'm.json') 'HardwareMandatory' 'Debug' 1 @((New-Rec 't_ro' 'FAILED' $true -Exit 3))
$dR = Write-Doc (Join-Path $Tmp 'r.json') 'HardwareUnit' 'Release' 1 @((New-Rec 't_rel' 'FAILED' $true -Exit 3))
# Accumulated exactly as LocalSuites.ps1 does ($inv = ...; $x += $inv); @(f) would nest the unary-comma array.
$all = @()
$i1 = Get-AvbFailureInventory -Doc $dU -Configuration 'Debug' -Suite 'HardwareUnit'; $all += $i1
$i2 = Get-AvbFailureInventory -Doc $dM -Configuration 'Debug' -Suite 'HardwareMandatory'; $all += $i2
$i3 = Get-AvbFailureInventory -Doc $dR -Configuration 'Release' -Suite 'HardwareUnit'; $all += $i3
$qD = @(Get-AvbReproductionQueue -Inventory $all -Configuration 'Debug'); $qR = @(Get-AvbReproductionQueue -Inventory $all -Configuration 'Release')
tc_check 'TEST-LOCALSUITES-007-reproduction-only-readonly-executed-failures-of-this-configuration-without-duplicates' `
    (((($qD | ForEach-Object { $_.Entry }) -join ',') -eq 't_ro,t_incomplete') -and ((($qR | ForEach-Object { $_.Entry }) -join ',') -eq 't_rel') -and
     (@($qD | Where-Object { $_.Configuration -ne 'Debug' }).Count -eq 0)) `
    ("debug=[{0}] release=[{1}]" -f (($qD | ForEach-Object { "$($_.Entry)/$($_.Suite)" }) -join ','), (($qR | ForEach-Object { $_.Entry }) -join ','))

# --- Static: LocalSuites orchestration ---------------------------------------------------------------------
$ls = [System.IO.File]::ReadAllText($LocalPath)
$lsAst = [System.Management.Automation.Language.Parser]::ParseFile($LocalPath, [ref]$null, [ref]$null)
$suitesAssign = $lsAst.Find({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq '$Suites' }, $true)
$order = if ($suitesAssign) { @($suitesAssign.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value }) -join ',' } else { '' }
# Q 2026-11-03: the only hardware suites, in this order (was: ...,HardwareMutation,HardwareMandatory last).
tc_check 'TEST-LOCALSUITES-008-suite-order-the-two-hardware-suites' ($order -eq 'HardwareUnit,HardwareIntegration') "order: $order"
tc_check 'TEST-LOCALSUITES-009-results-come-from-results-json-not-console-table-parsing' `
    (($ls -match '-ResultsJson \$json') -and ($ls -notmatch 'CI Test Summary --') -and ($ls -notmatch '\(\?<Status>FAILED')) 'LocalSuites still parses console output or does not request -ResultsJson'
# 010 (intent Q 2026-10-22, LocalSuites.ps1 suite comment): the run never waits indefinitely for an operator.  The
# reproduction offer is a bounded window with a default; with redirected input (noninteractive) it returns the default
# at once and never touches the console key buffer ([Console]::KeyAvailable throws there and ended the run after Debug).
$rk = 0; $keyN = { $script:rk++; return 'N' }; $keyNone = { $script:rk++; return $null }
$t0 = [Diagnostics.Stopwatch]::StartNew(); $cRed = Read-AvbTimedChoice -Prompt 'p' -Seconds 30 -Default 'y' -Keys @{ N = 'n'; Y = 'y'; Enter = 'y' } -Interactive $false -ReadKey $keyN; $msRed = $t0.ElapsedMilliseconds
$rkRed = $rk; $cN = Read-AvbTimedChoice -Prompt 'p' -Seconds 30 -Default 'y' -Keys @{ N = 'n'; Y = 'y' } -Interactive $true -ReadKey $keyN
$t0 = [Diagnostics.Stopwatch]::StartNew(); $cT = Read-AvbTimedChoice -Prompt 'p' -Seconds 1 -Default 'y' -Keys @{ N = 'n' } -Interactive $true -ReadKey $keyNone; $msT = $t0.ElapsedMilliseconds
$code = (($ls -split "`r?`n") | Where-Object { $_ -notmatch '^\s*#' }) -join "`n"
$iOffer = $code.IndexOf('Read-AvbTimedChoice -Prompt "Reproduce'); $iRepro = $code.IndexOf('& $RunElevated')
$fnTimed = $lsAst.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Read-AvbTimedChoice' }, $true)
$keyAvailOutside = [regex]::Matches($code, 'KeyAvailable').Count - $(if ($fnTimed) { [regex]::Matches($fnTimed.Extent.Text, 'KeyAvailable').Count } else { 0 })
tc_check 'TEST-LOCALSUITES-010-reproduction-offer-is-bounded-default-continues-noninteractive-and-runs-through-the-runner-gates' `
    ($cRed -eq 'y' -and $rkRed -eq 0 -and $msRed -lt 1000 -and $cN -eq 'n' -and $cT -eq 'y' -and $msT -ge 900 -and $msT -lt 5000 -and
     ($code -notmatch '(?i)Read-Host') -and $keyAvailOutside -eq 0 -and $iOffer -gt 0 -and $iRepro -gt $iOffer -and ($code -match '-not \$NoReproduce') -and
     ($code -match '& \$RunElevated -TestName \$item\.Entry -Suite \$item\.Suite -Configuration \$Configuration -CaptureDbgView')) `
    ("redirected->{0} ({1} key reads, {2} ms) key N->{3} timeout->{4} ({5} ms) read-host={6} KeyAvailable outside helper={7} offer@{8} call@{9}" -f
     $cRed, $rkRed, $msRed, $cN, $cT, $msT, ($code -match '(?i)Read-Host'), $keyAvailOutside, $iOffer, $iRepro)

# 019: a missing/untrusted results document must not silently remove a suite from the run (861e318 removed the stop
# without a replacement: the suite then contributed no inventory row and the run could exit 0 without it).  It is a
# Failure row (never reproduced: not executed) and the matrix continues with the next suite.
$ur = New-AvbUntrustedSuiteRow -Configuration 'Release' -Suite 'HardwareUnit' -Why 'no results document at x'
$loop = $lsAst.Find({ param($x) $x -is [System.Management.Automation.Language.ForEachStatementAst] -and $x.Variable.Extent.Text -eq '$Suite' }, $true)
$notOk = if ($loop) { $loop.Body.Find({ param($x) $x -is [System.Management.Automation.Language.IfStatementAst] -and $x.Clauses[0].Item1.Extent.Text -eq '-not $res.Ok' }, $false) } else { $null }
$loopText = if ($loop) { (($loop.Body.Extent.Text -split "`r?`n") | Where-Object { $_ -notmatch '^\s*#' }) -join "`n" } else { '' }
$thenText = if ($notOk) { $notOk.Clauses[0].Item2.Extent.Text } else { '' }
$elseText = if ($notOk -and $notOk.ElseClause) { (($notOk.ElseClause.Extent.Text -split "`r?`n") | Where-Object { $_ -notmatch '^\s*#' }) -join "`n" } else { $null }
$docOutsideElse = if ($null -ne $elseText) { [regex]::Matches($loopText.Replace($elseText, ''), '\$res\.Doc\b').Count } else { -1 }
$breaks = @([regex]::Matches($loopText, '(?m)^.*\bbreak\b.*$') | ForEach-Object { $_.Value })
tc_check 'TEST-LOCALSUITES-019-untrusted-results-document-is-a-failure-row-and-the-matrix-continues' `
    ($ur.Category -eq 'Failure' -and $ur.Verdict -eq 'RESULTS_UNTRUSTED' -and -not $ur.Executed -and $ur.Suite -eq 'HardwareUnit' -and $ur.Configuration -eq 'Release' -and
     $ur.Reason -match 'no results document' -and (Get-AvbReproductionQueue -Inventory @($ur) -Configuration 'Release').Count -eq 0 -and
     $null -ne $notOk -and ($thenText -match 'New-AvbUntrustedSuiteRow') -and ($thenText -match '\$cfgInventory \+=') -and $docOutsideElse -eq 0 -and
     $breaks.Count -eq 1 -and $breaks[0] -match 'operator stop') `
    ("row={0}/{1}/executed={2} notOk-branch={3} res.Doc outside trusted branch={4} breaks=[{5}]" -f $ur.Category, $ur.Verdict, $ur.Executed, [bool]$notOk, $docOutsideElse, ($breaks -join ' || '))

# 020: after a suite with failures/blocked items: optional bounded operator stop window, default CONTINUE; only the
# explicit stop choice ends the run (intent Q 2026-10-22).
tc_check 'TEST-LOCALSUITES-020-failed-suite-offers-a-bounded-stop-window-default-continue' `
    (($loopText -match "Read-AvbTimedChoice -Prompt `"[^`"]*stop[^`"]*`" -Seconds 30 -Default 'continue'") -and ($loopText -match "-eq 'stop'\) \{ \`$Stop = `"operator stop")) `
    'no bounded default-continue stop window after a failed suite'
$iInstall = $ls.IndexOf('Install-Driver-Elevated $Configuration'); $iPre = $ls.IndexOf('Invoke-AvbPreFlight $cfg.builtDriverSha256'); $iSuites = $ls.IndexOf('foreach ($Suite in $Suites)')
tc_check 'TEST-LOCALSUITES-011-installed-driver-must-equal-the-locally-built-driver-before-any-suite' `
    (($iInstall -gt 0) -and ($iPre -gt $iInstall) -and ($iSuites -gt $iPre) -and ($ls -match "HIL_REQUIRE_DEPLOYED_SHA = '1'") -and ($ls -match 'BUILD_DRIVER_SHA256 = \$BuiltSha256')) `
    ("install@{0} preflight@{1} suites@{2}" -f $iInstall, $iPre, $iSuites)
# 016: a step never reached is NOT_RUN, distinct from a step that ran without an exit code (a failure);
# LocalSuite_20261021_143840.log reported the unreached pre-flight as "<none: no exit code observed>".
$cfgInit = $lsAst.Find({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq '$cfg' }, $true)
$cfgInitText = if ($cfgInit) { $cfgInit.Right.Extent.Text } else { '' }
tc_check 'TEST-LOCALSUITES-016-unreached-step-reported-not-run-distinct-from-missing-exit-code' `
    (((Format-AvbStepResult 'NOT_RUN') -eq 'NOT_RUN') -and ((Format-AvbStepResult $null) -eq '<none: no exit code observed>') -and ((Format-AvbStepResult 0) -eq '0') -and
     ((Format-AvbStepResult 3) -eq '3') -and ($cfgInitText -match "install = 'NOT_RUN'") -and ($cfgInitText -match "preflight = 'NOT_RUN'") -and
     ($ls -match 'Format-AvbStepResult \$c\.install') -and ($ls -match 'Format-AvbStepResult \$c\.preflight')) `
    ("NOT_RUN->[{0}] null->[{1}] init: {2}" -f (Format-AvbStepResult 'NOT_RUN'), (Format-AvbStepResult $null), ($cfgInitText -replace '\s+', ' '))
tc_check 'TEST-LOCALSUITES-012-never-reboots-or-retries-installation' `
    (($ls -notmatch '(?i)Restart-Computer|shutdown(\.exe)?\s+/r|bcdedit') -and ([regex]::Matches($ls, 'Install-Driver-Elevated \$Configuration').Count -eq 1)) 'reboot command or repeated install found'

# --- Run-Tests-Elevated: routing and DbgView capture --------------------------------------------------------
$cmdS = New-AvbElevatedCommand -Configuration 'Debug' -TestName "test_x --case TC-O'1" -TestArgs '' -TestExecutable '' -Full $false -LogFile 'C:\l\t.log' `
            -Suite 'HardwareUnit' -ResultsJson 'C:\r\x.json' -LegacyScript 'C:\t\Run-Tests.ps1' -RunCiScript 'C:\t\Run-Tests-CI.ps1'
$cmdL = New-AvbElevatedCommand -Configuration 'Release' -TestName 'test_y' -TestArgs '--fast' -TestExecutable '' -Full $false -LogFile '' `
            -Suite '' -ResultsJson '' -LegacyScript 'C:\t\Run-Tests.ps1' -RunCiScript 'C:\t\Run-Tests-CI.ps1'
$psErr = $null; [void][System.Management.Automation.Language.Parser]::ParseInput($cmdS, [ref]$null, [ref]$psErr)
tc_check 'TEST-LOCALSUITES-013-suite-reproduction-runs-through-run-tests-ci-with-results-json' `
    (($cmdS -match [regex]::Escape("& 'C:\t\Run-Tests-CI.ps1' -Suite HardwareUnit -Configuration Debug -Tests @('test_x --case TC-O''1') -ResultsJson 'C:\r\x.json'")) -and
     ($cmdS -notmatch 'Run-Tests\.ps1') -and ($psErr.Count -eq 0) -and ($cmdS -match 'if \(\$null -eq \$_innerCode\) \{ \$_innerCode = 1 \}') -and
     ($cmdL -match [regex]::Escape("& 'C:\t\Run-Tests.ps1' -Configuration Release -TestExecutable 'test_y.exe' -TestArgs '--fast'"))) "suite: $cmdS | legacy: $cmdL"

$logMissing = Join-Path $Tmp 'never.log'; $logPresent = Join-Path $Tmp 'present.log'; $logEmpty = Join-Path $Tmp 'empty.log'
$sleeper = Start-Process powershell.exe -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 30' -PassThru -WindowStyle Hidden
$exited  = Start-Process powershell.exe -ArgumentList '-NoProfile', '-Command', 'exit 0' -PassThru -WindowStyle Hidden
$null = $exited.Handle; $exited.WaitForExit(15000) | Out-Null
try {
    $w0 = Wait-AvbDbgViewStarted -Process $null -LogFile $logMissing -TimeoutSeconds 1
    $w1 = Wait-AvbDbgViewStarted -Process $exited -LogFile $logMissing -TimeoutSeconds 1
    $w2 = Wait-AvbDbgViewStarted -Process $sleeper -LogFile $logMissing -TimeoutSeconds 1
    Set-Content -LiteralPath $logPresent -Value '00000001 0.0 IntelAvbFilter: hello' -Encoding ASCII
    $w3 = Wait-AvbDbgViewStarted -Process $sleeper -LogFile $logPresent -TimeoutSeconds 1
} finally { Stop-Process -Id $sleeper.Id -Force -ErrorAction SilentlyContinue }
tc_check 'TEST-LOCALSUITES-014-dbgview-activation-failure-is-reported-and-never-confirmed' `
    ((-not $w0.Ok) -and ($w0.Error -match 'not started') -and (-not $w1.Ok) -and ($w1.Error -match 'exited') -and (-not $w2.Ok) -and ($w2.Error -match 'created no log file') -and $w3.Ok) `
    ("null={0} exited={1} nolog={2} ok={3}" -f $w0.Error, $w1.Error, $w2.Error, $w3.Ok)
New-Item -ItemType File -Path $logEmpty -Force | Out-Null
$c1 = Get-AvbDbgViewCaptureResult $logMissing; $c2 = Get-AvbDbgViewCaptureResult $logEmpty; $c3 = Get-AvbDbgViewCaptureResult $logPresent
$el = [System.IO.File]::ReadAllText($ElevPath)
tc_check 'TEST-LOCALSUITES-015-capture-never-claimed-without-a-log-and-test-not-run-without-capture' `
    (($c1.Status -eq 'MISSING') -and ($c2.Status -eq 'EMPTY') -and ($c3.Status -eq 'CAPTURED') -and ($c3.Bytes -gt 0) -and
     ($el.IndexOf('CAPTURE NOT STARTED') -gt 0) -and ($el.IndexOf('CAPTURE NOT STARTED') -lt $el.IndexOf('Start-Process powershell -Verb RunAs')) -and
     ($el -match 'if \(\$captureFailed -and \$childExitCode -eq 0\) \{ exit 1 \}') -and ($el -match "'STOP_FAILED'") -and ($el -match "\`$outcome = 'TIMEOUT'")) `
    ("missing={0} empty={1} captured={2}/{3}" -f $c1.Status, $c2.Status, $c3.Status, $c3.Bytes)

# 021: a failed test build never stops the matrix and never passes silently (repair 3b, Q 2026-10-27).  Defect observed in
# LocalSuites_20261024_133502: one LNK1168 in the Release test build ended the whole run ("[STOPPED] test build failed
# (Release)"), so no Release suite ran.  Build-Tests records each failure in the manifest and Run-Tests-CI BLOCKs a
# not-built entry by provenance, so the matrix continues; the failure is listed and the run stays nonpassing.
$tbBad = @()
try { Import-Functions $LocalPath @('Get-AvbTestBuildFailures') } catch { $tbBad += $_.Exception.Message }
if ($tbBad.Count -eq 0) {
    $manTb = [pscustomobject]@{ entries = @(
        [pscustomobject]@{ name = 't_ok';  status = 'built';  reason = '' },
        [pscustomobject]@{ name = 't_lnk'; status = 'failed'; reason = 'compiler exit code 2 or output missing' },
        [pscustomobject]@{ name = 't_nm';  status = 'failed'; reason = 'nmake exit 0 but x.exe was not written by this build' }) }
    $tb0 = @(Get-AvbTestBuildFailures -ExitCode 0 -Manifest $manTb)
    $tb1 = @(Get-AvbTestBuildFailures -ExitCode 1 -Manifest $manTb)
    $tb2 = @(Get-AvbTestBuildFailures -ExitCode 1 -Manifest ([pscustomobject]@{ entries = @() }))
    $tb3 = @(Get-AvbTestBuildFailures -ExitCode $null -Manifest ([pscustomobject]@{ entries = @() }))
    if ($tb0.Count -ne 0) { $tbBad += "exit 0 reported failures: [$($tb0 -join '; ')]" }
    if ($tb1.Count -ne 2 -or ($tb1 -join ' ') -notmatch 't_lnk: compiler' -or ($tb1 -join ' ') -notmatch 't_nm: nmake') { $tbBad += "exit 1: [$($tb1 -join '; ')]" }
    if ($tb2.Count -ne 1 -or $tb2[0] -notmatch 'exit 1') { $tbBad += "nonzero exit without a failed entry passed silently: [$($tb2 -join '; ')]" }
    if ($tb3.Count -ne 1) { $tbBad += "missing exit code passed silently: [$($tb3 -join '; ')]" }
}
# STATIC (script body): the Build-Tests step sets no $Stop, and build failures make the final exit nonzero.
$lsTxt = [System.IO.File]::ReadAllText($LocalPath)
if ($lsTxt -match '\$Stop = "test build failed') { $tbBad += 'a test build failure still stops the run' }
if ($lsTxt -notmatch '\$BuildFailures\.Count -gt 0') { $tbBad += 'build failures do not make the run nonpassing' }
if ($lsTxt -notmatch 'Get-AvbTestBuildFailures \$cfg\.testBuild \$man') { $tbBad += 'the Build-Tests step does not use Get-AvbTestBuildFailures' }
tc_check 'TEST-LOCALSUITES-021-failed-test-build-continues-the-matrix-and-stays-nonpassing' ($tbBad.Count -eq 0) ($tbBad -join ' ### ')

# --- 024: Run-Tests-Elevated -Suite without -TestName runs the WHOLE registered suite through Run-Tests-CI.ps1
#     (elevated; the DbgView handling around the child is unchanged) and carries earlier halts (-PriorResultsDir) ---
$cmdW = $null; $wErr = ''
try {
    $cmdW = New-AvbElevatedCommand -Configuration 'Release' -TestName '' -TestArgs '' -TestExecutable '' -Full $false -LogFile 'C:\l\s.log' `
                -Suite 'HardwareIntegration' -ResultsJson 'C:\r\w.json' -PriorResultsDir 'C:\r' -LegacyScript 'C:\t\Run-Tests.ps1' -RunCiScript 'C:\t\Run-Tests-CI.ps1'
} catch { $wErr = $_.Exception.Message }
$wPsErr = $null; if ($cmdW) { [void][System.Management.Automation.Language.Parser]::ParseInput($cmdW, [ref]$null, [ref]$wPsErr) }
$elSrc = [System.IO.File]::ReadAllText($ElevPath)
tc_check 'TEST-LOCALSUITES-024-elevated-whole-suite-runs-every-registered-entry-through-run-tests-ci-with-prior-halts' `
    (($null -ne $cmdW) -and ($cmdW -match [regex]::Escape("& 'C:\t\Run-Tests-CI.ps1' -Suite HardwareIntegration -Configuration Release -ResultsJson 'C:\r\w.json' -PriorResultsDir 'C:\r' -LocalDiagnostic")) -and
     ($cmdW -notmatch ' -Tests ') -and ($cmdW -notmatch 'Run-Tests\.ps1') -and (@($wPsErr).Count -eq 0) -and ($elSrc -notmatch '-Suite requires -TestName')) `
    $(if ($wErr) { "error: $wErr" } else { "cmd: $cmdW" })

# --- 025 (STATIC): FullSuite.ps1 dispatches the canonical suite sequence through the runner gates -------------------
# Defects in FullSuite_20261103_063836.log: a bare Run-Tests-CI.ps1 call (default Unit/Debug in both configurations),
# the suite list overwritten by @("All") and -CaptureDbgView passed to Run-Tests-CI.ps1 (rejected, nothing ran).  The
# per-program *.exe loop is the operator's authoritative path (Q 2026-11-03) and stays, verbatim, beside the suite runs.
$fsPath = Join-Path $RepoRoot 'tools\archive\deprecated\FullSuite.ps1'
$fsAst = [System.Management.Automation.Language.Parser]::ParseFile($fsPath, [ref]$null, [ref]$null)
$lsAst = [System.Management.Automation.Language.Parser]::ParseFile($LocalPath, [ref]$null, [ref]$null)
$fsCmds = @($fsAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.CommandAst] }, $true))
$hasParam = { param($c, [string]$p) @($c.CommandElements | Where-Object { $_ -is [System.Management.Automation.Language.CommandParameterAst] -and $_.ParameterName -eq $p }).Count -gt 0 }
$listOf = { param($a, [string]$var) @($a.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq $var }, $true)) }
$valuesOf = { param($asg) @($asg.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value }) }
$fsBad = @()
$ciCalls = @($fsCmds | Where-Object { [string]$_.GetCommandName() -match 'Run-Tests-CI\.ps1$' })
$elCalls = @($fsCmds | Where-Object { [string]$_.GetCommandName() -match 'Run-Tests-Elevated\.ps1$' })
foreach ($c in $ciCalls) {
    if (-not (& $hasParam $c 'Suite')) { $fsBad += "Run-Tests-CI.ps1 without -Suite: $($c.Extent.Text)" }
    if (& $hasParam $c 'CaptureDbgView') { $fsBad += "Run-Tests-CI.ps1 with -CaptureDbgView (rejected by the runner): $($c.Extent.Text)" }
}
# Two kinds of elevated calls: the whole-suite runs, and the operator's per-program loop over the built *.exe (restored
# verbatim, Q 2026-11-03: authoritative, per-test elevation and DebugView capture; its blacklist of the 24 h and S3 tests
# stays until their infrastructure exists).  Both go through Run-Tests-Elevated.ps1 -> Run-Tests-CI.ps1 (all gates).
$elSuite = @($elCalls | Where-Object { & $hasParam $_ 'Suite' })
$elProg  = @($elCalls | Where-Object { -not (& $hasParam $_ 'Suite') })
foreach ($c in $elSuite) {
    foreach ($p in 'PriorResultsDir', 'ResultsJson', 'CaptureDbgView') { if (-not (& $hasParam $c $p)) { $fsBad += "Run-Tests-Elevated.ps1 -Suite without -${p}: $($c.Extent.Text)" } }
    if (& $hasParam $c 'TestName') { $fsBad += "whole-suite call restricted by -TestName: $($c.Extent.Text)" }
}
foreach ($c in $elProg) {
    foreach ($p in 'TestName', 'CaptureDbgView') { if (-not (& $hasParam $c $p)) { $fsBad += "per-program Run-Tests-Elevated.ps1 call without -${p}: $($c.Extent.Text)" } }
}
if ($ciCalls.Count -eq 0 -or $elSuite.Count -eq 0) { $fsBad += "expected Run-Tests-CI.ps1 (offline suites) and Run-Tests-Elevated.ps1 -Suite (hardware suites) calls; found $($ciCalls.Count) / $($elSuite.Count)" }
if ($elProg.Count -eq 0) { $fsBad += 'the per-program Run-Tests-Elevated.ps1 -TestName -CaptureDbgView loop is missing (authoritative, Q 2026-11-03)' }
$fsHw = @(& $listOf $fsAst '$HardwareSuites'); $fsOff = @(& $listOf $fsAst '$OfflineSuites'); $lsSuites = @(& $listOf $lsAst '$Suites')
if ($fsHw.Count -ne 1 -or $fsOff.Count -ne 1) { $fsBad += "suite lists must be assigned exactly once (HardwareSuites x$($fsHw.Count), OfflineSuites x$($fsOff.Count))" }
elseif ($lsSuites.Count -ne 1 -or ((& $valuesOf $fsHw[0]) -join ',') -ne ((& $valuesOf $lsSuites[0]) -join ',')) { $fsBad += "HardwareSuites '$((& $valuesOf $fsHw[0]) -join ',')' is not the LocalSuites order" }
elseif (((& $valuesOf $fsOff[0]) -join ',') -ne 'Unit,Integration') { $fsBad += "OfflineSuites '$((& $valuesOf $fsOff[0]) -join ',')'" }
tc_check 'TEST-LOCALSUITES-025-STATIC-fullsuite-runs-the-suites-and-the-per-program-loop-through-the-gates-without-invalid-arguments' ($fsBad.Count -eq 0) ($fsBad -join ' ### ')

# --- 026 (STATIC): complete enumeration - every suite that has a registry list in Run-Tests-CI.ps1 is dispatched by
#     FullSuite.ps1, every non-empty one by Run-Tests.ps1 -Full, and the documentation-only $HardwareDependentTests are
#     all inside a dispatched suite (removing the *.exe loop drops no registered test) ---
$rcAst = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'), [ref]$null, [ref]$null)
# List/table literals only: the runner's counters ($totalTests = 0, $failedTests ...) share the name suffix.
$regVars = @($rcAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Parent -is [System.Management.Automation.Language.NamedBlockAst] -and
                              $x.Left.Extent.Text -match '^\$(\w+?)(Tests|CoverageGaps|AttendedOnly)$' -and $x.Right.Extent.Text -match '^(\[ordered\])?@[\(\{]' }, $true))
$entryKey = { param([string]$e) (($e.Trim() -split '\s+')[0]) -replace '(?i)\.exe$', '' }
$regSuites = [ordered]@{}
foreach ($v in $regVars) {
    $null = $v.Left.Extent.Text -match '^\$(\w+?)(Tests|CoverageGaps|AttendedOnly)$'; $s = $Matches[1]
    if ($s -eq 'HardwareDependent') { continue }
    $h = $v.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    $vals = if ($h) { @($h.KeyValuePairs | ForEach-Object { $_.Item1.Value }) } else { & $valuesOf $v }
    if (-not $regSuites.Contains($s)) { $regSuites[$s] = @() }
    $regSuites[$s] += @($vals)
}
$fsSuites = @(if ($fsHw.Count -eq 1) { & $valuesOf $fsHw[0] }) + @(if ($fsOff.Count -eq 1) { & $valuesOf $fsOff[0] })
$rtAst = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\test\Run-Tests.ps1'), [ref]$null, [ref]$null)
$rtOrder = @(& $listOf $rtAst '$suiteOrder' | ForEach-Object { & $valuesOf $_ })
$dispatched = @($regSuites.Values | ForEach-Object { $_ } | ForEach-Object { & $entryKey $_ } | Sort-Object -Unique)
$hwDep = @(& $listOf $rcAst '$HardwareDependentTests' | ForEach-Object { & $valuesOf $_ } | ForEach-Object { & $entryKey $_ })
$enBad = @()
foreach ($s in $regSuites.Keys) {
    if ($fsSuites -notcontains $s) { $enBad += "FullSuite does not dispatch suite '$s' ($(@($regSuites[$s]).Count) registered entries)" }
    if (@($regSuites[$s]).Count -gt 0 -and $rtOrder -notcontains $s) { $enBad += "Run-Tests.ps1 -Full does not dispatch suite '$s' ($(@($regSuites[$s]).Count) registered entries)" }
}
$orphan = @($hwDep | Where-Object { $dispatched -notcontains $_ })
if ($orphan.Count -gt 0) { $enBad += "HardwareDependentTests outside every suite: $($orphan -join ', ')" }
# Q 2026-11-03: exactly two hardware suites (HardwareUnit, HardwareIntegration) besides the offline Unit/Integration.
if ((@($regSuites.Keys | Sort-Object) -join ',') -ne 'HardwareIntegration,HardwareUnit,Integration,Unit') { $enBad += "registry suites are '$((@($regSuites.Keys | Sort-Object)) -join ',')', expected exactly Unit, Integration, HardwareUnit, HardwareIntegration" }
tc_check 'TEST-LOCALSUITES-026-STATIC-every-registered-suite-is-dispatched-no-registered-test-left-out' ($enBad.Count -eq 0) ($enBad -join ' ### ')

# --- 027: a program classified Diagnostic/Supporting/Obsolete ($TestArtifactClassification) requested through
#     Run-Tests.ps1 is reported NOT_EXECUTED with its classification - not mislabelled as an inventory gap, never run ---
$rtSrc = [System.IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\test\Run-Tests.ps1'))
$clsFn = $rtAst.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Get-AvbArtifactClassification' }, $true)
$clsBad = @()
if ($clsFn) {
    $runCi = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
    . ([scriptblock]::Create($clsFn.Extent.Text))
    $cls = Get-AvbArtifactClassification
    foreach ($n in 'quick_diagnostics', 'intel_avb_diagnostics') { if (-not ($cls.Contains($n) -and [string]$cls[$n] -match '^Diagnostic:')) { $clsBad += "$n not classified Diagnostic" } }
    if ($rtSrc -notmatch "NOT_EXECUTED\(") { $clsBad += 'no NOT_EXECUTED(<classification>) status in Run-Tests.ps1' }
} else { $clsBad += 'Get-AvbArtifactClassification missing in tools/test/Run-Tests.ps1' }
tc_check 'TEST-LOCALSUITES-027-classified-non-test-program-is-not-executed-and-reported-with-its-classification' ($clsBad.Count -eq 0) ($clsBad -join ' ### ')

# --- 028 (STATIC): only HardwareUnit and HardwareIntegration are hardware suites (Q 2026-11-03).  HardwareMandatory and
#     HardwareMutation are no suite, list, lane, step or acceptance bundle in any executable script or workflow ---
$twoBad = @()
$codeFiles = @('tools\test\Run-Tests-CI.ps1', 'tools\test\Run-Tests.ps1', 'tools\test\Run-Tests-Elevated.ps1', 'tools\archive\deprecated\LocalSuites.ps1',
               'tools\archive\deprecated\FullSuite.ps1', 'tools\development\Check-System.ps1', 'tools\lib\TestVerdict.ps1', '.github\workflows\hil.yml')
foreach ($rel in $codeFiles) {
    $lines = [System.IO.File]::ReadAllLines((Join-Path $RepoRoot $rel))
    $n = 0
    foreach ($l in $lines) { $n++; if ($l -notmatch '^\s*#' -and $l -match '\bHardware(Mandatory|Mutation)\w*') { $twoBad += "${rel}:${n}: $($l.Trim())" } }
}
$lsVals = @(& $listOf $lsAst '$Suites' | ForEach-Object { & $valuesOf $_ }) -join ','
if ($lsVals -ne 'HardwareUnit,HardwareIntegration') { $twoBad += "LocalSuites `$Suites = '$lsVals'" }
$fsVals = @(& $listOf $fsAst '$HardwareSuites' | ForEach-Object { & $valuesOf $_ }) -join ','
if ($fsVals -ne 'HardwareUnit,HardwareIntegration') { $twoBad += "FullSuite `$HardwareSuites = '$fsVals'" }
$hilTxt = [System.IO.File]::ReadAllText((Join-Path $RepoRoot '.github\workflows\hil.yml'))
if ($hilTxt -notmatch [regex]::Escape("SUITES = ['HardwareUnit', 'HardwareIntegration']")) { $twoBad += 'hil.yml acceptance SUITES is not exactly HardwareUnit, HardwareIntegration' }
tc_check 'TEST-LOCALSUITES-028-STATIC-only-hardwareunit-and-hardwareintegration-are-hardware-suites' ($twoBad.Count -eq 0) (($twoBad | Select-Object -First 12) -join ' ### ')

# --- 029 (STATIC): every former HardwareMandatory / HardwareMutation entry is dispatched by HardwareUnit or
#     HardwareIntegration with identical arguments, and no entry is dispatched twice (investigations/hil-suite-migration-2026-11-03.md) ---
$rcAst2 = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'), [ref]$null, [ref]$null)
$keyOf = { param([string]$e) $p = $e.Trim() -split '\s+', 2; (($p[0] -replace '(?i)\.exe$', '') + $(if ($p.Count -gt 1) { ' ' + $p[1] } else { '' })) }
$entriesOf = { param([string]$var)
    $a = @(& $listOf $rcAst2 $var)
    if ($a.Count -eq 0) { return @() }
    $h = $a[0].Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    if ($h) { @($h.KeyValuePairs | ForEach-Object { & $keyOf $_.Item1.Value }) } else { @(& $valuesOf $a[0] | ForEach-Object { & $keyOf $_ }) } }
$hwDispatch = @(& $entriesOf '$HardwareUnitTests') + @(& $entriesOf '$HardwareIntegrationTests') + @(& $entriesOf '$HardwareIntegrationAttendedOnly')
$migBad = @()
$dups = @($hwDispatch | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { "$($_.Name) x$($_.Count)" })
if ($dups.Count -gt 0) { $migBad += "dispatched more than once: $($dups -join ', ')" }
$migrated = @('test_ioctl_version', 'test_ioctl_routing', 'test_ioctl_simple', 'test_ioctl_abi', 'test_tx_timestamp_provenance', 'test_tx_timestamp_retrieval',
              'test_ioctl_phc_query', 'test_ioctl_xstamp', 'test_ioctl_phc_crosstimestamp_v2', 'test_all_adapters', 'test_multidev_adapter_enum',
              'test_multi_adapter_phc_sync', 'test_send_ptp_debug', 'test_minimal_ioctl', 'ptp_clock_control_test', 'test_rx_timestamp',
              'test_rx_timestamp_complete', 'test_ptp_phc_stability --case UT-CORR-005', 'test_ptp_phc_stability --case UT-CORR-006')
$lost = @($migrated | Where-Object { $hwDispatch -notcontains $_ })
if ($lost.Count -gt 0) { $migBad += "former HardwareMandatory/HardwareMutation entries not dispatched by a hardware suite: $($lost -join ', ')" }
tc_check 'TEST-LOCALSUITES-029-STATIC-every-migrated-entry-dispatched-exactly-once-by-a-hardware-suite' ($migBad.Count -eq 0) ($migBad -join ' ### ')

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-LOCALSUITES: local orchestration contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-LOCALSUITES: local orchestration contract holds" -ForegroundColor Green
exit 0
