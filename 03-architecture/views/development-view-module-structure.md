# Development View: Module Structure and Build Organization

**View Type**: Development View  
**Standard**: ISO/IEC/IEEE 42010:2011  
**Last Updated**: 2025-12-17  
**Status**: Approved

## Purpose

Describes code organization, module dependencies, build configuration, and development workflows for the IntelAvbFilter driver.

## Stakeholders

- **Developers**: Navigate codebase and understand module boundaries
- **Build Engineers**: Configure build system and manage dependencies  
- **DevOps**: Automate CI/CD pipelines and artifact generation
- **Reviewers**: Enforce modularity and prevent circular dependencies

## Related Architecture Decisions

- **Relates to**: #92 (ADR-DEVICE-ABSTRACT-001: Strategy Pattern)
- **Relates to**: #126 (ADR-HAL-001: Hardware Abstraction Layer)
- **Relates to**: #134 (ADR-MULTI-001: Multi-NIC Support)
- **Relates to**: #90 (ADR-ARCH-001: NDIS 6.0 Framework)

---

## Module Structure

```
IntelAvbFilter/
│
├── 05-implementation/
│   ├── src/
│   │   ├── ndis/                    [NDIS Integration Layer]
│   │   │   ├── filter.c             - FilterAttach/Detach, entry points
│   │   │   ├── filter_send.c        - FilterSend/FilterSendNetBufferLists
│   │   │   ├── filter_receive.c     - FilterReceive callbacks
│   │   │   └── filter_ioctl.c       - IRP_MJ_DEVICE_CONTROL handler
│   │   │
│   │   ├── hal/                     [Hardware Abstraction Layer]
│   │   │   ├── device_abstraction.c - Strategy pattern interface
│   │   │   ├── i210_hal.c           - Intel I210 implementation
│   │   │   ├── i225_hal.c           - Intel I225 implementation  
│   │   │   ├── i226_hal.c           - Intel I226 implementation
│   │   │   └── bar0_access.c        - Direct MMIO register access
│   │   │
│   │   ├── events/                  [Event Subsystem]
│   │   │   ├── event_dispatcher.c   - Observer pattern mediator
│   │   │   ├── ring_buffer.c        - Lock-free circular buffer
│   │   │   ├── ptp_observer.c       - PTP timestamp event handler
│   │   │   └── avtp_observer.c      - AVTP diagnostic observer
│   │   │
│   │   ├── ptp/                     [PTP Clock Subsystem]
│   │   │   ├── ptp_clock.c          - PHC get/set/adjust operations
│   │   │   ├── ptp_timestamp.c      - TX/RX timestamp extraction
│   │   │   └── ptp_interrupt.c      - Target time interrupt handling
│   │   │
│   │   ├── tsn/                     [TSN Feature Implementations]
│   │   │   ├── qav_cbs.c            - Credit-Based Shaper (Qav)
│   │   │   ├── qbv_tas.c            - Time-Aware Scheduler (Qbv)
│   │   │   ├── frame_preemption.c   - Frame Preemption (802.3br)
│   │   │   └── stream_id.c          - Stream identification (802.1CB)
│   │   │
│   │   ├── diagnostics/             [Error Handling & Logging]
│   │   │   ├── error_reporting.c    - Unified error subsystem
│   │   │   ├── event_log.c          - Windows Event Log integration
│   │   │   └── debug_trace.c        - WPP tracing (debug builds)
│   │   │
│   │   └── utils/                   [Shared Utilities]
│   │       ├── memory.c             - Pool allocation wrappers
│   │       ├── synchronization.c    - Spinlock helpers
│   │       └── conversion.c         - Data format conversions
│   │
│   ├── include/
│   │   ├── public/                  [Public Headers - Exported to User-Mode]
│   │   │   ├── avb_ioctl.h          - IOCTL definitions and structures
│   │   │   ├── ptp_types.h          - PTP timestamp structures
│   │   │   └── tsn_config.h         - TSN configuration structures
│   │   │
│   │   └── internal/                [Internal Headers - Driver Only]
│   │       ├── device_context.h     - Per-adapter state
│   │       ├── hal_interface.h      - HAL function pointers
│   │       ├── event_types.h        - Event subsystem types
│   │       └── register_defs.h      - Generated register definitions
│   │
│   └── tests/
│       ├── unit/                    [Unit Tests - CTest Framework]
│       │   ├── test_ring_buffer.c   - Ring buffer correctness
│       │   ├── test_hal.c           - HAL abstraction tests
│       │   └── test_observers.c     - Observer pattern tests
│       │
│       └── integration/             [Integration Tests]
│           ├── test_ioctl.c         - IOCTL end-to-end tests
│           └── test_timestamps.c    - PTP timestamp accuracy
│
├── intel-ethernet-regs/             [Submodule - Register Definitions]
│   ├── devices/
│   │   ├── i210_v3_7.yaml           - I210 register map v3.7
│   │   ├── i225.yaml                - I225 register map
│   │   └── i226.yaml                - I226 register map
│   │
│   ├── tools/
│   │   └── reggen.py                - YAML → C header generator
│   │
│   └── gen/                         [Generated Headers]
│       ├── i210_regs.h
│       ├── i225_regs.h
│       └── i226_regs.h
│
└── external/                        [External Dependencies]
    └── intel_avb/                   [Submodule - Intel AVB Library]
        └── lib/
            ├── avb_log.h            - Logging macros
            └── avb_types.h          - AVB data types
```

