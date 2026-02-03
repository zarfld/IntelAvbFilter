# ARC-C-PTP-MONITOR: PTP Timestamp Event Monitor (Event Emission + Correlation)

**Issue**: #148  
**Type**: Architecture Component (ARC-C)  
**Priority**: High  
**Status**: Backlog  
**Phase**: 03 - Architecture Design

## Traceability

- Traces to:  #147 (ADR-PTP-001: Event Emission Architecture)
- **Depends on**: ADR-PERF-004 (Kernel Ring Buffer infrastructure)

## Component Type

**Integration Adapter (INTG)** - Bridges driver timestamps with user-mode packet capture

## Responsibility

The PTP Timestamp Event Monitor is responsible for:

**Driver-Side (Kernel Mode)**:
- Detecting PTP frames (EtherType 0x88F7) in RX/TX datapath
- Reading hardware timestamps from NIC PTP registers
- Extracting correlation keys (messageType, sequenceId, MAC, VLAN, PCP)
- Emitting timestamp events to lock-free ring buffer
- Managing ring buffer lifecycle (create, map, cleanup)

**User-Side (User Mode)**:
- Reading timestamp events from shared memory ring buffer
- Capturing PTP frames via Npcap
- Correlating driver events with captured packets
- Providing APIs for PTP synchronization analysis
- Visualizing latency, jitter, and path asymmetry

## Interfaces (Provided)

### Kernel-Mode Driver Interface

```c
// avb_ptp_monitor.h

// Initialize PTP monitoring
NTSTATUS AvbPtpMonitorInit(PAVB_DEVICE_CONTEXT Ctx);

// Enable/disable monitoring
NTSTATUS AvbPtpMonitorEnable(PAVB_DEVICE_CONTEXT Ctx, BOOLEAN Enable);

// Emit event (called from datapath)
VOID AvbPtpEmitTimestampEvent(
    PAVB_DEVICE_CONTEXT Ctx,
    PNET_BUFFER_LIST Nbl,
    UINT64 HwTimestampNs,
    UINT8 Direction  // 0=RX, 1=TX
);

// Cleanup
VOID AvbPtpMonitorCleanup(PAVB_DEVICE_CONTEXT Ctx);
```

### IOCTL Interface

```c
// Enable monitoring
IOCTL_AVB_ENABLE_PTP_MONITORING
  Input: None
  Output: SharedMemoryHandle (for ring buffer mapping)

// Disable monitoring  
IOCTL_AVB_DISABLE_PTP_MONITORING
  Input: None
  Output: None

// Get statistics
IOCTL_AVB_GET_PTP_MONITOR_STATS
  Input: None
  Output: struct { events_emitted, events_dropped, buffer_size }
```

### User-Mode Python API

```python
# ptp_monitor.py

class PtpMonitor:
    def __init__(self, interface_name: str):
        """Initialize monitor for network interface"""
    
    def enable(self):
        """Enable PTP event emission in driver"""
    
    def read_events(self) -\u003e Iterator[PtpTimestampEvent]:
        """Yield timestamp events from ring buffer"""
    
    def correlate_with_npcap(self, npcap_capture) -\u003e Iterator[CorrelatedEvent]:
        """Match driver events with Npcap packets"""
    
    def get_statistics(self) -\u003e dict:
        """Get monitoring stats (events, drops, etc.)"""
```

## Dependencies (Required)

### Internal Components
- **ADR-PERF-004**: Kernel Ring Buffer (shared memory, lock-free SPSC queue)
- **avb_hardware_access.c**: PTP register access (`AvbReadTimestamp`)
- **Filter datapath**: `FilterReceiveNetBufferLists`, `FilterSendNetBufferLists`

### External Dependencies
- **Npcap**: Packet capture library (user-mode)
- **Python packages**: pyshark or scapy (for Npcap integration)

### Windows APIs
- `ZwCreateSection` / `ZwMapViewOfSection` (shared memory)
- `MmBuildMdlForNonPagedPool` (MDL for user mapping)
- `KeQueryPerformanceCounter` (system timestamp correlation)

## Data Ownership

### Event Data Structure

```c
typedef struct _PTP_TIMESTAMP_EVENT {
    // Timing
    UINT64  hw_timestamp_ns;    // Hardware PTP timestamp
    UINT64  system_timestamp;   // KeQueryPerformanceCounter
    
    // Direction
    UINT8   direction;          // 0=RX, 1=TX
    
    // Correlation Key
    UINT8   messageType;        // PTP messageType (0-15)
    UINT16  sequenceId;         // PTP sequence number
    UINT8   domainNumber;       // PTP domain (0-255)
    UCHAR   sourceMac[6];       // Source MAC address
    UINT16  vlanId;             // VLAN ID (0 if untagged)
    UINT8   pcp;                // Priority Code Point
    UINT8   queueId;            // Hardware queue number
    
    // Reserved
    UINT32  reserved[6];        // Padding to 64 bytes
} PTP_TIMESTAMP_EVENT;
```

