# test_tas_lifecycle_eval_mutation.ps1
# TEST-TAS-LIFE-MUT-*: mutation audit of tests/ioctl/tas_lifecycle_eval.h against tests/unit/test_tas_restore_contract.c.
#
# The assertions live in test_tas_restore_contract (registered, TC-MOCK-014..023).  This test adds only what that test cannot
# show: that each condition in the evaluator is really observed by an assertion.  A mutant is KILLED only when the unmodified
# test_tas_restore_contract source, compiled against the mutated header, reports a failed assertion through its normal path:
# `[FAIL]` lines, complete totals, `[RESULT] FAILURE`, exit 1.  Get-MutantVerdict decides, and its negative controls prove that
# a launch failure, missing or wrong exit code, timeout, missing output, compile failure or crash is never a kill.
# Acceptance: the unmutated code passes; every operator matches exactly once; 65 of 65 KILLED.  Each mutant keeps its own verdict.
#
# Works on copies under $env:TEMP.  The repository is not touched, no driver or adapter is involved.  Offline logic only,
# never HIL evidence.  The compiler comes from tools\vs_compile.ps1.
#
# Operators recovered unchanged from the audit that gave 65 of 65 on 2026-11-06, which ran outside the repository.
# Traces to: #364.  TEST issue: pending, drafted for Q's review.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$Header    = Join-Path $RepoRoot 'tests\ioctl\tas_lifecycle_eval.h'
$VsCompile = Join-Path $RepoRoot 'tools\vs_compile.ps1'
$Clock     = [Diagnostics.Stopwatch]::StartNew()

$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++
    if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
    else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }

Write-Host ""
Write-Host "TEST-TAS-LIFE-MUT: mutation audit of the lifecycle evaluator (offline, not HIL evidence)" -ForegroundColor Cyan

