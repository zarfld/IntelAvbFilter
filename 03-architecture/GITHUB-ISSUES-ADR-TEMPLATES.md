# Architecture Decision Records (ADR) - GitHub Issue Templates

**Purpose**: Create these as GitHub Issues with labels `type:architecture:decision`, `phase:03-architecture`

---

## ADR-001: Use NDIS 6.0 for Maximum Windows Compatibility

**Title**: ADR-ARCH-001: Use NDIS 6.0 (not NDIS 6.50+) for Maximum Windows Compatibility

**Labels**: `type:architecture:decision`, `phase:03-architecture`, `priority:p0`, `compatibility`

---

### Description
Decision to use NDIS 6.0 API instead of newer NDIS 6.50+ for driver implementation to ensure maximum Windows version compatibility.

### Context
- **Requirement**: #31 (StR: NDIS Filter Driver for AVB/TSN)
- **Constraint**: Need to support Windows 7, 8, 10, and 11
- **Driver Type**: Lightweight Filter (LWF) for packet interception
- **User Base**: Enterprise environments with mixed Windows versions

**Problem**: Choose NDIS API version that balances modern features with broad Windows compatibility.

### Decision
**Use NDIS 6.0 API** for filter driver implementation.

**Implementation**:
```c
#define FILTER_MAJOR_NDIS_VERSION   6
#define FILTER_MINOR_NDIS_VERSION   0
```

**Location**: `filter.h`, `filter.c` (DriverEntry)

### Rationale

**Options Considered**:

1. **Option A: NDIS 6.0 (Windows Vista+)**
   - **Pros**: 
     - Supported on Windows 7, 8, 10, 11 (broad compatibility)
     - Stable, well-documented API
     - Sufficient features for lightweight filtering
     - Large driver ecosystem uses NDIS 6.0
   - **Cons**: 
     - Missing modern features (RSS enhancements, VMQ improvements)
     - Older API conventions
   - **Windows Support**: Vista SP1, 7, 8, 8.1, 10, 11

2. **Option B: NDIS 6.30 (Windows 8+)**
   - **Pros**: 
     - Better power management
     - Improved RSS and VMQ
     - Single-Root I/O Virtualization (SR-IOV)
   - **Cons**: 
     - Drops Windows 7 support (enterprise blocker)
     - Minimal benefit for lightweight filter
   - **Windows Support**: 8, 8.1, 10, 11

3. **Option C: NDIS 6.50+ (Windows 10 1607+) - REJECTED**
   - **Pros**: 
     - Modern API design
     - Better diagnostics and telemetry
     - Latest NDIS features
   - **Cons**: 
     - Requires Windows 10 1607+ (unacceptable for enterprise)
     - No Windows 7/8 support
     - Overkill for lightweight filtering
   - **Windows Support**: 10 (1607+), 11

**Why Option A (NDIS 6.0)?**:
- ✅ Satisfies enterprise requirement for Windows 7 compatibility
- ✅ Lightweight filter doesn't need NDIS 6.50+ features
- ✅ Proven stability and broad driver ecosystem
- ✅ Simplifies testing matrix (fewer OS variations)

### Consequences

#### Positive
- ✅ **Broadest compatibility**: Windows 7 through Windows 11
- ✅ **Enterprise friendly**: No forced OS upgrades
- ✅ **Stable API**: Well-tested, fewer surprises
- ✅ **Large knowledge base**: Extensive documentation and examples
- ✅ **Lower risk**: Mature API with known limitations

#### Negative
- ❌ **Missing modern features**: No NDIS 6.50+ enhancements
- ❌ **Older conventions**: More verbose API patterns
- ❌ **Future limitations**: May need upgrade for advanced features

#### Risks and Mitigations
- **Risk**: Windows 7 EOL (2020) makes this unnecessary
  - **Mitigation**: Enterprise extended support until 2023, some customers still on Win7
- **Risk**: Future AVB features require NDIS 6.50+
  - **Mitigation**: Current lightweight filter needs are met; can upgrade in v2.0 if needed

### Quality Attributes Addressed

| Quality Attribute | Impact | Measure |
|-------------------|--------|---------|
| **Compatibility** | +High | Windows 7, 8, 10, 11 support |
| **Reliability** | +High | Stable API, well-tested |
| **Maintainability** | Neutral | Familiar API patterns |
| **Feature Richness** | -Low | Missing NDIS 6.50+ features |

