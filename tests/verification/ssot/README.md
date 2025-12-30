# SSOT Verification Tests

**Purpose**: Automated test scripts to verify compliance with **Issue #24 (REQ-NF-SSOT-001)** - Single Source of Truth for IOCTL Interface.

**Architecture**: See **Issue #123 (ADR-SSOT-001)** for architectural decision rationale.

## Test Coverage

This directory contains test scripts for the following TEST issues:

### TEST-SSOT-001: No Duplicate IOCTL Definitions (#301)

**Script**: `Test-SSOT-001-NoDuplicates.ps1`

**Purpose**: Verify that IOCTL definitions exist ONLY in `include/avb_ioctl.h` and nowhere else.

**Usage**:
```powershell
# Basic run
.\Test-SSOT-001-NoDuplicates.ps1

# Verbose output
.\Test-SSOT-001-NoDuplicates.ps1 -Verbose
```

**Pass Criteria**:
- ✅ All `IOCTL_AVB_*` definitions in `include/avb_ioctl.h` only
- ✅ `_NDIS_CONTROL_CODE` macro defined in exactly one location
- ✅ SSOT header exists

**Expected Output**:
```
========================================
TEST-SSOT-001: Verify No Duplicate IOCTL Definitions
========================================

[TEST 1] Searching for IOCTL_AVB_* definitions...
  ✅ PASS: All IOCTL definitions in SSOT header only
    Location: include\avb_ioctl.h
    Count: 20 definitions

[TEST 2] Searching for _NDIS_CONTROL_CODE macro duplicates...
  ✅ PASS: _NDIS_CONTROL_CODE defined in exactly one location
    Location: include\avb_ioctl.h

[TEST 3] Verifying SSOT header exists...
  ✅ PASS: SSOT header exists
    Path: include\avb_ioctl.h
    Size: 8192 bytes

========================================
TEST SUMMARY
========================================
Tests Passed: 3
Tests Failed: 0

✅ TEST-SSOT-001: PASSED - No duplicate IOCTL definitions
```

---

### TEST-SSOT-002: CI Workflow Catches Violations (#300)

**Purpose**: Verify that `.github/workflows/check-ssot.yml` correctly detects and rejects SSOT violations.

**⚠️ This is a NEGATIVE TEST** - requires creating intentional violations to verify CI catches them.

**Manual Test Procedure**:

1. **Create test branch**:
   ```powershell
   git checkout -b test-ssot-violation
   ```

2. **Introduce violation** (edit any test file):
   ```c
   // Add to tests/unit/ioctl/test_minimal_ioctl.c
   #define IOCTL_AVB_INIT_DEVICE 0x00000001  // DUPLICATE - SHOULD FAIL CI
   ```

3. **Commit and push**:
   ```powershell
   git add tests/unit/ioctl/test_minimal_ioctl.c
   git commit -m "TEST: Introduce SSOT violation (should fail CI)"
   git push origin test-ssot-violation
   ```

4. **Verify CI fails**:
   - Go to GitHub Actions
   - Find workflow run for `test-ssot-violation` branch
   - **Expected**: ❌ Workflow FAILS with error message identifying duplicate

5. **Expected Error**:
   ```
   ❌ Error: Duplicate IOCTL definitions found outside SSOT header!
   Found definitions in:
     - include/avb_ioctl.h (SSOT - correct)
     - tests/unit/ioctl/test_minimal_ioctl.c (VIOLATION)
   
   All IOCTL definitions must be in include/avb_ioctl.h only.
   See Issue #24 for SSOT requirements.
   ```

6. **Clean up**:
   ```powershell
   git checkout master
   git branch -D test-ssot-violation
   git push origin --delete test-ssot-violation
   ```

**Pass Criteria**:
- ✅ CI workflow fails when duplicate IOCTLs introduced
- ✅ Error message clearly identifies violation
- ✅ Workflow passes on clean code

---

### TEST-SSOT-003: All Files Use SSOT Header Include (#302)

**Script**: `Test-SSOT-003-IncludePattern.ps1`

**Purpose**: Verify that all files using IOCTLs include the SSOT header `include/avb_ioctl.h`.

**Usage**:
```powershell
# Basic run
.\Test-SSOT-003-IncludePattern.ps1

# Verbose output (shows all files)
.\Test-SSOT-003-IncludePattern.ps1 -Verbose
```

**Pass Criteria**:
- ✅ All files using `IOCTL_AVB_*` include `include/avb_ioctl.h`
- ✅ Include paths are correct (resolve to SSOT header)
- ⚠️ SSOT comment present (recommended but not required)

**Expected Output**:
```
========================================
TEST-SSOT-003: Verify All Files Use SSOT Header Include
========================================

[STEP 1] Finding files that use IOCTL_AVB_* definitions...
  Found 6 file(s) using IOCTLs

[STEP 2] Verifying SSOT header includes...

[TEST 1] Checking include statement presence...
  ✅ PASS: All files include SSOT header
    Files checked: 6
    Files with include: 6

[TEST 2] Checking for SSOT reference comments...
  ✅ PASS: All includes have SSOT comment

[TEST 3] Verifying include path correctness...
  ✅ PASS: All include paths are correct

========================================
DETAILED REPORT
========================================
Total files using IOCTLs: 6
Files with correct include: 6
Files with include (no comment): 0
Files missing include: 0

========================================
TEST SUMMARY
========================================
Tests Passed: 3
Tests Failed: 0

✅ TEST-SSOT-003: PASSED - All files use SSOT header correctly
```

