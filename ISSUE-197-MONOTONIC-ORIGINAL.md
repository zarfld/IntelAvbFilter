# Issue #197 - Original Test Specification

**Test ID**: TEST-PHC-MONOTONIC-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)  
**Status**: Draft - Restored from corruption 2025-12-30

---

## 🧪 Test Case Specification

**Test ID**: TEST-PHC-MONOTONIC-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)

---

## 🔗 Traceability

**Traces to**: #47 (REQ-NF-REL-PHC-001: PHC Monotonicity Guarantee)  
**Verifies**: #47 (REQ-NF-REL-PHC-001: PHC Monotonicity Guarantee)  

**Related Requirements**:  
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)  
- #38 (REQ-F-IOCTL-PHC-003: PHC Time Offset Adjustment IOCTL)  
- #70 (REQ-F-IOCTL-PHC-002: PHC Time Set IOCTL)  
- #41 (REQ-F-PTP-003: TAI Epoch Initialization and Conversion)  

**Related Quality Scenarios**:  
- #106 (QA-SC-PERF-001: PTP Clock Read Latency Under Peak Load)

---

## 🎯 Test Objective

Validates that PTP hardware clock timestamps are strictly monotonically increasing under all conditions. Verifies no backward time jumps after offset adjustments, frequency changes, epoch resets, hardware errors, driver reload, or concurrent access. Ensures time never moves backward, which would violate causality and break distributed systems synchronization.

**Monotonicity Invariant**: For any two consecutive PHC reads: `timestamp[i+1] ≥ timestamp[i]` (strictly non-decreasing). Exception: Intentional backward adjustments (offset corrections) are allowed but must be atomic and documented.

---

## 📊 Level 1: Unit Tests (10 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_phc_monotonicity.cpp`  
**Mock Dependencies**: MockPhcManager, MockHardwareAccessLayer

### Scenario 1.1: Sequential Reads Never Decrease

**Given** PHC timestamp = 1,000,000,000 ns  
**When** 100 consecutive reads performed  
**Then** each read ≥ previous read (monotonically increasing)  
**And** no backward jumps detected

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, SequentialReadsNeverDecrease) {
    UINT64 previousTime = 0;
    const int NUM_READS = 100;
    
    // Simulate monotonically increasing hardware
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .Times(NUM_READS)
        .WillRepeatedly(Invoke([&](UINT64* timestamp) {
            *timestamp = previousTime;
            previousTime += 1000; // +1 µs per read
            return STATUS_SUCCESS;
        }));
    
    UINT64 lastRead = 0;
    for (int i = 0; i < NUM_READS; ++i) {
        UINT64 currentRead = 0;
        NTSTATUS status = phcManager->ReadTimestamp(&currentRead);
        
        EXPECT_EQ(status, STATUS_SUCCESS);
        if (i > 0) {
            EXPECT_GE(currentRead, lastRead)
                << "Backward jump detected: "
                << lastRead << " → " << currentRead;
        }
        lastRead = currentRead;
    }
}
```

### Scenario 1.2: Monotonicity After Positive Offset Adjustment

**Given** PHC timestamp = 1,000,000,000 ns  
**When** positive offset adjustment +10 µs applied  
**Then** new timestamp = 1,000,010,000 ns (increased)  
**And** subsequent reads ≥ 1,000,010,000 ns

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, MonotonicityAfterPositiveOffset) {
    UINT64 baseTime = 1000000000ULL;
    
    // Read before adjustment
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 beforeAdjustment = 0;
    phcManager->ReadTimestamp(&beforeAdjustment);
    EXPECT_EQ(beforeAdjustment, baseTime);
    
    // Apply positive offset (+10 µs)
    INT64 offset = +10000LL;
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime),
                       Return(STATUS_SUCCESS)));
    EXPECT_CALL(*mockPhc, WriteTimestamp(baseTime + offset))
        .WillOnce(Return(STATUS_SUCCESS));
    
    phcManager->ApplyOffsetAdjustment(offset);
    
    // Read after adjustment
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime + offset),
                       Return(STATUS_SUCCESS)));
    
    UINT64 afterAdjustment = 0;
    phcManager->ReadTimestamp(&afterAdjustment);
    
    // Verify monotonicity: after ≥ before
    EXPECT_GE(afterAdjustment, beforeAdjustment);
    EXPECT_EQ(afterAdjustment, baseTime + offset);
}
```

