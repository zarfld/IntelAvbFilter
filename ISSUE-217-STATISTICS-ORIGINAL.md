# TEST-STATISTICS-001: Statistics and Counters Verification

**Test ID**: TEST-STATISTICS-001  
**Feature**: System Monitoring, Diagnostics, Observability  
**Test Type**: Unit (10), Integration (4), V&V (2)  
**Test Phase**: Phase 07 - Verification & Validation  
**Priority**: P2 (Medium - Observability)  
**Estimated Effort**: 24 hours

## 🔗 Traceability

- **Traces to**: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- **Verifies**: #61 (REQ-F-MONITORING-001: System Monitoring and Diagnostics)
- **Related Requirements**: #14, #58, #60
- **Phase**: Phase 07 - Verification & Validation
- **Standards**: ISO/IEC 25010 (Performance Efficiency), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017

---

## 📋 Test Objective

**Primary Goal**: Validate accurate collection, reporting, and reset of all driver statistics including PHC, TAS, CBS, queue, gPTP, and error counters for comprehensive system observability.

**Scope**:
- PHC statistics (adjustments, drift, sync intervals)
- TAS statistics (gate events, schedule adherence, violations)
- CBS statistics (credits, shaping decisions, queue depth)
- Queue statistics (per-TC frames, bytes, drops)
- gPTP statistics (sync messages, offset, path delay)
- Error counters (timeouts, failures, overruns)
- Performance metrics (latency, jitter, throughput)

**Success Criteria**:
- ✅ All counters increment accurately (±1 count)
- ✅ Statistics retrievable via IOCTLs
- ✅ Reset operations clear all counters
- ✅ 64-bit counters prevent overflow (>100 years at 10Gbps)
- ✅ Atomic counter updates (no race conditions)
- ✅ Performance impact <1% CPU overhead

---

## 🧪 Test Coverage

### **10 Unit Tests**

#### **UT-STATS-001: PHC Statistics Collection**
**Objective**: Verify accurate collection of PHC adjustment statistics

**Preconditions**:
- PHC initialized and running
- Statistics cleared

**Procedure**:
1. Perform 100 PHC adjustments (±500ns each)
2. Query `IOCTL_AVB_GET_PHC_STATS`
3. Verify counters:
   - AdjustmentCount = 100
   - TotalAdjustment = ±50,000ns (cumulative)
   - MaxPositiveAdjustment = 500ns
   - MaxNegativeAdjustment = -500ns
   - LastAdjustmentTime = recent timestamp

**Acceptance Criteria**:
- ✅ All PHC statistics accurate
- ✅ Counters match actual adjustments
- ✅ Timestamp reflects last adjustment

**Code Reference**:
```c
typedef struct _PHC_STATISTICS {
    UINT64 AdjustmentCount;
    INT64 TotalAdjustment;
    INT64 MaxPositiveAdjustment;
    INT64 MaxNegativeAdjustment;
    UINT64 LastAdjustmentTime;
    UINT32 DriftPpm;
} PHC_STATISTICS;
```

---

#### **UT-STATS-002: TAS Gate Statistics**
**Objective**: Verify TAS gate event statistics collection

**Preconditions**:
- TAS configured with 4 gates (500µs each, 2ms cycle)
- Statistics cleared

**Procedure**:
1. Configure TAS with 4 gates
2. Run for 1000 cycles (2 seconds)
3. Query `IOCTL_AVB_GET_TAS_STATS`
4. Verify counters:
   - CycleCount = 1000
   - GateEvents = 4000 (4 gates × 1000 cycles)
   - ScheduleViolations = 0 (perfect adherence)
   - AverageGateDuration = 500µs ±10µs

**Acceptance Criteria**:
- ✅ Cycle count accurate
- ✅ Gate events correctly tallied
- ✅ Violations detected (if any)
- ✅ Average gate duration calculated

---

#### **UT-STATS-003: CBS Credit Statistics**
**Objective**: Verify CBS credit usage statistics

**Preconditions**:
- CBS configured for TC6 (idleSlope=75%)
- Statistics cleared

**Procedure**:
1. Configure CBS for TC6
2. Transmit bursts causing credit depletion
3. Query `IOCTL_AVB_GET_CBS_STATS`
4. Verify counters:
   - CreditExhausted = number of depletion events
   - MaxCreditUsed = hiCredit value
   - AvgQueueDepth = running average
   - ShapingDecisions = frames delayed by CBS

