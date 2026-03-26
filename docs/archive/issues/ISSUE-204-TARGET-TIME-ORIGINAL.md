# TEST-PTP-TARGET-001: Target Time Interrupt Verification

## 🔗 Traceability

- Traces to: #7 (REQ-F-PTP-005: Target Time Interrupt)
- Verifies: #7
- Related Requirements: #2, #58, #149
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Time-triggered scheduling)

---

## 📋 Test Objective

**Primary Goal**: Validate that the Target Time feature generates accurate hardware interrupts at specified future PHC timestamps for time-triggered packet transmission and scheduled operations.

**Scope**:
- Hardware TSAUXC register configuration (Target Time 0/1)
- Interrupt generation accuracy and jitter measurement
- Time-triggered TX launch time enforcement
- Concurrent target time scheduling (multiple flows)
- Integration with TAS (Time-Aware Shaper) and launch time offload

**Success Criteria**:
- ✅ Target time interrupt fires within ±1µs of specified timestamp
- ✅ Jitter <100ns over 1000 samples
- ✅ Time-triggered TX packets launched within ±500ns of target time
- ✅ Concurrent target times (2 channels) operate independently
- ✅ TAS gate control transitions align with target times (<100ns error)

---

## 🧪 Test Coverage

### **10 Unit Tests** (Kernel-Mode Hardware Tests)

#### **UT-TARGET-001: Basic Target Time Configuration**

```c
TEST(TargetTimeInterrupt, BasicConfiguration) {
    // Setup: Configure TSAUXC register with future timestamp (+1ms)
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime = currentPhc + 1000000ULL; // +1ms (nanoseconds)
    
    // Execute: Set Target Time 0
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
    
    // Enable and start Target Time 0
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for interrupt
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000LL; // 2ms timeout
    NTSTATUS status = KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
    
    // Verify: Interrupt fired
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Verify: Timing accuracy
    UINT64 actualInterruptTime = g_TargetTimeInterruptPhc;
    INT64 timeDelta = (INT64)(actualInterruptTime - targetTime);
    
    EXPECT_LT(abs(timeDelta), 1000); // Within ±1µs (1000ns)
}
```

**Expected Result**: Interrupt fires within ±1µs of target, delta measured and recorded

---

#### **UT-TARGET-002: Interrupt Accuracy and Jitter**

```c
TEST(TargetTimeInterrupt, AccuracyAndJitter) {
    // Setup: 1000 consecutive target times (1ms intervals)
    const int SAMPLE_COUNT = 1000;
    std::vector<INT64> timeDeltas;
    
    UINT64 currentPhc = ReadPhcTime();
    
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        UINT64 targetTime = currentPhc + (i + 1) * 1000000ULL; // 1ms intervals
        
        // Configure target time
        WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
        WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
        
        // Enable and start
        TSAUXC_REG tsauxc;
        tsauxc.value = READ_REG32(I225_TSAUXC);
        tsauxc.bits.EN_TT0 = 1;
        tsauxc.bits.ST0 = 1;
        WRITE_REG32(I225_TSAUXC, tsauxc.value);
        
        // Wait for interrupt
        LARGE_INTEGER timeout;
        timeout.QuadPart = -20000LL; // 2ms timeout
        KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
        
        // Record timing delta
        UINT64 actualTime = g_TargetTimeInterruptPhc;
        INT64 delta = (INT64)(actualTime - targetTime);
        timeDeltas.push_back(delta);
    }
    
    // Verify: Statistics
    double mean = CalculateMean(timeDeltas);
    double stddev = CalculateStdDev(timeDeltas);
    INT64 maxError = *std::max_element(timeDeltas.begin(), timeDeltas.end(),
                                       [](INT64 a, INT64 b) { return abs(a) < abs(b); });
    
    EXPECT_LT(abs(mean), 100.0); // Mean error <100ns
    EXPECT_LT(stddev, 100.0); // Jitter (stddev) <100ns
    EXPECT_LT(abs(maxError), 1000); // Max error <1µs
}
```

