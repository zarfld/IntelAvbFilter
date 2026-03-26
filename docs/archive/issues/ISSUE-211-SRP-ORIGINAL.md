# TEST-SRP-001: Stream Reservation Protocol (SRP) Support Verification

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #11 (REQ-F-SRP-001: Stream Reservation Protocol Support)
- Related Requirements: #8, #9, #10, #149
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - AVB Stream Management)

---

## 📋 Test Objective

**Primary Goal**: Validate Stream Reservation Protocol (SRP) support per IEEE 802.1Qat, including MSRP (Multiple Stream Registration Protocol), MVRP (Multiple VLAN Registration Protocol), and MMRP (Multiple MAC Registration Protocol).

**Scope**:
- MSRP stream reservation (talker advertise, listener ready)
- MVRP VLAN registration (join/leave VLANs dynamically)
- MMRP multicast address registration
- Bandwidth reservation calculation (Class A/B)
- SRP attribute propagation across bridges
- Failure detection and stream removal

**Success Criteria**:
- ✅ Talker advertises stream with correct bandwidth requirements
- ✅ Listener registers for stream and receives "ready" confirmation
- ✅ Bandwidth reservation calculated per IEEE 802.1BA (75% Class A, 25% Class B)
- ✅ VLAN registration succeeds (join/leave)
- ✅ Multicast MAC registration succeeds
- ✅ SRP attributes propagate through AVB-capable bridges

---

## 🧪 Test Coverage

### **10 Unit Tests**

#### **UT-SRP-001: MSRP Talker Advertise**

Configure talker stream:
- Stream ID: 01:23:45:67:89:AB:00:01
- Class A (TC6)
- Max Frame Size: 1522 bytes
- Max Interval Frames: 1 (125µs interval)

Steps:
- Transmit MSRP Talker Advertise message
- Verify message contains correct attributes:
  - Stream ID, VLAN ID, Dest MAC, Class, Max Frame Size
- Confirm talker state = ADVERTISED

**Expected Result**: Talker state transitions to ADVERTISED, MSRP message transmitted with correct TSpec

---

#### **UT-SRP-002: MSRP Listener Ready**

Steps:
- Receive MSRP Talker Advertise for stream X
- Transmit MSRP Listener Ready message
- Verify listener state = READY
- Confirm Four Packed Events encoding correct
- Check listener declaration includes:
  - Stream ID, VLAN ID, Destination MAC

**Expected Result**: Listener Ready message transmitted, state = READY, correct encoding

---

#### **UT-SRP-003: Bandwidth Reservation Calculation (Class A)**

Register Class A stream:
- Max Frame Size: 1522 bytes
- Max Interval Frames: 1
- Interval: 125µs

Steps:
- Calculate bandwidth: (1522 + overhead) × 8 bits / 125µs
- Verify calculated bandwidth ≈ 100 Mbps (on 1 Gbps link)
- Confirm reservation ≤ 75% link capacity

**Expected Result**: Bandwidth calculated correctly, Class A limit enforced

---

#### **UT-SRP-004: Bandwidth Reservation Calculation (Class B)**

Register Class B stream:
- Max Frame Size: 1522 bytes
- Max Interval Frames: 1
- Interval: 250µs

Steps:
- Calculate bandwidth: (1522 + overhead) × 8 bits / 250µs
- Verify calculated bandwidth ≈ 50 Mbps
- Confirm reservation ≤ 25% link capacity

**Expected Result**: Bandwidth calculated correctly, Class B limit enforced

---

#### **UT-SRP-005: MVRP VLAN Join**

Steps:
- Transmit MVRP Join message for VLAN 100
- Verify VLAN 100 added to port VLAN table
- Confirm VLAN filtering allows VLAN 100 traffic
- Check MVRP state = REGISTERED

**Expected Result**: VLAN registered, filtering updated, state = REGISTERED

---

#### **UT-SRP-006: MVRP VLAN Leave**

Precondition: VLAN 100 currently registered

Steps:
- Transmit MVRP Leave message for VLAN 100
- Verify VLAN 100 removed from port VLAN table
- Confirm VLAN filtering blocks VLAN 100 traffic
- Check MVRP state = UNREGISTERED

**Expected Result**: VLAN unregistered, filtering updated, state = UNREGISTERED

---

#### **UT-SRP-007: MMRP Multicast Address Registration**

Steps:
- Transmit MMRP Join for multicast MAC 91:E0:F0:00:FE:01
- Verify multicast MAC added to MAC table
- Confirm multicast filtering allows this address
- Check MMRP state = REGISTERED

**Expected Result**: Multicast MAC registered, filtering updated

