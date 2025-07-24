# Copilot Instructions - Intel AVB Filter Driver

## Project Overview

This is a Windows NDIS 6.30 lightweight filter driver that provides AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet controllers (I210, I219, I225, I226). The architecture creates a bridge between user-mode applications and hardware through three key layers.

## Architecture Flow

```
User Application → DeviceIoControl → NDIS Filter Driver → Intel AVB Library → Hardware
```

**Key components:**
- **NDIS Filter** (`filter.c/h`): Lightweight filter that attaches to Intel network adapters
- **AVB Integration** (`avb_integration.c/h`): Bridge layer handling IOCTLs and hardware abstraction
- **Intel AVB Library** (`external/intel_avb/lib/`): Cross-platform library with Windows-specific implementation
- **User API** (`avb_test.c`): Example of IOCTL-based communication

## Critical Architectural Patterns

### 1. Filter Module Context Pattern
Every filter instance has an `MS_FILTER` structure with an `AvbContext` field pointing to `AVB_DEVICE_CONTEXT`:
```cpp
typedef struct _MS_FILTER {
    // ... NDIS fields
    PVOID AvbContext;  // Points to AVB_DEVICE_CONTEXT
} MS_FILTER;
```

### 2. IOCTL Communication Pattern
User-mode apps communicate via device path `\\\\.\\IntelAvbFilter` using IOCTLs defined with `_NDIS_CONTROL_CODE(n, METHOD_BUFFERED)`:
```cpp
#define IOCTL_AVB_READ_REGISTER _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
```

### 3. Hardware Abstraction via Platform Operations
The Intel AVB library uses a platform operations structure (`intel_windows.c`) that routes all hardware access through NDIS filter IOCTLs, eliminating direct hardware access.

### 4. Pool Tag Convention
All memory allocations use AVB-specific pool tags for leak tracking:
- `'AvbR'` - AVB Request
- `'AvbM'` - AVB Memory  
- `'AvbF'` - AVB Filter

## Development Workflows

### Building
Open `IntelAvbFilter.sln` in Visual Studio with WDK installed. The project uses NDIS 6.30 (`FILTER_MINOR_NDIS_VERSION = 30`) for Windows 8+ compatibility with advanced TSN features.

### Testing
Build user-mode test app: `nmake -f avb_test.mak` then run `avb_test.exe`

### Debugging
Enable debug output via DebugView. Use these debug levels:
- `DL_TRACE` - Normal operation flow
- `DL_ERROR` - Error conditions with specific details
- `DL_WARN` - Warnings and recoverable issues

## Device-Specific Implementations

### Hardware Support Matrix
- **I210**: Basic IEEE 1588, 4 traffic classes
- **I219**: IEEE 1588 + MDIO PHY access
- **I225/I226**: Full TSN (TAS, FP, PTM), 8 traffic classes

### TSN Configuration Pattern
Use helper functions in `tsn_config.c` for device capability detection:
```cpp
if (AvbSupportsTas(deviceId)) {
    // Configure Time-Aware Shaper
    AvbGetDefaultTasConfig(&config);
}
```

## Integration Points

### NDIS Filter Integration
- Filter attaches only to Intel adapters (vendor ID 0x8086)
- Hardware access flows through NDIS OID requests rather than direct MMIO
- Device detection uses `AvbFindIntelFilterModule()` to locate appropriate filter instance

### Intel AVB Library Integration
- Library's Windows platform layer (`intel_windows.c`) communicates with filter via IOCTLs
- All hardware operations (PCI config, MMIO, MDIO, timestamps) route through filter
- Platform operations structure provides abstraction for cross-platform compatibility

### User-Mode API
- Applications open device handle to `\\\\.\\IntelAvbFilter`
- Use predefined IOCTL structures (e.g., `AVB_REGISTER_REQUEST`, `AVB_TIMESTAMP_REQUEST`)
- All operations are synchronous and return status in request structure

## Key Files for New Features

- **Add new IOCTL**: Update `avb_integration.h` (define), `avb_integration.c` (handler), `device.c` (dispatch)
- **Hardware support**: Extend device detection in `avb_integration.c`, add device-specific code to `external/intel_avb/lib/intel_*.c`
- **TSN features**: Use templates in `tsn_config.h/c`, implement hardware-specific logic in Intel library
- **Debug enhancement**: Add DEBUGP calls with appropriate debug levels throughout call paths

## Common Gotchas

- Always check `AvbContext` initialization before hardware operations
- IOCTL buffer sizes must be validated (input AND output buffer lengths)
- Hardware access is asynchronous - operations may need retry logic
- Pool tag mismatches indicate memory leaks - always use AVB-specific tags
- Device IDs determine feature availability - check capability before configuring TSN features
