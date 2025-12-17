# QA-SC-EVENT-001: PTP Event Latency Performance

**Issue**: #[TBD]  
**Type**: Quality Attribute Scenario (QA-SC)  
**Standard**: ISO/IEC/IEEE 42010:2011 (Architecture Description)  
**Related ADR**: #166 (Hardware Interrupt-Driven Events)  
**Phase**: 03 - Architecture Design

## Quality Attribute

**Attribute**: Performance (Latency)  
**Stimulus**: PTP hardware interrupt triggered (TSAUXC.TT0 event)  
**Response**: Event delivered to registered observer callback  
**Response Measure**: Latency < 1 millisecond (99th percentile)

## Scenario Description

### Context
AVB/TSN applications require precise time synchronization monitoring. The PTP event subsystem must deliver hardware timer events (Target Time 0, Target Time 1) to user-space observers with minimal latency to enable real-time network scheduling decisions.

### Stakeholder Concern
**Stakeholder**: Real-Time Application Developers  
**Concern**: PTP event latency must be deterministic and below application deadlines for time-critical network scheduling (e.g., stream gate control, traffic shaping).

## ATAM Scenario Structure (ISO/IEC/IEEE 42010)

| Element | Description |
|---------|-------------|
| **Source** | Intel I210/I225 NIC hardware timer (TSAUXC register interrupt) |
| **Stimulus** | Target Time 0 or Target Time 1 match triggers hardware interrupt |
| **Artifact** | PTP Event Subsystem (ISR → DPC → Observer callback chain) |
| **Environment** | Windows Kernel (IRQL DISPATCH_LEVEL), Real-time TSN application running |
| **Response** | Event delivered to registered observer callback with timestamp |
| **Response Measure** | **Latency: Interrupt trigger → Observer callback < 1ms (99th percentile)** |

## Architectural Tactics

### Tactic 1: Hardware Interrupt Priority (DIRQL)
**Category**: Performance - Reduce Latency  
**Implementation**:
- ISR executes at DIRQL (Device IRQ Level)
- Minimal ISR processing (read timestamp, queue DPC)
- DPC scheduled immediately after ISR completion

**Expected Impact**: ISR latency < 5µs (hardware read + queue DPC)

### Tactic 2: Lockless Event Queue
**Category**: Performance - Concurrency  
**Implementation**:
- Circular buffer with atomic head/tail pointers
- No spinlocks in ISR path
- Single-producer (ISR), single-consumer (DPC) pattern

**Expected Impact**: Zero contention delay in ISR

### Tactic 3: Direct Observer Callback (No Context Switch)
**Category**: Performance - Reduce Overhead  
**Implementation**:
- Observer callback invoked directly from DPC context
- No user-mode transition required for kernel observers
- Callback executes at DISPATCH_LEVEL

**Expected Impact**: DPC → Callback latency < 10µs

## Measurement Strategy

### Instrumentation Points
1. **T0**: TSAUXC interrupt trigger (hardware timestamp)
2. **T1**: ISR entry (KdQueryPerformanceCounter)
3. **T2**: DPC entry
4. **T3**: Observer callback entry
5. **T4**: Observer callback exit

### Metrics Collected
```c
typedef struct _PTP_EVENT_LATENCY_METRICS {
    LARGE_INTEGER InterruptTriggerTime;  // T0 (from TSAUXC)
    LARGE_INTEGER IsrEntryTime;          // T1
    LARGE_INTEGER DpcEntryTime;          // T2
    LARGE_INTEGER CallbackEntryTime;     // T3
    LARGE_INTEGER CallbackExitTime;      // T4
    
    // Derived metrics
    LONGLONG IsrLatency;      // T1 - T0 (hardware latency)
    LONGLONG DpcLatency;      // T2 - T1 (ISR → DPC)
    LONGLONG CallbackLatency; // T3 - T2 (DPC → Callback)
    LONGLONG TotalLatency;    // T3 - T0 (End-to-end)
} PTP_EVENT_LATENCY_METRICS;
```

### Test Procedure
1. Configure TSAUXC Target Time 0 to trigger every 10ms
2. Register test observer callback with latency instrumentation
3. Collect 1000 event samples
4. Calculate percentiles: 50th, 90th, 95th, 99th, max

### Pass Criteria
- **99th percentile total latency < 1ms**
- Mean ISR latency < 10µs
- Mean DPC latency < 50µs
- Mean callback latency < 20µs
- Zero dropped events (all events delivered)

## Tradeoffs and Risks

### Tradeoff 1: Latency vs. Throughput
**Decision**: Optimize for low latency over high throughput  
**Justification**: TSN applications prioritize deterministic timing over event rate  
**Risk Mitigation**: Event overflow detection (see QA-SC-EVENT-004)

### Tradeoff 2: Direct Callback vs. Queued Delivery
**Decision**: Direct callback from DPC context  
**Justification**: Eliminates context switch overhead (no user-mode IRP)  
**Risk**: Observer must execute quickly at DISPATCH_LEVEL  
**Mitigation**: Observer contract specifies <100µs execution limit

## Verification

### Unit Test
- **Test**: TEST-EVENT-001 (Mock hardware interrupt, measure latency)
- **Verifies**: #165 (REQ-F-EVENT-003: Deliver PTP events via observer callback)

### Integration Test
- **Test**: Actual hardware TSAUXC interrupt on I210 NIC
- **Measure**: End-to-end latency with oscilloscope + GPIO toggling
- **Pass Criteria**: 99th percentile < 1ms

### Acceptance Test
- **Test**: Real TSN application (stream gate controller)
- **Pass Criteria**: Application meets scheduling deadlines with PTP events

## Related Requirements

**Satisfies**:
- #165 (REQ-F-EVENT-003: Deliver PTP events via observer callback)
- #162 (REQ-NF-EVENT-001: Event latency < 1ms)

**Depends On**:
- #166 (ADR-EVENT-002: Hardware Interrupt-Driven Event Model)
- #172 (ARC-C-EVENT-002: Observer Registration Infrastructure)

## Architectural Views Affected

- **Logical View**: Observer pattern interaction diagram
- **Process View**: ISR → DPC → Callback sequence diagram
- **Physical View**: Interrupt routing (NIC → CPU → MSI-X)
- **Development View**: Event subsystem component dependencies

## Sensitivity Points

1. **DPC Priority**: DPC must have high priority to avoid preemption
2. **Interrupt Affinity**: Bind interrupt to specific CPU core for consistency
3. **Observer Execution Time**: Callback must complete quickly (<100µs)

## Non-Risks

- **Polling Overhead**: Not applicable (hardware interrupt-driven)
- **User-Mode Transition**: Not required (kernel observer callbacks)

## Acceptance Criteria

- [ ] Latency instrumentation implemented
- [ ] 1000-sample test harness created
- [ ] 99th percentile latency < 1ms measured
- [ ] TEST-EVENT-001 passes with instrumentation
- [ ] Integration test with real hardware passes
- [ ] Architecture views updated with latency annotations

---

**Status**: Backlog  
**Priority**: P0 (Critical)  
**Estimated Effort**: 4 hours (instrumentation + testing)  
**Dependencies**: #166, #172 implementation complete

**Version**: 1.0  
**Last Updated**: 2025-12-17  
**Next Review**: After Phase 05 implementation
