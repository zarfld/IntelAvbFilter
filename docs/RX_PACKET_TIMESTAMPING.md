# RX Packet Timestamping Configuration Guide

## Overview

This guide documents the complete RX packet timestamping feature for Intel I210/I226 Ethernet controllers. RX packet timestamping captures hardware timestamps for received packets in the packet buffer, enabling precise time measurement for Time-Sensitive Networking (TSN) applications.

## Architecture

RX packet timestamping requires **three-level configuration**:

1. **Global Enable** - Allocate 16 bytes in packet buffer (RXPBSIZE.CFG_TS_EN)
2. **Per-Queue Enable** - Enable timestamping for specific queues (SRRCTL[n].TIMESTAMP)
3. **Timer Control** - Configure which SYSTIM timer is used (TSAUXC)

## IOCTLs for RX Packet Timestamping

### 1. **IOCTL_AVB_SET_RX_TIMESTAMP** (Code 41)

**Purpose**: Enable/disable 16-byte timestamp allocation in RX packet buffer.

**Register**: RXPBSIZE (0x2404), bit 29 (CFG_TS_EN)

**Input/Output**: `AVB_RX_TIMESTAMP_REQUEST`
```c
typedef struct AVB_RX_TIMESTAMP_REQUEST {
    avb_u32 enable;            // in: 1=enable, 0=disable
    avb_u32 previous_rxpbsize; // out: value before change
    avb_u32 current_rxpbsize;  // out: value after change
    avb_u32 requires_reset;    // out: 1=port reset needed
    avb_u32 status;            // out: NDIS_STATUS
} AVB_RX_TIMESTAMP_REQUEST;
```

**Important**: Changing CFG_TS_EN requires port software reset (CTRL.RST) to take effect.

**Usage**:
```c
AVB_RX_TIMESTAMP_REQUEST rxReq = {0};
rxReq.enable = 1;  // Enable 16-byte timestamp buffer

DeviceIoControl(hDevice, IOCTL_AVB_SET_RX_TIMESTAMP,
                &rxReq, sizeof(rxReq),
                &rxReq, sizeof(rxReq),
                &bytesReturned, NULL);

if (rxReq.requires_reset) {
    printf("Port software reset required!\n");
}
```

### 2. **IOCTL_AVB_SET_QUEUE_TIMESTAMP** (Code 42)

**Purpose**: Enable/disable hardware timestamping for a specific receive queue.

**Register**: SRRCTL[n] (base 0x0C00C, stride 0x40), bit 30 (TIMESTAMP)

**Input/Output**: `AVB_QUEUE_TIMESTAMP_REQUEST`
```c
typedef struct AVB_QUEUE_TIMESTAMP_REQUEST {
    avb_u32 queue_index;       // in: Queue index (0-3)
    avb_u32 enable;            // in: 1=enable, 0=disable
    avb_u32 previous_srrctl;   // out: value before change
    avb_u32 current_srrctl;    // out: value after change
    avb_u32 status;            // out: NDIS_STATUS
} AVB_QUEUE_TIMESTAMP_REQUEST;
```

**Requirements**:
- RXPBSIZE.CFG_TS_EN must be enabled first
- SRRCTL[n].DESCTYPE must be non-zero for timestamp writeback

**Usage**:
```c
AVB_QUEUE_TIMESTAMP_REQUEST queueReq = {0};
queueReq.queue_index = 0;  // Queue 0
queueReq.enable = 1;       // Enable timestamping

DeviceIoControl(hDevice, IOCTL_AVB_SET_QUEUE_TIMESTAMP,
                &queueReq, sizeof(queueReq),
                &queueReq, sizeof(queueReq),
                &bytesReturned, NULL);
```

### 3. **IOCTL_AVB_SET_HW_TIMESTAMPING** (Code 40)

**Purpose**: Control which SYSTIM timer is used for timestamping.

**Register**: TSAUXC (0x0B640), bits 31, 29, 28, 27 (timer disable bits)

**Input/Output**: `AVB_HW_TIMESTAMPING_REQUEST`
```c
typedef struct AVB_HW_TIMESTAMPING_REQUEST {
    avb_u32 enable;            // in: 1=enable, 0=disable
    avb_u32 timer_mask;        // in: bit 0=SYSTIM0, 1=SYSTIM1, etc.
    avb_u32 enable_target_time;// in: enable target time interrupts
    avb_u32 enable_aux_ts;     // in: enable aux timestamp on SDP
    avb_u32 previous_tsauxc;   // out: value before change
    avb_u32 current_tsauxc;    // out: value after change
    avb_u32 status;            // out: NDIS_STATUS
} AVB_HW_TIMESTAMPING_REQUEST;
```

**Usage**:
```c
AVB_HW_TIMESTAMPING_REQUEST tsReq = {0};
tsReq.enable = 1;
tsReq.timer_mask = 0x01;  // Use SYSTIM0 (primary timer)

DeviceIoControl(hDevice, IOCTL_AVB_SET_HW_TIMESTAMPING,
                &tsReq, sizeof(tsReq),
                &tsReq, sizeof(tsReq),
                &bytesReturned, NULL);
```

