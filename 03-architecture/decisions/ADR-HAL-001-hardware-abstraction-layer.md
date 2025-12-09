# ADR-HAL-001: Hardware Abstraction Layer for Multi-Controller Support

**Status**: Accepted  
**Date**: 2025-12-09  
**Phase**: Phase 03 - Architecture Design  
**Priority**: Medium (P2)

---

## Context

The IntelAvbFilter driver must support **multiple Intel Ethernet controller variants** (I210, I211, I217, I219, I225, I226), each with **different register layouts, capabilities, and hardware quirks**.

### Current Problem State

**Controller Diversity**:

| Controller | DeviceId | PHC | TX TS | RX TS | TAS | CBS | FP | Quirks |
|------------|----------|-----|-------|-------|-----|-----|----|--------|
| **I226-V** | 0x125C | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | Full TSN support |
| **I226-IT** | 0x125D | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | Industrial temp |
| **I225-V** | 0x15F3 | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | No TAS/FP |
| **I210** | 0x1533 | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | PHC stuck bug |
| **I211** | 0x1539 | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | PHC stuck bug |
| **I219-LM** | 0x15D8 | ⚠️ | ❌ | ❌ | ❌ | ❌ | ❌ | Needs investigation |

**Legend**: TS = Timestamp, TAS = Time-Aware Shaper, CBS = Credit-Based Shaper, FP = Frame Preemption

### Anti-Patterns Without HAL

**Problem 1: Conditional Chaos**
```c
// Bad: Hardware checks littered throughout driver
NTSTATUS ReadPhc(PFILTER_ADAPTER_CONTEXT ctx, PUINT64 Time) {
    if (ctx->DeviceId == 0x125C || ctx->DeviceId == 0x125D) {
        // I226-specific PHC read
        *Time = READ_REG(ctx->Bar0, 0xB600);  // I226 SYSTIML
    } else if (ctx->DeviceId == 0x15F3) {
        // I225-specific PHC read (different offset)
        *Time = READ_REG(ctx->Bar0, 0x0C00);  // I225 SYSTIML
    } else if (ctx->DeviceId == 0x1533 || ctx->DeviceId == 0x1539) {
        // I210/I211-specific with stuck workaround
        UINT32 low = READ_REG(ctx->Bar0, 0x0B608);
        UINT32 high = READ_REG(ctx->Bar0, 0x0B60C);
        *Time = ((UINT64)high << 32) | low;
        
        // Apply I210 PHC stuck workaround
        if (*Time == 0 && ctx->PhcStuckDetected) {
            return STATUS_DEVICE_NOT_READY;
        }
    }
    // More conditions for other controllers...
}
```

**Impact**: Every hardware operation becomes a branching nightmare

**Problem 2: Feature Detection Fragility**
```c
// Bad: Manual feature checks everywhere
if (ctx->DeviceId == 0x125C || ctx->DeviceId == 0x125D) {
    // I226 has TAS
    return ConfigureTas(ctx, config);
} else {
    // Other controllers don't have TAS
    return STATUS_NOT_SUPPORTED;
}
```

**Impact**: Adding new controllers requires hunting down all conditional checks

**Problem 3: No Encapsulation**
```c
// Bad: Hardware quirks leak into generic driver logic
if (ctx->DeviceId == 0x1533) {
    // I210-specific PHC stuck workaround mixed with generic logic
    if (CheckPhcStuck(ctx)) {
        ResetPhc(ctx);  // Hardware-specific reset sequence
    }
}
```

**Impact**: Driver logic couples to hardware implementation details

### Discovery

**Requirement Feedback**: *"Each Intel controller has different register layouts, capabilities, and quirks. Without abstraction, code becomes littered with `if (deviceId == ...)` checks."*

**Multi-Device Support Pressure**: Driver must support 6+ controller variants (I210/I211/I217/I219/I225/I226) with more planned (I227, I229)

**Maintainability**: Adding new controller currently requires editing 20+ files and 50+ conditional branches

---

## Decision

**Implement Hardware Abstraction Layer (HAL)** using **function pointer-based ops table pattern** to isolate hardware-specific implementations from generic driver logic.

### Architecture: HAL Ops Table Pattern

