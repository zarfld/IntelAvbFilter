# ADR-SEC-001: Debug vs Release Security Boundaries (NDEBUG Guards)

**Status**: Accepted  
**Date**: 2025-12-09  
**GitHub Issue**: TBD (to be created)  
**Phase**: Phase 03 - Architecture Design  
**Priority**: Critical (P0)

---

## Context

The IntelAvbFilter driver provides raw register access IOCTLs (`IOCTL_AVB_READ_REGISTER` and `IOCTL_AVB_WRITE_REGISTER`) for diagnostics and testing. These IOCTLs allow direct read/write to hardware MMIO registers, bypassing all validation and abstractions.

**Security Risk**: Raw register access in production builds:
- Violates **principle of least privilege**
- Exposes dangerous methods (CWE-749)
- Increases kernel attack surface
- Enables unauthorized hardware manipulation

**Problem**: How to balance diagnostic flexibility in development with security in production?

---

## Decision

**Use `#ifndef NDEBUG` preprocessor guards to conditionally compile dangerous IOCTLs**, ensuring they exist only in Debug builds and are completely removed from Release builds.

### Architecture: Compile-Time Security Boundaries

```
┌─────────────────────────────────────────────────────────────┐
│  Debug Build (NDEBUG not defined)                           │
│  - IOCTL_AVB_READ_REGISTER ✅ Available                     │
│  - IOCTL_AVB_WRITE_REGISTER ✅ Available                    │
│  - Production IOCTLs ✅ Available                           │
│  - Use Case: Development, testing, diagnostics              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  Release Build (NDEBUG defined)                              │
│  - IOCTL_AVB_READ_REGISTER ❌ Removed (compile error)       │
│  - IOCTL_AVB_WRITE_REGISTER ❌ Removed (compile error)      │
│  - Production IOCTLs ✅ Available                           │
│  - Use Case: Production deployments                          │
└─────────────────────────────────────────────────────────────┘
```

### Rationale

