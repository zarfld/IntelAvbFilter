# Intel AVB Filter Driver Integration

This project integrates the Intel AVB library (from the `external\intel_avb` submodule) with an NDIS lightweight filter driver to provide AVB (Audio/Video Bridging) capabilities for Intel Ethernet controllers.

## Overview

The integration provides:
- Hardware abstraction layer for Intel Ethernet controllers (I210, I219, I225, I226)
- IEEE 1588 timestamping support
- Time-Sensitive Networking (TSN) features
- MDIO register access for PHY configuration
- User-mode interface through IOCTLs

## Architecture

```
User Application
       |
       | (DeviceIoControl)
       v
NDIS Filter Driver (avb_integration.c)
       |
       | (Platform Operations)
       v
Intel AVB Library (external/intel_avb/lib/)
       |
       | (Hardware Access)
       v
Intel Ethernet Controller Hardware
```

## Files Added/Modified

### New Files:
- `avb_integration.h` - Header for AVB integration
- `avb_integration.c` - Implementation of AVB integration
- `avb_test.c` - User-mode test application
- `avb_test.mak` - Makefile for test application

### Modified Files:
- `filter.h` - Added AvbContext to MS_FILTER structure
- `filter.c` - Added AVB context initialization/cleanup
- `device.c` - Added handling for AVB IOCTLs
- `precomp.h` - Added AVB integration header
- `IntelAvbFilter.vcxproj` - Added new source files and include paths

## IOCTLs Supported

| IOCTL | Purpose |
|-------|---------|
| `IOCTL_AVB_INIT_DEVICE` | Initialize AVB functionality |
| `IOCTL_AVB_GET_DEVICE_INFO` | Get device information |
| `IOCTL_AVB_READ_REGISTER` | Read hardware register |
| `IOCTL_AVB_WRITE_REGISTER` | Write hardware register |
| `IOCTL_AVB_GET_TIMESTAMP` | Read IEEE 1588 timestamp |
| `IOCTL_AVB_SET_TIMESTAMP` | Set IEEE 1588 timestamp |
| `IOCTL_AVB_SETUP_TAS` | Configure Time Aware Shaper |
| `IOCTL_AVB_SETUP_FP` | Configure Frame Preemption |
| `IOCTL_AVB_SETUP_PTM` | Configure PCIe Precision Time Measurement |
| `IOCTL_AVB_MDIO_READ` | Read MDIO register |
| `IOCTL_AVB_MDIO_WRITE` | Write MDIO register |

## Building

### Driver:
1. Open `IntelAvbFilter.sln` in Visual Studio
2. Ensure Windows Driver Kit (WDK) is installed
3. Select the appropriate configuration (Debug/Release, x64/ARM64)
4. Build the solution

### Test Application:
```cmd
cd /d "d:\Repos\IntelAvbFilter"
nmake -f avb_test.mak
```

## Installation

1. Build the driver
2. Copy `ndislwf.sys` and `IntelAvbFilter.inf` to a temporary directory
3. Install using Device Manager or:
   ```cmd
   pnputil /add-driver IntelAvbFilter.inf /install
   ```

## Usage

### Loading the Driver:
The driver will automatically attach to Intel Ethernet adapters when loaded.

### Testing:
```cmd
avb_test.exe
```

## Current Status

### ✅ Implemented:
- Basic driver framework with AVB integration
- IOCTL interface for user-mode communication
- Device context management
- Integration with Intel AVB library structure

### 🚧 Partially Implemented:
- Hardware access functions (skeleton implementations)
- Platform operations for Windows NDIS environment

### ❌ Not Yet Implemented:
- PCI configuration space access through NDIS
- MMIO register mapping and access
- MDIO register access through NDIS OIDs
- IEEE 1588 timestamp register access
- Hardware resource enumeration

## Hardware Access Implementation Notes

The current implementation provides a framework but requires completion of hardware access functions:

1. **PCI Configuration Access**: Needs implementation using NDIS APIs or WDM functions
2. **MMIO Access**: Requires mapping hardware registers through NDIS resource management
3. **MDIO Access**: Should use NDIS OIDs or direct register access for PHY communication
4. **Timestamp Access**: Needs to read/write IEEE 1588 timestamp registers

## Supported Intel Controllers

- **I210**: Full TSN support with IEEE 1588
- **I219**: Basic IEEE 1588 with MDIO PHY access
- **I225/I226**: Advanced TSN features including TAS and Frame Preemption

## Debug Output

Enable debug output by setting appropriate debug levels in the driver. Debug messages will appear in DebugView or kernel debugger.

## Known Limitations

1. Hardware access functions are not fully implemented
2. Device identification relies on manual configuration
3. Error handling could be more comprehensive
4. Performance optimization needed for production use

## Future Enhancements

1. Complete hardware access implementation
2. Add support for more Intel controller variants
3. Implement packet timestamping in the filter path
4. Add configuration through registry or INF file
5. Performance monitoring and statistics
6. Power management support

## Contributing

When adding new functionality:
1. Follow the existing code style
2. Add appropriate debug output
3. Update this README
4. Test with multiple Intel controller types if possible

## License

This code follows the same license as the Intel AVB library in the submodule.