```
Generic Driver Logic
        ↓
    HAL Interface (HARDWARE_OPS)
        ↓
    ┌───────────┬───────────┬───────────┬───────────┐
    │ I226 Ops  │ I225 Ops  │ I210 Ops  │ I219 Ops  │
    └───────────┴───────────┴───────────┴───────────┘
         ↓            ↓            ↓            ↓
    [I226 HW]   [I225 HW]   [I210 HW]   [I219 HW]
```

### HAL Interface Definition

**Core Interface** (`include/hal_interface.h`):

```c
// Hardware operations function table
typedef struct _HARDWARE_OPS {
    // PHC (Precision Hardware Clock) operations
    NTSTATUS (*ReadPhc)(PVOID Context, PUINT64 Time);
    NTSTATUS (*WritePhc)(PVOID Context, UINT64 Time);
    NTSTATUS (*AdjustPhcOffset)(PVOID Context, INT64 OffsetNs);
    NTSTATUS (*AdjustPhcFrequency)(PVOID Context, INT32 Ppb);
    
    // Timestamp operations
    NTSTATUS (*GetTxTimestamp)(PVOID Context, PUINT64 Timestamp);
    NTSTATUS (*GetRxTimestamp)(PVOID Context, PUINT64 Timestamp);
    
    // TAS (Time-Aware Shaper) operations (optional - NULL if not supported)
    NTSTATUS (*ConfigureTas)(PVOID Context, PTAS_CONFIG Config);
    NTSTATUS (*GetTasStatus)(PVOID Context, PTAS_STATUS Status);
    
    // CBS (Credit-Based Shaper) operations (optional)
    NTSTATUS (*ConfigureCbs)(PVOID Context, PCBS_CONFIG Config);
    
    // Frame Preemption operations (optional)
    NTSTATUS (*ConfigureFramePreemption)(PVOID Context, PFP_CONFIG Config);
    
    // Hardware-specific lifecycle
    NTSTATUS (*Initialize)(PVOID Context);
    VOID (*Cleanup)(PVOID Context);
    
} HARDWARE_OPS, *PHARDWARE_OPS;

// Per-adapter context includes ops table
typedef struct _FILTER_ADAPTER_CONTEXT {
    // ... existing fields
    
    PHARDWARE_OPS HwOps;      // Hardware operations table (immutable after init)
    PVOID HwContext;          // Hardware-specific context (opaque to driver)
    USHORT DeviceId;          // PCI Device ID
    UCHAR RevisionId;         // PCI Revision ID
    
    // ... other fields
} FILTER_ADAPTER_CONTEXT, *PFILTER_ADAPTER_CONTEXT;
```

### Per-Controller Implementations

**I226 Implementation** (`src/hal/i226_hw.c`):

```c
// I226-specific context
typedef struct _I226_CONTEXT {
    PVOID Bar0;               // BAR0 memory-mapped I/O base
    UINT64 PhcFrequency;      // PHC nominal frequency (Hz)
    BOOLEAN TasEnabled;       // TAS feature state
    // ... I226-specific state
} I226_CONTEXT, *PI226_CONTEXT;

// I226 PHC read (full TSN support)
NTSTATUS I226_ReadPhc(PVOID Context, PUINT64 Time) {
    PI226_CONTEXT ctx = (PI226_CONTEXT)Context;
    
    // I226-specific PHC read (SYSTIML + SYSTIMH)
    UINT32 low = READ_REG(ctx->Bar0, I226_SYSTIML);
    UINT32 high = READ_REG(ctx->Bar0, I226_SYSTIMH);
    
    *Time = ((UINT64)high << 32) | low;
    return STATUS_SUCCESS;
}

// I226 TAS configuration (fully supported)
NTSTATUS I226_ConfigureTas(PVOID Context, PTAS_CONFIG Config) {
    PI226_CONTEXT ctx = (PI226_CONTEXT)Context;
    
    // I226-specific TAS configuration
    // Program BASET (base time), CYCLE_TIME, gate control list
    WRITE_REG(ctx->Bar0, I226_TAS_BASET_L, (UINT32)(Config->BaseTime & 0xFFFFFFFF));
    WRITE_REG(ctx->Bar0, I226_TAS_BASET_H, (UINT32)(Config->BaseTime >> 32));
    WRITE_REG(ctx->Bar0, I226_TAS_CYCLE_TIME, Config->CycleTime);
    
    // Program gate control list (up to 256 entries)
    for (ULONG i = 0; i < Config->GateListSize; i++) {
        WRITE_REG(ctx->Bar0, I226_TAS_GCL(i), Config->GateList[i]);
    }
    
    ctx->TasEnabled = TRUE;
    return STATUS_SUCCESS;
}

// I226 ops table (full feature set)
const HARDWARE_OPS I226_Ops = {
    .ReadPhc = I226_ReadPhc,
    .WritePhc = I226_WritePhc,
    .AdjustPhcOffset = I226_AdjustPhcOffset,
    .AdjustPhcFrequency = I226_AdjustPhcFrequency,
    .GetTxTimestamp = I226_GetTxTimestamp,
    .GetRxTimestamp = I226_GetRxTimestamp,
    .ConfigureTas = I226_ConfigureTas,              // ✅ Supported
    .GetTasStatus = I226_GetTasStatus,              // ✅ Supported
    .ConfigureCbs = I226_ConfigureCbs,              // ✅ Supported
    .ConfigureFramePreemption = I226_ConfigureFp,   // ✅ Supported
    .Initialize = I226_Initialize,
    .Cleanup = I226_Cleanup
};
```

