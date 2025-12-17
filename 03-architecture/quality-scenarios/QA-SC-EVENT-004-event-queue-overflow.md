# QA-SC-EVENT-004: Event Queue Overflow Handling

**Issue**: #[TBD]  
**Type**: Quality Attribute Scenario (QA-SC)  
**Standard**: ISO/IEC/IEEE 42010:2011 (Architecture Description)  
**Related ADR**: #166 (Hardware Interrupt-Driven Events)  
**Phase**: 03 - Architecture Design

## Quality Attribute

**Attribute**: Reliability (Graceful Degradation)  
**Stimulus**: High-frequency PTP events exceed processing capacity  
**Response**: System detects overflow, drops oldest events, logs diagnostic  
**Response Measure**: No crash, no data corruption, overflow logged with count

## Scenario Description

### Context
Under extreme load (e.g., TSAUXC configured for 1ms intervals with slow observer callbacks), the event queue may fill faster than the DPC can drain it. The system must handle overflow gracefully without crashing or corrupting memory.

### Stakeholder Concern
**Stakeholder**: System Reliability Engineers  
**Concern**: Event queue overflow must not cause system crashes or undefined behavior. System must remain stable and provide diagnostic information about overflow conditions.

## ATAM Scenario Structure (ISO/IEC/IEEE 42010)

| Element | Description |
|---------|-------------|
| **Source** | TSAUXC hardware timer generating events every 100µs (10,000 events/sec) |
| **Stimulus** | DPC processing slower than event arrival rate (backlog accumulates) |
| **Artifact** | Event Queue (circular buffer between ISR and DPC) |
| **Environment** | High system load, slow observer callbacks (>100µs each) |
| **Response** | Oldest events dropped, overflow counter incremented, diagnostic event emitted |
| **Response Measure** | **No crash, no corruption, overflow count = dropped events, diagnostic log entry created** |

## Architectural Tactics

### Tactic 1: Bounded Queue with Drop-Oldest Policy
**Category**: Reliability - Fault Tolerance  
**Implementation**:
- Fixed-size circular buffer (64 entries)
- ISR checks queue full condition before write
- On overflow: Drop oldest event, increment overflow counter

**Expected Impact**: Bounded memory usage, deterministic overflow behavior

### Tactic 2: Overflow Diagnostics
**Category**: Reliability - Fault Detection  
**Implementation**:
- Atomic overflow counter (InterlockedIncrement)
- Diagnostic event emitted to ETW on first overflow
- Periodic logging (throttled to 1 event/second max)

**Expected Impact**: Observability into overflow conditions

### Tactic 3: Graceful Degradation (No Crash)
**Category**: Reliability - Fault Isolation  
**Implementation**:
- No ASSERT or BSOD on overflow
- ISR returns success even when queue full
- System continues operating (drops events but stays stable)

**Expected Impact**: High availability under load

## Event Queue Design

### Data Structure
```c
#define EVENT_QUEUE_SIZE 64  // Power of 2 for efficient masking

typedef struct _PTP_EVENT_QUEUE {
    PTP_EVENT Events[EVENT_QUEUE_SIZE];
    
    volatile LONG Head;       // Atomic: Producer (ISR) writes here
    volatile LONG Tail;       // Atomic: Consumer (DPC) reads here
    volatile LONG OverflowCount;
    
    BOOLEAN OverflowLogged;   // Throttle diagnostic logging
    LARGE_INTEGER LastOverflowTime;
} PTP_EVENT_QUEUE, *PPTP_EVENT_QUEUE;
```

### Overflow Detection Logic
```c
BOOLEAN PtpEnqueueEvent(
    _Inout_ PPTP_EVENT_QUEUE Queue,
    _In_ PPTP_EVENT Event
)
{
    LONG head = Queue->Head;
    LONG tail = Queue->Tail;
    LONG next_head = (head + 1) & (EVENT_QUEUE_SIZE - 1);
    
    // Check for overflow
    if (next_head == tail) {
        // Queue full: Drop oldest event
        InterlockedIncrement(&Queue->OverflowCount);
        
        // Log diagnostic (throttled)
        if (!Queue->OverflowLogged) {
            TraceEvents(TRACE_LEVEL_WARNING,
                "PTP event queue overflow: dropping oldest event");
            Queue->OverflowLogged = TRUE;
        }
        
        // Advance tail to drop oldest event
        Queue->Tail = (tail + 1) & (EVENT_QUEUE_SIZE - 1);
    }
    
    // Write event
    Queue->Events[head] = *Event;
    Queue->Head = next_head;
    
    return TRUE;  // Never fails (graceful degradation)
}
```

