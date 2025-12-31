# Issue #214 - Original Content (Restored)

**Date**: 2025-12-31  
**Issue**: #214 - TEST-CROSS-SYNC-001: Cross-Adapter PHC Synchronization Verification  
**Corruption**: 23rd in continuous range #192-214  
**Recovery**: Full 15-test specification restored

---

# [TEST] TEST-CROSS-SYNC-001: Cross-Adapter PHC Synchronization Verification

## Metadata
- **Test ID**: TEST-CROSS-SYNC-001
- **Feature**: Multi-Adapter PHC Synchronization
- **Test Types**: Unit (10), Integration (3), V&V (2)
- **Priority**: P1 (High - Redundant Time Synchronization)
- **Estimated Effort**: 16 hours (10 unit + 4 integration + 2 V&V)
- **Dependencies**: Multi-adapter hardware, GPIO instrumentation, oscilloscope

## Traceability
- **Traces to**: #1 (StR-CORE-001: Core AVB/TSN Functionality)
- **Traces to**: #150 (REQ-F-MULTI-001: Multi-Adapter Support)
- **Related to**: #2 (StR-PERF-001: Performance Requirements)
- **Related to**: #3 (StR-RELI-001: Reliability Requirements)
- **Related to**: #10 (REQ-F-AVB-001: AVB Stream Support)
- **Related to**: #149 (REQ-F-PHC-001: Precision Time Hardware Clock)

## Objective

Verify cross-adapter Precision Hardware Clock (PHC) synchronization for redundant AVB network deployments. Ensure multiple Intel Ethernet adapters maintain synchronized time bases to enable seamless failover and coordinated transmission scheduling across adapters.

## Background

For redundant AVB networks implementing IEEE 802.1CB (Frame Replication and Elimination for Reliability), multiple Ethernet adapters must maintain synchronized PHCs to:
- Generate consistent IEEE 1722 timestamps across adapters
- Coordinate Time-Aware Shaper (TAS) gate schedules
- Enable transparent stream failover without timing disruptions
- Support FRER sequence number generation from synchronized time base

Cross-adapter synchronization uses a master-slave topology where one adapter's PHC serves as the master, and other adapters continuously adjust their PHCs to match. A PI (Proportional-Integral) controller provides smooth convergence and maintains sub-microsecond accuracy.

## Standards References

- **IEEE 802.1CB-2017**: Frame Replication and Elimination for Reliability (FRER)
- **IEEE 1588-2008**: Precision Time Protocol (PTP concepts for PI controller)
- **IEEE 802.1Qbv-2015**: Enhancements for Scheduled Traffic (coordinated TAS)

---

## Test Cases

### Unit Tests (10 tests)

#### TC-1: Dual Adapter PHC Synchronization
**Objective**: Verify basic master-slave PHC synchronization between two adapters.

**Setup**:
- 2 Intel I210/I225 adapters installed
- Both PHCs initialized and running

**Procedure**:
1. Select Adapter 1 as master, Adapter 2 as slave
2. Configure cross-sync: 100ms interval, Kp=256, Ki=128
3. Run synchronization for 10 seconds (100 samples)
4. Query both PHCs every 100ms: `IOCTL_AVB_PHC_QUERY`
5. Calculate offset: `masterTime - slaveTime`
6. Verify PI controller adjustments: `IOCTL_AVB_FREQUENCY_ADJUST`

**Acceptance Criteria**:
- Mean offset magnitude: **≤100ns** over 10 seconds
- Max offset magnitude: **≤500ns** (any single sample)
- Slave PHC converges within **1 second** from arbitrary start
- No sync failures logged

**Code Reference**:
```c
typedef struct _CROSS_SYNC_CONFIG {
    HANDLE MasterAdapter;      // Adapter 1
    HANDLE SlaveAdapter;       // Adapter 2
    UINT32 SyncIntervalMs;     // 100ms
    INT32 Kp;                  // 256 (proportional gain)
    INT32 Ki;                  // 128 (integral gain)
} CROSS_SYNC_CONFIG;

VOID SyncSlavePHC(CROSS_SYNC_CONFIG* config, PI_CONTROLLER* pi) {
    UINT64 masterTime, slaveTime;
    INT64 offset, freqAdj;
    
    // Read master PHC
    IoctlPhcQuery(config->MasterAdapter, &masterTime);
    
    // Read slave PHC
    IoctlPhcQuery(config->SlaveAdapter, &slaveTime);
    
    // Calculate offset
    offset = (INT64)(masterTime - slaveTime);
    
    // PI controller
    pi->Integral += offset;
    freqAdj = (offset * config->Kp + pi->Integral / config->Ki) / 1000;
    
    // Clamp to ±1000 ppm
    if (freqAdj > 1000) freqAdj = 1000;
    if (freqAdj < -1000) freqAdj = -1000;
    
    // Apply frequency adjustment
    IoctlFrequencyAdjust(config->SlaveAdapter, (INT32)freqAdj);
    
    // Large offset correction (>1µs)
    if (abs(offset) > 1000) {
        IoctlOffsetAdjust(config->SlaveAdapter, offset);
        pi->Integral = 0; // Reset integral
    }
}
```