## Target Time and Auxiliary Timestamp IOCTLs

### 4. **IOCTL_AVB_SET_TARGET_TIME** (Code 43)

**Purpose**: Configure target time for time-triggered interrupts or SDP output.

**Registers**: 
- TRGTTIML0/H0 (0x0B644/0x0B648) - Target time 0
- TRGTTIML1/H1 (0x0B64C/0x0B650) - Target time 1

**Input/Output**: `AVB_TARGET_TIME_REQUEST`
```c
typedef struct AVB_TARGET_TIME_REQUEST {
    avb_u32 timer_index;       // in: Timer index (0 or 1)
    avb_u64 target_time;       // in: Target time in nanoseconds
    avb_u32 enable_interrupt;  // in: Enable interrupt when reached
    avb_u32 enable_sdp_output; // in: Enable SDP pin output
    avb_u32 sdp_mode;          // in: 0=level, 1=pulse, 2=clock
    avb_u32 previous_target;   // out: Previous target (verification)
    avb_u32 status;            // out: NDIS_STATUS
} AVB_TARGET_TIME_REQUEST;
```

**Usage**:
```c
// Get current time
AVB_CLOCK_CONFIG cfg;
DeviceIoControl(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG, ...);
ULONGLONG current_ns = ((ULONGLONG)cfg.systim_high << 32) | cfg.systim_low;

// Set target time 5 seconds in future
AVB_TARGET_TIME_REQUEST targetReq = {0};
targetReq.timer_index = 0;
targetReq.target_time = current_ns + 5000000000ULL;
targetReq.enable_interrupt = 1;

DeviceIoControl(hDevice, IOCTL_AVB_SET_TARGET_TIME, ...);
```

### 5. **IOCTL_AVB_GET_AUX_TIMESTAMP** (Code 44)

**Purpose**: Read captured timestamp from auxiliary timestamp registers.

**Registers**:
- AUXSTMPL0/H0 (0x0B65C/0x0B660) - Auxiliary timestamp 0
- AUXSTMPL1/H1 (0x0B664/0x0B668) - Auxiliary timestamp 1

**Input/Output**: `AVB_AUX_TIMESTAMP_REQUEST`
```c
typedef struct AVB_AUX_TIMESTAMP_REQUEST {
    avb_u32 timer_index;       // in: Timer index (0 or 1)
    avb_u64 timestamp;         // out: Captured timestamp in ns
    avb_u32 valid;             // out: 1=valid (AUTT flag set)
    avb_u32 clear_flag;        // in: 1=clear AUTT after reading
    avb_u32 status;            // out: NDIS_STATUS
} AVB_AUX_TIMESTAMP_REQUEST;
```

**Usage**:
```c
AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
auxReq.timer_index = 0;
auxReq.clear_flag = 1;  // Clear flag after reading

DeviceIoControl(hDevice, IOCTL_AVB_GET_AUX_TIMESTAMP,
                &auxReq, sizeof(auxReq),
                &auxReq, sizeof(auxReq),
                &bytesReturned, NULL);

if (auxReq.valid) {
    printf("Aux timestamp: %llu ns\n", auxReq.timestamp);
} else {
    printf("No SDP event captured yet\n");
}
```

## Complete Configuration Sequence

### Step-by-Step Setup

```c
HANDLE hDevice = CreateFileA("\\\\.\\IntelAvbFilter", ...);

// Step 1: Enable SYSTIM0 timer (clear bit 31 in TSAUXC)
AVB_HW_TIMESTAMPING_REQUEST tsReq = {0};
tsReq.enable = 1;
tsReq.timer_mask = 0x01;  // SYSTIM0
DeviceIoControl(hDevice, IOCTL_AVB_SET_HW_TIMESTAMPING, ...);

// Step 2: Enable 16-byte timestamp in packet buffer
AVB_RX_TIMESTAMP_REQUEST rxReq = {0};
rxReq.enable = 1;
DeviceIoControl(hDevice, IOCTL_AVB_SET_RX_TIMESTAMP, ...);

if (rxReq.requires_reset) {
    // Port software reset required (CTRL.RST)
    // WARNING: This will reset the adapter!
}

// Step 3: Enable per-queue timestamping (queue 0)
AVB_QUEUE_TIMESTAMP_REQUEST queueReq = {0};
queueReq.queue_index = 0;
queueReq.enable = 1;
DeviceIoControl(hDevice, IOCTL_AVB_SET_QUEUE_TIMESTAMP, ...);

// Done! RX packets on queue 0 will now have hardware timestamps
```

## Hardware Capabilities by Device

| Device | SYSTIM Timers | Target Time | Aux Timestamp | Queue Timestamp |
|--------|---------------|-------------|---------------|-----------------|
| I210   | 1 (SYSTIM0)   | 2 targets   | 2 channels    | 4 queues        |
| I211   | 1 (SYSTIM0)   | 2 targets   | 2 channels    | 2 queues        |
| I225   | 4 (0-3)       | 2 targets   | 2 channels    | 4 queues        |
| I226   | 4 (0-3)       | 2 targets   | 2 channels    | 4 queues        |

