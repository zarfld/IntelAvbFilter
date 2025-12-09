# ADR-ARCH-003: Device Abstraction via Strategy Pattern

**Status**: Accepted  
**Date**: 2025-12-08  
**GitHub Issue**: #92  
**Phase**: Phase 03 - Architecture Design  
**Priority**: Critical (P0)

---

## Context

IntelAvbFilter must support 8+ Intel Ethernet controller families, each with different:
- Register layouts and offsets
- PTP implementations (some lack features)
- TSN capabilities (TAS/CBS support varies)
- Hardware quirks and errata

**Problem**: How to support multiple device families without creating unmaintainable code with extensive `if`/`switch` statements?

---

## Decision

**Implement Strategy Pattern using function pointer tables** (`intel_device_ops_t`) with device registry for runtime dispatch.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  AVB Integration Layer (device-agnostic)                    │
│  - Calls: device_ops->get_systime(device, &systime)        │
└──────────────────┬──────────────────────────────────────────┘
                   │ Function pointer dispatch
┌──────────────────┴──────────────────────────────────────────┐
│  Device Registry (intel_device_registry.c)                  │
│  - Maps PCI Device ID → intel_device_ops_t*                │
│  - Registers: i210_ops, i219_ops, i225_ops, etc.           │
└──────────────────┬──────────────────────────────────────────┘
                   │ Selects implementation
┌──────────────────┴──────────────────────────────────────────┐
│  Device-Specific Implementations                             │
│  ├─ intel_i210_impl.c  (i210_ops)                           │
│  ├─ intel_i219_impl.c  (i219_ops)                           │
│  ├─ intel_i225_impl.c  (i225_ops)                           │
│  └─ ... 5 more device families                              │
└─────────────────────────────────────────────────────────────┘
```

### Rationale

1. **Extensibility**: Adding new devices requires only implementing operations table
   - No core code changes
   - ~500 lines per new device implementation

2. **Maintainability**: Device-specific logic isolated in dedicated files
   - Clear ownership boundaries
   - Easy to review device-specific changes

3. **Testability**: Mock operations tables enable unit testing without hardware
   - Can simulate device behaviors
   - Regression testing across all devices

4. **Performance**: Function pointer dispatch adds <10ns overhead
   - Negligible vs. register I/O (~300-500ns)
   - Modern CPUs predict indirect branches well

---

## Alternatives Considered

### Alternative 1: Switch/Case on Device ID

**Implementation**: Central dispatch function with `switch(device_id)`

```c
NTSTATUS GetSystime(device_t *device, u64 *systime) {
    switch (device->device_id) {
        case 0x1533: return i210_get_systime(device, systime);
        case 0x15B7: return i219_get_systime(device, systime);
        // ... 20+ more cases
    }
}
```

**Pros**:
- Simple to understand
- No function pointer overhead

**Cons**:
- ❌ O(n) code growth per device (every operation duplicates switch)
- ❌ Core code changes when adding devices
- ❌ Hard to test (must mock entire device ID)

**Decision**: **Rejected** - Poor maintainability

### Alternative 2: C++ Inheritance

**Implementation**: Virtual base class `IntelDevice` with derived classes

```cpp
class IntelDevice {
    virtual NTSTATUS GetSystime(u64 *systime) = 0;
};

class I210Device : public IntelDevice {
    NTSTATUS GetSystime(u64 *systime) override { ... }
};
```

**Pros**:
- Type-safe polymorphism
- Compiler-enforced interface

**Cons**:
- ❌ C99 project (kernel drivers avoid C++)
- ❌ Runtime overhead (vtable indirection)
- ❌ Complex ABI (name mangling, exception handling)

**Decision**: **Rejected** - Not idiomatic for Windows kernel drivers

### Alternative 3: Preprocessor Macros

**Implementation**: Compile-time device selection via `#ifdef`

```c
#ifdef DEVICE_I210
    #define get_systime i210_get_systime
#elif defined(DEVICE_I219)
    #define get_systime i219_get_systime
#endif
```

**Pros**:
- Zero runtime overhead
- Compiler optimizes aggressively

**Cons**:
- ❌ Compile-time dispatch (cannot support runtime device detection)
- ❌ Must compile separate binaries per device
- ❌ Complex build system

**Decision**: **Rejected** - Cannot detect device at runtime

---

## Consequences

### Positive
- ✅ Add new device: Implement 28 operations + register in table (~500 lines)
- ✅ Unit testable with mock operations tables
- ✅ Clear device capability documentation (ops table = contract)
- ✅ Minimal core code changes when adding devices

### Negative
- ❌ Additional abstraction layer (function pointer indirection)
- ❌ Slightly increased memory footprint (ops tables per device type)
- ❌ Debugging more complex (indirect calls harder to trace)

### Risks
- **NULL Function Pointers**: Crash if ops table incomplete
  - **Mitigation**: Mandatory validation at registration time
- **ABI Instability**: Ops structure changes break device implementations
  - **Mitigation**: Versioned structure (`ops_version` field)

---

## Implementation

### Device Operations Interface (`devices/intel_device_interface.h`)

