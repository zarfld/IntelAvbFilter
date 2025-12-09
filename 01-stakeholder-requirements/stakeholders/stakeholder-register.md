# Stakeholder Register - IntelAvbFilter

**Document Version**: 1.0  
**Last Updated**: 2025-12-07  
**Phase**: 01 - Stakeholder Requirements Definition  
**Standards**: ISO/IEC/IEEE 29148:2018, ISO/IEC/IEEE 12207:2017

## Purpose

This document identifies all stakeholders for the IntelAvbFilter project and documents their concerns, needs, and influence on system requirements.

**⚠️ Note**: This is a **supplementary document**. The authoritative source of stakeholder requirements is the collection of **GitHub Issues** labeled `type:stakeholder-requirement` and `phase:01-stakeholder-requirements`.

## Stakeholder Classification

### 1. Primary Technical Stakeholders (Runtime Components)

These are software components that directly interact with IntelAvbFilter at runtime.

#### 1.1 gPTP Stack (`zarfld/gptp`, `zarfld/gptp-fork`)

**GitHub Issue**: [#28: StR-001 - User-Mode API Surface for gPTP Stack Integration](https://github.com/zarfld/IntelAvbFilter/issues/28)

- **Role**: IEEE 802.1AS PTP servo implementation
- **Interaction**: Calls IntelAvbFilter IOCTLs for:
  - Hardware timestamp access (TX/RX)
  - PHC time query and adjustment
  - Cross timestamps (PHC ↔ system time)
- **Criticality**: **P0 (Critical)** - Core AVB/TSN functionality depends on this
- **Key Needs**:
  - Monotonic hardware timestamps (no jumps)
  - Sub-microsecond timestamp accuracy (<100ns)
  - Clear HW vs SW timestamp indicators
  - PHC control (read, adjust, frequency correction)
  - Predictable error handling
- **Success Metric**: gPTP converges to grandmaster within <1µs offset

---

### 2. Technical Dependency Stakeholders (Platform Libraries)

These are internal libraries that IntelAvbFilter depends on or integrates with.

#### 2.1 Intel AVB Register Abstraction (`zarfld/intel_avb`)

**GitHub Issue**: [#29: StR-002 - Intel AVB Register Abstraction Compatibility](https://github.com/zarfld/IntelAvbFilter/issues/29)

- **Role**: Hardware abstraction library for Intel NIC registers
- **Interaction**: IntelAvbFilter uses `intel_avb` to:
  - Configure TAS (Time-Aware Shaper / Qbv)
  - Configure CBS (Credit-Based Shaper / Qav)
  - Access PHC registers
  - Manage queues and priorities
- **Criticality**: **P1 (High)** - Enables maintainable, reusable hardware access
- **Key Needs**:
  - Stable API contract (IOCTL ↔ intel_avb mapping)
  - Semantic consistency (same types, units, constraints)
  - Kernel-mode compatibility (no user-mode dependencies)
  - Per-device configuration (I210/I219/I225/I226)
- **Success Metric**: No ad-hoc register poking; all hardware access via intel_avb

---

### 3. Conceptual / Specification Stakeholders

These define canonical semantics and standards compliance requirements.

#### 3.1 Standards Library (`zarfld/libmedia-network-standards`)

**GitHub Issue**: [#30: StR-003 - Standards Semantics Alignment](https://github.com/zarfld/IntelAvbFilter/issues/30)

- **Role**: Defines IEEE standards semantics (1588, 802.1AS, 802.1Q, AES67)
- **Interaction**: IntelAvbFilter must align with:
  - PTP time scales and epoch (IEEE 1588)
  - gPTP clock roles and states (IEEE 802.1AS)
  - TSN feature behavior (IEEE 802.1Q Qav/Qbv/Qbu)
- **Criticality**: **P1 (High)** - Standards compliance critical for interoperability
- **Key Needs**:
  - Naming consistency (`grandmasterID`, not `gmId`)
  - PTP epoch alignment (TAI, 1970-01-01)
  - TSN parameter semantics (gateStates, idleSlope)
  - No "driver-only" interpretations
- **Success Metric**: 100% of PTP/TSN terms match IEEE definitions

---

### 4. Upstream Platform Stakeholders

These are OS-level components IntelAvbFilter depends on.

#### 4.1 NDIS and Intel Miniport Drivers

**GitHub Issue**: [#31: StR-004 - NDIS Miniport Integration](https://github.com/zarfld/IntelAvbFilter/issues/31)

- **Role**: Windows NDIS stack + Intel NIC miniport drivers
- **Interaction**: IntelAvbFilter is an NDIS LWF that:
  - Attaches to Intel miniport drivers (e1d68x64.sys, iaNVMx64.sys)
  - Intercepts and timestamps packets
  - Accesses NIC registers via MMIO
  - Must not interfere with normal networking
- **Criticality**: **P0 (Critical)** - NDIS compliance is mandatory for Windows
- **Key Needs**:
  - Full NDIS LWF callback compliance
  - Transparent operation for non-AVB traffic (<1µs overhead)
  - Safe register access (no conflicts with miniport)
  - Hardware capability detection (I210/I219/I225/I226)
  - WHQL certification
- **Success Metric**: Passes Driver Verifier + WHQL tests; no BSOD in 24h stress test

---

### 5. Future / Planned Stakeholders

These will interact with IntelAvbFilter in future phases.

#### 5.1 Windows PTP/TSN Management Service (Future)

**GitHub Issue**: [#32: StR-005 - Future Windows PTP/TSN Management Service](https://github.com/zarfld/IntelAvbFilter/issues/32)

- **Role**: User-mode service for AVB/TSN configuration and monitoring
- **Interaction**: Will call IntelAvbFilter IOCTLs for:
  - TSN feature configuration (TAS, CBS)
  - PTP orchestration
  - Statistics and monitoring
  - Multi-NIC coordination
- **Criticality**: **P2 (Medium)** - Future requirement, but affects API design today
- **Key Needs**:
  - Stable IOCTL contract with versioning
  - Clear NIC identity (PCI location, friendly name)
  - Configuration atomicity (rollback on failure)
  - Actionable error messages
  - Predictable behavior on driver reload
- **Success Metric**: Service can manage TAS/CBS/PHC without writing custom code

---

### 6. Peer Stakeholders (External Devices)

These are external devices that expect IntelAvbFilter to produce standards-compliant traffic.

#### 6.1 AVB/TSN Endpoints and Test Tools

**GitHub Issue**: [#33: StR-006 - AVB/TSN Endpoint Interoperability](https://github.com/zarfld/IntelAvbFilter/issues/33)

- **Role**: Milan audio endpoints, TSN switches, embedded AVB devices, test tools
- **Interaction**: Expect IntelAvbFilter to:
  - Produce standards-compliant AVB/TSN traffic
  - Use correct VLAN tags and priorities
  - Respect TAS gate schedules
  - Maintain PTP synchronization
  - Establish AVTP streams correctly
- **Criticality**: **P1 (High)** - Interoperability is #1 requirement for production
- **Key Needs**:
  - Milan endpoint compatibility (AVDECC, AVTP streams)
  - TSN switch compliance (Qbv gate schedules)
  - PTP interoperability (IEEE 802.1AS grandmasters)
  - Qav compliance (CBS bandwidth reservation)
  - AVnu certification (long-term)
- **Success Metric**: Works with 3+ Milan endpoints, 2+ TSN switches; passes AVnu tests

---

## Stakeholder Relationship Map

```
                                    IntelAvbFilter
                                         │
                ┌────────────────────────┼────────────────────────┐
                │                        │                        │
           ┌────▼────┐            ┌──────▼──────┐         ┌──────▼──────┐
           │ gPTP    │            │ intel_avb   │         │   NDIS +    │
           │ Stack   │            │   Library   │         │  Miniport   │
           │  #28    │            │    #29      │         │    #31      │
           │ (P0)    │            │    (P1)     │         │    (P0)     │
           └─────────┘            └──────┬──────┘         └──────┬──────┘
                │                       │                       │
                │                       │                       │
                ▼                       ▼                       ▼
       ┌─────────────────┐     ┌────────────────┐     ┌────────────────┐
       │   Standards     │     │   Hardware     │     │   Windows OS   │
       │   Semantics     │     │   Registers    │     │     (NDIS)     │
       │      #30        │     │  (I210/I225)   │     │                │
       │     (P1)        │     └────────────────┘     └────────────────┘
       └─────────────────┘
                │
                ▼
       ┌─────────────────┐
       │  AVB/TSN        │
       │  Endpoints      │
       │     #33         │
       │    (P1)         │
       └─────────────────┘
                │
                ▼
       ┌─────────────────┐
       │  Future PTP/TSN │
       │    Service      │
       │      #32        │
       │     (P2)        │
       └─────────────────┘
```

**Legend**:
- **P0**: Critical priority (system cannot function without)
- **P1**: High priority (core features depend on)
- **P2**: Medium priority (future enhancement)

---

## Stakeholder Impact Analysis

### High Impact / High Influence

| Stakeholder | Impact on Requirements | Influence on Design |
|-------------|------------------------|---------------------|
| **NDIS + Miniport (#31)** | Mandatory NDIS compliance, transparent operation | Limits packet manipulation, register access model |
| **gPTP Stack (#28)** | Defines IOCTL API surface, timestamp semantics | Drives PHC access design, error handling |

### High Impact / Medium Influence

| Stakeholder | Impact on Requirements | Influence on Design |
|-------------|------------------------|---------------------|
| **intel_avb Library (#29)** | Reusable hardware abstraction | Shapes IOCTL parameter types, register access patterns |
| **Standards Semantics (#30)** | Naming, data types, behavior alignment | Constrains deviations from IEEE specs |
| **AVB/TSN Endpoints (#33)** | Interoperability acceptance criteria | Drives testing strategy, compliance validation |

### Medium Impact / Low Influence

| Stakeholder | Impact on Requirements | Influence on Design |
|-------------|------------------------|---------------------|
| **Future Service (#32)** | IOCTL versioning, error handling | Influences API design decisions today |

---

## Stakeholder Concerns and Requirements Mapping

| Stakeholder Concern | Requirement Category | GitHub Issue Link |
|---------------------|----------------------|-------------------|
| **gPTP needs hardware timestamps** | Functional (IOCTL API) | #28 |
| **intel_avb library reuse** | Non-Functional (Maintainability) | #29 |
| **Standards compliance** | Non-Functional (Compliance) | #30 |
| **NDIS compliance** | Functional + Non-Functional (Reliability) | #31 |
| **Future service API stability** | Non-Functional (Maintainability) | #32 |
| **Milan endpoint interoperability** | Functional + Non-Functional (Interoperability) | #33 |

---

## Stakeholder Communication Plan

### Primary Contacts

- **gPTP Stack**: Internal development (same maintainer)
- **intel_avb Library**: Internal library (same maintainer)
- **Standards Semantics**: IEEE specifications (public standards)
- **NDIS/Miniport**: Microsoft NDIS team (via WHQL feedback), Intel (NIC documentation)
- **Future Service**: Planned development (internal)
- **AVB/TSN Endpoints**: Community testing (AVnu Alliance, audio vendors)

### Communication Frequency

| Stakeholder | Frequency | Method |
|-------------|-----------|--------|
| gPTP Stack | Continuous | Code integration, issue tracking |
| intel_avb Library | Continuous | Submodule updates, API reviews |
| Standards Semantics | As needed | IEEE standard reviews, AVnu specs |
| NDIS/Miniport | Release cycles | WHQL testing, driver certification |
| Future Service | Quarterly | API design reviews |
| AVB/TSN Endpoints | Per release | Interoperability testing events |

---

## Acceptance Criteria for Stakeholder Requirements

All stakeholder requirements (#28-#33) must satisfy:

1. **Completeness**: All stakeholder needs documented
2. **Clarity**: Success criteria measurable and testable
3. **Consistency**: No conflicting requirements between stakeholders
4. **Traceability**: Linked to system requirements (Phase 02) and tests (Phase 07)
5. **Approval**: Stakeholder representatives acknowledge requirements

---

## References

- ISO/IEC/IEEE 29148:2018 - Requirements Engineering
- ISO/IEC/IEEE 12207:2017 - Software Life Cycle Processes
- GitHub Issues #28-#33 - Stakeholder Requirements
- `.github/instructions/phase-01-stakeholder-requirements.instructions.md`

---

**Document Status**: ✅ Complete (6 stakeholder classes identified)  
**Review Status**: Pending stakeholder validation  
**Next Phase**: Phase 02 - Requirements Analysis & Specification
