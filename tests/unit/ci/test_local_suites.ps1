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
    Import-Functions $LocalPath @('Read-AvbSuiteResults', 'Get-AvbFailureInventory', 'Get-AvbStopReason', 'Get-AvbReproductionQueue', 'Format-AvbStepResult')
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

# --- Stop rules ---------------------------------------------------------------------------------------------
$stopT = Get-AvbStopReason $doc
$docClean = Write-Doc (Join-Path $Tmp 'clean.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_fail' 'FAILED' $true -Exit 3), (New-Rec 't_blk' 'BLOCKED(SAFETY)' $false))
$docHalt  = Write-Doc (Join-Path $Tmp 'halt.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_fail' 'FAILED' $true -Exit 3)) -Halt 'CLEANUP_FAILED after x'
$docClean2 = Write-Doc (Join-Path $Tmp 'cleanup.json') 'HardwareUnit' 'Debug' 1 @((New-Rec 't_pass' 'PASSED' $true -Exit 0), (New-Rec 't_pass[STATE]' 'CLEANUP_FAILED' $false -Check $true))
tc_check 'TEST-LOCALSUITES-006-timeout-infra-cleanup-and-halt-stop-the-run' `
    (($stopT -match 'FAILED\(TIMEOUT\)') -and ($stopT -match 'INFRA_FAILURE') -and ((Get-AvbStopReason $docClean) -eq '') -and
     ((Get-AvbStopReason $docHalt) -match 'state-integrity halt') -and ((Get-AvbStopReason $docClean2) -match 'CLEANUP_FAILED')) `
    ("timeout/infra='{0}' clean='{1}' halt='{2}' cleanup='{3}'" -f $stopT, (Get-AvbStopReason $docClean), (Get-AvbStopReason $docHalt), (Get-AvbStopReason $docClean2))

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
tc_check 'TEST-LOCALSUITES-008-suite-order-unchanged-mandatory-last' ($order -eq 'HardwareUnit,HardwareIntegration,HardwareMutation,HardwareMandatory') "order: $order"
tc_check 'TEST-LOCALSUITES-009-results-come-from-results-json-not-console-table-parsing' `
    (($ls -match '-ResultsJson \$json') -and ($ls -notmatch 'CI Test Summary --') -and ($ls -notmatch '\(\?<Status>FAILED')) 'LocalSuites still parses console output or does not request -ResultsJson'
$iRead = $ls.IndexOf('Read-Host "Reproduce'); $iRepro = $ls.IndexOf('& $RunElevated')
tc_check 'TEST-LOCALSUITES-010-reproduction-is-operator-confirmed-with-dbgview-through-the-runner-gates' `
    (($iRead -gt 0) -and ($iRepro -gt $iRead) -and ($ls -match '-not \$NoReproduce') -and ($ls -match '& \$RunElevated -TestName \$item\.Entry -Suite \$item\.Suite -Configuration \$Configuration -CaptureDbgView')) `
    ("read-host@{0} call@{1}" -f $iRead, $iRepro)
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

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-LOCALSUITES: local orchestration contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-LOCALSUITES: local orchestration contract holds" -ForegroundColor Green
exit 0
