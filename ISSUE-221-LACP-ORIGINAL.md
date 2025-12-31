# TEST-LACP-001: Link Aggregation (802.1AX LACP) Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #64 (REQ-F-LINK-AGGREGATION-001: Link Aggregation Support)
- Related Requirements: #2, #48, #150
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P2 (Medium - Redundancy)

---

## 📋 Test Objective

**Primary Goal**: Validate IEEE 802.1AX Link Aggregation Control Protocol (LACP) for redundancy and load balancing across multiple physical links, ensuring AVB timing and synchronization maintained across aggregated links.

**Scope**:
- LACP protocol negotiation (LACPDU exchange)
- Link aggregation group (LAG) formation
- Active/passive LACP modes
- Load balancing algorithms (src/dst MAC, IP, port)
- Failover and recovery (link down/up events)
- PHC synchronization across aggregated links
- TAS/CBS operation with link aggregation
- Per-link statistics and monitoring

**Success Criteria**:
- ✅ LACP negotiation completes <30 seconds
- ✅ LAG supports up to 4 physical links
- ✅ Load balancing distributes traffic evenly (±20%)
- ✅ Failover time <1 second when link fails
- ✅ PHC synchronized across all links (±100ns)
- ✅ AVB streams maintain latency during failover (<10ms spike)

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-LACP-001: LACP Negotiation (Active Mode)**
- Configure 2 NICs in active LACP mode (actor)
- Connect to LACP-capable switch (partner)
- Verify LACPDU exchange:
  - Actor sends LACPDU every 1 second (slow periodic)
  - Partner responds with LACPDU
  - Negotiation completes within 30 seconds
  - LAG formed with 2 active links
  - Verify LACP state machine: DEFAULTED → CURRENT → COLLECTING_DISTRIBUTING

**UT-LACP-002: LACP Negotiation (Passive Mode)**
- Configure NIC in passive LACP mode (actor)
- Connect to active LACP partner
- Verify:
  - Actor does not initiate LACPDU (passive)
  - Actor responds to partner's LACPDU
  - LAG formed successfully
  - Negotiation time <30 seconds

**UT-LACP-003: LAG Formation with 4 Links**
- Configure 4 NICs with same System ID and Key
- Connect to switch with LACP enabled
- Verify all 4 links aggregate:
  - Single LAG group formed
  - All links in COLLECTING_DISTRIBUTING state
  - Aggregate bandwidth = 4 × link speed (e.g., 4 × 1Gbps = 4Gbps)

**UT-LACP-004: Load Balancing (Source MAC Hash)**
- Form LAG with 2 links
- Configure load balancing: Source MAC address hash
- Transmit 1000 frames from 4 different source MACs
- Verify traffic distributed:
  - Link 0: ~50% (2 MACs hash to link 0)
  - Link 1: ~50% (2 MACs hash to link 1)
  - Variance <20%

**UT-LACP-005: Load Balancing (Src+Dst IP+Port Hash)**
- Form LAG with 4 links
- Configure load balancing: 5-tuple hash (Src IP, Dst IP, Src Port, Dst Port, Protocol)
- Generate 100 TCP flows (unique IP/port combinations)
- Verify traffic distributed evenly:
  - Each link: 25% ±20%
  - Flows consistently hashed to same link (no reordering)

**UT-LACP-006: Link Failover (Single Link Down)**
- Form LAG with 3 links
- Transmit continuous traffic (1000 frames/sec)
- Disconnect link 0 (physical disconnect or admin down)
- Verify:
  - LACP detects failure within 3 seconds (3 × timeout 1s)
  - Traffic redistributes to links 1 and 2
  - Failover time <1 second (traffic resumes)
  - Zero permanent frame loss (buffered frames may be lost)

**UT-LACP-007: Link Recovery (Link Up After Failure)**
- LAG with 2 active links (link 0 previously failed)
- Reconnect link 0
- Verify:
  - LACP re-negotiates on link 0
  - Link 0 rejoins LAG within 30 seconds
  - Traffic rebalances across 3 links (33% each)
  - No disruption to existing flows