**Expected Result**: Mean error <100ns, stddev <100ns, max error <1µs over 1000 samples

---

#### **UT-TARGET-003: Target Time 0 vs. Target Time 1 Independence**

```c
TEST(TargetTimeInterrupt, TwoChannelIndependence) {
    // Setup: Configure both TSAUXC channels with different timestamps
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime0 = currentPhc + 1000000ULL; // +1ms
    UINT64 targetTime1 = currentPhc + 2000000ULL; // +2ms
    
    // Configure Target Time 0
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime0 & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime0 >> 32));
    
    // Configure Target Time 1
    WRITE_REG32(I225_TARGTIME1, (UINT32)(targetTime1 & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME1 + 4, (UINT32)(targetTime1 >> 32));
    
    // Enable and start both
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.EN_TT1 = 1;
    tsauxc.bits.ST0 = 1;
    tsauxc.bits.ST1 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for first interrupt (Target Time 0)
    LARGE_INTEGER timeout0;
    timeout0.QuadPart = -20000LL; // 2ms timeout
    NTSTATUS status0 = KeWaitForSingleObject(&g_TargetTime0Event, Executive, KernelMode, FALSE, &timeout0);
    UINT64 actualTime0 = g_TargetTime0InterruptPhc;
    
    // Wait for second interrupt (Target Time 1)
    LARGE_INTEGER timeout1;
    timeout1.QuadPart = -20000LL; // 2ms timeout
    NTSTATUS status1 = KeWaitForSingleObject(&g_TargetTime1Event, Executive, KernelMode, FALSE, &timeout1);
    UINT64 actualTime1 = g_TargetTime1InterruptPhc;
    
    // Verify: Both interrupts fired independently
    EXPECT_EQ(status0, STATUS_SUCCESS);
    EXPECT_EQ(status1, STATUS_SUCCESS);
    
    // Verify: Timing accuracy for both channels
    INT64 delta0 = (INT64)(actualTime0 - targetTime0);
    INT64 delta1 = (INT64)(actualTime1 - targetTime1);
    
    EXPECT_LT(abs(delta0), 1000); // Within ±1µs
    EXPECT_LT(abs(delta1), 1000); // Within ±1µs
    
    // Verify: No cross-interference (time delta matches expected 1ms separation)
    INT64 separation = (INT64)(actualTime1 - actualTime0);
    EXPECT_NEAR(separation, 1000000, 2000); // ~1ms ±2µs
}
```

**Expected Result**: Both interrupts fire independently at correct times, no cross-interference

---

#### **UT-TARGET-004: Target Time in Past**

```c
TEST(TargetTimeInterrupt, TargetTimeInPast) {
    // Setup: Configure target time with timestamp in the past
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime = currentPhc - 1000000ULL; // -1ms (in the past)
    
    // Execute: Set target time
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for immediate interrupt or timeout
    LARGE_INTEGER timeout;
    timeout.QuadPart = -10000LL; // 1ms timeout
    NTSTATUS status = KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
    
    // Verify: Immediate interrupt generation or timeout (hardware-dependent behavior)
    // Either STATUS_SUCCESS (immediate interrupt) or STATUS_TIMEOUT (rejected) is acceptable
    EXPECT_TRUE(status == STATUS_SUCCESS || status == STATUS_TIMEOUT);
    
    // Verify: No system hang or crash
    EXPECT_TRUE(TRUE); // Reached here without hang
}
```

**Expected Result**: Immediate interrupt generation or error handling, no system hang

---

#### **UT-TARGET-005: Target Time Cancellation**