$OperatorsJson = @'
[
[
"cleanup: drop regs_changed",
"c->regs_changed != 0 || ",
""
],
[
"cleanup: drop PHC discontinuity",
"c->phc == TAS_PHC_DISCONTINUOUS || ",
""
],
[
"cleanup: drop others_changed",
"c->others_changed != 0 || ",
""
],
[
"cleanup: drop TX probe failure",
"(c->tx_probed && !c->tx_shown) ||\n        ",
""
],
[
"cleanup: drop inconsistency",
" ||\n        (c->lifecycle_shown && !c->consistent)",
""
],
[
"phc: <= to <",
"return after <= before ? TAS_PHC_DISCONTINUOUS",
"return after < before ? TAS_PHC_DISCONTINUOUS"
],
[
"phc: unmeasured check off",
"if (!before_ok || !after_ok) {",
"if (FALSE) {"
],
[
"window: lower gap <= to <",
"if (gap <= 0) {",
"if (gap < 0) {"
],
[
"window: unattributable ignored",
"if (t->unattributable || !t->pause.seen || !t->restart.seen) {",
"if (!t->pause.seen || !t->restart.seen) {"
],
[
"window: restart required off",
"if (t->unattributable || !t->pause.seen || !t->restart.seen) {",
"if (t->unattributable || !t->pause.seen) {"
],
[
"window: proven calls need one side only",
"return t->inside_samples > 2 ? t->inside_samples - 2 : 0;",
"return t->inside_samples > 1 ? t->inside_samples - 1 : 0;"
],
[
"window: multi-cycle ignored",
" || pause_delta > 1 || restart_delta > 1) {",
") {"
],
[
"window: fallen counter ignored",
"if (pause_delta < 0 || restart_delta < 0 || pause_delta > 1",
"if (pause_delta > 1"
],
[
"window: restart-before-pause ignored",
"if (t->restart.seen && !t->pause.seen) {",
"if (FALSE) {"
],
[
"window: first-poll edge accepted",
"t->unattributable = TRUE;   /* no old read bounds it */",
"(void)0;"
],
[
"window: inside counts after restart",
"if (pause_delta == 1 && restart_delta == 0) {",
"if (pause_delta == 1) {"
],
[
"window: edge uses end of old poll",
"edge->last_old = t->last.qpc_start;",
"edge->last_old = t->last.qpc_end;"
],
[
"window: edge uses start of new poll",
"edge->first_new = p->qpc_end;",
"edge->first_new = p->qpc_start;"
],
[
"claims: pause entry upgraded",
"case TAS_LIFECYCLE_PAUSE_ONLY:      return TAS_CLAIM_ENTRY_OBSERVED;",
"case TAS_LIFECYCLE_PAUSE_ONLY:      return TAS_CLAIM_COMPLETION_INFERRED;"
],
[
"claims: restart entry upgraded",
"case TAS_LIFECYCLE_RESTART_ENTERED: return TAS_CLAIM_ENTRY_OBSERVED;",
"case TAS_LIFECYCLE_RESTART_ENTERED: return TAS_CLAIM_COMPLETION_INFERRED;"
],
[
"outcome: traffic not required",
"return e->traffic_shown ? TAS_LIFECYCLE_RESUMED : TAS_LIFECYCLE_RESTART_ENTERED;",
"return TAS_LIFECYCLE_RESUMED;"
],
[
"outcome: detach ignored",
"if (e->detach_delta != 0 || e->attach_delta != 0 ||",
"if (e->attach_delta != 0 ||"
],
[
"outcome: attach ignored",
"if (e->detach_delta != 0 || e->attach_delta != 0 ||",
"if (e->detach_delta != 0 ||"
],
[
"outcome: generation ignored",
"if (e->pause_delta != 1 || e->generation_delta != 1 || e->restart_delta > 1) {",
"if (e->pause_delta != 1 || e->restart_delta > 1) {"
],
[
"instance: separator not required",
" && name[guid_chars] == L'-';",
";"
],
[
"instance: prefix check off",
"if (list + at != found && bytes <= found_bytes && memcmp(list + at, found, bytes) == 0) {",
"if (FALSE) {"
],
[
"instance: duplicate accepted",
"if (found != NULL) {\n                return TAS_INSTANCE_AMBIGUOUS;\n            }",
""
],
[
"snapshot: value change ignored",
"if (before->entries[i].value != after->entries[i].value) {",
"if (FALSE) {"
],
[
"snapshot: failed read counted as compared",
"} else if (was != AVB_HWREG_STATE_VALID || now != AVB_HWREG_STATE_VALID) {",
"} else if (FALSE) {"
],
[
"phc: forward reported discontinuous",
": TAS_PHC_NOT_JUDGED;",
": TAS_PHC_DISCONTINUOUS;"
],
[
"phc: forward claimed continuous",
": TAS_PHC_NOT_JUDGED;",
": TAS_PHC_CONTINUOUS;"
],
[
"cleanup: drop !lifecycle_shown",
"if (!c->lifecycle_shown || c->regs_compared",
"if (c->regs_compared"
],
[
"cleanup: drop regs_compared",
"c->regs_compared == 0 || ",
""
],
[
"cleanup: drop regs_unreadable",
"c->regs_unreadable != 0 || ",
""
],
[
"cleanup: drop !tx_probed",
"!c->tx_probed || ",
""
],
[
"cleanup: drop continuity",
" || c->phc != TAS_PHC_CONTINUOUS",
""
],
[
"cleanup: VERIFIED never returned",
"    return TAS_RESTORATION_VERIFIED;\n}",
"    return TAS_RESTORATION_INCONCLUSIVE;\n}"
],
[
"plan: identity_ok unchecked",
"if (!f->identity_ok || !f->hw_state_ok || f->hw_caps == 0) {",
"if (!f->hw_state_ok || f->hw_caps == 0) {"
],
[
"plan: hw_state_ok unchecked",
"if (!f->identity_ok || !f->hw_state_ok || f->hw_caps == 0) {",
"if (!f->identity_ok || f->hw_caps == 0) {"
],
[
"plan: empty caps accepted",
"if (!f->identity_ok || !f->hw_state_ok || f->hw_caps == 0) {",
"if (!f->identity_ok || !f->hw_state_ok) {"
],
[
"plan: hardware TAS not required",
"if ((f->hw_caps & INTEL_CAP_TSN_TAS) == 0) {\n        return TAS_PLAN_SKIP_UNSUPPORTED;\n    }\n",
""
],
[
"plan: not implemented reads unsupported",
"return TAS_PLAN_SKIP_NOT_IMPLEMENTED;",
"return TAS_PLAN_SKIP_UNSUPPORTED;"
],
[
"plan: driver TAS not required",
"if ((f->driver_caps & INTEL_CAP_TSN_TAS) == 0) {\n        return TAS_PLAN_SKIP_NOT_IMPLEMENTED;\n    }\n",
""
],
[
"plan: state check off",
"if ((int)f->hw_state < (int)AVB_HW_PTP_READY) {",
"if (FALSE) {"
],
[
"plan: BAR_MAPPED accepted",
"(int)f->hw_state < (int)AVB_HW_PTP_READY",
"(int)f->hw_state < (int)AVB_HW_BAR_MAPPED"
],
[
"plan: measurability off",
"if ((f->driver_caps & TAS_CASE_A_DRIVER_CAPS) != TAS_CASE_A_DRIVER_CAPS) {",
"if (FALSE) {"
],
[
"plan: only one measurability bit",
"#define TAS_CASE_A_DRIVER_CAPS ((avb_u32)(INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO))",
"#define TAS_CASE_A_DRIVER_CAPS ((avb_u32)(INTEL_CAP_MMIO))"
],
[
"plan: other measurability bit",
"#define TAS_CASE_A_DRIVER_CAPS ((avb_u32)(INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO))",
"#define TAS_CASE_A_DRIVER_CAPS ((avb_u32)(INTEL_CAP_BASIC_1588))"
],
[
"plan: link not required",
"if (!f->link_up) {\n        return TAS_PLAN_SKIP_NO_LINK;\n    }\n",
""
],
[
"plan: management link not required",
"return f->separate_management_link ? TAS_PLAN_ELIGIBLE : TAS_PLAN_BLOCKED;",
"return TAS_PLAN_ELIGIBLE;"
],
[
"plan: count overwritten, not summed",
"eligible += classes[i] == TAS_PLAN_ELIGIBLE ? 1u : 0u;",
"eligible = classes[i] == TAS_PLAN_ELIGIBLE ? 1u : 0u;"
],
[
"plan: stops after the first adapter",
"for (i = 0; i < n; i++) {\n        classes[i] = tas_plan_classify(&a[i]);",
"for (i = 0; i < n && i < 1; i++) {\n        classes[i] = tas_plan_classify(&a[i]);"
],
[
"observer: target may observe itself",
"if (i == target || !a[i].identity_ok",
"if (!a[i].identity_ok"
],
[
"observer: link not required",
" || !a[i].link_up) {",
") {"
],
[
"observer: identity unchecked",
"if (i == target || !a[i].identity_ok || !a[i].link_up) {",
"if (i == target || !a[i].link_up) {"
],
[
"observer: enumeration order",
"k > 0 && a[order[k - 1]].net_luid > a[i].net_luid",
"FALSE"
],
[
"observer: TAS required of observers",
"if (i == target || !a[i].identity_ok || !a[i].link_up) {",
"if (i == target || !a[i].identity_ok || !a[i].link_up || (a[i].hw_caps & INTEL_CAP_TSN_TAS) == 0) {"
],
[
"exit: failed adapter ignored",
"            failed = failed || results[i] == 1;\n",
""
],
[
"exit: PHC ignored",
"            phc = phc || results[i] == 2;\n",
""
],
[
"exit: not run ignored",
"            not_run = not_run || results[i] == 3;\n",
""
],
[
"exit: not implemented passes",
"        case TAS_PLAN_SKIP_NOT_IMPLEMENTED:\n        case TAS_PLAN_INCONCLUSIVE:",
"        case TAS_PLAN_INCONCLUSIVE:"
],
[
"exit: inconclusive passes",
"        case TAS_PLAN_INCONCLUSIVE:\n        case TAS_PLAN_BLOCKED:\n            failed",
"        case TAS_PLAN_BLOCKED:\n            failed"
],
[
"exit: blocked passes",
"        case TAS_PLAN_INCONCLUSIVE:\n        case TAS_PLAN_BLOCKED:\n            failed",
"        case TAS_PLAN_INCONCLUSIVE:\n            failed"
],
[
"exit: nothing eligible passes",
"if (eligible == 0 || not_run) {",
"if (not_run) {"
],
[
"exit: not run outranks failure",
"    if (failed) {\n        return 1;\n    }\n    if (eligible == 0 || not_run) {\n        return 3;\n    }\n",
"    if (eligible == 0 || not_run) {\n        return 3;\n    }\n    if (failed) {\n        return 1;\n    }\n"
]
]
'@
$raw = ConvertFrom-Json $OperatorsJson          # through a variable: Windows PowerShell keeps a JSON array as one pipeline object
$Operators = @($raw)
tc_check 'TEST-TAS-LIFE-MUT-000-operator-count' ($Operators.Count -eq 65) "expected 65 operators, found $($Operators.Count)"

