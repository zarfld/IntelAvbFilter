# Run-All-SSOT-Tests.ps1
# Master test runner for all SSOT verification tests
# Verifies: Issue #24 (REQ-NF-SSOT-001)
# Test Issues: #301, #302, #303 (automated), #300 (manual)

param(
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Magenta
Write-Host "SSOT VERIFICATION TEST SUITE" -ForegroundColor Magenta
Write-Host "========================================" -ForegroundColor Magenta
Write-Host "Verifying: Issue #24 (REQ-NF-SSOT-001)" -ForegroundColor Gray
Write-Host "Architecture: Issue #123 (ADR-SSOT-001)" -ForegroundColor Gray
Write-Host ""

$scriptDir = $PSScriptRoot
$testResults = @()

# Define tests to run
$automatedTests = @(
    @{
        Name = "TEST-SSOT-001"
        Script = "Test-SSOT-001-NoDuplicates.ps1"
        Description = "Verify No Duplicate IOCTL Definitions"
        Issue = "#301"
    },
    @{
        Name = "TEST-SSOT-003"
        Script = "Test-SSOT-003-IncludePattern.ps1"
        Description = "Verify All Files Use SSOT Header Include"
        Issue = "#302"
    },
    @{
        Name = "TEST-SSOT-004"
        Script = "Test-SSOT-004-Completeness.ps1"
        Description = "Verify SSOT Header Contains All Required IOCTLs"
        Issue = "#303"
    }
)

# Run each automated test
$testNumber = 1
foreach ($test in $automatedTests) {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "[$testNumber/$($automatedTests.Count)] Running $($test.Name)..." -ForegroundColor Cyan
    Write-Host "Description: $($test.Description)" -ForegroundColor Gray
    Write-Host "Issue: $($test.Issue)" -ForegroundColor Gray
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    $scriptPath = Join-Path $scriptDir $test.Script
    
    try {
        if (Test-Path $scriptPath) {
            if ($Verbose) {
                & $scriptPath -Verbose
            } else {
                & $scriptPath
            }
            
            $exitCode = $LASTEXITCODE
            
            if ($exitCode -eq 0) {
                $testResults += @{
                    Name = $test.Name
                    Status = "PASSED"
                    Issue = $test.Issue
                }
            } else {
                $testResults += @{
                    Name = $test.Name
                    Status = "FAILED"
                    Issue = $test.Issue
                }
            }
        } else {
            Write-Host "⚠️  WARNING: Test script not found: $($test.Script)" -ForegroundColor Yellow
            $testResults += @{
                Name = $test.Name
                Status = "SKIPPED"
                Issue = $test.Issue
            }
        }
    } catch {
        Write-Host "❌ ERROR running $($test.Name): $_" -ForegroundColor Red
        $testResults += @{
            Name = $test.Name
            Status = "ERROR"
            Issue = $test.Issue
        }
    }
    
    Write-Host ""
    Write-Host ""
    $testNumber++
}

# Manual test reminder
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MANUAL TEST REQUIRED" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST-SSOT-002: Verify CI Workflow Catches Violations (Issue #300)" -ForegroundColor Yellow
Write-Host ""
Write-Host "This test requires manual negative testing:" -ForegroundColor Gray
Write-Host "1. Create test branch: git checkout -b test-ssot-violation" -ForegroundColor Gray
Write-Host "2. Introduce duplicate IOCTL in any test file" -ForegroundColor Gray
Write-Host "3. Commit and push to trigger CI" -ForegroundColor Gray
Write-Host "4. Verify CI FAILS with clear error message" -ForegroundColor Gray
Write-Host "5. Clean up test branch" -ForegroundColor Gray
Write-Host ""
Write-Host "See tests/verification/ssot/README.md for detailed procedure." -ForegroundColor Gray
Write-Host ""
Write-Host ""

# Summary Report
Write-Host "========================================" -ForegroundColor Magenta
Write-Host "FINAL TEST SUMMARY" -ForegroundColor Magenta
Write-Host "========================================" -ForegroundColor Magenta
Write-Host ""

$passedCount = ($testResults | Where-Object { $_.Status -eq "PASSED" }).Count
$failedCount = ($testResults | Where-Object { $_.Status -eq "FAILED" }).Count
$skippedCount = ($testResults | Where-Object { $_.Status -eq "SKIPPED" }).Count
$errorCount = ($testResults | Where-Object { $_.Status -eq "ERROR" }).Count

foreach ($result in $testResults) {
    $color = switch ($result.Status) {
        "PASSED" { "Green" }
        "FAILED" { "Red" }
        "SKIPPED" { "Yellow" }
        "ERROR" { "Red" }
    }
    
    $icon = switch ($result.Status) {
        "PASSED" { "✅" }
        "FAILED" { "❌" }
        "SKIPPED" { "⏭️ " }
        "ERROR" { "💥" }
    }
    
    Write-Host "$icon $($result.Name) ($($result.Issue)): $($result.Status)" -ForegroundColor $color
}

Write-Host ""
Write-Host "Automated Tests Run: $($testResults.Count)" -ForegroundColor Gray
Write-Host "Passed: $passedCount" -ForegroundColor Green
Write-Host "Failed: $failedCount" -ForegroundColor $(if ($failedCount -eq 0) { "Green" } else { "Red" })
Write-Host "Skipped: $skippedCount" -ForegroundColor $(if ($skippedCount -eq 0) { "Green" } else { "Yellow" })
Write-Host "Errors: $errorCount" -ForegroundColor $(if ($errorCount -eq 0) { "Green" } else { "Red" })
Write-Host ""
Write-Host "⚠️  Manual Test Required: TEST-SSOT-002 (Issue #300)" -ForegroundColor Yellow
Write-Host ""

if ($failedCount -eq 0 -and $errorCount -eq 0) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "✅ ALL AUTOMATED SSOT TESTS PASSED" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Issue #24 (REQ-NF-SSOT-001) verification successful!" -ForegroundColor Gray
    Write-Host "Don't forget to manually run TEST-SSOT-002 for complete coverage." -ForegroundColor Yellow
    Write-Host ""
    exit 0
} else {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "❌ SOME SSOT TESTS FAILED" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Action Required:" -ForegroundColor Yellow
    Write-Host "1. Review failed test output above" -ForegroundColor Yellow
    Write-Host "2. Fix SSOT compliance issues" -ForegroundColor Yellow
    Write-Host "3. Re-run this test suite" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "See Issue #24 and ADR-SSOT-001 (Issue #123) for guidance." -ForegroundColor Gray
    Write-Host ""
    exit 1
}
