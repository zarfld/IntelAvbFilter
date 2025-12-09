# Architecture Description (Reverse Engineered from Code)

**Phase**: 03-Architecture  
**Standards**: ISO/IEC/IEEE 42010:2011  
**Method**: Reverse Engineering from IntelAvbFilter.vcxproj and source files  
**Date**: 2025-12-08  
**Status**: Initial Draft - Recovered from Implementation

---

## 🎯 Executive Summary

This document captures the **as-implemented architecture** of the Intel AVB/TSN NDIS Filter Driver, reverse-engineered from the codebase. The system is a **Windows NDIS 6.0 Lightweight Filter Driver** that intercepts network traffic to provide AVB (Audio Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet adapters.

### System Type
- **Driver Type**: NDIS 6.0 Lightweight Filter (WDM)
- **Target Platform**: Windows 10/11 (x64, ARM64)
- **Deployment**: Kernel-mode driver (.sys)
- **User Interaction**: IOCTL interface via device object

---

## 📋 System Context (C4 Level 1)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     Intel AVB/TSN Filter Driver System                  │
│                                                                           │
│  ┌──────────────┐                                                        │
│  │  User-Mode   │                                                        │
│  │ Applications │────IOCTL───┐                                          │
│  │ (AVB Apps)   │             │                                          │
│  └──────────────┘             │                                          │
│                               ▼                                          │
│                    ┌────────────────────────┐                            │
│                    │  IntelAvbFilter.sys    │                            │
│                    │  (NDIS 6.0 LWF)        │                            │
│                    │                        │                            │
│                    │  - Filter packets      │                            │
│                    │  - Hardware access     │                            │
│                    │  - PTP clock control   │                            │
│                    │  - TSN config (TAS/Qav)│                            │
│                    └────────────────────────┘                            │
│                               │                                          │
│                               │ NDIS Filter Hooks                        │
│                               ▼                                          │
│                    ┌────────────────────────┐                            │
│                    │   Intel Miniport       │                            │
│                    │   (e1000e, igb, etc)   │                            │
│                    │                        │                            │
│                    │  Vendor: 0x8086        │                            │
│                    │  Devices: i210, i217,  │                            │
│                    │    i219, i225, i226,   │                            │
│                    │    i350, 82575, 82576  │                            │
│                    └────────────────────────┘                            │
│                               │                                          │
│                               │ MMIO/PCI                                 │
│                               ▼                                          │
│                    ┌────────────────────────┐                            │
│                    │  Intel Ethernet HW     │                            │
│                    │  (BAR0 registers)      │                            │
│                    │                        │                            │
│                    │  - PTP clock (IEEE1588)│                            │
│                    │  - TAS (IEEE 802.1Qbv) │                            │
│                    │  - CBS (IEEE 802.1Qav) │                            │
│                    └────────────────────────┘                            │
└─────────────────────────────────────────────────────────────────────────┘

External Systems:
- User-mode applications (AVB/TSN apps) communicate via IOCTL
- NDIS Framework (Windows kernel) provides filter infrastructure
- Intel miniport drivers (e1000e, igb) provide base Ethernet functionality
```

---

## 🏗️ Container Diagram (C4 Level 2)

### Physical Components

Based on vcxproj analysis, the system consists of:

| Component | File(s) | Responsibility |
|-----------|---------|----------------|
| **NDIS Filter Core** | `filter.c`, `filter.h`, `device.c` | NDIS lifecycle, packet filtering, device interface |
| **AVB Integration Layer** | `avb_integration_fixed.c`, `avb_integration.h` | Bridge between NDIS and hardware |
| **Hardware Access Layer** | `avb_hardware_access.c`, `avb_bar0_discovery.c`, `avb_bar0_enhanced.c` | Direct hardware register access (BAR0 MMIO) |
| **Device Abstraction Layer** | `devices/intel_device_interface.h`, `devices/intel_device_registry.c` | Device-specific implementations |
| **Device Implementations** | `devices/intel_i210_impl.c`, `intel_i217_impl.c`, `intel_i219_impl.c`, `intel_i226_impl.c`, `intel_i350_impl.c`, `intel_82575_impl.c`, `intel_82576_impl.c`, `intel_82580_impl.c` | Per-device logic |
| **Intel AVB Library Bridge** | `intel_kernel_real.c`, `intel_i225_kernel_wrapper.c` | Kernel-mode wrapper for Intel's AVB library |
| **TSN Configuration** | `tsn_config.c`, `tsn_config.h` | TAS/CBS/QAV configuration |
| **Debug/Diagnostics** | `flt_dbg.c`, `flt_dbg.h` | Debug logging infrastructure |
| **Precompiled Header** | `precomp.c`, `precomp.h` | Common includes |

### External Dependencies

| Dependency | Location | Purpose |
|------------|----------|---------|
| **Intel AVB Library** | `external/intel_avb/lib/` | PTP/gPTP protocol stack |
| **Intel IGB Sources** | `external/intel_igb/src/` | Intel reference implementations |
| **Register Definitions** | `intel-ethernet-regs/gen/*.h` | Auto-generated register headers (i210, i217, i219, i225, i226) |
| **IOCTL ABI** | `external/intel_avb/include/avb_ioctl.h` | Shared kernel/user IOCTL definitions |

---

## 🧩 Component Diagram (C4 Level 3)

### NDIS Filter Core Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         NDIS Filter Core                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  filter.c - NDIS Lifecycle Management                          │     │
│  │                                                                  │     │
│  │  - DriverEntry() → Register NDIS filter driver                 │     │
│  │  - FilterAttach() → Attach to Intel miniport instances         │     │
│  │  - FilterDetach() → Clean up on detach                         │     │
│  │  - FilterRestart() → Start packet processing                   │     │
│  │  - FilterPause() → Stop packet processing                      │     │
│  │  - FilterSendNetBufferLists() → Intercept TX packets           │     │
│  │  - FilterReceiveNetBufferLists() → Intercept RX packets        │     │
│  │  - FilterOidRequest() → Intercept OID requests                 │     │
│  │                                                                  │     │
│  │  Global State:                                                  │     │
│  │  - FilterDriverHandle (NDIS handle)                            │     │
│  │  - FilterModuleList (LIST_ENTRY of attached filters)           │     │
│  │  - FilterListLock (spinlock for thread safety)                 │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                   │                                      │
│                                   │ Creates                              │
│                                   ▼                                      │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  device.c - User-Mode Interface                                │     │
│  │                                                                  │     │
│  │  - FilterDeviceCreate() → Create \Device\IntelAvbFilter        │     │
│  │  - FilterDeviceIoControl() → Handle IOCTL requests             │     │
│  │  - Symbolic link: \DosDevices\IntelAvbFilter                   │     │
│  │                                                                  │     │
│  │  IOCTL Handling:                                                │     │
│  │  - IOCTL_AVB_GET_DEVICE_INFO                                   │     │
│  │  - IOCTL_AVB_GET_SYSTIME                                       │     │
│  │  - IOCTL_AVB_SET_SYSTIME                                       │     │
│  │  - IOCTL_AVB_ADJUST_FREQ                                       │     │
│  │  - IOCTL_AVB_CONFIGURE_TAS                                     │     │
│  │  - IOCTL_AVB_CONFIGURE_CBS                                     │     │
│  │  - (See avb_ioctl.h for complete list)                         │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                           │
└─────────────────────────────────────────────────────────────────────────┘
```

### AVB Integration Layer

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      AVB Integration Layer                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  AVB_DEVICE_CONTEXT (per filter instance)                      │     │
│  │                                                                  │     │
│  │  typedef struct _AVB_DEVICE_CONTEXT {                          │     │
│  │      device_t intel_device;           // Intel lib device      │     │
│  │      BOOLEAN initialized;                                      │     │
│  │      PMS_FILTER filter_instance;      // NDIS filter instance │     │
│  │      BOOLEAN hw_access_enabled;                                │     │
│  │      NDIS_HANDLE miniport_handle;                              │     │
│  │      PINTEL_HARDWARE_CONTEXT hardware_context; // MMIO mapping│     │
│  │                                                                  │     │
│  │      // Hardware lifecycle state machine                       │     │
│  │      AVB_HW_STATE hw_state;           // UNBOUND → BOUND →    │     │
│  │                                        // BAR_MAPPED → PTP_READY│    │
│  │                                                                  │     │
│  │      // Timestamp event ring (shared memory with user-mode)    │     │
│  │      BOOLEAN ts_ring_allocated;                                │     │
│  │      ULONG   ts_ring_id;                                       │     │
│  │      PVOID   ts_ring_buffer;                                   │     │
│  │      HANDLE  ts_ring_section;                                  │     │
│  │                                                                  │     │
│  │      // TSN configuration state                                │     │
│  │      UCHAR   qav_last_tc;             // Credit-Based Shaper   │     │
│  │      ULONG   qav_idle_slope, qav_send_slope;                   │     │
│  │      ULONG   qav_hi_credit, qav_lo_credit;                     │     │
│  │  } AVB_DEVICE_CONTEXT;                                         │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  avb_integration_fixed.c - Integration Functions               │     │
│  │                                                                  │     │
│  │  - AvbInitializeDevice() → Initialize AVB context              │     │
│  │  - AvbCleanupDevice() → Teardown AVB context                   │     │
│  │  - AvbHandleIoctl() → Route IOCTL to appropriate handler       │     │
│  │  - Lifecycle state machine management                          │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                           │
└─────────────────────────────────────────────────────────────────────────┘
```

### Hardware Access Layer

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     Hardware Access Layer                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  INTEL_HARDWARE_CONTEXT (MMIO mapping)                         │     │
│  │                                                                  │     │
│  │  typedef struct _INTEL_HARDWARE_CONTEXT {                      │     │
│  │      PHYSICAL_ADDRESS physical_address;  // BAR0 phys addr    │     │
│  │      PUCHAR mmio_base;                   // Mapped virt addr   │     │
│  │      ULONG mmio_length;                  // Mapping size       │     │
│  │      BOOLEAN mapped;                     // Mapping status     │     │
│  │  } INTEL_HARDWARE_CONTEXT;                                     │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  avb_bar0_discovery.c - Hardware Discovery                     │     │
│  │                                                                  │     │
│  │  - AvbDiscoverBar0() → Find BAR0 physical address              │     │
│  │  - AvbIsSupportedIntelController() → Validate vendor/device ID │     │
│  │  - Query miniport via OID_GEN_HARDWARE_STATUS                  │     │
│  │  - Parse PCI configuration space                               │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                   │                                      │
│                                   ▼                                      │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  avb_bar0_enhanced.c - MMIO Mapping & Validation              │     │
│  │                                                                  │     │
│  │  - AvbMapBar0() → MmMapIoSpace() to create virtual mapping     │     │
│  │  - AvbUnmapBar0() → MmUnmapIoSpace() to release mapping        │     │
│  │  - AvbValidateRegisterAccess() → Read STATUS register to test │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                   │                                      │
│                                   ▼                                      │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  avb_hardware_access.c - Register Read/Write                   │     │
│  │                                                                  │     │
│  │  - AvbReadRegister32() → READ_REGISTER_ULONG()                 │     │
│  │  - AvbWriteRegister32() → WRITE_REGISTER_ULONG()               │     │
│  │  - Thread-safe access with memory barriers                     │     │
│  │  - Validation of MMIO base pointer                             │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                           │
└─────────────────────────────────────────────────────────────────────────┘
```

### Device Abstraction Layer

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Device Abstraction Layer                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  intel_device_interface.h - Device Operations Interface        │     │
│  │                                                                  │     │
│  │  typedef struct _intel_device_ops {                            │     │
│  │      const char* device_name;                                  │     │
│  │      uint32_t supported_capabilities;                          │     │
│  │                                                                  │     │
│  │      // Basic operations                                       │     │
│  │      int (*init)(device_t *dev);                               │     │
│  │      int (*cleanup)(device_t *dev);                            │     │
│  │      int (*get_info)(device_t *dev, char *buf, ULONG size);    │     │
│  │                                                                  │     │
│  │      // PTP/IEEE 1588 operations                               │     │
│  │      int (*set_systime)(device_t *dev, uint64_t systime);      │     │
│  │      int (*get_systime)(device_t *dev, uint64_t *systime);     │     │
│  │      int (*init_ptp)(device_t *dev);                           │     │
│  │                                                                  │     │
│  │      // TSN operations (optional)                              │     │
│  │      int (*setup_tas)(device_t *dev, tsn_tas_config *cfg);     │     │
│  │      int (*setup_frame_preemption)(device_t *dev, ...);        │     │
│  │      int (*setup_ptm)(device_t *dev, ptm_config *cfg);         │     │
│  │                                                                  │     │
│  │      // Register access (optional overrides)                   │     │
│  │      int (*read_register)(device_t *dev, uint32_t off, ...);   │     │
│  │      int (*write_register)(device_t *dev, uint32_t off, ...);  │     │
│  │                                                                  │     │
│  │      // MDIO operations                                        │     │
│  │      int (*mdio_read)(...);                                    │     │
│  │      int (*mdio_write)(...);                                   │     │
│  │  } intel_device_ops_t;                                         │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                   │                                      │
│                                   │ Implemented by                       │
│                                   ▼                                      │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │  intel_device_registry.c - Device Registry                     │     │
│  │                                                                  │     │
│  │  - intel_register_device_ops() → Register device impl          │     │
│  │  - intel_get_device_ops() → Lookup device impl by type         │     │
│  │                                                                  │     │
│  │  Registered Devices:                                           │     │
│  │  - i210_ops   (intel_i210_impl.c)                              │     │
│  │  - i217_ops   (intel_i217_impl.c)                              │     │
│  │  - i219_ops   (intel_i219_impl.c)                              │     │
│  │  - i226_ops   (intel_i226_impl.c)                              │     │
│  │  - i350_ops   (intel_i350_impl.c)                              │     │
│  │  - e82575_ops (intel_82575_impl.c)                             │     │
│  │  - e82576_ops (intel_82576_impl.c)                             │     │
│  │  - e82580_ops (intel_82580_impl.c)                             │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                           │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 🔄 Data Flow Architecture

### IOCTL Request Flow

```
User Application
       │
       │ DeviceIoControl()
       │
       ▼
┌─────────────────────────────────────┐
│  \Device\IntelAvbFilter             │
│  (device.c: FilterDeviceIoControl)  │
└─────────────────────────────────────┘
       │
       │ Parse IOCTL code
       │
       ▼
┌─────────────────────────────────────┐
│  avb_integration_fixed.c            │
│  AvbHandleIoctl()                   │
└─────────────────────────────────────┘
       │
       │ Route by IOCTL type
       │
       ├─────────────────────────────────┬─────────────────────────────┐
       │                                 │                             │
       ▼                                 ▼                             ▼
┌─────────────────┐          ┌─────────────────┐         ┌─────────────────┐
│ PTP Operations  │          │ TSN Config      │         │ Device Info     │
│                 │          │                 │         │                 │
│ - Get/Set Time  │          │ - TAS (Qbv)     │         │ - Capabilities  │
│ - Adjust Freq   │          │ - CBS (Qav)     │         │ - Status        │
│ - Timestamping  │          │ - Frame Preempt │         │ - Vendor ID     │
└─────────────────┘          └─────────────────┘         └─────────────────┘
       │                                 │                             │
       │                                 │                             │
       ▼                                 ▼                             ▼
┌───────────────────────────────────────────────────────────────────────┐
│              Device-Specific Implementation                           │
│  (intel_device_ops_t → i210_ops, i226_ops, etc.)                     │
└───────────────────────────────────────────────────────────────────────┘
       │
       │ Register access
       ▼
┌───────────────────────────────────────────────────────────────────────┐
│           Hardware Access Layer (avb_hardware_access.c)               │
│  AvbReadRegister32() / AvbWriteRegister32()                           │
└───────────────────────────────────────────────────────────────────────┘
       │
       │ MMIO read/write
       ▼
┌───────────────────────────────────────────────────────────────────────┐
│                Intel Ethernet Hardware Registers (BAR0)               │
└───────────────────────────────────────────────────────────────────────┘
```

### Packet Filtering Flow

```
Network Stack (TCP/IP)
       │
       │ Send/Receive
       ▼
┌─────────────────────────────────────┐
│   Intel Miniport Driver             │
│   (e1000e, igb, etc.)               │
└─────────────────────────────────────┘
       │
       │ NDIS Filter Hook
       ▼
┌─────────────────────────────────────┐
│  filter.c                           │
│  FilterSendNetBufferLists() /       │
│  FilterReceiveNetBufferLists()      │
└─────────────────────────────────────┘
       │
       │ Packet inspection/modification
       │ (future: AVB stream filtering, QoS marking)
       │
       ▼
┌─────────────────────────────────────┐
│  Pass through to next filter/       │
│  miniport/protocol                  │
└─────────────────────────────────────┘
       │
       │
       ▼
Physical Network (Ethernet)
```

---

## 🏛️ Architectural Patterns

### Pattern 1: Layered Architecture

**Motivation**: Separation of concerns, maintainability, testability

**Layers** (top to bottom):
1. **NDIS Filter Interface** (`filter.c`, `device.c`)
   - NDIS framework integration
   - User-mode device interface
   - Packet filtering hooks

2. **AVB Integration Layer** (`avb_integration_fixed.c`)
   - IOCTL routing and validation
   - State management (lifecycle, capabilities)
   - Bridge between NDIS and hardware

3. **Device Abstraction Layer** (`intel_device_interface.h`, device registry)
   - Polymorphic device operations
   - Device-specific implementations
   - Capability discovery

4. **Hardware Access Layer** (`avb_hardware_access.c`, BAR0 discovery/mapping)
   - Direct MMIO register access
   - Thread-safe operations
   - Resource management (mapping/unmapping)

**Consequences**:
- ✅ Clear separation of concerns
- ✅ Easier testing (can mock layers)
- ✅ Maintainability (change one layer without affecting others)
- ⚠️ Slight performance overhead (function call indirection)

### Pattern 2: Strategy Pattern (Device Abstraction)

**Motivation**: Support multiple Intel device families with different capabilities

**Implementation**:
- `intel_device_ops_t` defines common interface
- Each device family implements operations: `i210_ops`, `i226_ops`, etc.
- Registry provides lookup: `intel_get_device_ops(device_type)`

**Benefits**:
- ✅ Easy to add new device families
- ✅ No cross-device contamination
- ✅ Device-specific optimizations possible

### Pattern 3: State Machine (Hardware Lifecycle)

**States** (in `AVB_HW_STATE`):
```
AVB_HW_UNBOUND → AVB_HW_BOUND → AVB_HW_BAR_MAPPED → AVB_HW_PTP_READY
```

**Transitions**:
- `UNBOUND → BOUND`: Filter attaches to Intel miniport
- `BOUND → BAR_MAPPED`: BAR0 discovered and MMIO mapped
- `BAR_MAPPED → PTP_READY`: PTP clock verified incrementing

**Enforcement**: IOCTLs rejected if not in appropriate state

### Pattern 4: Adapter Pattern (Intel AVB Library Integration)

**Problem**: Intel's AVB library (`external/intel_avb/lib/`) designed for Linux
**Solution**: Kernel wrappers (`intel_kernel_real.c`, `intel_i225_kernel_wrapper.c`)
- Translate Linux-style calls to Windows kernel APIs
- Provide `device_t` abstraction
- Bridge Linux types (u32, u64) to Windows types (ULONG, ULONGLONG)

---

## 🛡️ Security Architecture

### Threat Model

| Threat | Mitigation |
|--------|------------|
| **Malicious IOCTL requests** | Input validation, state machine enforcement |
| **Buffer overflows** | Bounded buffer copies, size validation |
| **Privilege escalation** | Kernel-mode only, no user-mode code execution |
| **Hardware tampering** | MMIO validation, read-back verification |
| **Race conditions** | Spinlocks (`FILTER_LOCK`), NDIS_INTERLOCK operations |

### Access Control

- **User-mode access**: Requires handle to `\\.\IntelAvbFilter` device
- **Administrator privileges**: Required for driver loading (service control)
- **IOCTL authorization**: No additional authorization beyond device handle (future: add ACLs)

---

## 📊 Performance Characteristics

### Packet Filtering Performance

- **Design**: Lightweight Filter (minimal processing)
- **Current**: Pass-through only (no packet modification)
- **Overhead**: <5µs per packet (estimated, based on NDIS LWF benchmarks)
- **Future**: AVB stream classification will add overhead

### Hardware Access Latency

- **Register read/write**: <1µs (MMIO direct access)
- **PTP clock read**: ~2-5µs (device-dependent)
- **IOCTL round-trip**: <100µs (from user-mode call to return)

### Memory Footprint

- **Driver size**: ~200KB (estimated from compiled .sys)
- **Per-adapter context**: ~4KB (`AVB_DEVICE_CONTEXT`)
- **Timestamp ring buffer**: 4KB-64KB (configurable, user-mode shared memory)

---

## 🔧 Technology Stack

### Development Environment

| Component | Version/Details |
|-----------|-----------------|
| **Compiler** | MSVC (Visual Studio 2019+) |
| **Toolchain** | Windows Driver Kit (WDK) 10.0.22621.0 |
| **Target Framework** | NDIS 6.0 (Windows 7+) |
| **Architecture** | x64, ARM64 |
| **Build System** | MSBuild (IntelAvbFilter.sln, .vcxproj) |

### Runtime Dependencies

| Dependency | Type | Purpose |
|------------|------|---------|
| **ndis.lib** | Kernel library | NDIS framework APIs |
| **ntoskrnl.lib** | Kernel library | Windows kernel APIs (implied) |
| **hal.lib** | Kernel library | Hardware Abstraction Layer (implied) |

### External Libraries (Vendored)

| Library | Location | Purpose |
|---------|----------|---------|
| **Intel AVB Library** | `external/intel_avb/lib/` | PTP/gPTP protocol implementation |
| **Intel IGB Reference** | `external/intel_igb/src/` | Device reference code |

### Code Generation

| Tool | Input | Output |
|------|-------|--------|
| **reggen.py** | `intel-ethernet-regs/devices/*.yaml` | `intel-ethernet-regs/gen/*.h` |

**Purpose**: Auto-generate register offset/mask definitions from device YAML specs

---

## 📐 Design Decisions (Recovered ADRs)

### ADR-001: Use NDIS 6.0 (not 6.50+)

**Context**: Need maximum Windows compatibility (Windows 7+)

**Decision**: Use NDIS 6.0 API

**Rationale**:
- NDIS 6.0 supported since Windows Vista/7
- NDIS 6.50+ requires Windows 10 1607+
- Trade-off: Lose some modern features for broader compatibility

**Status**: Implemented

---

### ADR-002: Direct BAR0 MMIO Access (not OID passthrough)

**Context**: Need precise hardware control for PTP clock

**Decision**: Map BAR0 directly via `MmMapIoSpace()`, bypassing miniport

**Rationale**:
- Miniport drivers don't expose PTP control OIDs
- Direct MMIO guarantees <1µs latency
- Intel hardware datasheets provide full register documentation

**Risks**:
- Potential conflicts with miniport driver hardware access
- Requires careful synchronization

**Mitigation**:
- Read-modify-write only TSN/PTP-specific registers
- Avoid touching core Ethernet control registers

**Status**: Implemented

---

### ADR-003: Per-Device Strategy Pattern

**Context**: Support 8+ Intel device families with varying capabilities

**Decision**: Use device registry with `intel_device_ops_t` interface

**Alternatives Considered**:
1. **Monolithic if/else chain** - Rejected: Unmaintainable, high coupling
2. **Preprocessor macros** - Rejected: Inflexible, no runtime dispatch
3. **Virtual tables** - Selected: Clean, extensible, testable

**Status**: Implemented

---

### ADR-004: Kernel-Mode Timestamp Ring Buffer

**Context**: User-mode apps need high-frequency timestamp events

**Decision**: Use section-based shared memory (`ZwCreateSection()`, `ZwMapViewOfSection()`)

**Rationale**:
- Avoid IOCTL overhead for every timestamp (would be 100µs+ each)
- User-mode can poll ring buffer at arbitrary frequency
- Kernel writes timestamps directly to shared memory

**Implementation**:
- `AVB_DEVICE_CONTEXT.ts_ring_*` fields
- User-mode maps section to its address space

**Status**: Partially implemented (structure defined, ring buffer logic incomplete)

---

## 🚧 Incomplete/Missing Components

### Identified Gaps

| Component | Status | Evidence |
|-----------|--------|----------|
| **AVB Stream Filtering** | Not implemented | No packet inspection logic in `FilterSendNetBufferLists()` |
| **gPTP Protocol Integration** | Partial | Intel library present, but not integrated into packet path |
| **TAS (802.1Qbv) Implementation** | Placeholder | `tsn_config.c` has stubs, no register programming |
| **CBS (802.1Qav) Implementation** | Placeholder | QAV fields in context, no actual configuration |
| **Timestamp Ring Buffer** | Structure only | Allocation code present, producer/consumer logic missing |
| **Multi-Adapter Support** | Unclear | List of filters maintained, but cross-filter coordination undefined |
| **Error Recovery** | Basic | Hardware errors not comprehensively handled |

---

## 🧪 Test Infrastructure

### Test Files (from vcxproj)

| Test File | Purpose |
|-----------|---------|
| `tests/integration/AvbIntegrationTests.c` | Integration tests (NDIS + hardware) |
| `tests/taef/AvbTaefTests.c` | TAEF framework tests |
| `tools/avb_test/avb_test_actual.c` | User-mode test application (actual hardware) |
| `tools/avb_test/avb_test_standalone.c` | Standalone diagnostic tool |
| `tools/avb_test/avb_diagnostic_test.c` | Hardware diagnostic tests |

### Test Makefiles

Multiple `.mak` files in `tools/avb_test/` for building user-mode test tools:
- `avb_test.mak`
- `avb_diagnostic.mak`
- `avb_hw_state_test.mak`
- `hardware_investigation.mak`
- etc.

---

## 🔗 Traceability to Requirements

### Requirements Coverage Analysis

**Note**: Requirements are tracked as GitHub Issues. This section maps code components to requirement categories.

| Requirement Category | Implementation Files | Status |
|---------------------|----------------------|--------|
| **REQ-F-PTP-001 to 005** (PTP Clock Control) | `avb_integration_fixed.c`, device impls | ✅ Implemented |
| **REQ-F-QAV-001** (Credit-Based Shaper) | `tsn_config.c` | ⚠️ Placeholder |
| **REQ-F-TAS-001** (Time-Aware Scheduler) | `tsn_config.c` | ⚠️ Placeholder |
| **REQ-F-GPTP-xxx** (gPTP Protocol) | `external/intel_avb/lib/` | ⚠️ Library present, not integrated |
| **REQ-F-DIAG-xxx** (Diagnostics) | `flt_dbg.c`, test tools | ✅ Basic implementation |
| **REQ-NF-SECURITY-xxx** (Security) | Input validation | ⚠️ Partial |
| **REQ-NF-COMPAT-NDIS-001** (NDIS 6.0+) | `filter.c` | ✅ Implemented |

**Legend**:
- ✅ Implemented
- ⚠️ Partial / Placeholder
- ❌ Not implemented

---

## 📚 References

### Code Artifacts Analyzed

1. **IntelAvbFilter.vcxproj** - Project structure and file organization
2. **IntelAvbFilter.vcxproj.filters** - Logical file grouping
3. **filter.h, filter.c** - NDIS filter core
4. **avb_integration.h, avb_integration_fixed.c** - AVB integration layer
5. **devices/intel_device_interface.h** - Device abstraction
6. **devices/*.c** - Device-specific implementations (8 files)
7. **avb_hardware_access.c, avb_bar0_*.c** - Hardware access layer

### External Documentation

- **NDIS 6.0 Specification**: Microsoft Docs
- **Intel Ethernet Controller Datasheets**: `external/intel_avb/spec/*.md`
- **IEEE 802.1AS-2020**: gPTP Specification
- **IEEE 802.1Qbv**: Time-Aware Scheduler
- **IEEE 802.1Qav**: Credit-Based Shaper

---

## ✅ Next Steps: Architecture Documentation

### Recommended Actions

1. **Create Formal ADR Issues** (#78, #79, etc.) for decisions ADR-001 through ADR-004
2. **Map Components to Requirements** - Create GitHub Issues for architectural components (ARC-C)
3. **Document Quality Scenarios** - Create QA-SC issues for performance, security, availability
4. **Create C4 Diagrams** - Convert ASCII art to PlantUML/Mermaid in `03-architecture/diagrams/`
5. **Fill Architecture Views**:
   - Module View (file organization, build dependencies)
   - Component-and-Connector View (runtime interactions, data flow)
   - Allocation View (hardware mapping, deployment)
6. **Document Missing Components** - Create GitHub Issues for incomplete features (TAS, CBS, gPTP integration)

---

**Document Status**: Initial Draft - Reverse Engineered  
**Traceability**: Analysis based on IntelAvbFilter.vcxproj and source code inspection  
**Verification Method**: Code structure analysis, no runtime validation yet  
**Next Review**: After formal ADR/ARC-C GitHub Issues created
