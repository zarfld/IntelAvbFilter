# Quality Attribute Scenario: QA-SC-REL-003 - Observer Fault Isolation

**Scenario ID**: QA-SC-REL-003  
**Quality Attribute**: Reliability (Fault Tolerance)  
**Priority**: P0 (Critical)  
**Status**: Proposed  
**Created**: 2025-01-15  
**Related Issues**: TBD (will be created)

---

## ATAM Quality Attribute Scenario Elements

### 1. Source of Stimulus
- **Who/What**: Observer callback implementation with software defect
- **Examples**:
  - Dereferencing NULL pointer in `OnEvent()` callback
  - Infinite loop consuming CPU
  - Stack overflow from recursion
  - Access violation reading invalid memory
  - Division by zero

### 2. Stimulus
- **Event**: Observer callback crashes or hangs during event processing
- **Trigger Conditions**:
  - Observer receives malformed event data
  - Callback accesses freed memory
  - Logic error causes exception
  - Timeout: Callback exceeds 10µs execution time
- **Frequency**: Rare but critical (1 in 10M events)

### 3. Artifact
- **System Element**: EventDispatcher, Observer management, Watchdog timer
- **Components Affected**:
  - `EventDispatcher::EmitEvent()` - Event distribution loop
  - `ObserverList` - Active observer registry
  - `DpcWatchdog` - Timeout detection (debug builds)
  - `ExceptionHandler` - Structured exception handling (SEH)

### 4. Environment
- **System State**: Normal operation, DISPATCH_LEVEL IRQL
- **Context**: Production driver on customer system
- **Conditions**:
  - 100 observers registered (10 NICs × 10 observers each)
  - High event rate (50K events/sec)
  - System uptime >7 days

### 5. Response
1. **Detect Fault** (within 100µs):
   - Watchdog timer detects callback timeout >10µs
   - OR structured exception handler (SEH) catches access violation
2. **Isolate Fault** (within 1ms):
   - Mark observer as "FAULTED" (atomic flag)
   - Remove from active observer list (lock-free removal)
   - Prevent future event delivery to this observer
3. **Preserve System Stability**:
   - Continue dispatching to remaining healthy observers
   - No system crash, no BSOD
   - Event processing latency <5% increase
4. **Log Diagnostic Information**:
   - Observer identity (name, instance address)
   - Faulting event (type, timestamp, payload)
   - Exception code (if applicable)
   - Stack trace (debug builds)
5. **Notify User-Mode**:
   - Set health status flag in ring buffer header
   - User-mode app queries status, receives alert

### 6. Response Measure
| Metric | Target | Measurement |
|--------|--------|-------------|
| **Fault Detection Time** | <100µs | Time from exception/timeout to detection |
| **Isolation Time** | <1ms | Time to remove observer from active list |
| **System Availability** | 99.99% | Remaining observers continue functioning |
| **Event Loss Rate** | <0.01% | Only events to faulted observer are lost |
| **Latency Impact** | <5% increase | Dispatch latency for healthy observers |
| **Diagnostic Coverage** | 100% | All faults logged with context |
| **Recovery Time** | <10s | User-mode app detects fault and reacts |

---

## Detailed Implementation

### Watchdog Timer (Debug Builds)

