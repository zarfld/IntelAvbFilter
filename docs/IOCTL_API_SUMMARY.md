# IOCTL API Summary - January 2025

## Production IOCTLs (Available in Release Builds)

### Clock and Timestamping Control

| IOCTL Code | Name | Purpose | Register Access |
|------------|------|---------|-----------------|
| 38 | `IOCTL_AVB_ADJUST_FREQUENCY` | Adjust PTP clock frequency | TIMINCA (0x0B608) |
| 39 | `IOCTL_AVB_GET_CLOCK_CONFIG` | Query SYSTIM/TIMINCA/TSAUXC state | Multiple reads |
| 40 | `IOCTL_AVB_SET_HW_TIMESTAMPING` | Control TSAUXC (4 timers, target time, aux TS) | TSAUXC (0x0B640) |
| 15 | `IOCTL_AVB_GET_TIMESTAMP` | Read SYSTIM counter | SYSTIML/H |
| 16 | `IOCTL_AVB_SET_TIMESTAMP` | Write SYSTIM counter | SYSTIML/H |

### RX Packet Timestamping (NEW - January 2025)

| IOCTL Code | Name | Purpose | Register Access |
|------------|------|---------|-----------------|
| 41 | `IOCTL_AVB_SET_RX_TIMESTAMP` | Enable 16-byte timestamp buffer | RXPBSIZE (0x2404) bit 29 |
| 42 | `IOCTL_AVB_SET_QUEUE_TIMESTAMP` | Enable per-queue timestamping | SRRCTL[n] bit 30 |
| 43 | `IOCTL_AVB_SET_TARGET_TIME` | Configure target time interrupts | TRGTTIML/H (0x0B644-0x0B650) |
| 44 | `IOCTL_AVB_GET_AUX_TIMESTAMP` | Read auxiliary timestamps | AUXSTMP0/1 (0x0B65C-0x0B668) |

### Device Management

| IOCTL Code | Name | Purpose |
|------------|------|---------|
| 1 | `IOCTL_AVB_GET_DEVICE_INFO` | Query device capabilities (VID/DID, features) |
| 2 | `IOCTL_AVB_INIT_DEVICE` | Initialize AVB hardware |
| 35 | `IOCTL_AVB_OPEN_ADAPTER` | Open adapter by VID/DID (multi-adapter support) |
| 37 | `IOCTL_AVB_GET_HW_STATE` | Query hardware initialization state |

### TSN Features (I225/I226)

| IOCTL Code | Name | Purpose |
|------------|------|---------|
| 18 | `IOCTL_AVB_SETUP_TAS` | Time-Aware Shaper (802.1Qbv) |
| 19 | `IOCTL_AVB_SETUP_FP` | Frame Preemption (802.1Qbu) |
| 20 | `IOCTL_AVB_SETUP_PTM` | PCIe Precision Time Measurement |

### MDIO/PHY Access

| IOCTL Code | Name | Purpose |
|------------|------|---------|
| 32 | `IOCTL_AVB_MDIO_READ` | Read PHY register via MDIO |
| 33 | `IOCTL_AVB_MDIO_WRITE` | Write PHY register via MDIO |

## Debug-Only IOCTLs (Excluded from Release Builds)

| IOCTL Code | Name | Purpose | Availability |
|------------|------|---------|--------------|
| 22 | `IOCTL_AVB_READ_REGISTER` | Raw register read (any offset) | **Debug builds only** (`#ifndef NDEBUG`) |
| 23 | `IOCTL_AVB_WRITE_REGISTER` | Raw register write (any offset) | **Debug builds only** (`#ifndef NDEBUG`) |

**Security Note**: These IOCTLs are guarded with `#ifndef NDEBUG` and completely removed from Release builds to prevent direct hardware access in production.

## Migration from Debug to Production IOCTLs

### Old Pattern (Debug-only) ❌
```c
#define REG_TIMINCA 0x0B608

AVB_REGISTER_REQUEST req;
req.offset = REG_TIMINCA;
req.value = 0x08000000;
DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, ...);  // Only in Debug
```

### New Pattern (Production) ✅
```c
AVB_FREQUENCY_REQUEST freq_req;
freq_req.increment_ns = 8;
freq_req.increment_frac = 0;
DeviceIoControl(h, IOCTL_AVB_ADJUST_FREQUENCY, ...);  // Always available
```

## Complete RX Packet Timestamping Setup