# The verdict of one run.  KILLED means an assertion of test_tas_restore_contract failed and its normal failure path ended
# the run: `[FAIL] label`, ` FAIL: n / m`, `[RESULT] FAILURE`, return 1 (tests/unit/test_tas_restore_contract.c).
# SURVIVED is the mirror: `[RESULT] ALL MOCK TESTS PASSED`, return 0.  Anything else is not a test result about the mutant.
function Get-MutantVerdict {
    param($e)
    if (-not $e.Compiled) { return @{ Verdict = 'INCONCLUSIVE'; Reason = 'invalid mutant: did not compile' } }
    if (-not $e.Launched) { return @{ Verdict = 'INFRA_FAILURE'; Reason = "launch failed: $($e.Launch)" } }
    if ($e.TimedOut) { return @{ Verdict = 'INCONCLUSIVE'; Reason = 'timed out, no assertion result' } }
    if ($null -eq $e.ExitCode) { return @{ Verdict = 'INFRA_FAILURE'; Reason = 'no exit code' } }
    if ($null -eq $e.Output -or $e.Output -eq '') { return @{ Verdict = 'INCONCLUSIVE'; Reason = "no test output, exit $($e.ExitCode): $($e.Launch)" } }
    $sum = [regex]::Match($e.Output, '(?m)^ PASS: (\d+) / (\d+)\s*\r?\n FAIL: (\d+) / (\d+)\s*$')
    if (-not $sum.Success) { return @{ Verdict = 'INCONCLUSIVE'; Reason = "incomplete output: no assertion totals, exit $($e.ExitCode)" } }
    $pass = [int]$sum.Groups[1].Value; $all = [int]$sum.Groups[2].Value; $fail = [int]$sum.Groups[3].Value
    $lines = ([regex]::Matches($e.Output, '(?m)^\s*\[FAIL\] ')).Count
    if ($all -lt 1 -or $all -ne [int]$sum.Groups[4].Value -or $pass + $fail -ne $all -or $lines -ne $fail) { return @{ Verdict = 'INCONCLUSIVE'; Reason = "inconsistent totals: pass $pass fail $fail of $all, $lines failure lines" } }
    if ($e.ExitCode -eq 1 -and $fail -ge 1 -and $e.Output -match '(?m)^\[RESULT\] FAILURE\s*$') { return @{ Verdict = 'KILLED'; Reason = "$fail of $all assertions failed" } }
    if ($e.ExitCode -eq 0 -and $fail -eq 0 -and $e.Output -match '(?m)^\[RESULT\] ALL MOCK TESTS PASSED\s*$') { return @{ Verdict = 'SURVIVED'; Reason = "$all of $all assertions passed" } }
    return @{ Verdict = 'INCONCLUSIVE'; Reason = "exit $($e.ExitCode) does not match the result it printed (fail $fail of $all)" }
}

