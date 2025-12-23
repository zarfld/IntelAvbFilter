# Test Scripts

**Which script should I use?**

## Quick Start (Most Common)

```batch
Quick-Test-Debug.bat         # Fast test with debug build
Quick-Test-Release.bat       # Fast test with release build
Run-Full-Tests.bat           # Comprehensive test suite
```

## Advanced Testing

```powershell
Test-Driver.ps1              # Full-featured (348 lines)
  -Configuration Debug|Release
  -Quick                     # Fast verification
  -Full                      # All tests
  -SkipHardwareCheck
  -CollectLogs
```

## Test Executables

Test executables are built to `build\tools\avb_test\x64\{Debug|Release}\*.exe`:
- avb_test_i210_um.exe
- avb_test_i219.exe
- avb_test_i226.exe
- comprehensive_ioctl_test.exe
- ptp_clock_control_test.exe
- tsauxc_toggle_test.exe

## Special Cases

- **run_tests.ps1** - Simple test executor (no install/build)
- **test_hardware_only.bat** - Hardware verification only
- **run_complete_diagnostics.bat** - Extended diagnostics

## Deprecated (Don't Use)

Moved to `tools/archive/deprecated/`:
- Test-Real-Release.bat (duplicate functionality)
