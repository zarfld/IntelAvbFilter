# TEST-QUEUE-PRIORITY-001: Queue Priority Mapping Verification

**Test ID**: TEST-QUEUE-PRIORITY-001  
**Feature**: Queue Management, Traffic Classification, Quality of Service (QoS)  
**Test Type**: Unit (10), Integration (3), V&V (2)  
**Test Phase**: Phase 07 - Verification & Validation  
**Priority**: P1 (High - Traffic Classification)  
**Estimated Effort**: 20 hours

## 🔗 Traceability

- **Traces to**: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- **Verifies**: #55 (REQ-F-QOS-001: Quality of Service Support)
- **Related Requirements**: #2, #48, #49
- **Phase**: Phase 07 - Verification & Validation
- **Standards**: IEEE 802.1Q (VLAN Priority), IEEE 802.1BA (AVB Systems), ISO/IEC/IEEE 12207:2017

---

## 📋 Test Objective

**Primary Goal**: Validate accurate mapping of IEEE 802.1Q Priority Code Points (PCP) to hardware traffic classes (TCs) and transmit queues, ensuring proper QoS differentiation for AVB traffic.

**Scope**:
- PCP to TC mapping (8 priorities → 8 TCs)
- TC to hardware queue assignment
- Strict priority scheduling verification
- Credit-Based Shaper (CBS) queue assignment (Class A → TC6, Class B → TC5)
- Traffic segregation (AVB vs. Best Effort)
- Queue configuration and statistics

**Success Criteria**:
- ✅ All 8 PCPs correctly mapped to TCs
- ✅ Class A traffic routed to TC6 (highest priority)
- ✅ Class B traffic routed to TC5
- ✅ Best Effort traffic isolated to TC0-TC4
- ✅ Strict priority enforced (TC6 > TC5 > ... > TC0)
- ✅ Queue statistics accurate (per-TC counters)

---

## 🧪 Test Coverage

### **10 Unit Tests**

#### **UT-QUEUE-001: Default PCP to TC Mapping**
**Objective**: Verify default IEEE 802.1Q PCP to TC mapping

**Preconditions**:
- Driver loaded with default configuration
- No custom PCP mapping configured

**Procedure**:
1. Query default mapping via `IOCTL_AVB_GET_QUEUE_MAPPING`
2. Verify IEEE 802.1Q default mapping:
   - PCP 0 (BE) → TC0, PCP 1 (BK) → TC1
   - PCP 2 (EE) → TC2, PCP 3 (CA) → TC3
   - PCP 4 (VI) → TC4, PCP 5 (VO) → TC5
   - PCP 6 (IC) → TC6, PCP 7 (NC) → TC7
3. Confirm mapping matches IEEE 802.1Q standard

**Acceptance Criteria**:
- ✅ All 8 PCPs map correctly to respective TCs
- ✅ Mapping matches IEEE 802.1Q Table 6-3
- ✅ IOCTL returns STATUS_SUCCESS

**Code Reference**:
```c
static UINT8 g_DefaultPcpToTc[8] = {
    0, // PCP 0 (BE) → TC0
    1, // PCP 1 (BK) → TC1
    2, // PCP 2 (EE) → TC2
    3, // PCP 3 (CA) → TC3
    4, // PCP 4 (VI) → TC4
    5, // PCP 5 (VO) → TC5
    6, // PCP 6 (IC) → TC6 (Class A)
    7  // PCP 7 (NC) → TC7
};
```

---

#### **UT-QUEUE-002: Custom PCP Mapping Configuration**
**Objective**: Verify ability to configure custom PCP to TC mapping

**Preconditions**:
- Driver loaded
- Administrator privileges

**Procedure**:
1. Configure custom mapping: PCP 3 → TC6 (remap CA to highest priority)
2. Send `IOCTL_AVB_SET_QUEUE_MAPPING` with new mapping
3. Verify register `TQAVCC[3]` updated to TC6
4. Transmit frame with PCP=3
5. Confirm frame egresses on TC6 queue (check queue counters)

**Acceptance Criteria**:
- ✅ Custom mapping accepted by driver
- ✅ TQAVCC register correctly updated
- ✅ Subsequent frames use new mapping
- ✅ No frame loss during reconfiguration

---

#### **UT-QUEUE-003: Class A Traffic to TC6**
**Objective**: Verify Class A AVB traffic routed to TC6 (highest priority)

