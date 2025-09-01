# Intel AVB Filter Driver - IOCTL Interface Documentation

## Overview

The Intel AVB Filter Driver provides a comprehensive DeviceIoControl interface for user-mode applications to interact with Intel Ethernet controllers that support AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) features.

**Device Path**: `\\.\IntelAvbFilter`

**Driver Type**: NDIS 6.30 Lightweight Filter Driver  
**Interface**: Windows DeviceIoControl API  
**Hardware Support**: Intel I210, I217, I219, I225, I226 Ethernet Controllers

## Quick Start Example

```cpp
#include "include/avb_ioctl.h"

// Open device handle
HANDLE h = CreateFile(L"\\\\.\\IntelAvbFilter", 
                     GENERIC_READ | GENERIC_WRITE, 
                     0, NULL, OPEN_EXISTING, 
                     FILE_ATTRIBUTE_NORMAL, NULL);

if (h == INVALID_HANDLE_VALUE) {
    printf("Error: %lu\n", GetLastError());
    return;
}

// Initialize device subsystem
DWORD bytesReturned = 0;
DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytesReturned, NULL);

// Read a register (example: Device Control Register)
AVB_REGISTER_REQUEST regReq;
ZeroMemory(&regReq, sizeof(regReq));
regReq.offset = 0x00000; // CTRL register

if (DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, 
                   &regReq, sizeof(regReq), 
                   &regReq, sizeof(regReq), 
                   &bytesReturned, NULL)) {
    printf("CTRL register: 0x%08X\n", regReq.value);
}

CloseHandle(h);
```

## Available IOCTLs

### Device Management

#### `IOCTL_AVB_INIT_DEVICE` (0x00082450)
**Purpose**: Initialize AVB device subsystem and hardware access  
**Input**: None  
**Output**: None  
**Usage**: Must be called before other operations  

```cpp
DWORD bytesReturned = 0;
BOOL success = DeviceIoControl(hDevice, IOCTL_AVB_INIT_DEVICE, 
                              NULL, 0, NULL, 0, &bytesReturned, NULL);
```

#### `IOCTL_AVB_GET_DEVICE_INFO` (0x00082454)
**Purpose**: Retrieve device information string  
**Input**: `AVB_DEVICE_INFO_REQUEST.buffer_size` (size of buffer)  
**Output**: `AVB_DEVICE_INFO_REQUEST` with device info string  

```cpp
AVB_DEVICE_INFO_REQUEST infoReq;
ZeroMemory(&infoReq, sizeof(infoReq));
infoReq.buffer_size = sizeof(infoReq.device_info);

if (DeviceIoControl(hDevice, IOCTL_AVB_GET_DEVICE_INFO,
                   &infoReq, sizeof(infoReq),
                   &infoReq, sizeof(infoReq), &bytesReturned, NULL)) {
    printf("Device: %s (status=0x%08X)\n", infoReq.device_info, infoReq.status);
}
```

#### `IOCTL_AVB_GET_HW_STATE` (0x00082494)
**Purpose**: Query current hardware state and capabilities  
**Input**: None  
**Output**: `AVB_HW_STATE_QUERY`  

```cpp
AVB_HW_STATE_QUERY stateReq;
ZeroMemory(&stateReq, sizeof(stateReq));

if (DeviceIoControl(hDevice, IOCTL_AVB_GET_HW_STATE,
                   &stateReq, sizeof(stateReq),
                   &stateReq, sizeof(stateReq), &bytesReturned, NULL)) {
    printf("Hardware State: %lu, VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
           stateReq.hw_state, stateReq.vendor_id, stateReq.device_id, stateReq.capabilities);
}
```

### Register Access

#### `IOCTL_AVB_READ_REGISTER` (0x00082458)
**Purpose**: Read 32-bit MMIO register from Intel controller  
**Input**: `AVB_REGISTER_REQUEST.offset` (register offset)  
**Output**: `AVB_REGISTER_REQUEST.value` (register value)  

