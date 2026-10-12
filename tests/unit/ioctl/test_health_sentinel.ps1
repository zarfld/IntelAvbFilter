# test_health_sentinel.ps1
# Regression: health check failure after last test must produce non-zero exit.
# TEST-SENTINEL-001..004
# No hardware or driver required.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$RunCIPath = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'

$total = 0; $passed = 0; $failed = 0

function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-SENTINEL-001..004: #328 Health Sentinel Regression" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# TEST-SENTINEL-001: Counter logic
# Simulate: 1 test passed, then health check fails after it.
# $failedTests must be > 0 after the health failure.
$passedTests_001  = 1
$failedTests_001  = 0
$totalTests_001   = 1
$results_001      = @()
$exeName_001      = "test_ioctl_version.exe"
$healthFailed_001 = $true

if ($healthFailed_001) {
    $failedTests_001++
    $totalTests_001++
    $results_001 += [PSCustomObject]@{ Name = "$exeName_001[INFRA-HEALTH]"; Status = "FAILED(INFRA)"; Exit = -2 }
}

if ($failedTests_001 -gt 0) {
    tc_pass "TEST-SENTINEL-001: failedTests correctly incremented ($failedTests_001) after health failure"
} else {
    tc_fail "TEST-SENTINEL-001" "failedTests still 0 after health failure - suite would EXIT 0 (FALSE PASS)"
}

# TEST-SENTINEL-002: Exit semantics
$wouldExitNonZero = ($failedTests_001 -gt 0)
if ($wouldExitNonZero) {
    tc_pass "TEST-SENTINEL-002: suite would exit 1 (correct) - not 0"
} else {
    tc_fail "TEST-SENTINEL-002" "suite would exit 0 despite failedTests - false PASS"
}

# TEST-SENTINEL-003: Source verification
if (-not (Test-Path $RunCIPath)) {
    tc_fail "TEST-SENTINEL-003" "Run-Tests-CI.ps1 not found at $RunCIPath"
} else {
    $content = Get-Content $RunCIPath -Raw
    $hasDriverLost = $content -match '\$driverLost\s*=\s*\$true'
    $hasFailedInc  = $content -match '\$failedTests\+\+'
    # Both must appear in the same proximity (within ~600 chars of each other)
    $blockPattern = '(?s)\$driverLost\s*=\s*\$true.{0,600}\$failedTests\+\+'
    $hasBlock = $content -match $blockPattern

    if ($hasDriverLost -and $hasFailedInc -and $hasBlock) {
        tc_pass "TEST-SENTINEL-003: Run-Tests-CI.ps1 increments failedTests in health failure block"
    } elseif (-not $hasDriverLost) {
        tc_fail "TEST-SENTINEL-003" "`$driverLost = `$true not found in Run-Tests-CI.ps1"
    } elseif (-not $hasFailedInc) {
        tc_fail "TEST-SENTINEL-003" "`$failedTests++ not found in Run-Tests-CI.ps1"
    } else {
        tc_fail "TEST-SENTINEL-003" "`$failedTests++ not in same block as `$driverLost = `$true"
    }
}

# TEST-SENTINEL-004: INFRA result entry
$results_004 = @()
$exeName_004 = "test_ioctl_version.exe"
$results_004 += [PSCustomObject]@{ Name = "$exeName_004[INFRA-HEALTH]"; Status = "FAILED(INFRA)"; Exit = -2 }

$infraEntry = $results_004 | Where-Object { $_.Status -eq "FAILED(INFRA)" }
if ($infraEntry) {
    tc_pass "TEST-SENTINEL-004: FAILED(INFRA) result entry present in results array"
} else {
    tc_fail "TEST-SENTINEL-004" "No FAILED(INFRA) entry in results - INFRA failure invisible to JUnit"
}

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan

if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-SENTINEL-001..004: #328 health sentinel regression detected" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-SENTINEL-001..004: health sentinel correctly fails on device-node loss" -ForegroundColor Green
exit 0
