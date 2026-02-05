---
description: "CRITICAL: Hardware Abstraction Layer (HAL) enforcement for src/ folder. NO device-specific code allowed."
applyTo: "src/**"
---

# Hardware Abstraction Layer (HAL) - STRICT ENFORCEMENT

## 🚨 ABSOLUTE RULE: NO Device-Specific Code in src/

**The files in folder `src/` MUST NOT contain ANY adapter-specific code, register addresses, or device-specific logic.**

Violations of this rule break:
- Multi-device support (I210, I226, E82575, etc.)
- Maintainability (changes spread across generic and device code)
- Testability (cannot mock device operations)
- Standards compliance (ISO/IEC/IEEE 42010 - Architecture separation)

## ✅ HAL Architecture Components

We have established a **clean Hardware Abstraction Layer**:

### 1. Interface Definition Layer
- **File**: `devices/intel_device_interface.h`
- **Purpose**: Common interface definition for Intel device-specific implementations
- **Contents**: `intel_device_ops_t` structure with function pointers
- **Extensibility**: Add new operations here when needed (e.g., target time, aux timestamp)

### 2. Device Implementation Layer
- **Files**: `devices/intel_*_impl.c` (e.g., `intel_i226_impl.c`, `intel_i210_impl.c`)
- **Purpose**: Device-specific register access and feature implementation
- **Contents**: Register addresses, hardware quirks, capability detection
- **Evidence-Based**: Register definitions from Linux drivers, Intel datasheets

### 3. Device Registry & Dispatcher
- **File**: `devices/intel_device_registry.c`
- **Purpose**: Runtime lookup of device operations
- **API**: `intel_get_device_ops(device_type)` returns ops for specific device
- **Isolation**: Prevents cross-contamination between device implementations

### 4. Generic Integration Layer (THIS IS src/)
- **Files**: `src/avb_integration_fixed.c`, `src/device.c`
- **Purpose**: NDIS filter integration, IOCTL handling, generic logic
- **RULE**: **NEVER contains register addresses or device-specific logic**
- **Usage**: Calls `dev->ops->operation()` through HAL interface

## ❌ FORBIDDEN in src/ (HAL Violations)

### Violation Type 1: Hardcoded Register Addresses
```c
// ❌ WRONG - Device-specific register address
uint32_t trgttiml_offset = 0x0B644;  // TRGTTIML0
intel_write_reg(dev, trgttiml_offset, value);

// ❌ WRONG - Direct register offset in generic code
intel_write_reg(dev, 0x0B640, tsauxc_value);  // TSAUXC register
```

### Violation Type 2: Device-Specific Feature Checks
```c
// ❌ WRONG - Device-specific capability logic
if (dev->pci_device_id == 0x15F2) {  // I226 device ID
    // I226-specific feature
}

// ❌ WRONG - Hardcoded device assumptions
if (is_i226) {
    setup_tas_i226(dev);
}
```

### Violation Type 3: Direct Hardware Quirks
```c
// ❌ WRONG - Device-specific workaround in generic code
if (dev->device_type == INTEL_DEVICE_I210) {
    // Apply I210-specific errata workaround
    intel_write_reg(dev, 0x1234, special_value);
}
```

### Violation Type 4: Register Bit Definitions
```c
// ❌ WRONG - Register bit masks in generic code
#define TSAUXC_EN_TT0  (1 << 0)   // I226-specific bit
#define TSAUXC_AUTT0   (1 << 16)  // I226-specific bit
```

## ✅ CORRECT HAL Usage Patterns

### Pattern 1: Use Device Operations Interface
```c
// ✅ CORRECT - Generic code using HAL
const intel_device_ops_t *ops = intel_get_device_ops(dev->device_type);
if (ops && ops->set_target_time) {
    int rc = ops->set_target_time(dev, timer_index, target_time_ns, enable_interrupt);
    if (rc == 0) {
        // Success - device handled hardware details
    }
}
```

### Pattern 2: Capability-Based Feature Detection
```c
// ✅ CORRECT - Use ops availability, not device ID
if (ops->setup_tas != NULL) {
    // Device supports TAS
    ops->setup_tas(dev, tas_config);
} else {
    // Device does not support TAS
    return STATUS_NOT_SUPPORTED;
}
```

### Pattern 3: Let Device Implementation Handle Details
```c
// ✅ CORRECT - Device ops hide register complexity
int rc = ops->get_systime(dev, &systime_ns);
// Device implementation reads SYSTIML/H, handles wraparound, etc.

int rc = ops->check_autt_flags(dev, &autt_flags);
// Device implementation reads TSAUXC, extracts AUTT0/AUTT1 bits
```

### Pattern 4: Extend HAL Interface When Needed
```c
// ✅ CORRECT - Add new operation to intel_device_ops_t
typedef struct _intel_device_ops {
    // ... existing ops ...
    
    // New operation for Task 7
    int (*set_target_time)(device_t *dev, uint8_t timer_index, 
                          uint64_t target_time_ns, int enable_interrupt);
    int (*check_autt_flags)(device_t *dev, uint8_t *autt_flags);
} intel_device_ops_t;

// Then each device implements it:
// devices/intel_i226_impl.c: Uses I226-specific TRGTTIML0/H registers
// devices/intel_i210_impl.c: Uses I210-specific registers (if different)
// devices/intel_e82575_impl.c: Returns -EOPNOTSUPP (not supported)
```

