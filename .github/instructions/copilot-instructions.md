applyTo:
 - "**" 
```

# Intel AVB Filter Driver - AI Coding Instructions

## Working principles
-- ensure you understand the architecture and patterns before coding
-- avoid redundancy, reuse existing functions and patterns (check existing code first)
-- Hardware-first policy: No Fake, No Stubs, no Simulations in production paths
-- Optional DEV_SIMULATION feature flag may be used for developer-only fallback paths (must be guarded by AVB_DEV_SIMULATION)
-- no implementation based assumptions, use specification or analysis results (ask if required)
-- no assumptions and no false advertising, investigate, prove and ensure correctness (e.g. if you assume that a function, struct or whatever is missing: check code first, and see if it is really missing before adding a new function/struct - that would prevent redundant implementation and compile errors due to that)
-- always use real hardware access patterns
-- use Intel hardware specifications for register access (SSOT headers + datasheets) , in case we have evidence of additional/different registeraccess pattterns these are allowed as well - but requires explanation reference to evidence.
-- follow existing code patterns and architecture
-- code needs to compile before commit, no broken code
-- Always reference the exact Intel datasheet section or spec version when implementing register access.
-- Validate all hardware reads/writes with range checks or masks from the specification (SSOT masks where available).
-- Every function must have a Doxygen comment explaining purpose, parameters, return values, and hardware context.
-- no duplicate or redundant implementations to avoid inconsistencies and confusion; use centralized, reusable functions instead
-- no ad-hoc file copies (e.g., *_fixed, *_new, *_correct); refactor in place step-by-step to avoid breakage
-- use descriptive naming patterns consistent with existing codebase for files, functions, variables, and types
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
- **Device Detection**: Successfully identifies Intel controllers with realistic capability reporting
- **IOCTL Interface**: Full user-mode API implementation with enhanced TSN support
- **Intel Library Integration**: Complete API compatibility layer with device-specific implementations
- **TSN Framework**: Advanced logic for Time-Aware Shaper and Frame Preemption on supported hardware
- **Build System**: Successfully compiles for x64/ARM64
- **BAR0 Hardware Discovery**: Microsoft NDIS patterns for PCI resource enumeration
- **Real Hardware Access**: `MmMapIoSpace()` and `READ_REGISTER_ULONG()` implementation
- **Clean Device Separation**: Device-specific implementations isolated in `devices/` directory
- **Multi-Adapter Support**: Context switching between different Intel controllers
- **Realistic Hardware Capabilities**: Accurate capability reporting based on actual hardware specs

### What Needs Hardware Validation ⚠️
- **Hardware Register Access**: Implementation complete, needs testing on real Intel controllers
- **Production Timing Accuracy**: IEEE 1588 hardware validation required
- **TSN Feature Testing**: I225/I226 advanced features need hardware validation
- **Multi-Device Context Switching**: Needs validation with real multi-adapter systems

## Essential Patterns

### DEV_SIMULATION flag
- Developer-only fallback logic must be guarded by `#ifdef AVB_DEV_SIMULATION` and off by default.
- Production builds must not enable AVB_DEV_SIMULATION.

### Clean Device Separation ✅ IMPLEMENTED
**CRITICAL**: Generic integration layer must NOT contain device-specific register definitions:

```c
// ❌ WRONG - Device-specific registers in generic layer
#include "intel-ethernet-regs/gen/i210_regs.h"
if (intel_read_reg(&dev, I210_CTRL, &value) != 0) { ... }

// ✅ CORRECT - Generic registers only
#define INTEL_GENERIC_CTRL_REG  0x00000  // Common to all Intel devices
if (intel_read_reg(&dev, INTEL_GENERIC_CTRL_REG, &value) != 0) { ... }
```

**Architecture Rules**:
- Generic layer (`avb_integration.c`): Only generic/common Intel register offsets
- Device-specific layer (`devices/intel_*_impl.c`): Device-specific registers and logic
- Intel library handles device-specific routing via device operations structure

### Hardware Capability Reality ✅ IMPLEMENTED
**CRITICAL**: Capability assignment must match actual hardware specifications:

```c
// ✅ CORRECT - Realistic capability assignment
switch (device_type) {
    case INTEL_DEVICE_82575:  // 2008 - No PTP
        baseline_caps = INTEL_CAP_MMIO | INTEL_CAP_MDIO;
        break;
    case INTEL_DEVICE_I210:   // 2013 - PTP, No TSN
        baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO;
        break;
    case INTEL_DEVICE_I226:   // 2020 - Full TSN
        baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | 
                       INTEL_CAP_TSN_TAS | INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | 
                       INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE;
        break;
}
```

**Timeline Reality**:
- TSN Standard: IEEE 802.1Qbv (2015), IEEE 802.1Qbu (2016)
- First Intel TSN: I225 (2019)
- Never claim TSN on pre-2019 Intel hardware

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
1. Validate buffer sizes and device capabilities
2. Use active context (from `IOCTL_AVB_OPEN_ADAPTER`)
3. Call Intel library hw function via device operations
4. Set request status and information with proper error codes

### Multi-Adapter Context Management ✅ IMPLEMENTED
```c
// Context switching pattern
case IOCTL_AVB_OPEN_ADAPTER:
    // Find target adapter by VID/DID
    // Switch global context (g_AvbContext)
    // Update Intel library device structure
    // Ensure hardware initialization for target
```

### Debug Output Conventions ✅
- Entry/Exit tracing with device identification
- Error reporting with explicit reasons and error codes
- Hardware ops clearly marked with device context
- Multi-adapter operations show VID/DID for clarity

## Build Requirements

### Prerequisites
- Visual Studio 2019 or later with Windows Driver Kit (WDK)
- Windows SDK 10.0.22000.0 or later
- Git (for submodule management)

### Target Platforms
- **Primary**: Windows 10/11 x64
- **Secondary**: Windows 10/11 ARM64
- **NDIS Version**: 6.30+ (Windows 8.1+)

### Dependencies
- Windows NDIS library (kernel-mode)
- Intel AVB external library (integrated as git submodule)

## Device Integration Patterns ✅

### Device Registry Pattern
All supported Intel devices are registered in `devices/intel_device_registry.c`:
```c
// Device identification and operations routing
const intel_device_ops_t* intel_get_device_ops(intel_device_type_t device_type) {
    switch (device_type) {
        case INTEL_DEVICE_I210: return &i210_ops;
        case INTEL_DEVICE_I226: return &i226_ops;
        // ...
        default: return NULL;
    }
}
```

### Device Implementation Pattern
Each device family has isolated implementation in `devices/intel_*_impl.c`:
- Honest capability reporting (no false advertising)
- Device-specific register access only
- Intel library integration via operations structure

## External Dependencies ✅

### Intel AVB Library Integration
Located in `external/intel_avb/lib/` - provides cross-platform hardware abstraction.

**Key Integration Points**:
- `intel_init()` - Device initialization
- `intel_read_reg()` / `intel_write_reg()` - Register access
- `intel_setup_time_aware_shaper()` - TSN configuration
- `intel_get_device_info()` - Device identification

### Register Definitions
Uses auto-generated SSOT headers from Intel specifications in `intel-ethernet-regs/gen/`.

## Code Quality Requirements

### Documentation
- Every public function requires Doxygen comments
- Include hardware context and Intel datasheet references
- Document capability requirements and error conditions

### Error Handling
- Use proper NTSTATUS codes for kernel functions
- Provide specific error reasons in debug output
- No silent failures - always log errors with context

### Testing
- All changes must compile successfully
- Hardware validation required for register access changes
- Multi-adapter scenarios must be tested when available