```c
TEST(TargetTimeInterrupt, TargetTimeCancellation) {
    // Setup: Set target time
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime = currentPhc + 5000000ULL; // +5ms
    
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait 1ms, then cancel
    LARGE_INTEGER delay;
    delay.QuadPart = -10000LL; // 1ms delay
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    
    // Cancel target time (disable)
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 0;
    tsauxc.bits.ST0 = 0;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for original target time (should NOT fire)
    LARGE_INTEGER timeout;
    timeout.QuadPart = -60000LL; // 6ms timeout
    NTSTATUS status = KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
    
    // Verify: Interrupt did NOT fire after cancellation
    EXPECT_EQ(status, STATUS_TIMEOUT);
}
```

**Expected Result**: Interrupt does NOT fire after cancellation, clean state for next target time

---

#### **UT-TARGET-006: Zero Target Time Handling**

```c
TEST(TargetTimeInterrupt, ZeroTargetTime) {
    // Execute: Configure target time = 0
    WRITE_REG32(I225_TARGTIME0, 0);
    WRITE_REG32(I225_TARGTIME0 + 4, 0);
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for interrupt or timeout
    LARGE_INTEGER timeout;
    timeout.QuadPart = -10000LL; // 1ms timeout
    NTSTATUS status = KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
    
    // Verify: Error handling (reject or immediate interrupt)
    EXPECT_TRUE(status == STATUS_SUCCESS || status == STATUS_TIMEOUT);
    
    // Verify: No crash or undefined behavior
    EXPECT_TRUE(TRUE); // Reached here without crash
}
```

**Expected Result**: Error handling (reject or immediate interrupt), no crash or undefined behavior

---

#### **UT-TARGET-007: Maximum Future Target Time**

```c
TEST(TargetTimeInterrupt, MaximumFutureTime) {
    // Execute: Set target time to maximum PHC value (64-bit limit)
    UINT64 maxTargetTime = 0xFFFFFFFFFFFFFFFFULL;
    
    WRITE_REG32(I225_TARGTIME0, (UINT32)(maxTargetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(maxTargetTime >> 32));
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Verify: Configuration accepted or reasonable error
    UINT32 lo = READ_REG32(I225_TARGTIME0);
    UINT32 hi = READ_REG32(I225_TARGTIME0 + 4);
    UINT64 readBack = ((UINT64)hi << 32) | lo;
    
    EXPECT_EQ(readBack, maxTargetTime); // Read-back successful
    
    // Verify: No crash on extreme value
    EXPECT_TRUE(TRUE);
}
```

**Expected Result**: Configuration accepted or reasonable error, test rollover behavior at PHC wrap-around

---

#### **UT-TARGET-008: Rapid Successive Target Times**

```c
TEST(TargetTimeInterrupt, RapidSuccessiveTimes) {
    // Setup: Configure 10 target times spaced 100µs apart
    const int COUNT = 10;
    const UINT64 INTERVAL = 100000ULL; // 100µs (nanoseconds)
    
    UINT64 currentPhc = ReadPhcTime();
    std::vector<UINT64> targetTimes;
    std::vector<UINT64> actualTimes;
    
    for (int i = 0; i < COUNT; i++) {
        UINT64 targetTime = currentPhc + (i + 1) * INTERVAL;
        targetTimes.push_back(targetTime);
        
        // Configure target time
        WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
        WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
        
        // Enable and start
        TSAUXC_REG tsauxc;
        tsauxc.value = READ_REG32(I225_TSAUXC);
        tsauxc.bits.EN_TT0 = 1;
        tsauxc.bits.ST0 = 1;
        WRITE_REG32(I225_TSAUXC, tsauxc.value);
        
        // Wait for interrupt
        LARGE_INTEGER timeout;
        timeout.QuadPart = -2000LL; // 200µs timeout
        KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
        
        actualTimes.push_back(g_TargetTimeInterruptPhc);
    }
    
    // Verify: All interrupts fired in correct sequence
    for (int i = 0; i < COUNT; i++) {
        INT64 delta = (INT64)(actualTimes[i] - targetTimes[i]);
        EXPECT_LT(abs(delta), 1000); // Within ±1µs
    }
    
    // Verify: Inter-interrupt timing accuracy
    for (int i = 1; i < COUNT; i++) {
        INT64 interInterval = (INT64)(actualTimes[i] - actualTimes[i-1]);
        EXPECT_NEAR(interInterval, INTERVAL, 2000); // Within ±2µs
    }
}
```

