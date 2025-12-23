# Script Consolidation Status - Issue #27

**ADR**: [ADR-SCRIPT-001: Consolidated Script Architecture](../03-architecture/decisions/ADR-SCRIPT-001-consolidated-script-architecture.md)  
**Issue**: #27 REQ-NF-SCRIPTS-001: Consolidate 80+ Redundant Scripts  
**Status**: Phase 1 Complete (Organization) | Phase 2 In Progress (Consolidation)

---

## Executive Summary

**Current State**: Scripts organized into `tools/` subdirectories  
**Target State**: 7 canonical scripts with deprecated wrappers for backward compatibility  
**Progress**: Phase 1 ✅ Complete | Phase 2 ⚠️ In Progress

---

## Phase 1: Organization (✅ COMPLETE)

**Goal**: Move all scripts from root to organized `tools/` structure  
**Status**: ✅ Completed (Commits: 2042c42, 0f60ea5, c5f6302, 04443c3)

**Result**:
- ✅ All 50+ scripts moved to tools/ subdirectories
- ✅ Root directory cleaned (no loose scripts)
- ✅ Path references updated to work from new locations
- ✅ All scripts tested before and after migration

**Directory Structure**:
```
tools/
├── setup/           (19 scripts)
├── build/           (9 scripts)
├── test/            (12 scripts)
├── development/     (13 scripts)
└── archive/
    └── deprecated/  (1 script: Nuclear-Install.bat)
```

---

## Phase 2: Consolidation (⚠️ IN PROGRESS)

**Goal**: Reduce 50+ scripts to 7 canonical entry points with parameter-driven design

### 🎯 Canonical "Go-To" Scripts (7 Total)

These are the **ONLY** scripts that should be used and maintained:

#### 1. **Setup-Driver.ps1** (Canonical Setup Script)
**Location**: `tools/setup/Setup-Driver.ps1`  
**Purpose**: Complete driver setup (test signing, installation, uninstall, update)  
**Status**: ⚠️ TO BE CREATED (consolidate 14 setup scripts)

**Consolidates**:
- `Complete-Driver-Setup.bat` - Full setup with certificate
- `Install-AvbFilter.ps1` - Driver installation
- `Install-Debug-Driver.bat` - Debug build install
- `Install-NewDriver.bat` - New driver install
- `Install-Now.bat` - Quick install
- `install_certificate_method.bat` - Cert-based install
- `install_devcon_method.bat` - Devcon install
- `install_filter_proper.bat` - Proper NDIS install
- `install_fixed_driver.bat` - Fixed driver install
- `install_ndis_filter.bat` - NDIS filter install
- `install_smart_test.bat` - Smart test install
- `Setup-Driver.bat` - Basic setup
- `setup_driver.ps1` - Alternative setup
- `Enable-TestSigning.bat` - Test signing helper

**Parameters**:
```powershell
Setup-Driver.ps1 [-Configuration Debug|Release] [-EnableTestSigning] [-Install] [-Uninstall] [-Update] [-Force]
```

**Replacement Examples**:
```powershell
# Old: Complete-Driver-Setup.bat
# New: Setup-Driver.ps1 -Configuration Release -Install -EnableTestSigning

# Old: Install-Debug-Driver.bat
# New: Setup-Driver.ps1 -Configuration Debug -Install

# Old: Enable-TestSigning.bat
# New: Setup-Driver.ps1 -EnableTestSigning
```

---

#### 2. **Build-Driver.ps1** (Canonical Build Script)
**Location**: `tools/build/Build-Driver.ps1`  
**Purpose**: Build and sign driver with all configurations  
**Status**: ⚠️ TO BE CREATED (consolidate 6 build scripts)

**Consolidates**:
- `Build-And-Sign-Driver.ps1` - Build + sign Release
- `Build-AllTests-Honest.ps1` - Build tests v1
- `Build-AllTests-TrulyHonest.ps1` - Build tests v2
- `build_i226_test.bat` - i226 test build
- `Fix-And-Install.bat` - Build + fix + install
- `fix_deployment_config.ps1` - Deployment config fix

**Parameters**:
```powershell
Build-Driver.ps1 [-Configuration Debug|Release] [-Sign] [-Tests] [-Device I210|I219|I226] [-Install]
```

**Replacement Examples**:
```powershell
# Old: Build-And-Sign-Driver.ps1
# New: Build-Driver.ps1 -Configuration Release -Sign

# Old: build_i226_test.bat
# New: Build-Driver.ps1 -Configuration Debug -Tests -Device I226

# Old: Fix-And-Install.bat
# New: Build-Driver.ps1 -Configuration Debug -Install
```

---

#### 3. **Test-Driver.ps1** (Canonical Test Script)
**Location**: `tools/test/Test-Driver.ps1`  
**Purpose**: Execute driver tests (quick, comprehensive, hardware-specific)  
**Status**: ⚠️ TO BE CREATED (consolidate 10 test scripts)

