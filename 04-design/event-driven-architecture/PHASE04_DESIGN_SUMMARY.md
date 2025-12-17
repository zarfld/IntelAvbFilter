# Phase 04: Detailed Design Summary - Event-Driven Architecture

**Date**: 2025-12-16  
**Status**: Complete  
**Standards**: IEEE 1016-2009 (Software Design Descriptions)

## Overview

This document summarizes the detailed design artifacts for the event-driven architecture supporting PTP/gPTP, AVTP, and ATDECC timing events.

## Design Artifacts Created

### Architecture Components (ARC-C)

| Issue | Component | Description | Priority |
|-------|-----------|-------------|----------|
| [#172](https://github.com/zarfld/IntelAvbFilter/issues/172) | Event Subject and Observer Infrastructure | Core Observer Pattern implementation | P0 |
| [#171](https://github.com/zarfld/IntelAvbFilter/issues/171) | PTP Hardware Timestamp Event Handler | Hardware interrupt-driven timestamp capture | P0 |
| [#173](https://github.com/zarfld/IntelAvbFilter/issues/173) | AVTP Stream Event Monitor | TU bit and diagnostic event monitoring | P1 |
| [#170](https://github.com/zarfld/IntelAvbFilter/issues/170) | ATDECC Event Dispatcher | Unsolicited notification handling | P1 |

### Test Cases (TEST)

| Issue | Test Case | Requirements Verified | Priority |
|-------|-----------|----------------------|----------|
| [#174](https://github.com/zarfld/IntelAvbFilter/issues/174) | PTP Timestamp Event Latency | #168, #165, #161 | P0 |
| [#175](https://github.com/zarfld/IntelAvbFilter/issues/175) | AVTP TU Bit Change Events | #169 | P0 |
| [#176](https://github.com/zarfld/IntelAvbFilter/issues/176) | ATDECC Unsolicited Notifications | #162 | P1 |
| [#177](https://github.com/zarfld/IntelAvbFilter/issues/177) | AVTP Diagnostic Counter Events | #164 | P1 |
| [#178](https://github.com/zarfld/IntelAvbFilter/issues/178) | Event Notification Latency (< 1μs) | #165 | P0 |
| [#179](https://github.com/zarfld/IntelAvbFilter/issues/179) | Zero Polling Overhead | #161 | P0 |

## Traceability Matrix

### Requirements → Architecture → Design → Tests

```
StR #167: Event-Driven TSN Monitoring
│
├─ REQ-F #168: PTP Timestamp Events
│  ├─ ADR #166: Hardware Interrupt-Driven Capture
│  │  └─ ARC-C #171: PTP Timestamp Event Handler
│  │     └─ TEST #174: PTP Timestamp Latency Test
│  │
│  └─ ADR #163: Observer Pattern
│     └─ ARC-C #172: Event Subject/Observer Infrastructure
│        └─ TEST #174, #178, #179
│
├─ REQ-F #169: AVTP TU Bit Events
│  └─ ADR #163: Observer Pattern
│     └─ ARC-C #173: AVTP Stream Monitor
│        └─ TEST #175: TU Bit Change Test
│
├─ REQ-F #162: ATDECC AEN Events
│  └─ ADR #163: Observer Pattern
│     └─ ARC-C #170: ATDECC Event Dispatcher
│        └─ TEST #176: ATDECC AEN Test
│
├─ REQ-F #164: AVTP Diagnostic Events
│  └─ ADR #163: Observer Pattern
│     └─ ARC-C #173: AVTP Stream Monitor
│        └─ TEST #177: Diagnostic Counter Test
│
├─ REQ-NF #165: Event Latency < 1μs
│  └─ ADR #166: Hardware Interrupts
│     └─ ARC-C #171, #172
│        └─ TEST #178: Latency Measurement
│
└─ REQ-NF #161: Zero Polling
   └─ ADR #166: Hardware Interrupts
      └─ ARC-C #171, #172
         └─ TEST #179: Zero Polling Verification
```

## Design Highlights

### 1. Observer Pattern Infrastructure (#172)

**Key Interfaces**:
```c
typedef VOID (*PFN_EVENT_CALLBACK)(PVOID Context, EVENT_TYPE Type, PVOID EventData);

typedef struct _EVENT_OBSERVER {
    LIST_ENTRY ListEntry;
    PFN_EVENT_CALLBACK OnEvent;
    PVOID Context;
    ULONG Priority;
} EVENT_OBSERVER;

NTSTATUS RegisterObserver(EVENT_SUBJECT* Subject, EVENT_OBSERVER* Observer, ULONG Priority);
VOID NotifyObservers(EVENT_SUBJECT* Subject, EVENT_TYPE Type, PVOID EventData);
```

**Design Decisions**:
- Priority-based observer ordering (0 = highest, 255 = lowest)
- Thread-safe registration/deregistration (KSPIN_LOCK)
- DISPATCH_LEVEL operation (no blocking)
- Target: < 200ns overhead per observer

**Zero Polling Scope Clarification**:
- **Critical Timing Paths**: PTP timestamp capture, AVTP stream monitoring, and event notification use hardware interrupts with zero polling overhead
- **Non-Critical Auxiliary Operations**: MDIO register access (PHY configuration) uses polling with 2-10 µs overhead, which is acceptable for rare, low-speed operations that are not timing-critical
- **Rationale**: Managing interrupts for slow auxiliary operations adds complexity without performance benefit; polling is simpler and sufficient for non-real-time paths

### 2. PTP Hardware Timestamp Handler (#171)

**Timing Budget**:
| Stage | Target Latency | Measurement |
|-------|----------------|-------------|
| HW capture → IRQ | < 100 ns | Hardware spec |
| IRQ → ISR entry | < 200 ns | OS metric |
| ISR execution | < 500 ns | GPIO toggle |
| DPC → Observer | < 500 ns | GPIO toggle |
| **Total** | **< 1.3 μs** | **Oscilloscope** |

**Key Design**:
```c
BOOLEAN PtpTimestampISR(NDIS_HANDLE InterruptContext, PULONG TargetProcessors) {
    // ~50ns: Read interrupt status
    TsIsr = ReadRegister(Adapter, I210_TSISR);
    
    // ~50ns: Acknowledge interrupt
    WriteRegister(Adapter, I210_TSISR, TsIsr);
    
    // ~100ns: Queue DPC
    NdisMQueueDpcEx(Adapter->InterruptHandle, 0, TargetProcessors);
    
    return TRUE;
    // Total: ~200ns (within 500ns budget)
}
```

### 3. AVTP Stream Monitor (#173)

**Monitoring Capabilities**:
- TU bit state tracking (0→1, 1→0 transitions)
- Timestamp validation (late/early detection)
- Sequence number continuity checking
- Per-stream diagnostic counters

**Data Structures**:
```c
typedef struct _AVTP_STREAM_STATE {
    UINT64 StreamId;
    BOOLEAN CurrentTuState;
    BOOLEAN PreviousTuState;
    UINT8 LastSequenceNumber;
    UINT32 LateTimestampCount;
    UINT32 EarlyTimestampCount;
    UINT32 PlayoutBufferWindow;  // Microseconds
} AVTP_STREAM_STATE;
```

### 4. ATDECC Event Dispatcher (#170)

**Supported AEN Types**:
- IDENTIFY_NOTIFICATION (identify button)
- CONTROLLER_AVAILABLE (discovery)
- ENTITY_AVAILABLE (discovery)
- gPTP_GRANDMASTER_CHANGED
- STREAM_FORMAT_CHANGED
- COUNTERS_VALID

**Message Flow**:
```
Trigger (button, state change)
  → Construct ATDECC_AEN_EVENT
  → NotifyObservers (internal observers)
  → SendAtdeccMessage (to all registered controllers)
```

## Testing Strategy

### Unit Tests
- Observer registration/deregistration
- Event data construction
- Interrupt acknowledgment
- TU bit extraction
- Sequence validation

### Integration Tests
- End-to-end event flow (hardware → observer)
- Multi-stream monitoring
- Multi-controller notification
- Stress testing (high event rates)

### Performance Tests
- **Latency measurement** (oscilloscope + GPIO)
- **Zero polling verification** (CPU profiler)
- **Jitter analysis** (statistical)
- **Load testing** (1600 events/second)

### Compliance Tests
- **IEEE 802.1AS**: PTP timestamp handling
- **IEEE 1722**: AVTP stream monitoring
- **IEEE 1722.1**: ATDECC message format
- **Milan**: TU bit response, latency requirements

## Implementation Readiness

### Phase 04 Exit Criteria
- ✅ All ARC-C issues created with detailed designs
- ✅ Class diagrams and data structures specified
- ✅ Interfaces documented (C function prototypes)
- ✅ Sequence diagrams for key flows
- ✅ All TEST issues created with procedures
- ✅ Traceability established (REQ → ADR → ARC-C → TEST)
- ✅ Performance budgets defined and allocated
- ✅ Zero polling scope clarified (critical paths only; MDIO excluded)

### Design Review Approval

**Status**: ✅ **APPROVED for Phase 05 Implementation**

**Review Date**: 2025-12-17

**Key Findings**:
- **Architectural Soundness**: Event-driven core (Observer Pattern) correctly replaces polling on critical paths
- **Standards Integration**: Tight alignment with IEEE 802.1AS (PTP), IEEE 1722 (AVTP), IEEE 1722.1 (ATDECC)
- **Testing Strategy**: Rigorous TDD approach with hardware-validated latency measurements
- **Traceability**: Complete chain from StR → REQ → ADR → ARC-C → TEST

**Clarification Applied**:
- Zero polling overhead applies to **critical timing paths** (PTP timestamps, AVTP events)
- MDIO operations (PHY configuration) use polling with 2-10 µs overhead (acceptable for rare, non-critical operations)
- Documentation updated to reflect this scope limitation

### Ready for Phase 05 (Implementation)
- **TDD Workflow**: Each TEST issue provides acceptance criteria
- **Design Constraints**: Timing budgets, IRQL levels, concurrency documented
- **Interface Contracts**: All public APIs defined
- **Error Handling**: Scenarios and mitigations specified

## Next Steps

### Phase 05: Implementation (TDD Approach)

1. **For each TEST issue**:
   - Write failing test first (Red)
   - Implement minimal code to pass (Green)
   - Refactor while keeping tests green
   - Measure performance against budget

2. **Implementation Order** (by priority):
   1. ARC-C #172: Event Subject/Observer Infrastructure (P0)
   2. ARC-C #171: PTP Timestamp Handler (P0)
   3. ARC-C #173: AVTP Stream Monitor (P1)
   4. ARC-C #170: ATDECC Event Dispatcher (P1)

3. **Verification**:
   - Run all tests after each component
   - Measure latency with oscilloscope
   - Verify zero polling with CPU profiler
   - Validate traceability chain

## Standards Compliance

### IEEE 1016-2009 (Software Design Descriptions)
- ✅ Design viewpoints documented (interface, behavior, data)
- ✅ Design concerns addressed (performance, reliability, security)
- ✅ Design rationale provided (ADRs)
- ✅ Traceability to requirements maintained

### ISO/IEC/IEEE 42010:2011 (Architecture Description)
- ✅ Architecture decisions documented (ADR #163, #166)
- ✅ Stakeholder concerns addressed (latency, determinism)
- ✅ Architecture views provided (component, sequence diagrams)

## Document Control

- **Version**: 1.0
- **Author**: Standards Compliance Team
- **Approved By**: [Pending Review]
- **Last Updated**: 2025-12-16

## References

- [#167](https://github.com/zarfld/IntelAvbFilter/issues/167) - StR-EVENT-001: Event-Driven TSN Monitoring
- [#163](https://github.com/zarfld/IntelAvbFilter/issues/163) - ADR-EVENT-001: Observer Pattern
- [#166](https://github.com/zarfld/IntelAvbFilter/issues/166) - ADR-EVENT-002: Hardware Interrupts
- IEEE 802.1AS-2020 - Timing and Synchronization
- IEEE 1722-2016 - Audio Video Transport Protocol
- IEEE 1722.1-2013 - Device Discovery, Connection Management
- AVNU Alliance Milan Protocol Stack Specification
- IEC 60802 - TSN Profile for Industrial Automation