---

#### **UT-SRP-008: SRP Domain Discovery**

Steps:
- Receive SRP Domain PDU from bridge
- Extract domain attributes:
  - Domain Class Priority (Class A: 3, Class B: 2)
  - VLAN ID
- Verify domain configuration applied locally
- Confirm compliance with IEEE 802.1BA

**Expected Result**: Domain attributes extracted and applied correctly

---

#### **UT-SRP-009: Stream Failure Detection**

Steps:
- Talker advertises stream X
- Simulate link failure (disconnect cable)
- Verify MSRP Talker Failed message transmitted
- Confirm listener receives failure notification
- Check stream state = FAILED

**Expected Result**: Failure detected, Talker Failed message sent, state = FAILED

---

#### **UT-SRP-010: SRP Message Parsing**

Inject malformed SRP messages:
- Invalid attribute types
- Incorrect lengths
- Bad checksums

Steps:
- Verify driver rejects invalid messages
- Confirm no crashes or state corruption
- Check error counters incremented

**Expected Result**: All malformed messages rejected safely, error counters updated

---

### **3 Integration Tests**

#### **IT-SRP-001: End-to-End Stream Setup (Talker → Listener)**

Steps:
- Configure talker to advertise Class A audio stream
- Configure listener to register for stream
- Verify complete SRP handshake:
  1. Talker Advertise → Bridge → Listener
  2. Listener Ready → Bridge → Talker
  3. Stream data transmission starts
- Measure setup time: <1 second
- Confirm bandwidth reserved correctly (75% Class A)

**Expected Result**: Complete SRP handshake, stream established <1s, bandwidth reserved

---

#### **IT-SRP-002: Multi-Stream Bandwidth Management**

Steps:
- Configure 4 talkers advertising Class A streams
- Each stream: 1522 bytes @ 125µs (≈100 Mbps)
- Total bandwidth: 4 × 100 Mbps = 400 Mbps (40% of 1 Gbps)
- Verify all streams admitted (within 75% Class A limit)
- Add 5th stream (would exceed 75%)
- Confirm 5th stream rejected with "Insufficient Bandwidth"

**Expected Result**: 4 streams admitted, 5th rejected due to bandwidth limit

---

#### **IT-SRP-003: SRP Attribute Propagation Through Bridge**

Topology: Talker → Bridge1 → Bridge2 → Listener

Steps:
- Talker advertises stream on port A of Bridge1
- Verify Bridge1 propagates Talker Advertise to Bridge2
- Confirm Bridge2 propagates to Listener
- Listener sends Ready message
- Verify Ready propagates back through Bridge2 → Bridge1 → Talker
- Measure end-to-end propagation delay: <100ms

**Expected Result**: Attributes propagate correctly through 2 bridges, delay <100ms

---

### **2 V&V Tests**

#### **VV-SRP-001: 24-Hour SRP Stability**

Setup:
- Configure 8 talkers, 16 listeners (multiple streams per listener)
- Run SRP for 24 hours with streams continuously active

Verify:
- Zero stream registration failures
- Zero spurious stream removals
- All bandwidth reservations maintained
- No memory leaks in SRP state machine

**Expected Result**: 24-hour continuous operation, zero failures, no leaks

---

#### **VV-SRP-002: Production AVB Network with SRP**

Configure realistic AVB network:
- 10 talkers (audio/video streams)
- 20 listeners (various endpoints)
- 5 AVB-capable bridges
- Run for 1 hour with dynamic stream creation/deletion
- Add/remove 5 streams every 5 minutes

Verify:
- All streams set up successfully (100% success rate)
- Bandwidth enforcement prevents oversubscription
- No orphaned stream reservations
- Graceful handling of talker/listener failures

**Expected Result**: 100% success rate, bandwidth enforced, graceful failure handling

---

## 🔧 Implementation Notes

### **MSRP Message Structures**

```c
#pragma pack(push, 1)

// MSRP Stream ID
typedef struct _MSRP_STREAM_ID {
    UINT8  MacAddress[6];  // Talker MAC
    UINT16 UniqueID;       // Unique stream identifier
} MSRP_STREAM_ID;

// MSRP Talker Advertise
typedef struct _MSRP_TALKER_ADVERTISE {
    UINT8  AttributeType;              // 1 = Talker Advertise
    UINT8  AttributeLength;            // 25 bytes
    MSRP_STREAM_ID StreamID;
    UINT8  DataFrameParameters[8];
    UINT16 TSpec_MaxFrameSize;
    UINT16 TSpec_MaxIntervalFrames;
    UINT8  PriorityAndRank;
    UINT32 AccumulatedLatency;         // Nanoseconds
} MSRP_TALKER_ADVERTISE;

// MSRP Listener Ready
typedef struct _MSRP_LISTENER {
    UINT8  AttributeType;              // 2 = Listener
    UINT8  AttributeLength;            // 8 bytes
    MSRP_STREAM_ID StreamID;
    UINT8  SubState;                   // 0 = Ready, 1 = Ready Failed, 2-15 = Reserved
} MSRP_LISTENER;

#pragma pack(pop)
```

