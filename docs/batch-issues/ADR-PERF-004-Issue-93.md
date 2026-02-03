# ADR-PERF-004: Kernel-Mode Shared Memory Ring Buffer for High-Frequency Timestamp Events

**Issue**: #93  
**Type**: Architecture Decision Record (ADR)  
**Priority**: P1 (High)  
**Status**: In Progress  
**Phase**: 03 - Architecture Design

## Description
Implement kernel-mode shared memory ring buffer for high-frequency timestamp event delivery to user-mode applications without IOCTL overhead.

## Context
PTP synchronization and TSN monitoring require delivery of timestamp events (TX/RX timestamps, clock adjustments) to user-mode at high frequency (100-1000 events/sec). Standard IOCTL polling introduces:
- >10µs per IOCTL call (user → kernel → user transitions)
- CPU overhead from frequent syscalls
- Event latency from polling delays

## Decision
**Implement lock-free, single-producer/single-consumer (SPSC) ring buffer in shared kernel/user memory** for zero-copy timestamp event delivery.

**Rationale**:
1. **Performance**: Ring buffer eliminates IOCTL overhead (user reads directly from shared memory)
2. **Scalability**: Supports 1000+ events/sec with <1% CPU overhead
3. **Predictability**: Deterministic write latency (no syscall overhead)
4. **Event ordering**: FIFO guarantees temporal ordering of timestamps

**Alternatives Considered**:
- **IOCTL polling**: Current implementation, rejected due to >10µs overhead per event
- **Event objects + WaitForSingleObject**: Rejected (still requires syscall per event)
- **Named pipes**: Rejected (high overhead, complex lifecycle)

## Consequences

### Positive
- ✅ Zero-copy event delivery (no kernel ↔ user data copies)
- ✅ <1µs event delivery latency (vs. >10µs IOCTL)
- ✅ Lock-free (no mutex overhead)

### Negative
- ❌ Complex memory management (shared section, MDL mapping)
- ❌ User-mode must poll ring buffer (cannot sleep on events)
- ❌ Fixed buffer size (overflow drops events)

### Risks
- **Buffer overflow**: Mitigated by configurable buffer size + overflow counters
- **Memory leaks**: Mitigated by careful MDL cleanup on process exit
- **Security**: Shared memory must be process-isolated (not global)

## Implementation Notes

**Ring Buffer Structure** (`avb_ring_buffer.h`):
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
} event_entry_t;
```

**Kernel Write (Producer)**:
```c
BOOLEAN RingBufferWrite(ring_buffer_t *rb, event_entry_t *event) {
    ULONG head = rb->head;
    ULONG tail = READ_ACQUIRE(&rb->tail);
    
    if ((head - tail) >= rb->capacity) {
        InterlockedIncrement64(&rb->dropped_events);
        return FALSE;  // Buffer full
    }
    
    event_entry_t *entry = &rb->data[head & rb->mask];
    memcpy(entry, event, sizeof(event_entry_t));
    
    WRITE_RELEASE(&rb->head, head + 1);
    return TRUE;
}
```

**User Read (Consumer)**:
```c
BOOLEAN RingBufferRead(ring_buffer_t *rb, event_entry_t *event) {
    ULONG tail = rb->tail;
    ULONG head = READ_ACQUIRE(&rb->head);
    
    if (tail == head) return FALSE;  // Buffer empty
    
    event_entry_t *entry = &rb->data[tail & rb->mask];
    memcpy(event, entry, sizeof(event_entry_t));
    
    WRITE_RELEASE(&rb->tail, tail + 1);
    return TRUE;
}
```

**Memory Mapping** (`avb_ring_buffer.c`):
```c
NTSTATUS AvbCreateSharedRingBuffer(HANDLE ProcessHandle, ring_buffer_t **KernelVA, ring_buffer_t **UserVA) {
    // 1. Allocate non-paged pool
    ULONG size = sizeof(ring_buffer_t) + RING_BUFFER_CAPACITY * sizeof(event_entry_t);
    *KernelVA = ExAllocatePoolWithTag(NonPagedPool, size, 'RBUF');
    
    // 2. Create MDL
    PMDL mdl = IoAllocateMdl(*KernelVA, size, FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(mdl);
    
    // 3. Map to user-mode
    __try {
        *UserVA = MmMapLockedPagesSpecifyCache(mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    return STATUS_SUCCESS;
}
```

## Performance Targets

| Metric | Target | Measured (Planned) |
|--------|--------|-------------------|
| Write latency | <500ns | TBD |
| Read latency | <1µs | TBD |
| Throughput | 1000 events/sec | TBD |
| CPU overhead | <1% at 1000 events/sec | TBD |

## Compliance

**Standards**: ISO/IEC/IEEE 12207:2017 (Implementation Process)  
**Safety**: IEC 61508 (Lock-free algorithms for real-time systems)

## Traceability

Traces to: #6 (REQ-F-PTP-005), #71 (REQ-NF-PERF-001), #83 (REQ-F-PERF-MONITOR-001)

**Addresses**:
- #6 (REQ-F-PTP-005: Get TX Timestamp - high-frequency event delivery)
- #71 (REQ-NF-PERF-001: <1µs Latency - eliminate IOCTL overhead)
- #83 (REQ-F-PERF-MONITOR-001: Export Performance Counters - event statistics)

**Implemented by**:
- #95 (ARC-C-002: AVB Integration Layer - planned)

---

**Status**: Proposed (Implementation pending)  
**Deciders**: Architecture Team, Performance Team  
**Date**: 2024-12-08  
**Supersedes**: None

**Next Steps**:
1. Prototype ring buffer implementation
2. Measure performance (write/read latency, throughput)
3. Security review (shared memory isolation)
4. User-mode library for ring buffer access
