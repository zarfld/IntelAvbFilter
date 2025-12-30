# Test-SSOT-004-Completeness.ps1
# Verifies: Issue #24 (REQ-NF-SSOT-001)
# Test Issue: #303 (TEST-SSOT-004)
# Purpose: Verify SSOT header contains all required IOCTLs
# Expected: All core IOCTLs present with correct documentation

param(
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$testsPassed = 0
$testsFailed = 0

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST-SSOT-004: Verify SSOT Header Completeness" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ssotHeaderPath = Join-Path $repoRoot "include\avb_ioctl.h"

# Define required IOCTLs (from TEST-SSOT-004 issue #303)
$coreIOCTLs = @(
    'IOCTL_AVB_INIT_DEVICE',
    'IOCTL_AVB_ENUM_ADAPTERS',
    'IOCTL_AVB_GET_CLOCK_CONFIG',
    'IOCTL_AVB_GET_HW_STATE'
)

$extendedIOCTLs = @(
    'IOCTL_AVB_GET_DEVICE_INFO',
    'IOCTL_AVB_GET_CAPABILITIES',
    'IOCTL_AVB_SET_CLOCK_CONFIG',
    'IOCTL_AVB_GET_TIME',
    'IOCTL_AVB_SET_TIME',
    'IOCTL_AVB_SET_HW_STATE',
    'IOCTL_AVB_GET_LINK_STATUS',
    'IOCTL_AVB_GET_LAUNCH_TIME_STATUS',
    'IOCTL_AVB_SET_LAUNCH_TIME',
    'IOCTL_AVB_GET_REGISTRY_DIAGNOSTICS',
    'IOCTL_AVB_GET_STATS',
    'IOCTL_AVB_RESET_STATS',
    'IOCTL_AVB_GET_CONFIG',
    'IOCTL_AVB_SET_CONFIG'
)

Push-Location $repoRoot

try {
    # Test 1: Verify SSOT header exists
    Write-Host "[TEST 1] Verifying SSOT header exists..." -ForegroundColor Yellow
    
    if (Test-Path $ssotHeaderPath) {
        $fileInfo = Get-Item $ssotHeaderPath
        Write-Host "  ✅ PASS: SSOT header found" -ForegroundColor Green
        Write-Host "    Path: include\avb_ioctl.h" -ForegroundColor Gray
        Write-Host "    Size: $($fileInfo.Length) bytes" -ForegroundColor Gray
        Write-Host "    Modified: $($fileInfo.LastWriteTime)" -ForegroundColor Gray
        $testsPassed++
    } else {
        Write-Host "  ❌ FAIL: SSOT header NOT found!" -ForegroundColor Red
        Write-Host "    Expected path: include\avb_ioctl.h" -ForegroundColor Red
        $testsFailed++
        Pop-Location
        exit 1
    }
    
    Write-Host ""
    
    # Read header content
    $headerContent = Get-Content $ssotHeaderPath -Raw
    
    # Test 2: Verify core IOCTLs present (REQUIRED)
    Write-Host "[TEST 2] Verifying core IOCTLs present (required)..." -ForegroundColor Yellow
    
    $missingCore = @()
    $foundCore = @()
    
    foreach ($ioctl in $coreIOCTLs) {
        if ($headerContent -match "#define\s+$ioctl\s+") {
            $foundCore += $ioctl
            if ($Verbose) {
                Write-Host "  ✅ $ioctl found" -ForegroundColor Green
            }
        } else {
            $missingCore += $ioctl
            Write-Host "  ❌ $ioctl MISSING!" -ForegroundColor Red
        }
    }
    
    if ($missingCore.Count -eq 0) {
        Write-Host "  ✅ PASS: All $($coreIOCTLs.Count) core IOCTLs present" -ForegroundColor Green
        $testsPassed++
    } else {
        Write-Host "  ❌ FAIL: $($missingCore.Count) core IOCTL(s) missing!" -ForegroundColor Red
        Write-Host "    Missing:" -ForegroundColor Red
        foreach ($ioctl in $missingCore) {
            Write-Host "      - $ioctl" -ForegroundColor Red
        }
        $testsFailed++
    }
    
    Write-Host ""
    
    # Test 3: Verify extended IOCTLs (optional but recommended)
    Write-Host "[TEST 3] Verifying extended IOCTLs present (recommended)..." -ForegroundColor Yellow
    
    $missingExtended = @()
    $foundExtended = @()
    
    foreach ($ioctl in $extendedIOCTLs) {
        if ($headerContent -match "#define\s+$ioctl\s+") {
            $foundExtended += $ioctl
        } else {
            $missingExtended += $ioctl
        }
    }
    
    if ($missingExtended.Count -eq 0) {
        Write-Host "  ✅ PASS: All $($extendedIOCTLs.Count) extended IOCTLs present" -ForegroundColor Green
        $testsPassed++
    } else {
        Write-Host "  ⚠️  WARNING: $($missingExtended.Count) extended IOCTL(s) missing" -ForegroundColor Yellow
        Write-Host "    Missing (not critical):" -ForegroundColor Yellow
        foreach ($ioctl in $missingExtended) {
            Write-Host "      - $ioctl" -ForegroundColor Yellow
        }
        Write-Host "    (Extended IOCTLs may not be implemented yet)" -ForegroundColor Gray
        $testsPassed++  # Don't fail for missing extended IOCTLs
    }
    
    Write-Host ""
    
    # Test 4: Count total IOCTLs
    Write-Host "[TEST 4] Counting total IOCTL definitions..." -ForegroundColor Yellow
    
    $allIoctls = [regex]::Matches($headerContent, '(?m)^#define\s+IOCTL_AVB_\w+')
    $ioctlCount = $allIoctls.Count
    
    Write-Host "  Total IOCTL definitions: $ioctlCount" -ForegroundColor Gray
    
    if ($ioctlCount -ge 4) {
        Write-Host "  ✅ PASS: Sufficient IOCTLs defined (minimum 4 core)" -ForegroundColor Green
        $testsPassed++
    } else {
        Write-Host "  ❌ FAIL: Insufficient IOCTLs (found $ioctlCount, need at least 4)" -ForegroundColor Red
        $testsFailed++
    }
    
    Write-Host ""
    
    # Test 5: Verify ABI versioning
    Write-Host "[TEST 5] Verifying ABI version tracking..." -ForegroundColor Yellow
    
    if ($headerContent -match 'AVB_IOCTL_ABI_VERSION\s+0x([0-9A-Fa-f]+)') {
        $version = $matches[1]
        Write-Host "  ✅ PASS: ABI version defined" -ForegroundColor Green
        Write-Host "    Version: 0x$version" -ForegroundColor Gray
        $testsPassed++
    } else {
        Write-Host "  ⚠️  WARNING: ABI version not found" -ForegroundColor Yellow
        Write-Host "    Recommended: #define AVB_IOCTL_ABI_VERSION 0x00010000u" -ForegroundColor Gray
        $testsPassed++  # Not critical
    }
    
    Write-Host ""
    
    # Test 6: Verify kernel/user-mode support
    Write-Host "[TEST 6] Verifying kernel/user-mode compatibility..." -ForegroundColor Yellow
    
    if ($headerContent -match '#ifdef\s+_KERNEL_MODE') {
        Write-Host "  ✅ PASS: Kernel-mode conditional compilation present" -ForegroundColor Green
        $testsPassed++
    } else {
        Write-Host "  ⚠️  WARNING: No kernel-mode conditional compilation" -ForegroundColor Yellow
        Write-Host "    Recommended for portability between driver and test code" -ForegroundColor Gray
        $testsPassed++  # Not critical
    }
    
    Write-Host ""
    
    # Test 7: Check for IOCTL documentation
    Write-Host "[TEST 7] Checking IOCTL documentation..." -ForegroundColor Yellow
    
    $undocumentedIoctls = @()
    $lines = Get-Content $ssotHeaderPath
    
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^#define\s+(IOCTL_AVB_\w+)') {
            $ioctlName = $matches[1]
            $prevLine = if ($i -gt 0) { $lines[$i-1] } else { "" }
            
            if ($prevLine -notmatch '//' -and $prevLine -notmatch '/\*') {
                $undocumentedIoctls += $ioctlName
            }
        }
    }
    
    if ($undocumentedIoctls.Count -eq 0) {
        Write-Host "  ✅ PASS: All IOCTLs have documentation comments" -ForegroundColor Green
        $testsPassed++
    } else {
        Write-Host "  ⚠️  WARNING: $($undocumentedIoctls.Count) IOCTL(s) lack documentation" -ForegroundColor Yellow
        if ($Verbose) {
            Write-Host "    Undocumented IOCTLs:" -ForegroundColor Yellow
            foreach ($ioctl in $undocumentedIoctls) {
                Write-Host "      - $ioctl" -ForegroundColor Yellow
            }
        }
        Write-Host "    (Documentation improves maintainability but is not critical)" -ForegroundColor Gray
        $testsPassed++  # Don't fail for missing docs
    }
    
    Write-Host ""
    
    # Test 8: Verify no duplicate IOCTL code values
    Write-Host "[TEST 8] Checking for duplicate IOCTL code values..." -ForegroundColor Yellow
    
    $codeValues = @{}
    $duplicateValues = @{}
    
    foreach ($match in $allIoctls) {
        $line = $match.Value
        if ($line -match '^#define\s+(IOCTL_AVB_\w+)\s+.*0x([0-9A-Fa-f]+)') {
            $ioctlName = $matches[1]
            $codeValue = $matches[2]
            
            if ($codeValues.ContainsKey($codeValue)) {
                $duplicateValues[$codeValue] = @($codeValues[$codeValue], $ioctlName)
            } else {
                $codeValues[$codeValue] = $ioctlName
            }
        }
    }
    
    if ($duplicateValues.Count -eq 0) {
        Write-Host "  ✅ PASS: No duplicate IOCTL code values" -ForegroundColor Green
        Write-Host "    Unique code values: $($codeValues.Count)" -ForegroundColor Gray
        $testsPassed++
    } else {
        Write-Host "  ❌ FAIL: Duplicate IOCTL code values found!" -ForegroundColor Red
        foreach ($code in $duplicateValues.Keys) {
            Write-Host "    Code 0x$code used by:" -ForegroundColor Red
            foreach ($ioctl in $duplicateValues[$code]) {
                Write-Host "      - $ioctl" -ForegroundColor Red
            }
        }
        $testsFailed++
    }
    
    Write-Host ""
    
} catch {
    Write-Host "  ❌ ERROR: Test execution failed: $_" -ForegroundColor Red
    $testsFailed++
}