---

## Module Dependency Graph

```
┌─────────────────────────────────────────────────────────────┐
│                     filter.c (NDIS Entry)                   │
└──────────────┬────────────────────┬─────────────────────────┘
               │                    │
       ┌───────▼────────┐   ┌──────▼──────┐
       │ filter_ioctl.c │   │filter_send.c│
       └───────┬────────┘   └──────┬──────┘
               │                    │
               └────────┬───────────┘
                        │
          ┌─────────────▼─────────────┐
          │   device_abstraction.c    │ ◄─── HAL Interface
          │   (Strategy Pattern)      │
          └─────────────┬─────────────┘
                        │
          ┌─────────────┼─────────────┐
          │             │             │
  ┌───────▼──────┐ ┌───▼──────┐ ┌───▼──────┐
  │  i210_hal.c  │ │i225_hal.c│ │i226_hal.c│
  └───────┬──────┘ └───┬──────┘ └───┬──────┘
          │            │            │
          └────────────┼────────────┘
                       │
              ┌────────▼────────┐
              │ bar0_access.c   │
              │ (MMIO Registers)│
              └────────┬────────┘
                       │
              ┌────────▼────────┐
              │ Generated Regs  │
              │ (i2XX_regs.h)   │
              └─────────────────┘


┌─────────────────────────────────────────────────────────────┐
│              event_dispatcher.c (Observer Pattern)           │
└──────────────┬────────────────────┬─────────────────────────┘
               │                    │
       ┌───────▼────────┐   ┌──────▼──────┐
       │ptp_observer.c  │   │avtp_observer│
       └───────┬────────┘   └──────┬──────┘
               │                    │
               └────────┬───────────┘
                        │
              ┌─────────▼─────────┐
              │  ring_buffer.c    │
              │  (Lock-Free)      │
              └───────────────────┘
```

**Dependency Rules**:
1. NDIS layer depends on HAL (not vice versa)
2. HAL implementations depend on generated register headers
3. Event subsystem is independent (can be tested in isolation)
4. No circular dependencies allowed (enforced by linker)

---

## Compilation Units and Link Order

### Static Libraries (.lib)

```makefile
# HAL Library (device abstraction + implementations)
hal.lib:
    - device_abstraction.c
    - i210_hal.c
    - i225_hal.c
    - i226_hal.c
    - bar0_access.c

# Event Subsystem Library
events.lib:
    - event_dispatcher.c
    - ring_buffer.c
    - ptp_observer.c
    - avtp_observer.c

# PTP Clock Library
ptp.lib:
    - ptp_clock.c
    - ptp_timestamp.c
    - ptp_interrupt.c

# TSN Features Library
tsn.lib:
    - qav_cbs.c
    - qbv_tas.c
    - frame_preemption.c
    - stream_id.c

# Diagnostics Library
diagnostics.lib:
    - error_reporting.c
    - event_log.c
    - debug_trace.c (DBG only)

# Utilities Library
utils.lib:
    - memory.c
    - synchronization.c
    - conversion.c
```