```cpp
AVB_REGISTER_REQUEST regReq;
ZeroMemory(&regReq, sizeof(regReq));
regReq.offset = 0x00008; // STATUS register

if (DeviceIoControl(hDevice, IOCTL_AVB_READ_REGISTER,
                   &regReq, sizeof(regReq),
                   &regReq, sizeof(regReq), &bytesReturned, NULL)) {
    printf("STATUS: 0x%08X (link %s)\n", regReq.value, 
           (regReq.value & 0x02) ? "UP" : "DOWN");
}
```

#### `IOCTL_AVB_WRITE_REGISTER` (0x0008245C)
**Purpose**: Write 32-bit MMIO register to Intel controller  
**Input**: `AVB_REGISTER_REQUEST.offset`, `AVB_REGISTER_REQUEST.value`  
**Output**: `AVB_REGISTER_REQUEST.status`  

```cpp
AVB_REGISTER_REQUEST regReq;
ZeroMemory(&regReq, sizeof(regReq));
regReq.offset = 0x00000; // CTRL register  
regReq.value = 0x40140041; // Example control value

if (DeviceIoControl(hDevice, IOCTL_AVB_WRITE_REGISTER,
                   &regReq, sizeof(regReq),
                   &regReq, sizeof(regReq), &bytesReturned, NULL)) {
    printf("CTRL register updated (status=0x%08X)\n", regReq.status);
}
```

### IEEE 1588 Precision Time Protocol (PTP)

#### `IOCTL_AVB_GET_TIMESTAMP` (0x00082460)
**Purpose**: Get current IEEE 1588 timestamp from hardware  
**Input**: `AVB_TIMESTAMP_REQUEST.clock_id` (optional, 0 for default)  
**Output**: `AVB_TIMESTAMP_REQUEST.timestamp` (64-bit nanosecond timestamp)  

```cpp
AVB_TIMESTAMP_REQUEST tsReq;
ZeroMemory(&tsReq, sizeof(tsReq));
tsReq.clock_id = 0; // Default hardware clock

if (DeviceIoControl(hDevice, IOCTL_AVB_GET_TIMESTAMP,
                   &tsReq, sizeof(tsReq),
                   &tsReq, sizeof(tsReq), &bytesReturned, NULL)) {
    printf("Hardware timestamp: %llu ns\n", tsReq.timestamp);
}
```

#### `IOCTL_AVB_SET_TIMESTAMP` (0x00082464)
**Purpose**: Set IEEE 1588 timestamp in hardware  
**Input**: `AVB_TIMESTAMP_REQUEST.timestamp`, `AVB_TIMESTAMP_REQUEST.clock_id`  
**Output**: `AVB_TIMESTAMP_REQUEST.status`  

```cpp
AVB_TIMESTAMP_REQUEST tsReq;
ZeroMemory(&tsReq, sizeof(tsReq));
tsReq.timestamp = 1000000000ULL; // 1 second in nanoseconds
tsReq.clock_id = 0;

if (DeviceIoControl(hDevice, IOCTL_AVB_SET_TIMESTAMP,
                   &tsReq, sizeof(tsReq),
                   &tsReq, sizeof(tsReq), &bytesReturned, NULL)) {
    printf("Timestamp set (status=0x%08X)\n", tsReq.status);
}
```

### Multi-Adapter Support

#### `IOCTL_AVB_ENUM_ADAPTERS` (0x0008247C)
**Purpose**: Enumerate available Intel adapters  
**Input**: `AVB_ENUM_REQUEST.index` (adapter index to query, 0-based)  
**Output**: `AVB_ENUM_REQUEST` with adapter info and total count  

```cpp
// Get total count and first adapter info
AVB_ENUM_REQUEST enumReq;
ZeroMemory(&enumReq, sizeof(enumReq));
enumReq.index = 0; // First adapter

if (DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                   &enumReq, sizeof(enumReq),
                   &enumReq, sizeof(enumReq), &bytesReturned, NULL)) {
    printf("Found %lu adapters\n", enumReq.count);
    printf("Adapter #0: VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
           enumReq.vendor_id, enumReq.device_id, enumReq.capabilities);
    
    // Enumerate remaining adapters
    for (ULONG i = 1; i < enumReq.count; i++) {
        enumReq.index = i;
        if (DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                           &enumReq, sizeof(enumReq),
                           &enumReq, sizeof(enumReq), &bytesReturned, NULL)) {
            printf("Adapter #%lu: VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
                   i, enumReq.vendor_id, enumReq.device_id, enumReq.capabilities);
        }
    }
}
```