**Preconditions**:
- CBS configured for Class A (StreamID 0x1234, PCP=6)
- AVB stream registered

**Procedure**:
1. Configure CBS for Class A stream
2. Transmit AVB frame with VLAN PCP=6
3. Verify frame routed to TC6 (check queue counters)
4. Confirm CBS parameters applied (idleSlope, sendSlope)

**Acceptance Criteria**:
- ✅ All Class A frames use TC6
- ✅ CBS shaping active (credit calculations correct)
- ✅ Zero frames on other TCs

**Code Reference**:
```c
// Class A: PCP 6 → TC6, CBS with 75% bandwidth
ConfigureCBS(TC6, IDLE_SLOPE_75_PERCENT, SEND_SLOPE_NEG_25_PERCENT);
```

---

#### **UT-QUEUE-004: Class B Traffic to TC5**
**Objective**: Verify Class B AVB traffic routed to TC5

**Preconditions**:
- CBS configured for Class B (StreamID 0x5678, PCP=5)
- AVB stream registered

**Procedure**:
1. Configure CBS for Class B stream
2. Transmit AVB frame with VLAN PCP=5
3. Verify frame routed to TC5
4. Confirm CBS shaping active (credit calculations)

**Acceptance Criteria**:
- ✅ All Class B frames use TC5
- ✅ CBS parameters correctly applied
- ✅ Proper bandwidth allocation (25%)

---

#### **UT-QUEUE-005: Best Effort Isolation**
**Objective**: Verify Best Effort traffic does not interfere with AVB traffic

**Preconditions**:
- Mixed traffic scenario configured
- AVB stream active on TC6

**Procedure**:
1. Transmit 100 frames with PCP=0 (Best Effort)
2. Transmit 10 AVB frames with PCP=6 (Class A)
3. Verify AVB frames egress first (strict priority)
4. Confirm BE frames do not interfere with AVB latency

**Acceptance Criteria**:
- ✅ AVB frames egress before BE frames
- ✅ AVB latency <2ms (unaffected by BE traffic)
- ✅ BE frames queued in TC0

---

#### **UT-QUEUE-006: Strict Priority Scheduling**
**Objective**: Verify strict priority scheduling enforcement

**Preconditions**:
- All 8 TCs configured with queued frames
- Strict priority mode enabled (TQAVCTRL)

**Procedure**:
1. Configure all 8 TCs with frames queued
2. Enable strict priority mode
3. Transmit frames on all TCs simultaneously
4. Verify egress order: TC7 → TC6 → TC5 → ... → TC0
5. Confirm no interleaving (TC7 drains completely before TC6)

**Acceptance Criteria**:
- ✅ Egress order strictly enforced
- ✅ Higher priority TC fully drained before lower TC
- ✅ Zero frame reordering within TC

**Code Reference**:
```c
NTSTATUS EnableStrictPriority() {
    UINT32 tqavctrl = READ_REG32(I225_TQAVCTRL);
    tqavctrl |= TQAVCTRL_TRANSMIT_MODE_SP;
    WRITE_REG32(I225_TQAVCTRL, tqavctrl);
    return STATUS_SUCCESS;
}
```

---

#### **UT-QUEUE-007: Queue Statistics Accuracy**
**Objective**: Verify per-TC queue statistics accuracy

**Preconditions**:
- Queue statistics cleared
- Multiple TCs active

**Procedure**:
1. Clear all queue counters via `IOCTL_AVB_RESET_STATS`
2. Transmit 100 frames to TC6, 50 to TC3, 200 to TC0
3. Query `IOCTL_AVB_GET_QUEUE_STATS`
4. Verify counters: TC6=100, TC3=50, TC0=200
5. Confirm byte counters accurate (frames × frame_size)

**Acceptance Criteria**:
- ✅ Frame counters accurate (±1 frame tolerance)
- ✅ Byte counters match transmitted bytes
- ✅ No counter overflow or underflow

**Code Reference**:
```c
typedef struct _QUEUE_STATS {
    UINT64 TxFrames[8]; // Per-TC frame counters
    UINT64 TxBytes[8];  // Per-TC byte counters
    UINT64 TxDrops[8];  // Per-TC drop counters
} QUEUE_STATS;

NTSTATUS GetQueueStats(QUEUE_STATS* stats) {
    for (UINT32 tc = 0; tc < 8; tc++) {
        stats->TxFrames[tc] = READ_REG64(I225_TC_TPCT(tc));
        stats->TxBytes[tc] = READ_REG64(I225_TC_TBCT(tc));
        stats->TxDrops[tc] = READ_REG64(I225_TC_TDCT(tc));
    }
    return STATUS_SUCCESS;
}
```