**Consolidates**:
- `run_tests.ps1` - Comprehensive test suite (ALREADY GOOD - can be renamed)
- `Quick-Test-Debug.bat` - Quick Debug test
- `Quick-Test-Release.bat` - Quick Release test
- `Test-Release.bat` - Release test
- `Test-Real-Release.bat` - Real release test
- `test_driver.ps1` - Driver test
- `test_hardware_only.bat` - Hardware-only test
- `test_local_i219.bat` - I219 local test
- `test_secure_boot_compatible.bat` - Secure boot test
- `run_complete_diagnostics.bat` - Full diagnostics

**Parameters**:
```powershell
Test-Driver.ps1 [-Configuration Debug|Release] [-Quick] [-HardwareOnly] [-Device I210|I219|I226] [-SecureBoot] [-Diagnostics]
```

**Replacement Examples**:
```powershell
# Old: Quick-Test-Debug.bat
# New: Test-Driver.ps1 -Configuration Debug -Quick

# Old: test_hardware_only.bat
# New: Test-Driver.ps1 -HardwareOnly

# Old: test_local_i219.bat
# New: Test-Driver.ps1 -Device I219

# Old: run_complete_diagnostics.bat
# New: Test-Driver.ps1 -Diagnostics
```

**Note**: `run_tests.ps1` is ALREADY well-structured and could become the canonical `Test-Driver.ps1` with minor renaming.

---

#### 4. **Manage-Certificate.ps1** (Canonical Certificate Script)
**Location**: `tools/setup/Manage-Certificate.ps1`  
**Purpose**: Certificate management (install, troubleshoot, validate)  
**Status**: ✅ ALREADY EXISTS (needs enhancement)

**Consolidates**:
- `troubleshoot_certificates.ps1` - Cert troubleshooting
- `Generate-CATFile.ps1` - CAT file generation

**Parameters**:
```powershell
Manage-Certificate.ps1 [-Install] [-Troubleshoot] [-GenerateCAT] [-CertPath <path>]
```

---

#### 5. **Sign-Driver.ps1** (Canonical Signing Script)
**Location**: `tools/build/Sign-Driver.ps1`  
**Purpose**: Sign driver packages  
**Status**: ✅ ALREADY EXISTS (good as-is)

**Consolidates**:
- `fix_test_signing.bat` - Test signing fix

**Parameters**: Already well-designed

---

#### 6. **Force-Driver-Reload.ps1** (Canonical Dev Script)
**Location**: `tools/development/Force-Driver-Reload.ps1`  
**Purpose**: Development shortcuts (reload, restart, status)  
**Status**: ✅ ALREADY EXISTS (needs enhancement)

**Consolidates**:
- `force_driver_reload.ps1` - Duplicate reload script
- `Force-StartDriver.ps1` - Force start
- `Start-AvbDriver.ps1` - Start driver
- `reinstall_debug_quick.bat` - Quick reinstall
- `reinstall-and-test.bat` - Reinstall + test
- `Smart-Update-Driver.bat` - Smart update
- `Update-Driver-Quick.bat` - Quick update

**Parameters**:
```powershell
Force-Driver-Reload.ps1 [-Reload] [-Restart] [-Update] [-Configuration Debug|Release] [-Test]
```

---

#### 7. **Check-Driver-Status.ps1** (Canonical Status Script)
**Location**: `tools/development/Check-Driver-Status.ps1`  
**Purpose**: Driver status checks and diagnostics  
**Status**: ✅ ALREADY EXISTS (needs enhancement)

**Consolidates**:
- `diagnose_capabilities.ps1` - Capability diagnostics
- `enhanced_investigation_suite.ps1` - Investigation tools
- `quick_start.ps1` - Quick start checks
- `Reboot-And-Test.ps1` - Reboot test

**Parameters**:
```powershell
Check-Driver-Status.ps1 [-Quick] [-Detailed] [-Capabilities] [-Investigation] [-PrepareReboot]
```

---

## 📋 Consolidation Checklist

### ⚠️ TO DO: Create Consolidated Scripts

- [ ] **Create `Setup-Driver.ps1`** - Consolidate 14 setup scripts with parameters
- [ ] **Create `Build-Driver.ps1`** - Consolidate 6 build scripts with parameters
- [ ] **Rename `run_tests.ps1` → `Test-Driver.ps1`** - Already good, just rename
- [ ] **Enhance `Manage-Certificate.ps1`** - Merge troubleshooting + CAT generation
- [ ] **Enhance `Force-Driver-Reload.ps1`** - Merge all reload/update variants
- [ ] **Enhance `Check-Driver-Status.ps1`** - Merge diagnostics and investigation

### ✅ ALREADY DONE