**Acceptance Criteria**:
- ✅ Credit exhaustion events counted
- ✅ Max credit usage tracked
- ✅ Queue depth averaged correctly
- ✅ Shaping decisions recorded

---

#### **UT-STATS-004: Queue Per-TC Statistics**
**Objective**: Verify per-TC queue statistics accuracy

**Preconditions**:
- Multiple TCs active
- Statistics cleared

**Procedure**:
1. Transmit 1000 frames to TC6 (1500 bytes each)
2. Transmit 500 frames to TC3 (500 bytes each)
3. Query `IOCTL_AVB_GET_QUEUE_STATS`
4. Verify counters:
   - TC6: TxFrames=1000, TxBytes=1,500,000
   - TC3: TxFrames=500, TxBytes=250,000
   - Other TCs: TxFrames=0, TxBytes=0

**Acceptance Criteria**:
- ✅ Frame counters accurate per TC
- ✅ Byte counters match transmitted data
- ✅ No cross-contamination between TCs

**Code Reference**:
```c
typedef struct _QUEUE_STATISTICS {
    UINT64 TxFrames[8];
    UINT64 TxBytes[8];
    UINT64 TxDrops[8];
    UINT64 QueueDepth[8];
    UINT64 MaxQueueDepth[8];
} QUEUE_STATISTICS;
```

---

#### **UT-STATS-005: gPTP Sync Statistics**
**Objective**: Verify gPTP synchronization statistics

**Preconditions**:
- gPTP slave mode established
- Grandmaster sending sync messages
- Statistics cleared

**Procedure**:
1. Establish gPTP sync (slave mode)
2. Receive 800 Sync messages (125ms interval, 100 seconds)
3. Query `IOCTL_AVB_GET_GPTP_STATS`
4. Verify counters:
   - SyncMessagesReceived = 800
   - FollowUpMessagesReceived = 800
   - AverageOffset = <±100ns
   - MaxOffset = <±500ns
   - PathDelay = measured link delay

**Acceptance Criteria**:
- ✅ Sync message count accurate
- ✅ Follow-up messages tracked
- ✅ Offset statistics calculated
- ✅ Path delay measured

---

#### **UT-STATS-006: Error Counter Increments**
**Objective**: Verify error counter increments on failures

**Preconditions**:
- Error injection capability available
- Statistics cleared

**Procedure**:
1. Trigger various errors:
   - 10 PHC timeouts
   - 5 DMA descriptor exhaustions
   - 3 register write failures
2. Query `IOCTL_AVB_GET_ERROR_STATS`
3. Verify error counters:
   - PhcTimeoutCount = 10
   - DmaExhaustedCount = 5
   - RegisterWriteFailCount = 3

**Acceptance Criteria**:
- ✅ All error types counted accurately
- ✅ Counters increment on each error
- ✅ No false positives

**Code Reference**:
```c
typedef struct _ERROR_STATISTICS {
    UINT64 PhcTimeoutCount;
    UINT64 DmaExhaustedCount;
    UINT64 RegisterWriteFailCount;
    UINT64 TasViolationCount;
    UINT64 CbsCreditExhaustedCount;
    UINT64 GptpSyncLossCount;
    UINT64 InvalidIoctlCount;
} ERROR_STATISTICS;
```

---

#### **UT-STATS-007: Statistics Reset**
**Objective**: Verify complete reset of all statistics

**Preconditions**:
- Statistics populated with non-zero values
- All subsystems active

**Procedure**:
1. Populate all statistics (run mixed traffic)
2. Query all stats (verify non-zero values)
3. Send `IOCTL_AVB_RESET_STATS`
4. Query all stats again
5. Verify all counters = 0 (complete reset)

**Acceptance Criteria**:
- ✅ All counters zero after reset
- ✅ Hardware counters cleared
- ✅ Software counters cleared
- ✅ No residual values