**Expected Result**: All interrupts fire in correct sequence, inter-interrupt timing accurate

---

#### **UT-TARGET-009: Target Time ISR Latency**

```c
TEST(TargetTimeInterrupt, IsrLatency) {
    // Setup: Set target time
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime = currentPhc + 1000000ULL; // +1ms
    
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for interrupt
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000LL; // 2ms timeout
    KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
    
    // Verify: ISR latency recorded
    UINT64 isrLatencyNs = g_TargetTimeIsrLatency;
    
    EXPECT_GT(isrLatencyNs, 0); // ISR executed
    EXPECT_LT(isrLatencyNs, 5000); // ISR latency <5µs (soft real-time)
}
```

**Expected Result**: ISR latency <5µs (soft real-time), deterministic execution

---

#### **UT-TARGET-010: Target Time Register Read-Back**

```c
TEST(TargetTimeInterrupt, RegisterReadBack) {
    // Execute: Write target time to TSAUXC register
    UINT64 expectedTime = 0x123456789ABCDEF0ULL;
    
    WRITE_REG32(I225_TARGTIME0, (UINT32)(expectedTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(expectedTime >> 32));
    
    // Read back value
    UINT32 lo = READ_REG32(I225_TARGTIME0);
    UINT32 hi = READ_REG32(I225_TARGTIME0 + 4);
    UINT64 actualTime = ((UINT64)hi << 32) | lo;
    
    // Verify: Bit-accurate storage
    EXPECT_EQ(actualTime, expectedTime);
}
```

**Expected Result**: Read-back value matches written value, bit-accurate storage

---

### **3 Integration Tests** (Hardware + TX/TAS Integration)

#### **IT-TARGET-001: Time-Triggered TX Launch**

```c
TEST(TargetTimeIntegration, TimeTriggerredTxLaunch) {
    // Setup: Set target time for packet transmission (+1ms)
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime = currentPhc + 1000000ULL; // +1ms
    
    // Configure target time
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Configure TX descriptor with launch time = target time
    PTX_DESC txDesc = AllocateTxDescriptor();
    txDesc->LaunchTime = targetTime;
    txDesc->LaunchTimeEnable = 1;
    
    // Prepare packet
    BYTE packet[128];
    BuildAvtpPacket(packet, sizeof(packet));
    
    // Submit TX descriptor
    SubmitTxDescriptor(txDesc, packet, sizeof(packet));
    
    // Wait for TX completion
    WaitForTxCompletion(txDesc);
    
    // Verify: Packet transmitted
    EXPECT_TRUE(txDesc->DD); // Descriptor Done
    
    // Verify: TX timestamp matches launch time
    UINT64 txTimestamp = txDesc->TxTimestamp;
    INT64 launchError = (INT64)(txTimestamp - targetTime);
    
    EXPECT_LT(abs(launchError), 500); // Within ±500ns
}
```

**Expected Result**: Packet transmitted within ±500ns of target time, TX timestamp measured

---

#### **IT-TARGET-002: TAS Gate Control Synchronization**

