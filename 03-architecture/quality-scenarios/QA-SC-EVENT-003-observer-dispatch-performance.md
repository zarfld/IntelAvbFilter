# QA-SC-EVENT-003: Observer Dispatch Performance

**Issue**: #[TBD]  
**Type**: Quality Attribute Scenario (QA-SC)  
**Standard**: ISO/IEC/IEEE 42010:2011 (Architecture Description)  
**Related ADR**: #163 (Observer Pattern for Event Distribution)  
**Phase**: 03 - Architecture Design

## Quality Attribute

**Attribute**: Performance (Execution Time)  
**Stimulus**: PTP event ready for dispatch to registered observers  
**Response**: All observers notified sequentially  
**Response Measure**: Dispatch time < 100µs per observer (99th percentile)

## Scenario Description

### Context
When a PTP hardware event occurs (Target Time match), the DPC must iterate through all registered observers and invoke their callbacks. Dispatch performance directly impacts event latency and system responsiveness.

### Stakeholder Concern
**Stakeholder**: Application Developers  
**Concern**: Observer dispatch overhead must be minimal to maintain deterministic event latency, especially when multiple observers are registered for the same event type.

## ATAM Scenario Structure (ISO/IEC/IEEE 42010)

| Element | Description |
|---------|-------------|
| **Source** | DPC context after ISR queued PTP event |
| **Stimulus** | Event ready for dispatch to 3 registered observers |
| **Artifact** | Observer Registration Infrastructure (iterator + callback invoker) |
| **Environment** | Windows Kernel DISPATCH_LEVEL, observer list with 3 entries |
| **Response** | All 3 observers notified with event data |
| **Response Measure** | **Dispatch time: < 100µs per observer (total < 300µs for 3)** |

## Architectural Tactics

### Tactic 1: Pre-Allocated Observer List
**Category**: Performance - Avoid Allocation  
**Implementation**:
- Observer list pre-allocated at registration time
- No dynamic memory allocation during dispatch
- Intrusive linked list (CONTAINING_RECORD pattern)

**Expected Impact**: Zero heap allocation overhead in hot path

### Tactic 2: Lockless Read (RCU-like)
**Category**: Performance - Concurrency  
**Implementation**:
- Observer list protected by read-copy-update pattern
- DPC reads observer list without acquiring lock
- Registration/unregistration uses write lock only

**Expected Impact**: Zero lock contention during dispatch

### Tactic 3: Sequential Iteration (No Parallelism)
**Category**: Simplicity over Complexity  
**Implementation**:
- Observers called sequentially (not parallel)
- Deterministic execution order (FIFO registration order)
- Predictable latency characteristics

**Expected Impact**: Linear scaling with observer count (predictable)

## Measurement Strategy

### Instrumentation Points
```c
typedef struct _OBSERVER_DISPATCH_METRICS {
    LARGE_INTEGER DispatchStartTime;
    ULONG ObserverCount;
    
    struct {
        PVOID ObserverContext;
        LARGE_INTEGER CallbackStartTime;
        LARGE_INTEGER CallbackEndTime;
        LONGLONG CallbackDuration;  // µs
    } ObserverMetrics[MAX_OBSERVERS];
    
    LARGE_INTEGER DispatchEndTime;
    LONGLONG TotalDispatchTime;  // µs
} OBSERVER_DISPATCH_METRICS;
```

### Test Procedure
1. Register 3 test observers with minimal callback implementations
2. Trigger PTP event (TSAUXC Target Time 0)
3. Measure dispatch times using QueryPerformanceCounter
4. Collect 100 samples
5. Calculate per-observer and total dispatch times

### Pass Criteria
- **Per-observer dispatch: < 100µs (99th percentile)**
- Total dispatch time < 300µs for 3 observers
- Dispatch overhead (iteration logic) < 10µs
- Linear scaling: 10 observers → <1ms total

## Tradeoffs and Risks

### Tradeoff 1: Sequential vs. Parallel Dispatch
**Decision**: Sequential dispatch  
**Justification**:
  - Deterministic execution order
  - Simpler synchronization (no parallel callback issues)
  - Predictable latency characteristics
**Alternative Rejected**: Parallel dispatch with work queues  
**Reason**: Complexity outweighs benefits (observer callbacks are <100µs each)