- [x] Scripts organized into `tools/` subdirectories
- [x] Root directory cleaned
- [x] Path references updated
- [x] `Sign-Driver.ps1` exists and works well

---

## 🚫 Scripts Marked for Deprecation (After Consolidation Complete)

Once the 7 canonical scripts are created, these scripts will be moved to `tools/archive/deprecated/` with deprecation wrappers:

### Setup Scripts (14 → deprecated):
- `Complete-Driver-Setup.bat`
- `Install-AvbFilter.ps1`
- `Install-Debug-Driver.bat`
- `Install-NewDriver.bat`
- `Install-Now.bat`
- `install_certificate_method.bat`
- `install_devcon_method.bat`
- `install_filter_proper.bat`
- `install_fixed_driver.bat`
- `install_ndis_filter.bat`
- `install_smart_test.bat`
- `Setup-Driver.bat`
- `setup_driver.ps1`
- `Enable-TestSigning.bat`

### Test Scripts (10 → deprecated):
- `Quick-Test-Debug.bat`
- `Quick-Test-Release.bat`
- `Test-Release.bat`
- `Test-Real-Release.bat`
- `test_driver.ps1`
- `test_hardware_only.bat`
- `test_local_i219.bat`
- `test_secure_boot_compatible.bat`
- `run_complete_diagnostics.bat`
- (Keep `run_tests.ps1` but rename to `Test-Driver.ps1`)

### Build Scripts (4 → deprecated):
- `Build-AllTests-Honest.ps1`
- `Build-AllTests-TrulyHonest.ps1`
- `build_i226_test.bat`
- `Fix-And-Install.bat`

### Development Scripts (9 → deprecated):
- `force_driver_reload.ps1` (duplicate)
- `Force-StartDriver.ps1`
- `Start-AvbDriver.ps1`
- `reinstall_debug_quick.bat`
- `reinstall-and-test.bat`
- `Smart-Update-Driver.bat`
- `Update-Driver-Quick.bat`
- `IntelAvbFilter-Cleanup.ps1`
- `Reboot-And-Test.ps1`

### Misc Scripts (5 → deprecated):
- `troubleshoot_certificates.ps1` (merge into Manage-Certificate.ps1)
- `Generate-CATFile.ps1` (merge into Manage-Certificate.ps1)
- `fix_deployment_config.ps1`
- `fix_test_signing.bat`
- `diagnose_capabilities.ps1`
- `enhanced_investigation_suite.ps1`
- `quick_start.ps1`

**Total to Deprecate**: ~40 scripts

---

## 🎯 Next Steps for Phase 2 Completion

1. **Create 3 new consolidated scripts**:
   - `tools/setup/Setup-Driver.ps1` (parameter-driven, consolidates 14 scripts)
   - `tools/build/Build-Driver.ps1` (parameter-driven, consolidates 6 scripts)
   - Rename `tools/test/run_tests.ps1` → `tools/test/Test-Driver.ps1`

2. **Enhance 3 existing scripts**:
   - `tools/setup/Manage-Certificate.ps1` (add troubleshooting + CAT generation)
   - `tools/development/Force-Driver-Reload.ps1` (merge all reload/update variants)
   - `tools/development/Check-Driver-Status.ps1` (merge diagnostics + investigation)

3. **Create deprecation wrappers** in `tools/archive/deprecated/` with warnings

4. **Update documentation**:
   - Update README.md with canonical script references
   - Create MIGRATION_GUIDE.md for users
   - Update .github/copilot-instructions.md to reference canonical scripts only

5. **Test consolidated scripts**:
   - Verify all use cases covered
   - Ensure parameters work correctly
   - Validate backward compatibility via deprecation wrappers

---

## Copilot Guidance

**⚠️ IMPORTANT**: When Copilot needs to perform any driver/build/test operation, it must **ONLY** use these 7 canonical scripts:

1. **Setup operations** → `tools/setup/Setup-Driver.ps1`
2. **Build operations** → `tools/build/Build-Driver.ps1`
3. **Test operations** → `tools/test/Test-Driver.ps1` (or `run_tests.ps1` until renamed)
4. **Certificate operations** → `tools/setup/Manage-Certificate.ps1`
5. **Signing operations** → `tools/build/Sign-Driver.ps1`
6. **Dev reload/update** → `tools/development/Force-Driver-Reload.ps1`
7. **Status checks** → `tools/development/Check-Driver-Status.ps1`

**❌ NEVER**:
- Create new scripts without explicit user approval
- Use deprecated scripts from `tools/archive/deprecated/`
- Reference scripts that don't exist in the canonical 7

**✅ ALWAYS**:
- Check this document before suggesting script usage
- Use parameters instead of creating new script variants
- Ask user to clarify which canonical script covers their use case

---

**Last Updated**: 2025-12-22  
**Maintained By**: Standards Compliance Team