### Implementation Notes
- **Header Settings**:
  ```c
  #define NDIS_SUPPORT_NDIS60 1
  #define FILTER_MAJOR_NDIS_VERSION 6
  #define FILTER_MINOR_NDIS_VERSION 0
  ```
- **Characteristics Structure**:
  ```c
  FChars.MajorNdisVersion = FILTER_MAJOR_NDIS_VERSION;
  FChars.MinorNdisVersion = FILTER_MINOR_NDIS_VERSION;
  ```
- **Registration**: Uses `NdisFRegisterFilterDriver()` with NDIS 6.0 callbacks

### Validation Criteria
- [x] Driver loads successfully on Windows 7 SP1
- [x] Driver loads successfully on Windows 10 22H2
- [x] Driver loads successfully on Windows 11 23H2
- [ ] HLK (Hardware Lab Kit) certification for NDIS 6.0 filter
- [x] Filter attaches to Intel miniports across OS versions

### Traceability
- **Addresses**: #31 (StR: NDIS Filter Driver)
- **Addresses**: #88 (REQ-NF-COMPAT-NDIS-001: NDIS 6.0+ Compatibility)
- **Relates to**: #ADR-002 (Direct BAR0 Access)
- **Components Affected**: #ARC-C-001 (NDIS Filter Core)
- **Verified by**: Manual testing on Windows 7, 10, 11

---

**Status**: Accepted (Implemented)  
**Date**: 2024 (Inferred from code)  
**Deciders**: Development Team  
**Code Evidence**: `filter.h` lines 24-26, `filter.c` DriverEntry

---

## ADR-002: Direct BAR0 MMIO Access (Bypass Miniport)

**Title**: ADR-HW-001: Use Direct BAR0 MMIO Access (Not OID Passthrough) for PTP Control

**Labels**: `type:architecture:decision`, `phase:03-architecture`, `priority:p0`, `hardware`, `performance`

---

### Description
Decision to access Intel Ethernet hardware registers directly via BAR0 MMIO mapping, bypassing the NDIS miniport driver, for precise PTP clock control.

### Context
- **Requirement**: #2, #3, #5, #6, #7 (REQ-F-PTP-001 through 005: Hardware PTP Clock Control)
- **Performance Target**: <1µs register access latency
- **Precision Requirement**: Sub-microsecond PTP timestamp accuracy
- **Problem**: NDIS miniport drivers don't expose PTP control OIDs

**Hardware Context**:
- Intel i210/i217/i219/i225/i226 Ethernet controllers
- IEEE 1588 PTP hardware timestamp units
- BAR0 contains memory-mapped register space
- Direct register access required for:
  - PTP clock set/get (`SYSTIML`, `SYSTIMH`)
  - Frequency adjustment (`TIMINCA`, `TSAUXC`)
  - Hardware timestamping (`RXSTMPL`, `RXSTMPH`, `TXSTMPL`, `TXSTMPH`)

### Decision
**Map BAR0 directly via `MmMapIoSpace()` and perform MMIO register access, bypassing miniport driver.**

**Implementation**:
```c
// Hardware context per filter instance
typedef struct _INTEL_HARDWARE_CONTEXT {
    PHYSICAL_ADDRESS physical_address;  // BAR0 physical address
    PUCHAR mmio_base;                  // Mapped virtual address
    ULONG mmio_length;                 // Mapping size
    BOOLEAN mapped;                    // Mapping status
} INTEL_HARDWARE_CONTEXT;

// Discovery: avb_bar0_discovery.c
NTSTATUS AvbDiscoverBar0(PMS_FILTER Filter, PPHYSICAL_ADDRESS PhysAddr, PULONG Length);

// Mapping: avb_bar0_enhanced.c
NTSTATUS AvbMapBar0(PHYSICAL_ADDRESS PhysAddr, ULONG Length, PUCHAR *MmioBase);

// Access: avb_hardware_access.c
ULONG AvbReadRegister32(PUCHAR MmioBase, ULONG Offset);
VOID AvbWriteRegister32(PUCHAR MmioBase, ULONG Offset, ULONG Value);
```

**Location**: `avb_hardware_access.c`, `avb_bar0_discovery.c`, `avb_bar0_enhanced.c`

### Rationale

**Options Considered**:

1. **Option A: OID Passthrough via Miniport - REJECTED**
   - **Pros**: 
     - Clean abstraction (miniport owns hardware)
     - No direct hardware access risks
     - Miniport handles hardware initialization
   - **Cons**: 
     - ❌ Intel miniports don't expose PTP OIDs (deal breaker)
     - ❌ High latency (>100µs for OID round-trip)
     - ❌ No standard PTP OIDs in NDIS
   - **Verdict**: Infeasible - no OID support

