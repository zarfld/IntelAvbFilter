<#
.SYNOPSIS
    Regression tests for Parse-TestResults.ps1 aggregation semantics.

.DESCRIPTION
    Verifies that:
    - INFRA_FAILURE always produces a JUnit <failure/> element (never a pass)
    - SKIP_UNSUPPORTED produces a JUnit <skipped/> element (never a pass)
    - Generic FAIL produces a JUnit <failure/> element
    - PASS produces a clean pass element
    - BLOCKED, CLEANUP_FAILED produce <failure/> elements

    These tests run without hardware or a loaded driver.

$RepoRoot   = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
    Implements: TEST-PROV-L06 (Parse-TestResults aggregation invariants)
    Verifies:   #199, #317 (no false-pass from INFRA_FAILURE)
    Related:    #328 (driver-loss must not produce passing test cases)
#>

Write-Host "  TEST-PROV-L06 - no hardware required" -ForegroundColor Cyan
$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$ParseScript = Join-Path $RepoRoot 'tools\test\Parse-TestResults.ps1'
$TempDir     = Join-Path $env:TEMP "parse_results_regression_$(Get-Random)"
New-Item -ItemType Directory -Path $TempDir | Out-Null

$total  = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host "  [PASS] $n" -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

function Invoke-ParseTest {
    param([string]$LogContent, [string]$ExpectedXmlFragment, [string]$TestName)
    $logFile = Join-Path $TempDir "$TestName.log"
    $xmlFile = Join-Path $TempDir "$TestName.xml"
    $LogContent | Set-Content -Path $logFile -Encoding UTF8

    & $ParseScript -LogFile $logFile -OutputFile $xmlFile 2>$null
    if (-not (Test-Path $xmlFile)) {
        tc_fail $TestName "Parse-TestResults.ps1 produced no XML output"
        return
    }
    $xml = Get-Content $xmlFile -Raw
    if ($xml -like "*$ExpectedXmlFragment*") {
        tc_pass $TestName
    } else {
        tc_fail $TestName "XML did not contain [$ExpectedXmlFragment].`nActual XML:`n$xml"
    }
}

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  Parse-TestResults Aggregation Regression" -ForegroundColor Cyan
Write-Host "  TEST-PROV-L06 â€” no hardware required" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-01: INFRA_FAILURE must produce <failure/>, never a pass
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-01-infra-failure-is-failure" `
  -LogContent "  [INFRA_FAILURE] TEST-PROV-H01: driver absent on mandatory HIL runner" `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-02: SKIP_UNSUPPORTED must produce <skipped/>, never a pass or failure
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-02-skip-unsupported-is-skip" `
  -LogContent "  [SKIP_UNSUPPORTED] TEST-PROV-H03: adapter lacks INTEL_CAP_BASIC_1588" `
  -ExpectedXmlFragment "<skipped"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-03: Mix of INFRA_FAILURE and PASS â†’ still failure (any INFRA_FAILURE wins)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-03-infra-plus-pass-is-failure" `
  -LogContent @"
  [PASS] TEST-PROV-H01/adapter:0: no self-comparison detected
  [INFRA_FAILURE] TEST-PROV-H01/adapter:1: driver lost mid-suite
"@ `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-04: BLOCKED must produce <failure/> (prerequisite not met is blocking)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-04-blocked-is-failure" `
  -LogContent "  [BLOCKED] TEST-PROV-H05: required test executable not found" `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-05: CLEANUP_FAILED must produce <failure/>
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-05-cleanup-failed-is-failure" `
  -LogContent "  [CLEANUP_FAILED] UT-CORR-005: PHC restore failed - hardware state uncertain" `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-06: Pure PASS produces no <failure/> and no <skipped/>
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$logFile06 = Join-Path $TempDir "L06-06-pass.log"
$xmlFile06 = Join-Path $TempDir "L06-06-pass.xml"
"  [PASS] TEST-PROV-H01: no self-comparison regression detected" | Set-Content $logFile06 -Encoding UTF8
& $ParseScript -LogFile $logFile06 -OutputFile $xmlFile06 2>$null
if (Test-Path $xmlFile06) {
    $xml06 = Get-Content $xmlFile06 -Raw
    if ($xml06 -match 'failures="0"' -and $xml06 -match 'skipped="0"') {
        tc_pass "L06-06-pure-pass-no-failure-no-skip"
    } else {
        tc_fail "L06-06-pure-pass-no-failure-no-skip" "PASS result incorrectly produced failure/skip element"
    }
} else {
    tc_fail "L06-06-pure-pass-no-failure-no-skip" "No XML output for PASS log"
}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-07: INFRA_FAILURE in mandatory HIL context â€” outcome must be failures=1
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$logFile07 = Join-Path $TempDir "L06-07-infra.log"
$xmlFile07 = Join-Path $TempDir "L06-07-infra.xml"
"  [INFRA_FAILURE] TEST-PROV-H02/adapter:0: no packets sent on capable hardware" | Set-Content $logFile07 -Encoding UTF8
& $ParseScript -LogFile $logFile07 -OutputFile $xmlFile07 2>$null
if (Test-Path $xmlFile07) {
    $xml07 = Get-Content $xmlFile07 -Raw
    if ($xml07 -match 'failures="1"' -and $xml07 -notmatch 'failures="0"') {
        tc_pass "L06-07-infra-failure-count-is-1"
    } else {
        tc_fail "L06-07-infra-failure-count-is-1" "failures attribute is not 1 in XML`n$xml07"
    }
} else {
    tc_fail "L06-07-infra-failure-count-is-1" "No XML output"
}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# Cleanup
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  Summary: $passed/$total passed, $failed failed" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-PROV-L06: $failed aggregation regression(s) detected" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-PROV-L06: all aggregation invariants correct" -ForegroundColor Green
exit 0