---

#### **UT-QUEUE-008: Invalid TC Assignment**
**Objective**: Verify driver rejects invalid TC assignments

**Preconditions**:
- Driver loaded
- Custom mapping interface available

**Procedure**:
1. Attempt to configure PCP 3 → TC9 (invalid, only TC0-TC7 exist)
2. Send `IOCTL_AVB_SET_QUEUE_MAPPING` with invalid TC
3. Verify driver rejects with STATUS_INVALID_PARAMETER
4. Confirm mapping unchanged (still default or previous valid mapping)

**Acceptance Criteria**:
- ✅ Invalid TC rejected immediately
- ✅ Error code STATUS_INVALID_PARAMETER returned
- ✅ No hardware register modification
- ✅ Previous mapping preserved

**Code Reference**:
```c
NTSTATUS SetQueueMapping(UINT8 pcp, UINT8 tc) {
    if (pcp > 7 || tc > 7) {
        return STATUS_INVALID_PARAMETER;
    }
    // ... configure mapping
}
```

---

#### **UT-QUEUE-009: Dynamic Remapping Under Traffic**
**Objective**: Verify queue remapping works during active traffic

**Preconditions**:
- Continuous traffic running on TC3
- Driver supports dynamic reconfiguration

**Procedure**:
1. Start continuous traffic on TC3 (100 frames/sec)
2. Reconfigure PCP 3 → TC6 (while traffic active)
3. Verify new frames use TC6 after reconfiguration
4. Confirm no frame loss during transition
5. Measure reconfiguration latency

**Acceptance Criteria**:
- ✅ Reconfiguration completes in <1ms
- ✅ All frames after reconfiguration use TC6
- ✅ Zero frame loss during transition
- ✅ No traffic interruption

---

#### **UT-QUEUE-010: Multi-Stream Queue Assignment**
**Objective**: Verify correct queue assignment for multiple simultaneous streams

**Preconditions**:
- Multiple AVB streams configured
- CBS enabled for Class A and Class B

**Procedure**:
1. Configure 4 Class A streams (StreamID 0x1000-0x1003, all PCP=6)
2. Configure 2 Class B streams (StreamID 0x2000-0x2001, all PCP=5)
3. Transmit all streams simultaneously
4. Verify all Class A → TC6, all Class B → TC5
5. Confirm stream isolation (no cross-talk)

**Acceptance Criteria**:
- ✅ All Class A streams use TC6
- ✅ All Class B streams use TC5
- ✅ Per-stream CBS credits independent
- ✅ No interference between streams

---

### **3 Integration Tests**

#### **IT-QUEUE-001: Queue Priority with TAS**
**Objective**: Verify queue priority coordination with Time-Aware Shaper (TAS)

**Preconditions**:
- TAS configured with gate control list
- Multiple TCs with queued traffic

**Procedure**:
1. Configure TAS schedule:
   - TC6 gate open: 0-500µs
   - TC5 gate open: 500-1000µs
   - TC0-TC4 gate open: 1000-2000µs (2ms cycle)
2. Transmit frames on all TCs
3. Verify frames egress only during assigned gate windows
4. Confirm strict priority + TAS coordination (TC6 always during its window)

**Acceptance Criteria**:
- ✅ All frames egress within gate windows
- ✅ Strict priority enforced within gate windows
- ✅ Zero gate violations
- ✅ Schedule repeats correctly (2ms cycle)

---

#### **IT-QUEUE-002: CBS + Queue Priority**
**Objective**: Verify Credit-Based Shaper interaction with queue priority

**Preconditions**:
- CBS configured for TC6 and TC5
- Bandwidth allocation: TC6=75%, TC5=25%

**Procedure**:
1. Configure CBS for TC6 (idleSlope=75% link bandwidth)
2. Configure CBS for TC5 (idleSlope=25% link bandwidth)
3. Transmit traffic exceeding bandwidth on both TCs
4. Verify TC6 receives 75% bandwidth, TC5 receives 25%
5. Confirm strict priority: TC6 drains first if both have credits

**Acceptance Criteria**:
- ✅ Bandwidth allocation accurate (±2%)
- ✅ Strict priority enforced when both TCs have credit
- ✅ CBS credit calculations correct
- ✅ No bandwidth starvation for TC5

