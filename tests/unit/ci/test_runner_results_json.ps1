# test_runner_results_json.ps1
# TEST-RESULTSJSON-*: Run-Tests-CI.ps1 writes a machine-readable results document (schema avb-runner-results/1)
# so that local orchestration (tools/archive/deprecated/LocalSuites.ps1) and CI evidence never parse console
# Format-Table output.
#
# Contract under test:
#   - one record per result row: verdict in the runner vocabulary (PASSED, FAILED, FAILED(TIMEOUT),
#     BLOCKED(PROVENANCE), BLOCKED(SAFETY), SKIPPED, COVERAGE-GAP, INCOMPLETE, INCONCLUSIVE, INFRA_FAILURE,
#     CLEANUP_FAILED), raw runner status, executed yes/no, the observed exit code ($null = none observed),
#     artifact path + SHA256, exact arguments, safety class, target DID, log path, configuration, and the
#     per-testcase verdict lines with adapter index/DID;
#   - categories stay distinct: SKIPPED / COVERAGE-GAP are not failures, BLOCKED is not "not run", a run in which
#     everything was blocked is not success and reports zero executed tests;
#   - a missing exit code never counts as success; an unknown runner status throws instead of defaulting;
#   - -CaptureDbgView is rejected by this runner (it never captured); written atomically to -ResultsJson.
#
# Layers: unit tests of the real functions (extracted by AST) and behavioral runs of a sandboxed runner copy
# (AVB_TEST_SANDBOX=1, as in tests/unit/ci/test_hil_binary_provenance.ps1).  Nothing here opens
# \\.\IntelAvbFilter, queries PnP, touches services/adapters, or writes repository logs.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$RunCIPath = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-RESULTSJSON: machine-readable runner results" -ForegroundColor Cyan

$Tmp = Join-Path $env:TEMP ("resjson_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

# ---------------------------------------------------------------------------------------------------------
# Unit: real functions (AST-extracted; never executes Run-Tests-CI.ps1 here)
# ---------------------------------------------------------------------------------------------------------
$ast = [System.Management.Automation.Language.Parser]::ParseFile($RunCIPath, [ref]$null, [ref]$null)
$fn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'ConvertTo-AvbRunnerVerdict' }, $true)
. ([scriptblock]::Create($fn.Extent.Text))
. (Join-Path $RepoRoot 'tools\lib\TestVerdict.ps1')

$map = [ordered]@{
    'PASSED' = 'PASSED'; 'FAILED' = 'FAILED'; 'FAILED(SKIP_ONLY)' = 'FAILED'; 'FAILED(TIMEOUT)' = 'FAILED(TIMEOUT)'
    'FAILED(INCONCLUSIVE)' = 'INCONCLUSIVE'; 'INCOMPLETE(NOT_IMPLEMENTED)' = 'INCOMPLETE'; 'FAILED(INFRA)' = 'INFRA_FAILURE'
    'FAILED(BUILD)' = 'INFRA_FAILURE'; 'INFRA_FAILURE' = 'INFRA_FAILURE'; 'CLEANUP_FAILED' = 'CLEANUP_FAILED'
    'BLOCKED' = 'BLOCKED(SAFETY)'; 'BLOCKED(SAFETY)' = 'BLOCKED(SAFETY)'; 'BLOCKED(PROVENANCE)' = 'BLOCKED(PROVENANCE)'
    'SKIPPED' = 'SKIPPED'; 'SKIPPED(INFRA)' = 'SKIPPED'; 'COVERAGE-GAP' = 'COVERAGE-GAP'
}
$bad = @(foreach ($k in $map.Keys) { $v = ConvertTo-AvbRunnerVerdict $k; if ($v -ne $map[$k]) { "$k -> $v" } })
tc_check 'TEST-RESULTSJSON-001-every-runner-status-maps-to-a-distinct-results-verdict' ($bad.Count -eq 0) ($bad -join '; ')