2. **Option B: WMI/WDF Integration - REJECTED**
   - **Pros**: 
     - User-mode friendly interface
     - Windows-standard management
   - **Cons**: 
     - ❌ Still requires miniport cooperation
     - ❌ Even higher latency (>500µs)
     - ❌ Complexity of WMI provider
   - **Verdict**: Too slow, still blocked by miniport

3. **Option C: Direct BAR0 MMIO Mapping - SELECTED**
   - **Pros**: 
     - ✅ <1µs register access latency
     - ✅ Complete hardware control
     - ✅ Intel datasheets provide full register documentation
     - ✅ No miniport dependency for PTP
     - ✅ Proven in Linux drivers (igb, e1000e)
   - **Cons**: 
     - ⚠️ Risk of conflict with miniport hardware access
     - ⚠️ Requires careful register selection (PTP only)
     - ⚠️ Must handle hardware state machine
   - **Verdict**: Only viable option for <1µs latency

**Why Option C?**:
- ✅ **Only option that works**: Miniports don't expose PTP OIDs
- ✅ **Meets latency requirement**: <1µs vs >100µs via OID
- ✅ **Proven approach**: Linux AVB drivers use direct MMIO
- ✅ **Intel documentation**: Full register specs available

### Consequences

#### Positive
- ✅ **Sub-microsecond latency**: Direct MMIO read/write ~500ns
- ✅ **Precise control**: Exact register bit manipulation
- ✅ **No miniport dependency**: Works with any Intel miniport
- ✅ **Hardware timestamp accuracy**: Sub-microsecond capture
- ✅ **Deterministic timing**: No OID queue delays

#### Negative
- ❌ **Miniport conflict risk**: Must avoid core Ethernet registers
- ❌ **Hardware complexity**: Direct state machine management
- ❌ **Limited portability**: Intel-specific register knowledge
- ❌ **No abstraction**: Filter owns hardware initialization

#### Risks and Mitigations

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| **Conflict with miniport register access** | High (crash/hang) | Low | Only touch PTP registers (`0x0B600-0x0B700` range); avoid core Ethernet control |
| **Hardware state corruption** | High (device reset) | Low | Read-modify-write only; validate register values before write |
| **BAR0 mapping failure** | High (no functionality) | Low | Graceful fallback; clear error reporting |
| **Race conditions on shared registers** | Medium (data corruption) | Medium | Use NDIS_INTERLOCK for critical sections; minimize shared register access |

**Mitigation Strategy**:
```c
// Safe register access pattern
ULONG AvbReadRegister32Safe(PINTEL_HARDWARE_CONTEXT HwCtx, ULONG Offset) {
    if (!HwCtx || !HwCtx->mapped || !HwCtx->mmio_base) return 0;
    if (Offset >= HwCtx->mmio_length) return 0; // Bounds check
    return READ_REGISTER_ULONG((PULONG)(HwCtx->mmio_base + Offset));
}

// Only touch PTP registers (0x0B600-0x0B700)
#define PTP_REGISTER_BASE 0x0B600
#define PTP_REGISTER_END  0x0B700
```

### Quality Attributes Addressed

| Quality Attribute | Impact | Measure |
|-------------------|--------|---------|
| **Performance** | +Very High | <1µs register access (OID would be >100µs) |
| **Precision** | +Very High | Sub-microsecond PTP timestamps |
| **Reliability** | -Low | Potential hardware conflicts (mitigated) |
| **Portability** | -Medium | Intel-specific (acceptable for Intel-only driver) |
| **Maintainability** | -Low | Hardware state management complexity |

### Implementation Notes

#### Hardware Lifecycle State Machine
```c
typedef enum _AVB_HW_STATE {
    AVB_HW_UNBOUND = 0,      // Filter not attached
    AVB_HW_BOUND,            // Attached to Intel miniport
    AVB_HW_BAR_MAPPED,       // BAR0 mapped, registers accessible
    AVB_HW_PTP_READY         // PTP clock verified incrementing
} AVB_HW_STATE;
```

#### Discovery Process
1. **Filter Attach** → Query miniport for vendor ID (0x8086) and device ID
2. **Query PCI Config** → Use `NdisMGetBusData()` to read BAR0 base address
3. **Map BAR0** → `MmMapIoSpace()` with `MmNonCached` attribute
4. **Validate Access** → Read `STATUS` register (`0x00008`) to verify mapping
5. **Verify PTP** → Read `SYSTIML/SYSTIMH` twice, confirm clock increments
6. **Transition to PTP_READY** → Enable hardware timestamping