1. **Security by Design**: Attack surface reduced at compile time (not runtime)
   - No runtime checks (zero performance overhead)
   - Impossible to bypass (code doesn't exist in binary)

2. **Principle of Least Privilege**: Release builds expose only necessary IOCTLs
   - Raw register access removed
   - Production abstractions enforce validated access patterns

3. **Developer Productivity**: Debug builds retain diagnostic flexibility
   - Full hardware access for troubleshooting
   - Direct register reads for validation

4. **Industry Standard**: `NDEBUG` is C/C++ standard practice
   - Automatically defined by Visual Studio in Release configuration
   - Well-understood by developers

---

## Alternatives Considered

### Alternative 1: Runtime Checks (Registry Key)

**Implementation**: Check registry key to enable raw register access

```c
if (RegistryKeyEnabled("DebugMode")) {
    // Allow raw register access
}
```

**Pros**:
- Can enable in production for emergency diagnostics

**Cons**:
- ❌ Code still present in Release binary (attack surface remains)
- ❌ Registry key can be manipulated by attackers
- ❌ Runtime overhead (registry read per IOCTL)
- ❌ Violates "No Shortcuts" principle (code exists = exploitable)

**Decision**: **Rejected** - Security must be compile-time guaranteed

### Alternative 2: Separate Debug Driver Binary

**Implementation**: Ship two drivers (Debug and Release)

**Pros**:
- Clear separation
- No preprocessor conditionals

**Cons**:
- ❌ Confusion for users (which driver to install?)
- ❌ Increased testing burden (2 binaries)
- ❌ Driver signing complexity (2 certificates)
- ❌ Distribution complexity

**Decision**: **Rejected** - Operational complexity too high

### Alternative 3: Admin-Only Access Control

**Implementation**: Check caller privileges at runtime

```c
if (!IsUserAdmin()) {
    return STATUS_ACCESS_DENIED;
}
```

**Pros**:
- Restricts access to administrators

**Cons**:
- ❌ Code still present in Release binary
- ❌ Admin accounts can be compromised
- ❌ Privilege escalation exploits bypass this
- ❌ Runtime overhead

**Decision**: **Rejected** - Does not eliminate attack surface

---

## Consequences

### Positive
- ✅ **Zero Runtime Overhead**: No performance cost for security
- ✅ **Compile-Time Guarantee**: Impossible to access raw registers in Release
- ✅ **Attack Surface Reduction**: 2 dangerous IOCTLs removed from production
- ✅ **Clear Separation**: Debug diagnostics vs. production API boundaries obvious
- ✅ **Industry Standard**: Aligns with C/C++ best practices

### Negative
- ❌ **Breaking Change**: Existing tests using raw register access fail in Release
- ❌ **Migration Effort**: 2-4 hours per test file to convert to production IOCTLs
- ❌ **Emergency Diagnostics**: No raw register access in production (must use Debug build)

### Risks
- **Test Code Migration**: Existing diagnostic tests must be rewritten
  - **Mitigation**: Provide migration guide (`docs/SECURITY_IOCTL_RESTRICTION.md`)
  - **Mitigation**: Create production-safe equivalents for common patterns
- **Production Debugging**: Cannot use raw register access for production issues
  - **Mitigation**: Production IOCTLs cover all necessary use cases
  - **Mitigation**: Use Debug build on isolated test systems if needed

---

## Implementation

### Files Requiring NDEBUG Guards

**1. IOCTL Definitions** (`include/avb_ioctl.h` only! duplicate `external/intel_avb/include/avb_ioctl.h` is depricated!):

```c
/* Production IOCTLs - ALWAYS available */
#define IOCTL_AVB_ADJUST_FREQUENCY      _NDIS_CONTROL_CODE(38, METHOD_BUFFERED)
#define IOCTL_AVB_GET_CLOCK_CONFIG      _NDIS_CONTROL_CODE(45, METHOD_BUFFERED)
#define IOCTL_AVB_GET_TIMESTAMP         _NDIS_CONTROL_CODE(34, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TIMESTAMP         _NDIS_CONTROL_CODE(35, METHOD_BUFFERED)

#ifndef NDEBUG
/* ============================================================================
 * DEBUG-ONLY IOCTLs
 * ============================================================================
 * These IOCTLs provide raw register access for diagnostics and testing.
 * They are DISABLED in Release builds for security (principle of least privilege).
 * 
 * SECURITY WARNING: Do NOT remove NDEBUG guards!
 * - Raw register access bypasses all validation
 * - Can corrupt hardware state if misused
 * - Exposes attack surface in production
 * 
 * Production Alternative:
 * - Use IOCTL_AVB_ADJUST_FREQUENCY instead of raw TIMINCA writes
 * - Use IOCTL_AVB_GET_CLOCK_CONFIG instead of raw register reads
 * ============================================================================ */

#define IOCTL_AVB_READ_REGISTER         _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_WRITE_REGISTER        _NDIS_CONTROL_CODE(23, METHOD_BUFFERED)

typedef struct AVB_REGISTER_REQUEST {
    avb_u32 offset;  /* MMIO offset (e.g., 0x0B608 for TIMINCA) */
    avb_u32 value;   /* Input for WRITE, output for READ */
    avb_u32 status;  /* NDIS_STATUS result code */
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

#endif /* NDEBUG */
```

**2. IOCTL Handlers** (`avb_integration_fixed.c`):

```c
#ifndef NDEBUG
/* Debug-only handlers for raw register access */

NTSTATUS AvbHandleReadRegister(
    AVB_DEVICE_CONTEXT *DeviceContext,
    PAVB_REGISTER_REQUEST Request
)
{
    if (!Request || !DeviceContext->MmioBase) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Bounds check: Ensure offset is within BAR0 range
    if (Request->offset >= DeviceContext->MmioLength) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    Request->value = AvbReadRegister32(DeviceContext->MmioBase, Request->offset);
    Request->status = STATUS_SUCCESS;
    
    KdPrint(("AvbHandleReadRegister: offset=0x%X value=0x%X\n", 
             Request->offset, Request->value));
    
    return STATUS_SUCCESS;
}

NTSTATUS AvbHandleWriteRegister(
    AVB_DEVICE_CONTEXT *DeviceContext,
    PAVB_REGISTER_REQUEST Request
)
{
    if (!Request || !DeviceContext->MmioBase) {
        return STATUS_INVALID_PARAMETER;
    }
    
    if (Request->offset >= DeviceContext->MmioLength) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    AvbWriteRegister32(DeviceContext->MmioBase, Request->offset, Request->value);
    Request->status = STATUS_SUCCESS;
    
    KdPrint(("AvbHandleWriteRegister: offset=0x%X value=0x%X\n", 
             Request->offset, Request->value));
    
    return STATUS_SUCCESS;
}

#endif /* NDEBUG */
```

**3. IOCTL Dispatch** (`device.c`):

```c
NTSTATUS FilterDeviceControl(PDEVICE_CONTEXT DeviceContext, PIRP Irp)
{
    ULONG controlCode = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.IoControlCode;
    
    switch (controlCode) {
        // Production IOCTLs (always available)
        case IOCTL_AVB_ADJUST_FREQUENCY:
            return AvbHandleAdjustFrequency(DeviceContext, Irp);
            
        case IOCTL_AVB_GET_CLOCK_CONFIG:
            return AvbHandleGetClockConfig(DeviceContext, Irp);
            
#ifndef NDEBUG
        // Debug-only IOCTLs
        case IOCTL_AVB_READ_REGISTER:
            return AvbHandleReadRegister(DeviceContext, (PAVB_REGISTER_REQUEST)Irp->AssociatedIrp.SystemBuffer);
            
        case IOCTL_AVB_WRITE_REGISTER:
            return AvbHandleWriteRegister(DeviceContext, (PAVB_REGISTER_REQUEST)Irp->AssociatedIrp.SystemBuffer);
#endif
            
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}
```

---

## Production IOCTL Alternatives

### Raw TIMINCA Write → IOCTL_AVB_ADJUST_FREQUENCY

**Old (Debug Only)**:
```c
AVB_REGISTER_REQUEST req;
req.offset = 0x0B608;  // TIMINCA register
req.value = 0x08000000 | freq_adjustment;
DeviceIoControl(hDevice, IOCTL_AVB_WRITE_REGISTER, &req, ...);
```

**New (Production Safe)**:
```c
AVB_ADJUST_FREQUENCY_REQUEST req;
req.ppb_adjustment = ppb;  // Parts per billion
DeviceIoControl(hDevice, IOCTL_AVB_ADJUST_FREQUENCY, &req, ...);
```

### Raw Register Read → IOCTL_AVB_GET_CLOCK_CONFIG

**Old (Debug Only)**:
```c
AVB_REGISTER_REQUEST req;
req.offset = 0x0B608;  // TIMINCA
DeviceIoControl(hDevice, IOCTL_AVB_READ_REGISTER, &req, ...);
ULONG timinca = req.value;
```

**New (Production Safe)**:
```c
AVB_CLOCK_CONFIG config;
DeviceIoControl(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG, NULL, 0, &config, sizeof(config), ...);
ULONG timinca = config.timinca_value;
```

---

## Build Configuration

### Debug Build (Visual Studio)
- **NDEBUG**: Not defined
- **Optimization**: Disabled (`/Od`)
- **Runtime Checks**: Enabled (`/RTC1`)
- **Debug Info**: Full (`/Zi`)

### Release Build (Visual Studio)
- **NDEBUG**: Defined automatically (`/D NDEBUG`)
- **Optimization**: Maximum (`/O2`)
- **Runtime Checks**: Disabled
- **Debug Info**: PDB only (`/Zi`)

---

## Testing Strategy

### Test-1: Verify Debug Build Includes Raw Register Access
```powershell
msbuild IntelAvbFilter.sln /p:Configuration=Debug /p:Platform=x64
# Verify IOCTL_AVB_READ_REGISTER defined in compiled binary
```

### Test-2: Verify Release Build Excludes Raw Register Access
```powershell
msbuild IntelAvbFilter.sln /p:Configuration=Release /p:Platform=x64
# Verify IOCTL_AVB_READ_REGISTER NOT in compiled binary
```

### Test-3: Production Test Works in Release
```powershell
# tools/avb_test/ptp_clock_control_production_test.c
# Uses only IOCTL_AVB_ADJUST_FREQUENCY (no raw register access)
.\ptp_clock_control_production_test.exe
```

### Test-4: Debug Test Fails to Compile in Release
```c
// This should cause compile error in Release build:
#include "avb_ioctl.h"
AVB_REGISTER_REQUEST req;  // Error: undefined type
DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, ...);  // Error: undefined IOCTL
```

---

## Compliance

**Standards**: 
- ISO/IEC/IEEE 12207:2017 (Security Engineering)
- ISO/IEC 27034 (Application Security)

**Security Standards**:
- CWE-749: Exposed Dangerous Method or Function
- CWE-250: Execution with Unnecessary Privileges

---

## Traceability

Traces to: 
- #23 (REQ-NF-SEC-DEBUG-001: Debug-Only Raw Register Access)

**Related Requirements**:
- #24 (REQ-NF-SSOT-001: Single Source of Truth for IOCTL headers)
- #73 (REQ-NF-SEC-IOCTL-001: IOCTL Access Control)
- #89 (REQ-NF-SECURITY-BUFFER-001: Buffer Overflow Protection)

**Verified by**:
- TEST-SEC-DEBUG-001: Debug build includes raw register access
- TEST-SEC-DEBUG-002: Release build excludes raw register access
- TEST-SEC-DEBUG-003: Production IOCTLs work in Release build
- TEST-SEC-DEBUG-004: Migration guide verified

---

## Migration Guide

**Impact**: Existing test code using `IOCTL_AVB_READ/WRITE_REGISTER` will fail to compile in Release builds.

**Affected Files**:
- `tools/avb_test/tsauxc_toggle_test.c` - Uses raw TSAUXC writes
- `tools/avb_test/ptp_clock_control_test.c` - Uses raw TIMINCA writes
- Any user-mode diagnostic tools

**Effort Estimate**: 2-4 hours per test file

**Documentation**: See `docs/SECURITY_IOCTL_RESTRICTION.md` Section 4 for step-by-step migration instructions.

---

## Status

**Current Status**: Accepted (2025-12-09)

**Decision Made By**: Architecture Team, Security Team

**Stakeholder Approval**:
- [x] Security Review Team - Approved (compile-time security boundaries meet CWE-749 mitigation)
- [x] Driver Implementation Team - Approved (NDEBUG guards implemented in filter.c)
- [x] Testing Team - Approved (debug builds verified with diagnostic IOCTLs)
- [x] Release Management - Approved (release builds verified without raw register access)

**Rationale for Acceptance**:
- Eliminates dangerous IOCTL attack surface in production (principle of least privilege)
- Compile-time enforcement prevents accidental exposure (no runtime overhead)
- Preserves diagnostic flexibility in development builds
- Aligns with industry standard NDEBUG convention
- Zero performance impact (removed code doesn't execute)

**Implementation Status**: Complete
- NDEBUG guards implemented in `filter.c` (DeviceControl dispatch)
- Debug builds tested with `IOCTL_AVB_READ_REGISTER` and `IOCTL_AVB_WRITE_REGISTER`
- Release builds verified to return STATUS_INVALID_DEVICE_REQUEST
- Build system configured (Debug: NDEBUG undefined, Release: NDEBUG defined)
- Documentation updated (SECURITY_IOCTL_RESTRICTION.md)

---

## Approval

**Approval Criteria Met**:
- [x] Security vulnerability (CWE-749) mitigated via compile-time removal
- [x] Development workflow preserved (debug builds retain diagnostic capabilities)
- [x] Zero runtime performance impact (removed code doesn't execute)
- [x] Industry-standard NDEBUG convention followed
- [x] Build system integration verified (Debug/Release configurations)
- [x] Test coverage complete (debug and release builds validated)

**Review History**:
- 2025-12-09: Architecture Team reviewed and approved NDEBUG guard strategy
- 2025-12-09: Security Team validated CWE-749 mitigation approach
- 2025-12-09: Implementation verified in filter.c

**Next Review Date**: On security audit or when adding new diagnostic IOCTLs

---

## Notes

- NDEBUG guards implement "No Shortcuts" principle (security cannot be optional)
- Aligns with "Slow is Fast" (proper abstractions now = faster debugging later)
- Migration effort is one-time cost for long-term security benefit

---

**Last Updated**: 2025-12-09  
**Author**: Architecture Team, Security Team
