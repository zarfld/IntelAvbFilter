# test_no_interactive_waits.ps1
# Regression test: confirm no CI/HIL test executable source file contains an
# unconditional interactive wait (system("pause"), unconditional _getch(), etc.)
# that would hang an unattended run.
#
# Evidence: October 11 HIL log showed a 65m55s gap caused by system("pause")
# in intel_avb_diagnostics.c â€” fixed in commit after b968cb1.
#
# This test is purely file-system logic: no device or driver required.
#
# Test ID: TEST-NOWAIT-001
# Verifies: unattended-safe diagnostic executables (CI/HIL quality gate)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0

function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-NOWAIT-001: No interactive waits in CI/HIL sources" -ForegroundColor Cyan
Write-Host "  Evidence: 65m55s hang from system(pause) in Oct-11 log" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# Directories/files that are compiled into CI/HIL executables.
# External/ and worktree/ are excluded â€” those files are not compiled by Build-Tests.ps1.
$SearchPaths = @(
    (Join-Path $RepoRoot "tests"),
    (Join-Path $RepoRoot "tools\diagnostics")
)

# Patterns that unconditionally block unattended execution.
# Commented-out occurrences (// or /* prefix) are acceptable.
$BlockingPatterns = @(
    @{ Pattern = 'system\s*\(\s*"pause"'; Label = 'system("pause")' },
    @{ Pattern = '(?<!//\s*)(?<!/\*\s*)_getch\s*\(\s*\)'; Label = 'unconditional _getch()' }
)

$violations = [System.Collections.Generic.List[string]]::new()

foreach ($dir in $SearchPaths) {
    $files = Get-ChildItem -Path $dir -Include "*.c","*.cpp" -Recurse -ErrorAction SilentlyContinue |
             Where-Object { $_.FullName -notmatch '\\external\\|\\worktrees?\\|\\archive\\' }
    foreach ($f in $files) {
        $content = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
        if (-not $content) { continue }
        foreach ($bp in $BlockingPatterns) {
            $matches_ = [System.Text.RegularExpressions.Regex]::Matches($content, $bp.Pattern)
            foreach ($m in $matches_) {
                # Find line number
                $lineNum = ($content.Substring(0, $m.Index) -split "`n").Count
                $line    = ($content -split "`n")[$lineNum - 1].Trim()
                # Skip commented-out occurrences
                if ($line -match "^\s*(/[/*])" ) { continue }  # // or /* comment start
                $rel = $f.FullName.Substring($RepoRoot.Length + 1)
                $violations.Add(("{0}:{1}: {2}" -f $rel, $lineNum, $bp.Label))
            }
        }
    }
}

if ($violations.Count -eq 0) {
    tc_pass "TEST-NOWAIT-001: no unconditional interactive waits found in CI/HIL test sources"
} else {
    $msg = "$($violations.Count) violation(s):`n" + ($violations -join "`n")
    tc_fail "TEST-NOWAIT-001" $msg
}

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan

if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-NOWAIT-001: interactive-wait regression detected" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-NOWAIT-001: all CI/HIL sources are unattended-safe" -ForegroundColor Green
exit 0