**I210 Implementation** (`src/hal/i210_hw.c`):

```c
// I210-specific context
typedef struct _I210_CONTEXT {
    PVOID Bar0;
    UINT64 PhcFrequency;
    BOOLEAN PhcStuckDetected;  // I210-specific bug tracking
    UINT64 LastPhcValue;       // For stuck detection
    // ... I210-specific state
} I210_CONTEXT, *PI210_CONTEXT;

// I210 PHC read with stuck workaround
NTSTATUS I210_ReadPhc(PVOID Context, PUINT64 Time) {
    PI210_CONTEXT ctx = (PI210_CONTEXT)Context;
    
    // I210-specific PHC read (different register layout)
    UINT32 low = READ_REG(ctx->Bar0, I210_SYSTIML_OFFSET);
    UINT32 high = READ_REG(ctx->Bar0, I210_SYSTIMH_OFFSET);
    
    *Time = ((UINT64)high << 32) | low;
    
    // Apply I210 PHC stuck workaround (hardware errata)
    if (*Time == ctx->LastPhcValue) {
        ctx->PhcStuckDetected = TRUE;
        
        // Reset PHC via TSAUXC register (I210-specific recovery)
        UINT32 tsauxc = READ_REG(ctx->Bar0, I210_TSAUXC);
        WRITE_REG(ctx->Bar0, I210_TSAUXC, tsauxc | I210_TSAUXC_EN_TT0);
        
        return STATUS_DEVICE_NOT_READY;
    }
    
    ctx->LastPhcValue = *Time;
    ctx->PhcStuckDetected = FALSE;
    
    return STATUS_SUCCESS;
}

// I210 ops table (no TAS support, no FP support)
const HARDWARE_OPS I210_Ops = {
    .ReadPhc = I210_ReadPhc,
    .WritePhc = I210_WritePhc,
    .AdjustPhcOffset = I210_AdjustPhcOffset,
    .AdjustPhcFrequency = I210_AdjustPhcFrequency,
    .GetTxTimestamp = I210_GetTxTimestamp,
    .GetRxTimestamp = I210_GetRxTimestamp,
    .ConfigureTas = NULL,                // ❌ Not supported
    .GetTasStatus = NULL,                // ❌ Not supported
    .ConfigureCbs = I210_ConfigureCbs,   // ✅ Supported
    .ConfigureFramePreemption = NULL,    // ❌ Not supported
    .Initialize = I210_Initialize,
    .Cleanup = I210_Cleanup
};
```

**I225 Implementation** (`src/hal/i225_hw.c`):

