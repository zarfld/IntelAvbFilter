# ARC-C-TS-007: Timestamp Event Subscription (Event-Based TX/RX Timestamp Notification)

**Issue**: #144  
**Type**: Architecture Component (ARC-C)  
**Priority**: P1 (High)  
**Status**: Backlog  
**Phase**: 03 - Architecture Design

## Description
Timestamp Event Subscription component providing event-based notification mechanism for TX/RX timestamps, allowing user-mode applications to subscribe to timestamp events via kernel ring buffers instead of polling IOCTLs. Reduces latency and CPU overhead for time-critical AVB applications.

## Responsibility
**Single Responsibility**: Event-based timestamp notification with subscription management

**Responsibilities**:
- Subscription management (subscribe/unsubscribe for TX/RX timestamps)
- Kernel ring buffer for timestamp events (lock-free producer/consumer)
- Event notification to user-mode (keyed events, shared memory)
- Multi-subscriber support (multiple applications receive same events)
- Overflow handling (drop oldest vs. block producer)
- Subscription cleanup on application exit
- Performance metrics (event delivery latency)

**Does NOT Do**:
- Timestamp generation (delegated to Hardware Access Layer)
- Packet processing (delegated to NDIS Filter Core)
- Hardware register access (delegated to Device implementations)
- IOCTL validation (delegated to IOCTL Dispatcher)

## Traceability

Traces to: #16 (REQ-F-IOCTL-TS-001: TX timestamp retrieval), #17 (REQ-F-IOCTL-TS-003: TX timestamp subscription), #18 (REQ-F-IOCTL-TS-004: RX timestamp subscription), #2 (REQ-F-PTP-001: Get/Set PTP timestamp)

**Satisfies Requirements**:
- Satisfies #16 (REQ-F-IOCTL-TS-001: Query TX timestamp with <100µs latency)
- Satisfies #17 (REQ-F-IOCTL-TS-003: Subscribe for TX timestamp events)
- Satisfies #18 (REQ-F-IOCTL-TS-004: Subscribe for RX timestamp events)
- Satisfies #2 (REQ-F-PTP-001: Low-latency timestamp access)

**Architecture Decisions**:
- Implements #135 (ADR-PERF-004: Kernel ring buffer timestamp events)
- Implements #132 (ADR-PERF-002: Direct BAR0 MMIO for <1µs latency)

## Interfaces

### Provided Interfaces (Subscription API)

**Subscription Management**:
```c
// Subscribe for TX timestamps
NTSTATUS AvbSubscribeTxTimestamps(
    _In_ PAVB_DEVICE_CONTEXT Context,
    _In_ HANDLE UserEventHandle,      // User-mode event to signal
    _Out_ PULONG SubscriptionId        // Unique subscription ID
);

// Subscribe for RX timestamps
NTSTATUS AvbSubscribeRxTimestamps(
    _In_ PAVB_DEVICE_CONTEXT Context,
    _In_ HANDLE UserEventHandle,
    _Out_ PULONG SubscriptionId
);

// Unsubscribe from timestamps
NTSTATUS AvbUnsubscribeTimestamps(
    _In_ PAVB_DEVICE_CONTEXT Context,
    _In_ ULONG SubscriptionId
);
```

**Ring Buffer Access** (shared memory):
```c
typedef struct _TIMESTAMP_EVENT {
    ULONGLONG Timestamp;           // Hardware timestamp (nanoseconds)
    ULONG SequenceId;              // Packet sequence ID
    UCHAR MessageType;             // PTP message type
    UCHAR Direction;               // TX (0) or RX (1)
    USHORT Reserved;
} TIMESTAMP_EVENT;

typedef struct _TIMESTAMP_RING_BUFFER {
    volatile ULONG WriteIndex;     // Producer index (kernel)
    volatile ULONG ReadIndex;      // Consumer index (user-mode)
    ULONG Capacity;                // Ring buffer size (power of 2)
    ULONG OverflowCount;           // Dropped events counter
    TIMESTAMP_EVENT Events[1024];  // Event array (1024 entries)
} TIMESTAMP_RING_BUFFER;
```

**Event Notification**:
```c
// Signal user-mode event when new timestamp available
VOID AvbNotifyTimestampEvent(
    _In_ PAVB_DEVICE_CONTEXT Context,
    _In_ TIMESTAMP_EVENT Event
);
```

### Required Interfaces (Dependencies)