#### `IOCTL_AVB_OPEN_ADAPTER` (0x00082480)
**Purpose**: Select specific adapter as active context  
**Input**: `AVB_OPEN_REQUEST.vendor_id`, `AVB_OPEN_REQUEST.device_id`  
**Output**: `AVB_OPEN_REQUEST.status`  

```cpp
// Select Intel I226 for subsequent operations
AVB_OPEN_REQUEST openReq;
ZeroMemory(&openReq, sizeof(openReq));
openReq.vendor_id = 0x8086;
openReq.device_id = 0x125B; // I226

if (DeviceIoControl(hDevice, IOCTL_AVB_OPEN_ADAPTER,
                   &openReq, sizeof(openReq),
                   &openReq, sizeof(openReq), &bytesReturned, NULL)) {
    if (openReq.status == 0) {
        printf("I226 adapter selected\n");
        // All subsequent IOCTLs will target the I226
    } else {
        printf("Failed to select I226 (status=0x%08X)\n", openReq.status);
    }
}
```

### TSN (Time-Sensitive Networking) Features

#### `IOCTL_AVB_SETUP_TAS` (0x00082468)
**Purpose**: Configure Time-Aware Shaper (TAS) for scheduled traffic  
**Input**: `AVB_TAS_REQUEST.config` (TSN TAS configuration)  
**Output**: `AVB_TAS_REQUEST.status`  

```cpp
AVB_TAS_REQUEST tasReq;
ZeroMemory(&tasReq, sizeof(tasReq));

// Configure 1ms cycle time with gate schedule
tasReq.config.base_time_s = 0;
tasReq.config.base_time_ns = 1000000; // Start in 1ms
tasReq.config.cycle_time_s = 0;
tasReq.config.cycle_time_ns = 1000000; // 1ms cycle

// Gate schedule: alternate between all queues and queue 0 only
tasReq.config.gate_states[0] = 0xFF; // All queues open
tasReq.config.gate_durations[0] = 500000; // 500?s
tasReq.config.gate_states[1] = 0x01; // Only queue 0
tasReq.config.gate_durations[1] = 500000; // 500?s

if (DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                   &tasReq, sizeof(tasReq),
                   &tasReq, sizeof(tasReq), &bytesReturned, NULL)) {
    printf("TAS configured (status=0x%08X)\n", tasReq.status);
} else {
    printf("TAS setup failed: %lu\n", GetLastError());
}
```

#### `IOCTL_AVB_SETUP_FP` (0x0008246C)
**Purpose**: Configure Frame Preemption for express traffic  
**Input**: `AVB_FP_REQUEST.config` (TSN FP configuration)  
**Output**: `AVB_FP_REQUEST.status`  

```cpp
AVB_FP_REQUEST fpReq;
ZeroMemory(&fpReq, sizeof(fpReq));

fpReq.config.preemptable_queues = 0xFE; // Queues 1-7 preemptable
fpReq.config.min_fragment_size = 64; // Minimum fragment size
fpReq.config.verify_disable = 0; // Enable verification

if (DeviceIoControl(hDevice, IOCTL_AVB_SETUP_FP,
                   &fpReq, sizeof(fpReq),
                   &fpReq, sizeof(fpReq), &bytesReturned, NULL)) {
    printf("Frame preemption configured (status=0x%08X)\n", fpReq.status);
} else {
    printf("FP setup failed: %lu\n", GetLastError());
}
```

#### `IOCTL_AVB_SETUP_PTM` (0x00082470)
**Purpose**: Configure PCIe Precision Time Measurement  
**Input**: `AVB_PTM_REQUEST.config` (PTM configuration)  
**Output**: `AVB_PTM_REQUEST.status`  

```cpp
AVB_PTM_REQUEST ptmReq;
ZeroMemory(&ptmReq, sizeof(ptmReq));

ptmReq.config.enabled = 1; // Enable PTM
ptmReq.config.clock_granularity = 16; // 16ns granularity

if (DeviceIoControl(hDevice, IOCTL_AVB_SETUP_PTM,
                   &ptmReq, sizeof(ptmReq),
                   &ptmReq, sizeof(ptmReq), &bytesReturned, NULL)) {
    printf("PTM configured (status=0x%08X)\n", ptmReq.status);
} else {
    printf("PTM setup failed: %lu\n", GetLastError());
}
```

