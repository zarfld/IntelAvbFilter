# ADR-PTP-001: Event Emission Architecture for PTP Timestamp Correlation (vs. Packet I/O)

**Issue**: #147  
**Type**: Architecture Decision Record (ADR)  
**Priority**: High  
**Status**: In Progress  
**Phase**: 03 - Architecture Design

## Traceability

Traces to: #149 (REQ-F-PTP-001: Hardware Timestamp Correlation for PTP/gPTP Protocol Analysis)

**Related Components**: #148 (ARC-C-PTP-MONITOR: PTP Timestamp Event Monitor)

**Related ADRs**:
- ADR-PERF-004 (Kernel Ring Buffer - provides infrastructure)
- ADR-SCOPE-001 (gPTP Architecture Separation)

## Decision Status

**Status**: Proposed

**Category**: Performance (PERF), Integration (INTG)

## Context

PTP synchronization monitoring and gPTP L2 protocol analysis require correlation of:
1. **Hardware timestamps** (from NIC PTP clock) - nanosecond precision
2. **Packet data** (Ethernet frames) - PTP protocol content  
3. **Stack processing metadata** - Queue ID, VLAN, PCP, direction

**Current Capability Gap**:
- Driver has access to hardware timestamps via PTP registers
- User-mode tools (Wireshark/Npcap) capture packet bytes
- **No mechanism to correlate**: timestamp ↔ packet ↔ metadata

**Architectural Question**: 
> How should the driver deliver timestamp + metadata to user-mode for correlation with captured packets?

**Forces in Conflict**:
1. **Performance**: Minimize datapath overhead (<1µs added latency)
2. **Simplicity**: Avoid duplicating existing packet capture (Npcap)
3. **NDIS Compliance**: No packet queuing, no datapath modifications
4. **Correlation Accuracy**: Unique keys to match timestamp ↔ packet
5. **Security**: No cross-process data leakage

## Decision

**We will implement event emission architecture with lock-free ring buffer for timestamp metadata delivery, NOT packet I/O via IOCTL.**

### Architecture Overview

```
User-Mode Application
├── Npcap Capture (frame bytes)
├── Driver Events (timestamps via ring buffer)
└── Correlator (match by messageType + sequenceId + MAC)

NDIS Filter Driver
├── FilterReceiveNetBufferLists → EtherType 0x88F7 detector
├── Read HW timestamp from PTP registers
├── Extract correlation key (minimal PTP header parse)
└── Emit event to ring buffer (lock-free write)
```

### Event Structure (64 bytes)

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

### Why Event Emission Over Packet I/O

1. **Clean separation**: Driver timestamps, Npcap captures, user correlates
2. **No duplication**: Leverage Npcap's existing packet capture
3. **Minimal overhead**: 64-byte events vs. 1500-byte packet copies (~23× reduction)
4. **NDIS compliant**: No datapath modifications, pure metadata extraction
5. **Scalable**: Ring buffer handles burst traffic, no queue management
6. **Performance**: ~500 CPU cycles vs. ~5000 cycles for packet I/O

## Consequences

### Positive
- ✅ Zero-copy event delivery (<1µs latency)
- ✅ No packet duplication (leverages Npcap)
- ✅ NDIS compliant (transparent pass-through maintained)
- ✅ Minimal memory (64 bytes/event vs. 1500 bytes/packet)
- ✅ Scalable (1000+ events/sec with <1% CPU)
- ✅ Reuses ADR-PERF-004 ring buffer infrastructure

### Negative
- ❌ User polling required (cannot sleep on events without signaling)
- ❌ Fixed buffer size (overflow drops events if user-mode slow)
- ❌ Requires Npcap (dependency on third-party tool)
- ❌ Correlation complexity (user-mode must implement matching)

### Neutral
- 🔄 User-mode library needed (Python/C# wrapper)
- 🔄 Correlation tool development (reference implementation)
- 🔄 Documentation (PTP monitoring workflow guide)

## Alternatives Considered

### 1. Packet I/O via IOCTL ❌ REJECTED

**Why**: 
- Memory copying hell (1500 bytes × 2 copies)
- Queue management nightmare (where? how long? overflow?)
- NDIS violation (holding NET_BUFFER_LIST breaks protocol stack)
- Duplicates Npcap (wasted effort)
- Performance: ~5000 CPU cycles vs. ~500 cycles for events

### 2. WFP Callout ❌ REJECTED  

**Why**: Too complex, adds ~10µs latency, still requires correlation

### 3. ETW Events ⚠️ PARTIAL CONSIDERATION

**Why**: Higher overhead (~1-2µs), less control over ordering
**Status**: Consider for Phase 2 diagnostics (ring buffer for real-time)

### 4. Named Pipes ❌ REJECTED

**Why**: ~50µs overhead (50× slower than ring buffer)

## Quality Attribute Impact

| Quality Attribute | Impact | Notes |
|-------------------|--------|-------|
| Performance | ++ | <1µs event latency, ~500 cycles |
| Scalability | ++ | 1000+ events/sec, lock-free |
| Simplicity | ++ | Reuses Npcap, no packet queuing |
| Security | 0 | Per-process ring buffer |
| Usability | − | Requires Npcap + correlation tool |

## Implementation Plan

### Phase 1: Event Emission (Week 1)
- [ ] Define `PTP_TIMESTAMP_EVENT` structure
- [ ] EtherType 0x88F7 detection in FilterReceive/Send
- [ ] Read HW timestamp from PTP registers
- [ ] Emit events to ring buffer
- [ ] Add IOCTL for ring buffer mapping

### Phase 2: Correlation Key (Week 2)
- [ ] Parse PTP header (messageType, sequenceId, domain)
- [ ] Extract VLAN/PCP from NET_BUFFER_LIST metadata
- [ ] Add TX path monitoring
- [ ] Handle edge cases (VLAN, fragmented NBLs)

### Phase 3: User Tooling (Week 3)
- [ ] Python library for ring buffer access
- [ ] Npcap integration (pyshark/scapy)
- [ ] Correlation algorithm
- [ ] Visualization (latency, jitter plots)

## References

- **ADR Document**: `03-architecture/decisions/ADR-PTP-001-event-emission-vs-packet-io.md`
- **Related ADR**: ADR-PERF-004 (Kernel Ring Buffer)
- **Standards**: IEEE 1588-2019 (PTP), IEEE 802.1AS-2020 (gPTP)

## Validation Plan

### Unit Tests
- PTP EtherType detection (0x88F7, VLAN handling)
- Correlation key extraction (messageType, sequenceId, MAC)
- Ring buffer write (overflow, dropped events)

### Integration Tests
- End-to-end: Driver RX → Event → User read
- Correlation accuracy: 100% match rate
- Performance: <1µs latency, <1% CPU overhead

### Performance Benchmarks
- Baseline vs. monitoring enabled at 100/1000/10000 pps
- Ring buffer throughput before overflow

## Approval Criteria

- [ ] All alternatives documented
- [ ] Performance validated (<1µs latency)
- [ ] NDIS compliance verified
- [ ] Security reviewed (no cross-process leakage)
