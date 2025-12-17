# Logical View: Event Subsystem Architecture

**View Type**: Logical View  
**Standard**: ISO/IEC/IEEE 42010:2011  
**Last Updated**: 2025-12-17  
**Status**: Approved

## Purpose

This view describes the logical structure of the event subsystem, focusing on class hierarchies, interfaces, and design patterns used to implement PTP/TSN event handling and propagation.

## Stakeholders

- **Software Architects**: Understand component relationships and design patterns
- **Developers**: Implement event-driven features following established patterns
- **Maintainers**: Troubleshoot event flow and extend functionality

## Concerns Addressed

- **Modularity**: How event handling is decomposed into cohesive components
- **Extensibility**: How new event types and observers can be added
- **Type Safety**: How event payloads are strongly typed
- **Decoupling**: How publishers and subscribers are decoupled

## Related Architecture Decisions

- **Relates to**: #93 (ADR-EVENT-001: Kernel-Mode Ring Buffer for Zero-Copy Event Delivery)
- **Relates to**: #147 (ADR-EVENT-EMIT-001: Event Emission Strategy - Observer Pattern)
- **Relates to**: #131 (ADR-ERR-001: Error Reporting Strategy)
- **Relates to**: #92 (ADR-DEVICE-ABSTRACT-001: Strategy Pattern for Multi-Controller Support)

## Related Requirements

- **Satisfies**: #19 (REQ-F-TSRING-001: Shared Memory Ring Buffer)
- **Satisfies**: #68 (REQ-F-EVENT-LOG-001: Windows Event Log Integration)
- **Satisfies**: #53 (REQ-F-INTEL-AVB-004: Error Handling)

## Quality Attributes

- **Relates to**: #180 (QA-SC-PERF-001: <1µs Timestamp Retrieval)
- **Relates to**: #181 (QA-SC-REL-001: 99.99% Uptime)

---

## Class Diagram: Core Event Subsystem

```
┌─────────────────────────────────────────────────────────────────┐
│                        IEventObserver                            │
│  (Abstract Interface - Observer Pattern)                        │
├─────────────────────────────────────────────────────────────────┤
│ + OnEvent(eventType: EVENT_TYPE, payload: void*): void          │
│ + GetObserverContext(): OBSERVER_CONTEXT*                       │
└─────────────────────────────────────────────────────────────────┘
                              ▲
                              │ implements
                              │
        ┌─────────────────────┴─────────────────────┐
        │                                           │
┌───────┴──────────┐                    ┌──────────┴─────────┐
│ PtpEventObserver │                    │ AvtpEventObserver  │
├──────────────────┤                    ├────────────────────┤
│ - ringBuffer     │                    │ - diagnosticLog    │
│ - userData       │                    │ - statsCounter     │
├──────────────────┤                    ├────────────────────┤
│ + OnEvent()      │                    │ + OnEvent()        │
│ + Initialize()   │                    │ + UpdateStats()    │
└──────────────────┘                    └────────────────────┘


┌─────────────────────────────────────────────────────────────────┐
│                     EventDispatcher                              │
│  (Singleton - Mediator Pattern)                                 │
├─────────────────────────────────────────────────────────────────┤
│ - observers: LIST_ENTRY                                         │
│ - observerLock: KSPIN_LOCK                                      │
│ - eventQueue: RING_BUFFER*                                      │
├─────────────────────────────────────────────────────────────────┤
│ + RegisterObserver(observer: IEventObserver*): NTSTATUS         │
│ + UnregisterObserver(observer: IEventObserver*): NTSTATUS       │
│ + EmitEvent(eventType: EVENT_TYPE, payload: void*): NTSTATUS    │
│ - DispatchToObservers(event: EVENT*): void                      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ uses
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        RingBuffer                                │
│  (Shared Memory - Zero-Copy Event Storage)                      │
├─────────────────────────────────────────────────────────────────┤
│ - buffer: EVENT_ENTRY[MAX_EVENTS]                               │
│ - writeIndex: ULONG (atomic)                                    │
│ - readIndex: ULONG (atomic)                                     │
│ - capacity: ULONG                                               │
├─────────────────────────────────────────────────────────────────┤
│ + Write(event: EVENT*): BOOLEAN                                 │
│ + Read(event: EVENT*): BOOLEAN                                  │
│ + IsFull(): BOOLEAN                                             │
│ + IsEmpty(): BOOLEAN                                            │
└─────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────┐
│                     HardwareEventSource                          │
│  (ISR Context - Hardware Abstraction)                           │
├─────────────────────────────────────────────────────────────────┤
│ - device: DEVICE_CONTEXT*                                       │
│ - interruptObject: PKINTERRUPT                                  │
│ - dpcObject: KDPC                                               │
├─────────────────────────────────────────────────────────────────┤
│ + OnInterrupt(context: void*): BOOLEAN (IRQL = DIRQL)           │
│ + DpcRoutine(dpc: PKDPC, ...): void (IRQL = DISPATCH_LEVEL)     │
│ - ExtractTimestamp(): PTP_TIMESTAMP                             │
│ - ClearInterruptStatus(): void                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ raises events via
                              ▼
                      EventDispatcher
```