```c
// DpcWatchdog.h
typedef struct _OBSERVER_WATCHDOG {
    KTIMER Timer;               // High-resolution timer
    KDPC TimerDpc;              // DPC for timeout handling
    volatile LONG Active;       // 1 = observer executing, 0 = idle
    IEventObserver* CurrentObserver;  // Observer being monitored
    LARGE_INTEGER StartTime;    // Performance counter at entry
    LARGE_INTEGER Threshold;    // 10µs = 10,000 * QPC frequency / 1,000,000
} OBSERVER_WATCHDOG;

// Start watchdog before observer callback
VOID Watchdog_Start(OBSERVER_WATCHDOG* watchdog, IEventObserver* observer)
{
    #if DBG
    watchdog->CurrentObserver = observer;
    watchdog->StartTime = KeQueryPerformanceCounter(NULL);
    InterlockedExchange(&watchdog->Active, 1);
    
    // Set timer for 10µs (converted to 100ns units)
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -100;  // 10µs in 100ns units (negative = relative)
    KeSetTimerEx(&watchdog->Timer, dueTime, 0, &watchdog->TimerDpc);
    #endif
}

// Stop watchdog after observer callback returns
VOID Watchdog_Stop(OBSERVER_WATCHDOG* watchdog)
{
    #if DBG
    InterlockedExchange(&watchdog->Active, 0);
    KeCancelTimer(&watchdog->Timer);
    #endif
}

// Watchdog DPC - fires if observer exceeds 10µs
VOID WatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
)
{
    OBSERVER_WATCHDOG* watchdog = (OBSERVER_WATCHDOG*)DeferredContext;
    
    if (InterlockedCompareExchange(&watchdog->Active, 0, 1) == 1) {
        // Observer still executing after 10µs - TIMEOUT
        LARGE_INTEGER endTime = KeQueryPerformanceCounter(NULL);
        LONGLONG elapsedTicks = endTime.QuadPart - watchdog->StartTime.QuadPart;
        
        DbgPrint("[FAULT] Observer %p exceeded 10µs timeout (elapsed: %lld ticks)\n",
                 watchdog->CurrentObserver, elapsedTicks);
        
        // Mark observer as FAULTED (will be removed by EventDispatcher)
        watchdog->CurrentObserver->State = OBSERVER_STATE_FAULTED;
        
        // Log to Event Log
        EventLog_WriteError(EVENT_OBSERVER_TIMEOUT, watchdog->CurrentObserver);
    }
}
```

### Structured Exception Handling (SEH)

```c
// EventDispatcher.c - EmitEvent with SEH
NTSTATUS EventDispatcher_EmitEvent(
    EVENT_DISPATCHER* dispatcher,
    EVENT_TYPE eventType,
    const VOID* eventData,
    SIZE_T eventSize
)
{
    NTSTATUS status = STATUS_SUCCESS;
    OBSERVER_NODE* node;
    ULONG faultedCount = 0;
    
    // Iterate observer list (lock-free read)
    for (node = dispatcher->ObserverList; node != NULL; node = node->Next) {
        IEventObserver* observer = node->Observer;
        
        // Skip already faulted observers
        if (observer->State == OBSERVER_STATE_FAULTED) {
            faultedCount++;
            continue;
        }
        
        // Start watchdog (debug builds only)
        Watchdog_Start(&dispatcher->Watchdog, observer);
        
        // Call observer with SEH protection
        __try {
            status = observer->Vtbl->OnEvent(observer, eventType, eventData, eventSize);
            
            if (!NT_SUCCESS(status)) {
                DbgPrint("[WARN] Observer %p returned error 0x%08X\n", observer, status);
                // Non-fatal error - log but continue
            }
        }
        __except (ExceptionFilter(GetExceptionInformation(), observer)) {
            // Exception caught - mark observer as FAULTED
            observer->State = OBSERVER_STATE_FAULTED;
            faultedCount++;
            
            DbgPrint("[FAULT] Observer %p faulted with exception 0x%08X\n",
                     observer, GetExceptionCode());
            
            // Log to Event Log
            EventLog_WriteError(EVENT_OBSERVER_EXCEPTION, observer);
        }
        
        // Stop watchdog
        Watchdog_Stop(&dispatcher->Watchdog);
    }
    
    // Remove faulted observers (lazy removal - next iteration)
    if (faultedCount > 0) {
        EventDispatcher_RemoveFaultedObservers(dispatcher);
    }
    
    return STATUS_SUCCESS;  // Always succeed - partial delivery OK
}

// Exception filter - log details and mark observer
LONG ExceptionFilter(
    EXCEPTION_POINTERS* exceptionInfo,
    IEventObserver* observer
)
{
    EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
    
    DbgPrint("[EXCEPTION] Observer %p:\n", observer);
    DbgPrint("  Code: 0x%08X\n", record->ExceptionCode);
    DbgPrint("  Address: %p\n", record->ExceptionAddress);
    DbgPrint("  Flags: 0x%08X\n", record->ExceptionFlags);
    
    // Capture stack trace (debug builds)
    #if DBG
    PVOID stackTrace[16];
    USHORT frameCount = RtlCaptureStackBackTrace(0, 16, stackTrace, NULL);
    for (USHORT i = 0; i < frameCount; i++) {
        DbgPrint("    Frame %d: %p\n", i, stackTrace[i]);
    }
    #endif
    
    // Mark observer as faulted (atomic)
    InterlockedExchange((LONG*)&observer->State, OBSERVER_STATE_FAULTED);
    
    // Continue execution (don't propagate exception)
    return EXCEPTION_EXECUTE_HANDLER;
}
```