# Every status string the runner emits is in the table above (a new status must be mapped deliberately).
$src = [System.IO.File]::ReadAllText($RunCIPath)
$emitted = @([regex]::Matches($src, "(?:-Status\s+|Status\s*=\s*)[`"']([A-Z_()\-]+)[`"']") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$unmapped = @($emitted | Where-Object { -not $map.Contains($_) })
$threw = $false; try { ConvertTo-AvbRunnerVerdict 'PASSED(SOMEHOW)' | Out-Null } catch { $threw = $true }
tc_check 'TEST-RESULTSJSON-002-unknown-status-throws-and-no-emitted-status-is-unmapped' ($threw -and $unmapped.Count -eq 0 -and $emitted.Count -ge 10) `
    ("threw={0}; emitted={1}; unmapped={2}" -f $threw, $emitted.Count, ($unmapped -join ','))

$tc = Get-AvbTestcaseVerdicts -Lines @('  [PASS] [Adapter0[VEN_8086:DEV_125B]] TC-RJ-001: ok', '[FAIL] TC-RJ-002/adapter:1: mismatch', 'noise', '[SKIP] TC-RJ-003: SKIP_UNSUPPORTED: no 1588')
tc_check 'TEST-RESULTSJSON-003-testcase-lines-keep-id-verdict-and-adapter' `
    ((@($tc).Count -eq 3) -and ($tc[0].Id -eq 'TC-RJ-001') -and ($tc[0].AdapterDid -eq '0x125B') -and ($tc[0].AdapterIndex -eq 0) -and
     ($tc[1].Verdict -eq 'FAIL') -and ($tc[1].AdapterIndex -eq 1) -and ($null -eq $tc[1].AdapterDid) -and ($tc[2].Verdict -eq 'SKIP_UNSUPPORTED')) `
    (($tc | ForEach-Object { "{0}:{1}:{2}:{3}" -f $_.Id, $_.Verdict, $_.AdapterIndex, $_.AdapterDid }) -join ' ')

# A missing exit code is never success: the verdict branch has no "$null -eq $exitCode" success path.
$verdictSuccessNull = [regex]::Matches($src, '\$exitCode -eq 0 -or \$null -eq \$exitCode').Count
tc_check 'TEST-RESULTSJSON-004-missing-exit-code-is-never-a-pass' ($verdictSuccessNull -eq 0) "$verdictSuccessNull success path(s) accept a missing exit code"

# ---------------------------------------------------------------------------------------------------------
# Behavioral: sandboxed runner copy
# ---------------------------------------------------------------------------------------------------------
$Tree  = Join-Path $Tmp 'tree'
$Fakes = Join-Path $Tmp 'fakes'
foreach ($d in (Join-Path $Tree 'tools\test'), (Join-Path $Tree 'tools\lib'), (Join-Path $Tree 'src'), (Join-Path $Tree 'include'), $Fakes) {
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}
foreach ($f in 'Run-Tests-CI.ps1', 'Convert-Logs-To-JUnit.ps1') { Copy-Item (Join-Path $RepoRoot "tools\test\$f") (Join-Path $Tree "tools\test\$f") }
foreach ($f in 'TestVerdict.ps1', 'NativeExit.ps1', 'SourceDigest.ps1') { Copy-Item (Join-Path $RepoRoot "tools\lib\$f") (Join-Path $Tree "tools\lib\$f") }
@'
function Get-AvbLifecycleSnapshot { return $null }
function Write-LifecycleDiff { param($Before, $After, $Label) return $false }
'@ | Set-Content (Join-Path $Tree 'tools\test\Lib-AvbLifecycle.ps1') -Encoding UTF8
Copy-Item (Join-Path $RepoRoot 'src\avb_integration_fixed.c') (Join-Path $Tree 'src\avb_integration_fixed.c')
Copy-Item (Join-Path $RepoRoot 'include\intel_pci_ids.h')     (Join-Path $Tree 'include\intel_pci_ids.h')
$TreeRunner = Join-Path $Tree 'tools\test\Run-Tests-CI.ps1'

$Classes = 'rj_pass=ReadOnly;rj_fail=ReadOnly;rj_timeout=ReadOnly;rj_incomplete=ReadOnly;rj_inconclusive=ReadOnly;rj_skip=ReadOnly;' +
           'rj_mutating=Mutating;rj_noprov=ReadOnly;rj_cleanup=ReadOnly;rj_rel=ReadOnly'

function New-Fake([string]$Name, [string]$Body) {
    $p = Join-Path $Fakes "$Name.ps1"
    Set-Content -LiteralPath $p -Value $Body -Encoding UTF8
    return $p
}
function Get-Entry([string]$Path) { return ('ps1:' + $Path.Replace('\', '/')) }
function Write-Manifest([string[]]$Paths, [string]$Configuration) {
    $dir = Join-Path $Tree "build\tools\avb_test\x64\$Configuration"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $entries = @(foreach ($p in $Paths) {
        $n = [System.IO.Path]::GetFileNameWithoutExtension($p)
        [ordered]@{ name = $n; type = 'sandbox-fake'; enabled = $true; status = 'built'; reason = ''; artifact = "$n.ps1"
                    sha256 = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; sourceRevision = 'sandbox-rev'; sourceDirty = $false }
    })
    $m = [ordered]@{ schemaVersion = 1; producer = 'test_runner_results_json.ps1'; configuration = $Configuration; platform = 'x64'; entries = $entries }
    ($m | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath (Join-Path $dir 'test-build-manifest.json') -Encoding UTF8
}
function New-StateFile([string]$Path) {
    $state = [ordered]@{
        adapters = @(
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_125B&SUBSYS_00008086&REV_04\RJ01'; PnpStatus = 'OK'; Problem = 0; AdminStatus = 'Up'; MediaStatus = 'Up'; FilterBound = 'True'; Name = 'RJ I226' },
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_1533&SUBSYS_00008086&REV_03\RJ02'; PnpStatus = 'OK'; Problem = 0; AdminStatus = 'Up'; MediaStatus = 'Up'; FilterBound = 'True'; Name = 'RJ I210' }
        )
        clock = @(
            [ordered]@{ Index = 0; Did = '0x125B'; Ok = $true; Timinca = 0x18000000; Tsauxc = 0 },
            [ordered]@{ Index = 1; Did = '0x1533'; Ok = $true; Timinca = 0;          Tsauxc = 0 }
        )
    }
    ($state | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Invoke-Sandbox {
    param([string]$Name, [string[]]$Tests, [string]$Configuration = 'Debug', [string[]]$Extra = @(), [int]$Timeout = 60)
    $dir = Join-Path $Tmp $Name; $logs = Join-Path $dir 'logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $stateFile = Join-Path $dir 'state.json'; New-StateFile $stateFile
    $json = Join-Path $dir 'results.json'
    $envs = [ordered]@{
        AVB_TEST_SANDBOX = '1'; AVB_TEST_ADAPTER_STATE_FILE = $stateFile; AVB_TEST_SANDBOX_CLASSES = $Classes
        AVB_TEST_STATE_SETTLE_SECONDS = '1'; GITHUB_ACTIONS = ''; GITHUB_ENV = ''; AVB_HIL_TARGET_DID = ''
        AVB_HIL_AUTHORIZE_UNSAFE = ''; AVB_TEST_HEALTH_FAIL_AFTER = ''
        AVB_TEST_SANDBOX_SOURCE_REVISION = 'sandbox-rev'; AVB_TEST_SANDBOX_SOURCE_DIRTY = '0'
        AVB_TEST_SANDBOX_SOURCE_MODE = ''; AVB_TEST_SANDBOX_SOURCE_DIGEST = ''
    }
    $lines = @()
    foreach ($k in $envs.Keys) {
        if ($envs[$k] -eq '') { $lines += "Remove-Item Env:$k -ErrorAction SilentlyContinue" }
        else { $lines += ("`$env:{0} = '{1}'" -f $k, $envs[$k].Replace("'", "''")) }
    }
    $testsLit = ($Tests | ForEach-Object { "'" + $_.Replace("'", "''") + "'" }) -join ', '
    $lines += "& '$TreeRunner' -Suite HardwareUnit -Tests @($testsLit) -TestTimeoutSeconds $Timeout -Configuration $Configuration -LogsDir '$logs' -ResultsJson '$json' $($Extra -join ' ')"
    $lines += 'exit $global:LASTEXITCODE'
    $wrapper = Join-Path $dir 'wrapper.ps1'
    $lines | Set-Content -LiteralPath $wrapper -Encoding UTF8
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -NonInteractive -File `"$wrapper`""
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $dir
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(300000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT'; Doc = $null; Json = $json; Dir = $dir } }
    $doc = $null
    if (Test-Path -LiteralPath $json) { $doc = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json }
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result); Doc = $doc; Json = $json; Dir = $dir }
}
function Get-Rec($r, [string]$Stem) { if ($null -eq $r.Doc) { return $null }; return @(@($r.Doc.records) | Where-Object { $_.name -like "$Stem.ps1*" -and -not $_.check })[0] }
function Show-Rec($rec) {
    if ($null -eq $rec) { return '<no record>' }
    return ("verdict={0} status={1} executed={2} exit={3} class={4} args='{5}' sha={6} log={7} tcs={8}" -f $rec.verdict, $rec.status, $rec.executed, $rec.exitCode,
            $rec.safetyClass, $rec.arguments, [bool]$rec.artifactSha256, [bool]$rec.logPath, @($rec.testcases).Count)
}
function Tail($r) { return (($r.Out -split "`n" | Select-Object -Last 12) -join ' | ') }

