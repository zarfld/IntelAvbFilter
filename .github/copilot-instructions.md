# Intel AVB Filter Driver - AI Coding Instructions

## Architecture Overview

This is a **Windows NDIS 6.30 lightweight filter driver** that bridges AVB/TSN capabilities between user applications and Intel Ethernet hardware. The key architectural insight is the **three-layer abstraction**:

1. **NDIS Filter Layer** (`filter.c`, `device.c`) - Windows kernel driver that attaches to Intel adapters
2. **AVB Integration Layer** (`avb_integration.c/.h`) - Hardware abstraction bridge with IOCTL interface  
3. **Intel AVB Library** (`external/intel_avb/lib/`) - Cross-platform hardware access library

**Critical Data Flow**: User App → DeviceIoControl → NDIS Filter → Platform Operations → Intel Library → Hardware

## ⚠️ **CURRENT IMPLEMENTATION STATUS - JANUARY 2025**

### What Actually Works ✅
- **NDIS Filter Infrastructure**: Complete and functional
- **Device Detection**: Successfully identifies Intel controllers
- **IOCTL Interface**: Full user-mode API implementation
- **Intel Library Integration**: Complete API compatibility layer
- **TSN Framework**: Advanced logic for Time-Aware Shaper and Frame Preemption
- **Build System**: Successfully compiles for x64/ARM64

### What Needs Work ❌
- **Hardware Access**: Currently **SIMULATED/STUBBED** - not real hardware access
- **MMIO Operations**: All memory-mapped I/O uses fake responses
- **Production Testing**: Cannot validate on real hardware
- **I217 Support**: Missing from device identification despite being implemented

## Essential Patterns (Updated for Reality)

### Memory Management
- **Always use AVB-specific pool tags**: `FILTER_ALLOC_TAG` ('AvbM'), `FILTER_REQUEST_ID` ('AvbR'), `FILTER_TAG` ('AvbF')
- **NonPagedPool** for all contexts that might be accessed at DISPATCH_LEVEL
- **Zero memory on allocation**: Use `ExAllocatePoolZero()` pattern consistently

### Hardware Access Abstraction ⚠️ **CURRENTLY SIMULATED**
The core pattern is **platform operations structure** in `avb_integration.c`:
```c
static const struct platform_ops ndis_platform_ops = {
    .init = AvbPlatformInit,
    .pci_read_config = AvbPciReadConfig,      // ← SIMULATED
    .mmio_read = AvbMmioRead,                 // ← SIMULATED
    .mdio_read = AvbMdioRead,                 // ← SIMULATED
    .read_timestamp = AvbReadTimestamp        // ← SIMULATED
};
```
**Current Reality**: These return fake values based on Intel specifications, not real hardware.

### IOCTL Processing Pattern ✅ **WORKING**
All AVB IOCTLs follow this pattern in `AvbHandleDeviceIoControl()`:
1. Validate buffer sizes (`inputBufferLength >= sizeof(REQUEST_TYPE)`)
2. Call Intel library function
3. Set `request->status` based on result
4. Set `information = sizeof(REQUEST_TYPE)`

### Debug Output Conventions ✅ **WORKING**
- **Entry/Exit tracing**: `DEBUGP(DL_TRACE, "==>FunctionName")` and `<==FunctionName`
- **Error reporting**: `DEBUGP(DL_ERROR, "Specific failure reason: 0x%x", errorCode)`
- **Hardware operations**: Logs show "simulated" operations, not real hardware

### TSN Configuration Pattern ✅ **LOGIC COMPLETE**
TSN features use configuration templates defined in `tsn_config.c`:
```c
// Example: Audio streaming configuration
const struct tsn_tas_config AVB_TAS_CONFIG_AUDIO = {
    .cycle_time = 125000,  // 125us cycle
    .num_entries = 4,
    .entries = { /* gate control entries */ }
};
```
**Device capability detection**: `AvbSupportsTas()`, `AvbSupportsFp()`, `AvbSupportsPtm()` functions work correctly.

## Critical Build Requirements ✅ **WORKING**

### Visual Studio Project Configuration
- **NDIS630=1** preprocessor definition is mandatory
- **Include paths must include**: `external\intel_avb\lib` for Intel library headers
- **Output name**: `ndislwf.sys` (not the project name)
- **Link with**: `ndis.lib` for NDIS functions

### Precompiled Headers
- **All .c files must include `precomp.h`** - no exceptions
- **precomp.h includes**: `ndis.h`, `filteruser.h`, `flt_dbg.h`, `filter.h`, `avb_integration.h`, `tsn_config.h` in dependency order

## Device Integration Patterns ✅ **WORKING**

### Filter Attachment
Each Intel adapter gets an `MS_FILTER` instance with embedded AVB context:
```c
// In MS_FILTER structure (filter.h):
PVOID AvbContext;  // Points to AVB_DEVICE_CONTEXT

// Initialization pattern in FilterAttach:
Status = AvbInitializeDevice(pFilter, &pFilter->AvbContext);
```

