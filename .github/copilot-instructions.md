# Intel AVB Filter Driver - AI Coding Instructions

## Architecture Overview

This is a **Windows NDIS 6.30 lightweight filter driver** that bridges AVB/TSN capabilities between user applications and Intel Ethernet hardware (I210, I219, I225, I226). The key architectural insight is the **three-layer abstraction**:

1. **NDIS Filter Layer** (`filter.c`, `device.c`) - Windows kernel driver that attaches to Intel adapters
2. **AVB Integration Layer** (`avb_integration.c/.h`) - Hardware abstraction bridge with IOCTL interface  
3. **Intel AVB Library** (`external/intel_avb/lib/`) - Cross-platform hardware access library

**Critical Data Flow**: User App → DeviceIoControl → NDIS Filter → Platform Operations → Intel Library → Hardware

## Essential Patterns

### Memory Management
- **Always use AVB-specific pool tags**: `FILTER_ALLOC_TAG` ('AvbM'), `FILTER_REQUEST_ID` ('AvbR'), `FILTER_TAG` ('AvbF')
- **NonPagedPool** for all contexts that might be accessed at DISPATCH_LEVEL
- **Zero memory on allocation**: Use `ExAllocatePoolZero()` pattern consistently

### Hardware Access Abstraction
The core pattern is **platform operations structure** in `avb_integration.c`:
```c
static const struct platform_ops ndis_platform_ops = {
    .init = AvbPlatformInit,
    .pci_read_config = AvbPciReadConfig,
    .mmio_read = AvbMmioRead,
    .mdio_read = AvbMdioRead,
    .read_timestamp = AvbReadTimestamp
};
```
**Never access hardware directly** - always go through these platform operations.

### IOCTL Processing Pattern
All AVB IOCTLs follow this pattern in `AvbHandleDeviceIoControl()`:
1. Validate buffer sizes (`inputBufferLength >= sizeof(REQUEST_TYPE)`)
2. Call Intel library function
3. Set `request->status` based on result
4. Set `information = sizeof(REQUEST_TYPE)`

### Debug Output Conventions
- **Entry/Exit tracing**: `DEBUGP(DL_TRACE, "==>FunctionName")` and `<==FunctionName`
- **Error reporting**: `DEBUGP(DL_ERROR, "Specific failure reason: 0x%x", errorCode)`
- **Hardware operations**: Always log register offsets, values, and results

## Critical Build Requirements

### Visual Studio Project Configuration
- **NDIS630=1** preprocessor definition is mandatory
- **Include paths must include**: `external\intel_avb\lib` for Intel library headers
- **Output name**: `ndislwf.sys` (not the project name)
- **Link with**: `ndis.lib` for NDIS functions

### Precompiled Headers
- **All .c files must include `precomp.h`** - no exceptions
- **precomp.h includes**: `ndis.h`, `filteruser.h`, project headers in dependency order

## Device Integration Patterns

### Filter Attachment
Each Intel adapter gets an `MS_FILTER` instance with embedded `AVB_DEVICE_CONTEXT`:
```c
// In MS_FILTER structure (filter.h):
PAVB_DEVICE_CONTEXT AvbContext;  // AVB integration context

// Initialization pattern in FilterAttach:
Status = AvbInitializeDevice(pFilter, &pFilter->AvbContext);
```

### Intel Device Identification
Device detection happens through PCI vendor/device ID checking:
```c
#define INTEL_VENDOR_ID 0x8086
// Supported devices: I210 (0x1533), I219 (0x15B7), I225 (0x15F2), I226 (0x3100)
```

## Cross-Component Communication

### User-Space to Kernel
- **Device name**: `\\\\.\\IntelAvbFilter` 
- **IOCTL codes**: Defined in `avb_integration.h` using `_NDIS_CONTROL_CODE` macro
- **Always use METHOD_BUFFERED** for IOCTL definitions

### Kernel to Hardware  
- **NDIS OID requests** for PCI config space access
- **Memory-mapped I/O** through NDIS resource management
- **Intel library abstraction** handles device-specific register layouts

## Testing and Debugging

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

## Critical Files Reference

- **`filter.h`**: Core NDIS filter definitions, MS_FILTER structure with AvbContext
- **`avb_integration.c`**: Main hardware abstraction implementation, IOCTL handlers
- **`device.c`**: IOCTL routing, connects user-space to AVB integration layer
- **`external/intel_avb/lib/intel_windows.c`**: Windows-specific hardware access using our IOCTL interface
- **`IntelAvbFilter.inf`**: Driver installation, filter class "scheduler", service auto-start

## External Dependencies

- **Intel AVB Library**: Git submodule at `external/intel_avb/`, provides hardware abstraction
- **Windows Driver Kit**: Required for NDIS headers and kernel development
- **NDIS 6.30**: Minimum version for required TSN/AVB features

When modifying this codebase, always consider the impact across all three architectural layers and maintain the platform operations abstraction pattern.