$pPass  = New-Fake 'rj_pass' "Write-Host '  [PASS] [Adapter0[VEN_8086:DEV_125B]] TC-RJ-001: ok'`r`nWrite-Host '  [PASS] TC-RJ-002/adapter:1: ok'`r`nexit 0"
$pFail  = New-Fake 'rj_fail' "Write-Host '  [FAIL] [Adapter1[VEN_8086:DEV_1533]] TC-RJ-010: register mismatch'`r`nexit 3"
$pTime  = New-Fake 'rj_timeout' "Write-Host '  [INFO] hanging'`r`nStart-Sleep -Seconds 60`r`nexit 0"
$pInc   = New-Fake 'rj_incomplete' "Write-Host '  [SKIP] TC-RJ-020: SKIP_NOT_IMPLEMENTED: driver lacks it'`r`nexit 0"
$pIncl  = New-Fake 'rj_inconclusive' "Write-Host '  [INCONCLUSIVE] TC-RJ-030: no evidence'`r`nexit 0"
$pSkip  = New-Fake 'rj_skip' "Write-Host '  [SKIP] TC-RJ-040: SKIP_UNSUPPORTED: adapter lacks capability'`r`nexit 2"
$pMut   = New-Fake 'rj_mutating' "Write-Host 'MUST NOT RUN'`r`nexit 0"
$pNoProv = New-Fake 'rj_noprov' "Write-Host 'MUST NOT RUN'`r`nexit 0"
$pClean = New-Fake 'rj_cleanup' ("`$sf = `$env:AVB_TEST_ADAPTER_STATE_FILE; `$s = Get-Content -LiteralPath `$sf -Raw | ConvertFrom-Json; " +
                                 "@(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_1533' })[0].AdminStatus = 'Down'; " +
                                 "(`$s | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath `$sf -Encoding UTF8`r`nWrite-Host '  [PASS] TC-RJ-050: ok'`r`nexit 0")