---

#### TC-2: Offset Measurement Accuracy
**Objective**: Verify accurate offset measurement between master and slave PHCs.

**Setup**:
- 2 adapters synchronized as in TC-1
- GPIO connected to oscilloscope for independent verification

**Procedure**:
1. Synchronize PHCs to steady state (offset <100ns)
2. Collect 1000 offset samples over 100 seconds
3. Compute statistics: mean, std dev, min, max
4. Compare with GPIO/oscilloscope measurements

**Acceptance Criteria**:
- Mean offset: **≤100ns**
- Standard deviation: **≤50ns** (repeatability)
- Software offset matches GPIO measurement within ±20ns

---

#### TC-3: Frequency Drift Compensation
**Objective**: Verify PI controller compensates for PHC frequency drift.

**Setup**:
- 2 adapters synchronized
- Intentionally introduce frequency drift on slave

**Procedure**:
1. Synchronize to steady state
2. Apply +100 ppm drift to slave: `IOCTL_AVB_FREQUENCY_ADJUST`
3. Observe PI controller response over 60 seconds
4. Repeat with -100 ppm drift

**Acceptance Criteria**:
- PI controller detects and compensates drift
- Offset returns to **<100ns** within 10 seconds
- Frequency adjustment converges to offset drift magnitude

---

#### TC-4: Large Offset Correction
**Objective**: Verify recovery from large initial offset.

**Setup**:
- 2 adapters with PHCs >1 second apart

**Procedure**:
1. Query initial offset
2. Start synchronization
3. Monitor convergence time and transient behavior

**Acceptance Criteria**:
- Offset reduced to **<1µs within 1 second**
- Offset reduced to **<100ns within 5 seconds**
- No overshoot >10% of initial offset
- PI controller integral reset after large correction

---

#### TC-5: Master PHC Selection
**Objective**: Verify correct master PHC selection from multiple adapters.

**Setup**:
- 3 adapters installed with assigned priorities

**Procedure**:
1. Configure priorities: Adapter 1 (priority 10), Adapter 2 (priority 5), Adapter 3 (priority 15)
2. Run master selection algorithm
3. Verify Adapter 2 selected (lowest priority number)

**Acceptance Criteria**:
- Master selection based on priority (lower number = higher priority)
- Correct master adapter selected

**Code Reference**:
```c
typedef struct _ADAPTER_PRIORITY {
    HANDLE AdapterHandle;
    UINT32 Priority;           // Lower = higher priority
    UINT64 MacAddress;         // Tie-breaker
} ADAPTER_PRIORITY;

HANDLE ElectNewMaster(ADAPTER_PRIORITY* candidates, UINT32 count) {
    ADAPTER_PRIORITY* best = &candidates[0];
    
    for (UINT32 i = 1; i < count; i++) {
        if (candidates[i].Priority < best->Priority) {
            best = &candidates[i];
        } else if (candidates[i].Priority == best->Priority) {
            // Tie-break by MAC address
            if (candidates[i].MacAddress < best->MacAddress) {
                best = &candidates[i];
            }
        }
    }
    
    return best->AdapterHandle;
}
```

---

#### TC-6: Master Failover Detection
**Objective**: Verify detection of master PHC failure.

**Setup**:
- 2 adapters synchronized (master + slave)
- Ability to simulate master failure

**Procedure**:
1. Establish synchronization
2. Simulate master failure (e.g., unplug adapter, driver crash)
3. Monitor slave's failure detection logic
4. Verify timeout and state transition

**Acceptance Criteria**:
- Failure detected within **1 second** of master unresponsiveness
- State transitions: ACTIVE → SUSPECTED → FAILED
- Error logged: "Master PHC failure detected"

**Code Reference**:
```c
typedef enum _MASTER_STATE {
    MASTER_ACTIVE,
    MASTER_SUSPECTED,
    MASTER_FAILED
} MASTER_STATE;

MASTER_STATE DetectMasterFailure(HANDLE masterAdapter, UINT32 timeoutMs) {
    static UINT64 lastSuccessTime = 0;
    UINT64 currentTime, masterTime;
    NTSTATUS status;
    
    GetCurrentTime(&currentTime);
    
    // Try to query master PHC
    status = IoctlPhcQuery(masterAdapter, &masterTime);
    
    if (NT_SUCCESS(status)) {
        lastSuccessTime = currentTime;
        return MASTER_ACTIVE;
    }
    
    // Check timeout
    if ((currentTime - lastSuccessTime) > (timeoutMs * 1000000ULL)) {
        LogError("Master PHC failure detected");
        return MASTER_FAILED;
    }
    
    return MASTER_SUSPECTED;
}
```