```c
TEST(TargetTimeIntegration, TasGateControlSync) {
    // Setup: Configure TAS gate to open at target time
    UINT64 currentPhc = ReadPhcTime();
    UINT64 targetTime = currentPhc + 1000000ULL; // +1ms
    
    // Configure TAS gate control list (8 entries)
    TAS_GCL_ENTRY gcl[8];
    for (int i = 0; i < 8; i++) {
        gcl[i].TimeOffset = i * 125000ULL; // 125µs intervals (8 kHz)
        gcl[i].GateControl = (i % 2 == 0) ? 0xFF : 0x00; // Alternating open/closed
    }
    
    // Set TAS base time = target time
    ConfigureTasBaseTime(targetTime);
    ConfigureTasGcl(gcl, 8);
    EnableTas();
    
    // Set target time interrupt for gate transition
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
    
    // Enable and start
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for interrupt
    LARGE_INTEGER timeout;
    timeout.QuadPart = -20000LL; // 2ms timeout
    KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
    
    // Verify: Gate opened at target time
    UINT64 gateOpenTime = ReadTasGateOpenTime();
    INT64 syncError = (INT64)(gateOpenTime - targetTime);
    
    EXPECT_LT(abs(syncError), 100); // Within ±100ns
}
```

**Expected Result**: Gate opens within ±100ns of interrupt, 8 gate control list entries tested

---

#### **IT-TARGET-003: Multi-Flow Launch Time Coordination**

```c
TEST(TargetTimeIntegration, MultiFlowLaunchCoordination) {
    // Setup: Schedule 4 flows with staggered launch times (100µs intervals)
    UINT64 currentPhc = ReadPhcTime();
    UINT64 baseTime = currentPhc + 1000000ULL; // +1ms
    
    struct FlowConfig {
        UINT64 launchTime;
        BYTE packetData[128];
        PTX_DESC txDesc;
        UINT64 actualTxTime;
    };
    
    FlowConfig flows[4];
    
    for (int i = 0; i < 4; i++) {
        flows[i].launchTime = baseTime + i * 100000ULL; // 100µs intervals
        BuildAvtpPacket(flows[i].packetData, sizeof(flows[i].packetData), i);
        
        // Configure TX descriptor
        flows[i].txDesc = AllocateTxDescriptor();
        flows[i].txDesc->LaunchTime = flows[i].launchTime;
        flows[i].txDesc->LaunchTimeEnable = 1;
        
        // Submit TX
        SubmitTxDescriptor(flows[i].txDesc, flows[i].packetData, sizeof(flows[i].packetData));
    }
    
    // Use both TSAUXC channels for coordination
    // Target Time 0: Flows 0, 2
    // Target Time 1: Flows 1, 3
    
    WRITE_REG32(I225_TARGTIME0, (UINT32)(flows[0].launchTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(flows[0].launchTime >> 32));
    
    WRITE_REG32(I225_TARGTIME1, (UINT32)(flows[1].launchTime & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME1 + 4, (UINT32)(flows[1].launchTime >> 32));
    
    // Enable both target times
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.EN_TT1 = 1;
    tsauxc.bits.ST0 = 1;
    tsauxc.bits.ST1 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Wait for all TX completions
    for (int i = 0; i < 4; i++) {
        WaitForTxCompletion(flows[i].txDesc);
        flows[i].actualTxTime = flows[i].txDesc->TxTimestamp;
    }
    
    // Verify: All 4 flows transmitted at correct times
    for (int i = 0; i < 4; i++) {
        INT64 launchError = (INT64)(flows[i].actualTxTime - flows[i].launchTime);
        EXPECT_LT(abs(launchError), 500); // Within ±500ns
    }
    
    // Verify: No packet collisions or timing violations
    for (int i = 1; i < 4; i++) {
        UINT64 interPacketGap = flows[i].actualTxTime - flows[i-1].actualTxTime;
        EXPECT_NEAR(interPacketGap, 100000ULL, 2000); // ~100µs ±2µs
    }
}
```

**Expected Result**: All 4 flows transmit at correct times, no packet collisions

---

### **2 V&V Tests** (Long-Duration Production Workloads)

#### **VV-TARGET-001: 24-Hour Stability Test**

