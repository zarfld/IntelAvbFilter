# TEST-GPTP-001: IEEE 802.1AS gPTP Stack Integration Verification

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #10 (REQ-F-GPTP-001: gPTP Synchronization Support)
- Related Requirements: #2, #3, #149, #150
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Time Synchronization Foundation)

---

## 📋 Test Objective

**Primary Goal**: Validate IEEE 802.1AS gPTP (generalized Precision Time Protocol) stack integration for distributed time synchronization across AVB network.

**Scope**:
- gPTP slave mode (sync to grandmaster)
- gPTP master mode (provide time to slaves)
- Best Master Clock Algorithm (BMCA)
- Peer delay mechanism (P2P)
- Sync message handling (Sync, Follow_Up, Pdelay_Req/Resp)
- PHC adjustment based on gPTP offsets

**Success Criteria**:
- ✅ gPTP slave synchronizes to grandmaster within ±1µs
- ✅ gPTP master provides time to slaves with ±1µs accuracy
- ✅ BMCA correctly selects best master
- ✅ Peer delay measured accurately (±10ns)
- ✅ Sync messages transmitted at configured rate (8/sec, 16/sec)
- ✅ PHC converges to grandmaster time within 10 seconds

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-GPTP-001: gPTP Slave Mode Initialization**
- Configure adapter in gPTP slave mode
- Verify gPTP state machine enters LISTENING state
- Confirm Sync/Follow_Up message reception enabled
- Check Pdelay request transmission starts (1/sec)

**UT-GPTP-002: Sync Message Reception**
- Inject gPTP Sync message from grandmaster
- Verify driver captures Sync timestamp (RX timestamp)
- Confirm Follow_Up message reception within 1ms
- Extract correction field and grandmaster time

**UT-GPTP-003: Peer Delay Measurement**
- Transmit Pdelay_Req message to peer
- Receive Pdelay_Resp and Pdelay_Resp_Follow_Up
- Calculate peer delay: (t4-t1) - (t3-t2)
- Verify peer delay accuracy ±10ns (over 100 measurements)

**UT-GPTP-004: PHC Adjustment from gPTP Offset**
- Calculate offset: grandmaster time - local PHC
- Apply offset adjustment via IOCTL_AVB_OFFSET_ADJUST
- Verify PHC updated correctly (read-back matches expected)
- Confirm offset reduced to <100ns after adjustment

**UT-GPTP-005: Sync Interval Configuration**
- Configure Sync interval: 125ms (8/sec - default)
- Verify Sync messages transmitted at correct rate
- Test alternate intervals: 62.5ms (16/sec), 250ms (4/sec)
- Measure actual transmission rate (±1% tolerance)

**UT-GPTP-006: gPTP Master Mode**
- Configure adapter in gPTP master mode
- Verify Sync/Follow_Up message transmission starts
- Confirm transmission rate: 8/sec (125ms interval)
- Check Sync timestamp captured and sent in Follow_Up

**UT-GPTP-007: Best Master Clock Algorithm (BMCA)**
- Configure 3 adapters: GM1 (priority 100), GM2 (priority 200), Slave
- Run BMCA on slave
- Verify slave selects GM1 (lower priority number)
- Change GM1 priority to 300
- Confirm slave switches to GM2 (now best master)

**UT-GPTP-008: gPTP Message Parsing**
- Inject malformed gPTP messages (bad TLVs, incorrect lengths)
- Verify driver rejects invalid messages
- Confirm no crashes or state corruption
- Check error counters incremented

**UT-GPTP-009: Correction Field Accumulation**
- Inject Sync with correction field = 1000ns
- Inject Follow_Up with correction field = 2000ns
- Calculate total correction: 1000ns + 2000ns = 3000ns
- Verify offset calculation includes total correction

**UT-GPTP-010: gPTP Port State Transitions**
- Start in INITIALIZING state
- Verify transition to LISTENING (no Sync received)
- Inject Sync messages
- Confirm transition to SLAVE (sync acquired)
- Stop Sync messages
- Verify timeout and return to LISTENING

---

### **4 Integration Tests**