## Register Reference

### RXPBSIZE (0x2404)
| Bits | Name | Description |
|------|------|-------------|
| 29   | CFG_TS_EN | Enable 16-byte timestamp in packet buffer |
| 5:0  | Rxpbsize | RX packet buffer size in KB |

### SRRCTL[n] (0x0C00C + n*0x40)
| Bits | Name | Description |
|------|------|-------------|
| 30   | TIMESTAMP | Enable per-queue timestamping |
| 27:25 | DESCTYPE | Descriptor type (must be non-zero) |
| 23:20 | BSIZEHDR | Header buffer size |
| 9:0  | BSIZEPACKET | Packet buffer size |

### TSAUXC (0x0B640)
| Bits | Name | Description |
|------|------|-------------|
| 31   | DIS_SYSTIM0 | 1=disable SYSTIM0 (primary), 0=enable |
| 30   | DIS_TS_CLEAR | Disable auto-clear of interrupt flags |
| 29   | DIS_SYSTIM3 | 1=disable SYSTIM3, 0=enable |
| 28   | DIS_SYSTIM2 | 1=disable SYSTIM2, 0=enable |
| 27   | DIS_SYSTIM1 | 1=disable SYSTIM1, 0=enable |
| 17   | AUTT1 | Aux timestamp 1 taken (read-only) |
| 10   | EN_TS1 | Enable aux timestamp 1 on SDP |
| 9    | AUTT0 | Aux timestamp 0 taken (read-only) |
| 8    | EN_TS0 | Enable aux timestamp 0 on SDP |
| 4    | EN_TT1 | Enable target time 1 interrupt |
| 0    | EN_TT0 | Enable target time 0 interrupt |

### TRGTTIML/H (Target Time Registers)
| Register | Address | Description |
|----------|---------|-------------|
| TRGTTIML0 | 0x0B644 | Target time 0 low (32-bit) |
| TRGTTIMH0 | 0x0B648 | Target time 0 high (32-bit) |
| TRGTTIML1 | 0x0B64C | Target time 1 low (32-bit) |
| TRGTTIMH1 | 0x0B650 | Target time 1 high (32-bit) |

### AUXSTMP (Auxiliary Timestamp Registers - Read-Only)
| Register | Address | Description |
|----------|---------|-------------|
| AUXSTMPL0 | 0x0B65C | Aux timestamp 0 low (32-bit) |
| AUXSTMPH0 | 0x0B660 | Aux timestamp 0 high (32-bit) |
| AUXSTMPL1 | 0x0B664 | Aux timestamp 1 low (32-bit) |
| AUXSTMPH1 | 0x0B668 | Aux timestamp 1 high (32-bit) |

## Testing

### Compile Tests
```powershell
# RX timestamping test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /Zi /I include /I external/intel_avb/lib tools/avb_test/rx_timestamping_test.c /Fe:rx_timestamping_test.exe'

# Target time test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /Zi /I include /I external/intel_avb/lib tools/avb_test/target_time_test.c /Fe:target_time_test.exe'
```

### Run Tests
```powershell
.\rx_timestamping_test.exe
.\target_time_test.exe
```

## Intel Datasheet References

- **I210 Datasheet v3.7** (333016):
  - Section 7.3.1: RX Packet Timestamping
  - Section 8.2.3.7.9: RXPBSIZE register
  - Section 8.2.3.10.1: SRRCTL registers
  - Section 8.12.36: TSAUXC register
  - Section 8.12.38-39: Target time registers
  - Section 8.12.40-41: Auxiliary timestamp registers

- **I225 Software Manual v1.3**:
  - Chapter 5: Time Synchronization
  - Section 5.3: Hardware Timestamping
  - Section 5.4: Target Time Events

## Notes

1. **Port Reset Requirement**: Changing RXPBSIZE.CFG_TS_EN requires port software reset (CTRL.RST). This is a hardware limitation.

2. **Descriptor Type**: SRRCTL[n].DESCTYPE must be non-zero for timestamp writeback to work. This is typically configured by the NDIS driver.

3. **Timer Selection**: For basic RX timestamping, use SYSTIM0 (timer_mask=0x01). Advanced applications can use multiple timers on I225/I226.

4. **SDP Pin Events**: Auxiliary timestamps capture the SYSTIM value when an SDP (Software Defined Pin) event occurs. Requires EN_TS0/EN_TS1 in TSAUXC.

5. **Target Time Accuracy**: Target time interrupts fire when SYSTIM crosses the programmed target value. Typical accuracy is 8ns (one SYSTIM increment).

## Security

All IOCTLs are **production-safe** and available in Release builds. Raw register access (`IOCTL_AVB_READ/WRITE_REGISTER`) is restricted to Debug builds only.

See **docs/SECURITY_IOCTL_RESTRICTION.md** for security architecture details.