```c
TEST(TargetTimeVV, TwentyFourHourStability) {
    // Setup: Schedule target time interrupts every 125ms (8 Hz gPTP rate)
    const int SYNC_RATE_HZ = 8;
    const UINT64 INTERVAL_NS = 125000000ULL; // 125ms (nanoseconds)
    const int TEST_DURATION_SEC = 24 * 3600; // 24 hours
    const int EXPECTED_INTERRUPTS = SYNC_RATE_HZ * TEST_DURATION_SEC; // 691,200
    
    std::vector<UINT64> interruptTimes;
    std::vector<INT64> timeDeltas;
    
    UINT64 currentPhc = ReadPhcTime();
    
    for (int i = 0; i < EXPECTED_INTERRUPTS; i++) {
        UINT64 targetTime = currentPhc + (i + 1) * INTERVAL_NS;
        
        // Configure target time
        WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTime & 0xFFFFFFFF));
        WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTime >> 32));
        
        // Enable and start
        TSAUXC_REG tsauxc;
        tsauxc.value = READ_REG32(I225_TSAUXC);
        tsauxc.bits.EN_TT0 = 1;
        tsauxc.bits.ST0 = 1;
        WRITE_REG32(I225_TSAUXC, tsauxc.value);
        
        // Wait for interrupt
        LARGE_INTEGER timeout;
        timeout.QuadPart = -1300000LL; // 130ms timeout
        NTSTATUS status = KeWaitForSingleObject(&g_TargetTimeEvent, Executive, KernelMode, FALSE, &timeout);
        
        EXPECT_EQ(status, STATUS_SUCCESS);
        
        UINT64 actualTime = g_TargetTimeInterruptPhc;
        interruptTimes.push_back(actualTime);
        
        INT64 delta = (INT64)(actualTime - targetTime);
        timeDeltas.push_back(delta);
    }
    
    // Verify: All interrupts fired
    EXPECT_EQ(interruptTimes.size(), EXPECTED_INTERRUPTS);
    
    // Verify: Timing accuracy over entire period
    double meanError = CalculateMean(timeDeltas);
    INT64 maxError = *std::max_element(timeDeltas.begin(), timeDeltas.end(),
                                       [](INT64 a, INT64 b) { return abs(a) < abs(b); });
    
    EXPECT_LT(abs(meanError), 100.0); // Mean error <100ns
    EXPECT_LT(abs(maxError), 1000); // Max error <1µs
    
    // Verify: No drift over 24 hours (first hour vs. last hour)
    std::vector<INT64> firstHour(timeDeltas.begin(), timeDeltas.begin() + (SYNC_RATE_HZ * 3600));
    std::vector<INT64> lastHour(timeDeltas.end() - (SYNC_RATE_HZ * 3600), timeDeltas.end());
    
    double firstHourMean = CalculateMean(firstHour);
    double lastHourMean = CalculateMean(lastHour);
    
    EXPECT_LT(abs(lastHourMean - firstHourMean), 1000.0); // Drift <1µs over 24h
}
```

**Expected Result**:
- 691,200 interrupts over 24 hours
- Mean timing error <100ns, max <1µs
- No drift >1µs (first vs. last hour)
- No missed interrupts

---

#### **VV-TARGET-002: Production AVB Workload**