### MDIO PHY Management

#### `IOCTL_AVB_MDIO_READ` (0x00082474)
**Purpose**: Read PHY register via MDIO interface  
**Input**: `AVB_MDIO_REQUEST.page`, `AVB_MDIO_REQUEST.reg`  
**Output**: `AVB_MDIO_REQUEST.value`  

```cpp
AVB_MDIO_REQUEST mdioReq;
ZeroMemory(&mdioReq, sizeof(mdioReq));
mdioReq.page = 0; // Main register page
mdioReq.reg = 1;  // BMSR (Basic Mode Status Register)

if (DeviceIoControl(hDevice, IOCTL_AVB_MDIO_READ,
                   &mdioReq, sizeof(mdioReq),
                   &mdioReq, sizeof(mdioReq), &bytesReturned, NULL)) {
    printf("MDIO PHY[0,1] (BMSR): 0x%04X\n", mdioReq.value);
    if (mdioReq.value & 0x0004) printf("  Link up\n");
    if (mdioReq.value & 0x0020) printf("  Auto-negotiation complete\n");
} else {
    printf("MDIO read failed: %lu\n", GetLastError());
}
```

#### `IOCTL_AVB_MDIO_WRITE` (0x00082478)
**Purpose**: Write PHY register via MDIO interface  
**Input**: `AVB_MDIO_REQUEST.page`, `AVB_MDIO_REQUEST.reg`, `AVB_MDIO_REQUEST.value`  
**Output**: `AVB_MDIO_REQUEST.status`  

```cpp
AVB_MDIO_REQUEST mdioReq;
ZeroMemory(&mdioReq, sizeof(mdioReq));
mdioReq.page = 0; // Main register page
mdioReq.reg = 0;  // BMCR (Basic Mode Control Register)
mdioReq.value = 0x1200; // Enable auto-negotiation and restart

if (DeviceIoControl(hDevice, IOCTL_AVB_MDIO_WRITE,
                   &mdioReq, sizeof(mdioReq),
                   &mdioReq, sizeof(mdioReq), &bytesReturned, NULL)) {
    printf("MDIO PHY auto-negotiation restarted (status=0x%08X)\n", mdioReq.status);
} else {
    printf("MDIO write failed: %lu\n", GetLastError());
}
```

### Credit-Based Shaping (QAV)

#### `IOCTL_AVB_SETUP_QAV` (0x0008248C)
**Purpose**: Configure Credit-Based Shaper for AVB traffic classes  
**Input**: `AVB_QAV_REQUEST` with shaper parameters  
**Output**: `AVB_QAV_REQUEST.status`  

```cpp
AVB_QAV_REQUEST qavReq;
ZeroMemory(&qavReq, sizeof(qavReq));

qavReq.tc = 1; // Traffic Class A (high priority)
qavReq.idle_slope = 75000000; // 75% of link bandwidth in bytes/sec  
qavReq.send_slope = -25000000; // Negative send slope
qavReq.hi_credit = 32768; // High credit limit in bytes
qavReq.lo_credit = -32768; // Low credit limit in bytes

if (DeviceIoControl(hDevice, IOCTL_AVB_SETUP_QAV,
                   &qavReq, sizeof(qavReq),
                   &qavReq, sizeof(qavReq), &bytesReturned, NULL)) {
    printf("QAV configured for TC%d (status=0x%08X)\n", qavReq.tc, qavReq.status);
} else {
    printf("QAV setup failed: %lu\n", GetLastError());
}
```

## Data Structures

### Core Request/Response Types

