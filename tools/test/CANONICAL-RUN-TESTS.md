# Canonical Script Architecture - TEST CATEGORY

**Date**: 2025-12-28  
**Status**: Run-Tests.ps1 -Quick parameter FIXED (line 356)

## Single Canonical Test Script

### Run-Tests.ps1 (PRIMARY)
**Location**: `tools/test/Run-Tests.ps1`  
**Lines**: 521  
**Purpose**: Comprehensive test executor with multiple modes

**Usage**:
```powershell
# Quick verification (2 tests: capability + diagnostic)
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick

# Full test suite (6 phases: Architecture → Investigation)
.\tools\test\Run-Tests.ps1 -Configuration Debug -Full

# Specific test
.\tools\test\Run-Tests.ps1 -Configuration Debug -TestExecutable avb_test_i226.exe

# Hardware-only compile
.\tools\test\Run-Tests.ps1 -HardwareOnly

# Compile diagnostics + system checks
.\tools\test\Run-Tests.ps1 -CompileDiagnostics

# Secure Boot compatibility check
.\tools\test\Run-Tests.ps1 -SecureBootCheck
```

**Parameters**:
- `-Configuration Debug|Release` - Build configuration to test
- `-Quick` - Run fast verification (capability + diagnostic tests) ✅ **NOW WORKING**
- `-Full` - Run comprehensive 6-phase test suite
- `-SkipHardwareCheck` - Skip hardware enumeration
- `-CollectLogs` - Save event logs for analysis
- `-TestExecutable <name>` - Run specific test by name
- `-HardwareOnly` - Compile with `/DHARDWARE_ONLY=1` flag
- `-CompileDiagnostics` - Compile diagnostic tools + system capability analysis
- `-SecureBootCheck` - Check Secure Boot status, install test certificate

**Test Coverage (-Quick)**:
1. `avb_capability_validation_test.exe` - Verify realistic hardware capability reporting
2. `avb_diagnostic_test.exe` - Comprehensive hardware diagnostics

**Test Coverage (-Full)**: 6 Phases
- **Phase 0**: Architecture Compliance (capability + device separation)
- **Phase 1**: Basic Hardware Diagnostics (diagnostic + hw_state)
- **Phase 2**: TSN IOCTL Handler Verification (test_tsn_ioctl_handlers + tsn_hardware_activation_validation)
- **Phase 3**: Device-Specific Testing (avb_i226_test + avb_i226_advanced_test + critical_prerequisites_investigation + enhanced_tas_investigation)
- **Phase 4**: Advanced TSN Feature Validation (chatgpt5_i226_tas_validation + corrected_i226_tas_test)
- **Phase 5**: Investigation Tools (hardware_investigation_tool)

## Reference Implementation (KEEP for educational purposes)

### test_driver.ps1
**Location**: `tools/test/test_driver.ps1`  
**Lines**: 349  
**Purpose**: Reference diagnostic implementation with detailed examples

**Features**:
- Service status checks
- Hardware enumeration (detailed VID/DID parsing)
- Device node access patterns
- Event log analysis
- Interactive test runner

**Why Keep**: Shows best practices for diagnostics, educational value

## Bug Fix Applied (2025-12-28)

**Issue**: `-Quick` parameter defined but never used (copy-paste bug)

**Before** (Line 356):
```powershell
} elseif ($Full) {
    # Quick tests only  <-- WRONG COMMENT
    Write-Step "Running Quick Verification Tests"
```

**After** (Line 356):
```powershell
} elseif ($Quick) {
    # Quick tests only  <-- NOW CORRECT
    Write-Step "Running Quick Verification Tests"
```

**Impact**: `-Quick` parameter now works correctly, runs 2 fast verification tests

## Proof of Fix

**Command**:
```powershell
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
```

**Expected Output**:
```
================================================================
  Intel AVB Filter Driver - Test Suite
  Configuration: Debug
================================================================

==> Checking Driver Service Status
[OK] Service found: Running

==> Enumerating Intel Network Adapters
[OK] Found 1 Intel network adapter(s)
  Device: Intel I226-V 2.5GbE Network Connection

==> Checking Test Executables
  [OK] avb_capability_validation_test.exe (46592 bytes)
  [OK] avb_diagnostic_test.exe (663040 bytes)

==> Running Quick Verification Tests

===============================================
Running: avb_capability_validation_test.exe
===============================================
(test output...)

===============================================
Running: avb_diagnostic_test.exe
===============================================
(test output...)
```

**Result**: ✅ `-Quick` parameter functional after line 356 fix

## Convenience Wrappers (Keep)

These scripts wrap Run-Tests.ps1 for common workflows:

### Quick-Test-Debug.bat
```batch
# Combines: Install-Driver.ps1 -Reinstall + Run-Tests.ps1 -Quick
# Purpose: Fast reload cycle during active development
# Workflow: Unbind → Stop → Install → Test (single command)
```

### Quick-Test-Release.bat
```batch
# Combines: Install-Driver.ps1 -Reinstall + Run-Tests.ps1 -Quick
# Purpose: Same as Debug but with Release build
```

**Why Keep Wrappers**:
- Developer convenience (single command vs 2 commands)
- Fast iteration cycle (10 seconds: reload + test)
- Path flexibility (detects repo root vs tools\test)

## Canonical Architecture Summary

**Complete Workflow**:
```powershell
# 1. Build
.\tools\build\Build-Driver.ps1 -Configuration Debug

# 2. Install
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall

# 3. Test
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
```

**Single Canonical per Category**:
- Build: `Build-Driver.ps1` ✅
- Install: `Install-Driver.ps1` ✅
- Test: **`Run-Tests.ps1`** ✅ **THIS DOCUMENT**

**Reference Implementations** (educational, not duplicates):
- Build Tests: `Build-Tests.ps1` (for user-mode test executables)
- Test Diagnostics: `test_driver.ps1` (detailed diagnostic examples)

## Files Modified (2025-12-28)

1. **Run-Tests.ps1** - Fixed line 356 (`elseif ($Full)` → `elseif ($Quick)`)
2. **Build-Tests.ps1** - Fixed output directory (repo root → `build\tools\avb_test\x64\{Configuration}\`)
3. **Quick-Test-Debug.bat** - Fixed paths, added directory detection
4. **Quick-Test-Release.bat** - Fixed paths, added directory detection

## Next Steps (Loop 2 Continuation)

- [x] Quick-Test-Debug.bat - ✅ Tested, KEEP as wrapper
- [ ] Quick-Test-Release.bat - Test and compare
- [ ] test_hardware_only.bat - Test with Run-Tests.ps1 -HardwareOnly
- [ ] run_complete_diagnostics.bat - Test with Run-Tests.ps1 -CompileDiagnostics
- [ ] test_secure_boot_compatible.bat - Test with Run-Tests.ps1 -SecureBootCheck
- [ ] run_tests.ps1 (lowercase) - Compare phase execution
- [ ] run_test_admin.ps1 - Compare UAC + test execution
- [ ] test_local_i219.bat - Compare bcdedit + pnputil
- [ ] Test-Release.bat - Compare with Quick-Test-Release.bat

**Archival Criteria**: Old script functionality fully covered by canonical (Run-Tests.ps1)
