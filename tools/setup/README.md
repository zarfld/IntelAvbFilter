# Setup Scripts - Canonical Installation Tools

**Goal**: ONE canonical installation script with all features consolidated.

## Canonical Scripts (2 scripts total)

### Install-Driver.ps1 - THE Canonical Installation Script
**Location**: `tools/setup/Install-Driver.ps1`  
**Lines**: 508  
**Status**: ✅ CANONICAL - All installation features consolidated

```powershell
# Install driver
.\Install-Driver.ps1 -Configuration Debug -InstallDriver

# Reinstall (clean uninstall + install)
.\Install-Driver.ps1 -Configuration Debug -Reinstall

# Uninstall only
.\Install-Driver.ps1 -Configuration Debug -UninstallDriver

# Enable test signing (requires reboot)
.\Install-Driver.ps1 -EnableTestSigning

# Check driver status
.\Install-Driver.ps1 -CheckStatus
```

**Consolidated Features**:
- ✅ Driver installation (netcfg method for NDIS filters)
- ✅ Test signing enablement (from Setup-Driver.ps1)
- ✅ Certificate management (from Install-Certificate.ps1)
- ✅ Diagnostic checks (from troubleshoot_certificates.ps1)
- ✅ Clean reinstall (from Clean-Install-Driver.ps1)
- ✅ Adapter restart (triggers NDIS filter service creation)
- ✅ STOP_PENDING detection (offers reboot if stuck)
- ✅ Driver store cleanup (removes duplicate oem*.inf)
- ✅ Build timestamp check (warns if downgrading)

**Critical**: Uses `sc.exe` explicitly (PowerShell `sc` = Set-Content alias)

---

### Install-Driver-Elevated.ps1 - UAC Wrapper for VS Code Tasks
**Location**: `tools/setup/Install-Driver-Elevated.ps1`  
**Lines**: 16  
**Status**: ✅ CANONICAL - Required for VS Code task automation

```powershell
# Auto-elevate with UAC prompt
.\Install-Driver-Elevated.ps1 -Configuration Debug -Action InstallDriver
.\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall
.\Install-Driver-Elevated.ps1 -Configuration Debug -Action UninstallDriver
```

**Usage**: Integrated with VS Code tasks (install-driver-debug, reinstall-driver-debug, uninstall-driver)

---

## VS Code Tasks (Recommended Method)

Run from VS Code Command Palette (Ctrl+Shift+P → "Tasks: Run Task"):
- **install-driver-debug** - Build + Install (auto-elevates)
- **reinstall-driver-debug** - Rebuild + Reinstall (auto-elevates)
- **uninstall-driver** - Remove driver (auto-elevates)

**Benefits**: Automatic build dependency, UAC elevation, integrated terminal output

---

## Archived Scripts (tools/archive/deprecated/setup/)

**Archived**: 20 scripts consolidated into Install-Driver.ps1

### Previously Archived (15 scripts):
- Complete-Driver-Setup.bat
- Install-AvbFilter.ps1
- Install-Debug-Driver.bat, Install-NewDriver.bat, Install-Now.bat
- install_certificate_method.bat, install_devcon_method.bat
- install_filter_proper.bat (taught us the correct netcfg method)
- install_fixed_driver.bat
- install_ndis_filter.bat (taught us adapter restart)
- install_smart_test.bat
- Setup-Driver.bat, setup_driver.ps1
- setup_hyperv_development.bat, setup_hyperv_vm_complete.bat

### Just Archived (5 scripts - 2025-12-28):
- **Setup-Driver.ps1** → Use `Install-Driver.ps1 -EnableTestSigning -InstallDriver`
- **Install-Certificate.ps1** → Certificate features integrated into Install-Driver.ps1
- **troubleshoot_certificates.ps1** → Diagnostic features integrated into `Install-Driver.ps1 -CheckStatus`
- **Clean-Install-Driver.ps1** → Clean reinstall integrated into `Install-Driver.ps1 -Reinstall`
- **Fix-Deployment-Config.ps1** → One-time VS project fix (not installation related)

**Consolidation Achievement**: 20 scripts → 2 canonical scripts (90% reduction)

---

## Key Installation Steps (NDIS Filter Drivers)

1. Copy .sys to `C:\Windows\System32\drivers\`
2. `netcfg.exe -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter`
3. **Restart Intel network adapters** (Disable-NetAdapter → Enable-NetAdapter)
4. Verify service: `sc.exe query IntelAvbFilter` → STATE: 4 RUNNING

**Critical**: Adapter restart is THE step that triggers NDIS filter service creation!