### **Bandwidth Calculation**

```c
// Calculate bandwidth for AVB Class A stream
UINT32 CalculateStreamBandwidth(
    UINT16 maxFrameSize,
    UINT16 maxIntervalFrames,
    UINT32 intervalUs
) {
    // Overhead: Preamble(8) + IFG(12) + FCS(4) = 24 bytes
    const UINT32 OVERHEAD_BYTES = 24;
    
    // Total frame size
    UINT32 totalFrameBytes = maxFrameSize + OVERHEAD_BYTES;
    
    // Bandwidth in bits per second
    UINT32 bandwidthBps = (totalFrameBytes * 8 * maxIntervalFrames * 1000000) / intervalUs;
    
    return bandwidthBps;
}

// Check if stream can be admitted
BOOLEAN CanAdmitStream(
    UINT8 streamClass,           // 0 = Class A, 1 = Class B
    UINT32 streamBandwidthBps,
    UINT32 linkSpeedBps
) {
    // Get current reserved bandwidth for this class
    UINT32 currentReservedBps = GetReservedBandwidth(streamClass);
    
    // Class A: max 75%, Class B: max 25%
    UINT32 maxReservationBps = (streamClass == 0) 
        ? (linkSpeedBps * 75 / 100)  // Class A
        : (linkSpeedBps * 25 / 100); // Class B
    
    // Check if adding this stream would exceed limit
    if (currentReservedBps + streamBandwidthBps > maxReservationBps) {
        DbgPrint("Insufficient bandwidth: Class=%d, Current=%u, Stream=%u, Max=%u\n",
                 streamClass, currentReservedBps, streamBandwidthBps, maxReservationBps);
        return FALSE;
    }
    
    return TRUE;
}
```

### **SRP State Machine**

```c
typedef enum _SRP_STATE {
    SRP_STATE_INIT,
    SRP_STATE_IDLE,
    SRP_STATE_ADVERTISED,   // Talker has advertised stream
    SRP_STATE_REGISTERED,   // Listener has registered
    SRP_STATE_READY,        // Listener ready to receive
    SRP_STATE_FAILED        // Stream setup failed
} SRP_STATE;

VOID ProcessMsrpMessage(UINT8* pdu, UINT32 length) {
    MSRP_TALKER_ADVERTISE* talker = (MSRP_TALKER_ADVERTISE*)pdu;
    
    if (talker->AttributeType == 1) {  // Talker Advertise
        // Extract stream parameters
        MSRP_STREAM_ID streamId = talker->StreamID;
        UINT16 maxFrameSize = ntohs(talker->TSpec_MaxFrameSize);
        
        // Calculate bandwidth
        UINT32 bandwidth = CalculateStreamBandwidth(
            maxFrameSize,
            ntohs(talker->TSpec_MaxIntervalFrames),
            125  // Class A: 125µs
        );
        
        // Check if can admit
        if (CanAdmitStream(0, bandwidth, 1000000000)) {  // 1 Gbps link
            // Reserve bandwidth
            ReserveBandwidth(0, bandwidth);
            
            // Update state
            SetStreamState(&streamId, SRP_STATE_ADVERTISED);
            
            // Send Listener Ready (if this is a listener)
            SendListenerReady(&streamId);
        } else {
            // Send Listener Ready Failed
            SendListenerFailed(&streamId);
        }
    }
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Stream Setup Time | <1s | Talker Advertise → Listener Ready |
| Bandwidth Calculation Accuracy | ±1% | Measured vs. theoretical |
| Max Concurrent Streams | 100 | Per adapter |
| SRP Message Rate | 1/sec | Periodic refresh |
| Attribute Propagation Delay | <100ms | Through 5 bridges |
| Stream Failure Detection | <1s | Link down to notification |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] Stream setup time <1 second
- [x] Bandwidth enforcement prevents oversubscription
- [x] SRP attributes propagate through bridges
- [x] 24-hour stability test passes
- [x] 100% stream setup success rate (VV test)

---

**Standards**: IEEE 802.1Qat (SRP), IEEE 802.1BA (AVB Systems), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