---

#### **IT-QUEUE-003: Queue Congestion and Flow Control**
**Objective**: Verify queue congestion handling without head-of-line blocking

**Preconditions**:
- TC0 queue capacity known (256 descriptors)
- High-priority TC6 traffic ready

**Procedure**:
1. Fill TC0 queue to capacity (256 descriptors)
2. Continue transmitting on TC0 (trigger backpressure)
3. Simultaneously transmit high-priority traffic on TC6
4. Verify TC6 traffic unaffected (no head-of-line blocking)
5. Confirm TC0 applies backpressure (frames queued or dropped)

**Acceptance Criteria**:
- ✅ TC6 traffic unaffected by TC0 congestion
- ✅ TC6 latency remains <2ms
- ✅ TC0 backpressure active (no overflow)
- ✅ No descriptor exhaustion for TC6

**Code Reference**:
```c
BOOLEAN IsQueueCongested(UINT8 tc) {
    UINT32 available = g_TxQueues[tc].AvailableDescriptors;
    UINT32 total = g_TxQueues[tc].TotalDescriptors;
    return (available < (total / 10)); // <10% available
}
```

---

### **2 V&V Tests**

#### **VV-QUEUE-001: Production Mixed Traffic**
**Objective**: Validate queue priority in production-like mixed traffic scenario

**Preconditions**:
- Production network topology configured
- Multiple AVB streams registered
- Best Effort traffic generators ready

**Procedure**:
1. Simulate production network:
   - 2 Class A streams (PCP=6, 8000 frames/sec each)
   - 2 Class B streams (PCP=5, 4000 frames/sec each)
   - Best Effort traffic (PCP=0, 10,000 frames/sec)
2. Run for 60 minutes
3. Measure and verify:
   - Class A latency <2ms (99.9th percentile)
   - Class B latency <50ms (99.9th percentile)
   - Zero AVB frame drops
   - BE traffic fills remaining bandwidth (no starvation)

**Acceptance Criteria**:
- ✅ Class A latency <2ms (99.9th percentile)
- ✅ Class B latency <50ms (99.9th percentile)
- ✅ Zero AVB frame drops over 60 minutes
- ✅ BE traffic utilizes remaining bandwidth
- ✅ No queue overflow events
- ✅ Queue statistics accurate throughout test

---

#### **VV-QUEUE-002: Failover Queue Reconfiguration**
**Objective**: Verify queue failover and reconfiguration under failure

**Preconditions**:
- Class A traffic active on TC6
- Failover mechanism implemented

**Procedure**:
1. Start with default mapping (PCP 6 → TC6)
2. Detect TC6 hardware failure (simulated via fault injection)
3. Reconfigure: PCP 6 → TC5 (fallback queue)
4. Verify Class A traffic continues on TC5
5. Measure reconfiguration time: <100ms
6. Confirm zero frame loss during failover

**Acceptance Criteria**:
- ✅ TC6 failure detected within 50ms
- ✅ Reconfiguration completes in <100ms
- ✅ Class A traffic resumes on TC5
- ✅ Zero frame loss during transition
- ✅ CBS parameters correctly applied to TC5
- ✅ Full functionality restored

---

## 🔧 Implementation Notes

### **PCP to TC Mapping Configuration**

```c
// Default IEEE 802.1Q mapping
static UINT8 g_DefaultPcpToTc[8] = {
    0, // PCP 0 (BE) → TC0
    1, // PCP 1 (BK) → TC1
    2, // PCP 2 (EE) → TC2
    3, // PCP 3 (CA) → TC3
    4, // PCP 4 (VI) → TC4
    5, // PCP 5 (VO) → TC5
    6, // PCP 6 (IC) → TC6 (Class A)
    7  // PCP 7 (NC) → TC7
};

NTSTATUS SetQueueMapping(UINT8 pcp, UINT8 tc) {
    if (pcp > 7 || tc > 7) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Update TQAVCC register (Traffic Class Assignment)
    UINT32 tqavcc = READ_REG32(I225_TQAVCC);
    
    // Clear existing mapping for this PCP (4 bits per PCP)
    tqavcc &= ~(0xF << (pcp * 4));
    
    // Set new TC assignment
    tqavcc |= (tc << (pcp * 4));
    
    WRITE_REG32(I225_TQAVCC, tqavcc);
    
    // Verify write
    UINT32 verify = READ_REG32(I225_TQAVCC);
    if (((verify >> (pcp * 4)) & 0xF) != tc) {
        return STATUS_IO_DEVICE_ERROR;
    }
    
    // Update driver state
    g_QueueMapping.PcpToTc[pcp] = tc;
    
    return STATUS_SUCCESS;
}
```