#### Register Safety Rules
- ✅ **Safe**: PTP registers (`0x0B600-0x0B700`)
  - `SYSTIML/SYSTIMH` - System time
  - `TIMINCA` - Frequency adjustment
  - `TSAUXC` - Auxiliary timestamp control
  - `RXSTMPL/RXSTMPH` - RX timestamps
  - `TXSTMPL/TXSTMPH` - TX timestamps
- ❌ **Unsafe**: Core Ethernet registers
  - `CTRL` (`0x00000`) - Device control (miniport owns)
  - `STATUS` (`0x00008`) - Read-only OK, write forbidden
  - `RCTL/TCTL` - RX/TX control (miniport owns)
  - Interrupt registers (miniport owns)

### Validation Criteria
- [x] BAR0 discovery succeeds on all supported Intel devices
- [x] MMIO mapping succeeds (physical → virtual)
- [x] Register read/write validated (read-back test)
- [x] PTP clock increments (verification test)
- [ ] No conflicts with miniport observed (stress test)
- [ ] Hardware error recovery validated

### Traceability
- **Addresses**: #2 (REQ-F-PTP-001: Hardware Clock Get/Set)
- **Addresses**: #3 (REQ-F-PTP-002: Frequency Adjustment)
- **Addresses**: #5 (REQ-F-PTP-003: Hardware Timestamping Control)
- **Addresses**: #6 (REQ-F-PTP-004: Rx Packet Timestamping)
- **Addresses**: #7 (REQ-F-PTP-005: Target Time & Auxiliary Timestamp)
- **Addresses**: #71 (REQ-NF-PERF: Low-latency hardware access)
- **Relates to**: #ADR-001 (NDIS 6.0 Choice)
- **Components Affected**: 
  - #ARC-C-002 (Hardware Access Layer)
  - #ARC-C-003 (AVB Integration Layer)
- **Verified by**: 
  - `tools/avb_test/avb_test_actual.c` (hardware validation)
  - `avb_test_i210.c` (i210 PTP tests)

---

**Status**: Accepted (Implemented)  
**Date**: 2024 (Inferred from code)  
**Deciders**: Development Team  
**Code Evidence**: 
- `avb_bar0_discovery.c` (discovery logic)
- `avb_bar0_enhanced.c` (mapping logic)
- `avb_hardware_access.c` (register access)
- `avb_integration.h` (INTEL_HARDWARE_CONTEXT, AVB_HW_STATE)

---

## ADR-003: Device Abstraction via Strategy Pattern

**Title**: ADR-DESIGN-001: Use Strategy Pattern for Intel Device Family Abstraction

**Labels**: `type:architecture:decision`, `phase:03-architecture`, `priority:p1`, `design-pattern`, `extensibility`

---

### Description
Decision to use Strategy Pattern (polymorphic device operations) to support multiple Intel Ethernet device families (i210, i217, i219, i225, i226, i350, 82575, 82576, 82580) with clean separation and extensibility.

### Context
- **Requirement**: #1 (StR: Hardware Abstraction for Multiple Intel Devices)
- **Device Families**: 8+ Intel Ethernet controllers
- **Variation**: Different register layouts, capabilities, errata
- **Problem**: Avoid if/else spaghetti while maintaining device-specific optimizations

**Device Capabilities Variation**:
| Device | PTP Support | TAS (Qbv) | CBS (Qav) | Frame Preemption |
|--------|-------------|-----------|-----------|------------------|
| i210   | ✅ Full     | ❌ No     | ✅ Yes    | ❌ No            |
| i225   | ✅ Full     | ✅ Yes    | ✅ Yes    | ✅ Yes           |
| i226   | ✅ Full     | ✅ Yes    | ✅ Yes    | ✅ Yes           |
| i219   | ⚠️ Limited  | ❌ No     | ❌ No     | ❌ No            |
| i350   | ⚠️ Basic    | ❌ No     | ✅ Yes    | ❌ No            |

### Decision
**Use Strategy Pattern with device-specific operation tables (`intel_device_ops_t`) and runtime dispatch via device registry.**

