# REQ-F-TSRING-001: Shared Memory Ring Buffer for Timestamp Events

**Issue**: #19  
**Type**: Functional Requirement  
**Priority**: P0 (Critical)  
**Status**: Backlog  
**Phase**: 05 - Implementation

## Summary

Implement shared memory ring buffer between kernel driver and user-mode applications for zero-copy, high-throughput timestamp event delivery.

## Business Context

**Problem**: Timestamp events (Rx/Tx packet timestamps, target time interrupts, auxiliary timestamps) need **low-latency, high-throughput** delivery to user-mode applications without excessive copying or context switching.

**Traditional Approach Limitations**:
- ❌ IOCTL per event → 10-50µs overhead per event
- ❌ Copy timestamp from kernel → user-mode buffer → extra memory copy
- ❌ Context switches → CPU scheduling latency
- ❌ Doesn't scale to 1000s of timestamps/second (AVB streaming, PTP sync)

**Discovered Solution**: **Shared memory ring buffer** mapped into both kernel-mode and user-mode address spaces, enabling:
- ✅ **Zero-copy**: Driver writes directly to shared buffer
- ✅ **Lock-free**: Producer (driver) and consumer (app) use atomic indices
- ✅ **Low latency**: <1µs to post timestamp event
- ✅ **High throughput**: Supports 10K+ events/second

## Discovered Implementation Pattern

**Ring Buffer Structure** (64 bytes per entry):

```c
typedef struct _PTP_TIMESTAMP_EVENT {
    UINT64  hw_timestamp_ns;    // Hardware PTP timestamp
    UINT64  system_timestamp;   // KeQueryPerformanceCounter
    UINT8   direction;          // 0=RX, 1=TX
    UINT8   messageType;        // PTP messageType
    UINT16  sequenceId;         // PTP sequence number
    UINT8   domainNumber;       // PTP domain
    UCHAR   sourceMac[6];       // Source MAC
    UINT16  vlanId;             // VLAN ID
    UINT8   pcp;                // Priority Code Point
    UINT8   queueId;            // Hardware queue
    // ... padding to 64 bytes
} PTP_TIMESTAMP_EVENT;
```

## IOCTLs

- `IOCTL_AVB_TS_SUBSCRIBE` (code 33): Subscribe to timestamp events
- `IOCTL_AVB_TS_RING_MAP` (code 34): Map shared ring buffer

## Functional Requirements

### REQ-F-TSRING-001.1: Ring Buffer Allocation and Subscription

**Given** application needs to receive timestamp events  
**When** application calls `IOCTL_AVB_TS_SUBSCRIBE`  
**Then** driver:
- Allocates unique ring ID (non-zero)
- Applies event type filters (TX_TS, RX_TS, TARGET_TIME, AUX_TS)
- Applies VLAN/PCP filters if specified
- Returns ring ID to application

### REQ-F-TSRING-001.2: Shared Memory Ring Buffer Mapping

**Given** application subscribed to timestamp events (has ring ID)  
**When** application calls `IOCTL_AVB_TS_RING_MAP`  
**Then** driver:
- Allocates non-paged pool memory (requested length, rounded to page size)
- Creates section object backed by allocated memory
- Maps section into system address space (kernel view)
- Returns section handle (`shm_token`) to user-mode
- User-mode maps section into its address space using `ZwMapViewOfSection()`

### REQ-F-TSRING-001.3: Lock-Free Ring Buffer Protocol

**Producer (Kernel-Mode Driver)**:
```c
BOOLEAN PostTimestampEvent(TS_RING_BUFFER* ring, TS_EVENT_ENTRY* event) {
    ULONG prod = ring->producer_index;
    ULONG cons = READ_VOLATILE(&ring->consumer_index);
    ULONG next = (prod + 1) % ring->capacity;
    
    // Check if ring full
    if (next == cons) {
        return FALSE;  // Ring full, drop event (or overwrite oldest)
    }
    
    // Write event to ring
    memcpy(&ring->entries[prod], event, sizeof(TS_EVENT_ENTRY));
    
    // Advance producer index (memory barrier ensures visibility)
    _WriteBarrier();
    ring->producer_index = next;
    return TRUE;
}
```

**Consumer (User-Mode Application)**:
```c
BOOLEAN ConsumeTimestampEvent(TS_RING_BUFFER* ring, TS_EVENT_ENTRY* event) {
    ULONG cons = ring->consumer_index;
    ULONG prod = READ_VOLATILE(&ring->producer_index);
    
    // Check if ring empty
    if (cons == prod) {
        return FALSE;  // No events available
    }
    
    // Read event from ring
    memcpy(event, &ring->entries[cons], sizeof(TS_EVENT_ENTRY));
    
    // Advance consumer index
    _ReadWriteBarrier();
    ring->consumer_index = (cons + 1) % ring->capacity;
    return TRUE;
}
```

## Performance Requirements

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Event posting latency** | <1µs | Time from hardware timestamp to ring write |
| **Ring throughput** | 10K events/sec | Typical AVB audio streaming (48kHz * 2 channels) |
| **Ring capacity** | 1024-4096 entries | Adjustable based on application needs |
| **Memory footprint** | 64-256KB per ring | `capacity * sizeof(TS_EVENT_ENTRY)` |
| **Mapping overhead** | <5ms | Time for `MapViewOfFile()` in UM |

## Hardware Support

All Intel controllers support timestamp event generation:
- **I210**: Rx/Tx packet timestamps, target time interrupts
- **I217/I219**: Limited timestamp support (Rx only)
- **I225/I226**: Full timestamp support (Rx/Tx, target time, auxiliary timestamps)

## Security Considerations

- Section object **only accessible** to process that called `IOCTL_AVB_TS_RING_MAP`
- No cross-process mapping (no shared HANDLE inheritance)
- Driver validates `ring_id` matches calling process before posting events
- Non-paged pool allocations limited to prevent DoS (max 256KB per ring)

## Traceability

- Traces to: #1 (StR-HWAC-001: Intel NIC AVB/TSN Feature Access), #13 (REQ-F-TS-SUB-001: Timestamp Event Subscription)
- **Implements IOCTLs**: 
  - `IOCTL_AVB_TS_SUBSCRIBE` (code 33)
  - `IOCTL_AVB_TS_RING_MAP` (code 34)
- Related to: #13 (REQ-F-TS-SUB-001: Timestamp Event Subscription)
- Verified by: To be created (TEST-TSRING-001)
