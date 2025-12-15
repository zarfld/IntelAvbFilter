# ADR-PERF-004: Kernel-Mode Shared Memory Ring Buffer for Timestamp Events

**Status**: Proposed (Implementation Pending)  
**Date**: 2025-12-08  
**GitHub Issue**: #93  
**Phase**: Phase 03 - Architecture Design  
**Priority**: High (P1)

---

## Context

PTP synchronization and TSN monitoring require delivery of timestamp events (TX/RX timestamps, clock adjustments) to user-mode applications at high frequency (100-1000 events/sec).

**Current Limitation**: Standard IOCTL polling introduces:
- >10µs per IOCTL call (user → kernel → user transitions)
- High CPU overhead from frequent syscalls
- Event latency from polling delays

**Problem**: How to deliver high-frequency timestamp events to user-mode without IOCTL overhead?

---

## Decision

**Implement lock-free, single-producer/single-consumer (SPSC) ring buffer in shared kernel/user memory** for zero-copy timestamp event delivery.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  User-Mode Application                                       │
│  - Polls ring buffer: while (RingBufferRead(&rb, &event))  │
│  - Zero-copy read (direct memory access)                    │
└──────────────────┬──────────────────────────────────────────┘
                   │ Shared Memory (MDL-mapped)
┌──────────────────┴──────────────────────────────────────────┐
│  Ring Buffer (Kernel + User Virtual Addresses)              │
│  - head (producer write index)                               │
│  - tail (consumer read index)                                │
│  - data[capacity] (event_entry_t array)                      │
└──────────────────┬──────────────────────────────────────────┘
                   │ Kernel writes
┌──────────────────┴──────────────────────────────────────────┐
│  Kernel-Mode Driver (IntelAvbFilter)                         │
│  - Writes events: RingBufferWrite(&rb, &event)              │
│  - Lock-free (no mutex overhead)                             │
└─────────────────────────────────────────────────────────────┘
```

### Rationale

1. **Performance**: Ring buffer eliminates IOCTL overhead
   - User reads directly from shared memory (no syscall)
   - Zero-copy event delivery

2. **Scalability**: Supports 1000+ events/sec with <1% CPU overhead
   - Lock-free algorithm (no mutex contention)
   - Predictable write latency (<1µs)

3. **Event Ordering**: FIFO guarantees temporal ordering of timestamps
   - Critical for PTP synchronization algorithms

4. **Simplicity**: Standard ring buffer pattern
   - Well-understood semantics
   - Easy to debug

---

## Alternatives Considered

### Alternative 1: IOCTL Polling (Current Implementation)

**Implementation**: User-mode polls `IOCTL_GET_TX_TIMESTAMP` repeatedly

**Pros**:
- Standard Windows driver pattern
- No shared memory management

**Cons**:
- ❌ >10µs overhead per IOCTL call
- ❌ High CPU usage at 1000 events/sec (~10% CPU)
- ❌ Event latency from polling frequency

**Decision**: **Rejected** - Cannot meet performance requirements

### Alternative 2: Event Objects + WaitForSingleObject

**Implementation**: Kernel signals event object when timestamp available

**Pros**:
- Blocking wait (no busy polling)
- Standard Windows synchronization primitive

**Cons**:
- ❌ Still requires IOCTL to retrieve timestamp data (>10µs)
- ❌ Context switch overhead per event
- ❌ Complex lifecycle (event cleanup on process exit)

**Decision**: **Rejected** - Still requires syscall per event

### Alternative 3: Named Pipes

**Implementation**: Use `CreateNamedPipe()` for kernel → user communication

**Pros**:
- Standard IPC mechanism
- Bidirectional communication

**Cons**:
- ❌ High overhead (~50µs per message)
- ❌ Complex lifecycle management
- ❌ Not designed for high-frequency events

**Decision**: **Rejected** - Too much overhead

---

## Consequences

### Positive
- ✅ Zero-copy event delivery (no kernel ↔ user data copies)
- ✅ <1µs event delivery latency (vs. >10µs IOCTL)
- ✅ Lock-free (no mutex overhead)
- ✅ Scalable to 1000+ events/sec

### Negative
- ❌ Complex memory management (shared section, MDL mapping)
- ❌ User-mode must poll ring buffer (cannot sleep on events)
- ❌ Fixed buffer size (overflow drops events)
- ❌ Security: Shared memory must be process-isolated

### Risks
- **Buffer Overflow**: High event rate exceeds capacity
  - **Mitigation**: Configurable buffer size + overflow counters
- **Memory Leaks**: Shared section not cleaned up on process exit
  - **Mitigation**: Careful MDL cleanup, process handle tracking
- **Security**: Malicious process reads other process's events
  - **Mitigation**: Per-process shared sections (not global)

---

## Implementation

### Ring Buffer Structure (`avb_ring_buffer.h`)

```c
typedef struct {
    volatile ULONG head;       // Producer (kernel) write index
    volatile ULONG tail;       // Consumer (user) read index
    ULONG capacity;            // Power of 2 (e.g., 4096 entries)
    ULONG mask;                // capacity - 1 (for fast modulo)
    volatile ULONGLONG dropped_events;  // Overflow counter
    UCHAR data[0];             // Flexible array (event_entry_t[capacity])
} ring_buffer_t;