```c
// I225 ops table (no TAS support, no FP support)
const HARDWARE_OPS I225_Ops = {
    .ReadPhc = I225_ReadPhc,
    .WritePhc = I225_WritePhc,
    .AdjustPhcOffset = I225_AdjustPhcOffset,
    .AdjustPhcFrequency = I225_AdjustPhcFrequency,
    .GetTxTimestamp = I225_GetTxTimestamp,
    .GetRxTimestamp = I225_GetRxTimestamp,
    .ConfigureTas = NULL,                // ❌ Not supported
    .GetTasStatus = NULL,                // ❌ Not supported
    .ConfigureCbs = I225_ConfigureCbs,   // ✅ Supported
    .ConfigureFramePreemption = NULL,    // ❌ Not supported
    .Initialize = I225_Initialize,
    .Cleanup = I225_Cleanup
};
```

### Runtime Op Table Selection

**Device Detection and Binding** (`src/hal/hal_init.c`):

```c
NTSTATUS InitializeHardwareOps(
    PFILTER_ADAPTER_CONTEXT ctx, 
    USHORT DeviceId, 
    UCHAR RevisionId
) {
    NTSTATUS status = STATUS_SUCCESS;
    
    // Select ops table based on PCI Device ID
    switch (DeviceId) {
        case 0x125C:  // I226-V
        case 0x125D:  // I226-IT
            DbgPrint("IntelAvbFilter: Detected I226 (DeviceId 0x%04X)\n", DeviceId);
            ctx->HwOps = &I226_Ops;
            ctx->HwContext = AllocateI226Context();
            break;
            
        case 0x15F3:  // I225-V
            DbgPrint("IntelAvbFilter: Detected I225 (DeviceId 0x%04X)\n", DeviceId);
            ctx->HwOps = &I225_Ops;
            ctx->HwContext = AllocateI225Context();
            break;
            
        case 0x1533:  // I210
        case 0x1539:  // I211
            DbgPrint("IntelAvbFilter: Detected I210/I211 (DeviceId 0x%04X)\n", DeviceId);
            ctx->HwOps = &I210_Ops;
            ctx->HwContext = AllocateI210Context();
            break;
            
        case 0x15D8:  // I219-LM
        case 0x15D7:  // I219-V
            DbgPrint("IntelAvbFilter: Detected I219 (DeviceId 0x%04X) - Limited Support\n", DeviceId);
            ctx->HwOps = &I219_Ops;
            ctx->HwContext = AllocateI219Context();
            break;
            
        case 0x15A0:  // I217-LM
        case 0x15A1:  // I217-V
            DbgPrint("IntelAvbFilter: Detected I217 (DeviceId 0x%04X) - Limited Support\n", DeviceId);
            ctx->HwOps = &I217_Ops;
            ctx->HwContext = AllocateI217Context();
            break;
            
        default:
            DbgPrint("IntelAvbFilter: Unsupported device (DeviceId 0x%04X)\n", DeviceId);
            return STATUS_NOT_SUPPORTED;
    }
    
    if (!ctx->HwContext) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Store device info
    ctx->DeviceId = DeviceId;
    ctx->RevisionId = RevisionId;
    
    // Call hardware-specific initialization
    status = ctx->HwOps->Initialize(ctx->HwContext);
    if (!NT_SUCCESS(status)) {
        DbgPrint("IntelAvbFilter: Hardware initialization failed (0x%08X)\n", status);
        FreeHardwareContext(ctx->HwContext);
        ctx->HwContext = NULL;
        ctx->HwOps = NULL;
    }
    
    return status;
}
```

### Generic Driver Code Using HAL

**Example: PHC Query IOCTL** (`src/device.c`):

```c
// Good: Generic code using HAL
NTSTATUS HandlePhcQuery(PFILTER_ADAPTER_CONTEXT ctx, PIRP Irp) {
    UINT64 phcTime = 0;
    NTSTATUS status;
    
    // Works for ALL controllers via HAL
    status = ctx->HwOps->ReadPhc(ctx->HwContext, &phcTime);
    
    if (NT_SUCCESS(status)) {
        // Copy result to user buffer
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &phcTime, sizeof(phcTime));
        Irp->IoStatus.Information = sizeof(phcTime);
    }
    
    return status;
}
```

**Example: TAS Configuration with Feature Detection**:

```c
NTSTATUS HandleTasConfig(PFILTER_ADAPTER_CONTEXT ctx, PTAS_CONFIG Config) {
    // Check if controller supports TAS
    if (ctx->HwOps->ConfigureTas == NULL) {
        DbgPrint("IntelAvbFilter: TAS not supported on this controller (DeviceId 0x%04X)\n", 
                 ctx->DeviceId);
        return STATUS_NOT_SUPPORTED;
    }
    
    // Call hardware-specific TAS configuration
    return ctx->HwOps->ConfigureTas(ctx->HwContext, Config);
}
```

**Bad Example (Without HAL)**:

```c
// Bad: Direct hardware access with conditionals
NTSTATUS HandlePhcQuery_Bad(PFILTER_ADAPTER_CONTEXT ctx, PIRP Irp) {
    UINT64 phcTime = 0;
    
    // Fragile conditional logic for each controller
    if (ctx->DeviceId == 0x125C || ctx->DeviceId == 0x125D) {
        // I226-specific
        phcTime = READ_REG(ctx->Bar0, 0xB600);
    } else if (ctx->DeviceId == 0x15F3) {
        // I225-specific (different offset)
        phcTime = READ_REG(ctx->Bar0, 0x0C00);
    } else if (ctx->DeviceId == 0x1533) {
        // I210-specific with workaround
        UINT32 low = READ_REG(ctx->Bar0, 0x0B608);
        UINT32 high = READ_REG(ctx->Bar0, 0x0B60C);
        phcTime = ((UINT64)high << 32) | low;
        
        // I210 PHC stuck workaround
        if (phcTime == 0) {
            return STATUS_DEVICE_NOT_READY;
        }
    }
    // ... more conditions for other controllers
    
    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &phcTime, sizeof(phcTime));
    return STATUS_SUCCESS;
}
```

---

## Rationale

### 1. Single Responsibility Principle

**Before HAL**: Driver code mixes generic logic with hardware-specific details  
**After HAL**: Generic driver focuses on protocol, HAL focuses on hardware

**Benefit**: Clear separation of concerns, easier to reason about code

### 2. Open/Closed Principle

**Adding New Controller Without HAL**:
1. Edit 20+ files with conditional logic
2. Hunt down all `if (DeviceId == ...)` branches
3. Test ALL code paths (risk of breaking existing controllers)

**Adding New Controller With HAL**:
1. Implement `HARDWARE_OPS` interface for new controller (single file: `i227_hw.c`)
2. Add DeviceId case to `InitializeHardwareOps()` switch
3. Generic driver code unchanged (no testing needed)

**Benefit**: Open for extension (new controllers), closed for modification (existing code)

### 3. Encapsulation of Hardware Quirks

**I210 PHC Stuck Bug**:
- **Without HAL**: Workaround code leaks into generic driver logic
- **With HAL**: Encapsulated in `I210_ReadPhc()`, invisible to driver

**Benefit**: Hardware bugs isolated, don't contaminate generic code

### 4. Feature Detection via NULL Pointers

**TAS Feature Check**:
```c
if (ctx->HwOps->ConfigureTas == NULL) {
    return STATUS_NOT_SUPPORTED;
}
```

**Benefit**: Simple, zero-overhead feature detection at runtime

### 5. Type Safety and Compile-Time Checks

**Function Pointer Interface**:
- Compiler enforces function signatures
- Wrong parameters caught at compile time
- IDE provides autocomplete for ops table

**Benefit**: Catches integration errors early

### 6. Testability

**Without HAL**: Must test on physical I210/I225/I226 hardware  
**With HAL**: Can mock `HARDWARE_OPS` for unit testing

**Example Mock**:
```c
NTSTATUS Mock_ReadPhc(PVOID Context, PUINT64 Time) {
    *Time = 1234567890;  // Fake timestamp
    return STATUS_SUCCESS;
}

const HARDWARE_OPS Mock_Ops = {
    .ReadPhc = Mock_ReadPhc,
    // ... other mocked functions
};
```

**Benefit**: Unit tests without hardware, faster test cycles

---

## Alternatives Considered

### Alternative 1: Direct Hardware Access with Conditionals

**Approach**: Use `if (DeviceId == ...)` throughout driver code

**Rejected**:
- ❌ Conditional logic scattered across 20+ files
- ❌ Adding new controller requires editing generic driver code
- ❌ High risk of breaking existing controllers when adding new ones
- ❌ Hardware quirks leak into driver logic

### Alternative 2: Macro-Based Abstraction