### Lock-Free Observer Removal

```c
// Remove faulted observers from linked list (lock-free)
VOID EventDispatcher_RemoveFaultedObservers(EVENT_DISPATCHER* dispatcher)
{
    OBSERVER_NODE** prevNext = &dispatcher->ObserverList;
    OBSERVER_NODE* node = dispatcher->ObserverList;
    ULONG removedCount = 0;
    
    while (node != NULL) {
        OBSERVER_NODE* next = node->Next;
        
        if (node->Observer->State == OBSERVER_STATE_FAULTED) {
            // Remove from list (atomic pointer update)
            InterlockedExchangePointer((PVOID*)prevNext, next);
            
            // Add to removal queue (freed in passive-level DPC)
            ExInterlockedInsertTailList(&dispatcher->RemovalQueue,
                                        &node->RemovalEntry,
                                        &dispatcher->RemovalLock);
            
            removedCount++;
            
            // Update health status (user-mode can query)
            InterlockedIncrement(&dispatcher->FaultedObserverCount);
            
            DbgPrint("[REMOVED] Observer %p from active list\n", node->Observer);
        } else {
            prevNext = &node->Next;
        }
        
        node = next;
    }
    
    if (removedCount > 0) {
        DbgPrint("[CLEANUP] Removed %lu faulted observers\n", removedCount);
        
        // Schedule passive-level DPC to free memory
        KeInsertQueueDpc(&dispatcher->CleanupDpc, NULL, NULL);
    }
}

// Cleanup DPC - runs at PASSIVE_LEVEL to free memory
VOID CleanupDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
)
{
    EVENT_DISPATCHER* dispatcher = (EVENT_DISPATCHER*)DeferredContext;
    LIST_ENTRY* entry;
    
    while ((entry = ExInterlockedRemoveHeadList(&dispatcher->RemovalQueue,
                                                 &dispatcher->RemovalLock)) != NULL) {
        OBSERVER_NODE* node = CONTAINING_RECORD(entry, OBSERVER_NODE, RemovalEntry);
        
        DbgPrint("[FREE] Observer node %p freed\n", node);
        ExFreePoolWithTag(node, 'bsOE');  // 'EObS' = Event Observer
    }
}
```

### Health Status Reporting

```c
// Ring buffer header - health status field
typedef struct _RING_BUFFER_HEADER {
    volatile LONG WriteIndex;       // Producer index
    volatile LONG ReadIndex;        // Consumer index
    ULONG Capacity;                 // Total slots
    ULONG EventSize;                // Bytes per event
    
    // Health status (NEW)
    volatile LONG FaultedObservers;  // Count of faulted observers
    volatile LONG TotalExceptions;   // Cumulative exception count
    volatile LONG LastExceptionCode; // Last exception code (0x00000000 = none)
    LARGE_INTEGER LastFaultTime;     // Timestamp of last fault
    
    UCHAR Reserved[48];             // Padding to 128 bytes
} RING_BUFFER_HEADER;

// User-mode queries health status
typedef struct _OBSERVER_HEALTH_STATUS {
    ULONG FaultedObservers;      // Current faulted count
    ULONG TotalExceptions;       // Lifetime exception count
    ULONG LastExceptionCode;     // 0xC0000005 = access violation, etc.
    LARGE_INTEGER LastFaultTime; // QPC timestamp
} OBSERVER_HEALTH_STATUS;

NTSTATUS IOCTL_GetObserverHealth(
    DEVICE_OBJECT* deviceObject,
    IRP* irp
)
{
    IO_STACK_LOCATION* stack = IoGetCurrentIrpStackLocation(irp);
    OBSERVER_HEALTH_STATUS* output = (OBSERVER_HEALTH_STATUS*)irp->AssociatedIrp.SystemBuffer;
    
    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(OBSERVER_HEALTH_STATUS)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    EVENT_DISPATCHER* dispatcher = GetEventDispatcher(deviceObject);
    
    // Read health status from ring buffer header (volatile reads)
    output->FaultedObservers = dispatcher->RingBuffer->Header->FaultedObservers;
    output->TotalExceptions = dispatcher->RingBuffer->Header->TotalExceptions;
    output->LastExceptionCode = dispatcher->RingBuffer->Header->LastExceptionCode;
    output->LastFaultTime = dispatcher->RingBuffer->Header->LastFaultTime;
    
    irp->IoStatus.Information = sizeof(OBSERVER_HEALTH_STATUS);
    return STATUS_SUCCESS;
}
```

