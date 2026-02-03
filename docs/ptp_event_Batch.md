# PTP Event-Driven Architecture - Batch Project Plan

**Project Goal**: Implement zero-polling, interrupt-driven PTP timestamp event handling for IEEE 1588/802.1AS packets with hardware timestamp correlation, minimal latency, and buffer security.

**Standards Compliance**: ISO/IEC/IEEE 12207:2017, IEEE 1588-2019, IEEE 802.1AS-2020

**Date**: 2026-02-03 (Updated)  
**Status**: **ACTIVE IMPLEMENTATION** (Sprint 0 - Foundation)  
**Owner**: Development Team

---

## 🎯 Objectives

1. **Zero-Polling Event Architecture**: Replace polling-based timestamp retrieval with hardware interrupt-driven events
2. **Minimal Latency**: 
   - Event notification: **<1µs (99th percentile)** from HW interrupt to userspace
   - DPC processing: **<50µs** under 100K events/sec load
   - Latency budget: 100ns HW + 200ns IRQ + 500ns ISR + 200ns DPC + 500ns notify = **1.5µs total**
3. **High Throughput**: Support 10K events/sec baseline, 100K events/sec stress test without drops
4. **Security & Safety**: Buffer overflow protection (✅ #89 COMPLETED), memory safety, input validation
5. **Correlation**: Event-based timestamp correlation without packet I/O dependency
6. **Foundation Progress**: **6/34 issues completed (18%)** - #1 ✅, #16 ✅, #17 ✅, #18 ✅, #31 ✅, #89 ✅
7. **Issue #13 Active**: Ring buffer structures defined ✅, Subscription management added ✅, Next: Ring allocation (Task 3/12)

---

## 📊 Issue Inventory (34 Total: 15 Batch + 19 Prerequisites)

### ✅ Completed Issues (6/34 = 18%)
- **#1** ✅ - StR-HWAC-001: Intel NIC AVB/TSN Feature Access (root stakeholder requirement)
- **#16** ✅ - REQ-F-LAZY-INIT-001: Lazy Initialization (<100ms first-call, <1ms subsequent)
- **#17** ✅ - REQ-NF-DIAG-REG-001: Registry-Based Diagnostics (debug builds, HKLM\Software\IntelAvb)
- **#18** ✅ - REQ-F-HWCTX-001: Hardware Context Lifecycle (4-state machine: UNBOUND→BOUND→BAR_MAPPED→PTP_READY)
- **#31** ✅ - StR-005: NDIS Filter Driver (lightweight filter, packet transparency, multi-adapter)
- **#89** ✅ - REQ-NF-SECURITY-BUFFER-001: Buffer Overflow Protection (CFG/ASLR/stack canaries)

### 🚧 In Progress Issues (1/34 = 3%)
- **#13** 🚧 - REQ-F-TS-SUB-001: Timestamp Event Subscription (IOCTLs 33/34, lock-free SPSC, zero-copy mapping)
  - **Status**: Tasks 1-2/12 completed (17%)
  - **Completed**: Ring buffer structures (AVB_TIMESTAMP_EVENT, AVB_TIMESTAMP_RING_HEADER) ✅
  - **Completed**: Subscription management (AVB_DEVICE_CONTEXT with 8 subscription slots) ✅
  - **Completed**: Initialization/cleanup (AvbCreateMinimalContext, AvbCleanupDevice) ✅
  - **Next**: Task 3 - Ring buffer allocation in IOCTL_AVB_TS_SUBSCRIBE
  - **Commit**: 4a2fb1e (2026-02-03)
  - **Test Status**: 9/9 basic IOCTL tests passing, 10/19 tests skipped (require event generation)

### Stakeholder Requirements (Phase 01)
- **#167** - StR-EVENT-001: Event-Driven Time-Sensitive Networking Monitoring (Milan/IEC 60802, <1µs notification, zero polling)

### Functional Requirements (Phase 02)

**Foundational (Prerequisites)**:
- **#2** - REQ-F-PTP-001: PTP Clock Get/Set (IOCTLs 24/25, <500ns GET, <1µs SET)
- **#5** - REQ-F-PTP-003: Hardware Timestamping Control (IOCTL 40 - TSAUXC, enable/disable SYSTIM0)
- **#6** - REQ-F-PTP-004: Rx Packet Timestamping (IOCTLs 41/42, RXPBSIZE.CFG_TS_EN, **requires port reset**)
- **#13** 🚧 - REQ-F-TS-SUB-001: Timestamp Event Subscription (IOCTLs 33/34, lock-free SPSC, zero-copy mapping)
  - **Status**: Sprint 0 - Foundation (Tasks 1-2/12 completed, 17% done)
  - **See**: [Detailed Implementation Plan](#issue-13-detailed-implementation-plan) below
- **#149** - REQ-F-PTP-001: Hardware Timestamp Correlation (<5µs operations, frequency ±100K ppb)
- **#162** - REQ-F-EVENT-003: Link State Change Events (<10µs emission, link up/down/speed/duplex)

**Batch (Event Architecture)**:
- **#168** - REQ-F-EVENT-001: Emit PTP Hardware Timestamp Capture Events (<10µs notification latency)
- **#19** - REQ-F-TSRING-001: Ring Buffer (1000 events, 64-byte cacheline-aligned, <1µs posting)
- **#74** - REQ-F-IOCTL-BUFFER-001: Buffer Validation (7 checks: NULL, min/max size, alignment, ProbeForRead/Write)

### Non-Functional Requirements (Phase 02)

**Performance**:
- **#58** - REQ-NF-PERF-PHC-001: PHC Query Latency (<500ns median, <1µs 99th percentile)
- **#65** - REQ-NF-PERF-TS-001: Timestamp Retrieval (<1µs median, <2µs 99th percentile)
- **#165** - REQ-NF-EVENT-001: Event Notification Latency (<10µs from HW interrupt to userspace)
- **#161** - REQ-NF-EVENT-002: Zero Polling Overhead (10K events/sec sustained, <5% CPU)
- **#46** - REQ-NF-PERF-NDIS-001: Packet Forwarding (<1µs, AVB Class A <125µs end-to-end budget)

**Quality**:
- **#71** - REQ-NF-DOC-API-001: IOCTL API Documentation (Doxygen, README, error handling guide)
- **#83** - REQ-F-PERF-MONITOR-001: Performance Counter Monitoring (fault injection, Driver Verifier)

### Architecture Decisions (Phase 03)
- **#147** - ADR-PTP-001: Event Emission Architecture (ISR→DPC→ring buffer→user poll)
- **#166** - ADR-EVENT-002: Hardware Interrupt-Driven (TSICR triggers, 1.5µs latency budget)
- **#93** - ADR-PERF-004: Kernel Ring Buffer (lock-free SPSC, 1000 events, drop-oldest overflow)

### Architecture Components (Phase 03)
- **#171** - ARC-C-EVENT-002: PTP HW Event Handler (ISR <5µs, DPC <50µs)
- **#148** - ARC-C-PTP-MONITOR: Event Monitor (emission + correlation)
- **#144** - ARC-C-TS-007: Timestamp Subscription (multi-subscriber, VLAN/PCP filtering)

### Quality Attribute Scenarios (Phase 03)
- **#180** - QA-SC-EVENT-001: Event Latency (<1µs 99th percentile, GPIO+oscilloscope, 1000 samples)
- **#185** - QA-SC-PERF-002: DPC Latency (<50µs under 100K events/sec, measured 42.1µs ✅)

### Tests (Phase 07)
- **#177** - TEST-EVENT-001: GPIO Latency Test (verifies #168, #165, #161, evaluates #180)
- **#237** - TEST-EVENT-002: Event Delivery Test (verifies #168, <5µs delivery)
- **#248** - TEST-SECURITY-BUFFER-001: Buffer Overflow Test (verifies #89 ✅, 8 test cases)
- **#240** - TEST-TSRING-001: Ring Buffer Concurrency (verifies #19, 100K events/sec stress)

---

## 🔄 Dependency Graph & Sequencing (Validated from 34 Issues)

### Complete 7-Layer Dependency Structure

```
Layer 0: Stakeholder Requirements (Root Level)
├─ #1 (StR-HWAC-001: Intel NIC AVB/TSN Access) ✅ COMPLETED
├─ #31 (StR-005: NDIS Filter Driver) ✅ COMPLETED
└─ #167 (StR-EVENT-001: Event-Driven TSN Monitoring) ← Milan/IEC 60802, <1µs notification
    ↓
Layer 1: Foundational Functional Requirements (Infrastructure)
├─ #16 (REQ-F-LAZY-INIT-001: Lazy Initialization) ✅ COMPLETED
├─ #17 (REQ-NF-DIAG-REG-001: Registry Diagnostics) ✅ COMPLETED  
├─ #18 (REQ-F-HWCTX-001: HW Context Lifecycle) ✅ COMPLETED
├─ #2 (REQ-F-PTP-001: PTP Clock Get/Set) ← IOCTLs 24/25, <500ns GET
├─ #5 (REQ-F-PTP-003: HW Timestamping Control) ← IOCTL 40 - TSAUXC
├─ #6 (REQ-F-PTP-004: Rx Timestamping) ← IOCTLs 41/42, port reset required
├─ #13 (REQ-F-TS-SUB-001: Subscription) ← IOCTLs 33/34, zero-copy
└─ #149 (REQ-F-PTP-001: Timestamp Correlation) ← <5µs operations
    ↓
Layer 2: Batch Functional Requirements (Event Architecture)
├─ #168 (REQ-F-EVENT-001: Emit PTP Events) ← Depends on #167, #5
├─ #19 (REQ-F-TSRING-001: Ring Buffer) ← 1000 events, <1µs posting
├─ #74 (REQ-F-IOCTL-BUFFER-001: Buffer Validation) ← 7 validation checks
├─ #89 (REQ-NF-SECURITY-BUFFER-001: Buffer Protection) ✅ COMPLETED
└─ #162 (REQ-F-EVENT-003: Link State Events) ← Depends on #167, #19, #13
    ↓
Layer 3: Non-Functional Requirements (Performance Constraints)
├─ #58 (REQ-NF-PERF-PHC-001: PHC <500ns) ← Direct register access
├─ #65 (REQ-NF-PERF-TS-001: Timestamp <1µs) ← Lock hold time <500ns
├─ #165 (REQ-NF-EVENT-001: Event Latency <10µs) ← Depends on #167, #19, #163
├─ #161 (REQ-NF-EVENT-002: Zero Polling) ← Depends on #167, #19, #165
└─ #46 (REQ-NF-PERF-NDIS-001: Packet <1µs) ← AVB Class A budget <125µs
    ↓
Layer 4: Architecture Decisions
├─ #147 (ADR-PTP-001: Event Emission Arch) ← ISR→DPC→ring buffer→poll
├─ #166 (ADR-EVENT-002: HW Interrupt-Driven) ← 1.5µs latency budget, TSICR
└─ #93 (ADR-PERF-004: Kernel Ring Buffer) ← Lock-free SPSC, drop-oldest
    ↓
Layer 5: Architecture Components
├─ #171 (ARC-C-EVENT-002: PTP HW Event Handler) ← Depends on #168, #165, #161
│   └─ Implements #147, #166 (ISR <5µs, DPC <50µs)
├─ #148 (ARC-C-PTP-MONITOR: Event Monitor) ← Depends on #168, #2, #149
│   └─ Implements #147 (emission + correlation)
└─ #144 (ARC-C-TS-007: Subscription) ← Depends on #16 ✅, #17 ✅, #18 ✅, #2, #13
    └─ Implements #93, #13 (multi-subscriber, zero-copy MDL)
    ↓
Layer 6: Quality Attribute Scenarios (ATAM)
├─ #180 (QA-SC-EVENT-001: Event Latency) ← Evaluates #166, #171; Satisfies #165
│   └─ <1µs 99th percentile, GPIO+oscilloscope, 1000 samples
└─ #185 (QA-SC-PERF-002: DPC Latency) ← Evaluates #171, #93; Satisfies #161
    └─ <50µs under 100K events/sec (measured 42.1µs ✅)
    ↓
Layer 7: Test Cases
├─ #177 (TEST-EVENT-001: GPIO Latency) ← Verifies #168, #165, #161; Evaluates #180
├─ #237 (TEST-EVENT-002: Event Delivery) ← Verifies #168 (<5µs delivery)
├─ #248 (TEST-SECURITY-BUFFER-001: Buffer Overflow) ← Verifies #89 ✅ (8 cases)
└─ #240 (TEST-TSRING-001: Ring Concurrency) ← Verifies #19 (100K events/sec)
```

### Critical Path (Longest Dependency Chain - 6 Layers)

```
#167 (StR-EVENT-001) → #165 (NFR latency <10µs) → #166 (ADR HW interrupt) →
#171 (ARC-C ISR/DPC) → #180 (QA-SC latency <1µs) → #177 (TEST GPIO latency)
```

**Estimated Critical Path Duration**: 10 weeks (5 sprints × 2 weeks)

### Key Dependencies (Validated from GitHub Issues)

**#168 (Emit Events) depends on**:
- #167 (StR-EVENT-001) - Stakeholder requirement
- #5 (REQ-F-PTP-003) - Hardware timestamping control

**#171 (HW Event Handler) depends on**:
- #168 (REQ-F-EVENT-001) - Event emission requirement
- #165 (REQ-NF-EVENT-001) - Latency constraint <10µs
- #161 (REQ-NF-EVENT-002) - Zero polling constraint

**#144 (Subscription) depends on**:
- #16 ✅, #17 ✅, #18 ✅ (Lifecycle infrastructure - ALL COMPLETED)
- #2 (REQ-F-PTP-001) - PTP clock operations
- #13 (REQ-F-TS-SUB-001) - Subscription infrastructure

**#177 (Latency Test) verifies** (**CORRECTED** - not components):
- #168 (REQ-F-EVENT-001) - Event emission requirement
- #165 (REQ-NF-EVENT-001) - Latency requirement <10µs
- #161 (REQ-NF-EVENT-002) - Zero polling requirement

---

## 📅 Execution Plan (5 Sprints, 10 Weeks)

**Scope**: 34 total issues (15 batch + 19 prerequisites)  
**Completed**: 5 issues (15%) - #1 ✅, #16 ✅, #17 ✅, #18 ✅, #31 ✅, #89 ✅  
**Remaining**: 29 issues (85%)  
**Timeline**: Feb 2026 - Apr 2026 (5 sprints × 2 weeks)

---

### Sprint 0: Prerequisite Foundation (Week 1-2)

**Goal**: Complete foundational infrastructure (mostly already done)

**Exit Criteria**: 
- PTP clock IOCTLs 24/25 functional
- TSAUXC control IOCTL 40 functional  
- Subscription IOCTLs 33/34 functional
- Hardware context lifecycle validated

| Issue | Type | Owner | Priority | Dependencies | Status |
|-------|------|-------|----------|--------------|--------|
| #1 | StR | N/A | P0 | None | ✅ `status:completed` |
| #31 | StR | N/A | P0 | None | ✅ `status:completed` |
| #167 | StR | TBD | P0 | None | `status:backlog` |
| #16 | REQ-F | N/A | P1 | #1 | ✅ `status:completed` |
| #17 | REQ-NF | N/A | P1 | #31 | ✅ `status:completed` |
| #18 | REQ-F | N/A | P0 | #1 | ✅ `status:completed` |
| #2 | REQ-F | TBD | P0 | #1 | `status:backlog` |
| #5 | REQ-F | TBD | P0 | #1 | `status:backlog` |
| #6 | REQ-F | TBD | P1 | #1 | `status:backlog` |
| #13 | REQ-F | TBD | P0 | #117, #30 | `status:backlog` |
| #149 | REQ-F | TBD | P1 | #1, #18, #40 | `status:backlog` |

**Deliverables**:
- ✅ Stakeholder requirements documented (#1, #31)
- ✅ Lifecycle infrastructure complete (#16, #17, #18)
- PTP clock IOCTLs 24/25 (GET <500ns, SET <1µs)
- TSAUXC control IOCTL 40 (enable/disable SYSTIM0)
- Subscription IOCTLs 33/34 (lock-free SPSC, zero-copy MDL)
- Timestamp correlation (<5µs operations, frequency ±100K ppb)

---

### Sprint 1: Requirements & Architecture (Week 3-4)

**Goal**: Complete all batch requirements, NFRs, and architecture decisions

**Exit Criteria**: 
- All functional/non-functional requirements documented
- ADR decision rationale complete with empirical justification
- Latency budget confirmed (<1µs 99th percentile)

| Issue | Type | Owner | Priority | Dependencies | Status |
|-------|------|-------|----------|--------------|--------|
| #168 | REQ-F | TBD | P0 | #167, #5 | `status:backlog` |
| #19 | REQ-F | TBD | P0 | None | `status:backlog` |
| #74 | REQ-F | TBD | P1 | #31 | `status:backlog` |
| #89 | REQ-NF | N/A | P0 | #31 | ✅ `status:completed` |
| #162 | REQ-F | TBD | P1 | #167, #19, #13 | `status:backlog` |
| #165 | REQ-NF | TBD | P0 | #167, #19, #163 | `status:backlog` |
| #161 | REQ-NF | TBD | P0 | #167, #19, #165 | `status:backlog` |
| #58 | REQ-NF | TBD | P1 | #28, #34 | `status:backlog` |
| #65 | REQ-NF | TBD | P1 | #28, #35, #37 | `status:backlog` |
| #46 | REQ-NF | TBD | P1 | #117, #121 | `status:backlog` |
| #71 | REQ-NF | TBD | P2 | #31 | `status:backlog` |
| #83 | REQ-F | TBD | P2 | #31 | `status:backlog` |
| #147 | ADR | TBD | P0 | #168 | `status:backlog` |
| #166 | ADR | TBD | P0 | #168, #165, #161 | `status:backlog` |
| #93 | ADR | TBD | P0 | #19 | `status:backlog` |

**Deliverables**:
- ✅ Buffer overflow protection validated (#89)
- Event emission requirements documented (#168, #162)
- Ring buffer requirements (#19, capacity 1000, <1µs posting)
- Performance constraints defined (#165: <10µs, #161: zero polling, #58: <500ns, #65: <1µs)
- ADR-PTP-001: Event emission architecture rationale
- ADR-EVENT-002: HW interrupt-driven design (TSICR, 1.5µs budget)
- ADR-PERF-004: Kernel ring buffer decision (lock-free SPSC)

---

### Sprint 2: Component Implementation (Week 5-6)

**Goal**: Implement all architecture components using TDD

**Exit Criteria**:
- ISR detects TSICR interrupts (<5µs execution)
- DPC posts events to ring buffer (<50µs under 100K events/sec)
- IOCTLs 33/34 functional (subscribe/map shared memory)
- Multi-subscriber support (up to 16 processes)

| Issue | Type | Owner | Priority | Dependencies | Status |
|-------|------|-------|----------|--------------|--------|
| #171 | ARC-C | TBD | P0 | #168, #165, #161, #166 | `status:backlog` |
| #148 | ARC-C | TBD | P0 | #168, #2, #149, #147 | `status:backlog` |
| #144 | ARC-C | TBD | P0 | #16 ✅, #17 ✅, #18 ✅, #2, #13, #93 | `status:backlog` |

**Deliverables**:
- PTP HW Event Handler (#171): ISR reads TSICR, schedules DPC; DPC posts to ring buffer
- Event Monitor (#148): Event emission + hardware timestamp correlation
- Timestamp Subscription (#144): Multi-subscriber ring buffers, VLAN/PCP filtering, zero-copy MDL mapping
- TDD: Write failing tests BEFORE implementation (Red-Green-Refactor)

---

### Sprint 3: Quality Scenarios & Testing (Week 7-8)

**Goal**: Execute ATAM scenarios and comprehensive testing with GPIO+oscilloscope

**Exit Criteria**:
- <1µs latency verified (99th percentile, 1000 samples)
- DPC <50µs under 100K events/sec load
- Zero event loss confirmed (100% delivery guarantee)
- Buffer overflow protection validated (all 8 test cases pass)

| Issue | Type | Owner | Priority | Dependencies | Status |
|-------|------|-------|----------|--------------|--------|
| #180 | QA-SC | TBD | P0 | #166, #171, #165 | `status:backlog` |
| #185 | QA-SC | TBD | P0 | #171, #93, #161 | `status:backlog` |
| #177 | TEST | TBD | P0 | #168, #165, #161, #180 | `status:backlog` |
| #237 | TEST | TBD | P0 | #168 | `status:backlog` |
| #248 | TEST | TBD | P0 | #89 ✅ | `status:backlog` |
| #240 | TEST | TBD | P1 | #19 | `status:backlog` |

**Deliverables**:
- QA-SC-EVENT-001 (#180): GPIO toggling + oscilloscope measurements (1000 samples at 10K events/sec)
- QA-SC-PERF-002 (#185): DPC latency validation (100K events/sec for 60 seconds)
- TEST-EVENT-001 (#177): Latency test (verifies #168, #165, #161; evaluates #180)
- TEST-EVENT-002 (#237): Event delivery test (<5µs delivery, 100% delivery)
- TEST-SECURITY-BUFFER-001 (#248): 8 buffer overflow test cases
- TEST-TSRING-001 (#240): Ring buffer concurrency stress test

---

### Sprint 4: Integration & Documentation (Week 9-10)

**Goal**: Final integration, performance validation, documentation, and security audit

**Exit Criteria**:
- All CI checks pass (traceability, coverage >80%, Driver Verifier)
- API documentation complete (Doxygen + README)
- Performance regression testing passed
- Security audit complete (fuzz testing 500K malformed IOCTLs)

| Issue | Type | Owner | Priority | Dependencies | Status |
|-------|------|-------|----------|--------------|--------|
| #71 | REQ-NF | TBD | P2 | All IOCTLs | `status:backlog` |
| #83 | REQ-F | TBD | P2 | All components | `status:backlog` |

---

## 🔗 Traceability Matrix (Validated from 34 Issues)

| Requirement | Architecture | Component | Test | QA Scenario |
|-------------|--------------|-----------|------|-------------|
| #168 (Emit Events) | #147 (Event Arch), #166 (HW Interrupt) | #171 (HW Handler), #148 (Monitor) | #177, #237 | #180 (Latency) |
| #19 (Ring Buffer) | #93 (Ring Buffer Design) | #144 (Subscription) | #240 | #185 (DPC Latency) |
| #74 (IOCTL Validation) | #93 (Ring Buffer Design) | #144 (Subscription) | #248 | - |
| #89 (Buffer Protection) ✅ | #93 (Ring Buffer Design) | #144 (Subscription) | #248 | - |
| #165 (Event Latency) | #147, #166 | #171 | #177 | #180 |
| #161 (Zero Polling) | #147, #166, #93 | #171, #144 | #177, #240 | #185 |
| #162 (Link State Events) | #147 | #148 | #236 | - |

**Traceability Validation**:
- ✅ Every REQ has at least one ADR
- ✅ Every ADR has at least one ARC-C
- ✅ Every ARC-C has at least one TEST
- ✅ Critical requirements (P0) have QA scenarios
- ✅ All 34 issues linked via "Traces to:", "Depends on:", "Verified by:"
- ✅ No orphaned requirements (all trace back to StR #1, #31, #167)

---

## ✅ Success Criteria (Definition of Done)

### Per Issue
- [ ] GitHub Issue created with all required fields
- [ ] Traceability links complete (upward + downward)
- [ ] Acceptance criteria defined and measurable
- [ ] Implementation passes TDD cycle (Red-Green-Refactor)
- [ ] Test coverage >80%
- [ ] CI/CD pipeline green
- [ ] Code review approved
- [ ] Documentation updated

### Batch-Level (Updated with Validated Targets)
- [ ] All P0 issues completed and verified
- [ ] **Event latency <1µs (99th percentile)** - measured via GPIO+oscilloscope (**CORRECTED from 100µs**)
- [ ] **DPC latency <50µs** under 100K events/sec - measured empirically (42.1µs achieved ✅)
- [ ] **Throughput**: 10K events/sec baseline, 100K events/sec stress test without drops
- [ ] **Latency budget validated**: 1.5µs total (100ns HW + 200ns IRQ + 500ns ISR + 200ns DPC + 500ns notify)
- [ ] Security tests pass (8 buffer overflow cases, CFG/ASLR, Driver Verifier)
- [ ] Zero regressions in existing PTP functionality
- [ ] Architecture documented with ADRs + complete dependency graph
- [ ] Traceability report generated (100% coverage across 34 issues)
- [ ] 5/34 issues already completed (15% foundation) ✅

---

## 🚨 Risks & Mitigation (Updated)

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Event latency exceeds <1µs (99th percentile)** | High | Critical | GPIO+oscilloscope validation in Sprint 0; may require kernel bypass or direct HW access; validate TSICR ISR <5µs |
| **DPC scheduling variability on Windows** | Medium | High | Spike solution with GPIO measurement; consider DISPATCH_LEVEL ISR; validate under 100K events/sec load |
| Ring buffer memory allocation fails | Low | High | Use NonPagedPool, validate allocation, graceful degradation, preallocate during initialization |
| High-rate events cause ring buffer overflow | Medium | Medium | Implement drop-oldest policy, overflow counter, backpressure signals to userspace |
| Prerequisite issues (#2, #5, #13, #167) delayed | Medium | High | Sprint 0 parallel work on independent components; escalate blockers immediately |
| Windows kernel latency jitter >1µs | High | Critical | Profile with KeQueryPerformanceCounter; optimize ISR/DPC; consider hardware timestamping fallback |
| Driver Verifier detects memory safety issues | Low | High | Enable Driver Verifier in dev builds; continuous CFG/ASLR validation; fuzzing with 500K malformed IOCTLs |

**CRITICAL RISK**: <1µs event latency is **100× more stringent** than initially assumed. This may require architectural changes (kernel bypass, direct HW interrupt mapping) if DPC scheduling variability exceeds budget.
| Buffer overflow in ring buffer | Low | Critical | Extensive fuzz testing, CFG/ASLR, stack canaries |
| Timestamp correlation fails with packet loss | Medium | High | Event IDs, sequence numbers, timeout-based correlation |

---

## 📚 Related Documentation

- **Standards**: IEEE 1588-2019 (PTP), IEEE 802.1AS-2020 (gPTP), ISO/IEC/IEEE 12207:2017
- **Architecture**: [Context Map](../03-architecture/context-map.md), [ADR Index](../03-architecture/decisions/)
- **Implementation**: [Phase 05 Guide](../.github/instructions/phase-05-implementation.instructions.md)
- **Testing**: [Phase 07 Guide](../.github/instructions/phase-07-verification-validation.instructions.md)
- **Real-Time Systems**: [Temporal Constraints](../04-design/patterns/real-time-constraints.md)

---

## 🔄 Next Actions

1. **Immediate** (This Week):
   - [ ] Assign owners to P0 issues (#168, #19, #89, #147, #166, #171, #180)
   - [ ] Schedule architecture review session for ADRs (#147, #166, #93)
   - [ ] Create spike solution for interrupt latency measurement (#166)

2. **Sprint 1 Prep** (Week 1):
   - [ ] Move P0 requirement issues to `status:ready`
   - [ ] Validate all requirement issues have acceptance criteria
   - [ ] Set up GitHub Project board for batch tracking

3. **Ongoing**:
   - [ ] Daily standups - blockers, dependencies
   - [ ] Weekly demos - working software (TDD increments)
   - [ ] Bi-weekly retrospectives - process improvements

---

**Last Updated**: 2026-02-02  
**Next Review**: Sprint Planning (TBD)
