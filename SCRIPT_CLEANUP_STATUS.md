# Script Cleanup Status - Issue #27

**Goal**: Consolidate 50+ redundant scripts into canonical implementations.

**Status**: Loop 2 (TEST category) COMPLETE ✅

---

## Category Summary

| Category | Total | Cleaned | Remaining | Status |
|----------|-------|---------|-----------|--------|
| **TEST** | 13 | 8 archived, 5 kept | 0 | ✅ COMPLETE |
| **BUILD** | 4 | - | 4 | ⏸️ Canonical (keep all) |
| **SETUP** | 5 | - | 5 | ⏸️ Need analysis |
| **DEVELOPMENT** | 11 | - | 11 | ❌ NOT STARTED |
| **ROOT** | 3 | - | 3 | ❌ NOT STARTED |
| **SCRIPTS/** | 4 | - | 4 | ❌ NOT STARTED |
| **TESTS/** | 2 | - | 2 | ❌ NOT STARTED |
| **EXTERNAL/** | ~40 | - | ~40 | 🚫 EXCLUDE (submodules) |
| **TOTAL (excluding external)** | ~42 | 8 | ~34 | 🔄 IN PROGRESS |

---

## ✅ COMPLETED: TEST Category (Loop 2 - 2025-12-28)

### Kept (5 scripts)
1. **Quick-Test-Debug.bat** (1,907 bytes) - Convenience wrapper
2. **Quick-Test-Release.bat** (2,260 bytes) - Convenience wrapper
3. **Run-Tests.ps1** (20,241 bytes) - **CANONICAL** test executor
4. **test_driver.ps1** (13,792 bytes) - Reference diagnostics
5. **Reboot-And-Test.ps1** (1,647 bytes) - Boot-time testing (specialized)

### Archived (8 scripts → tools/archive/deprecated/test/)
1. test_hardware_only.bat → `Run-Tests.ps1 -HardwareOnly`
2. run_complete_diagnostics.bat → `Run-Tests.ps1 -CompileDiagnostics`
3. test_secure_boot_compatible.bat → `Run-Tests.ps1 -SecureBootCheck`
4. run_tests.ps1 (lowercase) → `Run-Tests.ps1 -Full`
5. run_test_admin.ps1 → `Run-Tests.ps1 -TestExecutable`
6. test_local_i219.bat → Features in canonical scripts
7. Test-Release.bat → Quick-Test-Release.bat (obsolete)
8. Test-Real-Release.bat → Quick-Test-Release.bat (obsolete)

### Key Fixes
- ✅ Run-Tests.ps1 line 356: Fixed `-Quick` parameter
- ✅ Build-Tests.ps1: Output directory (repo root → build\tools\avb_test\x64\{Configuration}\)
- ✅ Quick-Test-*.bat: Path detection (repo root vs tools\test)

---

## ⏸️ BUILD Category (4 scripts - CANONICAL SET)

**Location**: `tools/build/`

All 4 scripts are canonical and should be KEPT:

1. **Build-Driver.ps1** (11,591 bytes) - **CANONICAL** driver build
2. **Build-Tests.ps1** (29,439 bytes) - **CANONICAL** test executable build
3. **Build-And-Sign.ps1** (11,384 bytes) - **CANONICAL** signing workflow
4. **Import-VisualStudioVars.ps1** (1,537 bytes) - Helper for environment setup

**Decision**: ✅ KEEP ALL (no cleanup needed, these are the canonical set)

---

## ⏸️ SETUP Category (5 scripts - NEED ANALYSIS)

**Location**: `tools/setup/`

1. **Install-Driver.ps1** (21,243 bytes) - **CANONICAL** driver installation
2. **Install-Driver-Elevated.ps1** (541 bytes) - UAC wrapper (already proven necessary)
3. **Setup-Driver.ps1** (6,687 bytes) - ❓ Analyze vs Install-Driver.ps1
4. **Install-Certificate.ps1** (8,810 bytes) - ❓ Check if integrated into Install-Driver.ps1
5. **troubleshoot_certificates.ps1** (14,961 bytes) - ❓ Diagnostic utility or duplicate?

**Next Action**: Compare Setup-Driver.ps1, Install-Certificate.ps1, troubleshoot_certificates.ps1 with Install-Driver.ps1 canonical.

---

## ❌ DEVELOPMENT Category (11 scripts - NOT STARTED)

**Location**: `tools/development/`

1. Check-System.ps1
2. diagnose_capabilities.ps1
3. enhanced_investigation_suite.ps1
4. fix_deployment_config.ps1
5. force_driver_reload.ps1
6. Force-Driver-Reload.ps1 (duplicate case?)
7. Force-StartDriver.ps1
8. IntelAvbFilter-Cleanup.ps1
9. quick_start.ps1
10. reinstall_debug_quick.bat
11. reinstall-and-test.bat
12. Smart-Update-Driver.bat
13. Start-AvbDriver.ps1
14. Update-Driver-Quick.bat

**Observation**: Multiple versions of driver reload/restart scripts. Likely duplicates.

**Next Action**: Apply Loop 1 + Loop 2 pattern (analyze all → identify canonical → test → archive duplicates).

---

## ❌ ROOT Directory Scripts (3 scripts - NOT STARTED)

**Location**: Repository root

1. **t.ps1** (304 bytes) - ❓ What is this?
2. **build_investigation_tools.cmd** (2,267 bytes) - ❓ Development utility?
3. **diagnostic_build.cmd** (1,801 bytes) - ❓ Build wrapper?

**Next Action**: Read each script, determine purpose, compare with canonical BUILD/DEVELOPMENT scripts.

---

## ❌ SCRIPTS/ Directory (4 scripts - NOT STARTED)

**Location**: `scripts/`

1. **build_and_test.ps1** - CI/CD workflow?
2. **create-labels.ps1** - GitHub automation
3. **create-labels-api.ps1** - GitHub automation (API version?)
4. **create-phase02-requirements.ps1** - Requirements generation

**Next Action**: Categorize as CI/CD vs Development vs Utilities.

---

## ❌ TESTS/ Directory (2 scripts - NOT STARTED)

**Location**: `tests/integration/avb/`

1. **build_nmake_temp.ps1** - Temporary build script?
2. **nm.ps1** - nmake wrapper?

**Next Action**: Check if replaced by Build-Tests.ps1 canonical.

---

## ❌ TOOLS/ Root (2 scripts - NOT STARTED)

**Location**: `tools/`

1. **build_tsauxc_test.ps1** - Test build script
2. **vs_compile.ps1** - Visual Studio compile wrapper

**Next Action**: Check if replaced by Build-Tests.ps1 or development scripts.

---

## 🚫 EXTERNAL/ Scripts (~40 scripts - EXCLUDE)

**Location**: `external/*/` (Git submodules)

- `external/intel_avb/lib/` - ~10 scripts (upstream repository)
- `external/windows_driver_samples/` - ~30 scripts (Microsoft samples)
- `external/intel_mfd/` - ~1 script (Intel MFD)

**Decision**: ✅ EXCLUDE from cleanup (belong to upstream repositories, managed via submodules).

---

## Cleanup Process (Two-Loop Pattern)

### Loop 1: Analysis
1. Read all scripts in category
2. Identify unique features
3. Find canonical script(s)
4. Document what each script does
5. Map features to canonical

### Loop 2: Test & Archive
1. Test old script functionality
2. Test canonical equivalent
3. Compare results
4. Archive if redundant
5. Update documentation

---

## Next Steps (Priority Order)

1. ✅ **TEST category** - COMPLETE
2. ⏸️ **SETUP category** - 3 scripts need analysis (Setup-Driver.ps1, Install-Certificate.ps1, troubleshoot_certificates.ps1)
3. ❌ **DEVELOPMENT category** - 11-14 scripts need Loop 1 + Loop 2
4. ❌ **ROOT scripts** - 3 scripts need categorization
5. ❌ **SCRIPTS/ directory** - 4 scripts need categorization (CI/CD vs utilities)
6. ❌ **TESTS/ directory** - 2 scripts check if replaced by Build-Tests.ps1
7. ❌ **TOOLS/ root** - 2 scripts check if replaced by canonical

**Estimated Total Cleanup**: ~34 scripts (excluding EXTERNAL submodules, BUILD canonical set already complete)

---

## Success Metrics

- **Before**: 50+ scripts scattered across repository
- **After Loop 2 (TEST)**: 13 → 5 kept, 8 archived
- **Target**: ~10-15 canonical scripts total (BUILD + SETUP + TEST + select DEVELOPMENT/UTILITIES)
- **Current Progress**: 8 archived, ~26 remaining to analyze

---

**Last Updated**: 2025-12-28 (Loop 2 TEST cleanup complete)