---

## Validation Criteria

### 1. Fault Detection
- ✅ **Access Violation**: SEH catches NULL pointer dereference, exception code 0xC0000005
- ✅ **Timeout**: Watchdog detects callback >10µs (debug builds)
- ✅ **Stack Overflow**: SEH catches 0xC00000FD
- ✅ **Division by Zero**: SEH catches 0xC0000094
- ✅ **Detection Time**: <100µs from fault to OBSERVER_STATE_FAULTED flag set

### 2. Fault Isolation
- ✅ **Removal**: Faulted observer removed from active list within 1ms
- ✅ **No Propagation**: Exception does not propagate to other observers
- ✅ **Healthy Observers**: Continue receiving events without interruption
- ✅ **Event Loss**: Only events to faulted observer are lost (<0.01% of total)

### 3. System Stability
- ✅ **No Crash**: System continues running, no BSOD (Blue Screen of Death)
- ✅ **Latency Impact**: Dispatch latency increases <5% after observer removal
- ✅ **CPU Usage**: No CPU spike or hang (infinite loop prevented by watchdog)
- ✅ **Memory Leaks**: Observer nodes freed in passive-level cleanup DPC

### 4. Diagnostics
- ✅ **Event Log**: Fault logged with observer identity, exception code, timestamp
- ✅ **Stack Trace**: Captured in debug builds (16 frames)
- ✅ **Health Status**: User-mode can query faulted observer count via IOCTL
- ✅ **Ring Buffer**: Health status fields in header (FaultedObservers, TotalExceptions)

---

## Test Cases

### TC-1: Access Violation in Observer Callback
**Setup**: Register observer with intentional NULL pointer dereference  
**Steps**:
1. Inject PTP_TX_TIMESTAMP_EVENT
2. Observer callback dereferences NULL pointer
3. SEH catches exception (0xC0000005)
4. Observer marked FAULTED and removed

**Expected**:
- Exception caught, logged to Event Log
- Observer removed from active list within 1ms
- Remaining 99 observers continue receiving events
- No system crash
- User-mode queries health status: FaultedObservers = 1

### TC-2: Timeout in Observer Callback (Debug Build)
**Setup**: Register observer with intentional infinite loop  
**Steps**:
1. Inject event
2. Observer callback enters infinite loop
3. Watchdog timer fires after 10µs
4. Observer marked FAULTED

**Expected**:
- Watchdog detects timeout within 100µs
- Observer removed from active list
- Event processing latency <5% increase
- Debug log shows timeout message

### TC-3: Multiple Faults (Stress Test)
**Setup**: Register 10 faulty observers (various exceptions)  
**Steps**:
1. Inject 100K events
2. All 10 faulty observers trigger exceptions
3. All removed within first 100 events

**Expected**:
- All 10 observers marked FAULTED and removed
- Remaining 90 healthy observers process all 100K events
- Event loss rate <0.01% (only events to faulted observers)
- Health status: FaultedObservers = 10, TotalExceptions = 10

### TC-4: User-Mode Health Monitoring
**Setup**: Faulty observer registered  
**Steps**:
1. User-mode app queries health status (baseline: FaultedObservers = 0)
2. Inject event → observer faults
3. User-mode app queries health status again

**Expected**:
- Baseline: FaultedObservers = 0
- After fault: FaultedObservers = 1
- LastExceptionCode = 0xC0000005 (or appropriate exception)
- LastFaultTime = recent timestamp

---

## Risk Analysis

### Risk 1: Watchdog False Positives (Debug Builds)
**Likelihood**: Low  
**Impact**: Medium (unnecessary observer removal)  
**Mitigation**:
- Set watchdog threshold to 10µs (5× typical callback time)
- Disable watchdog in release builds (SEH only)
- Log all timeout events for analysis

### Risk 2: Memory Leak from Failed Cleanup
**Likelihood**: Low  
**Impact**: High (resource exhaustion)  
**Mitigation**:
- Cleanup DPC runs at PASSIVE_LEVEL (safe memory operations)
- Use ExFreePoolWithTag with driver-specific tag ('bsOE')
- Driver Verifier detects leaks in testing

