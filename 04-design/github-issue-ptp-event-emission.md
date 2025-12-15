# GitHub Issues for PTP Event Emission Architecture

This document contains the content for creating GitHub Issues related to the PTP Event Emission architecture decision (ADR-PTP-001).

**✅ ISSUES CREATED**:
- **Issue #147**: [ADR] Event Emission Architecture for PTP Timestamp Correlation
- **Issue #148**: [ARC-C] PTP Timestamp Event Monitor

---

## Issue 1: Architecture Decision Record (ADR)

**Create via**: `.github/ISSUE_TEMPLATE/04-architecture-decision.yml`

### Title
```
[ADR] Event Emission Architecture for PTP Timestamp Correlation (vs. Packet I/O)
```

### Labels
- `architecture-decision`
- `phase-03`
- `priority:high`
- `category:performance`
- `category:integration`

### Content

#### Traceability
```markdown
## Traceability
- Traces to:  # (TBD - PTP monitoring requirement issue)
- **Related to**: ADR-PERF-004 (Kernel Ring Buffer - provides infrastructure)
```

#### Status
- **Proposed**

#### Category
- **Performance (PERF)**

#### Context
```markdown
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
```

#### Decision
```markdown
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
3. **Minimal overhead**: 64-byte events vs. 1500-byte packet copies
4. **NDIS compliant**: No datapath modifications, pure metadata extraction
5. **Scalable**: Ring buffer handles burst traffic, no queue management
```

#### Consequences
```markdown
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
```

#### Alternatives Considered
```markdown
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
```

#### Quality Attributes
```markdown
| Quality Attribute | Impact | Notes |
|-------------------|--------|-------|
| Performance | ++ | <1µs event latency, ~500 cycles |
| Scalability | ++ | 1000+ events/sec, lock-free |
| Simplicity | ++ | Reuses Npcap, no packet queuing |
| Security | 0 | Per-process ring buffer |
| Usability | − | Requires Npcap + correlation tool |
```

#### Implementation Plan
```markdown
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
```

#### References
```markdown
- **ADR Document**: `03-architecture/decisions/ADR-PTP-001-event-emission-vs-packet-io.md`
- **Related ADR**: ADR-PERF-004 (Kernel Ring Buffer)
- **Standards**: IEEE 1588-2019 (PTP), IEEE 802.1AS-2020 (gPTP)
```

---

## Issue 2: Architecture Component (ARC-C)

**Create via**: `.github/ISSUE_TEMPLATE/05-architecture-component.yml`

### Title
```
[ARC-C] PTP Timestamp Event Monitor (Event Emission + Correlation)
```

### Labels
- `architecture-component`
- `phase-03`
- `priority:high`

### Content

#### Traceability
```markdown
## Traceability
- Traces to:  # (ADR-PTP-001: Event Emission Architecture)
- **Depends on**: ADR-PERF-004 (Kernel Ring Buffer infrastructure)
```

#### Component Type
- **Integration Adapter (INTG)** - Bridges driver timestamps with user-mode packet capture

#### Responsibility
```markdown
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
```

#### Interfaces (Provided)

**Kernel-Mode Driver Interface**:
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

**IOCTL Interface**:
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

**User-Mode Python API**:
```python
# ptp_monitor.py

class PtpMonitor:
    def __init__(self, interface_name: str):
        """Initialize monitor for network interface"""
    
    def enable(self):
        """Enable PTP event emission in driver"""
    
    def read_events(self) -> Iterator[PtpTimestampEvent]:
        """Yield timestamp events from ring buffer"""
    
    def correlate_with_npcap(self, npcap_capture) -> Iterator[CorrelatedEvent]:
        """Match driver events with Npcap packets"""
    
    def get_statistics(self) -> dict:
        """Get monitoring stats (events, drops, etc.)"""
```

#### Dependencies (Required)

**Internal Components**:
- **ADR-PERF-004**: Kernel Ring Buffer (shared memory, lock-free SPSC queue)
- **avb_hardware_access.c**: PTP register access (`AvbReadTimestamp`)
- **Filter datapath**: `FilterReceiveNetBufferLists`, `FilterSendNetBufferLists`

**External Dependencies**:
- **Npcap**: Packet capture library (user-mode)
- **Python packages**: pyshark or scapy (for Npcap integration)