---

## Class Descriptions

### IEventObserver (Abstract Interface)

**Responsibility**: Define contract for event notification

**Attributes**:
- None (pure interface)

**Operations**:
- `OnEvent(eventType, payload)`: Callback invoked when subscribed event occurs
- `GetObserverContext()`: Returns observer-specific context for filtering

**Invariants**:
- Must be implemented by all event subscribers
- `OnEvent()` must return quickly (<10µs) to avoid blocking dispatcher

**Design Rationale**: Observer pattern decouples event producers from consumers, allowing runtime subscription/unsubscription without tight coupling.

---

### PtpEventObserver (Concrete Observer)

**Responsibility**: Handle PTP hardware timestamp events

**Attributes**:
- `ringBuffer`: Pointer to shared-memory ring buffer for user-mode access
- `userData`: User-space context for event filtering

**Operations**:
- `OnEvent()`: Writes PTP timestamp to ring buffer
- `Initialize()`: Allocates ring buffer and registers with dispatcher

**Dependencies**:
- RingBuffer (composition)
- EventDispatcher (registration)

**Thread Safety**: Uses atomic operations for ring buffer writes

---

### AvtpEventObserver (Concrete Observer)

**Responsibility**: Handle AVTP diagnostic events

**Attributes**:
- `diagnosticLog`: Circular buffer for diagnostic traces
- `statsCounter`: Performance counters for AVTP streams

**Operations**:
- `OnEvent()`: Logs diagnostic information and updates statistics
- `UpdateStats()`: Increments stream counters

**Design Note**: Lightweight observer focused on monitoring, not control plane

---

### EventDispatcher (Singleton)

**Responsibility**: Central event routing and observer management

**Attributes**:
- `observers`: Linked list of registered observers (LIST_ENTRY)
- `observerLock`: Spinlock protecting observer list (IRQL ≤ DISPATCH_LEVEL)
- `eventQueue`: Ring buffer for deferred event processing

**Operations**:
- `RegisterObserver()`: Adds observer to notification list
- `UnregisterObserver()`: Removes observer safely (waits for in-flight events)
- `EmitEvent()`: Publishes event to all matching observers
- `DispatchToObservers()`: Private method invoking observer callbacks

**Concurrency**:
- Observer list modifications protected by `observerLock`
- Event emission lock-free (uses ring buffer atomic operations)
- Supports multiple concurrent publishers (ISR + DPC contexts)

**Performance**: <1µs emission latency (lock-free fast path)

---

### RingBuffer (Zero-Copy Storage)

**Responsibility**: Lock-free, shared-memory event storage

**Attributes**:
- `buffer`: Fixed-size array of event entries (PAGE_ALIGNED)
- `writeIndex`: Atomic producer index (ULONG, cache-aligned)
- `readIndex`: Atomic consumer index (ULONG, cache-aligned)
- `capacity`: Power-of-2 size for efficient modulo operations

**Operations**:
- `Write()`: Atomic producer operation (compare-and-swap)
- `Read()`: Atomic consumer operation (user-mode reads)
- `IsFull()`: Check available space
- `IsEmpty()`: Check pending events

**Memory Layout**:
```
+0x0000: writeIndex (ULONG, atomic)
+0x0040: readIndex (ULONG, atomic) [cache-line aligned]
+0x0080: capacity (ULONG)
+0x00C0: buffer[0] (EVENT_ENTRY)
+0x00C0 + N*sizeof(EVENT_ENTRY): buffer[N]
```

**Design Rationale**: Lock-free ring buffer eliminates spinlock contention between ISR (producer) and user-mode consumer, achieving <500ns write latency.

