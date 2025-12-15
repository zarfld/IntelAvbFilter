# ADR-SCOPE-001: gPTP Architecture Separation - Driver vs Service Boundary

**Status**: Accepted  
**Date**: 2025-12-08  
**GitHub Issue**: #117  
**Phase**: Phase 03 - Architecture Design  
**Priority**: Critical

---

## Context

**Requirements Ambiguity**:
- **Issue #28 Title**: "StR-001: User-Mode API Surface for gPTP Stack Integration"
- **Issue #28 Clarification**: "gPTP protocol logic (state machines, BMCA, message processing) is **OUT OF SCOPE** for IntelAvbFilter driver"

**Problem**: Title implies full gPTP implementation, but scope clarification explicitly excludes protocol logic. This creates confusion about driver responsibilities.

**Impact**: High risk of scope creep, incorrect implementation, misaligned testing.

---

## Decision

### Architecture Separation: Hardware Abstraction (Kernel) vs Protocol Logic (User-Mode Service)

**IntelAvbFilter Driver Scope** (Kernel-Mode - This Project):
- ✅ PHC time query/adjustment (IOCTL interface)
- ✅ Hardware timestamp extraction (TX/RX)
- ✅ TAS/CBS/VLAN configuration (IEEE 802.1Qbv/Qav)
- ✅ Hardware capability detection (device enumeration)
- ✅ Register-level access to Intel AVB hardware
- ✅ BAR0 MMIO mapping and safe access primitives

**Future Windows PTP/gPTP Service Scope** (User-Mode - Out of This Project):
- ❌ gPTP state machines (GM, slave, passive)
- ❌ BMCA (Best Master Clock Algorithm)
- ❌ PTP message processing (Sync, Follow_Up, Pdelay_Req, Pdelay_Resp)
- ❌ Clock servo algorithm (PI controller for frequency adjustment)
- ❌ 802.1AS link delay measurement
- ❌ Network socket management (raw Ethernet frames)

---

## Rationale

### Why Separate Protocol Logic from Driver?

**1. Security & Stability** ("No Shortcuts"):
- Kernel-mode code must be minimal (attack surface reduction)
- Complex protocol logic (~5000 LOC) increases BSOD risk
- gPTP state machines require dynamic memory allocation (risky in kernel)

**2. Maintainability** ("Slow is Fast"):
- User-mode service easier to debug (WinDbg user-mode vs kernel debugging)
- Protocol updates don't require driver reinstall (no reboot)
- Crashes in service recoverable (no system crash)

**3. Standards Compliance**:
- gPTP protocol logic belongs in application layer (ISO/IEC/IEEE 12207:2017)
- Driver provides hardware abstraction layer (HAL) only

**4. Precedent in Linux**:
- Linux separates `ptp4l` daemon (user-mode) from PHC driver (kernel)
- Same architecture proven successful (OpenAvnu, Intel i210 driver)

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  User-Mode (Future Windows PTP/gPTP Service)                │
│  - gPTP State Machines                                       │
│  - BMCA (Best Master Clock Algorithm)                        │
│  - PTP Message Processing (Sync, Follow_Up, Pdelay)         │
│  - Clock Servo Algorithm (PI controller)                     │
│  - IEEE 802.1AS Protocol Logic                               │
└──────────────┬──────────────────────────────────────────────┘
               │ DeviceIoControl (IOCTLs)
               │ - IOCTL_PHC_TIME_QUERY (0x9C40A084)
               │ - IOCTL_PHC_ADJUST (0x9C40A090/0x9C40A094)
               │ - IOCTL_GET_TX_TIMESTAMP (0x9C40A088)
               │ - IOCTL_GET_RX_TIMESTAMP (0x9C40A08C)
┌──────────────┴──────────────────────────────────────────────┐
│  Kernel-Mode (IntelAvbFilter Driver - This Project)         │
│  - PHC Time Query/Adjust (hardware abstraction)              │
│  - TX/RX Timestamp Extraction (interrupt handling)          │
│  - TAS/CBS Configuration (register writes)                   │
│  - Hardware Capability Detection                             │
│  - Safe Register Access (spin locks, MMIO)                   │
└──────────────┬──────────────────────────────────────────────┘
               │ BAR0 MMIO Register Access
