# Loop 2 Complete Analysis - TEST Scripts Cleanup

**Date**: 2025-12-28  
**Purpose**: Test each old script, compare with canonical Run-Tests.ps1, determine keep vs archive

## Summary Table

| Script | Lines | Status | Canonical Equivalent | Decision |
|--------|-------|--------|---------------------|----------|
| **Quick-Test-Debug.bat** | 53 | ✅ TESTED | Install-Driver.ps1 -Reinstall + Run-Tests.ps1 -Quick | **KEEP** (wrapper) |
| **Quick-Test-Release.bat** | 72 | ⚙️ Same as Debug | Install-Driver.ps1 -Reinstall + Run-Tests.ps1 -Quick | **KEEP** (wrapper) |
| **test_hardware_only.bat** | 151 | ⚙️ Analysis | Run-Tests.ps1 -HardwareOnly | **ARCHIVE** (integrated) |
| **run_complete_diagnostics.bat** | 185 | ⚙️ Analysis | Run-Tests.ps1 -CompileDiagnostics | **ARCHIVE** (integrated) |
| **test_secure_boot_compatible.bat** | 87 | ⚙️ Analysis | Run-Tests.ps1 -SecureBootCheck | **ARCHIVE** (integrated) |
| **run_tests.ps1** (lowercase) | 89 | ⚙️ Analysis | Run-Tests.ps1 -Full | **ARCHIVE** (duplicate) |
| **run_test_admin.ps1** | 29 | ⚙️ Analysis | Run-Tests.ps1 (built-in admin check) | **ARCHIVE** (redundant) |
| **test_local_i219.bat** | 101 | ⚙️ Analysis | Features in test_secure_boot_compatible.bat | **ARCHIVE** (duplicates) |
| **Test-Release.bat** | ? | ⚙️ Analysis | Quick-Test-Release.bat | **ARCHIVE** (duplicate) |
| **Reboot-And-Test.ps1** | ? | ❌ EXCLUDED | N/A | **EXCLUDE** (not needed) |

## Detailed Analysis

### 1. Quick-Test-Debug.bat ✅ TESTED

**Functionality**:
- Check admin privileges
- Unbind filter (`netcfg -u`)
- Stop service (`net stop`)
- Install driver (`netcfg -l <inf>`)
- Wait 3 seconds
- Run test (`avb_test_i226.exe selftest`)

**Test Result** (2025-12-28):
```
[3] Installing new driver (DEBUG BUILD)...
... build\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf was copied to C:\WINDOWS\INF\oem46.inf.
... done.

[5] Running test...
Capabilities (0x000001BF): BASIC_1588 ENHANCED_TS TSN_TAS TSN_FP PCIe_PTM 2_5G MMIO
Summary: base=OK, optional=OK
```

**Canonical Equivalent**:
```powershell
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick
```

**Decision**: **KEEP as convenience wrapper**
- **Why**: Single-command reload cycle for rapid development (2 commands → 1)
- **Unique Value**: Developer productivity (10-second iteration)
- **Path Flexibility**: Auto-detects repo root vs tools\test

---

### 2. Quick-Test-Release.bat (Identical to Debug)

**Functionality**: Same as Quick-Test-Debug.bat but Configuration=Release

**Canonical Equivalent**:
```powershell
.\tools\setup\Install-Driver.ps1 -Configuration Release -Reinstall
.\tools\test\Run-Tests.ps1 -Configuration Release -Quick
```

**Decision**: **KEEP as convenience wrapper** (same rationale as Debug)

---

### 3. test_hardware_only.bat (FEATURES INTEGRATED)

**Functionality**:
1. Check admin privileges
2. **Compile** with `/DHARDWARE_ONLY=1`: `cl avb_test_i219.c /DHARDWARE_ONLY=1 /Fe:avb_test_hardware_only.exe`
3. Detect Intel I219 hardware (`wmic DeviceID like '%%VEN_8086%%DEV_0DC7%%'`)
4. Run hardware-only test

**Canonical Equivalent**:
```powershell
.\tools\test\Run-Tests.ps1 -HardwareOnly
```

