# Intel AVB Filter Driver

A Windows NDIS 6.30 lightweight filter driver that provides AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet controllers.

[![CI — Standards Compliance](https://github.com/zarfld/IntelAvbFilter/actions/workflows/ci-standards-compliance.yml/badge.svg)](https://github.com/zarfld/IntelAvbFilter/actions/workflows/ci-standards-compliance.yml)
[![Traceability Check](https://github.com/zarfld/IntelAvbFilter/actions/workflows/traceability-check.yml/badge.svg)](https://github.com/zarfld/IntelAvbFilter/actions/workflows/traceability-check.yml)

## 🎯 **Project Status — March 2026**

### CI / Test Infrastructure

| Gate | Status |
|------|--------|
| Driver build (MSBuild, Debug + Release) | ✅ Passing |
| SSOT magic-numbers gate (`src/`) | ✅ Passing |
| Hardware unit tests (self-hosted, 6×I226-LM) | ✅ **106 / 113 passed (93.8%)** |
| IOCTL ABI compatibility | ✅ 20/20 |
| PTP timestamp event subscription | ✅ 98 / 0 passed/failed (6 adapters) |
| SSOT & register verification | ✅ Passing |
| GitHub Issues traceability | ✅ Passing |

**Self-hosted runner**: Windows, 6× Intel I226-LM, driver installed, build number 345.  
**GitHub-hosted runners**: `windows-latest`, `windows-2022` — hardware-independent tests only.

### Hardware Test Results (2026-03-23, self-hosted, 6×I226-LM)

```
Test executables available : 105
Total tests run            : 113
Passed                     : 106  (93.8%)
Failed                     :   7  (see Known Failures)
```

#### ✅ Verified Working

- **Device enumeration** — 6 Intel I226-LM adapters detected and opened
- **Multi-adapter context isolation** — per-handle adapter switching confirmed
- **Register access (MMIO)** — all read/write operations clean
- **PTP / IEEE 1588 clock** — SYSTIM incrementing; frequency adjust, get/set time, offset correction all passing
- **PTP timestamp event subscription** — 97 P / 0 F; TX/RX timestamp events delivered
- **TAS (802.1Qbv) IOCTL** — 10 P / 0 F, configuration accepted
- **CBS (802.1Qav) IOCTL** — 14 P / 0 F, configuration accepted
- **Frame Preemption + PTM IOCTLs** — verified passing
- **IOCTL ABI stability** — 20/20 struct-size checks pass (no ABI regression)
- **ATDECC entity notification** — 5 P / 0 F (regression fixed: `MAX_ATDECC_SUBSCRIPTIONS` 4→16)
- **Performance baseline captured** — PHC P50=2300 ns, P99=4372 ns; TX=336 ns, RX=2076 ns
- **NDIS LWF send / receive paths** — passing
- **SSOT register constants** — zero raw hex literals in `src/`

#### ⚠️ Known Failures / Limitations

| Test | Status | Root Cause / Notes |
|------|--------|--------------------|
| `test_zero_polling_overhead` TC-ZP-002 | ⚠️ Known HW | EITR0=33024 µs HW interrupt coalescing — expected hardware behavior |
| `test_s3_sleep_wake` | ⏸ Excluded from CI | `SetSuspendState` would suspend the runner; run manually on dedicated machine |
| `test_tx_timestamp_retrieval` | 🔧 Ongoing | TX timestamp capture path under investigation |
| `avb_test_i219` | 🔌 No hardware | No I219 adapter on CI runner |
| `test_event_log` | 🔧 Intermittent | Timing-sensitive; passes in isolation |
| `test_perf_regression` | 📋 Baseline only | COMPARE mode works; CAPTURE mode exits 1 by design |
| `test_magic_numbers` | ✅ Superseded | Replaced by CI SSOT static gate (`ssot-magic-numbers-gate` job) |

## 🔧 **Supported Intel Controllers**

### Comprehensive Device Support Status

| Controller | Detection | Register Access | PTP/1588 | TSN IOCTLs | Notes |
|------------|-----------|-----------------|----------|------------|-------|
| **Intel I226** | ✅ Verified | ✅ Verified | ✅ Verified | ✅ Verified | **Primary CI platform** — 6× on self-hosted runner |
| **Intel I225** | ✅ Code ready | ✅ Code ready | ✅ Code ready | ✅ Code ready | Near-identical to I226; needs dedicated hardware run |
| **Intel I219** | ✅ Code ready | ✅ Code ready | ⚠️ Code ready | ✅ Code ready | No I219 on CI runner — `avb_test_i219` skipped |
| **Intel I210** | ✅ Code ready | ✅ Code ready | ⚠️ Code ready | ✅ Code ready | No I210 on CI runner; needs dedicated hardware run |
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

### **Quick Installation Using Automated Scripts** ✅ RECOMMENDED

We provide PowerShell scripts to simplify the installation process:

#### **Step 1: Enable Test Signing and Install Driver**
```powershell
# Open PowerShell as Administrator
cd C:\Users\dzarf\source\repos\IntelAvbFilter

# Check current status
.\setup_driver.ps1 -CheckStatus

# Enable test signing (requires reboot)
.\setup_driver.ps1 -EnableTestSigning
shutdown /r /t 0

# After reboot, install the driver
.\setup_driver.ps1 -InstallDriver
```

#### **Step 2: Troubleshoot Certificate Issues (if needed)**
```powershell
# Diagnose certificate problems
.\troubleshoot_certificates.ps1

# Fix certificate installation automatically
.\troubleshoot_certificates.ps1 -FixCertificates

# Verify certificates are properly installed
.\troubleshoot_certificates.ps1
```

### **Manual Installation** (Advanced Users)

For complete manual installation instructions, see [INSTALLATION_GUIDE.md](INSTALLATION_GUIDE.md)

#### **Driver Installation Requirements:**
```cmd
# Enable test signing (required for development)
bcdedit /set testsigning on
# Reboot required

# Install driver (requires real Intel hardware)
pnputil /add-driver IntelAvbFilter.inf /install

# Verify installation
dir \\.\IntelAvbFilter  # Should succeed if Intel hardware present
```

### **Common Certificate Errors - SOLVED** ✅

**Problem**: Getting certificate errors even after installing certificate manually

**Root Cause**: Certificate needs to be in **BOTH** stores:
- Trusted Root Certification Authorities
- **Trusted Publishers** ← Most commonly forgotten!

**Solution**: Use our automated script:
```powershell
.\troubleshoot_certificates.ps1 -FixCertificates
```

This will automatically:
1. Find your IntelAvbFilter certificate
2. Remove old/stale certificates
3. Install to **BOTH** required certificate stores
4. Verify installation success

### **Uninstalling the Driver**

```powershell
# Using setup script
.\setup_driver.ps1 -UninstallDriver

# Manual method
netcfg -v -u MS_IntelAvbFilter

# Disable test signing (optional)
bcdedit /set testsigning off
shutdown /r /t 0