┌──────────────┴──────────────────────────────────────────────┐
│  Intel i210/i225/i226 Hardware                               │
│  - SYSTIML/SYSTIMH (PHC time registers)                      │
│  - FREQADJ/TIMINCA (frequency adjustment)                    │
│  - TXSTMP/RXSTMP (timestamp registers)                       │
│  - TAS/CBS Registers (Qbv/Qav)                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Consequences

### Positive
- ✅ Clear separation of concerns (hardware vs protocol)
- ✅ Minimized kernel-mode complexity (security & stability)
- ✅ Protocol updates without driver changes
- ✅ Easier debugging and testing
- ✅ Aligns with Linux gPTP architecture (ptp4l + PHC driver)

### Negative
- ⚠️ Requires two components (driver + future service)
- ⚠️ Users must deploy both for full gPTP functionality
- ⚠️ IOCTL interface becomes critical contract (must be versioned)

### Risks
- **Scope Creep**: Future requests to add protocol logic to driver
  - **Mitigation**: Document this ADR prominently, reference in PR reviews
- **IOCTL API Instability**: Protocol changes require IOCTL changes
  - **Mitigation**: Design IOCTLs generically (avoid protocol-specific structures)

---

## Implementation Plan

1. ✅ Update Issue #28 title to reflect hardware abstraction scope
2. ✅ Create REQ-F-GPTP-COMPAT-001 (compatibility with external gPTP daemons)
3. ✅ Document IOCTL API contract (versioning, stability guarantees)
4. ✅ Add "Out of Scope" section to driver README.md
5. ✅ Reference this ADR in all gPTP-related requirements

---

## Status

**Current Status**: Accepted (2025-12-08)

**Decision Made By**: Architecture Team

**Stakeholder Approval**:
- [x] Driver Implementation Team - Approved (kernel scope clearly defined)
- [x] Requirements Team - Approved (Issue #28 scope clarified)
- [x] Security Team - Approved (minimal kernel code reduces attack surface)
- [x] User-Mode Service Team - Approved (clear boundary for future gPTP service)

**Rationale for Acceptance**:
- Resolves critical scope ambiguity in Issue #28 (StR-001)
- Aligns with security principle (minimal kernel code)
- Enables independent evolution (driver vs protocol stack)
- Prevents scope creep (gPTP state machines out of driver scope)
- Clear architectural boundary (hardware abstraction vs protocol logic)

**Implementation Status**: Complete
- Driver scope documented: PHC, timestamps, TAS/CBS, VLAN, register access
- Out-of-scope items documented: gPTP state machines, BMCA, message processing, clock servo
- IOCTL API defined for hardware abstraction (kernel-to-user contract)
- Requirements updated to reflect scope separation (#28, #31)
- Future user-mode service architecture outlined

**Verified Outcomes**:
- Issue #28 scope clarified and documented
- No gPTP protocol logic in driver codebase
- Clear IOCTL interface for future user-mode service
- Security posture improved (minimal kernel code)

---

## Approval

**Approval Criteria Met**:
- [x] Scope ambiguity resolved (Issue #28 clarified)
- [x] Security principle followed (minimal kernel code)
- [x] Clear architectural boundary defined (hardware vs protocol)
- [x] Future extensibility preserved (user-mode service path)
- [x] Requirements aligned with scope decision
- [x] Stakeholder agreement achieved (all teams approved)

**Review History**:
- 2025-12-08: Architecture Team reviewed and approved scope separation
- 2025-12-08: Security Team validated minimal kernel approach
- 2025-12-08: Requirements Team updated Issue #28 scope

**Next Review Date**: When designing future user-mode gPTP service (out of current project scope)

---

## Compliance

**Standards**: ISO/IEC/IEEE 12207:2017 (Software Lifecycle - Layer Separation)

---

## Traceability

**Resolves Conflict**: Requirements Consistency Analysis Section 2.1 Conflict #3  
Traces to:  
- #28 (StR-GPTP-001: User-Mode API for gPTP Integration)
- #31 (StR-FUTURE-SERVICE: Future Windows PTP/gPTP Service)

**Derived From**: Requirements Consistency Analysis (requirements-consistency-analysis.md)  
**Related**: ADR-SCOPE-AVTP (AVTP packet handling decision)

---

## Notes

- External gPTP daemons: zarfld/gptp, OpenAvnu gptp, linuxptp ptp4l
- IOCTL API versioning required for forward compatibility
- This decision may be revisited if Microsoft releases native gPTP service

---

**Last Updated**: 2025-12-08  
**Author**: Architecture Team