---

### TEST-SSOT-004: SSOT Header Completeness (#303)

**Script**: `Test-SSOT-004-Completeness.ps1`

**Purpose**: Verify that `include/avb_ioctl.h` contains all required IOCTL definitions with proper documentation.

**Usage**:
```powershell
# Basic run
.\Test-SSOT-004-Completeness.ps1

# Verbose output (lists all IOCTLs)
.\Test-SSOT-004-Completeness.ps1 -Verbose
```

**Pass Criteria**:
- ✅ SSOT header exists at `include/avb_ioctl.h`
- ✅ All 4 core IOCTLs present (INIT_DEVICE, ENUM_ADAPTERS, GET_CLOCK_CONFIG, GET_HW_STATE)
- ⚠️ Extended IOCTLs present (recommended)
- ⚠️ ABI versioning defined (recommended)
- ⚠️ Kernel/user-mode support (recommended)
- ✅ No duplicate IOCTL code values
- ⚠️ IOCTLs documented (recommended)

**Expected Output**:
```
========================================
TEST-SSOT-004: Verify SSOT Header Completeness
========================================

[TEST 1] Verifying SSOT header exists...
  ✅ PASS: SSOT header found
    Path: include\avb_ioctl.h
    Size: 8192 bytes
    Modified: 2025-12-30 10:30:00

[TEST 2] Verifying core IOCTLs present (required)...
  ✅ PASS: All 4 core IOCTLs present

[TEST 3] Verifying extended IOCTLs present (recommended)...
  ✅ PASS: All 14 extended IOCTLs present

[TEST 4] Counting total IOCTL definitions...
  Total IOCTL definitions: 20
  ✅ PASS: Sufficient IOCTLs defined (minimum 4 core)

[TEST 5] Verifying ABI version tracking...
  ✅ PASS: ABI version defined
    Version: 0x00010000

[TEST 6] Verifying kernel/user-mode compatibility...
  ✅ PASS: Kernel-mode conditional compilation present

[TEST 7] Checking IOCTL documentation...
  ✅ PASS: All IOCTLs have documentation comments

[TEST 8] Checking for duplicate IOCTL code values...
  ✅ PASS: No duplicate IOCTL code values
    Unique code values: 20

========================================
DETAILED REPORT
========================================
SSOT Header: include\avb_ioctl.h
Total IOCTLs: 20
Core IOCTLs: 4/4
Extended IOCTLs: 14/14

========================================
TEST SUMMARY
========================================
Tests Passed: 8
Tests Failed: 0

✅ TEST-SSOT-004: PASSED - SSOT header is complete
```

---

## Running All Tests

To run all SSOT verification tests:

```powershell
# Run all tests sequentially
.\Test-SSOT-001-NoDuplicates.ps1
.\Test-SSOT-003-IncludePattern.ps1
.\Test-SSOT-004-Completeness.ps1

# TEST-SSOT-002 requires manual negative testing (see above)
```

Or create a master test runner:

```powershell
# Run-All-SSOT-Tests.ps1
$tests = @(
    "Test-SSOT-001-NoDuplicates.ps1",
    "Test-SSOT-003-IncludePattern.ps1",
    "Test-SSOT-004-Completeness.ps1"
)

$allPassed = $true
foreach ($test in $tests) {
    Write-Host "Running $test..." -ForegroundColor Cyan
    & ".\$test"
    if ($LASTEXITCODE -ne 0) {
        $allPassed = $false
    }
    Write-Host ""
}

if ($allPassed) {
    Write-Host "✅ ALL SSOT TESTS PASSED" -ForegroundColor Green
    exit 0
} else {
    Write-Host "❌ SOME SSOT TESTS FAILED" -ForegroundColor Red
    exit 1
}
```

---

## CI Integration

The CI workflow `.github/workflows/check-ssot.yml` automatically runs validation equivalent to:
- TEST-SSOT-001 (no duplicates)
- TEST-SSOT-004 (core IOCTLs present)

**Triggers**: Every push to master/develop, all PRs

**CI validation is MANDATORY** - PRs cannot merge if SSOT violations detected.

---

## Traceability

| Test Script | TEST Issue | Requirement | Architecture |
|-------------|-----------|-------------|--------------|
| Test-SSOT-001-NoDuplicates.ps1 | #301 | #24 (REQ-NF-SSOT-001) | #123 (ADR-SSOT-001) |
| Test-SSOT-002 (Manual) | #300 | #24 (REQ-NF-SSOT-001) | #123 (ADR-SSOT-001) |
| Test-SSOT-003-IncludePattern.ps1 | #302 | #24 (REQ-NF-SSOT-001) | #123 (ADR-SSOT-001) |
| Test-SSOT-004-Completeness.ps1 | #303 | #24 (REQ-NF-SSOT-001) | #123 (ADR-SSOT-001) |

---

## Standards Compliance

- **ISO/IEC/IEEE 12207:2017**: Configuration Management (6.3.5)
- **IEEE 1012-2016**: Verification and Validation
- **XP Practices**: Test-Driven Development, Continuous Integration

---

## Maintenance

**When to Run**: 
- ✅ Before closing Issue #24
- ✅ After adding new IOCTL definitions
- ✅ After modifying IOCTL code values
- ✅ During code review of IOCTL-related changes
- ✅ As part of regression testing

**Version**: 1.0  
**Created**: 2025-12-30  
**Updated**: 2025-12-30
