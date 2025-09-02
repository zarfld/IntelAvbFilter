# Intel IGB Device Family Implementation

## Overview

This document describes the comprehensive Intel IGB (e1000) device family implementation added to the IntelAvbFilter project. The implementation follows the clean HAL architecture and provides device-specific support for legacy Intel Gigabit Ethernet controllers based on the Intel IGB driver specifications.

## Supported IGB Devices

| **Device Family** | **Device ID** | **Implementation** | **PTP Support** | **MDIO Support** | **Capabilities** |
|-------------------|---------------|-------------------|-----------------|------------------|------------------|
| **82575** | 0x10A7, 0x10A9, 0x10D6 | `intel_82575_impl.c` | Basic IEEE 1588 | ? Full MDIO | Basic PTP + MMIO |
| **82576** | 0x10C9, 0x10E6-0x10E8, 0x1526, 0x150A, 0x1518, 0x150D | Uses 82575 ops | Basic IEEE 1588 | ? Full MDIO | Basic PTP + MMIO |
| **82580** | 0x150E-0x1511, 0x1516, 0x1527 | `intel_82580_impl.c` | Enhanced IEEE 1588 | ? Full MDIO | Enhanced PTP + MMIO |
| **I350** | 0x1521-0x1524, 0x1546 | `intel_i350_impl.c` | Enhanced IEEE 1588 | ? Full MDIO | Enhanced PTP + Hardware TS |
| **I354** | 0x1F40, 0x1F41, 0x1F45 | Uses I350 ops | Enhanced IEEE 1588 | ? Full MDIO | Enhanced PTP + Hardware TS |

## Implementation Architecture

### Clean HAL Separation

Each IGB device implementation follows the established clean HAL architecture:

```
???????????????????????????????????????????????????????????
?                User Applications                         ?
???????????????????????????????????????????????????????????
                      ? DeviceIoControl
???????????????????????????????????????????????????????????
?         NDIS Filter Driver (filter.c)                   ?
???????????????????????????????????????????????????????????
                      ? IOCTL Dispatch
???????????????????????????????????????????????????????????
?    AVB Integration (avb_integration_fixed.c)            ?
???????????????????????????????????????????????????????????
                      ? Intel API Calls
???????????????????????????????????????????????????????????
?  Intel Kernel Real (intel_kernel_real.c)   CLEAN HAL   ?
???????????????????????????????????????????????????????????
                      ? intel_get_device_ops()
???????????????????????????????????????????????????????????
?        Device Registry (intel_device_registry.c)        ?
?  ????????????? ????????????? ????????????? ??????????? ?
?  ?   82575   ? ?   82580   ? ?   I350    ? ?  I354   ? ?
?  ?Basic PTP  ? ?Enhanced   ? ?Enhanced   ? ?Enhanced ? ?
?  ?+ MDIO     ? ?PTP + MDIO ? ?PTP + MDIO ? ?PTP+MDIO ? ?
?  ????????????? ????????????? ????????????? ??????????? ?
???????????????????????????????????????????????????????????
```

### Device-Specific Features

#### **Intel 82575** - Foundation IGB Device
- **File**: `devices/intel_82575_impl.c`
- **Capabilities**: `INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO`
- **PTP**: Basic IEEE 1588 with 4ns increment
- **MDIO**: Full PHY register access with e1000 bit field definitions
- **Registers**: Standard e1000 SYSTIM, TIMINCA, TSYNCRXCTL, TSYNCTXCTL
- **Target**: Legacy gigabit deployment with basic timestamping

#### **Intel 82576** - Enhanced Foundation  
- **Implementation**: Uses `e82575_ops` (compatible register layout)
- **Capabilities**: Same as 82575 with improved performance
- **PTP**: Basic IEEE 1588 compatible with 82575
- **MDIO**: Enhanced timing and reliability
- **Variants**: Multiple media types (Copper, Fiber, Serdes, Quad-port)

#### **Intel 82580** - Advanced IGB
- **File**: `devices/intel_82580_impl.c` 
- **Capabilities**: `INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO`
- **PTP**: Enhanced IEEE 1588 with 6ns increment and improved precision
- **MDIO**: Enhanced timing with 2000 iteration timeout
- **Special**: Uses `IGB_82580_TSYNC_SHIFT` (24-bit) for timestamp adjustment
- **Features**: Event packet detection, enhanced auxiliary functions