### Driver Binary (.sys)

```makefile
IntelAvbFilter.sys:
    # NDIS entry points (must be first for DriverEntry)
    - filter.c
    - filter_ioctl.c
    - filter_send.c
    - filter_receive.c
    
    # Link static libraries
    + hal.lib
    + events.lib
    + ptp.lib
    + tsn.lib
    + diagnostics.lib
    + utils.lib
    
    # External dependencies
    + ndis.lib          # NDIS 6.0 import library
    + ntoskrnl.lib      # NT kernel functions
    + hal.lib (WDK)     # Hardware Abstraction Layer
```

**Link Order**: NDIS → HAL → Events → PTP → TSN → Diagnostics → Utils → System Libs

---

## Build Configuration Matrix

| Configuration | Defines | Optimizations | Debug Info | Use Case |
|---------------|---------|---------------|------------|----------|
| **Debug (Checked)** | `DBG=1`, `_DEBUG` | `/Od` (none) | Full PDB | Development, debugging |
| **Release (Free)** | `NDEBUG` | `/O2` (max speed) | Minimal | Production deployment |
| **Test** | `DBG=1`, `UNIT_TEST` | `/O1` (balanced) | Full PDB | Automated testing |
| **Instrumentation** | `DBG=1`, `WPP_TRACING` | `/O2` | Full PDB | Performance profiling |

**Preprocessor Macros**:
```c
#if DBG
    #define ASSERT(x) if (!(x)) KeBugCheckEx(...)
    #define DBG_PRINT(x) DbgPrint x
#else
    #define ASSERT(x)
    #define DBG_PRINT(x)
#endif

#ifdef UNIT_TEST
    #define STATIC        // Make static functions testable
    #define INLINE
#else
    #define STATIC static
    #define INLINE __forceinline
#endif
```

---

## Code Organization Principles

### 1. Layered Architecture (Top → Bottom)

```
┌──────────────────────────────────┐
│     NDIS Integration Layer       │  ← OS Interface
├──────────────────────────────────┤
│   Application Logic (PTP/TSN)    │  ← Business Logic
├──────────────────────────────────┤
│  Hardware Abstraction Layer (HAL)│  ← Device Interface
├──────────────────────────────────┤
│     Generated Register Defs      │  ← Hardware Specs
└──────────────────────────────────┘
```

**Rule**: Upper layers depend on lower layers only (no reverse dependencies)

### 2. Interface Segregation

Each module exposes minimal public interface:
```c
// device_abstraction.h (public interface)
typedef struct _DEVICE_OPS {
    NTSTATUS (*GetPtpTime)(DEVICE_CONTEXT*, PTP_TIME*);
    NTSTATUS (*SetPtpTime)(DEVICE_CONTEXT*, PTP_TIME*);
    // ... only essential operations
} DEVICE_OPS;

// i210_hal.c (private implementation)
static NTSTATUS I210_GetPtpTime(...);  // Not exposed
static NTSTATUS I210_SetPtpTime(...);
```

### 3. Single Responsibility

Each `.c` file has one clear purpose:
- `filter.c`: NDIS lifecycle (attach/detach/pause/restart)
- `filter_ioctl.c`: IOCTL dispatch **only**
- `ring_buffer.c`: Ring buffer data structure **only**

**Anti-pattern**: `utils.c` with unrelated helper functions ❌

---

## Build Automation

### Register Header Generation

```powershell
# Generate headers from YAML before compilation
py -3 intel-ethernet-regs/tools/reggen.py `
    intel-ethernet-regs/devices/i225.yaml `
    intel-ethernet-regs/gen

# Output: intel-ethernet-regs/gen/i225_regs.h
```

### MSBuild Integration

```xml
<Target Name="GenerateRegisterHeaders" BeforeTargets="ClCompile">
  <Exec Command="py -3 intel-ethernet-regs\tools\reggen.py ..." />
</Target>
```