### Scenario 1.3: Monotonicity Protection on Negative Offset (Underflow)

**Given** PHC timestamp = 1,000 ns  
**When** negative offset adjustment -2,000 ns attempted (would cause underflow)  
**Then** adjustment rejected (STATUS_INVALID_PARAMETER)  
**And** PHC timestamp unchanged (still 1,000 ns)  
**And** monotonicity preserved

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, MonotonicityProtectionOnUnderflow) {
    UINT64 currentTime = 1000ULL; // 1 µs
    INT64 negativeOffset = -2000LL; // Would go negative
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                       Return(STATUS_SUCCESS)));
    
    // WriteTimestamp should NOT be called (underflow prevented)
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0);
    
    NTSTATUS status = phcManager->ApplyOffsetAdjustment(negativeOffset);
    
    EXPECT_EQ(status, STATUS_INVALID_PARAMETER);
    
    // Verify PHC unchanged
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 afterAttempt = 0;
    phcManager->ReadTimestamp(&afterAttempt);
    EXPECT_EQ(afterAttempt, currentTime); // Unchanged
}
```

### Scenario 1.4: Monotonicity After Valid Negative Offset

**Given** PHC timestamp = 10,000,000 ns  
**When** negative offset adjustment -5,000 ns applied  
**Then** new timestamp = 9,995,000 ns  
**But** subsequent reads continue from 9,995,000 ns (no backward jump relative to adjusted time)

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, MonotonicityAfterValidNegativeOffset) {
    UINT64 baseTime = 10000000ULL;
    INT64 negativeOffset = -5000LL;
    UINT64 adjustedTime = baseTime + negativeOffset;
    
    // Read before adjustment
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 beforeAdjustment = 0;
    phcManager->ReadTimestamp(&beforeAdjustment);
    
    // Apply negative offset
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime),
                       Return(STATUS_SUCCESS)));
    EXPECT_CALL(*mockPhc, WriteTimestamp(adjustedTime))
        .WillOnce(Return(STATUS_SUCCESS));
    
    NTSTATUS status = phcManager->ApplyOffsetAdjustment(negativeOffset);
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Read after adjustment
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(adjustedTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 afterAdjustment = 0;
    phcManager->ReadTimestamp(&afterAdjustment);
    
    EXPECT_EQ(afterAdjustment, adjustedTime);
    
    // Note: This DOES create a backward jump relative to wall-clock time,
    // but it's intentional for offset correction. The key is that
    // subsequent reads never go backward from the adjusted time.
}
```

### Scenario 1.5: Monotonicity After Epoch Reset

**Given** PHC timestamp = 5,000,000,000 ns (5 seconds)  
**When** epoch reset to 0 (IOCTL_AVB_PHC_SET with timestamp = 0)  
**Then** new timestamp = 0 ns  
**And** subsequent reads start from 0 and increase monotonically

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, MonotonicityAfterEpochReset) {
    UINT64 currentTime = 5000000000ULL; // 5 seconds
    
    // Read before reset
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 beforeReset = 0;
    phcManager->ReadTimestamp(&beforeReset);
    EXPECT_EQ(beforeReset, currentTime);
    
    // Reset to epoch (0)
    EXPECT_CALL(*mockPhc, WriteTimestamp(0ULL))
        .WillOnce(Return(STATUS_SUCCESS));
    
    phcManager->ResetEpoch();
    
    // Read after reset (should be near 0)
    UINT64 afterReset = 0;
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(100ULL), // Small increment from reset
                       Return(STATUS_SUCCESS)));
    
    phcManager->ReadTimestamp(&afterReset);
    EXPECT_LT(afterReset, 10000ULL); // <10 µs (near epoch)
    
    // Verify subsequent reads are monotonic from new base
    UINT64 lastRead = afterReset;
    for (int i = 0; i < 10; ++i) {
        UINT64 currentRead = lastRead + 1000; // +1 µs
        EXPECT_CALL(*mockPhc, ReadTimestamp(_))
            .WillOnce(DoAll(SetArgPointee<0>(currentRead),
                           Return(STATUS_SUCCESS)));
        
        UINT64 timestamp = 0;
        phcManager->ReadTimestamp(&timestamp);
        EXPECT_GE(timestamp, lastRead);
        lastRead = timestamp;
    }
}
```

### Scenario 1.6: Monotonicity During Frequency Adjustment

**Given** PHC running at normal frequency  
**When** frequency adjusted +100 ppb (faster clock)  
**Then** timestamp continues increasing (faster rate)  
**And** no backward jump at adjustment moment

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, MonotonicityDuringFrequencyAdjustment) {
    UINT64 baseTime = 1000000000ULL;
    
    // Read before frequency adjustment
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 beforeFreqChange = 0;
    phcManager->ReadTimestamp(&beforeFreqChange);
    
    // Apply frequency adjustment (+100 ppb)
    UINT32 newIncval = 0x60000064; // Adjusted INCVAL
    EXPECT_CALL(*mockHal, WriteRegister(INCVAL_REGISTER, newIncval))
        .WillOnce(Return(STATUS_SUCCESS));
    
    phcManager->SetFrequencyAdjustment(+100);
    
    // Read immediately after frequency change
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(baseTime + 1000), // +1 µs
                       Return(STATUS_SUCCESS)));
    
    UINT64 afterFreqChange = 0;
    phcManager->ReadTimestamp(&afterFreqChange);
    
    // Verify no backward jump
    EXPECT_GE(afterFreqChange, beforeFreqChange);
}
```