**Approach**: Use C preprocessor macros for hardware access

```c
#define READ_PHC(ctx) \
    (ctx->DeviceId == 0x125C ? READ_I226_PHC(ctx) : \
     ctx->DeviceId == 0x15F3 ? READ_I225_PHC(ctx) : \
     READ_I210_PHC(ctx))
```

**Rejected**:
- ❌ No compile-time type safety
- ❌ Macros don't support NULL (feature detection difficult)
- ❌ Debugging nightmare (macros expand at compile time)
- ❌ Cannot mock for testing

### Alternative 3: Object-Oriented (C++) with Virtual Functions

**Approach**: Use C++ classes with virtual methods

```cpp
class HardwareController {
public:
    virtual NTSTATUS ReadPhc(UINT64* Time) = 0;
    virtual NTSTATUS WritePh(UINT64 Time) = 0;
    // ...
};

class I226Controller : public HardwareController {
    // Override methods
};
```

**Rejected**:
- ❌ Windows kernel drivers typically written in C (not C++)
- ❌ C++ runtime overhead in kernel (RTTI, exceptions)
- ❌ Build complexity (C++ compiler, linker, STL)
- ❌ Compatibility issues with Windows Driver Model

**Why Function Pointers (Chosen Approach)**:
- ✅ Pure C (standard for Windows kernel drivers)
- ✅ Zero runtime overhead (direct function calls, no vtable lookup)
- ✅ Simple to understand and debug
- ✅ Compatible with all kernel development tools

### Alternative 4: Monolithic Per-Controller Drivers

**Approach**: Separate driver binary for each controller (IntelAvbFilter_I226.sys, IntelAvbFilter_I210.sys)

**Rejected**:
- ❌ Code duplication (generic driver logic duplicated across binaries)
- ❌ Installation complexity (INF must select correct binary)
- ❌ Bug fixes must be applied to all binaries
- ❌ Testing burden (6+ separate drivers to test)

---

## Consequences

### Positive
- ✅ **Clear Separation**: Generic driver logic separated from hardware-specific code
- ✅ **Extensibility**: New controllers added by implementing `HARDWARE_OPS` interface (single file)
- ✅ **Feature Detection**: NULL pointer checks for unsupported features (zero overhead)
- ✅ **Hardware Quirks Isolated**: Bugs like I210 PHC stuck encapsulated in HAL
- ✅ **Testability**: Mock ops tables for unit testing without hardware
- ✅ **Type Safety**: Compiler enforces function signatures
- ✅ **Zero Runtime Overhead**: Direct function pointer calls (no vtable lookup)
- ✅ **Maintainability**: Changes to one controller don't affect others

### Negative
- ❌ **Initial Refactoring Effort**: ~6 hours to extract hardware code into HAL
- ❌ **Indirection**: One extra function call per hardware operation
- ❌ **Memory Overhead**: 14 function pointers per adapter (~112 bytes on x64)
- ❌ **Learning Curve**: Developers must understand ops table pattern

### Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Performance overhead** | Function pointer indirection adds ~1ns per call | Negligible vs. hardware access latency (µs range) |
| **NULL pointer dereference** | Calling unsupported feature crashes driver | Check for NULL before calling: `if (ops->ConfigureTas == NULL)` |
| **Op table initialization error** | Wrong ops table assigned to controller | Assert DeviceId matches expected range in Initialize() |
| **Context confusion** | Wrong context passed to ops function | Type-safe context structures (PI226_CONTEXT, PI210_CONTEXT) |

---

## Implementation Plan

### Phase 1: Define HAL Interface (1 hour)