### CI/CD Pipeline (GitHub Actions)

```yaml
- name: Generate Register Headers
  run: |
    Get-ChildItem intel-ethernet-regs/devices -Filter *.yaml | ForEach-Object {
      py -3 intel-ethernet-regs/tools/reggen.py $_.FullName intel-ethernet-regs/gen
    }

- name: Build Driver (Release)
  run: msbuild IntelAvbFilter.sln /p:Configuration=Release /p:Platform=x64

- name: Run Unit Tests
  run: ctest --test-dir build/tests --output-on-failure
```

---

## Development Workflow

### 1. Feature Development

```
1. Create feature branch: git checkout -b feature/qbv-tas
2. Implement in isolated module: tsn/qbv_tas.c
3. Write unit tests: tests/unit/test_qbv_tas.c
4. Build & test locally: msbuild + ctest
5. Open PR with traceability: Implements #9 (REQ-F-TAS-001)
6. Code review (2 approvers required)
7. Merge to master (squash commits)
```

### 2. Bug Fixes

```
1. Reproduce issue locally with unit test
2. Implement fix in minimal scope
3. Verify fix with regression test
4. Document root cause in commit message
5. Link to GitHub issue: Fixes #123
```

### 3. Submodule Updates

```
# Update intel-ethernet-regs submodule
cd intel-ethernet-regs
git pull origin main
git checkout <specific-commit>  # Pin to stable version
cd ..
git add intel-ethernet-regs
git commit -m "Update intel-ethernet-regs to v2.1.0"
```

---

## Testing Strategy

### Unit Testing (Per Module)

```c
// tests/unit/test_ring_buffer.c
void test_RingBuffer_Write_Success() {
    RING_BUFFER rb;
    RingBuffer_Init(&rb, 16);
    
    EVENT event = {0};
    ASSERT_TRUE(RingBuffer_Write(&rb, &event));
    ASSERT_EQUAL(1, RingBuffer_Count(&rb));
}

void test_RingBuffer_Overflow() {
    // ... test overflow handling
}
```

### Integration Testing

```c
// tests/integration/test_ioctl.c
void test_IOCTL_GET_CLOCK_TIME_E2E() {
    // Open device handle
    // Send IOCTL_AVB_GET_CLOCK_TIME
    // Verify timestamp format and range
    // Close handle
}
```

### Coverage Requirements

- **Unit tests**: ≥80% line coverage (measured with OpenCppCoverage)
- **Integration tests**: All IOCTLs covered with positive & negative cases
- **Stress tests**: 1M events/sec for 1 hour (no crashes/leaks)

---

## Traceability

### Module → Requirement Mapping

| Module | Requirements Satisfied |
|--------|------------------------|
| `filter_ioctl.c` | #2-#13 (All IOCTL requirements) |
| `i210_hal.c` | #44 (Hardware Detection), #77 (Firmware) |
| `ring_buffer.c` | #19 (Shared Memory Ring Buffer) |
| `ptp_clock.c` | #34, #38, #39 (PHC operations) |
| `qav_cbs.c` | #49 (Credit-Based Shaper) |
| `qbv_tas.c` | #50 (Time-Aware Scheduler) |

### Build Artifacts → Design Traceability

| Artifact | Design Documents |
|----------|------------------|
| `IntelAvbFilter.sys` | [Logical View](logical-view-event-subsystem.md), [Process View](process-view-event-flow.md) |
| `hal.lib` | ADR #92 (Strategy Pattern), ADR #126 (HAL) |
| `events.lib` | ADR #93 (Ring Buffer), ADR #147 (Observer) |

---

## Validation Criteria

- ✅ No circular dependencies (verified with dependency analyzer)
- ✅ All modules buildable in isolation (verified with incremental builds)
- ✅ Clean separation between NDIS/HAL/Events (verified with code review)
- ✅ Generated headers up-to-date (verified in CI)

---

## References

- ISO/IEC/IEEE 42010:2011 - Development View Requirements
- Windows Driver Kit (WDK) - Module Organization Best Practices
- ADR #92 (Strategy Pattern for HAL)
- ADR #126 (Hardware Abstraction Layer)
