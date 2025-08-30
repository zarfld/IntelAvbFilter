# Intel AVB Filter Driver - AI Coding Instructions

## Working principles
-- ensure you understand the architecture and patterns before coding
-- avvoid redundandcy, reuse existing functions and patterns (check existing code first)
-- Hardware-first policy: No Fake, No Stubs, no Simulations in production paths
-- Optional DEV_SIMULATION feature flag may be used for developer-only fallback paths (must be guarded by AVB_DEV_SIMULATION)
-- no implementation based assumtions, use specification or analysis results (ask if required)
-- no assumtions and no false advertising, investigate, prove and ensure correctness (e.g. if you assume that a function, struct or whatever is missing: check code first, and see if it is really missing before adding a new function/struct - that would prevent redundand implementation and compile errors due to that)
-- always use real hardware access patterns
-- use Intel hardware specifications for register access (SSOT headers + datasheets)
-- code needs to compile before commit, no broken code
-- Always reference the exact Intel datasheet section or spec version when implementing register access.
-- Validate all hardware reads/writes with range checks or masks from the specification (SSOT masks where available).
-- Every function must have a Doxygen comment explaining purpose, parameters, return values, and hardware context.
-- no duplicate or redundant implementations to avoid inconsistencies and confusion; use centralized, reusable functions instead
-- no ad-hoc file copies (e.g., *_fixed, *_new, *_correct); refactor in place step-by-step to avoid breakage
-- Clean submit rules:
   - each commit compiles and passes checks
   - small, single-purpose, reviewable diffs (no WIP noise)
   - no dead or commented-out code; remove unused files
   - run formatter and static analysis before commit
   - update docs/tests and reference the spec/issue in the message
   - use feature flags or compatibility layers when incremental changes risk breakage
-- Avoid unnecessary duplication. Duplication is acceptable when it improves clarity, isolates modules, or is required for performance.
-- Avoid code that is difficult to understand. Prefer clear naming and structure over excessive comments or unnecessary helper variables.
-- Avoid unnecessary complexity. Keep required abstractions for maintainability, testability, or hardware safety
-- Design modules so that changes in one module do not require changes in unrelated modules. Avoid dependencies that cause single changes to break multiple areas.
-- Design components for reuse where practical, but prioritize correctness and domain fit over forced generalization.
-- Prefer incremental modification of existing code over reimplementation; adapt existing functions instead of creating redundant new ones
-- NOT SUPPORTED - Intel device but no AVB/TSN support: Intel(R) 82574L

## Architecture Overview

This is a **Windows NDIS 6.30 lightweight filter driver** that bridges AVB/TSN capabilities between user applications and Intel Ethernet hardware. The key architectural insight is the **three-layer abstraction**:

1. **NDIS Filter Layer** (`filter.c`, `device.c`) - Windows kernel driver that attaches to Intel adapters
2. **AVB Integration Layer** (`avb_integration.c/.h`) - Hardware abstraction bridge with IOCTL interface  
3. **Intel AVB Library** (`external/intel_avb/lib/`) - Cross-platform hardware access library

**Critical Data Flow**: User App → DeviceIoControl → NDIS Filter → Platform Operations → Intel Library → Hardware

## ✅ CURRENT IMPLEMENTATION STATUS - JANUARY 2025

### What Actually Works ✅
- **NDIS Filter Infrastructure**: Complete and functional
- **Device Detection**: Successfully identifies Intel controllers
- **IOCTL Interface**: Full user-mode API implementation
- **Intel Library Integration**: Complete API compatibility layer
- **TSN Framework**: Advanced logic for Time-Aware Shaper and Frame Preemption
- **Build System**: Successfully compiles for x64/ARM64
- **BAR0 Hardware Discovery**: Microsoft NDIS patterns for PCI resource enumeration (**NEW!**)
- **Real Hardware Access**: `MmMapIoSpace()` and `READ_REGISTER_ULONG()` implementation (**NEW!**)

### What Needs Hardware Validation ⚠️
- **Hardware Register Access**: Implementation complete, needs testing on real Intel controllers
- **Production Timing Accuracy**: IEEE 1588 hardware validation required
- **TSN Feature Testing**: I225/I226 advanced features need hardware validation
- **I217 Support**: Missing from device identification despite being implemented

## Essential Patterns

### DEV_SIMULATION flag
- Developer-only fallback logic must be guarded by `#ifdef AVB_DEV_SIMULATION` and off by default.
- Production builds must not enable AVB_DEV_SIMULATION.

### Hardware Access Abstraction ✅ REAL HARDWARE
The core pattern is **platform operations structure**, centralized in `external/intel_avb/lib/intel_windows.h`:
```c
struct platform_ops {
  int (*init)(device_t *dev);
  int (*pci_read_config)(device_t *dev, uint32_t offset, uint32_t *value);
  int (*mmio_read)(device_t *dev, uint32_t offset, uint32_t *value);
  int (*mmio_write)(device_t *dev, uint32_t offset, uint32_t value);
  int (*mdio_read)(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value);
  int (*mdio_write)(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value);
  int (*read_timestamp)(device_t *dev, uint64_t *timestamp);
};
```

### Real Hardware Access Pattern ✅ IMPLEMENTED
Use `MmMapIoSpace()` + `READ_REGISTER_ULONG()` and SSOT register headers for offsets/masks.

### IOCTL Processing Pattern ✅ WORKING
1. Validate buffer sizes
2. Call Intel library hw function
3. Set request status and information

### Debug Output Conventions ✅
- Entry/Exit tracing
- Error reporting with explicit reasons
- Hardware ops clearly marked

## Build Requirements
[unchanged]

## Device Integration Patterns ✅
[unchanged]

## External Dependencies ✅
[unchanged]