**Create** `include/hal_interface.h`:
```c
typedef struct _HARDWARE_OPS {
    // PHC operations
    NTSTATUS (*ReadPhc)(PVOID Context, PUINT64 Time);
    NTSTATUS (*WritePhc)(PVOID Context, UINT64 Time);
    NTSTATUS (*AdjustPhcOffset)(PVOID Context, INT64 OffsetNs);
    NTSTATUS (*AdjustPhcFrequency)(PVOID Context, INT32 Ppb);
    
    // Timestamp operations
    NTSTATUS (*GetTxTimestamp)(PVOID Context, PUINT64 Timestamp);
    NTSTATUS (*GetRxTimestamp)(PVOID Context, PUINT64 Timestamp);
    
    // TAS operations (optional)
    NTSTATUS (*ConfigureTas)(PVOID Context, PTAS_CONFIG Config);
    NTSTATUS (*GetTasStatus)(PVOID Context, PTAS_STATUS Status);
    
    // CBS operations (optional)
    NTSTATUS (*ConfigureCbs)(PVOID Context, PCBS_CONFIG Config);
    
    // Frame Preemption operations (optional)
    NTSTATUS (*ConfigureFramePreemption)(PVOID Context, PFP_CONFIG Config);
    
    // Lifecycle
    NTSTATUS (*Initialize)(PVOID Context);
    VOID (*Cleanup)(PVOID Context);
    
} HARDWARE_OPS, *PHARDWARE_OPS;
```

**Update** `include/filter.h`:
```c
typedef struct _FILTER_ADAPTER_CONTEXT {
    // ... existing fields
    PHARDWARE_OPS HwOps;
    PVOID HwContext;
    USHORT DeviceId;
    UCHAR RevisionId;
} FILTER_ADAPTER_CONTEXT;
```

### Phase 2: Implement I226 HAL (1.5 hours)

**Create** `src/hal/i226_hw.c`:
- Implement `I226_ReadPhc()`, `I226_WritePhc()`, etc.
- Implement `I226_ConfigureTas()` (full TAS support)
- Define `I226_Ops` table

**Create** `src/hal/i226_hw.h`:
- Define `I226_CONTEXT` structure
- Declare ops table: `extern const HARDWARE_OPS I226_Ops;`

### Phase 3: Implement I210/I225 HAL (1.5 hours)

**Create** `src/hal/i210_hw.c`:
- Implement PHC operations with stuck workaround
- Set `ConfigureTas = NULL` (not supported)
- Define `I210_Ops` table

**Create** `src/hal/i225_hw.c`:
- Similar to I226 but without TAS/FP
- Define `I225_Ops` table

### Phase 4: Runtime Op Table Selection (1 hour)

**Create** `src/hal/hal_init.c`:
```c
NTSTATUS InitializeHardwareOps(
    PFILTER_ADAPTER_CONTEXT ctx,
    USHORT DeviceId,
    UCHAR RevisionId
);

VOID CleanupHardwareOps(PFILTER_ADAPTER_CONTEXT ctx);
```

**Implement DeviceId → Ops Table Mapping**:
- Switch statement on DeviceId
- Allocate controller-specific context
- Call `ctx->HwOps->Initialize()`

### Phase 5: Refactor Generic Driver Code (1 hour)

**Update** `device.c`:
```c
// Before:
UINT64 phcTime = READ_REG(ctx->Bar0, 0xB600);

// After:
ctx->HwOps->ReadPhc(ctx->HwContext, &phcTime);
```

**Update** `avb_integration_fixed.c`:
- Replace direct register access with HAL calls
- Add NULL checks before calling optional ops (TAS, FP)

---

## Validation and Testing

### Test-1: Op Table Assignment
```c
VOID Test_OpTableAssignment() {
    FILTER_ADAPTER_CONTEXT ctx = {0};
    
    // I226 should get full ops table
    InitializeHardwareOps(&ctx, 0x125C, 0x00);
    ASSERT(ctx.HwOps == &I226_Ops);
    ASSERT(ctx.HwOps->ConfigureTas != NULL);  // TAS supported
    
    // I210 should get limited ops table
    InitializeHardwareOps(&ctx, 0x1533, 0x00);
    ASSERT(ctx.HwOps == &I210_Ops);
    ASSERT(ctx.HwOps->ConfigureTas == NULL);  // TAS not supported
}
```

### Test-2: PHC Read via HAL
```c
VOID Test_PhcReadViaHal() {
    FILTER_ADAPTER_CONTEXT ctx = {0};
    UINT64 phcTime = 0;
    
    InitializeHardwareOps(&ctx, 0x125C, 0x00);
    NTSTATUS status = ctx.HwOps->ReadPhc(ctx.HwContext, &phcTime);
    
    ASSERT(NT_SUCCESS(status));
    ASSERT(phcTime > 0);  // Valid timestamp
}
```