## Measurement Strategy

### Test Procedure: Overflow Induction
```c
// Step 1: Configure fast event generation
TSAUXC_CONFIG config = {
    .TargetTime0Interval = 100us,  // 10,000 events/sec
};

// Step 2: Register slow observer
RegisterPtpObserver(&SlowObserver);

void SlowObserver(PVOID Context, PPTP_EVENT Event) {
    // Intentionally slow callback (1ms)
    KeDelayExecutionThread(KernelMode, FALSE, &OneMill isecond);
}

// Step 3: Run for 10 seconds
Sleep(10000);

// Step 4: Check overflow metrics
ULONG dropped = ReadOverflowCount();
ULONG delivered = ReadDeliveredCount();

// Expected: 
//   - Delivered: ~10,000 events (DPC can process 1000/sec)
//   - Dropped: ~90,000 events (overflow due to slow observer)
//   - No crash or corruption
```

### Pass Criteria
- **No BSOD or crash under overflow**
- Overflow counter accurately reflects dropped events
- ETW diagnostic event emitted on first overflow
- Delivered events have correct data (no corruption)
- System remains responsive after overflow cleared

## Tradeoffs and Risks

### Tradeoff 1: Drop-Oldest vs. Drop-Newest
**Decision**: Drop oldest events  
**Justification**: Deliver most recent data (stale data less useful)  
**Alternative**: Drop newest events (preserve history)  
**Reason**: Real-time systems prioritize current state over history

### Tradeoff 2: Fail-Fast vs. Graceful Degradation
**Decision**: Graceful degradation (drop events, continue operating)  
**Justification**: High availability > event completeness  
**Alternative**: ASSERT and crash (fail-fast)  
**Reason**: TSN systems require continuous operation

### Tradeoff 3: Queue Size (64 entries)
**Decision**: 64-entry queue (fixed size)  
**Justification**: Balance memory vs. overflow tolerance  
**Analysis**:
  - At 10ms event interval: 64 events = 640ms backlog tolerance
  - At 1ms event interval: 64 events = 64ms backlog tolerance
  - Sufficient for normal DPC scheduling jitter (<10ms)

## Overflow Recovery

### Automatic Recovery
```c
// DPC drains queue faster than events arrive
while (!QueueEmpty()) {
    DequeueAndDispatchEvent();
}

// Once caught up, overflow counter stops incrementing
// No manual intervention required
```

### Manual Recovery (Diagnostic)
```powershell
# Check overflow status
Get-EventQueueStatus
# Output: OverflowCount=1523, CurrentDepth=0

# Reset overflow counter (diagnostic only)
Reset-EventQueueOverflowCounter
```

## Related Requirements

**Satisfies**:
- #164 (REQ-F-EVENT-004: Emit diagnostic events for overflow)
- #162 (REQ-NF-EVENT-001: Graceful degradation under load)

**Depends On**:
- #166 (ADR-EVENT-002: Hardware Interrupt-Driven Events)
- #171 (ARC-C-EVENT-001: PTP Event Handler with queue)

## Architectural Views Affected

- **Logical View**: Event queue component with overflow logic
- **Process View**: ISR overflow handling sequence
- **Data View**: Event queue structure with overflow counter

## Sensitivity Points

1. **Queue Size**: Smaller queue → more frequent overflows
2. **Event Rate**: Higher rate → increased overflow probability
3. **Observer Callback Duration**: Slower callbacks → higher queue depth

## Monitoring and Diagnostics

### ETW Event
```c
EventWriteEventQueueOverflow(
    EventProvider,
    TotalDropped,
    CurrentQueueDepth,
    EventRate
);
```

### Performance Counter
```
\IntelAvbFilter\Events Dropped/sec
\IntelAvbFilter\Event Queue Depth
```

### WMI Property
```
Get-CimInstance -Namespace root\wmi -ClassName IntelAvbFilter_EventQueue
# Properties: OverflowCount, CurrentDepth, MaxDepthSinceReset
```

## Acceptance Criteria

- [ ] Overflow handling implemented (drop-oldest policy)
- [ ] Overflow counter implemented (atomic increment)
- [ ] ETW diagnostic event implemented
- [ ] Overflow induction test passes (no crash)
- [ ] Overflow counter accuracy validated
- [ ] Performance counters exposed
- [ ] Architecture views show overflow paths

---

**Status**: Backlog  
**Priority**: P1 (High)  
**Estimated Effort**: 4 hours (implementation + testing)  
**Dependencies**: #166, #171 implementation complete

**Version**: 1.0  
**Last Updated**: 2025-12-17  
**Next Review**: After Phase 05 implementation