### Scenario 1.7: Monotonicity After Hardware Read Error Recovery

**Given** hardware read returns error (MMIO failure)  
**When** driver retries read  
**Then** retry succeeds with valid timestamp  
**And** timestamp ≥ last successful read (monotonicity preserved)

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, MonotonicityAfterHardwareErrorRecovery) {
    UINT64 lastGoodTime = 1000000000ULL;
    
    // First read succeeds
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(lastGoodTime),
                       Return(STATUS_SUCCESS)));
    
    UINT64 beforeError = 0;
    phcManager->ReadTimestamp(&beforeError);
    
    // Second read fails (hardware error)
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(Return(STATUS_DEVICE_HARDWARE_ERROR));
    
    UINT64 duringError = 0;
    NTSTATUS status = phcManager->ReadTimestamp(&duringError);
    EXPECT_EQ(status, STATUS_DEVICE_HARDWARE_ERROR);
    
    // Third read succeeds (recovery)
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(lastGoodTime + 10000), // +10 µs
                       Return(STATUS_SUCCESS)));
    
    UINT64 afterRecovery = 0;
    status = phcManager->ReadTimestamp(&afterRecovery);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_GE(afterRecovery, beforeError); // Monotonicity maintained
}
```

### Scenario 1.8: No Backward Jump on Driver Reload

**Given** PHC timestamp = 10,000,000,000 ns before driver unload  
**When** driver unloaded and reloaded  
**Then** PHC timestamp persists (hardware state)  
**Or** if re-initialized, starts from epoch (0) with clear discontinuity marker

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, NoBackwardJumpOnDriverReload) {
    UINT64 timeBeforeUnload = 10000000000ULL;
    
    // Read before driver unload
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(timeBeforeUnload),
                       Return(STATUS_SUCCESS)));
    
    UINT64 beforeUnload = 0;
    phcManager->ReadTimestamp(&beforeUnload);
    
    // Simulate driver unload
    phcManager.reset();
    
    // Simulate driver reload (hardware state persists)
    phcManager = std::make_unique<PhcManager>(mockAdapter.get());
    
    // Read after reload (hardware register unchanged)
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(timeBeforeUnload + 50000), // +50 µs
                       Return(STATUS_SUCCESS)));
    
    UINT64 afterReload = 0;
    phcManager->Initialize();
    phcManager->ReadTimestamp(&afterReload);
    
    // Verify no backward jump (hardware clock continued running)
    EXPECT_GE(afterReload, beforeUnload);
}
```

### Scenario 1.9: Concurrent Read Monotonicity (Race Condition)