# Negative controls: none of these may ever read as a kill.
$FailedRun = " [FAIL] x: got=0 want=1`n PASS: 1 / 2`n FAIL: 1 / 2`n[RESULT] FAILURE`n"
$CleanRun  = " PASS: 2 / 2`n FAIL: 0 / 2`n[RESULT] ALL MOCK TESTS PASSED`n"
$Controls = @(
    @('launch-failure', @{ Compiled = $true; Launched = $false; TimedOut = $false; ExitCode = $null; Output = $null; Launch = 'not found' }, 'INFRA_FAILURE'),
    @('missing-exit-code', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = $null; Output = $FailedRun; Launch = '' }, 'INFRA_FAILURE'),
    @('invalid-exit-code', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 2; Output = $FailedRun; Launch = '' }, 'INCONCLUSIVE'),
    @('exit-1-without-failure-result', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 1; Output = $CleanRun; Launch = '' }, 'INCONCLUSIVE'),
    @('timeout', @{ Compiled = $true; Launched = $true; TimedOut = $true; ExitCode = $null; Output = $FailedRun; Launch = '' }, 'INCONCLUSIVE'),
    @('missing-evidence', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 1; Output = $null; Launch = '' }, 'INCONCLUSIVE'),
    @('empty-output-exit-1', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 1; Output = ''; Launch = "'t.exe' not found" }, 'INCONCLUSIVE'),
    @('parser-or-compile-failure', @{ Compiled = $false; Launched = $false; TimedOut = $false; ExitCode = $null; Output = $null; Launch = '' }, 'INCONCLUSIVE'),
    @('unexpected-crash', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = -1073741819; Output = " [FAIL] x: got=0 want=1`n"; Launch = '' }, 'INCONCLUSIVE'),
    @('crash-after-totals', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = -1073741819; Output = $FailedRun; Launch = '' }, 'INCONCLUSIVE'),
    @('truncated-totals', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 1; Output = " [FAIL] x: got=0 want=1`n PASS: 1 / 2`n"; Launch = '' }, 'INCONCLUSIVE'),
    @('inconsistent-totals', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 1; Output = " PASS: 1 / 3`n FAIL: 1 / 3`n[RESULT] FAILURE`n"; Launch = '' }, 'INCONCLUSIVE'),
    @('positive-kill', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 1; Output = $FailedRun; Launch = '' }, 'KILLED'),
    @('positive-survivor', @{ Compiled = $true; Launched = $true; TimedOut = $false; ExitCode = 0; Output = $CleanRun; Launch = '' }, 'SURVIVED')
)
foreach ($c in $Controls) { $v = (Get-MutantVerdict $c[1]).Verdict; tc_check "TEST-TAS-LIFE-MUT-CTRL-$($c[0])" ($v -eq $c[2]) "got $v, want $($c[2])" }