Write-Manifest @($pPass, $pFail, $pTime, $pInc, $pIncl, $pSkip, $pMut, $pClean) 'Debug'   # rj_noprov deliberately absent

# --- Run A: one item per category, Debug -----------------------------------------------------------------
$rA = Invoke-Sandbox 'runA' @((Get-Entry $pPass), (Get-Entry $pFail), (Get-Entry $pTime), (Get-Entry $pInc), (Get-Entry $pIncl), (Get-Entry $pSkip),
                              (Get-Entry $pMut), (Get-Entry $pNoProv), (Get-Entry $pClean)) -Timeout 8
$docOk = ($null -ne $rA.Doc) -and ($rA.Doc.schema -eq 'avb-runner-results/1')
tc_check 'TEST-RESULTSJSON-010-completed-run-writes-the-results-document' ($docOk -and (@(Get-ChildItem -LiteralPath $rA.Dir -Filter '*.tmp-*').Count -eq 0)) (Tail $rA)
if (-not $docOk) {
    Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
    exit 1
}
$recPass = Get-Rec $rA 'rj_pass'; $recFail = Get-Rec $rA 'rj_fail'; $recTime = Get-Rec $rA 'rj_timeout'
$recInc = Get-Rec $rA 'rj_incomplete'; $recIncl = Get-Rec $rA 'rj_inconclusive'; $recSkip = Get-Rec $rA 'rj_skip'
$recMut = Get-Rec $rA 'rj_mutating'; $recNoProv = Get-Rec $rA 'rj_noprov'; $recClean = Get-Rec $rA 'rj_cleanup'
$recState = @(@($rA.Doc.records) | Where-Object { $_.check -and $_.verdict -eq 'CLEANUP_FAILED' })