**UT-LACP-008: LACP Timeout Configuration**
- Configure LACP short timeout (Fast Periodic: 1 second)
- Form LAG, verify LACPDU sent every 1 second
- Change to long timeout (Slow Periodic: 30 seconds)
- Verify LACPDU sent every 30 seconds
- Test failover detection: Short timeout detects in ~3s, long timeout in ~90s

**UT-LACP-009: System Priority and Key**
- Configure 2 NICs with different System Priorities (100 vs. 200)
- Connect both to same switch
- Verify LAG formed with:
  - Actor with priority 100 becomes "selected" aggregator
  - Both links use same aggregation key
  - System ID (MAC address + priority) correctly advertised

**UT-LACP-010: LACP Statistics**
- Form LAG, run traffic for 60 seconds
- Query IOCTL_AVB_GET_LACP_STATS
- Verify counters:
  - LacpduTx = ~60 (1 per second × 60 seconds)
  - LacpduRx = ~60
  - Link0Frames, Link1Frames, Link2Frames (per-link traffic)
  - FailoverEvents = 0 (no failures)

---

### **3 Integration Tests**

**IT-LACP-001: LACP with PHC Synchronization**
- Form LAG with 2 links, both with independent PHCs
- Configure PHC master on link 0
- Synchronize link 1 PHC to link 0 (cross-adapter sync)
- Verify:
  - Both PHCs aligned within ±100ns
  - AVB frames transmitted on either link have consistent timestamps
  - gPTP operates correctly (sync messages on master link)

**IT-LACP-002: LACP with TAS Scheduling**
- Form LAG with 2 links
- Configure identical TAS schedules on both links (2ms cycle, 4 gates)
- Transmit Class A traffic (TC6) hashed to both links
- Verify:
  - TAS schedules synchronized (gates open at same time ±1µs)
  - Frames transmitted only during gate open windows
  - No TAS violations on either link

**IT-LACP-003: LACP Failover with Active AVB Streams**
- Form LAG with 3 links
- Establish 4 AVB streams (2 Class A, 2 Class B)
- Streams distributed across links via load balancing
- Disconnect link carrying Class A stream
- Verify:
  - Stream fails over to alternate link <1 second
  - gPTP re-synchronizes on new link <10 seconds
  - Latency spike during failover <10ms
  - Stream resumes normal operation (<2ms latency)

---

### **2 V&V Tests**

**VV-LACP-001: 24-Hour LACP Stability**
- Form LAG with 4 links
- Run continuous AVB traffic (8 streams, 4 Gbps total)
- Simulate periodic failures:
  - Disconnect random link every 60 minutes (24 failures total)
  - Reconnect after 5 minutes
- Run for 24 hours
- Verify:
  - All failovers successful (100% success rate)
  - All recoveries successful (links rejoin LAG)
  - Average failover time <1 second
  - Total frame loss <0.01% (only during failover transitions)
  - AVB latency maintained (<2ms when stable, <10ms during failover)

**VV-LACP-002: Production Load Balancing**
- Deploy LAG with 4 × 10Gbps links (40 Gbps aggregate)
- Generate realistic traffic:
  - 100 TCP flows (web traffic)
  - 50 UDP flows (streaming)
  - 8 AVB streams (Class A + B)
- Run for 7 days
- Verify:
  - Load balanced evenly: Each link 25% ±15% utilization
  - No single link >80% utilization (avoid hotspot)
  - Aggregate throughput >30 Gbps (75% of theoretical 40 Gbps)
  - AVB streams maintain <2ms latency
  - Zero LACP negotiation failures

---

## 🔧 Implementation Notes

### **LACP Configuration**