typedef struct {
    u64 timestamp_ns;          // Event timestamp (PTP time)
    u32 event_type;            // TX_TSTAMP, RX_TSTAMP, CLOCK_ADJUST
    u32 queue_id;              // TXQ/RXQ index
    u64 value;                 // Timestamp or adjustment value
    u32 reserved[2];           // Padding to 32 bytes
} event_entry_t;

#define RING_BUFFER_CAPACITY 4096  // Power of 2
```

### Kernel Write (Producer) (`avb_ring_buffer.c`)

```c
BOOLEAN RingBufferWrite(ring_buffer_t *rb, event_entry_t *event) {
    ULONG head = rb->head;
    ULONG tail = READ_ACQUIRE(&rb->tail);
    
    // Check if buffer full
    if ((head - tail) >= rb->capacity) {
        InterlockedIncrement64(&rb->dropped_events);
        return FALSE;  // Buffer full, event dropped
    }
    
    // Write event to ring buffer
    event_entry_t *entry = (event_entry_t*)&rb->data[(head & rb->mask) * sizeof(event_entry_t)];
    memcpy(entry, event, sizeof(event_entry_t));
    
    // Publish new head (release semantics)
    WRITE_RELEASE(&rb->head, head + 1);
    return TRUE;
}
```

### User Read (Consumer) (User-Mode Library)

```c
BOOLEAN RingBufferRead(ring_buffer_t *rb, event_entry_t *event) {
    ULONG tail = rb->tail;
    ULONG head = READ_ACQUIRE(&rb->head);
    
    // Check if buffer empty
    if (tail == head) {
        return FALSE;  // No events available

---

## Architecture Diagrams

The kernel ring buffer timestamp event architecture is visualized in the following C4 diagram:

### Sequence Diagram - Timestamp Event Subscription & Delivery
**[Sequence Diagram - Timestamp Event Subscription & Delivery](../C4-DIAGRAMS-MERMAID.md#sequence-timestamp-event-subscription)**

Illustrates the complete flow of high-frequency timestamp event delivery:

**Phase 1: Subscription Setup**
1. **User Application** → Subscribe request (IOCTL_AVB_SUBSCRIBE_EVENTS)
2. **IOCTL Dispatcher** → Route to Event Manager (<5µs)
3. **Event Manager** → Allocate shared memory ring buffer (4096 entries)
4. **Event Manager** → Map into user-mode virtual address space
5. **Response** → Return MDL handle to application

**Phase 2: Event Production (Kernel, 100-1000 Hz)**
1. **Hardware Interrupt** → TX/RX timestamp capture
2. **Event Manager** → Lock-free ring buffer write (SPSC pattern)
   - **Producer (Kernel)**: Atomic head increment
   - **Consumer (User-Mode)**: Atomic tail increment
   - **No Locks**: Zero contention, deterministic latency
3. **Memory Barriers** → Ensure visibility across CPU cores

**Phase 3: Event Consumption (User-Mode)**
1. **User Application** → Poll shared memory ring buffer (no IOCTL overhead)
2. **Ring Buffer Read** → Check `head != tail`, read event, increment tail
3. **Processing** → Application-level timestamp event handling

**Performance Characteristics**:
- **Write Latency**: <2µs (kernel producer)
- **Read Latency**: <500ns (user consumer, no syscall)
- **Throughput**: 100-1000 events/sec sustained
- **Overflow Handling**: Dropped event counter (no blocking)

### Container Diagram - Event Manager Integration
**[C4 Container Diagram - Event Manager](../C4-DIAGRAMS-MERMAID.md#l2-container-diagram)**

Shows how the Event Manager integrates with:
- **IOCTL Dispatcher**: Subscription request routing
- **Hardware Access Layer**: Timestamp capture callbacks
- **Shared Memory**: MDL-backed ring buffer (kernel/user-mode)

**Key Insights**:
- **Lock-Free SPSC**: Single-Producer Single-Consumer eliminates lock overhead
- **Shared Memory**: Bypasses IOCTL path for reads (no context switch)
- **Memory Barriers**: `READ_ACQUIRE` / `WRITE_RELEASE` ensure correct ordering
- **Overflow Strategy**: Drop events (increment counter) vs. blocking producer
- **Power-of-2 Capacity**: Fast modulo via bitmask (`head & (capacity - 1)`)

For complete architecture documentation, see [C4-DIAGRAMS-MERMAID.md](../C4-DIAGRAMS-MERMAID.md).
    }
    
    // Read event from ring buffer
    event_entry_t *entry = (event_entry_t*)&rb->data[(tail & rb->mask) * sizeof(event_entry_t)];
    memcpy(event, entry, sizeof(event_entry_t));
    
    // Publish new tail (release semantics)
    WRITE_RELEASE(&rb->tail, tail + 1);
    return TRUE;
}
```

### Memory Mapping (`avb_ring_buffer.c`)

```c
NTSTATUS AvbCreateSharedRingBuffer(
    _In_ HANDLE ProcessHandle,
    _Out_ ring_buffer_t **KernelVA,
    _Out_ ring_buffer_t **UserVA
)
{
    // Calculate buffer size
    ULONG size = sizeof(ring_buffer_t) + 
                 RING_BUFFER_CAPACITY * sizeof(event_entry_t);
    
    // 1. Allocate non-paged pool (kernel virtual address)
    *KernelVA = (ring_buffer_t*)ExAllocatePoolWithTag(NonPagedPool, size, 'RBUF');
    if (!*KernelVA) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize ring buffer
    RtlZeroMemory(*KernelVA, size);
    (*KernelVA)->capacity = RING_BUFFER_CAPACITY;
    (*KernelVA)->mask = RING_BUFFER_CAPACITY - 1;
    
    // 2. Create MDL
    PMDL mdl = IoAllocateMdl(*KernelVA, size, FALSE, FALSE, NULL);
    if (!mdl) {
        ExFreePoolWithTag(*KernelVA, 'RBUF');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    MmBuildMdlForNonPagedPool(mdl);
    
    // 3. Map to user-mode virtual address
    __try {
        *UserVA = (ring_buffer_t*)MmMapLockedPagesSpecifyCache(
            mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        IoFreeMdl(mdl);
        ExFreePoolWithTag(*KernelVA, 'RBUF');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    if (!*UserVA) {
        IoFreeMdl(mdl);
        ExFreePoolWithTag(*KernelVA, 'RBUF');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    KdPrint(("AvbCreateSharedRingBuffer: KernelVA=%p, UserVA=%p, Size=0x%X\n",
             *KernelVA, *UserVA, size));
    
    return STATUS_SUCCESS;
}
```

---

## Performance Targets

| Metric | Target | Measured (Planned) |
|--------|--------|-------------------|
| Write latency | <500ns | TBD |
| Read latency | <1µs | TBD |
| Throughput | 1000 events/sec | TBD |
| CPU overhead | <1% at 1000 events/sec | TBD |

---

## Compliance

**Standards**: ISO/IEC/IEEE 12207:2017 (Implementation Process)  
**Safety**: IEC 61508 (Lock-free algorithms for real-time systems)

---

## Traceability

Traces to: 
- #6 (REQ-F-PTP-005: Get TX Timestamp - high-frequency event delivery)
- #71 (REQ-NF-PERF-001: <1µs Latency - eliminate IOCTL overhead)
- #83 (REQ-F-PERF-MONITOR-001: Export Performance Counters - event statistics)

**Implemented by**:
- #95 (ARC-C-002: AVB Integration Layer - planned)

**Verified by**:
- TEST-RINGBUF-001: Ring Buffer Performance Tests

---

## Status

**Current Status**: **Proposed** (Implementation Pending)

**Decision Made By**: Architecture Team, Performance Team

**Stakeholder Approval**:
- [x] Driver Architecture Team
- [x] Performance Engineering Team
- [ ] Security Team (pending shared memory security review)
- [ ] Integration Team (pending prototype validation)

**Rationale for Proposal**:
- Eliminates >10µs IOCTL overhead via zero-copy shared memory
- Supports high-frequency timestamp events (100-1000 events/sec)
- Lock-free SPSC design minimizes CPU overhead
- Proven pattern for low-latency event delivery

**Implementation Status**: Proposed (awaiting prototype + security review)

---

## Approval

**Approval Criteria** (To Be Met Before Acceptance):
- [ ] Prototype implementation completed
- [ ] Performance validated (<1µs write latency, <500ns read latency)
- [ ] Security review passed (shared memory isolation, overflow handling)
- [ ] Lock-free algorithm verified (memory barriers, race condition testing)
- [ ] User-mode library implemented and tested
- [ ] Buffer capacity tuned (4096 entries default validated)

**Review History**:
- **2025-12-08**: Proposed (Architecture + Performance Teams)
- **2025-12-08**: Design documented (SPSC ring buffer + MDL mapping)
- **Pending**: Prototype implementation
- **Pending**: Security review
- **Pending**: Performance benchmarks

**Next Review Date**: After prototype implementation (Week 1 of implementation phase)

**Path to Acceptance**:
1. Implement prototype ring buffer (Week 1)
2. Measure performance (write/read latency, throughput)
3. Security review (shared memory isolation)
4. User-mode library implementation
5. Update status to "Accepted" after validation

---

## Notes

- Lock-free algorithm requires proper memory barriers (acquire/release semantics)
- Buffer capacity must be power of 2 for fast modulo operation (bitwise AND)
- Overflow counter allows monitoring event loss without additional IOCTLs

---

## Next Steps

1. Prototype ring buffer implementation
2. Measure performance (write/read latency, throughput)
3. Security review (shared memory isolation)
4. User-mode library for ring buffer access
5. Update to "Accepted" status after prototype validation

---

**Last Updated**: 2025-12-15  
**Author**: Architecture Team, Performance Team  
**Document Version**: 1.0 (Proposed)