$Revision = (& git -C $RepoRoot rev-parse HEAD 2>$null)
$Dirty = @(& git -C $RepoRoot status --porcelain --untracked-files=no 2>$null).Count
Write-Host ("  [PROVENANCE] revision {0} tracked changes {1} header {2} assertions {3}" -f $Revision, $Dirty, (Get-FileHash -LiteralPath $Header).Hash, (Get-FileHash -LiteralPath "$RepoRoot\tests\unit\test_tas_restore_contract.c").Hash)

$source = [IO.File]::ReadAllText($Header).Replace("`r`n", "`n")
$Tmp = Join-Path $env:TEMP ('tas_life_mut_{0}' -f [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($Tmp)
try {
    $NewTree = {
        param($name, $text)
        $d = "$Tmp\$name"
        foreach ($x in 'tests\unit', 'tests\ioctl', 'tests\common', 'include', 'external\intel_avb\lib') { [void][IO.Directory]::CreateDirectory("$d\$x") }
        foreach ($f in [IO.Directory]::GetFiles("$RepoRoot\tests\common", '*.h')) { [IO.File]::Copy($f, "$d\tests\common\$([IO.Path]::GetFileName($f))") }
        foreach ($f in [IO.Directory]::GetFiles("$RepoRoot\external\intel_avb\lib", '*.h')) { [IO.File]::Copy($f, "$d\external\intel_avb\lib\$([IO.Path]::GetFileName($f))") }
        [IO.File]::Copy("$RepoRoot\tests\unit\test_tas_restore_contract.c", "$d\tests\unit\test_tas_restore_contract.c")
        foreach ($h in 'tas_restore.h', 'tas_restoration_eval.h', 'tas_wire_match.h') { [IO.File]::Copy("$RepoRoot\tests\ioctl\$h", "$d\tests\ioctl\$h") }
        foreach ($f in [IO.Directory]::GetFiles("$RepoRoot\include", '*.h')) { [IO.File]::Copy($f, "$d\include\$([IO.Path]::GetFileName($f))") }
        [IO.File]::WriteAllText("$d\tests\ioctl\tas_lifecycle_eval.h", $text)
        return $d
    }
    $names = @{}; $bad = New-Object System.Collections.ArrayList
    $null = & $NewTree 'baseline' $source
    $i = 0
    foreach ($m in $Operators) {
        $i++
        $count = ([regex]::Matches($source, [regex]::Escape([string]$m[1]))).Count
        if ($count -ne 1) { [void]$bad.Add("$($m[0]) matched $count times"); continue }
        $id = 'm{0:D2}' -f $i
        $names[$id] = [string]$m[0]
        $null = & $NewTree $id $source.Replace([string]$m[1], [string]$m[2])
    }
    tc_check 'TEST-TAS-LIFE-MUT-001-every-operator-matches-once' ($bad.Count -eq 0) ($bad -join '; ')

    # Runs inside the Visual Studio environment that vs_compile.ps1 provides.
    $Worker = @"
`$jobs = New-Object System.Collections.ArrayList
`$cap = [Environment]::ProcessorCount
foreach (`$d in [IO.Directory]::GetDirectories('$Tmp')) {
    while (@(`$jobs | Where-Object { -not `$_.HasExited }).Count -ge `$cap) { Start-Sleep -Milliseconds 20 }
    `$si = [Diagnostics.ProcessStartInfo]@{ FileName = 'cmd.exe'; Arguments = '/c cl /nologo /W1 /I include /I external\intel_avb\lib tests\unit\test_tas_restore_contract.c /Fe:t.exe >cl.log 2>&1'; UseShellExecute = `$false; WorkingDirectory = `$d; CreateNoWindow = `$true }
    [void]`$jobs.Add([Diagnostics.Process]::Start(`$si))
}
foreach (`$j in `$jobs) { `$j.WaitForExit() }
exit 0
"@
    $WorkerFile = "$Tmp.worker.ps1"
    [IO.File]::WriteAllText($WorkerFile, $Worker)
    # The VS shell writes notices to stderr; whether each mutant compiled is judged from its own t.exe below.
    $ErrorActionPreference = 'Continue'
    $compiler = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $VsCompile -BuildCmd "powershell -NoProfile -ExecutionPolicy Bypass -File `"$WorkerFile`"" 2>&1 | Out-String
    $ErrorActionPreference = 'Stop'
    $tCompile = $Clock.Elapsed.TotalSeconds

    # Collects what one run left behind.  Judgement is Get-MutantVerdict's alone.
    $run = {
        param($d)
        $e = @{ Compiled = (Test-Path -LiteralPath "$d\t.exe"); Launched = $false; TimedOut = $false; ExitCode = $null; Output = $null; Launch = '' }
        if (-not $e.Compiled) { return $e }
        try {
            # An explicit path: cmd may not search the current directory.  Not Start-Process: a fast exit can leave ExitCode empty.
            $p = [Diagnostics.Process]::Start([Diagnostics.ProcessStartInfo]@{ FileName = 'cmd.exe'; Arguments = "/c `"`"$d\t.exe`" >`"$d\out.txt`" 2>`"$d\err.txt`"`""; UseShellExecute = $false; WorkingDirectory = $d; CreateNoWindow = $true })
            $e.Launched = $true
            if (-not $p.WaitForExit(30000)) { $e.TimedOut = $true; $p.Kill(); return $e }
            $e.ExitCode = $p.ExitCode
        } catch { $e.Launch = "$_"; return $e }
        if (Test-Path -LiteralPath "$d\err.txt") { $e.Launch = ([IO.File]::ReadAllText("$d\err.txt")).Trim() }
        if (Test-Path -LiteralPath "$d\out.txt") { $e.Output = [IO.File]::ReadAllText("$d\out.txt") }
        return $e
    }

    $Verdicts = @{ KILLED = 0; SURVIVED = 0; INFRA_FAILURE = 0; INCONCLUSIVE = 0 }
    $baseEv = & $run "$Tmp\baseline"
    $baseV = Get-MutantVerdict $baseEv
    $clLog = if (Test-Path -LiteralPath "$Tmp\baseline\cl.log") { (Get-Content -LiteralPath "$Tmp\baseline\cl.log" | Select-Object -First 6) -join ' | ' } else { $compiler.Trim() }
    Write-Host ("  [BASELINE] offline unmutated code: {0} ({1})" -f $baseV.Verdict, $baseV.Reason)
    tc_check 'TEST-TAS-LIFE-MUT-002-unmutated-code-passes' ($baseV.Verdict -eq 'SURVIVED') "baseline is $($baseV.Verdict): $($baseV.Reason).  $clLog  [$($baseEv.Launch)]"
    foreach ($id in ($names.Keys | Sort-Object)) {
        $v = Get-MutantVerdict (& $run "$Tmp\$id")
        $Verdicts[$v.Verdict]++
        Write-Host ("  [MUTANT] {0} {1}: {2} ({3})" -f $id, $v.Verdict, $names[$id], $v.Reason)
        tc_check "TEST-TAS-LIFE-MUT-$id $($names[$id])" ($v.Verdict -eq 'KILLED') "$($v.Verdict): $($v.Reason)"
    }
    Write-Host ("  [TIME] compile {0:N1} s, total {1:N1} s" -f $tCompile, $Clock.Elapsed.TotalSeconds)
    Write-Host ("  [MUTATION] killed={0} survived={1} infra_failure={2} inconclusive={3} of {4}" -f $Verdicts.KILLED, $Verdicts.SURVIVED, $Verdicts.INFRA_FAILURE, $Verdicts.INCONCLUSIVE, $names.Count)
    $complete = ($baseV.Verdict -eq 'SURVIVED' -and $names.Count -eq 65 -and $Verdicts.KILLED -eq 65)
    tc_check 'TEST-TAS-LIFE-MUT-003-complete-mutation-coverage' $complete "not complete: baseline $($baseV.Verdict), killed $($Verdicts.KILLED) of 65"
} finally {
    Remove-Item -LiteralPath $Tmp -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "$Tmp.worker.ps1" -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $Tmp) { Write-Host "  [CLEANUP_FAILED] TEST-TAS-LIFE-MUT: sandbox kept: $Tmp"; $failed++ }
}

Write-Host ""
Write-Host ("TEST-TAS-LIFE-MUT: {0} passed, {1} failed of {2} (offline, not HIL evidence)" -f $passed, $failed, $total) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
if ($failed) { exit 1 }
exit 0
