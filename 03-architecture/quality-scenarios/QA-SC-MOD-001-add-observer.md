# QA-SC-MOD-001: Adding New Event Observer Type

**Scenario ID**: QA-SC-MOD-001  
**Quality Attribute**: Modifiability  
**Category**: Architecture Extension  
**Priority**: High  
**Status**: Defined  
**Date Created**: 2025-12-18

## Scenario Description

**Source**: Developer adding support for new TSN feature (e.g., Frame Preemption events)  
**Stimulus**: Request to add a new observer type to handle frame preemption diagnostic events  
**Artifact**: Event subsystem (Observer pattern implementation)  
**Environment**: Development time  
**Response**: New observer can be added without modifying existing observers or EventDispatcher core logic  
**Response Measure**: Implementation requires <4 hours, <100 LOC, no changes to existing observer implementations

---

## ATAM Quality Attribute Scenario Format

| Element | Description |
|---------|-------------|
| **Source** | Developer implementing TSN IEEE 802.3br Frame Preemption support |
| **Stimulus** | Add new event type: `FRAME_PREEMPTION_EVENT` with statistics (preempted frames, verification status) |
| **Artifact** | Event Subsystem: EventDispatcher, IEventObserver interface, observer registry |
| **Environment** | Development time, Visual Studio 2022, WDK 10.0.22621.0 |
| **Response** | 1. Create new `FramePreemptionObserver` class implementing `IEventObserver`<br>2. Register observer during driver initialization<br>3. Observer receives events via `OnEvent()` callback<br>4. No modifications to EventDispatcher or existing observers required |
| **Response Measure** | - Implementation time: <4 hours (design, code, test)<br>- Code changes: <100 LOC (new observer only)<br>- Impact: 0 changes to existing observers<br>- Test effort: <2 hours (unit tests for new observer)<br>- Build time impact: <5 seconds additional compilation |

---

## Related Requirements

**Traces to**:
- #68 (REQ-F-LOG-001: Event Logging) - New observer must integrate with event logging
- #53 (REQ-F-ERR-002: Error Handling) - New observer must handle errors gracefully
- #19 (REQ-F-RINGBUF-001: Ring Buffer) - New observer writes to shared ring buffer

**Addresses Quality Concerns**:
- Extensibility: Add new functionality without modifying existing code
- Maintainability: Isolate new features in separate modules
- Testability: New observers independently testable

---

## Related Architecture Decisions

**Relates to**:
- ADR #147 (ADR-OBS-001: Observer Pattern) - Defines observer interface and registration mechanism
- ADR #93 (ADR-RINGBUF-001: Lock-Free Ring Buffer) - New observer uses existing ring buffer
- ADR #131 (ADR-ERR-001: Error Reporting) - New observer follows unified error reporting

**Architecture View**: [Logical View - Event Subsystem](../views/logical/logical-view-event-subsystem.md)

---

## Detailed Scenario Walk-Through

### Step 1: Define Event Structure

```c
// New event type for frame preemption diagnostics
typedef struct _FRAME_PREEMPTION_EVENT {
    EVENT_HEADER Header;           // 24 bytes (common header)
    
    UINT32 PreemptedFrameCount;    // Total frames preempted
    UINT32 VerificationAttempts;   // SMD-V handshake attempts
    UINT32 VerificationFailures;   // Failed verifications
    UINT32 ExpressFrameCount;      // Express MAC frames
    UINT32 PreemptableFrameCount;  // Preemptable MAC frames
    UINT16 PortNumber;             // TSN port
    UINT16 Reserved;
} FRAME_PREEMPTION_EVENT;          // Total: 48 bytes
```

### Step 2: Implement Observer

```c
// FramePreemptionObserver.c (NEW FILE)
typedef struct _FRAME_PREEMPTION_OBSERVER {
    // IEventObserver interface (must be first member)
    IEventObserver Base;
    
    // Observer-specific state
    RING_BUFFER* RingBuffer;
    PVOID UserContext;
    
    // Statistics
    UINT64 TotalEventsReceived;
    UINT64 EventsDropped;
    
} FRAME_PREEMPTION_OBSERVER;

// Implementation of IEventObserver::OnEvent
NTSTATUS FramePreemptionObserver_OnEvent(
    IEventObserver* this,
    const void* eventData,
    UINT32 eventSize
) {
    FRAME_PREEMPTION_OBSERVER* self = CONTAINING_RECORD(
        this, 
        FRAME_PREEMPTION_OBSERVER, 
        Base
    );
    
    // Validate event type
    const EVENT_HEADER* header = (const EVENT_HEADER*)eventData;
    if (header->EventType != EVENT_TYPE_FRAME_PREEMPTION) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Write to ring buffer
    BOOLEAN written = RingBuffer_Write(
        self->RingBuffer, 
        eventData, 
        eventSize
    );
    
    if (written) {
        InterlockedIncrement64(&self->TotalEventsReceived);
    } else {
        InterlockedIncrement64(&self->EventsDropped);
    }
    
    return STATUS_SUCCESS;
}

// Constructor
NTSTATUS FramePreemptionObserver_Create(
    RING_BUFFER* ringBuffer,
    FRAME_PREEMPTION_OBSERVER** outObserver
) {
    FRAME_PREEMPTION_OBSERVER* observer = 
        ExAllocatePoolWithTag(NonPagedPool, sizeof(*observer), 'OFP');
    
    if (!observer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize IEventObserver interface
    observer->Base.OnEvent = FramePreemptionObserver_OnEvent;
    observer->Base.GetObserverContext = FramePreemptionObserver_GetContext;
    
    // Initialize observer state
    observer->RingBuffer = ringBuffer;
    observer->TotalEventsReceived = 0;
    observer->EventsDropped = 0;
    
    *outObserver = observer;
    return STATUS_SUCCESS;
}
```