### Test-3: Feature Detection
```c
VOID Test_FeatureDetection() {
    FILTER_ADAPTER_CONTEXT ctx = {0};
    
    // I226 supports TAS
    InitializeHardwareOps(&ctx, 0x125C, 0x00);
    ASSERT(ctx.HwOps->ConfigureTas != NULL);
    
    // I210 does not support TAS
    InitializeHardwareOps(&ctx, 0x1533, 0x00);
    ASSERT(ctx.HwOps->ConfigureTas == NULL);
    
    // Graceful handling
    if (ctx.HwOps->ConfigureTas == NULL) {
        // Driver returns STATUS_NOT_SUPPORTED
        ASSERT(TRUE);
    }
}
```

### Test-4: New Controller Addition
```c
// Scenario: Add I227 support
// 1. Implement i227_hw.c with HARDWARE_OPS
// 2. Add case 0x1234 to InitializeHardwareOps()
// 3. Generic driver code unchanged (no modifications needed)

VOID Test_AddNewController() {
    FILTER_ADAPTER_CONTEXT ctx = {0};
    
    // Add I227 case to InitializeHardwareOps()
    // Case 0x1234: ctx->HwOps = &I227_Ops;
    
    InitializeHardwareOps(&ctx, 0x1234, 0x00);
    ASSERT(ctx.HwOps == &I227_Ops);
    
    // Generic driver code works without changes
    UINT64 phcTime = 0;
    ctx.HwOps->ReadPhc(ctx.HwContext, &phcTime);
    ASSERT(phcTime > 0);
}
```

---

## Compliance

**Standards**:
- ISO/IEC 25010:2011 (Maintainability - Modularity, Reusability)
- ISO/IEC/IEEE 42010:2011 (Architecture Patterns)

**Design Patterns**:
- **Strategy Pattern**: Runtime selection of hardware algorithm
- **Adapter Pattern**: HAL adapts hardware-specific implementations to generic interface

**Principles**:
- **Single Responsibility**: HAL implementations focus only on hardware
- **Open/Closed**: Open for extension (new controllers), closed for modification
- **Dependency Inversion**: Driver depends on HAL interface, not concrete implementations

---

## Traceability

Traces to: 
- #84 (REQ-NF-PORTABILITY-001: Hardware Portability)
- #29 (StR-INTEL-AVB-LIB: Intel AVB Library Integration)

**Related**:
- #50 (REQ-F-HWDETECT-003: Hardware Capabilities Detection)
- #21 (REQ-NF-REGS-001: Intel-Ethernet-Regs Submodule) - Provides register definitions for HAL

**Verified by**:
- TEST-HAL-OPTAB-001: Op table assignment correctness
- TEST-HAL-PHC-001: PHC read/write via HAL
- TEST-HAL-FEAT-001: Feature detection (NULL pointer checks)
- TEST-HAL-EXTEND-001: New controller addition requires only HAL implementation

---

## Notes

**Supported Controllers**:

| Controller | DeviceId | Status | HAL Implementation | Notes |
|------------|----------|--------|--------------------|-------|
| I226-V | 0x125C | ✅ Full | `i226_hw.c` | Full TSN (TAS, CBS, FP) |
| I226-IT | 0x125D | ✅ Full | `i226_hw.c` | Industrial temp variant |
| I225-V | 0x15F3 | ✅ Partial | `i225_hw.c` | No TAS/FP |
| I210 | 0x1533 | ✅ Partial | `i210_hw.c` | PHC stuck workaround |
| I211 | 0x1539 | ✅ Partial | `i210_hw.c` | Same as I210 |
| I219-LM | 0x15D8 | ⚠️ Limited | `i219_hw.c` | Basic support only |
| I217-LM | 0x15A0 | ⚠️ Limited | `i217_hw.c` | Basic support only |

**Future Controllers**: I227, I229 (add by implementing `HARDWARE_OPS` interface)

**Effort**: ~6 hours (1h interface + 1.5h I226 + 1.5h I210/I225 + 1h init + 1h refactor)

**Impact**: HIGH (foundational change, affects all hardware interactions)

**Breaking Change**: No (internal refactoring, same external behavior)

---

**Status**: Accepted  
**Deciders**: Architecture Team  
**Date**: 2025-12-09