#### **Intel I350** - Premium IGB
- **File**: `devices/intel_i350_impl.c`
- **Capabilities**: `INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO`
- **PTP**: Premium IEEE 1588 with 8ns increment and hardware timestamping
- **MDIO**: Premium MDIO with enhanced error detection
- **Features**: 
  - Hardware Rx/Tx timestamping with dedicated registers
  - Enhanced auxiliary snapshot functions
  - Improved timestamp precision and reliability
  - Multiple timestamp capture modes

#### **Intel I354** - Backplane Variant
- **Implementation**: Uses `i350_ops` (register-compatible)
- **Capabilities**: Same as I350 with backplane-specific optimizations
- **PTP**: Same premium IEEE 1588 support as I350
- **Target**: Server/enterprise backplane deployments

## Register Specifications

All IGB implementations use register definitions from `external/intel_igb/src/e1000_regs.h`:

### Common e1000 Registers
```c
#define E1000_SYSTIML   0x0B600  /* System time register Low - RO */
#define E1000_SYSTIMH   0x0B604  /* System time register High - RO */
#define E1000_TIMINCA   0x0B608  /* Increment attributes register - RW */
#define E1000_TSAUXC    0x0B640  /* Timesync Auxiliary Control register */
#define E1000_TSYNCRXCTL 0x0B620 /* Rx Time Sync Control register - RW */
#define E1000_TSYNCTXCTL 0x0B614 /* Tx Time Sync Control register - RW */
#define E1000_MDIC      0x00020  /* MDI Control - RW */
```

### Device-Specific Timing Constants
```c
// 82575: Basic 4ns increment
#define TIMINCA_82575   0x08000004

// 82580: Enhanced 6ns increment  
#define TIMINCA_82580   0x80000006

// I350: Premium 8ns increment
#define TIMINCA_I350    0x80000008
```

## MDIO Implementation

All IGB devices implement comprehensive MDIO support using standardized e1000 bit field definitions:

### MDIC Register Fields
```c
#define E1000_MDIC_DATA_MASK    0x0000FFFF
#define E1000_MDIC_REG_MASK     0x001F0000
#define E1000_MDIC_REG_SHIFT    16
#define E1000_MDIC_PHY_MASK     0x03E00000
#define E1000_MDIC_PHY_SHIFT    21
#define E1000_MDIC_OP_MASK      0x0C000000
#define E1000_MDIC_OP_SHIFT     26
#define E1000_MDIC_R_MASK       0x10000000  /* Ready bit */
#define E1000_MDIC_I_MASK       0x20000000  /* Interrupt enable */
#define E1000_MDIC_E_MASK       0x40000000  /* Error bit */
```

### MDIO Operation Flow
1. **Build Command**: Assemble MDIC register value with PHY address, register, operation
2. **Write MDIC**: Issue command to hardware
3. **Poll Ready**: Wait for `E1000_MDIC_R_MASK` bit 
4. **Check Error**: Verify `E1000_MDIC_E_MASK` is clear
5. **Extract Data**: Read result from `E1000_MDIC_DATA_MASK` field

### Enhanced Timing per Device
- **82575/82576**: 1000 iteration timeout, 10?s delay
- **82580/I350**: 2000 iteration timeout, 5?s delay (enhanced performance)

## Device Identification

The `AvbGetIntelDeviceType()` function has been expanded to recognize all IGB device IDs:

```c
intel_device_type_t AvbGetIntelDeviceType(UINT16 did)
{ 
    switch(did) {
        // 82575 family
        case 0x10A7: return INTEL_DEVICE_82575;  // 82575EB Copper
        case 0x10A9: return INTEL_DEVICE_82575;  // 82575EB Fiber/Serdes  
        case 0x10D6: return INTEL_DEVICE_82575;  // 82575GB Quad Copper
        
        // 82576 family  
        case 0x10C9: return INTEL_DEVICE_82576;  // 82576 Gigabit Network Connection
        case 0x10E6: return INTEL_DEVICE_82576;  // 82576 Fiber
        // ... [additional variants]
        
        // 82580 family
        case 0x150E: return INTEL_DEVICE_82580;  // 82580 Copper
        case 0x150F: return INTEL_DEVICE_82580;  // 82580 Fiber  
        // ... [additional variants]
        
        // I350 family
        case 0x1521: return INTEL_DEVICE_I350;   // I350 Copper
        case 0x1522: return INTEL_DEVICE_I350;   // I350 Fiber
        // ... [additional variants]
        
        default: return INTEL_DEVICE_UNKNOWN; 
    }
}
```