### **Queue Statistics Collection**

```c
typedef struct _QUEUE_STATS {
    UINT64 TxFrames[8]; // Per-TC frame counters
    UINT64 TxBytes[8];  // Per-TC byte counters
    UINT64 TxDrops[8];  // Per-TC drop counters
    UINT64 LastUpdateTime; // Timestamp of last update
} QUEUE_STATS;

NTSTATUS GetQueueStats(QUEUE_STATS* stats) {
    for (UINT32 tc = 0; tc < 8; tc++) {
        // Read hardware counters (I225 has per-TC statistics)
        stats->TxFrames[tc] = READ_REG64(I225_TC_TPCT(tc));
        stats->TxBytes[tc] = READ_REG64(I225_TC_TBCT(tc));
        stats->TxDrops[tc] = READ_REG64(I225_TC_TDCT(tc));
    }
    
    KeQuerySystemTime((PLARGE_INTEGER)&stats->LastUpdateTime);
    
    return STATUS_SUCCESS;
}

NTSTATUS ResetQueueStats() {
    for (UINT32 tc = 0; tc < 8; tc++) {
        // Clear hardware counters
        WRITE_REG32(I225_TC_TPCT(tc), 0);
        WRITE_REG32(I225_TC_TBCT(tc), 0);
        WRITE_REG32(I225_TC_TDCT(tc), 0);
    }
    
    return STATUS_SUCCESS;
}
```

### **Strict Priority Scheduling**

```c
NTSTATUS EnableStrictPriority() {
    // Configure TQAVCTRL for strict priority mode
    UINT32 tqavctrl = READ_REG32(I225_TQAVCTRL);
    
    // Set transmit mode to strict priority (not round-robin)
    tqavctrl |= TQAVCTRL_TRANSMIT_MODE_SP;
    
    // Disable data transmission until configured
    tqavctrl &= ~TQAVCTRL_DATA_TRAN_EN;
    
    WRITE_REG32(I225_TQAVCTRL, tqavctrl);
    
    // Enable transmission
    tqavctrl |= TQAVCTRL_DATA_TRAN_EN;
    WRITE_REG32(I225_TQAVCTRL, tqavctrl);
    
    DbgPrint("Strict priority scheduling enabled\n");
    return STATUS_SUCCESS;
}
```

### **Queue Congestion Handling**

```c
BOOLEAN IsQueueCongested(UINT8 tc) {
    // Check descriptor availability for this TC
    UINT32 availableDesc = g_TxQueues[tc].AvailableDescriptors;
    UINT32 totalDesc = g_TxQueues[tc].TotalDescriptors;
    
    // Consider congested if <10% descriptors available
    if (availableDesc < (totalDesc / 10)) {
        return TRUE;
    }
    
    return FALSE;
}

NTSTATUS HandleQueueBackpressure(UINT8 tc) {
    if (IsQueueCongested(tc)) {
        // For BE traffic (TC0-TC4), apply backpressure
        if (tc < 5) {
            // Pause admission control for this TC
            g_TxQueues[tc].BackpressureActive = TRUE;
            
            // Optionally send PAUSE frame (802.3x)
            SendPauseFrame(tc);
        }
        // For AVB traffic (TC5-TC7), never apply backpressure
        // (rely on admission control to prevent congestion)
    }
    
    return STATUS_SUCCESS;
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| PCP Mapping Accuracy | 100% | All 8 PCPs correctly mapped |
| Class A to TC6 Routing | 100% | All Class A frames use TC6 |
| Class B to TC5 Routing | 100% | All Class B frames use TC5 |
| Strict Priority Enforcement | 100% | TC7→TC6→...→TC0 order |
| Queue Statistics Accuracy | ±1 frame | Hardware counter validation |
| Dynamic Remapping Time | <1ms | Configuration update latency |
| Failover Reconfiguration | <100ms | Queue reassignment time |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] PCP mapping 100% accurate
- [x] Strict priority enforced
- [x] Class A/B correctly routed to TC6/TC5
- [x] Queue statistics accurate
- [x] Zero AVB frame drops in production test

---

**Standards**: IEEE 802.1Q (VLAN Priority), IEEE 802.1BA (AVB Systems), ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
