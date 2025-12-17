# QA-SC-EVENT-002: Zero CPU Polling Overhead

**Issue**: #[TBD]  
**Type**: Quality Attribute Scenario (QA-SC)  
**Standard**: ISO/IEC/IEEE 42010:2011 (Architecture Description)  
**Related ADR**: #166 (Hardware Interrupt-Driven Events)  
**Phase**: 03 - Architecture Design

## Quality Attribute

**Attribute**: Performance (Resource Utilization)  
**Stimulus**: No PTP events occurring (idle state)  
**Response**: Zero CPU cycles consumed by event subsystem  
**Response Measure**: CPU utilization = 0% during idle periods

## Scenario Description

### Context
Traditional event systems use polling loops that continuously check hardware registers, consuming CPU cycles even when no events occur. The PTP event subsystem must avoid polling overhead to preserve CPU resources for application workloads.

### Stakeholder Concern
**Stakeholder**: System Administrators, Energy-Conscious Users  
**Concern**: Background polling loops waste CPU cycles and increase power consumption, especially on battery-powered embedded TSN devices.

## ATAM Scenario Structure (ISO/IEC/IEEE 42010)

| Element | Description |
|---------|-------------|
| **Source** | System idle state (no PTP events for extended period) |
| **Stimulus** | No TSAUXC interrupts triggered (no timer events) |
| **Artifact** | PTP Event Subsystem (driver background operations) |
| **Environment** | Windows system monitoring CPU usage (Task Manager, Performance Monitor) |
| **Response** | Event subsystem consumes zero CPU cycles |
| **Response Measure** | **CPU utilization: 0% when no events (measured over 60 seconds)** |

## Architectural Tactics

### Tactic 1: Hardware Interrupt-Driven (No Polling)
**Category**: Performance - Resource Management  
**Implementation**:
- No timer-based polling threads
- No periodic register reads in idle state
- Events triggered exclusively by hardware interrupts (MSI-X)

**Expected Impact**: Zero background CPU when idle

### Tactic 2: Passive ISR/DPC Model
**Category**: Performance - Reactive Processing  
**Implementation**:
- ISR executes only when hardware interrupt fires
- DPC scheduled only when ISR detects event
- No "check for events" background loops

**Expected Impact**: CPU scheduler does not execute driver code in idle state

### Tactic 3: Lazy Resource Cleanup
**Category**: Performance - Deferred Work  
**Implementation**:
- Event queue memory retained across idle periods
- No periodic "housekeeping" timers
- Cleanup occurs only on unregistration or driver unload

**Expected Impact**: No timer-driven callbacks during idle

## Measurement Strategy

### Instrumentation
1. **Windows Performance Monitor**:
   - Counter: `\Processor(_Total)\% Processor Time` for IntelAvbFilter.sys
   - Sample rate: 1 second
   - Duration: 60 seconds idle (no PTP events triggered)

2. **ETW (Event Tracing for Windows)**:
   - Provider: IntelAvbFilter driver
   - Events: ISR entry, DPC entry, Timer callbacks
   - Expected: Zero events during idle period

3. **CPU Profiler** (xperf/WPA):
   - Profile driver functions during 60-second idle
   - Expected: Zero samples attributed to IntelAvbFilter.sys

### Test Procedure
```powershell
# Step 1: Load driver and register observer
RegisterPtpObserver -EventType TargetTime0

# Step 2: Disable TSAUXC events (no interrupts)
DisablePtpTimer -TargetTime 0

# Step 3: Monitor CPU for 60 seconds
$perfCounter = "\Process(IntelAvbFilter)\% Processor Time"
Get-Counter -Counter $perfCounter -SampleInterval 1 -MaxSamples 60

# Expected: All samples = 0.00%

# Step 4: Verify with ETW
xperf -start DriverTrace -on IntelAvbFilter
Start-Sleep -Seconds 60
xperf -stop DriverTrace -d trace.etl

# Expected: No ISR/DPC events in trace
```