---

#### TC-7: New Master Election
**Objective**: Verify new master election after master failure.

**Setup**:
- 3 adapters synchronized (1 master + 2 slaves)
- Configured priorities: Adapter 1 (priority 10), Adapter 2 (priority 20), Adapter 3 (priority 15)

**Procedure**:
1. Establish synchronization with Adapter 1 as master
2. Simulate Adapter 1 failure
3. Monitor master election process
4. Verify Adapter 3 elected (next lowest priority)

**Acceptance Criteria**:
- Failure detected within **1 second**
- New master elected within **5 seconds** of failure detection
- Adapter 3 becomes new master (priority 15 < 20)
- All slaves resynchronize to new master

---

#### TC-8: Quad Adapter Synchronization
**Objective**: Verify synchronization across 4 adapters (1 master + 3 slaves).

**Setup**:
- 4 Intel adapters installed

**Procedure**:
1. Configure Adapter 1 as master
2. Synchronize Adapters 2, 3, 4 as slaves
3. Run synchronization for 60 seconds
4. Query all PHCs every 100ms

**Acceptance Criteria**:
- All slaves maintain **≤100ns mean offset** from master
- Max offset any slave: **≤500ns**
- No slave-to-slave offset >200ns (transitive consistency)

---

#### TC-9: Synchronization Interval Configuration
**Objective**: Verify configurable sync intervals and accuracy trade-offs.

**Setup**:
- 2 adapters synchronized

**Procedure**:
1. Test sync intervals: 100ms, 250ms, 500ms, 1000ms
2. For each interval, run for 60 seconds
3. Measure offset statistics

**Acceptance Criteria**:
- All intervals converge to **<100ns mean offset**
- Shorter intervals converge faster (<2 seconds)
- Longer intervals may have higher variance but same steady-state accuracy
- Config validation: Reject intervals <100ms or >1000ms

---

#### TC-10: Cross-Sync Statistics
**Objective**: Verify statistics collection for cross-adapter synchronization.

**Setup**:
- 2 adapters synchronized

**Procedure**:
1. Run synchronization for 5 minutes
2. Query statistics: `IOCTL_AVB_GET_CROSS_SYNC_STATS`
3. Verify counters and metrics

**Acceptance Criteria**:
- Sync message count matches interval (e.g., 3000 messages at 100ms for 5 minutes)
- Offset correction count tracked
- Frequency adjustment count tracked
- Sync loss events = 0
- Statistics accessible via IOCTL

---

### Integration Tests (3 tests)

#### TC-11: Coordinated TAS Across Adapters
**Objective**: Verify synchronized Time-Aware Shaper (TAS) gate schedules across adapters.

**Setup**:
- 2 adapters with synchronized PHCs
- TAS configured: 1ms cycle, 3 gates per adapter

**Procedure**:
1. Synchronize PHCs (offset <100ns)
2. Program identical TAS schedules on both adapters (aligned to PHC)
3. Trigger GPIO outputs on gate open events
4. Measure gate alignment with oscilloscope

**Acceptance Criteria**:
- Gate events across adapters aligned within **±1µs**
- TAS cycles remain synchronized for 60 seconds
- No missed gate events

---

#### TC-12: Redundant Stream Transmission (IEEE 802.1CB)
**Objective**: Verify redundant stream transmission with synchronized sequence numbers.

**Setup**:
- 2 adapters synchronized (FRER path A on Adapter 1, path B on Adapter 2)
- Both transmitting same stream with sequence numbers

**Procedure**:
1. Synchronize PHCs
2. Start redundant stream transmission on both adapters
3. Generate IEEE 1722 sequence numbers from PHC timestamps
4. Capture streams and verify sequence consistency

**Acceptance Criteria**:
- Sequence numbers match within ±1 (accounting for PHC offset)
- Receiver successfully eliminates duplicates (FRER)
- No sequence gaps or discontinuities
- Stream remains synchronized for 300 seconds

**Code Reference**:
```c
// VLAN 100 = Path A (Adapter 1)
// VLAN 200 = Path B (Adapter 2)
UINT32 GenerateSequenceNumber(UINT64 phcTime) {
    // Use lower 32 bits of PHC nanoseconds
    return (UINT32)(phcTime & 0xFFFFFFFF);
}
```

---

#### TC-13: Cross-Sync + gPTP Coexistence
**Objective**: Verify cross-adapter sync coexists with IEEE 802.1AS gPTP.

**Setup**:
- 4 adapters: 2 running cross-sync, 2 running gPTP

