# Comparison: Quick-Test-Debug.bat vs Canonical Scripts

**Date**: 2025-12-28  
**Purpose**: Loop 2 functional comparison to determine if Quick-Test-Debug.bat can be archived

## Quick-Test-Debug.bat Functionality

**What it does**:
1. Check admin privileges
2. Unbind filter (`netcfg -u MS_IntelAvbFilter`)
3. Stop service (`net stop IntelAvbFilter`)
4. Install driver (`netcfg -l <inf> -c s -i MS_IntelAvbFilter`)
5. Wait 3 seconds for service to start
6. Run test (`avb_test_i226.exe selftest`)

**Workflow**: Quick driver reload + test (development iteration cycle)

## Canonical Script Equivalent

**NOT EQUIVALENT** - Requires TWO separate canonical scripts:

### Part 1: Driver Installation (Install-Driver.ps1)
```powershell
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
```
**Covers**:
- ✅ Unbind filter
- ✅ Stop service  
- ✅ Install driver
- ✅ Wait for service

**Missing from Quick-Test-Debug.bat**:
- Timestamp check (uses newest file)
- Build verification
- Cleanup old installations
- Error handling and retry logic

### Part 2: Test Execution (Run-Tests.ps1)
```powershell
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
```
**Should cover**:
- ✅ Run quick verification tests

**ACTUAL BUG**: `-Quick` parameter defined but NEVER USED!
- Line 19: `[switch]$Quick,` (parameter defined)
- Line 356: `} elseif ($Full) {` (should be `elseif ($Quick)`)
- Line 378: `} elseif ($Full) {` (correct - full test suite)

**Result**: `-Quick` parameter does nothing, falls through to no tests!

### Equivalent Canonical Workflow (BROKEN)
```powershell
# What SHOULD work but doesn't:
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
# ❌ -Quick does nothing, no tests run!

# Current workaround:
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
.\tools\test\Run-Tests.ps1 -Configuration Debug -TestExecutable avb_test_i226.exe
# ✅ Works but verbose

# Better approach after fixing Run-Tests.ps1:
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
# ✅ Would work after line 356 fix
```

## Unique Features (Quick-Test-Debug.bat vs Canonical)

### Quick-Test-Debug.bat has:
1. **Single command** - All-in-one reload + test (developer convenience)
2. **Minimal output** - No verbose diagnostics
3. **Fast iteration** - 10-second cycle (unbind → install → test)
4. **Path detection** - Works from repo root OR tools\test\

### Canonical scripts have:
1. **Separation of concerns** - Install vs Test (cleaner architecture)
2. **Comprehensive checks** - Hardware detection, service validation, build verification
3. **Timestamp logic** - Uses newest file (.sys/.inf/.cat)
4. **Flexible test selection** - Can run specific tests, quick suite, or full suite
5. **Better error handling** - Retry logic, detailed diagnostics

## Recommendation

**DO NOT ARCHIVE** - Quick-Test-Debug.bat provides unique value:

### Why Keep It:
1. **Developer workflow** - Fast reload cycle during active development (unbind → install → test in one command)
2. **No canonical equivalent** - Would need `Install-Driver.ps1 -Reinstall ; Run-Tests.ps1 -Quick` (2 commands, more typing)
3. **Convenience** - Single .bat file, no need to remember parameter combinations
4. **Path flexibility** - Auto-detects repo root vs tools\test execution

### Required Fix:
**Run-Tests.ps1 line 356** - Change `elseif ($Full)` to `elseif ($Quick)` to make `-Quick` parameter functional

### After Fix:
- Quick-Test-Debug.bat = Wrapper for common dev workflow
- Keep as convenience wrapper (similar to how Run-Full-Tests.bat wraps `-Full`)
- Update comment header to reference canonical: `# Wrapper: Install-Driver.ps1 -Reinstall + Run-Tests.ps1 -Quick`

## Test Results

### Quick-Test-Debug.bat Output (2025-12-28):
```
[3] Installing new driver (DEBUG BUILD)...
Trying to install MS_IntelAvbFilter ...
... build\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf was copied to C:\WINDOWS\INF\oem46.inf.
... done.

[5] Running test...
Capabilities (0x000001BF): BASIC_1588 ENHANCED_TS TSN_TAS TSN_FP PCIe_PTM 2_5G MMIO
PTP: running (SYSTIM=0x0000000000F62018)
Device: Intel I226 2.5G Ethernet - Advanced TSN (0x0)
TAS OK (0x0)
FP ON OK (0x0)
FP OFF OK (0x0)
PTM ON OK (0x0)
PTM OFF OK (0x0)
Summary: base=OK, optional=OK
```

**Result**: ✅ All tests passed, driver reloaded successfully

### Canonical Equivalent (after fixing Run-Tests.ps1):
```powershell
# Step 1: Reinstall driver
PS> .\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
# (same installation output)

# Step 2: Run quick tests
PS> .\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
# (would run avb_capability_validation_test.exe + avb_diagnostic_test.exe)
```

**Difference**: Quick-Test-Debug.bat runs `avb_test_i226.exe selftest` (single test), canonical runs 2 different tests

## Action Items

1. **Fix Run-Tests.ps1 line 356**: Change `elseif ($Full)` → `elseif ($Quick)`
2. **Keep Quick-Test-Debug.bat** as convenience wrapper
3. **Update header comment** to reference canonical equivalents
4. **Document in README.md** - List as "Convenience Wrappers (Keep)"

## Conclusion

**Status**: Keep as convenience wrapper (NOT a duplicate)  
**Canonical Equivalent**: Install-Driver.ps1 -Reinstall + Run-Tests.ps1 -Quick (2 commands)  
**Unique Value**: Single-command reload cycle for rapid development iteration  
**Required Fix**: Run-Tests.ps1 line 356 ($Quick parameter not working)