```cpp
// Basic register access
typedef struct AVB_REGISTER_REQUEST {
    avb_u32 offset;  // MMIO register offset (byte offset from BAR0)
    avb_u32 value;   // Register value (in for WRITE, out for READ)
    avb_u32 status;  // NTSTATUS return code
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

// Device information
typedef struct AVB_DEVICE_INFO_REQUEST {
    char device_info[1024]; // Device description string
    avb_u32 buffer_size;    // in/out: size of device_info used
    avb_u32 status;         // NTSTATUS return code  
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

// Timestamp operations
typedef struct AVB_TIMESTAMP_REQUEST {
    avb_u64 timestamp; // 64-bit nanosecond timestamp
    avb_u32 clock_id;  // Clock identifier (0 for default)
    avb_u32 status;    // NTSTATUS return code
} AVB_TIMESTAMP_REQUEST, *PAVB_TIMESTAMP_REQUEST;

// Adapter enumeration
typedef struct AVB_ENUM_REQUEST {
    avb_u32 index;        // in: adapter index (0-based)
    avb_u32 count;        // out: total adapter count
    avb_u16 vendor_id;    // out: PCI vendor ID
    avb_u16 device_id;    // out: PCI device ID
    avb_u32 capabilities; // out: capability bitmask
    avb_u32 status;       // out: NTSTATUS return code
} AVB_ENUM_REQUEST, *PAVB_ENUM_REQUEST;

// Hardware state query
typedef struct AVB_HW_STATE_QUERY {
    avb_u32 hw_state;     // Hardware state enum value
    avb_u16 vendor_id;    // PCI vendor ID
    avb_u16 device_id;    // PCI device ID
    avb_u32 capabilities; // Current capability bitmask
    avb_u32 reserved;     // Reserved for future use
} AVB_HW_STATE_QUERY, *PAVB_HW_STATE_QUERY;
```

## Hardware Capabilities

The driver reports capabilities via the `capabilities` field in enumeration and state queries:

```cpp
#define INTEL_CAP_BASIC_1588     0x00000001  // IEEE 1588 timestamping
#define INTEL_CAP_ENHANCED_TS    0x00000002  // Enhanced timestamp features  
#define INTEL_CAP_TSN_TAS        0x00000004  // Time-Aware Shaper
#define INTEL_CAP_TSN_FP         0x00000008  // Frame Preemption
#define INTEL_CAP_PCIe_PTM       0x00000010  // PCIe Precision Time Measurement
#define INTEL_CAP_2_5G           0x00000020  // 2.5 Gigabit support
#define INTEL_CAP_EEE            0x00000040  // Energy Efficient Ethernet
#define INTEL_CAP_MMIO           0x00000080  // Memory-mapped register access
#define INTEL_CAP_MDIO           0x00000100  // PHY management interface

// Capability checking example
if (capabilities & INTEL_CAP_TSN_TAS) {
    printf("Time-Aware Shaper supported\n");
    // Configure TAS...
} else {
    printf("TAS not supported on this controller\n");
}
```

### Per-Controller Capabilities

| Controller | Capabilities | Typical Value |
|------------|--------------|---------------|
| **Intel I210** | Basic PTP + MMIO | `0x00000083` |
| **Intel I217** | Basic PTP + MMIO | `0x00000081` |
| **Intel I219** | PTP + MMIO + MDIO | `0x00000183` |  
| **Intel I225** | Full TSN (no EEE) | `0x0000003F` |
| **Intel I226** | Full TSN + EEE | `0x000001BF` |

## Error Handling

All IOCTLs return Windows error codes via `GetLastError()` and NTSTATUS codes in the `status` field:

```cpp
// Common error handling pattern
AVB_REGISTER_REQUEST regReq;
ZeroMemory(&regReq, sizeof(regReq));
regReq.offset = 0x00000;

if (!DeviceIoControl(hDevice, IOCTL_AVB_READ_REGISTER, 
                    &regReq, sizeof(regReq), 
                    &regReq, sizeof(regReq), &bytesReturned, NULL)) {
    DWORD error = GetLastError();
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
            printf("Driver not loaded or no Intel hardware\n");
            break;
        case ERROR_INVALID_FUNCTION:
            printf("IOCTL not supported by this controller\n");
            break;
        case ERROR_DEVICE_NOT_READY:
            printf("Hardware not initialized\n");
            break;
        default:
            printf("IOCTL failed: %lu\n", error);
            break;
    }
} else if (regReq.status != 0) {
    printf("Operation failed: NTSTATUS=0x%08X\n", regReq.status);
}
```

### Common Error Scenarios