## Capabilities Matrix

| **Device** | **Basic 1588** | **Enhanced TS** | **MMIO** | **MDIO** | **Hardware TS** | **Notes** |
|------------|----------------|-----------------|----------|----------|-----------------|-----------|
| 82575      | ?             | ?              | ?       | ?       | ?              | Foundation device |
| 82576      | ?             | ?              | ?       | ?       | ?              | Same ops as 82575 |
| 82580      | ?             | ?              | ?       | ?       | ?? Basic        | Enhanced precision |
| I350       | ?             | ?              | ?       | ?       | ?              | Full hardware TS |
| I354       | ?             | ?              | ?       | ?       | ?              | Same ops as I350 |

## Integration Status

### ? **COMPLETED INTEGRATION:**

1. **Device Implementations**: All 5 IGB device types implemented
2. **Device Registry**: All devices registered in `intel_device_registry.c`
3. **Device Identification**: All device IDs added to `AvbGetIntelDeviceType()`
4. **Capability Assignment**: Baseline capabilities set in `AvbBringUpHardware()`
5. **Build Integration**: All implementations compile successfully
6. **External Declarations**: All device operations properly declared

### ? **PRODUCTION READY FEATURES:**

- **Real MMIO Access**: All devices use production `MmMapIoSpace()` + `READ_REGISTER_ULONG()`
- **Hardware Register Access**: Direct e1000 register programming with proper bit field operations
- **PTP Clock Support**: Hardware SYSTIM register access for all devices
- **MDIO PHY Access**: Complete PHY register access with proper timing and error handling
- **Multi-Device Support**: Clean device separation prevents cross-contamination
- **Error Handling**: Comprehensive error detection and reporting
- **Debug Logging**: Detailed logging for hardware bring-up and operation

## Usage Examples

### Device Enumeration
```c
// Application detects all Intel adapters including IGB devices
AVB_ENUM_REQUEST enum_req = {0};
DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS, &enum_req, sizeof(enum_req), 
                &enum_req, sizeof(enum_req), &bytes, NULL);

// Results now include 82575, 82576, 82580, I350, I354 devices
```

### Device-Specific Operations  
```c
// Open specific IGB adapter (e.g., I350)
AVB_OPEN_REQUEST open_req = {0x1521, 0x8086}; // I350 Copper
DeviceIoControl(hDevice, IOCTL_AVB_OPEN_ADAPTER, &open_req, sizeof(open_req),
                &open_req, sizeof(open_req), &bytes, NULL);

// Read hardware timestamp from I350
AVB_TIMESTAMP_REQUEST ts_req = {0};
DeviceIoControl(hDevice, IOCTL_AVB_GET_TIMESTAMP, &ts_req, sizeof(ts_req),
                &ts_req, sizeof(ts_req), &bytes, NULL);
```

### Register Access
```c
// Read I350 enhanced PTP register
AVB_REGISTER_REQUEST reg_req = {0x0B640, 0}; // TSAUXC
DeviceIoControl(hDevice, IOCTL_AVB_READ_REGISTER, &reg_req, sizeof(reg_req),
                &reg_req, sizeof(reg_req), &bytes, NULL);
// I350 returns enhanced auxiliary control settings
```

## Benefits

### **Legacy Support**
- Extends AVB/TSN capabilities to older Intel hardware
- Provides upgrade path for legacy gigabit deployments  
- Maintains investment in existing 82575/82576/82580 hardware

### **Performance Scaling**
- 82575: Basic PTP for legacy compatibility
- 82580: Enhanced precision for improved timing
- I350: Premium hardware timestamping for critical applications

### **Production Deployment**
- Clean device separation prevents operational conflicts
- Comprehensive error handling ensures reliable operation
- Detailed logging supports troubleshooting and maintenance

### **Architecture Benefits**
- **Scalable**: Easy to add more IGB variants or newer Intel families
- **Maintainable**: Each device implementation is completely isolated
- **Testable**: Individual device implementations can be validated separately
- **Compatible**: Maintains compatibility with existing I210/I219/I226 implementations

The IGB device family implementation provides comprehensive legacy Intel Ethernet support while maintaining the clean HAL architecture and production-quality standards established for modern Intel devices. This extends the IntelAvbFilter's capabilities to support virtually all Intel Gigabit Ethernet controllers from the past decade.