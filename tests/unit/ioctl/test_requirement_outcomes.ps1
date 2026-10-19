# test_requirement_outcomes.ps1
# TEST-COVERAGE-VERDICT-001..006: scripts/requirement_test_outcomes.py classifies requirements from
# the traceability matrix that Convert-Logs-To-JUnit.ps1 writes (generated here, not hand-written),
# so a requirement carrying SKIP_NOT_IMPLEMENTED / INCONCLUSIVE is INCOMPLETE and never verified.
#
# Implements: test verdict taxonomy (docs/testing/test-verdict-contract.md)
# Verifies:   #340 (fail-closed verification)
# No hardware, no driver.  Requires python on PATH (CI Windows runners have it); missing python is a FAILURE.

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$Convert   = Join-Path $RepoRoot 'tools\test\Convert-Logs-To-JUnit.ps1'
$Script    = Join-Path $RepoRoot 'scripts\requirement_test_outcomes.py'

$total = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; Write-Host "  [PASS] $n" -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host "  [FAIL] ${n}: $why" -ForegroundColor Red }

Write-Host "  TEST-COVERAGE-VERDICT-001..006 - requirement outcome classification (no hardware)" -ForegroundColor Cyan

$py = $null
foreach ($c in 'python', 'py') { if (Get-Command $c -ErrorAction SilentlyContinue) { $py = $c; break } }
if (-not $py) { tc_fail 'TEST-COVERAGE-VERDICT-SETUP' 'python not found on PATH'; exit 1 }

$Tmp = Join-Path $env:TEMP ("req_outcomes_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

# Build a matrix with the real converter from synthetic logs: one requirement per scenario.
function New-Matrix {
    param([string]$Name, [string]$Req, [string]$LogBody)
    $dir = Join-Path $Tmp $Name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Set-Content (Join-Path $dir 'suite.exe_20260101_000000.log') -Value "  Verifies: #$Req (REQ-X)`n$LogBody" -Encoding UTF8
    & $Convert -LogsDir $dir -RunDate 20260101 -OutputFile (Join-Path $dir 'o.xml') *>$null
    return $dir
}
function Run-Outcomes { param([string]$Dir, [switch]$Strict)
    $a = @($Script, $Dir); if ($Strict) { $a += '--fail-on-incomplete' }
    $ErrorActionPreference = 'Continue'   # PS 5.1 turns native stderr into a terminating error under 'Stop'
    $out = & $py @a 2>&1 | Out-String
    return @{ Out = $out; Exit = $LASTEXITCODE }
}

$d1 = New-Matrix 'verified'   '101' "  [PASS] TC-A-1/adapter:0: ok`n  [SKIP_UNSUPPORTED] TC-A-1/adapter:1: ntstatus=0xC00000BB"
$d2 = New-Matrix 'incomplete' '102' "  [PASS] TC-B-1/adapter:0: ok`n  [SKIP_NOT_IMPLEMENTED] TC-B-2/adapter:0: ntstatus=0xC0000002"
$d3 = New-Matrix 'inconcl'    '103' "  [PASS] TC-C-1/adapter:0: ok`n  [INCONCLUSIVE] TC-C-2/adapter:0: capability unknown"
$d4 = New-Matrix 'failing'    '104' "  [PASS] TC-D-1/adapter:0: ok`n  [FAIL] TC-D-2/adapter:0: wrong"
$d5 = New-Matrix 'skiponly'   '105' "  [SKIP_UNSUPPORTED] TC-E-1/adapter:0: ntstatus=0xC00000BB"

$r = Run-Outcomes $d1 -Strict
if ($r.Exit -eq 0 -and $r.Out -match 'Verified: 1' -and $r.Out -notmatch 'Incomplete requirements') { tc_pass 'TEST-COVERAGE-VERDICT-001-pass-plus-genuine-unsupported-is-verified' }
else { tc_fail 'TEST-COVERAGE-VERDICT-001' "exit=$($r.Exit)`n$($r.Out)" }

$r = Run-Outcomes $d2
if ($r.Exit -eq 0 -and $r.Out -match 'Incomplete: 1' -and $r.Out -match 'Incomplete requirements: #102' -and $r.Out -match 'Verified: 0') { tc_pass 'TEST-COVERAGE-VERDICT-002-not-implemented-requirement-is-incomplete-not-verified' }
else { tc_fail 'TEST-COVERAGE-VERDICT-002' "exit=$($r.Exit)`n$($r.Out)" }

$r = Run-Outcomes $d2 -Strict
if ($r.Exit -eq 1) { tc_pass 'TEST-COVERAGE-VERDICT-003-strict-mode-fails-on-incomplete' } else { tc_fail 'TEST-COVERAGE-VERDICT-003' "exit=$($r.Exit) expected 1" }

$r = Run-Outcomes $d3
if ($r.Out -match 'Incomplete requirements: #103' -and $r.Out -match 'Verified: 0') { tc_pass 'TEST-COVERAGE-VERDICT-004-inconclusive-requirement-is-incomplete-not-verified' }
else { tc_fail 'TEST-COVERAGE-VERDICT-004' $r.Out }

$r = Run-Outcomes $d4 -Strict
if ($r.Exit -eq 1 -and $r.Out -match 'Failing requirements: #104') { tc_pass 'TEST-COVERAGE-VERDICT-005-failure-is-failing-and-strict-fails' } else { tc_fail 'TEST-COVERAGE-VERDICT-005' "exit=$($r.Exit)`n$($r.Out)" }

$r = Run-Outcomes $d5
if ($r.Out -match 'Skip-only \(no evidence\): 1' -and $r.Out -match 'Verified: 0') { tc_pass 'TEST-COVERAGE-VERDICT-006-skip-only-is-not-verified' } else { tc_fail 'TEST-COVERAGE-VERDICT-006' $r.Out }

# Legacy 3-count rows (matrix written before this change) still parse
$legacy = Join-Path $Tmp 'legacy'; New-Item -ItemType Directory -Path $legacy -Force | Out-Null
@'
| Requirement | Test Suite | Test Issue | :white_check_mark: PASS | :x: FAIL | :next_track_button: SKIP |
|-------------|------------|:----------:|:-----------------------:|:--------:|:------------------------:|
| [#7](https://x/7) | s | [#1](https://x/1) | 2 | 0 | 1 |
'@ | Set-Content (Join-Path $legacy 'traceability-summary-20260101.md') -Encoding UTF8
$r = Run-Outcomes $legacy
if ($r.Out -match 'Verified: 1') { tc_pass 'TEST-COVERAGE-VERDICT-007-legacy-3-count-rows-still-parse' } else { tc_fail 'TEST-COVERAGE-VERDICT-007' $r.Out }

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "  Summary: $($total - $failed)/$total passed, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0