**Given** 10 threads reading PHC simultaneously  
**When** all threads call PHC_ReadTimestamp() concurrently  
**Then** each thread's sequential reads are monotonic  
**And** global ordering may vary but no thread sees backward jump

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, ConcurrentReadMonotonicity) {
    std::atomic<UINT64> simulatedTime(1000000000ULL);
    const int NUM_THREADS = 10;
    const int READS_PER_THREAD = 100;
    
    // Mock hardware returns atomically incrementing time
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillRepeatedly(Invoke([&](UINT64* timestamp) {
            *timestamp = simulatedTime.fetch_add(100); // +100 ns per read
            return STATUS_SUCCESS;
        }));
    
    std::vector<std::thread> threads;
    std::atomic<int> violationCount(0);
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            UINT64 lastRead = 0;
            for (int i = 0; i < READS_PER_THREAD; ++i) {
                UINT64 currentRead = 0;
                phcManager->ReadTimestamp(&currentRead);
                
                if (i > 0 && currentRead < lastRead) {
                    violationCount++;
                }
                lastRead = currentRead;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(violationCount, 0) << "Detected " << violationCount
                                 << " monotonicity violations";
}
```

### Scenario 1.10: Stress Test - 1 Million Reads, No Backward Jumps

**Given** PHC initialized  
**When** 1,000,000 consecutive reads performed  
**Then** 0 backward jumps detected  
**And** all reads monotonically increasing

**Code Example**:
```cpp
TEST_F(PhcMonotonicityTest, StressTestOneMillionReads) {
    const int NUM_READS = 1000000;
    UINT64 simulatedTime = 0;
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .Times(NUM_READS)
        .WillRepeatedly(Invoke([&](UINT64* timestamp) {
            *timestamp = simulatedTime;
            simulatedTime += 10; // +10 ns per read
            return STATUS_SUCCESS;
        }));
    
    UINT64 lastRead = 0;
    int backwardJumps = 0;
    
    for (int i = 0; i < NUM_READS; ++i) {
        UINT64 currentRead = 0;
        phcManager->ReadTimestamp(&currentRead);
        
        if (i > 0 && currentRead < lastRead) {
            backwardJumps++;
        }
        lastRead = currentRead;
    }
    
    EXPECT_EQ(backwardJumps, 0) << "Detected " << backwardJumps
                                 << " backward jumps in " << NUM_READS
                                 << " reads";
}
```

**Expected Results (Level 1)**:
- ✅ All 10 unit test cases pass
- ✅ Code coverage >90% for monotonicity validation logic
- ✅ 100 sequential reads: 0 backward jumps
- ✅ Underflow protection prevents negative timestamps
- ✅ 1,000,000 stress test reads: 0 backward jumps
- ✅ Concurrent access: 0 monotonicity violations

---

## 🔗 Level 2: Integration Tests (4 test cases)

**Test Framework**: Google Test + Mock NDIS  
**Test File**: `05-implementation/tests/integration/test_monotonicity_integration.cpp`  
**Mock Dependencies**: MockNdisFilter, MockAdapterContext, Real PHC Manager

### Scenario 2.1: User-Mode IOCTL Monotonicity Test

**Given** user-mode application calling IOCTL_AVB_PHC_QUERY repeatedly  
**When** 1000 consecutive IOCTL calls issued  
**Then** all returned timestamps monotonically increasing  
**And** 0 backward jumps

**Code Example**:
```cpp
TEST_F(MonotonicityIntegrationTest, UserModeIoctlMonotonicity) {
    HANDLE deviceHandle = OpenMockDevice(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    const int NUM_QUERIES = 1000;
    UINT64 lastTimestamp = 0;
    int backwardJumps = 0;
    
    for (int i = 0; i < NUM_QUERIES; ++i) {
        UINT64 timestamp = 0;
        DWORD bytesReturned = 0;
        
        BOOL success = DeviceIoControl(
            deviceHandle,
            IOCTL_AVB_PHC_QUERY,
            NULL, 0,
            &timestamp, sizeof(UINT64),
            &bytesReturned,
            NULL);
        
        EXPECT_TRUE(success);
        
        if (i > 0 && timestamp < lastTimestamp) {
            backwardJumps++;
        }
        lastTimestamp = timestamp;
    }
    
    EXPECT_EQ(backwardJumps, 0);
    
    CloseHandle(deviceHandle);
}
```

### Scenario 2.2: Monotonicity Across gPTP Synchronization

**Given** slave adapter synchronizing to master via gPTP  
**When** slave applies offset corrections over 100 sync intervals  
**Then** slave timestamps remain monotonic (no backward jumps)  
**And** sync converges to <1 µs error

**Code Example**:
```cpp
TEST_F(MonotonicityIntegrationTest, MonotonicityAcrossGptpSync) {
    MockAdapterContext masterAdapter(DEVICE_I226);
    MockAdapterContext slaveAdapter(DEVICE_I226);
    
    // Master at 10 seconds
    masterAdapter.GetPhcManager()->WriteTimestamp(10000000000ULL);
    
    // Slave at epoch (0)
    slaveAdapter.GetPhcManager()->InitializeEpoch();
    
    UINT64 lastSlaveTime = 0;
    int backwardJumps = 0;
    
    for (int i = 0; i < 100; ++i) {
        // Read master time
        UINT64 masterTime = 0;
        masterAdapter.GetPhcManager()->ReadTimestamp(&masterTime);
        
        // Read slave time
        UINT64 slaveTime = 0;
        slaveAdapter.GetPhcManager()->ReadTimestamp(&slaveTime);
        
        // Check monotonicity
        if (i > 0 && slaveTime < lastSlaveTime) {
            backwardJumps++;
        }
        lastSlaveTime = slaveTime;
        
        // Calculate offset and apply correction
        INT64 offset = (INT64)masterTime - (INT64)slaveTime;
        INT64 correction = offset / 2; // P-controller
        
        slaveAdapter.GetPhcManager()->ApplyOffsetAdjustment(correction);
    }
    
    EXPECT_EQ(backwardJumps, 0) << "Detected backward jumps during gPTP sync";
}
```

### Scenario 2.3: Multi-Adapter Monotonicity (4 Adapters)

**Given** 4 Intel I226 adapters  
**When** all 4 queried simultaneously (1000 iterations)  
**Then** each adapter's timestamps are monotonic independently  
**And** no cross-adapter interference

**Code Example**:
```cpp
TEST_F(MonotonicityIntegrationTest, MultiAdapterMonotonicity) {
    const int NUM_ADAPTERS = 4;
    const int NUM_ITERATIONS = 1000;
    
    std::vector<MockAdapterContext> adapters;
    for (int i = 0; i < NUM_ADAPTERS; ++i) {
        adapters.emplace_back(DEVICE_I226);
        adapters[i].GetPhcManager()->Initialize();
    }
    
    std::vector<UINT64> lastTimestamps(NUM_ADAPTERS, 0);
    std::vector<int> backwardJumps(NUM_ADAPTERS, 0);
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (int a = 0; a < NUM_ADAPTERS; ++a) {
            UINT64 timestamp = 0;
            adapters[a].GetPhcManager()->ReadTimestamp(&timestamp);
            
            if (iter > 0 && timestamp < lastTimestamps[a]) {
                backwardJumps[a]++;
            }
            lastTimestamps[a] = timestamp;
        }
    }
    
    for (int a = 0; a < NUM_ADAPTERS; ++a) {
        EXPECT_EQ(backwardJumps[a], 0)
            << "Adapter " << a << " had backward jumps";
    }
}
```

### Scenario 2.4: Monotonicity During System Suspend/Resume

**Given** PHC running normally before system suspend  
**When** system enters S3 sleep and resumes  
**Then** PHC timestamp after resume ≥ timestamp before suspend  
**Or** if reset to epoch, discontinuity is clearly marked

**Code Example**:
```cpp
TEST_F(MonotonicityIntegrationTest, MonotonicityAcrossSuspendResume) {
    // Read before suspend
    UINT64 beforeSuspend = 0;
    phcManager->ReadTimestamp(&beforeSuspend);
    
    // Simulate system suspend (driver receives DEVICE_POWER_STATE)
    mockAdapter->SimulatePowerTransition(PowerDeviceD3);
    
    // Simulate wake (some time passes in hardware)
    mockAdapter->SimulatePowerTransition(PowerDeviceD0);
    phcManager->OnResume();
    
    // Read after resume
    UINT64 afterResume = 0;
    phcManager->ReadTimestamp(&afterResume);
    
    // Verify monotonicity (hardware clock may have continued or reset)
    if (phcManager->IsEpochPreservedAcrossSuspend()) {
        EXPECT_GE(afterResume, beforeSuspend);
    } else {
        // If reset to epoch, verify it's near 0
        EXPECT_LT(afterResume, 100000ULL); // <100 µs
    }
}
```

**Expected Results (Level 2)**:
- ✅ User-mode IOCTLs: 1000 queries, 0 backward jumps
- ✅ gPTP sync: 100 intervals, 0 backward jumps, <1 µs convergence
- ✅ Multi-adapter: All 4 adapters monotonic independently
- ✅ Suspend/resume: Monotonicity preserved or clearly reset to epoch

---

## 🎯 Level 3: V&V Tests (3 test cases)

**Test Framework**: User-mode harness  
**Test File**: `07-verification-validation/tests/monotonicity_vv_tests.cpp`  
**Test Environment**: Intel I226 adapter, Windows 10/11

### Scenario 3.1: Long-Term Monotonicity Verification (24 Hours)

**Given** Intel I226 adapter running continuously  
**When** PHC queried every 1 second for 24 hours  
**Then** all 86,400 samples monotonically increasing  
**And** 0 backward jumps detected

**Code Example**:
```cpp
TEST_F(MonotonicityVVTest, LongTermMonotonicityVerification) {
    HANDLE deviceHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    const int SAMPLES = 86400; // 24 hours (1 sample/second)
    UINT64 lastTimestamp = 0;
    int backwardJumps = 0;
    
    for (int i = 0; i < SAMPLES; ++i) {
        UINT64 timestamp = 0;
        DWORD bytesReturned = 0;
        
        DeviceIoControl(
            deviceHandle,
            IOCTL_AVB_PHC_QUERY,
            NULL, 0,
            &timestamp, sizeof(UINT64),
            &bytesReturned,
            NULL);
        
        if (i > 0 && timestamp < lastTimestamp) {
            backwardJumps++;
            // Log violation for analysis
            printf("Backward jump at sample %d: %llu → %llu\n",
                   i, lastTimestamp, timestamp);
        }
        lastTimestamp = timestamp;
        
        Sleep(1000); // 1 second
    }
    
    EXPECT_EQ(backwardJumps, 0) << "Detected " << backwardJumps
                                 << " backward jumps in 24 hours";
    
    CloseHandle(deviceHandle);
}
```

### Scenario 3.2: High-Frequency Sampling Monotonicity (1 MHz Query Rate)

**Given** Intel I226 adapter  
**When** PHC queried at 1 MHz rate for 10 seconds  
**Then** all 10,000,000 samples monotonically increasing  
**And** 0 backward jumps

**Code Example**:
```cpp
TEST_F(MonotonicityVVTest, HighFrequencySamplingMonotonicity) {
    HANDLE deviceHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    const int SAMPLES = 10000000; // 10 million
    UINT64 lastTimestamp = 0;
    int backwardJumps = 0;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < SAMPLES; ++i) {
        UINT64 timestamp = 0;
        DWORD bytesReturned = 0;
        
        DeviceIoControl(
            deviceHandle,
            IOCTL_AVB_PHC_QUERY,
            NULL, 0,
            &timestamp, sizeof(UINT64),
            &bytesReturned,
            NULL);
        
        if (i > 0 && timestamp < lastTimestamp) {
            backwardJumps++;
        }
        lastTimestamp = timestamp;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        endTime - startTime).count();
    
    EXPECT_EQ(backwardJumps, 0);
    
    // Verify achieved high query rate
    double queryRate = (double)SAMPLES / duration;
    printf("Achieved query rate: %.2f queries/sec\n", queryRate);
    
    CloseHandle(deviceHandle);
}
```

### Scenario 3.3: Monotonicity Under Production Load (Real gPTP Sync)

**Given** two Intel I226 adapters (master + slave)  
**And** gPTP daemon running (real 802.1AS implementation)  
**When** gPTP syncs for 1 hour  
**Then** slave PHC timestamps remain monotonic throughout sync  
**And** 0 backward jumps detected on slave  
**And** final sync error <1 µs

**Code Example**:
```cpp
TEST_F(MonotonicityVVTest, MonotonicityUnderProductionLoad) {
    // This test requires real gPTP daemon (e.g., gptp from Intel AVB)
    // Start gPTP master on adapter 0
    // Start gPTP slave on adapter 1
    
    HANDLE slaveHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter1");
    ASSERT_NE(slaveHandle, INVALID_HANDLE_VALUE);
    
    const int DURATION_SECONDS = 3600; // 1 hour
    const int SAMPLE_INTERVAL_MS = 100; // 100 ms
    const int NUM_SAMPLES = DURATION_SECONDS * 10;
    
    UINT64 lastTimestamp = 0;
    int backwardJumps = 0;
    
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        UINT64 timestamp = 0;
        DWORD bytesReturned = 0;
        
        DeviceIoControl(
            slaveHandle,
            IOCTL_AVB_PHC_QUERY,
            NULL, 0,
            &timestamp, sizeof(UINT64),
            &bytesReturned,
            NULL);
        
        if (i > 0 && timestamp < lastTimestamp) {
            backwardJumps++;
        }
        lastTimestamp = timestamp;
        
        Sleep(SAMPLE_INTERVAL_MS);
    }
    
    EXPECT_EQ(backwardJumps, 0)
        << "Detected backward jumps during gPTP sync";
    
    CloseHandle(slaveHandle);
}
```

**Expected Results (Level 3)**:
- ✅ 24-hour test: 86,400 samples, 0 backward jumps
- ✅ High-frequency test: 10,000,000 samples, 0 backward jumps
- ✅ Production gPTP: 1 hour sync, 0 backward jumps, <1 µs final error

---

## 📋 Test Data

**Monotonicity Invariant**:
```cpp
// For any two consecutive PHC reads:
// timestamp[i+1] >= timestamp[i] (strictly non-decreasing)
// Exception: Intentional backward adjustment (offset correction)
// is allowed but must be atomic and documented
```

**Underflow Protection**:
```cpp
// Reject offset adjustments that would cause negative time:
if ((INT64)currentTime + offsetNs < 0) {
    return STATUS_INVALID_PARAMETER; // Preserve monotonicity
}
```

**Test Scenarios**:
```cpp
// Sequential reads: 100, 1000, 1,000,000 iterations
// Concurrent reads: 10 threads × 100 reads each
// Long-term: 86,400 reads over 24 hours
// High-frequency: 10,000,000 reads in 10 seconds
```

---

## 🛠️ Implementation Notes

**Test Framework**: Google Test 1.14.0  
**Mock Framework**: Google Mock  
**Coverage Tool**: OpenCppCoverage  
**CI Integration**: GitHub Actions (every commit)

**Mock Dependencies**:
- MockPhcManager (PHC read/write, offset adjustment)
- MockHardwareAccessLayer (SYSTIML/SYSTIMH registers)
- MockNdisFilter (IOCTL dispatch)

**Build Command**:
```bash
msbuild 05-implementation/tests/unit/unit_tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