**Implementation**:
```c
// Device operations interface (devices/intel_device_interface.h)
typedef struct _intel_device_ops {
    const char* device_name;
    uint32_t supported_capabilities;
    
    // Basic operations
    int (*init)(device_t *dev);
    int (*cleanup)(device_t *dev);
    int (*get_info)(device_t *dev, char *buffer, ULONG size);
    
    // PTP/IEEE 1588 operations
    int (*set_systime)(device_t *dev, uint64_t systime);
    int (*get_systime)(device_t *dev, uint64_t *systime);
    int (*init_ptp)(device_t *dev);
    
    // TSN operations (optional - can be NULL)
    int (*setup_tas)(device_t *dev, struct tsn_tas_config *config);
    int (*setup_frame_preemption)(device_t *dev, struct tsn_fp_config *config);
    
    // Register access (optional overrides)
    int (*read_register)(device_t *dev, uint32_t offset, uint32_t *value);
    int (*write_register)(device_t *dev, uint32_t offset, uint32_t value);
} intel_device_ops_t;

// Device registry (devices/intel_device_registry.c)
const intel_device_ops_t* intel_get_device_ops(intel_device_type_t device_type);
int intel_register_device_ops(intel_device_type_t device_type, const intel_device_ops_t *ops);

// Device implementations
extern const intel_device_ops_t i210_ops;  // devices/intel_i210_impl.c
extern const intel_device_ops_t i226_ops;  // devices/intel_i226_impl.c
// ... (8 device implementations)
```

**Usage**:
```c
// Lookup device operations at runtime
const intel_device_ops_t *ops = intel_get_device_ops(dev->device_type);
if (ops && ops->set_systime) {
    result = ops->set_systime(dev, new_time);
}
```

**Location**: `devices/intel_device_interface.h`, `devices/intel_device_registry.c`, `devices/intel_*_impl.c`

### Rationale

**Options Considered**:

1. **Option A: Monolithic if/else Chain - REJECTED**
   ```c
   if (device_id == I210_DEVICE_ID) {
       // i210-specific code
   } else if (device_id == I226_DEVICE_ID) {
       // i226-specific code
   } else if ...
   ```
   - **Pros**: Simple, no abstraction overhead
   - **Cons**: 
     - ❌ Unmaintainable (100+ if/else blocks)
     - ❌ High coupling (every change touches main code)
     - ❌ Difficult to test in isolation
     - ❌ Code duplication across branches
   - **Verdict**: Unscalable