---

### HardwareEventSource (ISR/DPC Context)

**Responsibility**: Extract hardware events and initiate dispatch

**Attributes**:
- `device`: Device context containing BAR0 mapping
- `interruptObject`: Kernel interrupt object (MSI-X vector)
- `dpcObject`: Deferred Procedure Call for DISPATCH_LEVEL processing

**Operations**:
- `OnInterrupt()`: ISR at DIRQL, clears interrupt and queues DPC (<5µs)
- `DpcRoutine()`: DPC at DISPATCH_LEVEL, reads timestamp and emits event
- `ExtractTimestamp()`: Reads PTP timestamp from hardware registers
- `ClearInterruptStatus()`: Acknowledges interrupt to NIC

**IRQL Constraints**:
- `OnInterrupt()`: DIRQL (hardware interrupt level)
- `DpcRoutine()`: DISPATCH_LEVEL (deferred processing)

**Timing Budget**:
- ISR: <5µs (read status, queue DPC, clear interrupt)
- DPC: <50µs (read timestamp, emit event, optional logging)

---

## Component Dependencies

```
┌─────────────────┐
│ filter.c (NDIS) │
│  FilterAttach   │
└────────┬────────┘
         │ creates
         ▼
┌─────────────────────┐       ┌──────────────────┐
│ HardwareEventSource │──────▶│ EventDispatcher  │
│   (ISR/DPC)         │emits  │   (Singleton)    │
└─────────────────────┘       └────────┬─────────┘
                                       │ notifies
                              ┌────────┴────────┐
                              ▼                 ▼
                    ┌──────────────┐  ┌──────────────┐
                    │PtpEventObserver│ │AvtpEventObserver│
                    └───────┬────────┘  └──────────────┘
                            │ writes
                            ▼
                    ┌──────────────┐
                    │  RingBuffer  │ ←─ User-mode reads
                    │ (Shared Mem) │
                    └──────────────┘
```

---

## Design Patterns Applied

### 1. Observer Pattern (GoF Behavioral)

**Intent**: Define one-to-many dependency so when event occurs, all observers are notified

**Participants**:
- **Subject**: EventDispatcher
- **Observer**: IEventObserver interface
- **ConcreteObserver**: PtpEventObserver, AvtpEventObserver

**Benefits**:
- Loose coupling between event source and handlers
- Open/Closed principle: Add new observers without modifying dispatcher
- Dynamic subscription at runtime

### 2. Singleton Pattern (GoF Creational)

**Intent**: Ensure single EventDispatcher instance system-wide

**Implementation**:
- Global `g_EventDispatcher` pointer initialized in DriverEntry
- Lazy initialization on first `RegisterObserver()` call
- Protected by global spinlock

**Rationale**: Single dispatch point ensures consistent event ordering and simplifies observer management

### 3. Template Method Pattern (Event Processing)

**Intent**: Define skeleton of event processing, deferring steps to subclasses

**Implementation**:
```c
// Base template in EventDispatcher
void ProcessEvent(EVENT* event) {
    ValidateEvent(event);        // Common validation
    FilterObservers(event);      // Common filtering
    NotifyObservers(event);      // Calls OnEvent() on each
    LogEvent(event);             // Common logging
}

// ConcreteObserver provides:
void OnEvent(EVENT* event) {
    // Observer-specific handling
}
```

---

## Interface Specifications

### IEventObserver::OnEvent()

```c
typedef VOID (*PFN_EVENT_OBSERVER_CALLBACK)(
    _In_ EVENT_TYPE eventType,
    _In_reads_bytes_(payloadSize) PVOID payload,
    _In_ ULONG payloadSize,
    _In_opt_ PVOID observerContext
);

/**
 * @brief Notifies observer of event occurrence
 * 
 * @param eventType Type of event (PTP_TX_TIMESTAMP, AVTP_STREAM_ERROR, etc.)
 * @param payload Event-specific data structure
 * @param payloadSize Size of payload in bytes (for validation)
 * @param observerContext Context provided during RegisterObserver()
 * 
 * @remarks
 * - IRQL: DISPATCH_LEVEL (called from DPC context)
 * - Must complete in <10µs to avoid blocking other observers
 * - Must not raise IRQL or acquire locks held by higher-IRQL code
 * - Payload valid only during callback; copy if needed for async processing
 */
```

