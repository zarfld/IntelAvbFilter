# Script Functional Analysis
**Purpose**: Analyze ACTUAL functionality of 50+ scripts (by content, not names)  
**Issue**: #27 REQ-NF-SCRIPTS-001: Consolidate 80+ Redundant Scripts  
**Date**: 2025-12-09

## Analysis Method

This document analyzes scripts by:
1. Reading actual code content
2. Testing functionality where possible
3. Grouping by actual behavior (not assumed from names)
4. Identifying duplicates, broken scripts, and unique implementations

## SETUP SCRIPTS (19 total)

### Group A: Complete Setup Workflows

#### `Complete-Driver-Setup.bat`
**What it ACTUALLY does**:
- Extracts certificate from built driver (.sys file)
- Installs cert to Root and TrustedPublisher stores
- Complete cleanup (sc stop, sc delete, netcfg -u)
- Installs via `netcfg -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter`
- Uses **Release** build by default: `x64\Release\IntelAvbFilter\`
- Checks service status after install
- Runs `avb_test_i226.exe caps` if exists
- Good error handling with error code explanations
- Checks Windows event log on failure

**Unique features**: Auto-extracts cert from driver, tests after install

---

#### `Install-Debug-Driver.bat`
**What it ACTUALLY does**:
- Uses **Debug** build: `x64\Debug\IntelAvbFilter\`
- Verifies files exist (Driver.sys, Driver.inf, optional .cat)
- Complete cleanup (sc stop/delete, netcfg -u)
- Verifies certificate via PowerShell check
- Installs via `netcfg -v -l INF -c s -i MS_IntelAvbFilter`
- Verifies service created afterward
- Good error messages with common causes
- Checks event log on failure

**Unique features**: Debug-specific, verifies cert before install

**Difference vs Complete-Driver-Setup.bat**: Debug vs Release builds, cert verification vs extraction

---

#### `install_filter_proper.bat` ⭐ (REFERENCE IMPLEMENTATION)
**What it ACTUALLY does**:
- **THE CORRECT way to install NDIS filter drivers** (42 lines)
- Clean, focused implementation without unnecessary extras
- Uses relative path `..\..\x64\Debug\IntelAvbFilter\`
- Workflow: sc stop → netcfg -u → netcfg -l INF -c s -i MS_IntelAvbFilter → sc start → sc query
- **Proper netcfg flags**: `-c s` (service class) + `-i MS_IntelAvbFilter` (component ID)
- This is what NDIS filter drivers NEED - nothing more, nothing less

**Unique features**: Reference implementation - other scripts ADD features (certs, diagnostics) but this is THE CORE

**Assessment**: This is the CANONICAL method for NDIS filter installation. Name "proper" indicates this is the authoritative reference.

---

#### `Setup-Driver.bat`
**What it ACTUALLY does**:
- Lives in `tools\setup\` (correct location)
- Uses relative path `..\..\x64\Debug\IntelAvbFilter\`
- Calls pnputil /add-driver + /install
- sc create IntelAvbFilter
- sc start
- sc query
- Lists network adapters via PowerShell Get-NetAdapter
- NO certificate handling
- Uses **sc create** instead of netcfg (different approach!)

**Unique features**: Uses pnputil instead of netcfg, lists adapters

**Difference vs others**: pnputil approach vs netcfg approach

---

### Group B: Parameterized Multi-Function Scripts

#### `setup_driver.ps1` ⭐ (CANONICAL CANDIDATE)
**What it ACTUALLY does**:
- Parameters: -EnableTestSigning, -CreateCertificate, -InstallDriver, -UninstallDriver, -CheckStatus, -DriverPath
- Helper functions: Write-Step, Write-Success, Write-Failure
- Can enable test signing (bcdedit /set testsigning on)
- Can create self-signed certificate
- Can install driver via netcfg
- Can uninstall driver (sc stop/delete + netcfg -u)
- Can check status
- Default $DriverPath = "x64\Debug\IntelAvbFilter"
- Clean modular design

**Unique features**: Multi-function with parameters, can do everything

**Assessment**: EXCELLENT canonical script candidate - covers 80% of setup use cases

---

#### `Setup-Driver.ps1` (DIFFERENT from setup_driver.ps1!)
**What it ACTUALLY does**:
- Interactive wizard (NO parameters)
- #Requires -RunAsAdministrator
- Checks test signing status
- Prompts user to enable if needed
- Offers reboot if test signing changed
- Hardcoded to Debug build
- Uses pnputil for driver install
- Uses sc create/start for service

**Unique features**: Interactive prompts, user-friendly wizard

**Difference vs setup_driver.ps1**: Interactive vs parameterized, wizard vs automation-friendly

---

### Group C: Installation Method Variants

#### `Install-AvbFilter.ps1` (pnputil method)
**What it ACTUALLY does**:
- Uses pnputil /add-driver + /install
- Creates/updates service via sc create
- Starts service via sc start
- Lists Intel adapters via Get-NetAdapter
- Tests device interface accessibility via C# interop (CreateFile API)
- Good for automation (PowerShell script)

**Unique features**: Tests device interface, uses pnputil

---

#### `install_devcon_method.bat` ⚠️ (DEPRECATED TOOL)
**What it ACTUALLY does**: (need to read)
- Likely uses devcon.exe (deprecated, not included in modern WDK)

**Assessment**: Should be marked deprecated (relies on removed tool)

---

#### `install_ndis_filter.bat` (153 lines - German comments)
**What it ACTUALLY does**:
- German comments: "Korrekte Installation für NDIS Lightweight Filter Driver"
- **Key insight** (line 61): "CRITICAL: NDIS Lightweight Filters require netcfg.exe, not pnputil!"
- Verifies files exist (.sys, .inf, .cat)
- **Copies driver to System32/drivers** (line 48) - unique approach!
- Uses netcfg -v -l (preferred method)
- Falls back to pnputil if netcfg fails
- Has alternative manual service installation path
- Debug build, comprehensive error handling

**Unique features**: Copies driver to System32/drivers manually, explains WHY netcfg is needed

**Assessment**: Educational reference - explains NDIS filter requirements

---

#### `install_fixed_driver.bat` (62 lines)
**What it ACTUALLY does**:
- Comment: "Critical Fix: NetCfgInstanceId in [Install] section"
- **Complete cleanup**: Removes ALL old packages via pnputil loop
- Clears driver cache: deletes from System32/drivers and DriverStore
- **Release build**: x64\Release\IntelAvbFilter\
- Uses netcfg, falls back to pnputil
- Checks for specific DebugView messages: "NDIS filter driver registered successfully"

**Unique features**: Most aggressive cleanup (removes ALL packages + cache)

**Assessment**: For fixing stuck installations, more thorough than others

---

#### `install_smart_test.bat` (180 lines)
**What it ACTUALLY does**:
- Comment: "Auto-detects file locations and works with Secure Boot"
- **Auto-detects driver location**: Checks x64\Debug, Debug, current dir
- Verifies hardware: Uses WMIC to check for Intel I219 (VEN_8086, DEV_0DC7)
- Checks for certificate (.cer file)
- Multi-location support for different build configurations

**Unique features**: Auto-detection of build location, hardware verification

**Assessment**: "Smart" refers to auto-detection, good for different environments

---

### Group D: Certificate Management

#### `Manage-Certificate.ps1` ⭐ (57 lines - CANONICAL CANDIDATE)
**What it ACTUALLY does**:
- German: "Automatisches Zertifikatsmanagement für Intel AVB Filter"
- **Auto-exports certificate from PrivateCertStore**
- Finds newest cert matching "Intel AVB Filter Test Certificate"
- Exports to IntelAvbTestCert.cer in repo root
- **Auto-installs to LocalMachine\Root** if running as admin
- Falls back to manual instructions if not admin
- Shows certificate details (thumbprint, expiry)
- Provides next steps: bcdedit, restart, pnputil

**Unique features**: Fully automated certificate export + install workflow

**Assessment**: EXCELLENT canonical Manage-Certificate.ps1 - already well-designed!

---

#### `Enable-TestSigning.bat`
**What it ACTUALLY does**: (from comment analysis)
- "Configure Windows to allow installation of test-signed drivers"
- Likely: bcdedit /set testsigning on

**Assessment**: Subset of setup_driver.ps1 -EnableTestSigning

---

#### `troubleshoot_certificates.ps1`
**What it ACTUALLY does**: (from comment analysis)
- "Certificate Troubleshooting Script"
- Diagnostic/troubleshooting purpose

**Assessment**: Separate utility, not part of normal flow

---

#### `install_certificate_method.bat`
**Status**: Need to read

---

### Group E: Hyper-V Development Setup

#### `setup_hyperv_development.bat`, `setup_hyperv_vm_complete.bat`
**Purpose**: Specific to Hyper-V VM development environment

**Assessment**: Special-purpose scripts, likely keep separate

---

## BUILD SCRIPTS (9 total)

### Group A: Driver Build + Signing

#### `Build-And-Sign-Driver.ps1` ⭐ (CANONICAL CANDIDATE)
**What it ACTUALLY does**:
- Parameters: -Configuration (Debug|Release), -Platform (x64), -CertificateName, -SkipSigning
- German comments
- STEP 1: Finds WDK makecat.exe (searches multiple SDK versions)
- STEP 2: (need to read lines 50+)
- Appears comprehensive: builds + generates CAT + signs

**Unique features**: Multi-step, parameterized, finds WDK tools

**Assessment**: EXCELLENT canonical candidate for Build-Driver.ps1

---

### Group A: Driver Build + Signing

#### `Sign-Driver.ps1` (100+ lines - German)
**What it ACTUALLY does**:
- German: "Treiber-Signierung für Intel AVB Filter"
- Creates test certificate using makecert.exe
- Searches multiple WDK SDK versions for tools
- Creates cert in PrivateCertStore
- Signs CAT file using signtool.exe
- Provides next steps: bcdedit, restart, certmgr, pnputil

**Assessment**: Separate signing workflow (subset of Build-And-Sign-Driver.ps1)

**Decision**: Keep as separate Sign-Driver.ps1 canonical (ADR specifies this)

---

#### `Generate-CATFile.ps1`
**What it ACTUALLY does**: (need to read)
- Likely CAT file generation only

**Assessment**: Subset functionality, could be merged or separate utility

---

### Group B: Test Build Scripts

#### `Build-AllTests-Honest.ps1` vs `Build-AllTests-TrulyHonest.ps1`
**What they ACTUALLY do**:

**Build-AllTests-Honest.ps1**:
- Builds 10 user-mode test tools
- Tracks success/failure counts
- Uses vcvars64.bat
- Builds via nmake with .mak files
- Clean + build per test
- Reports actual results (no false advertising!)

**Build-AllTests-TrulyHonest.ps1**:
- Similar but "TRULY honest" (implies previous was already honest?)
- Also uses vcvars64.bat
- Has Build-Test helper function
- Verifies .exe exists after build
- Changes to $RepoRoot before building
- Checks for VS2022 at hardcoded path

**Differences**:
- TrulyHonest: Verifies .exe exists after build
- TrulyHonest: Better repo root handling
- Both: Same purpose (build all tests with honest reporting)

**Assessment**: DUPLICATES - consolidate into one Build-Tests.ps1 (take best of both)

---

#### `build_i226_test.bat`
**Status**: Need to read - likely single-test builder

---

### Group C: Driver-Specific Build Scripts

#### `Fix-And-Install.bat`
**Status**: Need to read - likely build + install combo

---

#### `fix_deployment_config.ps1`, `fix_test_signing.bat`
**Status**: Need to read - likely configuration fixes

---

## TEST SCRIPTS (12 total)

### Group A: Comprehensive Test Runners

#### `run_tests.ps1` ⭐ (CANONICAL CANDIDATE - already analyzed)
**What it ACTUALLY does**:
- Parameters: -BuildConfig (Debug|Release), -TestType (all|caps|selftest|specific)
- Finds test executables in `build\tools\avb_test\x64\$BuildConfig\`
- Runs avb_test_i210.exe, avb_test_i226.exe with specified test type
- Good output formatting
- Already working well from tools/test/ location

**Assessment**: EXCELLENT canonical Test-Driver.ps1 candidate

---

#### `test_driver.ps1`, `run_test_admin.ps1`
**Status**: Need to read

---

### Group B: Configuration-Specific Test Scripts

#### `Quick-Test-Debug.bat` (47 lines)
**What it ACTUALLY does**:
- Hardcoded **Debug build**: x64\Debug\IntelAvbFilter\
- Workflow: netcfg -u → net stop → netcfg install → avb_test_i226.exe selftest
- Mentions checking DebugView for specific messages
- Comment: "Quick reload and test script"

**Assessment**: Hardcoded Debug version of reload+test workflow

---

#### `Quick-Test-Release.bat` (49 lines)
**What it ACTUALLY does**:
- **KEY STEP** (line 8): Copies x64\Release\IntelAvbFilter.sys to package directory FIRST
- Comment: "Updated version with proper package copy"
- Hardcoded **Release build**: x64\Release\IntelAvbFilter\
- Workflow: copy latest build → netcfg -u → net stop → netcfg install → avb_test_i226.exe
- Ensures package directory has latest build before install

**Unique features**: Copies build to package first (ensures fresh binary)

**Assessment**: Hardcoded Release version, but has important "copy first" step

---

### Group C: Hardware/Environment-Specific Tests

#### `test_hardware_only.bat`, `test_local_i219.bat`, `test_secure_boot_compatible.bat`
**Status**: Need to read

**Assessment**: Likely special-purpose tests for specific scenarios

---

#### `run_complete_diagnostics.bat`, `Reboot-And-Test.ps1`
**Status**: Need to read

---

## DEVELOPMENT SCRIPTS (13 total)

### Group A: Driver Reload/Restart

#### `Force-Driver-Reload.ps1` vs `force_driver_reload.ps1`
**Status**: Need to read - likely duplicates (case difference only)

**Assessment**: Pick one, mark other deprecated

---

#### `Force-StartDriver.ps1`, `Start-AvbDriver.ps1`
**Status**: Need to read

**Assessment**: Likely similar functionality

---

### Group B: Status/Diagnostics

#### `Check-Driver-Status.ps1` ⭐ (CANONICAL CANDIDATE per ADR)
**Status**: Need to read

**Assessment**: ADR specifies this as canonical script

---

#### `diagnose_capabilities.ps1`, `enhanced_investigation_suite.ps1`
**Status**: Need to read

---

### Group C: Quick Reinstall/Update

### Group C: Quick Reinstall/Update

#### `reinstall-and-test.bat` (52 lines)
**What it ACTUALLY does**:
- Stops/deletes service
- **Calls Install-Debug-Driver.bat** (delegates install)
- Starts service
- Runs 3 tests: test_minimal.exe, test_direct_clock.exe, test_clock_config.exe
- Checks for expected capabilities: Caps=0x000001BF

**Assessment**: Orchestrator script - delegates to other scripts

---

#### `IntelAvbFilter-Cleanup.ps1`, `quick_start.ps1`
**Status**: Need to read

---

## SUMMARY OF FINDINGS (So Far)

### Clear Duplicates Identified
1. **Build-AllTests-Honest.ps1** vs **Build-AllTests-TrulyHonest.ps1**: Same purpose, minor differences
2. **Force-Driver-Reload.ps1** vs **force_driver_reload.ps1**: Likely identical except case

### Canonical Script Candidates
1. **Setup-Driver.ps1**: `setup_driver.ps1` (parameterized, multi-function) ⭐
2. **Build-Driver.ps1**: `Build-And-Sign-Driver.ps1` (comprehensive build+sign) ⭐
3. **Test-Driver.ps1**: `run_tests.ps1` (already well-designed) ⭐
4. **Check-Driver-Status.ps1**: Exists, need to analyze ⭐

### Scripts to Deprecate (Likely)
- `install_filter_proper.bat`: Subset of Install-Debug-Driver.bat
- `install_devcon_method.bat`: Uses deprecated devcon.exe tool
- `Enable-TestSigning.bat`: Subset of setup_driver.ps1
- Build-AllTests variants: Keep best one, deprecate other
- Quick-Test-*.bat variants: Likely covered by run_tests.ps1 with params

### Special-Purpose Scripts (Likely Keep)
- `setup_hyperv_*`: Hyper-V VM development
- `troubleshoot_certificates.ps1`: Diagnostic tool
- Hardware-specific tests: test_hardware_only.bat, test_local_i219.bat

### More Setup Scripts Analyzed

#### `Install-NewDriver.bat`
**What it ACTUALLY does**:
- Minimal script (51 lines)
- Cleanup: sc stop/delete, netcfg -u
- Installs **Release** build: `x64\Release\IntelAvbFilter\`
- Uses netcfg -v -l
- Runs `avb_test_i226.exe caps` if exists
- Good error code explanations

**Assessment**: Simpler version of Complete-Driver-Setup.bat (no cert handling)

---

#### `Install-Now.bat` ⚠️ (MINIMAL, DEBUG ONLY)
**What it ACTUALLY does**:
- VERY minimal (17 lines)
- Changes to `x64\Debug\IntelAvbFilter\` directory
- Runs netcfg directly (no cleanup!)
- **NO cleanup before install** - dangerous!
- No error handling beyond exit codes
- Hardcoded Debug build

**Assessment**: DANGEROUS - no cleanup, likely causes conflicts. Should be deprecated.

---

#### `install_certificate_method.bat`
**What it ACTUALLY does**:
- Claims "works with Secure Boot ENABLED (no test signing required)"
- Installs certificate: `certutil -addstore root IntelAvbFilter.cer`
- Uses **pnputil** (not netcfg): `/add-driver IntelAvbFilter.inf /install`
- Fallback to DevCon if pnputil fails
- Debug build: `x64\Debug\`

**Unique features**: pnputil + certificate method, claims Secure Boot compatible

**Assessment**: Interesting alternative approach, keep for secure boot scenarios

---

#### `install_devcon_method.bat` ⚠️ (DEPRECATED TOOL)
**What it ACTUALLY does**:
- Searches for `devcon.exe` in multiple WDK locations
- **DevCon is NOT included in modern WDK** (deprecated by Microsoft)
- Uses `devcon install IntelAvbFilter.inf` command
- Debug build: `x64\Debug\`

**Assessment**: DEPRECATED - relies on removed tool. Mark for deprecation.

---

## DEVELOPMENT SCRIPTS (Detailed Analysis)

#### `Force-Driver-Reload.ps1` (148 lines)
**What it ACTUALLY does**:
- Admin check
- Step 1: Unbind filter from adapters (`netcfg -u MS_IntelAvbFilter`)
- Step 2: Force stop service (Stop-Service + sc.exe stop as fallback)
- Step 3: (lines 50-148 need reading)
- Handles stuck states gracefully

**Assessment**: Robust reload script with error handling

---

#### `force_driver_reload.ps1` ⭐ (CORRECT RELOAD METHOD - 30 lines)
**What it ACTUALLY does**:
- **CRITICAL INSIGHT** (line 2 comment): "This is required because sc.exe start/stop doesn't reload the driver binary"
- **THE CORRECT way to reload driver after rebuild**:
  1. `netcfg.exe -u MS_IntelAvbFilter` → Unload old binary from memory
  2. `install_ndis_filter.bat` → Install fresh binary from disk
  3. `sc.exe start IntelAvbFilter` → Start service with NEW binary
  4. `comprehensive_ioctl_test.exe` → Verify new code works
- Mentions checking DebugView for '!!! DIAG:' messages

**Why this matters**: `sc stop + sc start` does NOT reload the binary! Driver stays in memory!

**Assessment**: ESSENTIAL developer workflow script - ensures you test the code you JUST compiled, not cached binary

**DIFFERENT from Force-Driver-Reload.ps1**:
- force_driver_reload.ps1: Binary reload workflow (netcfg -u required)
- Force-Driver-Reload.ps1: Handles stuck service states (force stop, unbind adapters)

**Decision**: KEEP BOTH - different purposes! One reloads binary, one handles stuck states

---

#### `Check-Driver-Status.ps1` ⭐ (107 lines - CANONICAL CANDIDATE per ADR)
**What it ACTUALLY does**:
- [1] Checks installed driver in DriverStore
- [2] Checks local Release build
- [3] Compares timestamps (warns if installed is older than local)
- [4] Checks service status via Get-Service
- Clean output with color coding
- Diagnostic information

**Unique features**: Compares installed vs local build timestamps!

**Assessment**: EXCELLENT canonical script - unique diagnostic value

---

## TEST SCRIPTS (Detailed Analysis)

#### `test_driver.ps1` (348 lines - CANONICAL CANDIDATE)
**What it ACTUALLY does**:
- Parameters: -SkipHardwareCheck, -RunAllTests, -CollectLogs
- Requires Administrator
- Helper functions: Write-Step, Write-Success, Write-Failure, Write-Info
- Comprehensive test suite banner
- (Need to read lines 50-348 for full functionality)

**Assessment**: Another canonical candidate - comprehensive test suite

**DUPLICATE vs run_tests.ps1?** Need to compare functionality:
- run_tests.ps1: Runs specific test executables (avb_test_i210.exe, avb_test_i226.exe) with params
- test_driver.ps1: Appears to be comprehensive test suite with hardware checks, log collection

**Decision**: Likely DIFFERENT purposes - keep both or merge best features

---

## SUMMARY OF FINDINGS (Updated)

### Confirmed Duplicates
1. **Build-AllTests-Honest.ps1** vs **Build-AllTests-TrulyHonest.ps1**: Same purpose (pick best features)
2. **Install-Now.bat** (minimal, dangerous) - No cleanup step, DEPRECATE

### Canonical Script Candidates (Confirmed)
1. **Setup-Driver.ps1**: `setup_driver.ps1` (parameterized, multi-function) ⭐
2. **Build-Driver.ps1**: Base on `install_filter_proper.bat` (reference NDIS install) + `setup_driver.ps1` parameters ⭐
2. **Build-Driver.ps1**: `Build-And-Sign-Driver.ps1` (comprehensive) ⭐
3. **Test-Driver.ps1**: `run_tests.ps1` OR `test_driver.ps1` (need comparison) ⭐
4. **Check-Driver-Status.ps1**: Exists, EXCELLENT diagnostic tool (timestamp comparison) ⭐
5. **Force-Driver-Reload.ps1**: KEEP BOTH - `force_driver_reload.ps1` (binary reload) + `Force-Driver-Reload.ps1` (stuck states)
### Scripts to Deprecate (Confirmed)
- `Install-Now.bat`: No cleanup step - dangerous, causes conflicts
- `install_devcon_method.bat`: Uses deprecated devcon.exe (not in modern WDK)
- `Enable-TestSigning.bat`: Subset of setup_driver.ps1 -EnableTestSigning
- Build-AllTests variants: Consolidate to one (take best features from both)

### Special-Purpose Scripts (Keep Separate)
- `install_certificate_method.bat`: Secure Boot compatible (pnputil + cert)
- `Complete-Driver-Setup.bat`: Release builds with cert extraction
- `Install-Debug-Driver.bat`: Debug builds with cert verification
- `troubleshoot_certificates.ps1`: Diagnostic tool
- `setup_hyperv_*`: Hyper-V VM development

### Different Implementations (NOT Duplicates!)
- **Setup-Driver.ps1** (interactive) vs **setup_driver.ps1** (parameterized)
- **Complete-Driver-Setup.bat** (Release) vs **Install-Debug-Driver.bat** (Debug)
- **install_filter_proper.bat** ⭐ REFERENCE implementation for NDIS filter install
- **force_driver_reload.ps1** ⭐ CORRECT method to reload binary after rebuild
- **Setup-Driver.ps1** (interactive) vs **setup_driver.ps1** (parameterized)
- **Complete-Driver-Setup.bat** (Release + cert extraction) vs **Install-Debug-Driver.bat** (Debug + cert verification)
- **Install-NewDriver.bat** (Release, with test) vs **Install-Now.bat** (Debug, dangerous)
- **install_certificate_method.bat** (pnputil+cert) vs others (netcfg)
- **run_tests.ps1** (run test exes) vs **test_driver.ps1** (comprehensive suite)
- **Force-Driver-Reload.ps1** (stuck states) vs **force_driver_reload.ps1** (binary reload

1. **Compare run_tests.ps1 vs test_driver.ps1** - determine if duplicates or complementary
2. **Read remaining scripts**:
   - Test scripts: Quick-Test-*.bat, Test-Release.bat, test_hardware_only.bat, etc.
   - Development scripts: reinstall-*, Smart-Update-Driver.bat, etc.
   - Build scripts: Sign-Driver.ps1, Generate-CATFile.ps1, etc.
3. **Test key canonical candidates** to verify they work
4. **Create consolidation plan** with clear rationale

## CRIhort scripts can be REFERENCE implementations** (short ≠ inferior!)
   - `install_filter_proper.bat` (42 lines) = THE CORRECT NDIS filter install method
   - `force_driver_reload.ps1` (30 lines) = THE CORRECT binary reload workflow
   - Name matters: "proper" and explanatory comments indicate authoritative implementations

2. **Key technical concepts revealed in comments**:
   - "sc.exe start/stop doesn't reload the driver binary" → Must use netcfg -u to unload
   - "Proper NDIS Filter Installation using netcfg" → This is the reference method
   - Read the WHY comments, not just the code!

3. **Scripts with similar names have DIFFERENT implementations**:
   - Setup-Driver.ps1 (interactive) ≠ setup_driver.ps1 (parameterized)
   - Force-Driver-Reload.ps1 (stuck states) ≠ force_driver_reload.ps1 (binary reload)
   - Complete-Driver-Setup.bat (Release) ≠ Install-Debug-Driver.bat (Debug)

4. **Installation methods differ**:
   - netcfg method: install_filter_proper.bat ⭐ (reference), most scripts
   - pnputil method: Setup-Driver.bat, install_certificate_method.bat
   - devcon method: install_devcon_method.bat (DEPRECATED - tool removed from WDK)

5. **Cannot consolidate based on names or line count - MUST read content AND comments

4. **Cannot consolidate based on names alone - MUST analyze content!**

## CONSOLIDATION PLAN (Phase 2 - Issue #27)

**Hybrid Approach**: Canonical Scripts + Thin Wrappers

### Architecture

**Layer 1: Canonical Scripts (7 - The "Engines")**
- `tools/Setup-Driver.ps1` - Full-featured setup with parameters
- `tools/Build-Driver.ps1` - Comprehensive build logic
- `tools/Test-Driver.ps1` - Test execution engine
- `tools/Manage-Certificate.ps1` - Already excellent (keep as-is)
- `tools/Sign-Driver.ps1` - Already exists (keep as-is)
- `tools/Force-Driver-Reload.ps1` - Handles stuck states (keep as-is)
- `tools/Check-Driver-Status.ps1` - Already excellent (keep as-is)

**Layer 2: Copilot-Friendly Wrappers (15-20 - The "Interface")**
- Thin wrappers (2-3 lines) that call canonical scripts with preset parameters
- Example: `Install-Debug-Driver.bat` → calls `Setup-Driver.ps1 -Configuration Debug -EnableTestSigning -InstallDriver`
- Clear names make them discoverable and prevent script proliferation

### Why This Works

✅ **Copilot-Friendly**: Clear script names (Install-Debug-Driver.bat) → obvious purpose
✅ **User-Friendly**: Browse folders, see clear names, double-click to run
✅ **Consolidated**: All logic in 7 canonical scripts
✅ **Maintainable**: Fix bug once in canonical script, all wrappers benefit
✅ **No Proliferation**: Wrappers are 2-3 lines (won't duplicate them)
✅ **Discoverable**: Clear names beat parameter memorization

### Implementation Plan

1. Create 7 canonical scripts based on best existing implementations
2. Create thin wrappers for common workflows in appropriate folders
3. Keep reference implementations (install_filter_proper.bat - educational)
4. Deprecate true duplicates to tools/archive/deprecated/

### Wrappers to Create

**tools/setup/** (setup wrappers):
- Install-Debug-Driver.bat → Setup-Driver.ps1 -Configuration Debug
- Install-Release-Driver.bat → Setup-Driver.ps1 -Configuration Release
- Enable-TestSigning.bat → Setup-Driver.ps1 -EnableTestSigning
- Uninstall-Driver.bat → Setup-Driver.ps1 -UninstallDriver

**tools/build/** (build wrappers):
- Build-Debug.bat → Build-Driver.ps1 -Configuration Debug
- Build-Release.bat → Build-Driver.ps1 -Configuration Release
- Build-And-Sign.bat → Build-Driver.ps1 -Sign

**tools/test/** (test wrappers):
- Quick-Test-Debug.bat → Test-Driver.ps1 -Configuration Debug -Quick
- Quick-Test-Release.bat → Test-Driver.ps1 -Configuration Release -Quick
- Run-Full-Tests.bat → Test-Driver.ps1 -Full

**tools/development/** (dev wrappers):
- Reload-Driver-Binary.bat → calls force_driver_reload.ps1 (keep 30-line version)
- Quick-Reinstall-Debug.bat → Setup-Driver.ps1 -Configuration Debug -Reinstall

### Scripts to Keep As-Is (Reference/Educational)

- **install_filter_proper.bat** (42 lines) - THE reference NDIS filter install
- **force_driver_reload.ps1** (30 lines) - THE correct binary reload method
- **install_certificate_method.bat** - Secure Boot method (pnputil)
- **troubleshoot_certificates.ps1** - Diagnostic tool
- **setup_hyperv_*.bat** - Special environment setup

### Scripts to Deprecate (True Duplicates)

**Move to tools/archive/deprecated/**:
- Build-AllTests-TrulyHonest.ps1 (duplicate of Build-AllTests-Honest.ps1)
- Install-Now.bat (dangerous - no cleanup)
- install_devcon_method.bat (uses deprecated devcon.exe)
- Test-Real-Release.bat (duplicate functionality)
- Complete-Driver-Setup.bat (replaced by wrappers)