2. **Option B: Preprocessor Macros - REJECTED**
   ```c
   #ifdef I210_SUPPORT
       i210_set_systime(dev, time);
   #elif defined(I226_SUPPORT)
       i226_set_systime(dev, time);
   #endif
   ```
   - **Pros**: Compile-time optimization
   - **Cons**: 
     - ❌ No runtime dispatch (can't support multiple devices)
     - ❌ Inflexible (requires recompilation per device)
     - ❌ Combinatorial explosion (2^8 = 256 configs)
   - **Verdict**: Too rigid

3. **Option C: Strategy Pattern (Virtual Tables) - SELECTED**
   ```c
   const intel_device_ops_t *ops = intel_get_device_ops(dev_type);
   ops->set_systime(dev, time);
   ```
   - **Pros**: 
     - ✅ Clean separation of concerns
     - ✅ Easy to add new devices (1 new file)
     - ✅ Testable in isolation (mock ops table)
     - ✅ Runtime dispatch (multi-device support)
     - ✅ Optional operations (NULL for unsupported)
   - **Cons**: 
     - ⚠️ Function pointer indirection (~1ns overhead)
     - ⚠️ Slightly more complex initialization
   - **Verdict**: Best balance

**Why Option C?**:
- ✅ **Extensibility**: Add i227/i228 by creating one new .c file
- ✅ **Maintainability**: Device logic isolated, no cross-contamination
- ✅ **Testability**: Mock ops table for unit tests
- ✅ **Capability discovery**: Check `ops->setup_tas != NULL` for TAS support

### Consequences

#### Positive
- ✅ **Easy to add devices**: 1 file per device family
- ✅ **No cross-contamination**: i210 code never touches i226 code
- ✅ **Optional features**: NULL function pointers for unsupported operations
- ✅ **Testability**: Mock device ops in unit tests
- ✅ **Runtime dispatch**: Single driver binary supports all devices
- ✅ **Clear boundaries**: Device-specific logic isolated

#### Negative
- ❌ **Function pointer overhead**: ~1ns per call (negligible)
- ❌ **Initialization complexity**: Must register all devices at startup
- ❌ **Indirect debugging**: Function pointers harder to trace in debugger

#### Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| **NULL pointer dereference** | Always check `ops && ops->func_ptr != NULL` before call |
| **Registration forgotten** | Static initialization in `DriverEntry`, assert on lookup failure |
| **ABI mismatch** | Version field in `intel_device_ops_t` structure |

### Quality Attributes Addressed

| Quality Attribute | Impact | Measure |
|-------------------|--------|---------|
| **Maintainability** | +Very High | Add device = 1 new file, ~500 LOC |
| **Testability** | +High | Mock ops table for isolated testing |
| **Extensibility** | +Very High | Future devices require no core changes |
| **Performance** | -Negligible | Function pointer overhead <1ns |
| **Code Quality** | +High | Separation of concerns, DRY |

### Implementation Notes

#### Device Registration (DriverEntry)
```c
// drivers/device_registry.c
void AvbRegisterAllDevices(void) {
    intel_register_device_ops(INTEL_I210, &i210_ops);
    intel_register_device_ops(INTEL_I217, &i217_ops);
    intel_register_device_ops(INTEL_I219, &i219_ops);
    intel_register_device_ops(INTEL_I225, &i225_ops);
    intel_register_device_ops(INTEL_I226, &i226_ops);
    intel_register_device_ops(INTEL_I350, &i350_ops);
    intel_register_device_ops(INTEL_82575, &e82575_ops);
    intel_register_device_ops(INTEL_82576, &e82576_ops);
    intel_register_device_ops(INTEL_82580, &e82580_ops);
}
```

#### Device Operations Example (i210)
```c
// devices/intel_i210_impl.c
const intel_device_ops_t i210_ops = {
    .device_name = "Intel i210",
    .supported_capabilities = CAP_PTP | CAP_CBS,  // No TAS
    .init = i210_init,
    .cleanup = i210_cleanup,
    .get_info = i210_get_info,
    .set_systime = i210_set_systime,
    .get_systime = i210_get_systime,
    .init_ptp = i210_init_ptp,
    .setup_tas = NULL,  // i210 doesn't support TAS
    .setup_frame_preemption = NULL,  // i210 doesn't support FP
};
```

#### Capability Checking
```c
// Check if device supports TAS before calling
const intel_device_ops_t *ops = intel_get_device_ops(dev->device_type);
if (ops && ops->setup_tas) {
    result = ops->setup_tas(dev, &tas_config);
} else {
    return STATUS_NOT_SUPPORTED;
}
```

### Validation Criteria
- [x] All 8 device implementations registered
- [x] Device lookup succeeds for known device IDs
- [x] NULL operations handled gracefully
- [ ] Unit tests for each device implementation
- [ ] Mock device ops in integration tests

### Traceability
- **Addresses**: #1 (StR: Hardware Abstraction)
- **Addresses**: #10, #11, #12, #13 (REQ-F-DIAG: Device-specific diagnostics)
- **Relates to**: #ADR-002 (Direct BAR0 Access)
- **Components Affected**: 
  - #ARC-C-003 (Device Abstraction Layer)
  - All device implementations (8 components)
- **Verified by**: Device-specific test tools (`avb_test_i210`, `avb_test_i226`)

---

**Status**: Accepted (Implemented)  
**Date**: 2024 (Inferred from code)  
**Deciders**: Development Team  
**Code Evidence**: 
- `devices/intel_device_interface.h` (interface definition)
- `devices/intel_device_registry.c` (registry implementation)
- `devices/intel_i210_impl.c`, `devices/intel_i226_impl.c`, etc. (8 implementations)

---

## ADR-004: Kernel-Mode Shared Memory Timestamp Ring Buffer

**Title**: ADR-PERF-001: Use Section-Based Shared Memory for High-Frequency Timestamp Events

**Labels**: `type:architecture:decision`, `phase:03-architecture`, `priority:p2`, `performance`, `ipc`

---

### Description
Decision to use kernel-mode section-based shared memory (ring buffer) for delivering high-frequency PTP timestamp events to user-mode applications, avoiding IOCTL overhead.

### Context
- **Requirement**: User-mode AVB applications need hardware timestamps for every packet
- **Frequency**: 1000-10000 timestamps/second (high-frequency)
- **Performance**: <10µs timestamp delivery latency
- **Problem**: IOCTL round-trip overhead (~100µs) unacceptable for high-frequency events

**Alternatives**:
- **IOCTL per timestamp**: 100µs overhead → max 10K timestamps/sec
- **Shared memory**: <1µs overhead → >1M timestamps/sec

### Decision
**Use Windows section objects (`ZwCreateSection()`) to create kernel-mode shared memory ring buffer mapped into user-mode address space.**

**Implementation**:
```c
// AVB_DEVICE_CONTEXT (avb_integration.h)
typedef struct _AVB_DEVICE_CONTEXT {
    // ... other fields ...
    
    // Timestamp event ring (section-based mapping)
    BOOLEAN ts_ring_allocated;
    ULONG   ts_ring_id;
    PVOID   ts_ring_buffer;     // Kernel-space view base address
    ULONG   ts_ring_length;     // Buffer size (bytes)
    PMDL    ts_ring_mdl;        // MDL for mapping
    ULONGLONG ts_user_cookie;   // User-provided cookie (echoed back)
    HANDLE  ts_ring_section;    // Section handle (returned to user-mode)
    SIZE_T  ts_ring_view_size;  // Mapped view size
} AVB_DEVICE_CONTEXT;

// IOCTL for ring buffer allocation
#define IOCTL_AVB_ALLOC_TS_RING CTL_CODE(FILE_DEVICE_NETWORK, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Ring buffer structure
typedef struct _AVB_TS_RING_HEADER {
    ULONG write_index;   // Kernel writes here (incremented atomically)
    ULONG read_index;    // User-mode reads here
    ULONG size;          // Total entries
    ULONG entry_size;    // sizeof(AVB_TS_ENTRY)
} AVB_TS_RING_HEADER;

typedef struct _AVB_TS_ENTRY {
    ULONGLONG timestamp;   // PTP timestamp (nanoseconds)
    ULONG packet_id;       // Packet identifier
    UCHAR packet_type;     // TX/RX
    UCHAR reserved[3];
} AVB_TS_ENTRY;
```

**Location**: `avb_integration.h` (structure), `avb_integration_fixed.c` (IOCTL handler - to be implemented)

### Rationale

**Options Considered**:

1. **Option A: IOCTL per Timestamp - REJECTED**
   ```c
   // User-mode polling loop
   while (running) {
       DeviceIoControl(hDevice, IOCTL_AVB_GET_TIMESTAMP, ...);
       // Process timestamp
   }
   ```
   - **Pros**: Simple, synchronous
   - **Cons**: 
     - ❌ 100µs overhead per timestamp
     - ❌ Max 10K timestamps/sec (insufficient)
     - ❌ High CPU usage (busy polling)
   - **Verdict**: Too slow

2. **Option B: Event-Driven Callback - REJECTED**
   - **Pros**: Asynchronous notification
   - **Cons**: 
     - ❌ Still requires IOCTL to retrieve timestamp
     - ❌ Complexity of user-mode callback registration
     - ❌ Kernel→user-mode transitions expensive
   - **Verdict**: Not faster than IOCTL

3. **Option C: Shared Memory Ring Buffer - SELECTED**
   ```c
   // Kernel writes (in hardware interrupt context)
   ULONG idx = InterlockedIncrement(&ring->write_index) % ring->size;
   ring->entries[idx].timestamp = hw_timestamp;
   
   // User-mode reads (polling or blocking)
   while (ring->read_index != ring->write_index) {
       AVB_TS_ENTRY *entry = &ring->entries[ring->read_index % ring->size];
       process_timestamp(entry);
       ring->read_index++;
   }
   ```
   - **Pros**: 
     - ✅ <1µs timestamp delivery (memory write)
     - ✅ Zero-copy (no IOCTL overhead)
     - ✅ Supports >1M timestamps/sec
     - ✅ Lock-free (atomic producer, single consumer)
   - **Cons**: 
     - ⚠️ Complexity of ring buffer management
     - ⚠️ Potential overrun if user-mode slow
   - **Verdict**: Only option meeting performance requirement

**Why Option C?**:
- ✅ **Performance**: <1µs vs 100µs (100x faster)
- ✅ **Scalability**: Handles burst traffic (1000+ timestamps/sec)
- ✅ **Zero-copy**: Direct memory access, no IOCTL syscall

### Consequences

#### Positive
- ✅ **Low latency**: <1µs timestamp delivery
- ✅ **High throughput**: >1M timestamps/sec capacity
- ✅ **Zero-copy**: No IOCTL overhead
- ✅ **Burst tolerance**: Ring buffer absorbs bursts

#### Negative
- ❌ **Complexity**: Ring buffer management (producer/consumer)
- ❌ **Overrun risk**: If user-mode too slow, old timestamps lost
- ❌ **Memory overhead**: Fixed buffer size (4KB-64KB per adapter)

#### Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| **Ring buffer overrun** | Size buffer for 2x expected burst (default 4KB = 512 entries) |
| **User-mode crash leaves section mapped** | Clean up on handle close |
| **Memory corruption** | User-mode read-only mapping; kernel validates indices |

### Quality Attributes Addressed

| Quality Attribute | Impact | Measure |
|-------------------|--------|---------|
| **Performance** | +Very High | <1µs timestamp delivery (vs 100µs IOCTL) |
| **Throughput** | +Very High | >1M timestamps/sec (vs 10K via IOCTL) |
| **Scalability** | +High | Handles burst traffic without drops |
| **Complexity** | -Medium | Ring buffer synchronization |
| **Memory** | -Low | 4KB-64KB per adapter |

### Implementation Notes

#### Ring Buffer Allocation (IOCTL Handler)
```c
NTSTATUS AvbAllocateTimestampRing(
    PAVB_DEVICE_CONTEXT AvbContext,
    ULONG RequestedSize,
    PHANDLE SectionHandle
) {
    // 1. Create section object
    LARGE_INTEGER size;
    size.QuadPart = RequestedSize;
    ZwCreateSection(&section, SECTION_ALL_ACCESS, NULL, &size, PAGE_READWRITE, SEC_COMMIT, NULL);
    
    // 2. Map into kernel space
    ZwMapViewOfSection(section, NtCurrentProcess(), &kernel_view, 0, RequestedSize, NULL, &view_size, ViewUnmap, 0, PAGE_READWRITE);
    
    // 3. Initialize ring buffer header
    AVB_TS_RING_HEADER *header = (AVB_TS_RING_HEADER*)kernel_view;
    header->write_index = 0;
    header->read_index = 0;
    header->size = (RequestedSize - sizeof(AVB_TS_RING_HEADER)) / sizeof(AVB_TS_ENTRY);
    
    // 4. Return section handle to user-mode
    *SectionHandle = section;
    return STATUS_SUCCESS;
}
```

#### User-Mode Mapping
```c
// User-mode application
HANDLE hSection;
DeviceIoControl(hDevice, IOCTL_AVB_ALLOC_TS_RING, ...);

// Map section into user-mode address space
AVB_TS_RING_HEADER *ring = (AVB_TS_RING_HEADER*)MapViewOfFile(hSection, FILE_MAP_READ, 0, 0, 0);

// Poll for new timestamps
while (running) {
    while (ring->read_index != ring->write_index) {
        ULONG idx = ring->read_index % ring->size;
        AVB_TS_ENTRY *entry = &ring->entries[idx];
        printf("Timestamp: %llu ns\n", entry->timestamp);
        ring->read_index++;
    }
    Sleep(1);  // Or use event for blocking
}
```

### Validation Criteria
- [ ] Section creation succeeds
- [ ] Kernel-mode mapping succeeds
- [ ] User-mode mapping succeeds
- [ ] Ring buffer write (kernel) → read (user-mode) works
- [ ] Atomic increment prevents race conditions
- [ ] Overrun detection and handling works
- [ ] Performance test: 10K timestamps/sec sustained

### Traceability
- **Addresses**: #6 (REQ-F-PTP-004: Rx Packet Timestamping)
- **Addresses**: #71 (REQ-NF-PERF: Low-latency timestamp delivery)
- **Addresses**: #83 (REQ-F-PERF-MONITOR-001: Performance Counter Monitoring)
- **Relates to**: #ADR-002 (Direct BAR0 Access for timestamp capture)
- **Components Affected**: 
  - #ARC-C-002 (Hardware Access Layer - timestamp capture)
  - #ARC-C-003 (AVB Integration Layer - ring buffer management)
- **Verified by**: User-mode test application polling ring buffer

---

**Status**: Proposed (Structure defined, not fully implemented)  
**Date**: 2024 (Structure added)  
**Deciders**: Development Team  
**Code Evidence**: 
- `avb_integration.h` (AVB_DEVICE_CONTEXT ring buffer fields)
- **Implementation Pending**: IOCTL handler, kernel producer logic, user-mode consumer example

---

**END OF ADR TEMPLATES**

---

## Next Steps

1. **Create these as GitHub Issues**: Copy each ADR template to a new issue with appropriate labels
2. **Add issue numbers to code**: Update code comments with `// Implements: #ADR-001` references
3. **Create ARC-C issues**: Component specifications (see next file)
4. **Link requirements**: Update requirement issues with `Implemented by: #ADR-xxx` links