### EventDispatcher::EmitEvent()

```c
NTSTATUS EmitEvent(
    _In_ EVENT_TYPE eventType,
    _In_reads_bytes_(payloadSize) PVOID payload,
    _In_ ULONG payloadSize
);

/**
 * @brief Publishes event to all registered observers
 * 
 * @param eventType Event classification (filters observers)
 * @param payload Event data (must be non-paged memory)
 * @param payloadSize Payload size for validation
 * 
 * @return STATUS_SUCCESS if event dispatched
 *         STATUS_INSUFFICIENT_RESOURCES if ring buffer full
 * 
 * @remarks
 * - IRQL: ≤ DISPATCH_LEVEL
 * - Lock-free for common case (no observer list modification)
 * - Guarantees delivery order per event type
 * - Returns immediately; observers notified asynchronously
 */
```

---

## Traceability

### Satisfied Requirements

| Requirement | How Satisfied |
|-------------|---------------|
| #19 (REQ-F-TSRING-001) | RingBuffer class provides zero-copy shared memory |
| #53 (REQ-F-INTEL-AVB-004) | EventDispatcher propagates error events to observers |
| #68 (REQ-F-EVENT-LOG-001) | AvtpEventObserver logs to Windows Event Log |

### Architecture Decisions Implemented

| ADR | Implementation |
|-----|----------------|
| #93 (Ring Buffer) | RingBuffer class with atomic producer/consumer indices |
| #147 (Observer Pattern) | IEventObserver interface + EventDispatcher mediator |
| #131 (Error Reporting) | Error events routed through same observer mechanism |

### Quality Scenarios Addressed

| Scenario | Evidence |
|----------|----------|
| #180 (QA-SC-PERF-001: <1µs Timestamp Retrieval) | Lock-free EmitEvent() + DPC processing |
| #181 (QA-SC-REL-001: 99.99% Uptime) | Observer failures isolated; dispatcher continues |

---

## Validation Criteria

### Structural Validation

- ✅ All observers implement IEventObserver interface
- ✅ EventDispatcher is singleton (single global instance)
- ✅ RingBuffer uses atomic operations (no spinlocks)
- ✅ ISR completes in <5µs (validated with GPIO + oscilloscope)

### Behavioral Validation

- ✅ Observer registration/unregistration thread-safe
- ✅ Event ordering preserved per event type
- ✅ Observer failures don't affect other observers
- ✅ Ring buffer overflow handled gracefully (drop oldest)

### Performance Validation

- ✅ EmitEvent() latency <1µs (measured with QueryPerformanceCounter)
- ✅ OnEvent() callbacks complete in <10µs (enforced by watchdog)
- ✅ Ring buffer write latency <500ns (lock-free path)

---

## Design Constraints

1. **IRQL Constraints**:
   - ISR: DIRQL (minimize time, queue DPC)
   - DPC: DISPATCH_LEVEL (main event processing)
   - Observer callbacks: DISPATCH_LEVEL (no blocking calls)

2. **Memory Constraints**:
   - Ring buffer size: 4KB (fits in single page)
   - Max concurrent observers: 16 (linked list overhead)
   - Event payload: ≤256 bytes (fits in cache line)

3. **Timing Constraints**:
   - ISR latency: <5µs (hard real-time)
   - DPC latency: <50µs (soft real-time)
   - Observer callback: <10µs (enforced by dispatcher)

---

## Future Extensions

### Planned Enhancements

1. **Priority-based Observer Ordering**: High-priority observers notified first
2. **Event Filtering**: Observers subscribe to specific event subtypes
3. **Async Event Processing**: Work items for long-running handlers
4. **Event Replay**: Debug mode records events for post-mortem analysis

### Backward Compatibility

- Observer interface versioned (IEventObserverV1, V2, ...)
- Old observers continue working with extended dispatcher
- New event types ignored by old observers (graceful degradation)

---

## References

- ISO/IEC/IEEE 42010:2011 - Architecture Description Standard
- ISO/IEC/IEEE 29148:2018 - Requirements Engineering
- "Design Patterns: Elements of Reusable Object-Oriented Software" (GoF)
- Windows Driver Development - Synchronization and IRQL (MSDN)
- ADR-EVENT-001: Ring Buffer Architecture (#93)
- ADR-EVENT-EMIT-001: Observer Pattern Implementation (#147)