```c
typedef struct intel_device_ops {
    ULONG ops_version;  // Version for ABI compatibility
    
    // Lifecycle (3 ops)
    NTSTATUS (*init)(device_t *device);
    NTSTATUS (*cleanup)(device_t *device);
    NTSTATUS (*get_info)(device_t *device, device_info_t *info);
    
    // PTP Clock Control (8 ops)
    NTSTATUS (*get_systime)(device_t *device, u64 *systime);
    NTSTATUS (*set_systime)(device_t *device, u64 systime);
    NTSTATUS (*adjust_freq)(device_t *device, s32 ppb);
    NTSTATUS (*get_tx_tstamp)(device_t *device, u64 *tstamp);
    NTSTATUS (*get_rx_tstamp)(device_t *device, u64 *tstamp);
    NTSTATUS (*enable_tstamp)(device_t *device, BOOLEAN enable);
    NTSTATUS (*config_pins)(device_t *device, pin_config_t *config);
    NTSTATUS (*get_caps)(device_t *device, ptp_caps_t *caps);
    
    // TSN Features (8 ops)
    NTSTATUS (*setup_tas)(device_t *device, tas_config_t *config);
    NTSTATUS (*setup_cbs)(device_t *device, u8 queue, cbs_config_t *config);
    NTSTATUS (*enable_launch_time)(device_t *device, BOOLEAN enable);
    NTSTATUS (*set_launch_time)(device_t *device, u8 queue, u64 launch_time);
    NTSTATUS (*get_tas_status)(device_t *device, tas_status_t *status);
    NTSTATUS (*get_cbs_status)(device_t *device, u8 queue, cbs_status_t *status);
    NTSTATUS (*reset_tas)(device_t *device);
    NTSTATUS (*reset_cbs)(device_t *device, u8 queue);
    
    // Diagnostics (6 ops)
    NTSTATUS (*read_register)(device_t *device, u32 offset, u32 *value);
    NTSTATUS (*write_register)(device_t *device, u32 offset, u32 value);
    NTSTATUS (*dump_registers)(device_t *device, register_dump_t *dump);
    NTSTATUS (*get_statistics)(device_t *device, device_stats_t *stats);
    NTSTATUS (*self_test)(device_t *device, self_test_results_t *results);
    NTSTATUS (*get_device_info)(device_t *device, device_info_t *info);
    
    // Reserved for future expansion (3 ops)
    void *reserved[3];
} intel_device_ops_t;

#define INTEL_DEVICE_OPS_VERSION 1
```

### Device Registry (`devices/intel_device_registry.c`)

```c
typedef struct {
    intel_device_type_t device_type;
    USHORT pci_device_id;
    const char *device_name;
    intel_device_ops_t *ops;
} device_registry_entry_t;

static device_registry_entry_t device_registry[] = {
    {INTEL_I210, 0x1533, "i210", &intel_i210_ops},
    {INTEL_I211, 0x1539, "i211", &intel_i211_ops},
    {INTEL_I217, 0x153A, "i217-LM", &intel_i217_ops},
    {INTEL_I219, 0x15B7, "i219-LM", &intel_i219_ops},
    {INTEL_I225, 0x15F2, "i225-LM", &intel_i225_ops},
    {INTEL_I226, 0x125B, "i226-LM", &intel_i226_ops},
    {INTEL_I350, 0x1521, "i350", &intel_i350_ops},
    {INTEL_82575, 0x10A7, "82575EB", &intel_82575_ops},
    {INTEL_DEVICE_UNKNOWN, 0, NULL, NULL}
};

intel_device_ops_t* intel_get_device_ops(USHORT pci_device_id) {
    for (int i = 0; device_registry[i].ops != NULL; i++) {
        if (device_registry[i].pci_device_id == pci_device_id) {
            // Validate ops table
            if (device_registry[i].ops->ops_version != INTEL_DEVICE_OPS_VERSION) {
                return NULL;  // Version mismatch
            }
            return device_registry[i].ops;
        }
    }
    return NULL;  // Unsupported device
}
```

### Runtime Dispatch (`avb_integration.c`)

```c
NTSTATUS AvbInitializeDevice(AVB_DEVICE_CONTEXT *DeviceContext) {
    // Get device operations for PCI device ID
    DeviceContext->device_ops = intel_get_device_ops(DeviceContext->pci_device_id);
    if (!DeviceContext->device_ops) {
        KdPrint(("AvbInitializeDevice: Unsupported device ID 0x%04X\n", 
                 DeviceContext->pci_device_id));
        return STATUS_NOT_SUPPORTED;
    }
    
    // Call device-specific initialization
    NTSTATUS status = DeviceContext->device_ops->init(DeviceContext);
    if (!NT_SUCCESS(status)) {
        KdPrint(("AvbInitializeDevice: Device init failed: 0x%08X\n", status));
        return status;
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS AvbGetSystemTime(AVB_DEVICE_CONTEXT *DeviceContext, u64 *systime) {
    // Dispatch to device-specific implementation
    return DeviceContext->device_ops->get_systime(DeviceContext, systime);
}
```

---

## Compliance

**Design Pattern**: Strategy Pattern (Gang of Four)  
**Standards**: IEEE 1016-2009 (Software Design Descriptions)

---

## Traceability

Traces to: 
- #1 (StR: Hardware Abstraction for Multiple Intel Devices)
- #10 (REQ-F-DIAG-001: Read Hardware Registers)
- #11 (REQ-F-DIAG-002: Dump Register State)
- #12 (REQ-F-DIAG-003: Device Self-Test)
- #13 (REQ-F-DIAG-004: Get Device Statistics)

**Implemented by**:
- #97 (ARC-C-004: Device Abstraction Layer)

**Used by**:
- #95 (ARC-C-002: AVB Integration Layer)

**Device Implementations**:
- #98 (i210), #99 (i219), #100 (i225), #101 (i226), #102 (i217), #103 (i350), #104 (82575), #105 (82576)

---

## Notes

- Operations table validation prevents NULL pointer crashes
- Versioning allows future ops structure changes without breaking existing implementations
- Reserved fields enable backward-compatible additions

---

**Last Updated**: 2025-12-08  
**Author**: Architecture Team