**Run Command**:
```bash
.\x64\Release\unit_tests.exe --gtest_filter=PhcMonotonicityTest.*
```

---

## 🧹 Postconditions / Cleanup

- Reset PHC to known state after each test
- Release mock adapter contexts
- Generate monotonicity violation report (if any)
- Verify no memory leaks
- Generate code coverage report (target >90%)

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement(s) (#47, #2, #38, #70, #41)
- [x] Test has clear pass/fail criteria (0 backward jumps)
- [x] Test is deterministic (mocked hardware with controlled increments)
- [x] Test is automated (Google Test, CI/CD)
- [x] Test data documented (sequential, concurrent, stress scenarios)
- [x] Expected results quantified (0 backward jumps in all scenarios)
- [x] Traceability links complete (#47, #2, #38, #70, #41, #106)
- [x] Underflow protection validated (negative offset rejection)
- [x] Concurrent access tested (10 threads, 0 violations)
- [x] Stress test included (1,000,000 reads, 0 backward jumps)
- [x] Long-term stability tested (24 hours, 0 backward jumps)
- [x] Production load tested (gPTP sync, 0 backward jumps)

---

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Restored**: 2025-12-30 (from corruption)  
**Owner**: QA Team + PHC Team  
**Blocked By**: #47 (REQ-NF-REL-PHC-001 implementation)