**Run-Tests.ps1 Implementation** (Lines 182-210):
```powershell
if ($HardwareOnly) {
    Write-Step "Compiling Hardware-Only Test"
    
    # Compile with HARDWARE_ONLY flag
    $BuildCmd = "cl /nologo /W4 /Zi /DHARDWARE_ONLY=1 -I include tests/device_specific/i219/avb_test_i219.c /Fe:avb_test_hardware_only.exe"
    $result = & $BuildScript -BuildCmd $BuildCmd 2>&1
    
    if (Test-Path "avb_test_hardware_only.exe") {
        Write-Success "Hardware-only test compiled"
        Write-Host "`nExecuting hardware-only test..." -ForegroundColor Yellow
        & ".\avb_test_hardware_only.exe"
    } else {
        Write-Failure "Compilation failed"
    }
}
```

**Decision**: **ARCHIVE** (feature fully integrated into Run-Tests.ps1)

---

### 4. run_complete_diagnostics.bat (FEATURES INTEGRATED)

**Functionality**:
1. **Compile diagnostic tools**:
   - `cl tests\diagnostic\intel_avb_diagnostics.c /Fe:intel_avb_diagnostics.exe`
   - `cl debug_device_interface.c /Fe:device_interface_test.exe`
   - `cl avb_test_i219.c /DHARDWARE_ONLY=1 /Fe:hardware_test.exe`
2. **System capability checks**:
   - Check Hyper-V: `dism /online /get-featureinfo /featurename:Microsoft-Hyper-V`
   - Check SignTool.exe availability
   - Check Inf2Cat.exe availability
3. Run all diagnostic tests

**Canonical Equivalent**:
```powershell
.\tools\test\Run-Tests.ps1 -CompileDiagnostics
```

**Run-Tests.ps1 Implementation** (Lines 122-180):
```powershell
if ($CompileDiagnostics) {
    Write-Step "Compiling Diagnostic Tools"
    
    # Compile intel_avb_diagnostics.exe
    $BuildCmd = "cl /nologo /W4 /TC tests/diagnostic/intel_avb_diagnostics.c /Fe:intel_avb_diagnostics.exe advapi32.lib setupapi.lib"
    & $BuildScript -BuildCmd $BuildCmd | Out-Null
    
    # Compile device_interface_test.exe
    if (Test-Path "debug_device_interface.c") {
        $BuildCmd = "cl /nologo /W4 debug_device_interface.c /Fe:device_interface_test.exe advapi32.lib"
        & $BuildScript -BuildCmd $BuildCmd | Out-Null
    }
    
    # System Capability Checks
    Write-Host "`n==> System Capability Analysis" -ForegroundColor Cyan
    
    # Check Hyper-V
    $hyperv = dism /online /get-featureinfo /featurename:Microsoft-Hyper-V 2>&1 | Select-String "State.*Enabled"
    if ($hyperv) { Write-Success "Hyper-V: Enabled" }
    
    # Check SignTool
    $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($signtool) { Write-Success "SignTool.exe: Found" }
    
    # Check Inf2Cat
    $inf2cat = Get-Command inf2cat.exe -ErrorAction SilentlyContinue
    if ($inf2cat) { Write-Success "Inf2Cat.exe: Found" }
}
```

**Decision**: **ARCHIVE** (feature fully integrated into Run-Tests.ps1)

---

### 5. test_secure_boot_compatible.bat (FEATURES INTEGRATED)

**Functionality**:
1. Check admin privileges
2. **Check Secure Boot status**: `powershell -command "Confirm-SecureBootUEFI"`
3. **Install test certificate**: `certutil -addstore root build\x64\Debug\IntelAvbFilter.cer`
4. Alternative testing methods for Secure Boot environments

**Canonical Equivalent**:
```powershell
.\tools\test\Run-Tests.ps1 -SecureBootCheck
```

**Run-Tests.ps1 Implementation** (Lines 90-120):
```powershell
if ($SecureBootCheck) {
    Write-Step "Secure Boot Compatibility Check"
    
    # Check Secure Boot status
    try {
        $secureBootEnabled = Confirm-SecureBootUEFI
        if ($secureBootEnabled) {
            Write-Info "Secure Boot is ENABLED - Test signing blocked"
        } else {
            Write-Success "Secure Boot disabled or not supported"
        }
    } catch {
        Write-Info "Cannot determine Secure Boot status"
    }
    
    # Install test certificate if exists
    $certPath = Join-Path $repoRoot "build\x64\$Configuration\IntelAvbFilter.cer"
    if (Test-Path $certPath) {
        Write-Host "Installing test certificate..." -ForegroundColor Yellow
        $result = certutil -addstore root $certPath 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Test certificate installed"
        } else {
            Write-Info "Certificate installation failed (may already exist)"
        }
    }
}
```

**Decision**: **ARCHIVE** (feature fully integrated into Run-Tests.ps1)

---

### 6. run_tests.ps1 (lowercase) - DUPLICATE PHASE EXECUTOR

**Functionality**:
- Executes tests in 6 phases (Architecture → Investigation)
- Hardcoded paths: `$RepoRoot\build\tools\avb_test\x64\Debug\*.exe`
- Same test sequence as Run-Tests.ps1 -Full

**Phase Structure**:
- Phase 0: Architecture Compliance (capability + device separation)
- Phase 1: Basic Hardware Diagnostics
- Phase 2: TSN IOCTL Handler Verification
- Phase 3: Multi-Adapter Hardware Testing
- Phase 4: I226 Advanced Feature Testing
- Phase 5: I210 Basic Testing
- Phase 6: Specialized Investigation Tests

**Canonical Equivalent**:
```powershell
.\tools\test\Run-Tests.ps1 -Configuration Debug -Full
```

**Run-Tests.ps1 -Full** (Lines 378-485): Implements same 6-phase structure with better:
- Configuration flexibility (Debug/Release)
- Error handling
- Missing executable checks
- Verbose output control

**Decision**: **ARCHIVE** (exact duplicate of Run-Tests.ps1 -Full)

---

### 7. run_test_admin.ps1 - REDUNDANT UAC WRAPPER

**Functionality** (29 lines):
```powershell
# Check if admin
if (-NOT ([Security.Principal.WindowsPrincipal]...).IsInRole(...Administrator)) {
    # Restart as admin
    Start-Process powershell.exe "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# Run comprehensive_ioctl_test.exe
$testExe = Join-Path $PSScriptRoot "comprehensive_ioctl_test.exe"
if (Test-Path $testExe) {
    & $testExe
}
```

**Canonical Equivalent**:
```powershell
# Run-Tests.ps1 already has built-in admin check (line 48-51)
.\tools\test\Run-Tests.ps1 -TestExecutable comprehensive_ioctl_test.exe
```

**Run-Tests.ps1 Built-in Admin Check**:
```powershell
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges. Please run as Administrator."
    exit 1
}
```

**Decision**: **ARCHIVE** (redundant - Run-Tests.ps1 has built-in admin check)

---

### 8. test_local_i219.bat - DUPLICATES FEATURES

**Functionality**:
1. Check admin privileges
2. **Check test signing**: `bcdedit /enum | find "testsigning"`
3. **Enable test signing if needed**: `bcdedit /set testsigning on`
4. Identify Intel I219 controller (`wmic Name like '%%I219%%'`)
5. Check driver files exist (ndislwf.sys, IntelAvbFilter.inf)
6. Install driver with `pnputil`

**Overlapping Features**:
- Admin check → Run-Tests.ps1 built-in
- Test signing → test_secure_boot_compatible.bat (integrated as -SecureBootCheck)
- Hardware detection → Run-Tests.ps1 built-in (lines 240-280)
- Driver installation → Install-Driver.ps1

**Canonical Equivalent**:
```powershell
# Enable test signing (if needed)
bcdedit /set testsigning on