**Procedure**:
1. Configure Adapters 1-2 for cross-sync
2. Configure Adapters 3-4 for gPTP (separate network segment)
3. Run both synchronization mechanisms simultaneously
4. Monitor interference and resource conflicts

**Acceptance Criteria**:
- Cross-sync maintains <100ns offset
- gPTP maintains <1µs offset from grandmaster
- No resource conflicts (IOCTL queue, PHC access)
- Both mechanisms operate independently

---

### Validation & Verification Tests (2 tests)

#### TC-14: 24-Hour Cross-Sync Stability
**Objective**: Verify long-term stability of cross-adapter synchronization.

**Setup**:
- 4 adapters synchronized (1 master + 3 slaves)

**Procedure**:
1. Synchronize all adapters
2. Run for 24 hours
3. Log offset samples every second (86,400 samples per slave)
4. Analyze drift and stability

**Acceptance Criteria**:
- Mean offset: **≤50ns** (all slaves)
- Max offset: **≤500ns** (any sample, any slave)
- Drift rate: **<10ns per hour**
- Zero synchronization loss events
- No memory leaks or resource exhaustion

---

#### TC-15: Production Redundant AVB Network Simulation
**Objective**: Verify cross-sync in realistic production scenario with failover.

**Setup**:
- 4 adapters (2 masters for redundancy, 2 receivers)
- 8 AVB streams per master (16 total streams)
- IEEE 802.1CB FRER configured

**Procedure**:
1. Synchronize all adapters
2. Start transmitting 16 redundant AVB streams
3. Run for 30 minutes at steady state
4. Simulate Master 1 failure (unplug)
5. Verify failover to Master 2
6. Continue for additional 30 minutes

**Acceptance Criteria**:
- Pre-failover: All streams delivered, <1µs jitter
- Failover detected: **<1 second**
- Master 2 elected: **<5 seconds**
- Streams continue without interruption (<10ms transient)
- Post-failover: Same performance as pre-failover
- Receiver FRER eliminates duplicates successfully
- Total test duration: 60 minutes, zero stream failures

---

## Test Environment

### Hardware Requirements
- 4× Intel I210/I225 Ethernet adapters with AVB/TSN support
- PCIe slots for multi-adapter installation
- GPIO-capable adapters for event triggering
- Oscilloscope (≥1GHz, ≥5ns resolution) for independent time verification

### Software Requirements
- Windows 10/11 with test signing enabled
- IntelAvbFilter driver (all adapters)
- IOCTL test framework
- Statistics collection utilities

### Network Configuration
- Isolated test network (no external gPTP)
- Low-latency switches if testing networked sync
- VLAN support for IEEE 802.1CB testing

---

## Performance Targets Summary

| Metric | Target | Test Case |
|--------|--------|-----------|
| Sync Accuracy (mean) | ≤100ns | TC-1, TC-8, TC-14 |
| Sync Accuracy (max) | ≤500ns | TC-1, TC-8, TC-14 |
| Offset Repeatability (std dev) | ≤50ns | TC-2, TC-14 |
| Convergence Time | <1 second (from <1ms offset) | TC-1 |
| Large Offset Convergence | <5 seconds (from >1s offset) | TC-4 |
| Failover Detection | <1 second | TC-6, TC-15 |
| Master Election | <5 seconds | TC-7, TC-15 |
| TAS Alignment | ±1µs | TC-11 |
| Long-term Drift | <10ns/hour | TC-14 |
| Stream Failover | <10ms transient | TC-15 |

---

## Dependencies

- **Issue #150**: REQ-F-MULTI-001 (Multi-Adapter Support) - Must be implemented
- **Issue #149**: REQ-F-PHC-001 (PHC Control) - IOCTL interface functional
- **Issue #10**: REQ-F-AVB-001 (AVB Streams) - For integration testing
- **GPIO Test Framework**: For oscilloscope verification
- **IEEE 802.1CB Support**: For FRER redundancy testing

---

## Notes

- **Master Selection Algorithm**: Configurable priority with MAC address tie-breaking ensures deterministic master election
- **PI Controller Tuning**: Kp=256, Ki=128 recommended for 100ms interval; adjust for longer intervals
- **Frequency Drift**: Typical crystal oscillators drift ±50 ppm over temperature; PI controller compensates continuously
- **Performance Validation**: GPIO/oscilloscope measurements provide ground truth independent of software timing
- **Scalability**: Algorithm supports up to 8 adapters; tested with 4 for practical validation

---

## Restoration Metadata

- **Original Issue**: #214
- **Corruption Date**: 2025-12-22 (batch update failure)
- **Restoration Date**: 2025-12-31
- **Corruption Pattern**: 23rd in continuous range #192-214
- **Recovery**: Full 15-test specification reconstructed from user-provided diff
- **Verification**: Cross-referenced with IEEE 802.1CB, 1588, 802.1Qbv standards