tc_check 'TEST-RESULTSJSON-011-passed-record-carries-exit-artifact-hash-log-and-adapter-testcases' `
    (($recPass.verdict -eq 'PASSED') -and $recPass.executed -and ($recPass.exitCode -eq 0) -and ($recPass.artifactSha256 -eq (Get-FileHash -LiteralPath $pPass).Hash) -and
     $recPass.logPath -and (Test-Path -LiteralPath $recPass.logPath) -and ($recPass.safetyClass -eq 'ReadOnly') -and ($recPass.configuration -eq 'Debug') -and
     (@($recPass.testcases).Count -eq 2) -and (@($recPass.testcases)[0].AdapterDid -eq '0x125B') -and (@($recPass.testcases)[1].AdapterIndex -eq 1)) (Show-Rec $recPass)
tc_check 'TEST-RESULTSJSON-012-failure-keeps-the-exact-exit-code-and-failing-testcase' `
    (($recFail.verdict -eq 'FAILED') -and ($recFail.exitCode -eq 3) -and (@($recFail.testcases).Count -eq 1) -and (@($recFail.testcases)[0] -isnot [array]) -and
     (@($recFail.testcases)[0].Id -eq 'TC-RJ-010') -and (@($recFail.testcases)[0].Verdict -eq 'FAIL') -and (@($recFail.testcases)[0].AdapterDid -eq '0x1533')) (Show-Rec $recFail)
tc_check 'TEST-RESULTSJSON-013-timeout-is-executed-with-no-observed-exit-code' `
    (($recTime.verdict -eq 'FAILED(TIMEOUT)') -and $recTime.executed -and ($null -eq $recTime.exitCode)) (Show-Rec $recTime)
tc_check 'TEST-RESULTSJSON-014-incomplete-and-inconclusive-are-distinct-categories' `
    (($recInc.verdict -eq 'INCOMPLETE') -and ($recIncl.verdict -eq 'INCONCLUSIVE') -and ($recInc.exitCode -eq 0)) ("incomplete: {0} | inconclusive: {1}" -f (Show-Rec $recInc), (Show-Rec $recIncl))
tc_check 'TEST-RESULTSJSON-015-skipped-is-not-converted-to-a-failure' `
    (($recSkip.verdict -eq 'SKIPPED') -and ($recSkip.exitCode -eq 2) -and ($rA.Doc.counts.SKIPPED -ge 1)) (Show-Rec $recSkip)
tc_check 'TEST-RESULTSJSON-016-blocked-items-are-recorded-as-not-executed-with-their-reason' `
    (($recMut.verdict -eq 'BLOCKED(SAFETY)') -and -not $recMut.executed -and ($null -eq $recMut.exitCode) -and ($recMut.reason -match 'TC-SAFETY-GATE-001') -and
     ($recNoProv.verdict -eq 'BLOCKED(PROVENANCE)') -and -not $recNoProv.executed -and ($recNoProv.reason -match 'TC-SAFETY-GATE-002') -and
     ($rA.Out -notmatch 'MUST NOT RUN')) ("mutating: {0} | noprov: {1}" -f (Show-Rec $recMut), (Show-Rec $recNoProv))