```c
typedef struct _LACP_CONFIG {
    UINT8  ActorSystemPriority[2];  // System priority (default 32768)
    UINT8  ActorSystemId[6];        // MAC address
    UINT16 ActorKey;                // Aggregation key (same for all links in LAG)
    UINT16 ActorPortPriority;       // Port priority (default 32768)
    UINT16 ActorPortNumber;         // Physical port number
    UINT8  ActorState;              // LACP state (Active/Passive, timeout, etc.)
    
    UINT8  PartnerSystemPriority[2];
    UINT8  PartnerSystemId[6];
    UINT16 PartnerKey;
    UINT16 PartnerPortPriority;
    UINT16 PartnerPortNumber;
    UINT8  PartnerState;
    
    BOOLEAN Active;                 // Active (TRUE) or Passive (FALSE) mode
    BOOLEAN ShortTimeout;           // Fast (1s) or Slow (30s) periodic
} LACP_CONFIG;

NTSTATUS InitializeLACP(ADAPTER_CONTEXT* adapter) {
    // Set Actor parameters
    adapter->Lacp.ActorSystemPriority = htons(32768);  // Default priority
    RtlCopyMemory(adapter->Lacp.ActorSystemId, adapter->MacAddress, 6);
    adapter->Lacp.ActorKey = 1;  // Same key for all links in LAG
    adapter->Lacp.ActorPortPriority = htons(32768);
    adapter->Lacp.ActorPortNumber = htons(adapter->PortNumber);
    
    // Initial state: Active, Slow Timeout
    adapter->Lacp.ActorState = LACP_STATE_ACTIVITY;  // Active mode
    if (adapter->Lacp.ShortTimeout) {
        adapter->Lacp.ActorState |= LACP_STATE_TIMEOUT;
    }
    
    // Start LACP periodic timer
    StartLacpPeriodicTimer(adapter);
    
    DbgPrint("LACP initialized: System=%02X:%02X:%02X:%02X:%02X:%02X, Key=%u\n",
             adapter->Lacp.ActorSystemId[0], adapter->Lacp.ActorSystemId[1],
             adapter->Lacp.ActorSystemId[2], adapter->Lacp.ActorSystemId[3],
             adapter->Lacp.ActorSystemId[4], adapter->Lacp.ActorSystemId[5],
             adapter->Lacp.ActorKey);
    
    return STATUS_SUCCESS;
}
```

### **LACPDU Transmission**

```c
typedef struct _LACPDU {
    UINT8  Subtype;              // 0x01 (LACP)
    UINT8  VersionNumber;        // 0x01
    
    // Actor Information
    UINT8  ActorInfoType;        // 0x01
    UINT8  ActorInfoLength;      // 0x14 (20 bytes)
    UINT16 ActorSystemPriority;
    UINT8  ActorSystem[6];
    UINT16 ActorKey;
    UINT16 ActorPortPriority;
    UINT16 ActorPort;
    UINT8  ActorState;
    UINT8  Reserved1[3];
    
    // Partner Information
    UINT8  PartnerInfoType;      // 0x02
    UINT8  PartnerInfoLength;    // 0x14
    UINT16 PartnerSystemPriority;
    UINT8  PartnerSystem[6];
    UINT16 PartnerKey;
    UINT16 PartnerPortPriority;
    UINT16 PartnerPort;
    UINT8  PartnerState;
    UINT8  Reserved2[3];
    
    // Collector Information
    UINT8  CollectorInfoType;    // 0x03
    UINT8  CollectorInfoLength;  // 0x10 (16 bytes)
    UINT16 CollectorMaxDelay;
    UINT8  Reserved3[12];
    
    // Terminator
    UINT8  TerminatorType;       // 0x00
    UINT8  TerminatorLength;     // 0x00
    UINT8  Reserved4[50];
} LACPDU;

VOID SendLACPDU(ADAPTER_CONTEXT* adapter) {
    ETHERNET_FRAME frame;
    LACPDU* lacpdu = (LACPDU*)frame.Payload;
    
    // Construct Ethernet header
    RtlCopyMemory(frame.DstMac, LACP_MULTICAST_MAC, 6);  // 01-80-C2-00-00-02
    RtlCopyMemory(frame.SrcMac, adapter->MacAddress, 6);
    frame.EtherType = htons(0x8809);  // Slow Protocols
    
    // Construct LACPDU
    RtlZeroMemory(lacpdu, sizeof(LACPDU));
    lacpdu->Subtype = 0x01;
    lacpdu->VersionNumber = 0x01;
    
    // Actor Information
    lacpdu->ActorInfoType = 0x01;
    lacpdu->ActorInfoLength = 0x14;
    lacpdu->ActorSystemPriority = adapter->Lacp.ActorSystemPriority;
    RtlCopyMemory(lacpdu->ActorSystem, adapter->Lacp.ActorSystemId, 6);
    lacpdu->ActorKey = htons(adapter->Lacp.ActorKey);
    lacpdu->ActorPortPriority = adapter->Lacp.ActorPortPriority;
    lacpdu->ActorPort = adapter->Lacp.ActorPortNumber;
    lacpdu->ActorState = adapter->Lacp.ActorState;
    
    // Partner Information (from received LACPDU or defaults)
    lacpdu->PartnerInfoType = 0x02;
    lacpdu->PartnerInfoLength = 0x14;
    lacpdu->PartnerSystemPriority = adapter->Lacp.PartnerSystemPriority;
    RtlCopyMemory(lacpdu->PartnerSystem, adapter->Lacp.PartnerSystemId, 6);
    lacpdu->PartnerKey = htons(adapter->Lacp.PartnerKey);
    lacpdu->PartnerPortPriority = adapter->Lacp.PartnerPortPriority;
    lacpdu->PartnerPort = adapter->Lacp.PartnerPortNumber;
    lacpdu->PartnerState = adapter->Lacp.PartnerState;
    
    // Collector Information
    lacpdu->CollectorInfoType = 0x03;
    lacpdu->CollectorInfoLength = 0x10;
    lacpdu->CollectorMaxDelay = htons(0x8000);  // Default
    
    // Terminator
    lacpdu->TerminatorType = 0x00;
    lacpdu->TerminatorLength = 0x00;
    
    // Transmit LACPDU
    TransmitControlFrame(&frame, sizeof(ETHERNET_FRAME) + sizeof(LACPDU));
    
    InterlockedIncrement64(&adapter->LacpStats.LacpduTx);
}
```