**IT-GPTP-001: Single Grandmaster, Multiple Slaves**
- Configure 1 grandmaster, 3 slaves
- Run gPTP for 60 seconds
- Measure slave offsets from grandmaster
- Verify all slaves within ±1µs of grandmaster (99th percentile)
- Confirm stable synchronization (no oscillations)

**IT-GPTP-002: Grandmaster Failover (BMCA)**
- Configure 2 grandmasters: GM1 (priority 100), GM2 (priority 200)
- Configure 2 slaves
- Verify slaves sync to GM1 (best master)
- Disconnect GM1 (simulate failure)
- Measure failover time to GM2
- Verify failover <10 seconds
- Confirm slaves maintain sync to GM2

**IT-GPTP-003: gPTP + TAS Coordination**
- Configure grandmaster with TAS (base time sync)
- Configure slave with TAS
- Sync slave to grandmaster via gPTP
- Program TAS on slave: base time = grandmaster base time
- Verify TAS gates open simultaneously on both adapters (±1µs)
- Run for 10 minutes
- Confirm synchronized TAS operation

**IT-GPTP-004: Multi-Hop gPTP (Transparent Clock)**
- Configure 3 adapters in chain: GM → Bridge → Slave
- Bridge acts as transparent clock (forwards + corrects Sync)
- Measure slave offset from GM
- Verify slave syncs within ±2µs (includes bridge delay)
- Confirm correction field updated by bridge

---

### **2 V&V Tests**

**VV-GPTP-001: 24-Hour gPTP Stability**
- Configure 1 grandmaster, 4 slaves
- Run gPTP for 24 hours
- Sample slave offsets every 1 second (86,400 samples)
- Verify:
  - Mean offset: ±500ns
  - Max offset: ±1µs
  - No sync loss events
  - No state machine failures

**VV-GPTP-002: Production AVB Network (End-to-End)**
- Configure realistic AVB network:
  - 1 grandmaster (industrial atomic clock reference)
  - 4 talkers (audio/video streams)
  - 8 listeners (endpoints)
- Run for 1 hour
- Verify:
  - All devices sync to grandmaster within ±1µs
  - Stream latency bounds met (Class A: ≤2ms)
  - Zero timestamp discontinuities
  - Peer delay stable (±10ns)

---

## 🔧 Implementation Notes

### **gPTP Message Structures**

```c
#pragma pack(push, 1)

// gPTP Sync Message
typedef struct _GPTP_SYNC_MSG {
    UINT8 MessageType;              // 0x0 = Sync
    UINT8 VersionPTP;               // 0x2 = IEEE 802.1AS
    UINT16 MessageLength;           // 44 bytes
    UINT8 DomainNumber;             // 0 = default domain
    UINT8 Reserved1;
    UINT16 Flags;                   // 0x0208 = two-step flag
    UINT64 CorrectionField;         // Nanoseconds × 2^16
    UINT32 Reserved2;
    UINT8 SourcePortIdentity[10];
    UINT16 SequenceId;
    UINT8 Control;                  // 0x0 = Sync
    INT8 LogMessageInterval;        // -3 = 125ms (8/sec)
    UINT64 OriginTimestamp;         // Not used in two-step
} GPTP_SYNC_MSG;

// gPTP Follow_Up Message
typedef struct _GPTP_FOLLOWUP_MSG {
    UINT8 MessageType;              // 0x8 = Follow_Up
    UINT8 VersionPTP;
    UINT16 MessageLength;
    UINT8 DomainNumber;
    UINT8 Reserved1;
    UINT16 Flags;
    UINT64 CorrectionField;
    UINT32 Reserved2;
    UINT8 SourcePortIdentity[10];
    UINT16 SequenceId;
    UINT8 Control;                  // 0x2 = Follow_Up
    INT8 LogMessageInterval;
    UINT64 PreciseOriginTimestamp;  // Actual Sync TX timestamp
} GPTP_FOLLOWUP_MSG;

#pragma pack(pop)
```

### **gPTP Offset Calculation**

