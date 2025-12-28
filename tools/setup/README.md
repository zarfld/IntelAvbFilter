# Setup Scripts - Canonical Installation Tools

**Which script should I use?**

## Primary Scripts (Production Use)

### Install-Driver.ps1 (Canonical - 500+ lines, fully featured)
```powershell
# Install driver
.\Install-Driver.ps1 -Configuration Debug -InstallDriver

# Reinstall (uninstall + install)
.\Install-Driver.ps1 -Configuration Debug -Reinstall

# Uninstall only
.\Install-Driver.ps1 -Configuration Debug -UninstallDriver
```

**Features**:
- Adapter restart (triggers NDIS filter service creation)
- STOP_PENDING detection (offers reboot if stuck)
- Driver store cleanup (removes duplicate oem*.inf)
- Build timestamp check (warns if downgrading)
- Full command output (no suppression for debugging)

**Critical Fix**: Uses `sc.exe` explicitly (PowerShell `sc` = Set-Content alias)

### Install-Driver-Elevated.ps1 (Wrapper for VS Code Tasks)
```powershell
# Launches Install-Driver.ps1 with UAC elevation
.\Install-Driver-Elevated.ps1 -Configuration Debug -Action InstallDriver
.\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall
.\Install-Driver-Elevated.ps1 -Configuration Debug -Action UninstallDriver
```

**Usage**: Integrated with VS Code tasks (install-driver-debug, reinstall-driver-debug, uninstall-driver)

### Setup-Driver.ps1 (Complete Workflow)
```powershell
# Full setup with certificate installation
.\Setup-Driver.ps1 -Configuration Debug -EnableTestSigning -InstallDriver -CheckStatus
```

## Supporting Scripts

- **Install-Certificate.ps1** - Certificate management
- **troubleshoot_certificates.ps1** - Certificate diagnostics

## VS Code Tasks (Recommended)

Run from VS Code Command Palette (Ctrl+Shift+P → "Tasks: Run Task"):
- **install-driver-debug** - Build + Install (auto-elevates)
- **reinstall-driver-debug** - Rebuild + Reinstall (auto-elevates)
- **uninstall-driver** - Remove driver (auto-elevates)

**Benefits**: Automatic build dependency, UAC elevation, integrated terminal output

## Archived Scripts (Historical Reference)

Moved to `tools/archive/deprecated/` (15 scripts):
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

**Why archived**: Redundant with canonical Install-Driver.ps1 which incorporates all lessons learned

## Key Installation Steps (NDIS Filter Drivers)

1. Copy .sys to `C:\Windows\System32\drivers\`
2. `netcfg.exe -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter`
3. **Restart Intel network adapters** (Disable-NetAdapter → Enable-NetAdapter)
4. Verify service: `sc.exe query IntelAvbFilter` → STATE: 4 RUNNING

**Critical**: Adapter restart is THE step that triggers NDIS filter service creation!
