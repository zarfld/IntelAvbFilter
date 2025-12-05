# Intel AVB Filter Driver

A Windows NDIS 6.30 lightweight filter driver that provides AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet controllers.

## 🎯 **HONEST STATUS UPDATE - January 2025**

**Build Status**: ✅ **WORKING** - Driver compiles successfully  
**Basic Features**: ✅ **FUNCTIONAL** - Device enumeration, register access working  
**TSN IOCTL Handlers**: ✅ **FIXED** - TAS/FP/PTM IOCTLs no longer return ERROR_INVALID_FUNCTION  
**Advanced Features**: ⚠️ **MIXED RESULTS** - Handler infrastructure working, some hardware activation issues remain

### **Hardware Testing Results** (Updated January 2025)

#### **✅ What's Actually Working:**
- **Device enumeration**: Functional (2 Intel adapters detected)
- **Multi-adapter context switching**: Functional
- **Register access infrastructure**: All MMIO operations working
- **TSN IOCTL handlers**: **✅ FIXED** - TAS/FP/PTM handlers now functional (no more Error 1)
- **I226 PTP clock**: Running properly (SYSTIM incrementing)
- **I226 MDIO PHY management**: Fully operational
- **I226 interrupt management**: EITR programming working
- **Basic queue management**: Priority setting functional

#### **❌ What Has Issues:**
- **I210 PTP clock**: Stuck at zero despite proper configuration
- **I226 TAS activation**: Enable bit doesn't stick despite complete setup
- **I226 Frame Preemption**: Configuration not taking effect
- **I226 EEE**: Register writes being ignored by hardware
- **I226 2.5G speed**: Currently limited to 100 Mbps (infrastructure?)

#### **⚠️ What Needs Investigation:**
- **I226 advanced TSN hardware activation**: May require Intel-specific initialization sequences
- **Speed configuration**: May be limited by network infrastructure
- **Power management features**: May require additional hardware prerequisites

### **🎉 Recent Fixes (January 2025)**

#### **TSN IOCTL Handler Fix - COMPLETED** ✅
**Issue**: TAS, Frame Preemption, and PTM IOCTLs returned `ERROR_INVALID_FUNCTION` (Error 1)  
**Root Cause**: Missing case handlers in IOCTL switch statement  
**Fix**: Added proper case handlers that call Intel library functions  
**Status**: **VERIFIED WORKING** with hardware testing

**Before Fix**:
```
❌ IOCTL_AVB_SETUP_TAS: Not implemented (Error: 1)
❌ IOCTL_AVB_SETUP_FP: Not implemented (Error: 1)
❌ IOCTL_AVB_SETUP_PTM: Not implemented (Error: 1)
```

**After Fix**:
```
✅ IOCTL_AVB_SETUP_TAS: Handler exists and succeeded (FIX WORKED)
✅ IOCTL_AVB_SETUP_FP: Handler exists and succeeded (FIX WORKED)  
✅ IOCTL_AVB_SETUP_PTM: Handler exists, returned error 31 (FIX WORKED)
```

## 🔧 **Supported Intel Controllers**

### **Comprehensive Device Support Status:**

| Controller | Basic Detection | Register Access | PTP/1588 | TSN IOCTLs | TSN Hardware | Current Issues |
|------------|----------------|-----------------|----------|------------|--------------|----------------|
| **Intel I210** | ✅ Working | ✅ Working | ❌ Clock stuck | ✅ **Ready** | ❌ Limited | PTP initialization |
| **Intel I217** | ⚠️ Code ready | ⚠️ Ready | ⚠️ Ready | ✅ **Ready** | ❌ Limited | Needs hardware testing |
| **Intel I219** | ✅ Working | ⚠️ Ready | ⚠️ Ready | ✅ **Ready** | ❌ Limited | Needs hardware testing |
| **Intel I225** | ✅ Working | ⚠️ Ready | ⚠️ Ready | ✅ **Ready** | ❌ TAS issues | Needs hardware testing |
| **Intel I226** | ✅ Working | ✅ Working | ✅ Working | ✅ **Working** | ❌ TAS/FP failing | Hardware activation needs work |

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