### Intel Device Identification ⚠️ **INCOMPLETE**
Device detection happens through PCI vendor/device ID checking:
```c
#define INTEL_VENDOR_ID 0x8086

// CURRENTLY SUPPORTED (incomplete list):
// I210 (0x1533), I219 (0x15B7), I225 (0x15F2), I226 (0x3100 - WRONG!)

// SHOULD SUPPORT (from actual code):
// I210: 0x1533, 0x1536, 0x1537, 0x1538, 0x157B
// I217: 0x153A, 0x153B  // ← MISSING but implemented!
// I219: 0x15B7, 0x15B8, 0x15D6, 0x15D7, 0x15D8, 0x0DC7, 0x1570, 0x15E3
// I225: 0x15F2, 0x15F3, 0x0D9F
// I226: 0x125B, 0x125C, 0x125D  // Not 0x3100!
```

**Hardware Specifications**: Detailed NIC specifications are available in `external/intel_avb/spec/`:
- **I210**: `333016 - I210_Datasheet_v_3_7.pdf`, `332763_I210_SpecUpdate_Rev3.0.md`
- **I217**: `i217-ethernet-controller-datasheet-2.md` ← **Missing from instructions**
- **I219**: `ethernet-connection-i219-datasheet.md`
- **I225/I226**: `2407151103_Intel-Altera-KTI226V-S-RKTU_C26159200.pdf`, `621661-Intel® Ethernet Controller I225-Public External Specification Update-v1.2.md`

## Cross-Component Communication ✅ **WORKING**

### User-Space to Kernel
- **Device name**: `\\Device\\IntelAvbFilter` (kernel), `\\DosDevices\\IntelAvbFilter` (user-mode)
- **IOCTL codes**: Defined in `avb_integration.h` using `_NDIS_CONTROL_CODE` macro
- **Always use METHOD_BUFFERED** for IOCTL definitions

### Kernel to Hardware ⚠️ **SIMULATED**
- **NDIS OID requests** for PCI config space access (works for device detection only)
- **Memory-mapped I/O** through NDIS resource management (NOT IMPLEMENTED - simulated)
- **Intel library abstraction** handles device-specific register layouts (with fake data)

## Testing and Debugging ✅ **WORKING**

### Build Commands
```cmd
# Driver build (Visual Studio required)
# Open IntelAvbFilter.sln, build for x64/ARM64

# Test application build
nmake -f avb_test.mak
```

### Debug Setup
- **Enable debug output**: Set debug levels in driver, use DebugView.exe
- **Kernel debugging**: Use DbgengKernelDebugger (configured in .vcxproj)
- **Driver installation**: `pnputil /add-driver IntelAvbFilter.inf /install`

**Note**: Debug output shows simulated operations, not real hardware access.

## Critical Files Reference (Updated Status)

- **`filter.h`**: ✅ Core NDIS filter definitions, MS_FILTER structure with AvbContext
- **`avb_integration.c`**: ⚠️ Hardware abstraction implementation, **SIMULATED hardware access**
- **`device.c`**: ✅ IOCTL routing, connects user-space to AVB integration layer
- **`tsn_config.c/.h`**: ✅ TSN configuration templates and device capability detection
- **`avb_hardware_access.c`**: ⚠️ **SIMULATED** hardware access using Intel spec patterns
- **`intel_kernel_real.c`**: ⚠️ TSN framework with **SIMULATED** register programming
- **`external/intel_avb/lib/intel_windows.c`**: ⚠️ Windows-specific **SIMULATED** hardware access
- **`IntelAvbFilter.inf`**: ✅ Driver installation, filter class "scheduler", service auto-start

## External Dependencies ✅ **AVAILABLE**

- **Intel AVB Library**: Git submodule at `external/intel_avb/`, provides hardware abstraction framework
- **Intel NIC Specifications**: Complete hardware datasheets in `external/intel_avb/spec/`
- **Windows Driver Kit**: Required for NDIS headers and kernel development
- **NDIS 6.30**: Minimum version for required TSN/AVB features

## 🚨 **CRITICAL IMPLEMENTATION GAPS**

### Priority 1: Real Hardware Access
1. **MMIO Implementation**: Replace all simulated register access with real memory mapping
2. **PCI Configuration**: Implement actual PCI config space access beyond device detection
3. **Hardware Testing**: Validate on real Intel controllers

### Priority 2: Device Support Completion
1. **Add I217 Support**: Include missing I217 device IDs (0x153A, 0x153B)
2. **Fix I226 Device IDs**: Correct from 0x3100 to 0x125B/0x125C/0x125D
3. **Complete Device Variants**: Add all known device ID variants

### Priority 3: Production Validation
1. **Hardware-in-Loop Testing**: Test on actual Intel hardware
2. **Performance Validation**: Measure real timing accuracy
3. **Error Handling**: Handle real hardware failure scenarios

## 🎯 **SUCCESS CRITERIA FOR PRODUCTION**

Implementation is production-ready when:
- [ ] All simulated hardware access replaced with real MMIO operations
- [ ] Hardware register access tested on actual Intel controllers
- [ ] I217 support added and tested
- [ ] Device ID table matches actual supported hardware
- [ ] TSN features validated on real I225/I226 hardware
- [ ] Performance meets timing requirements for AVB/TSN applications

---

**Current Status**: Architecture Complete - Hardware Implementation Required  
**Last Updated**: January 2025  
**Next Milestone**: Real MMIO Implementation

When modifying this codebase, prioritize replacing simulation with real hardware access while maintaining the excellent architectural patterns already established.