1. **Driver Not Loaded**: `CreateFile()` returns `ERROR_FILE_NOT_FOUND`
2. **No Intel Hardware**: `CreateFile()` succeeds, but IOCTLs return `ERROR_DEVICE_NOT_READY`
3. **Unsupported Feature**: `DeviceIoControl()` returns `ERROR_INVALID_FUNCTION`
4. **Hardware Access Failed**: Successful IOCTL but non-zero `status` field
5. **Invalid Parameters**: `ERROR_INVALID_PARAMETER` from `DeviceIoControl()`

## Advanced Usage Patterns

### Multi-Adapter Workflow

```cpp
// 1. Enumerate all Intel adapters
std::vector<AVB_ENUM_REQUEST> adapters;
AVB_ENUM_REQUEST enumReq;
ZeroMemory(&enumReq, sizeof(enumReq));

// Get total count
if (DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS, 
                   &enumReq, sizeof(enumReq), 
                   &enumReq, sizeof(enumReq), &bytesReturned, NULL)) {
    
    // Enumerate each adapter
    for (ULONG i = 0; i < enumReq.count; i++) {
        enumReq.index = i;
        if (DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                           &enumReq, sizeof(enumReq),
                           &enumReq, sizeof(enumReq), &bytesReturned, NULL)) {
            adapters.push_back(enumReq);
        }
    }
}

// 2. Select specific adapter type (e.g., I226 for TSN)
for (const auto& adapter : adapters) {
    if (adapter.device_id == 0x125B && // I226
        (adapter.capabilities & INTEL_CAP_TSN_TAS)) {
        
        AVB_OPEN_REQUEST openReq;
        ZeroMemory(&openReq, sizeof(openReq));
        openReq.vendor_id = adapter.vendor_id;
        openReq.device_id = adapter.device_id;
        
        if (DeviceIoControl(hDevice, IOCTL_AVB_OPEN_ADAPTER,
                           &openReq, sizeof(openReq),
                           &openReq, sizeof(openReq), &bytesReturned, NULL)) {
            printf("Selected I226 for TSN operations\n");
            break;
        }
    }
}

// 3. All subsequent IOCTLs target the selected adapter
```

### TSN Configuration Sequence

```cpp
// Complete TSN setup for real-time audio/video streaming
void ConfigureTSNForAVB(HANDLE hDevice) {
    DWORD bytesReturned = 0;
    
    // 1. Ensure I226 is selected (has full TSN support)
    AVB_OPEN_REQUEST openReq;
    ZeroMemory(&openReq, sizeof(openReq));
    openReq.vendor_id = 0x8086;
    openReq.device_id = 0x125B; // I226
    DeviceIoControl(hDevice, IOCTL_AVB_OPEN_ADAPTER, 
                   &openReq, sizeof(openReq), 
                   &openReq, sizeof(openReq), &bytesReturned, NULL);
    
    // 2. Configure Credit-Based Shaper for AVB Class A
    AVB_QAV_REQUEST qavReq;
    ZeroMemory(&qavReq, sizeof(qavReq));
    qavReq.tc = 1; // Traffic Class A (audio)
    qavReq.idle_slope = 75000000; // 75% of 1Gbps
    qavReq.send_slope = -25000000;
    qavReq.hi_credit = 32768;
    qavReq.lo_credit = -32768;
    DeviceIoControl(hDevice, IOCTL_AVB_SETUP_QAV,
                   &qavReq, sizeof(qavReq),
                   &qavReq, sizeof(qavReq), &bytesReturned, NULL);
    
    // 3. Configure Time-Aware Shaper for scheduled traffic
    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));
    tasReq.config.cycle_time_ns = 125000; // 125?s for 8kHz audio
    tasReq.config.gate_states[0] = 0x02; // Class A window
    tasReq.config.gate_durations[0] = 62500; // 50% of cycle
    tasReq.config.gate_states[1] = 0xFD; // Other traffic
    tasReq.config.gate_durations[1] = 62500; // 50% of cycle
    DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                   &tasReq, sizeof(tasReq),
                   &tasReq, sizeof(tasReq), &bytesReturned, NULL);
    
    // 4. Configure Frame Preemption for express traffic
    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    fpReq.config.preemptable_queues = 0xFC; // Queues 2-7 preemptable
    fpReq.config.min_fragment_size = 64;
    DeviceIoControl(hDevice, IOCTL_AVB_SETUP_FP,
                   &fpReq, sizeof(fpReq),
                   &fpReq, sizeof(fpReq), &bytesReturned, NULL);
    
    // 5. Synchronize to network time via PTP
    // (Application-specific timing synchronization)
}
```