### Tradeoff 2: Lock-Free Read vs. Full Locking
**Decision**: Lock-free read during dispatch  
**Justification**: Avoids lock contention in hot path  
**Risk**: Observer may be unregistered during dispatch  
**Mitigation**: Reference counting + RCU-like grace period

## Observer Contract (Performance Requirements)

Observers must adhere to strict performance contract:

```c
// Observer Callback Contract
VOID PtpObserverCallback(
    _In_ PVOID Context,
    _In_ PPTP_EVENT Event
)
/**
 * Performance Requirements:
 *   - MUST complete in < 100µs (99th percentile)
 *   - MUST NOT block (no waits, no locks with contention)
 *   - MUST NOT allocate memory (use pre-allocated buffers)
 *   - Executes at DISPATCH_LEVEL (cannot page fault)
 * 
 * Violations:
 *   - Exceeding 100µs → Event latency SLA violated
 *   - Blocking → System deadlock risk
 *   - Memory allocation → Potential failure + overhead
 */
{
    // Minimal processing only
    // Complex work must be queued to background thread
}
```

## Verification

### Unit Test
- **Test**: Mock observer with instrumented callbacks
- **Verifies**: Dispatch overhead < 10µs (excluding callback execution)
- **Validates**: Iteration logic is minimal

### Performance Test
- **Test**: 3 observers with 50µs sleep-equivalent callbacks
- **Expected**: Total dispatch ~160µs (10µs overhead + 3×50µs callbacks)
- **Pass**: < 200µs actual

### Stress Test
- **Test**: 10 observers with 50µs callbacks
- **Expected**: ~510µs (10µs overhead + 10×50µs)
- **Pass**: Linear scaling confirmed

### Boundary Test
- **Test**: Observer callback violates contract (sleeps 10ms)
- **Expected**: Dispatch time = 10ms (no crash, but SLA violated)
- **Validation**: Logging/diagnostic warning emitted

## Optimization Opportunities

### Fast Path for Single Observer
```c
if (ObserverCount == 1) {
    // Fast path: Direct callback (no iteration)
    SingleObserver->Callback(Event);
} else {
    // Slow path: Iterate observer list
    for_each_observer(observer) {
        observer->Callback(Event);
    }
}
```
**Expected Gain**: ~5µs saved for single-observer case

### Inline Callback for Kernel Observers
```c
// Direct call (no function pointer indirection)
if (observer->Type == KERNEL_OBSERVER) {
    observer->Callback(Event);  // Inlined by compiler
}
```
**Expected Gain**: ~2µs per callback (reduced instruction cache misses)

## Related Requirements

**Satisfies**:
- #165 (REQ-F-EVENT-003: Deliver events via observer callbacks)
- #162 (REQ-NF-EVENT-001: Event latency < 1ms)

**Depends On**:
- #163 (ADR-EVENT-001: Observer Pattern for Event Distribution)
- #172 (ARC-C-EVENT-002: Observer Registration Infrastructure)

## Architectural Views Affected

- **Logical View**: Observer pattern sequence showing dispatch loop
- **Process View**: DPC → Observer callback execution timeline
- **Development View**: Observer manager component interface

## Sensitivity Points

1. **Observer Count**: Performance degrades linearly with observer count
2. **Observer Callback Duration**: Misbehaving observer can violate SLA for all
3. **Lock Contention**: Registration during dispatch can introduce contention

## Non-Risks

- **Memory Overhead**: Observer list is small (<1KB for 10 observers)
- **Cache Misses**: Observer list fits in L1 cache (sequential access)

## Acceptance Criteria

- [ ] Dispatch instrumentation implemented
- [ ] 100-sample performance test passes
- [ ] Per-observer dispatch < 100µs (99th percentile)
- [ ] Linear scaling test passes (1, 3, 10 observers)
- [ ] Observer contract documented in API header
- [ ] Diagnostic warning for slow observers implemented
- [ ] Architecture views updated with dispatch sequence

---

**Status**: Backlog  
**Priority**: P1 (High)  
**Estimated Effort**: 3 hours (instrumentation + testing)  
**Dependencies**: #163, #172 implementation complete

**Version**: 1.0  
**Last Updated**: 2025-12-17  
**Next Review**: After Phase 05 implementation