### Pass Criteria
- **CPU utilization: 0.00% during 60-second idle**
- Zero ISR executions (ETW trace)
- Zero DPC executions (ETW trace)
- Zero timer callbacks (ETW trace)
- No unexpected wake-ups in CPU profiler

## Tradeoffs and Risks

### Tradeoff 1: Polling vs. Interrupts
**Decision**: Pure interrupt-driven model (no hybrid)  
**Justification**: Eliminates polling overhead entirely  
**Alternative Rejected**: Periodic polling (1ms timer) with adaptive throttling  
**Reason**: Even 1ms timer = 1000 wake-ups/sec = measurable overhead

### Tradeoff 2: Immediate Cleanup vs. Lazy Cleanup
**Decision**: Lazy cleanup (retain resources during idle)  
**Justification**: Avoids periodic cleanup timers  
**Risk**: Slightly higher memory footprint during idle  
**Mitigation**: Event queue memory is minimal (<1KB per observer)

## Verification

### Unit Test
- **Test**: Mock "no events" scenario, verify zero timer registrations
- **Pass**: No KeSetTimer or IoStartTimer calls when idle

### Integration Test
- **Test**: Load driver, register observer, disable TSAUXC, monitor CPU
- **Pass**: 60-second idle period shows 0% CPU for IntelAvbFilter.sys

### Stress Test
- **Test**: Register 10 observers, leave system idle for 1 hour
- **Pass**: CPU remains 0% throughout hour

## Anti-Patterns Avoided

### Anti-Pattern 1: Periodic Status Check
```c
// ❌ WRONG: Polling loop
VOID PollingThread(PVOID Context) {
    while (TRUE) {
        CheckForPtpEvents();  // Wastes CPU even when no events
        KeDelayExecutionThread(KernelMode, FALSE, &OneMillisecond);
    }
}
```

### Anti-Pattern 2: "Adaptive" Polling
```c
// ❌ WRONG: Even adaptive polling has baseline overhead
if (NoEventsFor10Seconds) {
    PollingInterval = 100ms;  // Still 10 wake-ups/sec
} else {
    PollingInterval = 1ms;    // 1000 wake-ups/sec
}
```

### Correct Pattern: Pure Interrupt-Driven
```c
// ✅ CORRECT: Zero overhead when idle
NTSTATUS IsrHandler(PKINTERRUPT Interrupt, PVOID Context) {
    // Only executes when hardware fires interrupt
    // Zero CPU when no interrupts
}
```

## Related Requirements

**Satisfies**:
- #161 (REQ-NF-EVENT-002: Zero polling overhead)
- #162 (REQ-NF-EVENT-001: Low latency via interrupts)

**Depends On**:
- #166 (ADR-EVENT-002: Hardware Interrupt-Driven Event Model)
- #171 (ARC-C-EVENT-001: PTP Event Handler)

## Architectural Views Affected

- **Process View**: No background threads or timers shown
- **Physical View**: Hardware interrupt routing (no polling loops)
- **Development View**: No timer dependencies in component diagram

## Sensitivity Points

1. **ISR Registration**: Must use ConnectInterrupt (not timer-based polling)
2. **DPC Queueing**: Must be event-driven (not periodic)
3. **Resource Lifetime**: Must avoid periodic cleanup timers

## Non-Risks

- **Event Loss**: Interrupts are reliable (no events missed without polling)
- **Wake Latency**: Interrupt triggers immediately (no polling interval delay)

## Acceptance Criteria

- [ ] CPU monitoring test harness created
- [ ] 60-second idle test passes (0% CPU)
- [ ] ETW trace shows zero driver activity during idle
- [ ] CPU profiler shows zero samples during idle
- [ ] No KeSetTimer or IoStartTimer calls in event subsystem code paths
- [ ] Architecture views show interrupt-driven model (no polling threads)

---

**Status**: Backlog  
**Priority**: P1 (High)  
**Estimated Effort**: 2 hours (testing + verification)  
**Dependencies**: #166, #171 implementation complete

**Version**: 1.0  
**Last Updated**: 2025-12-17  
**Next Review**: After Phase 05 implementation