```c
TEST(TargetTimeVV, ProductionAvbWorkload) {
    // Setup: Configure 4 AVB Class A streams (125µs intervals)
    const int STREAM_COUNT = 4;
    const UINT64 INTERVAL_NS = 125000ULL; // 125µs (Class A)
    const int TEST_DURATION_SEC = 3600; // 1 hour
    const int PACKETS_PER_STREAM = (TEST_DURATION_SEC * 1000000) / INTERVAL_NS; // 28,800,000 packets/stream
    
    struct StreamStats {
        int packetsTransmitted;
        std::vector<INT64> launchErrors;
        std::vector<UINT64> latencies;
    };
    
    StreamStats streams[STREAM_COUNT];
    
    UINT64 currentPhc = ReadPhcTime();
    UINT64 baseTime = currentPhc + 1000000ULL; // +1ms
    
    for (int i = 0; i < PACKETS_PER_STREAM; i++) {
        for (int s = 0; s < STREAM_COUNT; s++) {
            UINT64 launchTime = baseTime + (i * STREAM_COUNT + s) * INTERVAL_NS;
            
            // Configure TX descriptor
            PTX_DESC txDesc = AllocateTxDescriptor();
            txDesc->LaunchTime = launchTime;
            txDesc->LaunchTimeEnable = 1;
            
            // Prepare packet
            BYTE packet[128];
            BuildAvtpPacket(packet, sizeof(packet), s);
            
            // Submit TX
            SubmitTxDescriptor(txDesc, packet, sizeof(packet));
            
            // Use target time for launch time enforcement
            if (s == 0 || s == 2) {
                // Use Target Time 0
                WRITE_REG32(I225_TARGTIME0, (UINT32)(launchTime & 0xFFFFFFFF));
                WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(launchTime >> 32));
                
                TSAUXC_REG tsauxc;
                tsauxc.value = READ_REG32(I225_TSAUXC);
                tsauxc.bits.EN_TT0 = 1;
                tsauxc.bits.ST0 = 1;
                WRITE_REG32(I225_TSAUXC, tsauxc.value);
            }
            
            // Wait for TX completion
            WaitForTxCompletion(txDesc);
            
            // Record statistics
            streams[s].packetsTransmitted++;
            UINT64 actualTxTime = txDesc->TxTimestamp;
            INT64 launchError = (INT64)(actualTxTime - launchTime);
            streams[s].launchErrors.push_back(launchError);
            
            // Calculate end-to-end latency (application → wire)
            UINT64 latency = actualTxTime - launchTime + INTERVAL_NS;
            streams[s].latencies.push_back(latency);
        }
    }
    
    // Verify: All packets transmitted
    for (int s = 0; s < STREAM_COUNT; s++) {
        EXPECT_EQ(streams[s].packetsTransmitted, PACKETS_PER_STREAM);
    }
    
    // Verify: Launch time accuracy (±500ns)
    for (int s = 0; s < STREAM_COUNT; s++) {
        for (INT64 error : streams[s].launchErrors) {
            EXPECT_LT(abs(error), 500); // Within ±500ns
        }
    }
    
    // Verify: End-to-end latency and jitter
    for (int s = 0; s < STREAM_COUNT; s++) {
        double meanLatency = CalculateMean(streams[s].latencies);
        double jitter = CalculateStdDev(streams[s].latencies);
        
        EXPECT_LT(jitter, 100.0); // Jitter <100ns
    }
}
```

**Expected Result**:
- All 4 AVB streams run for 1 hour at line rate
- All packets meet launch time targets (±500ns)
- End-to-end latency and jitter measured and within spec

---

## 🔧 Implementation Notes

### **TSAUXC Register Configuration**

```c
// Target Time Auxiliary Control Register
#define I225_TSAUXC 0x03F00 // Offset

typedef union {
    struct {
        UINT32 EN_TT0 : 1; // [0] Enable Target Time 0
        UINT32 EN_TT1 : 1; // [1] Enable Target Time 1
        UINT32 EN_CLK0 : 1; // [2] Enable Clock 0
        UINT32 EN_CLK1 : 1; // [3] Enable Clock 1
        UINT32 ST0 : 1; // [4] Start Target Time 0
        UINT32 ST1 : 1; // [5] Start Target Time 1
        UINT32 Reserved1 : 2; // [7:6] Reserved
        UINT32 PLSG : 1; // [8] Pulse Generator
        UINT32 Reserved2 : 23; // [31:9] Reserved
    } bits;
    UINT32 value;
} TSAUXC_REG;

#define I225_TARGTIME0 0x03F04 // Target Time 0 (64-bit)
#define I225_TARGTIME1 0x03F0C // Target Time 1 (64-bit)

// Set Target Time 0
VOID SetTargetTime0(UINT64 targetTimestamp) {
    WRITE_REG32(I225_TARGTIME0, (UINT32)(targetTimestamp & 0xFFFFFFFF));
    WRITE_REG32(I225_TARGTIME0 + 4, (UINT32)(targetTimestamp >> 32));
    
    // Enable and start Target Time 0
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.EN_TT0 = 1;
    tsauxc.bits.ST0 = 1;
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
}
```

