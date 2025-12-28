# Development Scripts

**Purpose**: Developer utilities for driver development workflow

## 🎯 Active Development Scripts

### Check-System.ps1
**Status diagnostic tool** - Compares installed vs local driver timestamps

**Usage**:
```powershell
.\Check-System.ps1
```

**What it checks**:
- Currently installed driver (location, size, timestamp)
- Local Release build (timestamp comparison)
- Service status (Running/Stopped)
- Device interface test (via avb_test_i226.exe)
- DebugView status (for kernel debug output)

**When to use**: Before starting development session to verify system state

---

### quick_start.ps1
**Interactive installation wizard** - Step-by-step driver setup guide

**Usage**:
```powershell
.\quick_start.ps1
```

**What it does**:
- Detects current system status (test signing, certificates, driver installation)
- Provides guided installation steps based on status
- Interactive prompts for each action
- Links to troubleshooting scripts

**When to use**: First-time setup or when helping new developers

---

## 📋 Separation of Concerns

Development scripts have been reorganized by **concern**:

### SETUP Concern → `tools/setup/`

**For driver installation, reinstallation, and configuration:**

| Old Script (archived) | Canonical Replacement | Example Command |
|-----------------------|----------------------|-----------------|
| force_driver_reload.ps1 | Install-Driver.ps1 -Reinstall | `.\tools\setup\Install-Driver.ps1 -Reinstall` |
| Force-Driver-Reload.ps1 | Install-Driver.ps1 -Reinstall | `.\tools\setup\Install-Driver.ps1 -Reinstall` |
| Smart-Update-Driver.bat | Install-Driver.ps1 -Reinstall | `.\tools\setup\Install-Driver.ps1 -Reinstall` |
| Update-Driver-Quick.bat | Install-Driver.ps1 -Reinstall | `.\tools\setup\Install-Driver.ps1 -Reinstall` |
| reinstall_debug_quick.bat | Install-Driver.ps1 -Reinstall -Configuration Debug | `.\tools\setup\Install-Driver.ps1 -Reinstall -Configuration Debug` |
| IntelAvbFilter-Cleanup.ps1 | Clean-Install-Driver.ps1 | `.\tools\setup\Clean-Install-Driver.ps1 -Configuration Debug` |
| fix_deployment_config.ps1 | Fix-Deployment-Config.ps1 | `.\tools\setup\Fix-Deployment-Config.ps1` |
| reinstall-and-test.bat | Install-Driver.ps1 + Run-Tests.ps1 | See below ↓ |

**Key canonical scripts**:
- **Install-Driver.ps1** - Main driver installation script
  - Parameters: `-Configuration {Debug|Release}`, `-Reinstall`, `-InstallDriver`, `-UninstallDriver`, `-CheckStatus`
  - Location: `tools/setup/Install-Driver.ps1`
- **Clean-Install-Driver.ps1** - Comprehensive clean install (removes ALL OEM packages)
  - Location: `tools/setup/Clean-Install-Driver.ps1`
- **Fix-Deployment-Config.ps1** - One-time VS deployment config fix
  - Location: `tools/setup/Fix-Deployment-Config.ps1`

---

### TEST Concern → `tools/test/`

**For test execution and orchestration:**

| Old Script (archived) | Canonical Replacement | Example Command |
|-----------------------|----------------------|-----------------|
| diagnose_capabilities.ps1 | Run-Tests.ps1 -Full | `.\tools\test\Run-Tests.ps1 -Configuration Debug -Full` |
| enhanced_investigation_suite.ps1 | Run-Tests.ps1 -Full | `.\tools\test\Run-Tests.ps1 -Configuration Debug -Full` |

**Key canonical script**:
- **Run-Tests.ps1** - Complete test orchestration
  - Parameters: `-Configuration {Debug|Release}`, `-Quick`, `-Full`, `-TestPattern`
  - Location: `tools/test/Run-Tests.ps1`

---

### REDUNDANT Scripts (archived)

These provided no unique functionality:

| Script | Why Redundant | Replacement |
|--------|---------------|-------------|
| Force-StartDriver.ps1 | Just `sc start IntelAvbFilter` | `sc start IntelAvbFilter` |
| Start-AvbDriver.ps1 | Just `sc start IntelAvbFilter` | `sc start IntelAvbFilter` |

---

## 📦 Archived Scripts

All deprecated scripts moved to concern-based archive:
- **tools/archive/deprecated/setup/** - 8 SETUP-concern scripts
- **tools/archive/deprecated/test/** - 2 TEST-concern scripts
- **tools/archive/deprecated/development/** - 2 REDUNDANT scripts

**Why archived**: Functionality consolidated into canonical scripts with better separation of concerns

---

## 🚀 Common Workflows

### Reload driver after rebuild
```powershell
# Clean reinstall (recommended)
.\tools\setup\Install-Driver.ps1 -Reinstall -Configuration Debug

# Comprehensive clean install (removes ALL OEM packages)
.\tools\setup\Clean-Install-Driver.ps1 -Configuration Debug
```

### Run full test suite
```powershell
.\tools\test\Run-Tests.ps1 -Configuration Debug -Full
```

### Check system status before work
```powershell
.\tools\development\Check-System.ps1
```

### First-time setup
```powershell
.\tools\development\quick_start.ps1
```

---

## 📚 Related Documentation

- **Setup Scripts**: `tools/setup/README.md`
- **Test Scripts**: `tools/test/README.md`
- **Build Scripts**: `tools/build/README.md`
