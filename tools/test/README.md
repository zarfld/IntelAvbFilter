# Test Scripts - SINGLE CANONICAL SCRIPT

**Separation of Concerns**:
- **TEST** (this folder): Test execution only (assume driver installed)
- **BUILD**: `tools/build/Build-Driver.ps1`, `Build-Tests.ps1`
- **INSTALL/RELOAD**: `tools/setup/Install-Driver.ps1`
- **BOOT CONFIG**: `tools/setup/` (not TEST concern)

---

## ✅ CANONICAL TEST SCRIPT (SINGLE SCRIPT)

### Run-Tests.ps1 (615 lines - 100% Feature Complete)

**ALL test functionality consolidated into this single script.**

```powershell
# Quick test (2 tests - fast verification)
.\Run-Tests.ps1 -Configuration Debug -Quick

# Full test (6 phases - comprehensive)
.\Run-Tests.ps1 -Configuration Debug -Full

# Run specific test
.\Run-Tests.ps1 -Configuration Debug -TestExecutable avb_test_i226.exe

# Default (no mode) - runs all 16 tests
.\Run-Tests.ps1 -Configuration Debug
```

**Parameters**:
- `-Configuration Debug|Release` - Build configuration
- `-Quick` - Fast verification (capability + diagnostic)
- `-Full` - All 6 phases (Architecture → Investigation)
- `-SkipHardwareCheck` - Skip Intel adapter detection
- `-CollectLogs` - Save event logs with timestamp
- `-TestExecutable <name>` - Run specific test
- `-HardwareOnly` - Compile with `/DHARDWARE_ONLY=1`
- `-CompileDiagnostics` - Compile diagnostic tools, check Hyper-V/SignTool/Inf2Cat
- `-SecureBootCheck` - Check Secure Boot status and certificate validity

**Features** (22 total - 100% complete):
1. ✅ Admin privilege check
2. ✅ Helper functions (Write-Step/Success/Failure/Info)
3. ✅ Driver service status check
4. ✅ Intel adapter enumeration with VID/DID parsing
5. ✅ Device name identification (I210/I217/I219/I226/I350)
6. ✅ Supported device check
7. ✅ Device node access test
8. ✅ Test executable availability
9. ✅ Quick mode (2 tests)
10. ✅ Full mode (6 phases)
11. ✅ All tests mode (16 tests)
12. ✅ Event log collection
13. ✅ **Test Summary** (status table with [OK]/[FAIL])
14. ✅ **Overall Assessment** (diagnosis + next steps)
15. ✅ HardwareOnly compilation mode
16. ✅ CompileDiagnostics mode
17. ✅ SecureBootCheck mode
18. ✅ Parameter validation
19. ✅ Error handling and diagnostics
20. ✅ DebugView recommendation
21. ✅ Timestamp-based log files
22. ✅ Multi-adapter support

---

## 🗂️ Archived Scripts (Final Consolidation - 2025-12-28)

**Location**: `tools/archive/deprecated/test/`

All functionality integrated into **SINGLE canonical Run-Tests.ps1** (615 lines, 22 features).

### Loop 2 Archival (8 scripts)

| Script | Canonical Replacement | Archival Reason |
|--------|----------------------|-----------------|
| **test_hardware_only.bat** | `Run-Tests.ps1 -HardwareOnly` | Feature integrated as parameter |
| **run_complete_diagnostics.bat** | `Run-Tests.ps1 -CompileDiagnostics` | Feature integrated as parameter |
| **test_secure_boot_compatible.bat** | `Run-Tests.ps1 -SecureBootCheck` | Feature integrated as parameter |
| **run_tests.ps1** (lowercase) | `Run-Tests.ps1 -Full` | Exact duplicate (6-phase executor) |
| **run_test_admin.ps1** | `Run-Tests.ps1` | Redundant UAC wrapper (built-in admin check) |
| **test_local_i219.bat** | `Install-Driver.ps1` + `Run-Tests.ps1` | Mixes INSTALL + TEST concerns |
| **Test-Release.bat** | `Quick-Test-Release.bat` | Obsolete (wrong paths: `x64\` vs `build\x64\`) |
| **Test-Real-Release.bat** | `Quick-Test-Release.bat` | Duplicate of Test-Release.bat |

### Final Consolidation (4 scripts)

| Script | Canonical Replacement | Archival Reason |
|--------|----------------------|-----------------|
| **Quick-Test-Debug.bat** | `Install-Driver.ps1` + `Run-Tests.ps1 -Quick` | **Violates separation**: Mixes INSTALL (netcfg -u, net stop, netcfg -v -l) with TEST |
| **Quick-Test-Release.bat** | `Install-Driver.ps1` + `Run-Tests.ps1 -Quick` | **Violates separation**: Mixes .sys copy + INSTALL + TEST |
| **Reboot-And-Test.ps1** | Use `tools/setup/` scripts | **Wrong category**: Boot config (sc.exe config start=boot) is SETUP concern, not TEST |
| **test_driver.ps1** (349 lines) | `Run-Tests.ps1` | **100% feature coverage**: All 22 features integrated (proven via comparison table) |

**Separation of Concerns Principle**:
- ✅ **TEST scripts** = Test execution ONLY (assume driver installed)
- ❌ **TEST scripts** ≠ INSTALL/RELOAD (use `tools/setup/Install-Driver.ps1`)
- ❌ **TEST scripts** ≠ BOOT CONFIG (use `tools/setup/` scripts)
- ❌ **TEST scripts** ≠ BUILD (use `tools/build/Build-Driver.ps1`, `Build-Tests.ps1`)

**Total Archived**: 12 scripts (8 from Loop 2 + 4 from Final Consolidation)

### Why Archive?
- ✅ **Features integrated**: All unique functionality now in Run-Tests.ps1 parameters
- ✅ **Better error handling**: Canonical scripts have comprehensive validation
- ✅ **Path detection**: Wrappers auto-detect execution directory (repo root vs tools\test)
- ✅ **Maintainability**: Single source of truth vs 8 duplicate implementations

### What We Kept
- **Quick-Test-Debug.bat** / **Quick-Test-Release.bat** - Convenience wrappers (single-command reload cycle)
- **Run-Tests.ps1** - SINGLE canonical test executor
- **test_driver.ps1** - Reference implementation with educational value (detailed diagnostics)
- **Reboot-And-Test.ps1** - Specialized boot-time testing (out of scope for normal workflow)

## Test Executables

Built to `build\tools\avb_test\x64\{Debug|Release}\*.exe`:
- avb_test_i210_um.exe
- avb_test_i219.exe
- avb_test_i226.exe
- comprehensive_ioctl_test.exe
- ptp_clock_control_test.exe
- tsauxc_toggle_test.exe
- (and more... see `tools/build/Build-Tests.ps1` for full list)