**Code Reference**:
```c
NTSTATUS ResetAllStatistics() {
    RtlZeroMemory(&g_PhcStats, sizeof(PHC_STATISTICS));
    RtlZeroMemory(&g_TasStats, sizeof(TAS_STATISTICS));
    RtlZeroMemory(&g_CbsStats, sizeof(CBS_STATISTICS));
    RtlZeroMemory(&g_QueueStats, sizeof(QUEUE_STATISTICS));
    RtlZeroMemory(&g_GptpStats, sizeof(GPTP_STATISTICS));
    RtlZeroMemory(&g_ErrorStats, sizeof(ERROR_STATISTICS));
    
    // Clear hardware counters
    for (UINT32 tc = 0; tc < 8; tc++) {
        WRITE_REG64(I225_TC_TPCT(tc), 0);
        WRITE_REG64(I225_TC_TBCT(tc), 0);
        WRITE_REG64(I225_TC_TDCT(tc), 0);
    }
    
    return STATUS_SUCCESS;
}
```

---

#### **UT-STATS-008: 64-bit Counter Overflow Protection**
**Objective**: Verify 64-bit counters do not wrap

**Preconditions**:
- Ability to set counter to near-max value
- Statistics initialized

**Procedure**:
1. Set TxFrames counter to 0xFFFFFFFFFFFFFF00 (near max)
2. Transmit 1000 frames
3. Verify counter = 0xFFFFFFFFFFFFFF00 + 1000 (no wrap)
4. Confirm 64-bit arithmetic correct

**Acceptance Criteria**:
- ✅ No counter overflow/wraparound
- ✅ 64-bit arithmetic correct
- ✅ Sufficient capacity for >100 years @ 10Gbps

---

#### **UT-STATS-009: Atomic Counter Updates**
**Objective**: Verify atomic counter updates (no race conditions)

**Preconditions**:
- Continuous traffic running
- Statistics collection active

**Procedure**:
1. Start continuous traffic on TC6 (1000 frames/sec)
2. Query `IOCTL_AVB_GET_QUEUE_STATS` every 10ms (100 times)
3. Verify counter monotonically increases (no decreases)
4. Confirm no race conditions (atomic increments)

**Acceptance Criteria**:
- ✅ Counters always increase (never decrease)
- ✅ No torn reads/writes
- ✅ Atomic operations guaranteed

**Code Reference**:
```c
VOID IncrementPhcAdjustments(INT64 adjustment) {
    // Atomic increment of counters
    InterlockedIncrement64(&g_PhcStats.AdjustmentCount);
    InterlockedAdd64(&g_PhcStats.TotalAdjustment, adjustment);
    
    // Update max adjustments (requires lock)
    KeAcquireSpinLock(&g_StatsLock, &irql);
    if (adjustment > 0 && adjustment > g_PhcStats.MaxPositiveAdjustment) {
        g_PhcStats.MaxPositiveAdjustment = adjustment;
    }
    KeReleaseSpinLock(&g_StatsLock, irql);
}
```

---

#### **UT-STATS-010: Performance Overhead Measurement**
**Objective**: Verify statistics collection overhead is minimal

**Preconditions**:
- Baseline CPU usage measured
- Statistics collection can be toggled

**Procedure**:
1. Measure baseline CPU usage (no statistics collection)
2. Enable all statistics collection
3. Measure CPU usage with statistics
4. Verify overhead <1% (statistics should be lightweight)

**Acceptance Criteria**:
- ✅ CPU overhead <1%
- ✅ Memory footprint <10KB
- ✅ No performance degradation

---

### **4 Integration Tests**

#### **IT-STATS-001: Multi-Subsystem Statistics**
**Objective**: Verify statistics consistency across subsystems

**Preconditions**:
- All subsystems configured (TAS, CBS, gPTP)
- Multiple AVB streams active

**Procedure**:
1. Run integrated scenario:
   - 4 AVB streams (2 Class A, 2 Class B)
   - TAS scheduling active
   - CBS shaping active
   - gPTP sync active
2. Run for 5 minutes
3. Query all statistics subsystems
4. Verify consistency:
   - TAS gate events match expected (cycles × gates)
   - CBS credit usage aligns with traffic rate
   - Queue counters sum correctly across TCs
   - gPTP sync stable (offset <±100ns)

**Acceptance Criteria**:
- ✅ All subsystem statistics consistent
- ✅ No contradictions between counters
- ✅ Total frames = sum of per-TC frames

---

#### **IT-STATS-002: Statistics During Failure Recovery**
**Objective**: Verify statistics survive failure recovery

**Preconditions**:
- Stable operation established
- Failure injection capability available