```c
// Calculate offset between local PHC and grandmaster
INT64 CalculateGptpOffset(
    UINT64 syncTxTime,      // From Follow_Up
    UINT64 syncRxTime,      // Local RX timestamp
    UINT64 correctionField, // From Sync + Follow_Up
    UINT64 peerDelay        // Measured peer delay
) {
    // Correction field is in nanoseconds × 2^16
    INT64 correction_ns = (INT64)(correctionField >> 16);
    
    // Offset = (Sync TX time + correction - peer delay) - Sync RX time
    INT64 offset = (INT64)(syncTxTime + correction_ns - peerDelay - syncRxTime);
    
    return offset;
}

// Apply offset to PHC
VOID ApplyGptpOffset(INT64 offset) {
    // Use PI controller for smooth convergence
    static INT64 integral = 0;
    const INT64 Kp = 1;      // Proportional gain
    const INT64 Ki = 1000;   // Integral gain (slower)
    
    integral += offset;
    
    // Calculate frequency adjustment (ppb)
    INT32 freqAdj = (INT32)((offset * Kp + integral / Ki) / 1000);
    
    // Clamp to ±1000 ppm
    if (freqAdj > 1000000) freqAdj = 1000000;
    if (freqAdj < -1000000) freqAdj = -1000000;
    
    // Apply frequency adjustment
    AVB_FREQUENCY_ADJUST_REQUEST req;
    req.FrequencyAdjustmentPpb = freqAdj;
    DeviceIoControl(hDevice, IOCTL_AVB_FREQUENCY_ADJUST, &req, ...);
    
    // Apply offset adjustment if large (>1µs)
    if (abs(offset) > 1000) {
        AVB_OFFSET_ADJUST_REQUEST offsetReq;
        offsetReq.OffsetNanoseconds = offset;
        DeviceIoControl(hDevice, IOCTL_AVB_OFFSET_ADJUST, &offsetReq, ...);
    }
}
```

### **BMCA Implementation**

```c
typedef struct _GPTP_PORT_PRIORITY {
    UINT8 GrandmasterPriority1;
    UINT8 GrandmasterClockClass;
    UINT8 GrandmasterClockAccuracy;
    UINT16 GrandmasterOffsetScaledLogVariance;
    UINT8 GrandmasterPriority2;
    UINT8 GrandmasterIdentity[8];
} GPTP_PORT_PRIORITY;

// Compare two ports, return TRUE if A is better than B
BOOLEAN IsBetterMaster(GPTP_PORT_PRIORITY* A, GPTP_PORT_PRIORITY* B) {
    // Priority1 (lower is better)
    if (A->GrandmasterPriority1 < B->GrandmasterPriority1) return TRUE;
    if (A->GrandmasterPriority1 > B->GrandmasterPriority1) return FALSE;
    
    // Clock class (lower is better)
    if (A->GrandmasterClockClass < B->GrandmasterClockClass) return TRUE;
    if (A->GrandmasterClockClass > B->GrandmasterClockClass) return FALSE;
    
    // Clock accuracy (lower is better)
    if (A->GrandmasterClockAccuracy < B->GrandmasterClockAccuracy) return TRUE;
    if (A->GrandmasterClockAccuracy > B->GrandmasterClockAccuracy) return FALSE;
    
    // Priority2 (lower is better)
    if (A->GrandmasterPriority2 < B->GrandmasterPriority2) return TRUE;
    if (A->GrandmasterPriority2 > B->GrandmasterPriority2) return FALSE;
    
    // Grandmaster identity (lexicographically lower is better)
    return (memcmp(A->GrandmasterIdentity, B->GrandmasterIdentity, 8) < 0);
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Slave Sync Accuracy (mean) | ±500ns | Offset from grandmaster |
| Slave Sync Accuracy (max) | ±1µs | 99th percentile |
| Peer Delay Accuracy | ±10ns | Over 100 measurements |
| Sync Message Rate | 8/sec | 125ms interval (default) |
| BMCA Failover Time | <10s | Grandmaster switch |
| PHC Convergence Time | <10s | From cold start |

---

## 📈 Acceptance Criteria

- [ ] All 10 unit tests pass
- [ ] All 4 integration tests pass
- [ ] All 2 V&V tests pass
- [ ] Slave sync accuracy ±1µs (99th percentile)
- [ ] Peer delay accuracy ±10ns
- [ ] BMCA selects correct grandmaster
- [ ] 24-hour stability test passes
- [ ] Production AVB network synchronized

---

**Standards**: IEEE 802.1AS-2020, IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