### Timestamp Synchronization

```cpp
// High-precision timestamp capture for audio/video synchronization
void SynchronizeToNetworkTime(HANDLE hDevice) {
    AVB_TIMESTAMP_REQUEST tsReq;
    DWORD bytesReturned = 0;
    
    // Capture hardware timestamps for correlation
    ULONGLONG hwTimestamps[10];
    ULONGLONG sysTimestamps[10];
    
    for (int i = 0; i < 10; i++) {
        LARGE_INTEGER qpc1, qpc2, freq;
        QueryPerformanceFrequency(&freq);
        
        QueryPerformanceCounter(&qpc1);
        
        // Get hardware timestamp
        ZeroMemory(&tsReq, sizeof(tsReq));
        if (DeviceIoControl(hDevice, IOCTL_AVB_GET_TIMESTAMP,
                           &tsReq, sizeof(tsReq),
                           &tsReq, sizeof(tsReq), &bytesReturned, NULL)) {
            QueryPerformanceCounter(&qpc2);
            
            hwTimestamps[i] = tsReq.timestamp;
            sysTimestamps[i] = ((qpc1.QuadPart + qpc2.QuadPart) / 2) * 1000000000ULL / freq.QuadPart;
        }
        
        Sleep(1); // 1ms spacing
    }
    
    // Calculate correlation between hardware and system time
    // (Use for audio/video synchronization)
}
```

## Testing and Validation

The driver includes comprehensive test tools:

- **`avb_multi_adapter_test.exe`** - Multi-adapter enumeration and testing
- **`avb_i226_test.exe`** - I226-specific TSN feature testing  
- **`avb_i226_advanced_test.exe`** - Advanced I226 features
- **`avb_test_i210.exe`** - I210-specific PTP testing

Example test execution:
```cmd
# Comprehensive multi-adapter test
.\build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe

# Expected output:
# ? Device opened successfully
# ? Total Intel AVB adapters found: 2
# ? I226 capabilities: 0x000001BF (full TSN support)
# ? Register access working
```

## Current Status and Known Issues

### ? Working Features (Hardware Validated)
- **Device enumeration and multi-adapter support**: Fully functional
- **Register access (MMIO)**: All read/write operations working  
- **IEEE 1588 basic timestamping**: I226 working, I210 has issues
- **MDIO PHY management**: Full PHY register access on I226
- **Interrupt management**: EITR configuration functional
- **Basic queue management**: Priority setting working

### ? Known Issues (Under Investigation)
- **I210 PTP clock initialization**: Clock stuck at zero despite proper configuration
- **I226 TAS activation**: Enable bit won't stick despite complete setup sequence
- **I226 Frame Preemption**: Configuration not taking effect  
- **I226 EEE features**: Register writes being ignored by hardware
- **Speed configuration**: Limited to infrastructure capabilities

### ?? Recommendations for Service Teams
1. **Use multi-adapter enumeration first** to discover available hardware
2. **Check capabilities bitmask** before attempting TSN configuration
3. **Implement proper error handling** for unsupported features
4. **Test on I226 hardware** for best TSN feature support
5. **Validate PTP synchronization** before relying on timing accuracy

## See Also

- **Intel Ethernet Controller Datasheets**: Official hardware specifications
- **IEEE 802.1 Standards**: AVB/TSN protocol specifications  
- **Intel AVB Library Documentation**: Cross-platform hardware abstraction
- **Windows Driver Development**: NDIS filter driver architecture
- **SSOT Register Definitions**: `intel-ethernet-regs/` directory for register offsets

---

**Last Updated**: January 2025  
**Driver Version**: 1.0  
**Supported Controllers**: Intel I210, I217, I219, I225, I226  
**Windows Compatibility**: Windows 10 1809+, Windows 11