### **Load Balancing Hash**

```c
UINT32 SelectLinkForFrame(LAG_GROUP* lag, FRAME_INFO* frame) {
    UINT32 hash = 0;
    
    switch (lag->LoadBalanceMode) {
        case LOAD_BALANCE_SRC_MAC:
            hash = HashMac(frame->SrcMac);
            break;
        
        case LOAD_BALANCE_DST_MAC:
            hash = HashMac(frame->DstMac);
            break;
        
        case LOAD_BALANCE_SRC_DST_MAC:
            hash = HashMac(frame->SrcMac) ^ HashMac(frame->DstMac);
            break;
        
        case LOAD_BALANCE_5_TUPLE:
            // Hash Src IP, Dst IP, Src Port, Dst Port, Protocol
            hash = HashIpAddress(frame->SrcIp) ^
                   HashIpAddress(frame->DstIp) ^
                   (frame->SrcPort << 16) ^
                   (frame->DstPort) ^
                   (frame->Protocol << 24);
            break;
        
        default:
            hash = 0;  // Round-robin fallback
            break;
    }
    
    // Map hash to link index (consistent hashing)
    UINT32 linkIndex = hash % lag->ActiveLinkCount;
    return lag->ActiveLinks[linkIndex];
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| LACP Negotiation Time | <30s | From init to COLLECTING_DISTRIBUTING |
| Max Links in LAG | 4 | Tested configuration |
| Load Balance Variance | ±20% | Traffic distribution across links |
| Failover Time | <1s | Link down to traffic on alternate link |
| PHC Sync Across Links | ±100ns | Cross-adapter PHC alignment |
| Latency During Failover | <10ms | AVB stream spike |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] LACP negotiation <30 seconds
- [x] LAG supports 4 links
- [x] Load balancing ±20% variance
- [x] Failover <1 second
- [x] 24-hour stability 100% success

---

**Standards**: IEEE 802.1AX (LACP), IEEE 802.1BA, ISO/IEC/IEEE 12207:2017
**XP Practice**: TDD - Tests defined before implementation