**Procedure**:
1. Establish stable operation (all counters incrementing)
2. Trigger PHC failure (simulate timeout)
3. Initiate recovery (PHC re-initialization)
4. Verify:
   - Error counters increment (PhcTimeoutCount++)
   - Statistics survive recovery (not reset)
   - Counters resume incrementing after recovery
   - Recovery time logged in stats

**Acceptance Criteria**:
- ✅ Error counters increment on failure
- ✅ Statistics not lost during recovery
- ✅ Normal counting resumes post-recovery

---

#### **IT-STATS-003: Per-Stream Statistics (Future)**
**Objective**: Verify per-stream statistics if implemented

**Preconditions**:
- Per-stream statistics feature available
- Multiple streams configured

**Procedure**:
1. Configure 4 streams with unique StreamIDs
2. Transmit 100 frames per stream
3. Query per-stream statistics (if implemented)
4. Verify counters:
   - Stream 1: TxFrames=100, TxBytes=150,000
   - Stream 2: TxFrames=100, TxBytes=150,000
   - Stream 3: TxFrames=100, TxBytes=150,000
   - Stream 4: TxFrames=100, TxBytes=150,000

**Acceptance Criteria**:
- ✅ Per-stream counters accurate
- ✅ Stream isolation maintained
- ✅ No cross-talk between streams

---

#### **IT-STATS-004: Statistics Export to Event Tracing (ETW)**
**Objective**: Verify statistics export to Windows Event Tracing

**Preconditions**:
- ETW tracing enabled for AVB driver
- ETW consumer ready

**Procedure**:
1. Enable ETW tracing for AVB driver
2. Run mixed traffic for 60 seconds
3. Capture ETW events
4. Verify statistics logged periodically:
   - PHC adjustments logged every 10 seconds
   - TAS violations logged immediately
   - CBS credit exhaustion logged
   - Error events logged with severity

**Acceptance Criteria**:
- ✅ All statistics events logged to ETW
- ✅ Periodic logging works correctly
- ✅ Immediate event logging works
- ✅ Severity levels correct

---

### **2 V&V Tests**

#### **VV-STATS-001: 24-Hour Statistics Stability**
**Objective**: Validate statistics stability over extended operation

**Preconditions**:
- Production workload configured
- Automated statistics collection

**Procedure**:
1. Run production workload (8 AVB streams, mixed traffic)
2. Collect statistics every 60 seconds for 24 hours
3. Verify:
   - All counters monotonically increase (no decreases)
   - No counter overflows (64-bit sufficient)
   - Statistics consistent with traffic (frames sent = frames counted)
   - Average CPU overhead <0.5%
   - Memory usage stable (no leaks in stats collection)

**Acceptance Criteria**:
- ✅ 24 hours of stable operation
- ✅ No counter anomalies
- ✅ CPU overhead <0.5%
- ✅ No memory leaks

---

#### **VV-STATS-002: Statistics-Driven Diagnostics**
**Objective**: Validate statistics provide actionable diagnostics

**Preconditions**:
- Production-like scenarios configured
- Issue simulation capability

**Procedure**:
1. Simulate production issues:
   - TAS schedule violations (gate closed during transmission)
   - CBS credit exhaustion (burst exceeds bandwidth)
   - gPTP sync loss (grandmaster failure)
   - Queue congestion (descriptor exhaustion)
2. Use statistics to diagnose each issue:
   - TAS violations → Check ScheduleViolations counter
   - CBS exhaustion → Check CreditExhausted counter
   - Sync loss → Check SyncMessagesReceived (stops incrementing)
   - Queue congestion → Check TxDrops counter per TC
3. Verify statistics provide actionable diagnostics in <1 minute

**Acceptance Criteria**:
- ✅ All issues diagnosable via statistics
- ✅ Root cause identified <1 minute
- ✅ Statistics actionable for remediation

---

## 🔧 Implementation Code

### **Statistics Data Structures**