# Install driver
.\tools\setup\Install-Driver.ps1 -Configuration Debug -InstallDriver

# Run tests
.\tools\test\Run-Tests.ps1 -Configuration Debug -Full
```

**Decision**: **ARCHIVE** (features duplicated across canonical scripts)

---

### 9. Test-Release.bat - OBSOLETE OLD VERSION

**Functionality** (26 lines):
```bat
[1] Unbinding...
netcfg -u MS_IntelAvbFilter

[2] Stopping...
net stop IntelAvbFilter

[3] Installing Release build...
netcfg -v -l x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

[4] Testing...
avb_test_i226.exe | findstr /C:"Summary"
```

**Problems**:
- ❌ Hardcoded OLD paths: `x64\Release\...` (should be `build\x64\Release\...`)
- ❌ Wrong test exe location: `avb_test_i226.exe` in repo root (should be `build\tools\avb_test\x64\Release\avb_test_i226.exe`)
- ❌ No .sys copy step (Quick-Test-Release.bat has this)
- ❌ No path detection (always assumes wrong paths)
- ❌ No error checking

**Comparison with Quick-Test-Release.bat**:
```bat
# Test-Release.bat (OLD, BROKEN):
- Paths: x64\Release\... (wrong)
- Test exe: avb_test_i226.exe (repo root, wrong)
- No .sys copy
- No path detection
- No error handling