tc_check 'TEST-RESULTSJSON-017-cleanup-failure-is-a-check-record-separate-from-the-test-verdict' `
    (($recClean.verdict -eq 'PASSED') -and ($recState.Count -eq 1) -and -not $recState[0].executed -and ($recState[0].name -match 'rj_cleanup') -and
     (@($recState[0].testcases).Count -eq 1) -and (@($recState[0].testcases)[0] -isnot [array]) -and
     (@($recState[0].testcases)[0].Id -eq 'TC-INFRA-STATE-001')) ("test: {0} | state records: {1}" -f (Show-Rec $recClean), $recState.Count)
$c = $rA.Doc.counts
tc_check 'TEST-RESULTSJSON-018-summary-counts-executed-vs-not-executed-and-overall-matches-exit' `
    (($rA.Doc.executedCount -eq 7) -and ($rA.Doc.notExecutedCount -eq 2) -and ($c.PASSED -eq 2) -and ($c.FAILED -eq 1) -and ($c.'FAILED(TIMEOUT)' -eq 1) -and
     ($c.'BLOCKED(SAFETY)' -eq 1) -and ($c.'BLOCKED(PROVENANCE)' -eq 1) -and ($c.CLEANUP_FAILED -eq 1) -and
     ($rA.Doc.overall -eq 'FAILED') -and ($rA.Doc.exitCode -eq 1) -and ($rA.Exit -eq 1) -and ($rA.Doc.evidenceClass -eq 'sandbox')) `
    ("executed={0} notExecuted={1} counts={2} overall={3} doc.exit={4} exit={5}" -f $rA.Doc.executedCount, $rA.Doc.notExecutedCount, ($c | ConvertTo-Json -Compress), $rA.Doc.overall, $rA.Doc.exitCode, $rA.Exit)

# --- Run B: everything blocked - not success, zero executed --------------------------------------------------
$rB = Invoke-Sandbox 'runB' @((Get-Entry $pMut), (Get-Entry $pNoProv))
tc_check 'TEST-RESULTSJSON-019-all-blocked-run-is-not-success-and-reports-zero-executed' `
    (($null -ne $rB.Doc) -and ($rB.Doc.executedCount -eq 0) -and ($rB.Doc.overall -ne 'PASSED') -and ($rB.Doc.exitCode -ne 0) -and ($rB.Exit -ne 0) -and
     ($rB.Doc.counts.'BLOCKED(SAFETY)' -eq 1) -and ($rB.Doc.counts.'BLOCKED(PROVENANCE)' -eq 1)) `
    ("doc={0} executed={1} overall={2} exit={3}" -f ($null -ne $rB.Doc), $(if ($rB.Doc) { $rB.Doc.executedCount }), $(if ($rB.Doc) { $rB.Doc.overall }), $rB.Exit)

# --- Run C: Release configuration keeps its identity -------------------------------------------------------
$pRel = New-Fake 'rj_rel' "Write-Host '  [PASS] TC-RJ-060: ok'`r`nexit 0"
Write-Manifest @($pRel) 'Release'
$rC = Invoke-Sandbox 'runC' @((Get-Entry $pRel)) -Configuration 'Release'
$recRel = Get-Rec $rC 'rj_rel'
tc_check 'TEST-RESULTSJSON-020-configuration-is-recorded-per-document-and-record' `
    (($null -ne $recRel) -and ($rC.Doc.configuration -eq 'Release') -and ($recRel.configuration -eq 'Release') -and ($recRel.verdict -eq 'PASSED') -and ($rC.Exit -eq 0) -and ($rC.Doc.overall -eq 'PASSED')) `
    ("{0} | doc cfg={1}" -f (Show-Rec $recRel), $(if ($rC.Doc) { $rC.Doc.configuration }))

# --- Run D: -CaptureDbgView is rejected, nothing executes ---------------------------------------------------
$rD = Invoke-Sandbox 'runD' @((Get-Entry $pPass)) -Extra @('-CaptureDbgView')
tc_check 'TEST-RESULTSJSON-021-capturedbgview-is-rejected-instead-of-silently-ignored' `
    (($rD.Exit -eq 1) -and ($rD.Out -match 'does not capture DebugView') -and ($rD.Out -match 'Run-Tests-Elevated\.ps1 -TestName') -and ($rD.Out -notmatch 'TC-RJ-001')) (Tail $rD)

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-RESULTSJSON: runner results are not machine-readable or lose categories" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-RESULTSJSON: runner writes complete machine-readable results" -ForegroundColor Green
exit 0
