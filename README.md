# Intel AVB Filter Driver

A Windows NDIS 6.30 lightweight filter driver that provides AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet controllers.

[![CI — Standards Compliance](https://github.com/zarfld/IntelAvbFilter/actions/workflows/ci-standards-compliance.yml/badge.svg)](https://github.com/zarfld/IntelAvbFilter/actions/workflows/ci-standards-compliance.yml)
[![Traceability Check](https://github.com/zarfld/IntelAvbFilter/actions/workflows/traceability-check.yml/badge.svg)](https://github.com/zarfld/IntelAvbFilter/actions/workflows/traceability-check.yml)

## 🎯 **Project Status — April 2026**

### CI / Test Infrastructure

| Gate | Status |
|------|--------|
| Driver build (MSBuild, Debug + Release) | ✅ Passing |
| SSOT magic-numbers gate (`src/`) | ✅ Passing |
| Hardware unit tests (self-hosted, 6×I226-LM) | ✅ **513 / 513 executed, 0 failures** (107 skipped — no matching HW) |
| Hardware unit tests (self-hosted, I219) | ⚠️ **In progress** — I219 PHC frozen fix (#361) |
| IOCTL ABI compatibility | ✅ 20/20 |
| PTP timestamp event subscription | ✅ Passing (6 adapters) |
| SSOT & register verification | ✅ Passing |
| GitHub Issues traceability | ✅ Passing |

**Self-hosted runner 1** (`6XI226MACHINE`): Windows, 6× Intel I226-LM, driver installed.  
**Self-hosted runner 2** (this machine): Windows, 1× Intel I219-LM + 1× Intel I210 + 1× Intel I226-LM, driver installed — added April 2026.  
**GitHub-hosted runners**: `windows-latest`, `windows-2022` — hardware-independent tests only.

### Hardware Test Results (2026-04-06, self-hosted, 6×I226-LM)

```
Test cases in suite        : 620
Skipped (no matching HW)   : 107
Actually executed          : 513
Passed                     : 513  (100%)
Failed                     :   0
```

> **Note**: CI run 2026-04-21 ([#361](https://github.com/zarfld/IntelAvbFilter/pulls/361)) shows
> new failures under investigation: I219 PHC frozen after get/set (`UT-PTP-GETSET-013`) and
> TC-PERF-TS threshold regressions across 6 adapters — fix in progress.

#### ✅ Verified Working

- **Device enumeration** — 6 Intel I226-LM adapters detected and opened
- **Multi-adapter context isolation** — per-handle adapter switching confirmed
- **Register access (MMIO)** — all read/write operations clean
- **PTP / IEEE 1588 clock** — SYSTIM incrementing; frequency adjust, get/set time, offset correction all passing
- **PTP timestamp event subscription** — TX/RX timestamp events delivered on all adapters
- **TAS (802.1Qbv) IOCTL** — configuration accepted
- **CBS (802.1Qav) IOCTL** — configuration accepted
- **Frame Preemption + PTM IOCTLs** — verified passing
- **IOCTL ABI stability** — 20/20 struct-size checks pass (no ABI regression)
- **ATDECC entity notification** — passing (regression fixed: `MAX_ATDECC_SUBSCRIPTIONS` 4→16)
- **Performance baseline captured** — PHC P50=2300 ns, P99=4372 ns; TX=336 ns, RX=2076 ns
- **NDIS LWF send / receive paths** — passing
- **SSOT register constants** — zero raw hex literals in `src/`

#### ⚠️ Known Failures / In Progress

| Test | Status | Root Cause / Notes |
|------|--------|--------------------|
| `test_zero_polling_overhead` TC-ZP-002 | ⚠️ Known HW | EITR0=33 024 µs HW interrupt coalescing — controlled by NDIS miniport; filter driver has no EITR0 control path |
| `test_s3_sleep_wake` TC-S3-002 | ⏸ Unconfirmed | S3 wake-up PHC preservation still pending; `FilterRestart` fix coded but needs controlled wake environment to verify |
| `test_tx_timestamp_retrieval` | 🔧 Ongoing | TX timestamp capture delta assertion not yet automated (Cat.3 gap — #199) |
| `test_event_log` TC-ETW-001 | 🔧 Feature gap | Driver ETW manifest not registered; Event ID 1 not emitted on init — fix in progress (#269) |
| `UT-PTP-GETSET-013` ForceSet Readback | 🔧 In progress | I219 PHC frozen after forced time set (~3.8 M ns divergence) — fix in #361 |
| `TC-PERF-TS-001/002/003` | 🔧 Under review | Timestamp latency exceeds thresholds on I219 path (median ~5 µs vs. 5 µs limit) — threshold or MMIO path review in #361 |

## 🔧 **Supported Intel Controllers**

### Comprehensive Device Support Status

| Controller | Detection | Register Access | PTP/1588 | TSN IOCTLs | Notes |
|------------|-----------|-----------------|----------|------------|-------|
| **Intel I226** | ✅ Verified | ✅ Verified | ✅ Verified | ✅ Verified | **Primary CI platform** — 6× I226-LM on runner 1; 1× on runner 2 |
| **Intel I225** | ✅ Code ready | ✅ Code ready | ✅ Code ready | ✅ Code ready | Near-identical to I226; needs dedicated hardware run |
| **Intel I219** | ✅ Verified | ✅ Verified | ⚠️ In progress | ✅ Verified | Hardware on runner 2 (added Apr 2026); PHC frozen fix in progress (#361) |
| **Intel I210** | ✅ Verified | ✅ Verified | ⚠️ In progress | ✅ Verified | Hardware on runner 2 (added Apr 2026); PTP path under test |
| **Intel I217** | ⚠️ Code ready | ⚠️ Code ready | ⚠️ Code ready | ✅ Code ready | Needs hardware testing |

**Not Supported**: Intel 82574L (lacks AVB/TSN hardware features)

## 📦 **Build Requirements**

### Prerequisites

#### **Critical: Windows Driver Kit (WDK) Version Requirements**

**⚠️ IMPORTANT: Specific WDK versions are required for NDIS 6.30 and KMDF compatibility**

**Recommended WDK Versions:**
- **Primary**: **WDK 10.0.22621** (Windows 11, version 22H2) - Latest stable
- **Alternative**: **WDK 10.0.19041** (Windows 10, version 2004) - Proven compatibility
- **Minimum**: **WDK 10.0.17763** (Windows 10, version 1809) - NDIS 6.30 minimum

**Download Links:**
- **Latest WDK**: https://developer.microsoft.com/windows/hardware/windows-driver-kit
- **Previous WDK Versions**: https://learn.microsoft.com/en-us/windows-hardware/drivers/other-wdk-downloads

#### **Visual Studio Compatibility Matrix:**
| WDK Version | Visual Studio | NDIS Support | KMDF Support | Status |
|-------------|---------------|--------------|--------------|---------|
| **WDK 11 (22621)** | **VS 2022** | ✅ NDIS 6.30+ | ✅ KMDF 1.33+ | **Recommended** |
| **WDK 10 (19041)** | **VS 2019/2022** | ✅ NDIS 6.30+ | ✅ KMDF 1.31+ | **Proven** |
| WDK 10 (17763) | VS 2017/2019 | ✅ NDIS 6.30 | ✅ KMDF 1.29+ | Minimum |

#### **Additional Requirements:**
- **Visual Studio 2019** (minimum) or **Visual Studio 2022** (recommended)
- **Intel Ethernet Controller** (I210, I217, I219, I225, or I226)
- **Windows 10 1809+** or **Windows 11** (for NDIS 6.30 runtime support)

### Build

#### **Step 1: Environment Setup**
```cmd
# Verify WDK installation
reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10

# Verify Visual Studio + WDK integration
# Open Visual Studio -> Extensions -> Verify "Windows Driver Kit" extension
```

#### **Step 2: Clone and Build**
```cmd
git clone --recursive https://github.com/zarfld/IntelAvbFilter.git
cd IntelAvbFilter

# Open IntelAvbFilter.sln in Visual Studio
# Verify project targets:
# - Configuration: Debug/Release
# - Platform: x64 (required for modern Intel controllers)
# - Windows SDK Version: 10.0.22621.0 (or compatible)
# - Platform Toolset: WindowsKernelModeDriver10.0

# Build command
msbuild IntelAvbFilter.sln /p:Configuration=Debug /p:Platform=x64
```

#### **Step 3: Build Verification**
```cmd
# Build output should show:
# - NDIS630=1 preprocessor definition
# - Proper WDK include paths
# - Successful linking with ndis.lib
# - Output: IntelAvbFilter.sys (filter driver binary)
```

### Build Troubleshooting

#### **Common WDK Issues:**

**1. "NDIS.h not found"**
- **Solution**: Verify WDK installation and Visual Studio integration
- **Check**: Project properties → Include Directories should contain WDK paths

**2. "Unresolved external symbol NdisXxx"**
- **Solution**: Add `ndis.lib` to additional dependencies
- **Check**: Project properties → Linker → Input → Additional Dependencies

**3. "Incompatible KMDF version"**
- **Solution**: Use KMDF coinstaller matching your WDK version
- **Check**: Download from WDK redistributables or use installed version

**4. "Target platform version mismatch"**
- **Solution**: Set Windows SDK version to match your WDK installation
- **Recommended**: 10.0.22621.0 for WDK 11

## 🚀 **Installation**

All installation features are consolidated in `tools/setup/Install-Driver.ps1`. Run from an **Administrator** PowerShell prompt in the repo root.

#### **Step 1: Enable Test Signing and Install Driver**

```powershell
# Check current driver and certificate status
.\tools\setup\Install-Driver.ps1 -CheckStatus

# Enable test signing (requires reboot)
.\tools\setup\Install-Driver.ps1 -EnableTestSigning
shutdown /r /t 0

# After reboot, install the driver
.\tools\setup\Install-Driver.ps1 -Configuration Debug -InstallDriver
```

#### **Step 2: Reinstall or Uninstall**

```powershell
# Clean reinstall (uninstall + install)
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall

# Uninstall only
.\tools\setup\Install-Driver.ps1 -Configuration Debug -UninstallDriver
```

#### **Certificate Issues**

`Install-Driver.ps1` handles both required certificate stores (Trusted Root + Trusted Publishers) automatically:

```powershell
# Diagnose and fix certificate problems
.\tools\setup\Install-Driver.ps1 -CheckStatus
.\tools\setup\Install-Driver.ps1 -Configuration Debug -Reinstall
```

### **VS Code Tasks** (Recommended for Development)

Run from Command Palette (`Ctrl+Shift+P → Tasks: Run Task`):
- **install-driver-debug** — Build + Install (auto-elevates via UAC)
- **reinstall-driver-debug** — Rebuild + Reinstall (auto-elevates)
- **uninstall-driver** — Remove driver (auto-elevates)

### **Manual Installation** (Advanced Users)

For complete manual installation instructions see [docs/installation-guide.md](docs/installation-guide.md).

```cmd
# Enable test signing (required for development)
bcdedit /set testsigning on
# Reboot required

# Install driver (requires real Intel hardware)
pnputil /add-driver IntelAvbFilter.inf /install

# Verify installation
dir \\.\IntelAvbFilter  # Should succeed if Intel hardware present
```

## 📖 **Usage**

After installing the driver, run tests using the canonical test runner:

```powershell
# Quick verification (2 tests — fast, confirms driver is alive)
.\tools\test\Run-Tests.ps1 -Configuration Debug -Quick

# Full test suite (6 phases — comprehensive hardware validation)
.\tools\test\Run-Tests.ps1 -Configuration Debug -Full

# Full suite with log capture
.\tools\test\Run-Tests.ps1 -Configuration Debug -Full -CollectLogs

# Run a specific test executable
.\tools\test\Run-Tests.ps1 -Configuration Debug -TestExecutable avb_test_i226.exe
```

See [tools/test/README.md](tools/test/README.md) for all `-TestExecutable` names and parameters.

To build the driver:

```powershell
# Debug build (incremental)
.\tools\build\Build-Driver.ps1 -Configuration Debug

# Release build
.\tools\build\Build-Driver.ps1 -Configuration Release

# Clean rebuild
.\tools\build\Build-Driver.ps1 -Configuration Debug -Clean
```

See [tools/build/README.md](tools/build/README.md) for full build script documentation.

## 🤝 **Contributing**

1. Create a GitHub Issue before starting work (`Fixes #N` or `Implements #N` in your PR)
2. Write a failing test before implementing — TDD (Red → Green → Refactor)
3. Ensure all CI gates pass before requesting review
4. Keep PRs small and focused (one logical change per PR)

Full development workflow, coding standards, and traceability requirements are in [`.github/copilot-instructions.md`](.github/copilot-instructions.md).

## 📄 **License**

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE) for the full text.

Derivative works must be distributed under the same license (copyleft). Commercial use is permitted provided the source remains open.