### Risk 3: SEH Performance Overhead
**Likelihood**: N/A (always present)  
**Impact**: Low (<100ns per observer)  
**Mitigation**:
- SEH uses hardware exception tables (minimal overhead when no exception)
- Measured: <50ns overhead for try/except block
- Acceptable given fault tolerance benefit

### Risk 4: Race Condition in Observer List Removal
**Likelihood**: Low (lock-free design)  
**Impact**: High (double-free or use-after-free)  
**Mitigation**:
- InterlockedExchangePointer for atomic list updates
- Observer nodes added to removal queue (freed in passive-level DPC)
- No direct memory freeing at DISPATCH_LEVEL

### Risk 5: Cascading Failures (All Observers Fault)
**Likelihood**: Very Low  
**Impact**: Medium (no event delivery)  
**Mitigation**:
- Independent observer implementations (no shared state)
- Faults isolated to individual observers
- System continues running even if all observers removed
- User-mode app detects via health status query

---

## Trade-offs

### Fault Tolerance vs. Performance
- **Benefit**: System remains stable even with faulty observers
- **Cost**: ~100ns SEH overhead per observer callback (try/except block)
- **Justification**: Reliability > 100ns latency for driver stability

### Watchdog (Debug) vs. Production Performance
- **Benefit**: Detects timeout-based faults (infinite loops, deadlocks)
- **Cost**: Timer overhead (~500ns) + DPC scheduling
- **Justification**: Debug-only feature, disabled in release builds

### Lazy Removal vs. Immediate Removal
- **Benefit**: No locks required, DISPATCH_LEVEL safe
- **Cost**: Faulted observer remains in list until next EmitEvent() call
- **Justification**: Simplifies concurrency, acceptable latency (~1ms)

---

## Traceability

### Requirements Verified
- **#53** (Error Handling): Graceful degradation on observer faults
- **#68** (Event Logging): Faults logged to Event Log
- **#19** (Ring Buffer): Health status in ring buffer header

### Architecture Decisions Satisfied
- **#147** (Observer Pattern): Fault isolation per observer
- **#131** (Error Reporting): Structured exception handling
- **#93** (Lock-Free Ring Buffer): Removal queue uses lock-free design

### Architecture Views Referenced
- [Logical View - Event Subsystem](../views/logical/logical-view-event-subsystem.md): Observer lifecycle
- [Process View - Event Flow](../views/process/process-view-event-flow.md): Exception handling sequence

---

## Implementation Checklist

- [ ] **Phase 1**: Implement SEH in EventDispatcher::EmitEvent() (~2 hours)
  - Add __try/__except blocks around observer callbacks
  - Implement ExceptionFilter with logging
  - Test with intentional access violation

- [ ] **Phase 2**: Implement lock-free observer removal (~3 hours)
  - InterlockedExchangePointer for list updates
  - Removal queue with ExInterlockedInsertTailList
  - Passive-level cleanup DPC

- [ ] **Phase 3**: Implement watchdog timer (debug builds) (~4 hours)
  - KTIMER + KDPC for timeout detection
  - Watchdog_Start/Watchdog_Stop wrappers
  - Test with intentional infinite loop

- [ ] **Phase 4**: Add health status reporting (~2 hours)
  - Extend ring buffer header with health fields
  - Implement IOCTL_GetObserverHealth
  - User-mode test app

- [ ] **Phase 5**: Write unit tests (~4 hours)
  - TC-1: Access violation test
  - TC-2: Timeout test (debug build)
  - TC-3: Multiple faults stress test
  - TC-4: Health monitoring test

- [ ] **Phase 6**: Integration testing (~2 hours)
  - Test with real hardware events (inject faults)
  - Verify no BSOD under fault conditions
  - Measure latency impact (<5%)

**Estimated Total**: ~17 hours

---

## Acceptance Criteria

| Criterion | Threshold | Measurement |
|-----------|-----------|-------------|
| Fault detection time | <100µs | QPC timestamps around SEH/__try block |
| Observer removal time | <1ms | Time from fault to removal from active list |
| System stability | 100% | No BSOD after 1M faults injected |
| Event loss rate | <0.01% | Only events to faulted observer lost |
| Latency impact | <5% | Dispatch latency before/after observer removal |
| Diagnostic coverage | 100% | All fault types logged (access violation, timeout, stack overflow) |
| Memory leaks | 0 | Driver Verifier shows no leaks after 10K observer faults |

---

**Scenario Status**: Ready for implementation  
**Next Steps**: Create GitHub issue linking to Requirements #53, #68, #19