**Depends on**:
- Hardware Access Layer (#140): `HalReadPhcTimestamp()` for timestamp capture
- AVB Integration Layer (#139): `AVB_DEVICE_CONTEXT` for adapter context
- Adapter Context Manager (#143): Context lookup for multi-adapter

**Provides to**:
- User-mode applications: Shared memory ring buffer + keyed events
- IOCTL Dispatcher (#142): Subscription IOCTLs (subscribe/unsubscribe)

## Technology Stack
- **Language**: C (Kernel-mode)
- **Framework**: Windows Driver Framework (WDF) 1.31
- **Synchronization**: Lock-free ring buffer (atomic operations)
- **User-mode notification**: Keyed events (`KeSetEvent()`)
- **Shared memory**: MDL-backed section object (kernel↔user-mode)

## Quality Requirements

| Attribute | Requirement | Measure |
|-----------|-------------|---------|
| **Performance** | Event notification latency | <100µs (kernel→user-mode) |
| **Throughput** | Max event rate | \u003e10,000 events/sec |
| **Reliability** | No event loss (normal load) | 0 dropped events if <80% capacity |
| **Concurrency** | Thread-safe access | Lock-free ring buffer correctness |

## Key Data Structures

**Subscription Entry**:
```c
typedef struct _TIMESTAMP_SUBSCRIPTION {
    LIST_ENTRY ListEntry;          // Links to next/prev subscription
    ULONG SubscriptionId;          // Unique ID
    HANDLE UserEventHandle;        // User-mode event to signal
    PKEVENT KernelEvent;           // Kernel event object
    PTIMESTAMP_RING_BUFFER RingBuffer; // Shared ring buffer
    PVOID RingBufferMdl;           // MDL for shared memory
    ULONG EventCount;              // Total events delivered
    ULONG OverflowCount;           // Dropped events
    TIMESTAMP_TYPE Type;           // TX or RX
} TIMESTAMP_SUBSCRIPTION, *PTIMESTAMP_SUBSCRIPTION;

typedef enum _TIMESTAMP_TYPE {
    TimestampTypeTx = 0,
    TimestampTypeRx = 1
} TIMESTAMP_TYPE;
```

**Per-Adapter Subscription List**:
```c
// Added to AVB_DEVICE_CONTEXT
typedef struct _AVB_DEVICE_CONTEXT {
    // ... existing fields
    
    NDIS_SPIN_LOCK TxSubscriptionLock;
    LIST_ENTRY TxSubscriptionList;
    ULONG TxSubscriptionCount;
    
    NDIS_SPIN_LOCK RxSubscriptionLock;
    LIST_ENTRY RxSubscriptionList;
    ULONG RxSubscriptionCount;
    
    ULONG NextSubscriptionId;      // Auto-incrementing ID
} AVB_DEVICE_CONTEXT;
```

## Control Flow

**Subscription Flow** (IOCTL_AVB_TS_SUBSCRIBE_TX):
```
User-mode: CreateEvent() → DeviceIoControl(SUBSCRIBE_TX, eventHandle)
    ↓
Kernel: AvbSubscribeTxTimestamps()
    ↓
1. Validate event handle (MmGetSystemAddressForMdlSafe)
    ↓
2. Allocate TIMESTAMP_SUBSCRIPTION structure
    ↓
3. Create kernel event object from user handle (ObReferenceObjectByHandle)
    ↓
4. Allocate ring buffer (PAGE_SIZE, lock-free)
    ↓
5. Create MDL for shared memory (MmBuildMdlForNonPagedPool)
    ↓
6. Map to user-mode address space (MmMapLockedPagesSpecifyCache)
    ↓
7. Assign subscription ID (context-\u003eNextSubscriptionId++)
    ↓
8. Insert into subscription list (context-\u003eTxSubscriptionList)
    ↓
9. Return subscription ID + ring buffer user-mode address
```

**Event Notification Flow** (TX timestamp interrupt):
```
Hardware: TX timestamp ready
    ↓
ISR: Deferred to DPC
    ↓
DPC: AvbNotifyTimestampEvent(Event)
    ↓
1. Lock subscription list (acquire spin lock)
    ↓
2. For each TX subscription:
    ↓
3. Write event to ring buffer (atomic WriteIndex increment)
    ↓ (if buffer full → overflow_count++, drop oldest)
4. Signal user-mode event (KeSetEvent)
    ↓
5. Release spin lock
    ↓
User-mode: WaitForSingleObject() returns → read from ring buffer
```

**Unsubscription Flow** (IOCTL_AVB_TS_UNSUBSCRIBE):
```
User-mode: DeviceIoControl(UNSUBSCRIBE, subscriptionId)
    ↓
Kernel: AvbUnsubscribeTimestamps(subscriptionId)
    ↓
1. Acquire subscription list lock
    ↓
2. Find subscription by ID
    ↓
3. Remove from list (RemoveEntryList)
    ↓
4. Release lock
    ↓
5. Unmap user-mode address (MmUnmapLockedPages)
    ↓
6. Free MDL (IoFreeMdl)
    ↓
7. Free ring buffer
    ↓
8. Dereference kernel event (ObDereferenceObject)
    ↓
9. Free subscription structure
```

## Testing Strategy

**Unit Tests** (user-mode):
- Subscription lifecycle (subscribe → receive events → unsubscribe)
- Ring buffer correctness (write/read indices, wraparound)
- Overflow handling (buffer full scenarios)
- Multi-subscriber (same adapter, different event handles)

**Performance Tests**:
- Event delivery latency (<100µs kernel→user-mode)
- High-frequency events (10,000 events/sec sustained)
- Memory usage (ring buffer size vs. event rate)

**Stress Tests**:
- Concurrent subscriptions (multiple applications)
- Subscribe/unsubscribe during event generation (race conditions)
- Application crash cleanup (orphaned subscriptions)

**Integration Tests**:
- TX timestamp capture → event notification → user-mode reception
- RX timestamp capture → event notification → user-mode reception
- Multi-adapter (different adapters, independent subscriptions)

## Status

**Implementation Status**: To Be Implemented  
**Code Files**: `timestamp_event_subscription.c` (planned), `timestamp_ring_buffer.h` (planned)  
**Dependencies**: Requires AVB Integration Layer (#139) and Hardware Access Layer (#140)  
**Owner**: TBD  
**Standards**: ISO/IEC/IEEE 12207:2017 (Implementation Process), IEEE 1588 (PTP timestamp precision)