```c
// Step 1: Enable SYSTIM0 timer
AVB_HW_TIMESTAMPING_REQUEST tsReq = {0};
tsReq.enable = 1;
tsReq.timer_mask = 0x01;
DeviceIoControl(h, IOCTL_AVB_SET_HW_TIMESTAMPING, &tsReq, ...);

// Step 2: Enable 16-byte timestamp buffer
AVB_RX_TIMESTAMP_REQUEST rxReq = {0};
rxReq.enable = 1;
DeviceIoControl(h, IOCTL_AVB_SET_RX_TIMESTAMP, &rxReq, ...);

// Step 3: Enable per-queue timestamping
AVB_QUEUE_TIMESTAMP_REQUEST queueReq = {0};
queueReq.queue_index = 0;
queueReq.enable = 1;
DeviceIoControl(h, IOCTL_AVB_SET_QUEUE_TIMESTAMP, &queueReq, ...);
```

## TSAUXC Control Features

The `IOCTL_AVB_SET_HW_TIMESTAMPING` provides comprehensive control of Intel's Time Sync Auxiliary Control register:

### Multiple SYSTIM Timers (I225/I226)
- **SYSTIM0**: Primary timer (bit 31 = 0 to enable)
- **SYSTIM1**: Secondary timer (bit 27 = 0 to enable)
- **SYSTIM2**: Tertiary timer (bit 28 = 0 to enable)
- **SYSTIM3**: Quaternary timer (bit 29 = 0 to enable)

### Target Time Interrupts
- **EN_TT0** (bit 0): Enable target time 0 interrupt
- **EN_TT1** (bit 4): Enable target time 1 interrupt
- Fires when SYSTIM crosses programmed target value

### Auxiliary Timestamps
- **EN_TS0** (bit 8): Enable aux timestamp 0 on SDP pin event
- **EN_TS1** (bit 10): Enable aux timestamp 1 on SDP pin event
- **AUTT0** (bit 9): Aux timestamp 0 flag (read-only)
- **AUTT1** (bit 17): Aux timestamp 1 flag (read-only)

## Test Files

| Test File | Purpose | IOCTLs Tested |
|-----------|---------|---------------|
| `ptp_clock_control_production_test.c` | PTP clock control | 38, 39, 15, 16 |
| `hw_timestamping_control_test.c` | TSAUXC control | 40, 39 |
| `rx_timestamping_test.c` | RX packet timestamping | 41, 42 |
| `target_time_test.c` | Target time & aux timestamps | 43, 44, 39 |

## Compile Tests

```powershell
# PTP clock control test
powershell -File tools/vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /Zi /I include tools/avb_test/ptp_clock_control_production_test.c /Fe:ptp_test.exe'

# RX timestamping test
powershell -File tools/vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /Zi /I include tools/avb_test/rx_timestamping_test.c /Fe:rx_test.exe'

# Target time test
powershell -File tools/vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /Zi /I include tools/avb_test/target_time_test.c /Fe:target_test.exe'
```

## Hardware Compatibility

| Device | Basic PTP | Multiple SYSTIM | Target Time | Aux Timestamp | Queue TS |
|--------|-----------|-----------------|-------------|---------------|----------|
| 82575  | ❌        | ❌              | ❌          | ❌            | ❌       |
| I210   | ✅        | ❌ (1 timer)    | ✅ (2)      | ✅ (2)        | ✅ (4)   |
| I211   | ✅        | ❌ (1 timer)    | ✅ (2)      | ✅ (2)        | ✅ (2)   |
| I225   | ✅        | ✅ (4 timers)   | ✅ (2)      | ✅ (2)        | ✅ (4)   |
| I226   | ✅        | ✅ (4 timers)   | ✅ (2)      | ✅ (2)        | ✅ (4)   |

## Architecture Compliance ✅

All new IOCTLs follow project principles:

- **Hardware-first policy**: Real hardware access, no simulations
- **Security**: Debug-only IOCTLs properly guarded with `#ifndef NDEBUG`
- **Clean abstractions**: User-mode API hides register complexity
- **SSOT compliance**: Register offsets from auto-generated headers
- **Device-specific**: Capability checking prevents unsupported operations

## Documentation

- **docs/SECURITY_IOCTL_RESTRICTION.md** - Security architecture
- **docs/RX_PACKET_TIMESTAMPING.md** - Complete RX timestamping guide
- **include/avb_ioctl.h** - IOCTL definitions and structures
- **Intel I210 Datasheet v3.7** (333016) - Hardware specifications
- **Intel I225 Software Manual v1.3** - TSN features

## Version History

- **January 2025**: Added IOCTLs 41-44 (RX packet timestamping, target time, aux timestamps)
- **January 2025**: Enhanced IOCTL 40 with full TSAUXC support (4 timers, target time, aux TS)
- **January 2025**: Added IOCTLs 38-40 (frequency adjust, clock config, HW timestamping)
- **January 2025**: Added debug-only guards for IOCTLs 22-23

---

**Total Production IOCTLs**: 18  
**Debug-Only IOCTLs**: 2  
**Status**: All implementations compile without errors ✅
