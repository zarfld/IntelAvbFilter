# Development Scripts

**Which script should I use?**

## Quick Start (Most Common)

```batch
Reload-Driver-Binary.bat     # Reload driver binary after rebuild
Quick-Reinstall-Debug.bat    # Fast reinstall debug build
Check-Driver-Status.bat      # Check if installed driver is up-to-date
```

## Driver Reload (Critical for Development)

**⚠️ Important**: `sc stop/start` does NOT reload the driver binary!

- **force_driver_reload.ps1** (30 lines) - THE correct binary reload
  - Comment: "This is required because sc.exe start/stop doesn't reload the driver binary"
  - Workflow: netcfg -u → install → sc start → test
  - Use this after every rebuild to test NEW code

- **Force-Driver-Reload.ps1** (148 lines) - Handles stuck driver states
  - Unbinds stuck network adapters
  - Clears service dependency locks
  - Use when driver won't unload normally

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