### Step 3: Register Observer During Driver Init

```c
// In filter.c::FilterAttach()
NTSTATUS FilterAttach(...) {
    // ... existing code ...
    
    // Create frame preemption observer (NEW CODE)
    FRAME_PREEMPTION_OBSERVER* fpObserver = NULL;
    status = FramePreemptionObserver_Create(
        deviceContext->EventRingBuffer,
        &fpObserver
    );
    
    if (NT_SUCCESS(status)) {
        // Register with event dispatcher
        EventDispatcher_RegisterObserver(
            deviceContext->EventDispatcher,
            (IEventObserver*)fpObserver
        );
    }
    
    // ... rest of initialization ...
}
```

---

## Validation Criteria

### Structural Criteria

- ✅ New observer implements `IEventObserver` interface
- ✅ Observer registration via `EventDispatcher_RegisterObserver()`
- ✅ No modifications to `EventDispatcher` core logic
- ✅ No modifications to existing observers (PtpEventObserver, AvtpEventObserver)

### Behavioral Criteria

- ✅ New observer receives only relevant event types
- ✅ Events written to ring buffer in <1µs
- ✅ Observer handles errors gracefully (ring buffer full → drop + count)
- ✅ Observer lifecycle managed correctly (create → register → unregister → destroy)

### Performance Criteria

- ✅ Event dispatch latency unchanged (<2µs total for all observers)
- ✅ New observer `OnEvent()` completes in <500ns (measured)
- ✅ No additional lock contention (lock-free ring buffer)

### Testability Criteria

- ✅ Unit test: Observer creation and initialization
- ✅ Unit test: OnEvent() with valid frame preemption event
- ✅ Unit test: OnEvent() with invalid event type (rejected)
- ✅ Unit test: Ring buffer overflow handling
- ✅ Integration test: End-to-end event flow (hardware → observer → user-mode)

---

## Risk Analysis

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Observer registration fails | Low | Medium | Return error from FilterAttach, log to Event Log |
| Observer callback hangs | Low | High | Watchdog timer in debug builds, unregister observer |
| Ring buffer overflow | Medium | Low | Drop event, increment counter, emit diagnostic |
| Memory leak in observer | Low | Medium | Code review, static analysis (PREfast), unit tests |

---

## Trade-offs

### Pros (Benefits of Observer Pattern)

- ✅ **Extensibility**: Add new observers without touching existing code
- ✅ **Isolation**: New observer bugs don't affect others
- ✅ **Testability**: Independent unit tests for each observer
- ✅ **Maintainability**: Clear separation of concerns

### Cons (Costs)

- ❌ **Slight performance overhead**: Iteration over observer list (~100ns per observer)
- ❌ **Memory overhead**: Each observer ~200 bytes (negligible for 5-10 observers)
- ❌ **Complexity**: More indirection (interface pointers)

**Decision**: Pros outweigh cons for maintainability and extensibility.

---

## Implementation Checklist

- [ ] Define `FRAME_PREEMPTION_EVENT` structure in `event_types.h`
- [ ] Implement `FramePreemptionObserver.c` (new file)
- [ ] Add observer creation to `FilterAttach()`
- [ ] Add observer cleanup to `FilterDetach()`
- [ ] Write unit tests (5 test cases minimum)
- [ ] Update documentation (architecture views, user guide)
- [ ] Code review (2 approvers required)
- [ ] Validate performance (measure `OnEvent()` latency)

**Estimated Effort**: 4 hours (implementation) + 2 hours (testing) = 6 hours total

---

## References

- [Logical View: Event Subsystem](../views/logical/logical-view-event-subsystem.md) - Observer pattern implementation
- ADR #147 (Observer Pattern Design)
- ISO/IEC 25010:2011 - Modifiability quality attribute
- Gang of Four Design Patterns - Observer Pattern