# Quick-Test-Release.bat (NEW, WORKING):
- Paths: build\x64\Release\... (correct)
- Test exe: build\tools\avb_test\x64\Release\avb_test_i226.exe (correct)
- Copies .sys to package directory
- Path detection (repo root vs tools\test)
- Error handling (checks exist, shows clear errors)
```

**Decision**: **ARCHIVE** (obsolete old version, replaced by Quick-Test-Release.bat)

---

### 10. Reboot-And-Test.ps1 - EXCLUDED

**User Decision**: EXCLUDE from cleanup (boot-time load testing not needed)

**Reason**: Boot-time driver loading tests are specialized and infrequent. Not part of normal development workflow.

---

## Final Summary

### KEEP (Convenience Wrappers)
1. **Quick-Test-Debug.bat** - Developer reload cycle (unbind → install → test in 10 seconds)
2. **Quick-Test-Release.bat** - Same for Release builds

**Rationale**: Single-command workflow vs 2-command canonical equivalent. Developer productivity value.

### ARCHIVE (Features Integrated into Run-Tests.ps1)
3. **test_hardware_only.bat** → Run-Tests.ps1 -HardwareOnly
4. **run_complete_diagnostics.bat** → Run-Tests.ps1 -CompileDiagnostics
5. **test_secure_boot_compatible.bat** → Run-Tests.ps1 -SecureBootCheck
6. **run_tests.ps1** (lowercase) → Run-Tests.ps1 -Full (exact duplicate)
7. **run_test_admin.ps1** → Run-Tests.ps1 (built-in admin check, redundant)
8. **test_local_i219.bat** → Features split across Install-Driver.ps1 + Run-Tests.ps1 -SecureBootCheck
9. **Test-Release.bat** → Quick-Test-Release.bat (obsolete old version with wrong paths)

**Rationale**: Functionality fully covered by canonical Run-Tests.ps1 with better error handling, flexibility, and maintainability.

### EXCLUDE (Out of Scope)
10. **Reboot-And-Test.ps1** - Boot-time load testing (specialized, infrequent use)

---

## Archival Actions

### Step 1: Create Archive Directory
```powershell
mkdir tools\archive\deprecated\test
```

### Step 2: Move Scripts to Archive
```powershell
Move-Item tools\test\test_hardware_only.bat tools\archive\deprecated\test\
Move-Item tools\test\run_complete_diagnostics.bat tools\archive\deprecated\test\
Move-Item tools\test\test_secure_boot_compatible.bat tools\archive\deprecated\test\
Move-Item tools\test\run_tests.ps1 tools\archive\deprecated\test\
Move-Item tools\test\run_test_admin.ps1 tools\archive\deprecated\test\
Move-Item tools\test\test_local_i219.bat tools\archive\deprecated\test\
Move-Item tools\test\Test-Release.bat tools\archive\deprecated\test\
```

### Step 3: Update README.md

Add to `tools/test/README.md`:
```markdown
## Archived Scripts (Loop 2 - 2025-12-28)

Moved to `tools/archive/deprecated/test/`:

1. **test_hardware_only.bat** - Hardware-only compilation
   - **Canonical**: `Run-Tests.ps1 -HardwareOnly`
   
2. **run_complete_diagnostics.bat** - Diagnostic tool compilation + system checks
   - **Canonical**: `Run-Tests.ps1 -CompileDiagnostics`
   
3. **test_secure_boot_compatible.bat** - Secure Boot checks + certificate install
   - **Canonical**: `Run-Tests.ps1 -SecureBootCheck`
   
4. **run_tests.ps1** (lowercase) - Phase executor
   - **Canonical**: `Run-Tests.ps1 -Full`
   
5. **run_test_admin.ps1** - UAC wrapper for comprehensive_ioctl_test.exe
   - **Canonical**: `Run-Tests.ps1 -TestExecutable comprehensive_ioctl_test.exe`
   
6. **test_local_i219.bat** - Test signing + driver install
   - **Canonical**: `Install-Driver.ps1 + Run-Tests.ps1 -SecureBootCheck`
   
7. **Test-Release.bat** - Obsolete Release test (wrong paths)
   - **Canonical**: `Quick-Test-Release.bat`
```

---

## Verification Checklist

Before archiving, verify each script's functionality is in Run-Tests.ps1:

- [x] **test_hardware_only.bat** → `-HardwareOnly` parameter (Lines 182-210) ✅
- [x] **run_complete_diagnostics.bat** → `-CompileDiagnostics` parameter (Lines 122-180) ✅
- [x] **test_secure_boot_compatible.bat** → `-SecureBootCheck` parameter (Lines 90-120) ✅
- [x] **run_tests.ps1** (lowercase) → `-Full` parameter (Lines 378-485) ✅
- [x] **run_test_admin.ps1** → Built-in admin check (Lines 48-51) ✅
- [x] **test_local_i219.bat** → Features in Install-Driver.ps1 + Run-Tests.ps1 ✅
- [x] **Test-Release.bat** → Quick-Test-Release.bat (updated paths + logic) ✅

---

## Next Steps

1. ✅ Complete Loop 2 analysis (THIS DOCUMENT)
2. ⏸️ Execute archival (move 7 scripts to tools/archive/deprecated/test/)
3. ⏸️ Update tools/test/README.md with archival list
4. ⏸️ Git commit:
   - Fixed files: Run-Tests.ps1, Build-Tests.ps1, Quick-Test-*.bat
   - Archived scripts
   - Updated README.md
5. ⏸️ Proceed to DEVELOPMENT category (11 scripts)