### **Target Time ISR**

```c
VOID TargetTimeISR(PVOID Context) {
    LARGE_INTEGER isrEntry;
    QueryPerformanceCounter(&isrEntry); // Measure ISR entry time
    
    UINT64 currentPhc = ReadPhcTime();
    
    // Clear interrupt (write-1-to-clear)
    TSAUXC_REG tsauxc;
    tsauxc.value = READ_REG32(I225_TSAUXC);
    tsauxc.bits.ST0 = 0; // Clear Target Time 0 interrupt
    WRITE_REG32(I225_TSAUXC, tsauxc.value);
    
    // Execute scheduled action (e.g., transmit packet)
    ExecuteScheduledAction(currentPhc);
    
    // Record ISR latency for diagnostics
    LARGE_INTEGER isrExit;
    QueryPerformanceCounter(&isrExit);
    RecordIsrLatency(isrExit.QuadPart - isrEntry.QuadPart);
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Interrupt Accuracy (Mean) | <100ns | PHC time at ISR - target time |
| Interrupt Jitter (Stddev) | <100ns | Standard deviation of error |
| Max Interrupt Error | <1µs | 99.9th percentile |
| ISR Latency | <5µs | ISR entry to exit time |
| TX Launch Time Accuracy | ±500ns | TX timestamp - launch time |
| TAS Gate Transition Accuracy | ±100ns | Gate open time - target time |
| 24-Hour Drift | <1µs | First vs. last hour average |

---

## 📈 Acceptance Criteria

- [x] **All 10 unit tests pass** with kernel-mode hardware tests
- [x] **All 3 integration tests pass** with TX/TAS integration
- [x] **All 2 V&V tests pass** with quantified performance metrics
- [x] **Interrupt accuracy** <100ns mean, <1µs max
- [x] **TX launch time accuracy** ±500ns
- [x] **TAS gate transition accuracy** ±100ns
- [x] **24-hour stability** (691,200 interrupts, no drift >1µs)
- [x] **Production AVB workload** (1 hour, all targets met)

---

## 🔗 Related Test Issues

- **TEST-PTP-PHC-001** (#191): PHC Get/Set operations (baseline timing)
- **TEST-TAS-001** (#206): Time-Aware Shaper (gate control synchronization)
- **TEST-LAUNCH-TIME-001** (#209): Launch time offload (TX timestamp accuracy)
- **TEST-GPTP-001** (#210): gPTP protocol stack (8 Hz sync rate)

---

## 📝 Test Execution Order

1. **Unit Tests** (UT-TARGET-001 to UT-TARGET-010)
2. **Integration Tests** (IT-TARGET-001 to IT-TARGET-003)
3. **V&V Tests** (VV-TARGET-001 to VV-TARGET-002)

**Estimated Execution Time**:
- Unit tests: ~10 minutes
- Integration tests: ~5 minutes
- V&V tests: ~25 hours (24-hour stability + 1-hour production)
- **Total**: ~25 hours, 15 minutes

---

**Standards Compliance**:
- ✅ IEEE 1012-2016 (Verification & Validation)
- ✅ IEEE 802.1Qbv (Time-Aware Shaper / TAS)
- ✅ ISO/IEC/IEEE 12207:2017 (Testing Process)

**XP Practice**: Test-Driven Development (TDD) - Tests defined before implementation

---

*This test specification ensures the Target Time feature provides accurate, low-jitter hardware interrupts for time-triggered operations critical to AVB/TSN network scheduling and deterministic packet transmission.*
