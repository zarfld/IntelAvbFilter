# Development Scripts

**Purpose**: Scripts for active driver development (build-test-debug cycles)

## 🎯 CANONICAL SCRIPTS (Use These)

### Force-Driver-Reload.ps1 ✅
**THE** correct way to reload driver binary after rebuild.

**Why**: `sc stop/start` does NOT reload the .sys file from disk!

**Usage**:
```powershell
.\Force-Driver-Reload.ps1                            # Reload Release driver
.\Force-Driver-Reload.ps1 -Configuration Debug       # Reload Debug driver
.\Force-Driver-Reload.ps1 -TestAfterReload           # Run quick test after reload
.\Force-Driver-Reload.ps1 -QuickMode                 # Skip stuck service checks
```

**Consolidated from**:
- force_driver_reload.ps1 (simple reload)
- Smart-Update-Driver.bat (stuck service detection)
- Update-Driver-Quick.bat (quick update)

**Workflow**: Unbind → Stop → Remove → Install → Verify → Test

## Status and Diagnostics

- **Check-Driver-Status.ps1** (107 lines)
  - Compares installed driver timestamp vs local build
  - Shows service status
  - Warns if installed is older than local build

- **diagnose_capabilities.ps1** - Hardware capability diagnostics
- **enhanced_investigation_suite.ps1** - Deep investigation tools

## Quick Workflows

- **reinstall-and-test.bat** - Orchestrator script
  - Calls Install-Debug-Driver.bat
  - Runs 3 tests: test_minimal.exe, test_direct_clock.exe, test_clock_config.exe

- **Smart-Update-Driver.bat** - Auto-detects changes and updates
- **IntelAvbFilter-Cleanup.ps1** - Clean uninstall + registry cleanup

## Deprecated (Don't Use)

Moved to `tools/archive/deprecated/`:
- Update-Driver-Quick.bat (use Quick-Reinstall-Debug.bat)