### Ring Buffer Metadata

```c
typedef struct {
    volatile ULONG head;        // Producer write index
    volatile ULONG tail;        // Consumer read index
    ULONG capacity;             // 4096 entries (default)
    ULONGLONG dropped_events;   // Overflow counter
    UCHAR data[0];              // PTP_TIMESTAMP_EVENT[capacity]
} ptp_ring_buffer_t;
```

## Design Constraints

### Technology Stack
- **Driver**: C (Windows Kernel, NDIS 6.0)
- **User Library**: Python 3.8+ (cross-platform tool)
- **Packet Capture**: Npcap (required dependency)

### Performance Requirements
- **Datapath overhead**: <1µs added latency per PTP packet
- **CPU overhead**: <1% at 1000 PTP packets/sec
- **Event throughput**: Support 10,000 events/sec without drops

### Memory Constraints
- **Ring buffer size**: 4096 entries × 64 bytes = 256 KB (non-paged pool)
- **Max instances**: 1 ring buffer per network adapter

### Security Constraints
- **Per-process isolation**: Shared memory accessible only to creating process
- **No privilege escalation**: User-mode cannot write to ring buffer
- **Validation**: Bounds checking on all PTP header parsing

## Non-Functional Requirements

### Performance
- Event emission latency: <500 CPU cycles
- Lock-free ring buffer writes (no mutex)
- Zero-copy event delivery

### Reliability
- Graceful overflow handling (drop events, increment counter)
- No packet drops from driver monitoring
- Ring buffer cleanup on process exit

### Usability
- Python API for easy integration
- Reference correlation tool provided
- Clear documentation with examples

### Testability
- Unit tests for PTP header parsing
- Integration tests for end-to-end correlation
- Performance benchmarks (latency, throughput)

## Acceptance Criteria

- [ ] Driver emits events for PTP RX/TX packets (EtherType 0x88F7)
- [ ] Hardware timestamps read from NIC PTP registers
- [ ] Correlation keys extracted correctly (messageType, sequenceId, MAC, VLAN, PCP, queue)
- [ ] Ring buffer supports 1000+ events/sec without drops
- [ ] User-mode library successfully correlates Npcap packets with driver events
- [ ] Added datapath latency <1µs (measured via GPIO instrumentation)
- [ ] CPU overhead <1% at 1000 pps (measured via ETW)
- [ ] No packet drops observed with monitoring enabled
- [ ] Per-process ring buffer isolation verified (security test)
- [ ] Reference Python tool demonstrates end-to-end correlation

## Implementation Phases

### Phase 1: Event Emission Infrastructure (Week 1)
- [ ] Define `PTP_TIMESTAMP_EVENT` structure (`avb_ptp_events.h`)
- [ ] Integrate with ADR-PERF-004 ring buffer
- [ ] Add EtherType 0x88F7 detection in `FilterReceiveNetBufferLists`
- [ ] Read hardware timestamp from PTP registers (`AvbReadTimestamp`)
- [ ] Emit events to ring buffer (`RingBufferWrite`)
- [ ] Add IOCTL for user-mode to map shared memory

### Phase 2: Correlation Key Extraction (Week 2)
- [ ] Parse PTP header minimally (messageType, sequenceId, domain)
- [ ] Extract VLAN/PCP from `NET_BUFFER_LIST` metadata
- [ ] Add TX path monitoring in `FilterSendNetBufferLists`
- [ ] Handle edge cases (fragmented NBLs, chained NBs)

### Phase 3: User-Mode Tooling (Week 3)
- [ ] Python library for ring buffer access (`ptp_monitor.py`)
- [ ] Npcap integration (pyshark or scapy)
- [ ] Correlation algorithm (sequence ID + MAC matching)
- [ ] Visualization (latency histograms, jitter plots)

## References

- **ADR Document**: `03-architecture/decisions/ADR-PTP-001-event-emission-vs-packet-io.md`
- **GitHub Issue**: #147 (ADR-PTP-001)
- **Related Component**: ADR-PERF-004 (Kernel Ring Buffer)
- **Standards**: IEEE 1588-2019 (PTP), IEEE 802.1AS-2020 (gPTP)