## 🔍 HAL Compliance Checklist

Before committing code to `src/`, verify:

- [ ] **No hardcoded register addresses** (no `0x****` offset literals)
- [ ] **No device ID checks** (no `if (device_id == 0x****)`)
- [ ] **No register bit definitions** (no `#define REG_BIT (1 << N)` for device registers)
- [ ] **Uses device ops interface** (all hardware access via `dev->ops->*()`)
- [ ] **NULL checks for optional ops** (not all devices support all features)
- [ ] **Graceful degradation** (return `STATUS_NOT_SUPPORTED` if op is NULL)
- [ ] **Device-agnostic logic only** (algorithm/protocol logic OK, hardware details NO)

## 📐 Architecture Diagram

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
?    ⚠️  NO REGISTER ADDRESSES ALLOWED HERE ⚠️           ?
???????????????????????????????????????????????????????????
                      ? dev->ops->operation()
???????????????????????????????????????????????????????????
?       Device Registry (intel_device_registry.c)         ?
?              intel_get_device_ops(type)                 ?
???????????????????????????????????????????????????????????
         ?           ?           ?           ?
   ????????????   ????????????   ????????????   ????????????
   ? I210  ?   ? I226  ?   ?82575 ?   ? I350 ?
   ?Impl   ?   ?Impl   ?   ?Impl  ?   ?Impl ?
   ????????????   ????????????   ????????????   ????????????
   Register   Register   Register   Register
   Addresses  Addresses  Addresses  Addresses
   I210-spec  I226-spec  82575-sp   I350-spec
```

## 🛠️ How to Add New Device Operation (Step-by-Step)

### Example: Adding Target Time Support (Issue #13 Task 7)

**Step 1**: Extend HAL interface (`devices/intel_device_interface.h`)
```c
typedef struct _intel_device_ops {
    // ... existing operations ...
    
    // Target time operations (Task 7)
    int (*set_target_time)(device_t *dev, uint8_t timer_index, 
                          uint64_t target_time_ns, int enable_interrupt);
    int (*get_target_time)(device_t *dev, uint8_t timer_index, 
                          uint64_t *target_time_ns);
    int (*check_autt_flags)(device_t *dev, uint8_t *autt_flags);
    int (*clear_autt_flag)(device_t *dev, uint8_t timer_index);
} intel_device_ops_t;
```

**Step 2**: Implement for each device (`devices/intel_i226_impl.c`)
```c
static int i226_set_target_time(device_t *dev, uint8_t timer_index,
                                uint64_t target_time_ns, int enable_interrupt)
{
    // I226-SPECIFIC register addresses OK here
    uint32_t trgttiml_offset = (timer_index == 0) ? 0x0B644 : 0x0B64C;
    uint32_t trgttimh_offset = (timer_index == 0) ? 0x0B648 : 0x0B650;
    
    uint32_t time_low = (uint32_t)(target_time_ns & 0xFFFFFFFF);
    uint32_t time_high = (uint32_t)(target_time_ns >> 32);
    
    if (intel_write_reg(dev, trgttiml_offset, time_low) != 0) return -1;
    if (intel_write_reg(dev, trgttimh_offset, time_high) != 0) return -1;
    
    if (enable_interrupt) {
        uint32_t tsauxc;
        intel_read_reg(dev, 0x0B640, &tsauxc);  // TSAUXC
        tsauxc |= (timer_index == 0) ? (1 << 0) : (1 << 4);  // EN_TT0/EN_TT1
        intel_write_reg(dev, 0x0B640, tsauxc);
    }
    return 0;
}

const intel_device_ops_t i226_ops = {
    // ... existing ops ...
    .set_target_time = i226_set_target_time,
    .get_target_time = i226_get_target_time,
    .check_autt_flags = i226_check_autt_flags,
    .clear_autt_flag = i226_clear_autt_flag,
};
```

**Step 3**: Use in generic code (`src/avb_integration_fixed.c`)
```c
case IOCTL_AVB_SET_TARGET_TIME:
    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
    if (!ops || !ops->set_target_time) {
        status = STATUS_NOT_SUPPORTED;
        break;
    }
    
    int rc = ops->set_target_time(&activeContext->intel_device,
                                  tgt_req->timer_index,
                                  tgt_req->target_time,
                                  tgt_req->enable_interrupt);
    // NO register addresses here!
    break;
```

## 🔗 Related Files

- **Interface**: `devices/intel_device_interface.h` - HAL contract definition
- **Registry**: `devices/intel_device_registry.c` - Device ops lookup
- **Implementations**: `devices/intel_*_impl.c` - Device-specific code lives here
- **Generic Layer**: `src/avb_integration_fixed.c` - Uses HAL, no device specifics
- **Instruction**: `.github/instructions/hardware_abstraction_device_impl.md` - Rules for device implementations

## 📚 Standards Compliance

- **ISO/IEC/IEEE 42010:2011**: Architecture viewpoints and separation of concerns
- **ISO/IEC/IEEE 12207:2017**: Software life cycle processes (design process)
- **XP Practice**: Simple Design - "No Duplication" (device logic centralized in device implementations)
- **DDD Tactical Pattern**: Repository pattern (device ops = repository for hardware access)

---

**Remember**: If you find yourself writing a register offset in `src/`, STOP and add it to the device implementation instead!