**Windows APIs**:
- `ZwCreateSection` / `ZwMapViewOfSection` (shared memory)
- `MmBuildMdlForNonPagedPool` (MDL for user mapping)
- `KeQueryPerformanceCounter` (system timestamp correlation)

#### Data Ownership

**Event Data Structure** (`PTP_TIMESTAMP_EVENT`, 64 bytes):
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
    UINT32  reserved[6];        // Padding
} PTP_TIMESTAMP_EVENT;
```

**Ring Buffer Metadata** (`ring_buffer_t` from ADR-PERF-004):
```c
typedef struct {
    volatile ULONG head;        // Producer write index
    volatile ULONG tail;        // Consumer read index
    ULONG capacity;             // 4096 entries (default)
    ULONGLONG dropped_events;   // Overflow counter
    UCHAR data[0];              // PTP_TIMESTAMP_EVENT[capacity]
} ptp_ring_buffer_t;
```

#### Design Constraints

**Technology Stack**:
- **Driver**: C (Windows Kernel, NDIS 6.0)
- **User Library**: Python 3.8+ (cross-platform tool)
- **Packet Capture**: Npcap (required dependency)

**Performance Requirements**:
- **Datapath overhead**: <1µs added latency per PTP packet
- **CPU overhead**: <1% at 1000 PTP packets/sec
- **Event throughput**: Support 10,000 events/sec without drops

**Memory Constraints**:
- **Ring buffer size**: 4096 entries × 64 bytes = 256 KB (non-paged pool)
- **Max instances**: 1 ring buffer per network adapter

**Security Constraints**:
- **Per-process isolation**: Shared memory accessible only to creating process
- **No privilege escalation**: User-mode cannot write to ring buffer
- **Validation**: Bounds checking on all PTP header parsing

#### Non-Functional Requirements

**Performance**:
- Event emission latency: <500 CPU cycles
- Lock-free ring buffer writes (no mutex)
- Zero-copy event delivery

**Reliability**:
- Graceful overflow handling (drop events, increment counter)
- No packet drops from driver monitoring
- Ring buffer cleanup on process exit

**Usability**:
- Python API for easy integration
- Reference correlation tool provided
- Clear documentation with examples

**Testability**:
- Unit tests for PTP header parsing
- Integration tests for end-to-end correlation
- Performance benchmarks (latency, throughput)

#### Acceptance Criteria

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

---

## Instructions for Creating Issues

### Step 1: Create ADR Issue

1. Go to: https://github.com/zarfld/IntelAvbFilter/issues/new/choose
2. Select: **"Architecture Decision Record (ADR)"**
3. Copy content from **Issue 1** above
4. Submit issue
5. **Note the issue number** (e.g., #123)

### Step 2: Update ADR Document

After creating Issue #123:

```bash
# Update ADR-PTP-001 with issue number
# Edit line 5: **GitHub Issue**: TBD → **GitHub Issue**: #123
```

### Step 3: Create ARC-C Issue

1. Go to: https://github.com/zarfld/IntelAvbFilter/issues/new/choose
2. Select: **"Architecture Component (ARC-C)"**
3. Copy content from **Issue 2** above
4. **Update traceability line** to reference ADR issue: `Traces to: #123`
5. Submit issue
6. **Note the issue number** (e.g., #124)

### Step 4: Link Issues

Add cross-references:
- In ADR issue (#123): Add comment with link to ARC-C issue (#124)
- In ARC-C issue (#124): Already linked via "Traces to: #123"

### Step 5: Update Labels

Ensure both issues have appropriate labels:
- **ADR Issue**: `architecture-decision`, `phase-03`, `priority:high`, `category:performance`
- **ARC-C Issue**: `architecture-component`, `phase-03`, `priority:high`

---

## Next Steps After Issue Creation

1. **Review ADR** with architecture team
2. **Get approval** on event emission approach (vs. packet I/O)
3. **Create implementation issues** for Phase 1-3 tasks
4. **Update project board** with new issues
5. **Begin Phase 1 implementation** (event emission infrastructure)

---

**Document Version**: 1.0  
**Date**: 2025-12-12  
**Purpose**: GitHub Issue templates for PTP Event Emission architecture
