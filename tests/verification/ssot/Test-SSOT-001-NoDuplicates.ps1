# Test-SSOT-001-NoDuplicates.ps1
# Verifies: Issue #24 (REQ-NF-SSOT-001)
# Test Issue: #301 (TEST-SSOT-001)
# Purpose: Verify NO duplicate IOCTL definitions exist in codebase
# Expected: All IOCTL definitions ONLY in include/avb_ioctl.h

param(
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$testsPassed = 0
$testsFailed = 0

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST-SSOT-001: Verify No Duplicate IOCTL Definitions" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Test 1: Check for duplicate IOCTL_AVB_* definitions
Write-Host "[TEST 1] Searching for IOCTL_AVB_* definitions..." -ForegroundColor Yellow

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
Push-Location $repoRoot

try {
    # Find all IOCTL definitions (exclude external/ and archive/ directories)
    $ioctlMatches = Get-ChildItem -Recurse -Include *.c,*.h,*.cpp |
        Select-String -Pattern "^#define\s+IOCTL_AVB_" |
        Where-Object { 
            $_.Path -notmatch "\\build\\" -and 
            $_.Path -notmatch "\\\.git\\" -and
            $_.Path -notmatch "\\external\\" -and
            $_.Path -notmatch "\\archive\\"
        }
    
    if ($ioctlMatches.Count -eq 0) {
        Write-Host "  ❌ FAIL: No IOCTL definitions found at all!" -ForegroundColor Red
        $testsFailed++
    } else {
        # Group by file
        $fileGroups = $ioctlMatches | Group-Object Path
        
        if ($Verbose) {
            Write-Host "  Found $($ioctlMatches.Count) IOCTL definitions in $($fileGroups.Count) file(s)" -ForegroundColor Gray
        }
        
        # Check if all are in SSOT header
        $ssotPath = Join-Path $repoRoot "include\avb_ioctl.h"
        $nonSsotFiles = $fileGroups | Where-Object { $_.Name -ne $ssotPath }
        
        if ($nonSsotFiles.Count -eq 0) {
            Write-Host "  ✅ PASS: All IOCTL definitions in SSOT header only" -ForegroundColor Green
            Write-Host "    Location: include\avb_ioctl.h" -ForegroundColor Gray
            Write-Host "    Count: $($ioctlMatches.Count) definitions" -ForegroundColor Gray
            $testsPassed++
        } else {
            Write-Host "  ❌ FAIL: Duplicate IOCTL definitions found outside SSOT!" -ForegroundColor Red
            Write-Host "    SSOT Header: include\avb_ioctl.h ($($fileGroups | Where-Object { $_.Name -eq $ssotPath } | Select-Object -ExpandProperty Count)) definitions" -ForegroundColor Yellow
            Write-Host "    VIOLATIONS:" -ForegroundColor Red
            foreach ($file in $nonSsotFiles) {
                $relativePath = $file.Name.Replace($repoRoot, "").TrimStart("\")
                Write-Host "      - $relativePath ($($file.Count) definitions)" -ForegroundColor Red
                
                if ($Verbose) {
                    foreach ($match in $file.Group) {
                        Write-Host "        Line $($match.LineNumber): $($match.Line.Trim())" -ForegroundColor Gray
                    }
                }
            }
            $testsFailed++
        }
    }
} catch {
    Write-Host "  ❌ ERROR: Test execution failed: $_" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# Test 2: Check for duplicate _NDIS_CONTROL_CODE macro
Write-Host "[TEST 2] Searching for _NDIS_CONTROL_CODE macro duplicates..." -ForegroundColor Yellow

try {
    $macroMatches = Get-ChildItem -Recurse -Include *.c,*.h,*.cpp |
        Select-String -Pattern "^#define\s+_NDIS_CONTROL_CODE" |
        Where-Object { 
            $_.Path -notmatch "\\build\\" -and 
            $_.Path -notmatch "\\\.git\\" -and
            $_.Path -notmatch "\\external\\" -and
            $_.Path -notmatch "\\archive\\"
        }
    
    $macroFileGroups = $macroMatches | Group-Object Path
    
    if ($macroFileGroups.Count -eq 0) {
        Write-Host "  ⚠️  WARNING: _NDIS_CONTROL_CODE macro not found" -ForegroundColor Yellow
        # Not a failure - macro might be in Windows headers
    } elseif ($macroFileGroups.Count -eq 1) {
        Write-Host "  ✅ PASS: _NDIS_CONTROL_CODE defined in exactly one location" -ForegroundColor Green
        $relativePath = $macroFileGroups[0].Name.Replace($repoRoot, "").TrimStart("\")
        Write-Host "    Location: $relativePath" -ForegroundColor Gray
        $testsPassed++
    } else {
        Write-Host "  ❌ FAIL: _NDIS_CONTROL_CODE macro duplicated!" -ForegroundColor Red
        foreach ($file in $macroFileGroups) {
            $relativePath = $file.Name.Replace($repoRoot, "").TrimStart("\")
            Write-Host "    - $relativePath" -ForegroundColor Red
        }
        $testsFailed++
    }
} catch {
    Write-Host "  ❌ ERROR: Test execution failed: $_" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# Test 3: Verify SSOT header exists
Write-Host "[TEST 3] Verifying SSOT header exists..." -ForegroundColor Yellow

$ssotHeaderPath = Join-Path $repoRoot "include\avb_ioctl.h"
if (Test-Path $ssotHeaderPath) {
    $fileSize = (Get-Item $ssotHeaderPath).Length
    Write-Host "  ✅ PASS: SSOT header exists" -ForegroundColor Green
    Write-Host "    Path: include\avb_ioctl.h" -ForegroundColor Gray
    Write-Host "    Size: $fileSize bytes" -ForegroundColor Gray
    $testsPassed++
} else {
    Write-Host "  ❌ FAIL: SSOT header NOT found at include\avb_ioctl.h" -ForegroundColor Red
    $testsFailed++
}

Write-Host ""

# Summary
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TEST SUMMARY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Tests Passed: $testsPassed" -ForegroundColor Green
Write-Host "Tests Failed: $testsFailed" -ForegroundColor $(if ($testsFailed -eq 0) { "Green" } else { "Red" })
Write-Host ""

if ($testsFailed -eq 0) {
    Write-Host "✅ TEST-SSOT-001: PASSED - No duplicate IOCTL definitions" -ForegroundColor Green
    Write-Host ""
    Write-Host "Verification complete. Issue #24 requirement satisfied." -ForegroundColor Gray
    exit 0
} else {
    Write-Host "❌ TEST-SSOT-001: FAILED - Duplicate definitions detected" -ForegroundColor Red
    Write-Host ""
    Write-Host "Action Required:" -ForegroundColor Yellow
    Write-Host "1. Remove duplicate IOCTL definitions from files listed above" -ForegroundColor Yellow
    Write-Host "2. Add include for SSOT header: #include ""include/avb_ioctl.h""" -ForegroundColor Yellow
    Write-Host "3. Re-run this test to verify compliance" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "See Issue #24 and ADR-SSOT-001 (Issue #123) for guidance." -ForegroundColor Gray
    exit 1
}

Pop-Location