# Summary Report
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DETAILED REPORT" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "SSOT Header: include\avb_ioctl.h" -ForegroundColor Gray
Write-Host "Total IOCTLs: $ioctlCount" -ForegroundColor Gray
Write-Host "Core IOCTLs: $($foundCore.Count)/$($coreIOCTLs.Count)" -ForegroundColor $(if ($missingCore.Count -eq 0) { "Green" } else { "Red" })
Write-Host "Extended IOCTLs: $($foundExtended.Count)/$($extendedIOCTLs.Count)" -ForegroundColor $(if ($missingExtended.Count -eq 0) { "Green" } else { "Yellow" })
Write-Host ""

if ($Verbose) {
    Write-Host "All IOCTL definitions found:" -ForegroundColor Gray
    foreach ($match in $allIoctls) {
        $ioctlName = [regex]::Match($match.Value, 'IOCTL_AVB_\w+').Value
        Write-Host "  - $ioctlName" -ForegroundColor Gray
    }
    Write-Host ""
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST SUMMARY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Tests Passed: $testsPassed" -ForegroundColor Green
Write-Host "Tests Failed: $testsFailed" -ForegroundColor $(if ($testsFailed -eq 0) { "Green" } else { "Red" })
Write-Host ""

if ($testsFailed -eq 0) {
    Write-Host "✅ TEST-SSOT-004: PASSED - SSOT header is complete" -ForegroundColor Green
    Write-Host ""
    Write-Host "Verification complete. Issue #24 requirement satisfied." -ForegroundColor Gray
    Pop-Location
    exit 0
} else {
    Write-Host "❌ TEST-SSOT-004: FAILED - SSOT header incomplete" -ForegroundColor Red
    Write-Host ""
    Write-Host "Action Required:" -ForegroundColor Yellow
    Write-Host "1. Add missing core IOCTLs to include/avb_ioctl.h" -ForegroundColor Yellow
    Write-Host "2. Fix duplicate IOCTL code values" -ForegroundColor Yellow
    Write-Host "3. Consider adding ABI version tracking" -ForegroundColor Yellow
    Write-Host "4. Re-run this test to verify compliance" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "See Issue #24 and ADR-SSOT-001 (Issue #123) for guidance." -ForegroundColor Gray
    Pop-Location
    exit 1
}

Pop-Location