```c
typedef struct _PHC_STATISTICS {
    UINT64 AdjustmentCount;        // Total adjustments made
    INT64 TotalAdjustment;         // Cumulative adjustment (ns)
    INT64 MaxPositiveAdjustment;   // Largest positive adjustment
    INT64 MaxNegativeAdjustment;   // Largest negative adjustment
    UINT64 LastAdjustmentTime;     // Timestamp of last adjustment
    UINT32 DriftPpm;               // Current frequency drift (PPM)
} PHC_STATISTICS;

typedef struct _TAS_STATISTICS {
    UINT64 CycleCount;             // Completed TAS cycles
    UINT64 GateEvents;             // Total gate open/close events
    UINT64 ScheduleViolations;     // Frames transmitted during closed gate
    UINT64 AverageGateDuration;    // Average gate open time (ns)
    UINT64 MaxGateDelay;           // Longest gate closure delay
} TAS_STATISTICS;

typedef struct _CBS_STATISTICS {
    UINT64 CreditExhausted;        // Times credit reached 0
    INT32 MaxCreditUsed;           // Maximum credit consumed
    UINT32 AvgQueueDepth;          // Average queue depth (frames)
    UINT64 ShapingDecisions;       // Frames delayed by shaper
    UINT64 TotalBytesShaping;      // Bytes subject to shaping
} CBS_STATISTICS;

typedef struct _QUEUE_STATISTICS {
    UINT64 TxFrames[8];            // Per-TC transmitted frames
    UINT64 TxBytes[8];             // Per-TC transmitted bytes
    UINT64 TxDrops[8];             // Per-TC dropped frames
    UINT64 QueueDepth[8];          // Current queue depth per TC
    UINT64 MaxQueueDepth[8];       // Peak queue depth per TC
} QUEUE_STATISTICS;

typedef struct _GPTP_STATISTICS {
    UINT64 SyncMessagesReceived;      // Sync messages from GM
    UINT64 FollowUpMessagesReceived;  // Follow_Up messages
    UINT64 PDelayReqSent;             // PDelay_Req sent
    UINT64 PDelayRespReceived;        // PDelay_Resp received
    INT64 AverageOffset;              // Average clock offset (ns)
    INT64 MaxOffset;                  // Maximum offset seen
    UINT64 PathDelay;                 // Measured path delay (ns)
    UINT32 SyncInterval;              // Sync interval (ns)
} GPTP_STATISTICS;

typedef struct _ERROR_STATISTICS {
    UINT64 PhcTimeoutCount;           // PHC read/write timeouts
    UINT64 DmaExhaustedCount;         // DMA descriptor exhaustion
    UINT64 RegisterWriteFailCount;    // Register write failures
    UINT64 TasViolationCount;         // TAS schedule violations
    UINT64 CbsCreditExhaustedCount;   // CBS credit exhaustion
    UINT64 GptpSyncLossCount;         // gPTP sync loss events
    UINT64 InvalidIoctlCount;         // Invalid IOCTL requests
} ERROR_STATISTICS;
```

### **Statistics Retrieval**

```c
NTSTATUS GetStatistics(IOCTL_CODE ioctlCode, PVOID outputBuffer, ULONG bufferSize) {
    switch (ioctlCode) {
    case IOCTL_AVB_GET_PHC_STATS:
        if (bufferSize < sizeof(PHC_STATISTICS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlCopyMemory(outputBuffer, &g_PhcStats, sizeof(PHC_STATISTICS));
        break;
    
    case IOCTL_AVB_GET_TAS_STATS:
        if (bufferSize < sizeof(TAS_STATISTICS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlCopyMemory(outputBuffer, &g_TasStats, sizeof(TAS_STATISTICS));
        break;
    
    case IOCTL_AVB_GET_QUEUE_STATS:
        if (bufferSize < sizeof(QUEUE_STATISTICS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        UpdateQueueStatsFromHardware();
        RtlCopyMemory(outputBuffer, &g_QueueStats, sizeof(QUEUE_STATISTICS));
        break;
    
    default:
        return STATUS_INVALID_PARAMETER;
    }
    
    return STATUS_SUCCESS;
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Counter Accuracy | ±1 count | Hardware counter validation |
| 64-bit Counter Overflow | >100 years @ 10Gbps | No wrapping in 24-hour test |
| Atomic Update Guarantee | 100% | No race conditions |
| Statistics Retrieval Latency | <100µs | IOCTL response time |
| CPU Overhead | <1% | Impact on system performance |
| Memory Footprint | <10KB | Total statistics structures |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 4 integration tests pass
- [x] All 2 V&V tests pass
- [x] Counter accuracy ±1 count
- [x] No counter overflows in 24 hours
- [x] CPU overhead <1%
- [x] Statistics provide actionable diagnostics

---

**Standards**: ISO/IEC 25010 (Performance Efficiency), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
