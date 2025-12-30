# Issue #198 - Original Test Specification

**Test ID**: TEST-IOCTL-XSTAMP-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)  
**Status**: Draft - Restored from corruption 2025-12-30

---

## 🧪 Test Case Specification

**Test ID**: TEST-IOCTL-XSTAMP-001  
**Test Name**: Cross-Timestamp IOCTL Verification  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical)  
**Phase**: 07-verification-validation

---

## 🔗 Traceability

**Traces to**: #48 (REQ-F-IOCTL-PHC-004: Cross-Timestamp IOCTL)  
**Verifies**: #48 (REQ-F-IOCTL-PHC-004)  

**Related Requirements**:  
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)  
- #58 (REQ-NF-PERF-PHC-001: PHC Read Latency <500ns)  

**Related Quality Scenarios**:  
- #110 (QA-SC-PERF-001: PHC Clock Read Latency Under Peak Load)  
- #104 (QA-SC-USABILITY-002: Clock Correlation Use Cases)

---

## 📋 Test Objective

Validates the **IOCTL_AVB_PHC_CROSSTIMESTAMP** interface for correlating PHC (Precision Hardware Clock) timestamps with system time (QueryPerformanceCounter). Verifies:

1. **Accurate correlation** between PHC nanosecond timestamps and system performance counter ticks
2. **Correlation accuracy** <10µs delta between PHC and system time
3. **Concurrent access** safety (multiple threads reading cross-timestamps simultaneously)
4. **Error handling** for hardware failures, NULL buffers, invalid buffer sizes
5. **Performance** requirements (low latency, high-frequency sampling)
6. **No privilege requirements** (read-only operation, no administrator access needed)

**Key Use Case**: Applications need to correlate AVB network time (PHC) with Windows system time (QPC) to synchronize audio/video playback, control systems, and distributed events across heterogeneous time bases.

---

## 🎯 Test Coverage

### **10 Unit Tests** (Google Test, Mock NDIS)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_ioctl_crosstimestamp.cpp`  
**Mock Dependencies**: MockPhcManager, MockHardwareAccessLayer, MockNdisFilter

#### UT-XSTAMP-001: Valid Cross-Timestamp Read

**Objective**: Verify successful cross-timestamp query returns correlated PHC and system time.

**Test Steps**:
1. Initialize PHC with known timestamp (e.g., 1,000,000,000 ns)
2. Call IOCTL_AVB_PHC_CROSSTIMESTAMP with valid output buffer
3. Validate output structure contains:
   - `phcTimestampNs` (UINT64, nanoseconds)
   - `systemTimestampTicks` (UINT64, QueryPerformanceCounter ticks)
   - `correlationAccuracyNs` (UINT32, estimated accuracy in nanoseconds)
4. Verify `correlationAccuracyNs` <10,000 (10µs)

**Expected Result**:
- STATUS_SUCCESS
- Valid PHC timestamp (non-zero)
- Valid system timestamp (non-zero)
- Correlation accuracy <10µs

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, ValidCrossTimestampRead) {
    CROSS_TIMESTAMP_DATA xstamp = {0};
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(1000000000ULL),
                       Return(STATUS_SUCCESS)));
    
    NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                NULL, 0, 
                                &xstamp, sizeof(xstamp));
    
    EXPECT_EQ(STATUS_SUCCESS, status);
    EXPECT_GT(xstamp.phcTimestampNs, 0);
    EXPECT_GT(xstamp.systemTimestampTicks, 0);
    EXPECT_LT(xstamp.correlationAccuracyNs, 10000); // <10µs
}
```

---

#### UT-XSTAMP-002: PHC-System Correlation Calculation

**Objective**: Verify correlation calculation converts between PHC nanoseconds and system ticks correctly.

**Test Steps**:
1. Read cross-timestamp: `xstamp1 = {phc1, sys1, accuracy1}`
2. Wait 100ms
3. Read cross-timestamp: `xstamp2 = {phc2, sys2, accuracy2}`
4. Calculate elapsed time:
   - PHC elapsed: `phcDelta = phc2 - phc1` (nanoseconds)
   - System elapsed: `sysDelta = (sys2 - sys1) * 1e9 / QPF` (nanoseconds, where QPF = QueryPerformanceFrequency)
5. Verify correlation: `|phcDelta - sysDelta| < 10,000` (10µs tolerance)

**Expected Result**:
- Both timestamps advance by ~100ms
- PHC and system deltas agree within 10µs

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, PhcSystemCorrelationCalculation) {
    CROSS_TIMESTAMP_DATA xstamp1, xstamp2;
    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    
    // First read
    IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, NULL, 0, &xstamp1, sizeof(xstamp1));
    
    Sleep(100); // 100ms
    
    // Second read
    IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, NULL, 0, &xstamp2, sizeof(xstamp2));
    
    // Calculate deltas
    INT64 phcDelta = xstamp2.phcTimestampNs - xstamp1.phcTimestampNs;
    INT64 sysDelta = (xstamp2.systemTimestampTicks - xstamp1.systemTimestampTicks) 
                     * 1000000000LL / qpf.QuadPart;
    
    // Verify correlation (within 10µs)
    EXPECT_NEAR(phcDelta, sysDelta, 10000);
}
```

---

#### UT-XSTAMP-003: NULL Output Buffer

**Objective**: Verify NULL output buffer returns STATUS_INVALID_PARAMETER.

**Test Steps**:
1. Call IOCTL_AVB_PHC_CROSSTIMESTAMP with NULL output buffer
2. Verify STATUS_INVALID_PARAMETER returned

**Expected Result**: STATUS_INVALID_PARAMETER

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, NullOutputBuffer) {
    NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                NULL, 0, 
                                NULL, 0);
    
    EXPECT_EQ(STATUS_INVALID_PARAMETER, status);
}
```

---

#### UT-XSTAMP-004: Invalid Buffer Size

**Objective**: Verify buffer size validation rejects undersized buffers.

**Test Steps**:
1. Allocate buffer smaller than sizeof(CROSS_TIMESTAMP_DATA)
2. Call IOCTL_AVB_PHC_CROSSTIMESTAMP with undersized buffer
3. Verify STATUS_BUFFER_TOO_SMALL returned

**Expected Result**: STATUS_BUFFER_TOO_SMALL

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, InvalidBufferSize) {
    BYTE smallBuffer[8]; // Too small
    
    NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                NULL, 0, 
                                smallBuffer, sizeof(smallBuffer));
    
    EXPECT_EQ(STATUS_BUFFER_TOO_SMALL, status);
}
```

---

#### UT-XSTAMP-005: Correlation Accuracy Metric

**Objective**: Verify correlation accuracy field reflects realistic hardware latency.

**Test Steps**:
1. Read 100 cross-timestamp samples
2. For each sample, verify `correlationAccuracyNs` is:
   - Non-zero (realistic accuracy estimate)
   - <10,000 ns (10µs, per specification)
   - Consistent across samples (±20% variation acceptable)

**Expected Result**:
- All accuracy values <10µs
- Mean accuracy 1-5µs (typical for modern hardware)
- Standard deviation <2µs

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, CorrelationAccuracyMetric) {
    std::vector<UINT32> accuracies;
    
    for (int i = 0; i < 100; i++) {
        CROSS_TIMESTAMP_DATA xstamp;
        IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, NULL, 0, &xstamp, sizeof(xstamp));
        
        accuracies.push_back(xstamp.correlationAccuracyNs);
        EXPECT_LT(xstamp.correlationAccuracyNs, 10000); // <10µs
    }
    
    double mean = CalculateMean(accuracies);
    EXPECT_LT(mean, 5000); // Mean <5µs
}
```

---

#### UT-XSTAMP-006: Hardware Read Failure

**Objective**: Verify graceful handling when PHC hardware read fails.

**Test Steps**:
1. Configure mock hardware to return error on PHC read
2. Call IOCTL_AVB_PHC_CROSSTIMESTAMP
3. Verify STATUS_DEVICE_HARDWARE_ERROR returned

**Expected Result**: STATUS_DEVICE_HARDWARE_ERROR

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, HardwareReadFailure) {
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(Return(STATUS_DEVICE_HARDWARE_ERROR));
    
    CROSS_TIMESTAMP_DATA xstamp;
    NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                NULL, 0, 
                                &xstamp, sizeof(xstamp));
    
    EXPECT_EQ(STATUS_DEVICE_HARDWARE_ERROR, status);
}
```

---

#### UT-XSTAMP-007: Concurrent Cross-Timestamp Reads

**Objective**: Verify thread safety for concurrent cross-timestamp queries.

**Test Steps**:
1. Spawn 10 threads
2. Each thread reads 100 cross-timestamps
3. Verify:
   - No STATUS_DEVICE_BUSY errors (serialization handled internally)
   - All timestamps monotonically increasing (per thread)
   - No data corruption (atomic reads)

**Expected Result**:
- All 1000 reads succeed
- No race conditions or corrupt data

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, ConcurrentCrossTimestampReads) {
    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([&]() {
            UINT64 lastPhc = 0;
            for (int i = 0; i < 100; i++) {
                CROSS_TIMESTAMP_DATA xstamp;
                NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                           NULL, 0, 
                                           &xstamp, sizeof(xstamp));
                
                if (status == STATUS_SUCCESS && xstamp.phcTimestampNs >= lastPhc) {
                    successCount++;
                    lastPhc = xstamp.phcTimestampNs;
                }
            }
        });
    }
    
    for (auto& th : threads) th.join();
    EXPECT_EQ(1000, successCount);
}
```

---

#### UT-XSTAMP-008: Adapter Not Initialized

**Objective**: Verify error when PHC not initialized.

**Test Steps**:
1. Unload driver (adapter not initialized)
2. Call IOCTL_AVB_PHC_CROSSTIMESTAMP
3. Verify STATUS_DEVICE_NOT_READY returned

**Expected Result**: STATUS_DEVICE_NOT_READY

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, AdapterNotInitialized) {
    UnloadDriver();
    
    CROSS_TIMESTAMP_DATA xstamp;
    NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                NULL, 0, 
                                &xstamp, sizeof(xstamp));
    
    EXPECT_EQ(STATUS_DEVICE_NOT_READY, status);
}
```

---

#### UT-XSTAMP-009: No Privilege Requirements

**Objective**: Verify cross-timestamp read does NOT require administrator privileges (read-only operation).

**Test Steps**:
1. Run test as non-administrator user
2. Call IOCTL_AVB_PHC_CROSSTIMESTAMP
3. Verify STATUS_SUCCESS (no ACCESS_DENIED)

**Expected Result**: STATUS_SUCCESS (read access allowed for all users)

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, NoPrivilegeRequirements) {
    // Run as non-admin (mock non-privileged context)
    CROSS_TIMESTAMP_DATA xstamp;
    
    NTSTATUS status = IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                NULL, 0, 
                                &xstamp, sizeof(xstamp));
    
    EXPECT_EQ(STATUS_SUCCESS, status); // No privilege check
}
```

---

#### UT-XSTAMP-010: Multiple Samples Statistical Validation

**Objective**: Verify statistical properties of cross-timestamp samples over time.

**Test Steps**:
1. Collect 1000 cross-timestamp samples over 1 second
2. Calculate statistics:
   - Mean correlation accuracy
   - Standard deviation of accuracy
   - Maximum accuracy
   - Outlier detection (samples >3σ from mean)
3. Verify:
   - Mean accuracy <5µs
   - Std dev <2µs
   - Max accuracy <10µs
   - <1% outliers

**Expected Result**:
- Statistical distribution centered around 1-5µs
- No accuracy spikes >10µs

**Code Example**:
```cpp
TEST_F(CrossTimestampIoctlTest, MultipleSamplesStatisticalValidation) {
    std::vector<UINT32> accuracies;
    
    for (int i = 0; i < 1000; i++) {
        CROSS_TIMESTAMP_DATA xstamp;
        IoControl(IOCTL_AVB_PHC_CROSSTIMESTAMP, NULL, 0, &xstamp, sizeof(xstamp));
        accuracies.push_back(xstamp.correlationAccuracyNs);
        Sleep(1); // 1ms spacing
    }
    
    EXPECT_LT(CalculateMean(accuracies), 5000); // Mean <5µs
    EXPECT_LT(CalculateStdDev(accuracies), 2000); // Std dev <2µs
    EXPECT_LT(*std::max_element(accuracies.begin(), accuracies.end()), 10000); // Max <10µs
}
```

**Expected Results (Level 1)**:
- ✅ All 10 unit test cases pass
- ✅ Code coverage >90% for cross-timestamp IOCTL logic
- ✅ Correlation accuracy <10µs (all samples)
- ✅ Mean accuracy <5µs
- ✅ Thread-safe concurrent access (1000 reads, 0 errors)

---

## 🔗 Level 2: Integration Tests (3 test cases)

**Test Framework**: Google Test + Mock NDIS + User-Mode Harness  
**Test File**: `05-implementation/tests/integration/test_crosstimestamp_integration.cpp`

### IT-XSTAMP-001: User-Mode Application End-to-End

**Objective**: Verify cross-timestamp IOCTL accessible from user-mode application via DeviceIoControl.

**Test Steps**:
1. Open device handle: `\\.\IntelAvbFilter0`
2. Prepare output buffer: `CROSS_TIMESTAMP_DATA xstamp`
3. Call DeviceIoControl with IOCTL_AVB_PHC_CROSSTIMESTAMP
4. Verify:
   - DeviceIoControl returns TRUE
   - PHC timestamp valid (>0)
   - System timestamp valid (>0)
   - Correlation accuracy <10µs

**Expected Result**:
- User-mode app successfully reads cross-timestamp
- Correlation accuracy <10µs

**Code Example**:
```cpp
TEST_F(CrossTimestampIntegrationTest, UserModeApplicationEndToEnd) {
    HANDLE hDevice = CreateFile(L"\\\\.\\IntelAvbFilter0", 
                               GENERIC_READ, 0, NULL, 
                               OPEN_EXISTING, 0, NULL);
    ASSERT_NE(INVALID_HANDLE_VALUE, hDevice);
    
    CROSS_TIMESTAMP_DATA xstamp = {0};
    DWORD bytesReturned = 0;
    
    BOOL success = DeviceIoControl(hDevice, 
                                   IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                   NULL, 0, 
                                   &xstamp, sizeof(xstamp), 
                                   &bytesReturned, NULL);
    
    EXPECT_TRUE(success);
    EXPECT_GT(xstamp.phcTimestampNs, 0);
    EXPECT_GT(xstamp.systemTimestampTicks, 0);
    EXPECT_LT(xstamp.correlationAccuracyNs, 10000); // <10µs
    
    CloseHandle(hDevice);
}
```

---

### IT-XSTAMP-002: Correlation with QueryPerformanceCounter

**Objective**: Verify PHC-system correlation matches independent QueryPerformanceCounter calls.

**Test Steps**:
1. Read cross-timestamp: `xstamp = {phcNs, sysTicks, accuracy}`
2. Immediately call QueryPerformanceCounter: `qpc1`
3. Verify correlation:
   - `sysTicks` ≈ `qpc1` (within ±10µs, accounting for call latency)
4. Convert `qpc1` to nanoseconds: `qpc1Ns = qpc1 * 1e9 / QPF`
5. Verify PHC and QPC timestamps agree: `|phcNs - qpc1Ns| < 50µs` (conservative tolerance for two separate calls)

**Expected Result**:
- Cross-timestamp system ticks match QueryPerformanceCounter
- PHC and system time correlated within 50µs

**Code Example**:
```cpp
TEST_F(CrossTimestampIntegrationTest, CorrelationWithQueryPerformanceCounter) {
    CROSS_TIMESTAMP_DATA xstamp;
    DWORD bytesReturned = 0;
    
    DeviceIoControl(hDevice, IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                   NULL, 0, &xstamp, sizeof(xstamp), &bytesReturned, NULL);
    
    LARGE_INTEGER qpc1, qpf;
    QueryPerformanceCounter(&qpc1);
    QueryPerformanceFrequency(&qpf);
    
    // Verify system timestamp correlation
    INT64 deltaTicksNs = llabs((INT64)xstamp.systemTimestampTicks - qpc1.QuadPart) 
                        * 1000000000LL / qpf.QuadPart;
    EXPECT_LT(deltaTicksNs, 10000); // <10µs between driver capture and user-mode call
    
    // Verify PHC-system correlation
    INT64 qpc1Ns = qpc1.QuadPart * 1000000000LL / qpf.QuadPart;
    INT64 phcSysDelta = llabs((INT64)xstamp.phcTimestampNs - qpc1Ns);
    EXPECT_LT(phcSysDelta, 50000); // <50µs (two separate calls)
}
```

---

### IT-XSTAMP-003: Concurrent Multi-Threaded Reads

**Objective**: Verify concurrent user-mode applications can read cross-timestamps without interference.

**Test Steps**:
1. Spawn 4 user-mode processes
2. Each process spawns 4 threads (16 threads total)
3. Each thread reads 100 cross-timestamps
4. Verify:
   - No STATUS_DEVICE_BUSY errors
   - All 1600 reads succeed
   - Timestamps monotonically increasing (per thread)

**Expected Result**:
- 1600 successful reads
- No resource contention errors

**Code Example**:
```cpp
TEST_F(CrossTimestampIntegrationTest, ConcurrentMultiThreadedReads) {
    std::atomic<int> totalSuccessfulReads{0};
    std::vector<std::thread> threads;
    
    // Per-thread workload
    auto threadFunc = [&](HANDLE hDevice) {
        for (int i = 0; i < 100; i++) {
            CROSS_TIMESTAMP_DATA xstamp;
            DWORD bytesReturned = 0;
            BOOL success = DeviceIoControl(hDevice, IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                          NULL, 0, &xstamp, sizeof(xstamp), 
                                          &bytesReturned, NULL);
            if (success) totalSuccessfulReads++;
        }
    };
    
    // Spawn 16 threads (4 processes × 4 threads simulated)
    for (int t = 0; t < 16; t++) {
        HANDLE hDevice = OpenDevice();
        threads.emplace_back(threadFunc, hDevice);
    }
    
    for (auto& th : threads) th.join();
    
    EXPECT_EQ(1600, totalSuccessfulReads);
}
```

**Expected Results (Level 2)**:
- ✅ User-mode DeviceIoControl succeeds
- ✅ Correlation with QueryPerformanceCounter within 50µs
- ✅ 1600 concurrent reads, 0 errors

---

## 🎯 Level 3: V&V Tests (2 test cases)

**Test Framework**: User-Mode Harness  
**Test File**: `07-verification-validation/tests/crosstimestamp_vv_tests.cpp`  
**Test Environment**: Intel I226 adapter, Windows 10/11

### VV-XSTAMP-001: Correlation Accuracy Over Time

**Objective**: Verify cross-timestamp correlation accuracy remains <10µs over extended operation.

**Test Steps**:
1. Read cross-timestamp samples every 100ms for 10 minutes (600 samples)
2. For each sample, verify:
   - Correlation accuracy <10µs
   - PHC and system timestamps advance consistently
3. Calculate drift: `|(phcDelta - sysDelta) / phcDelta| < 0.0001` (0.01%, equivalent to 100 PPM)

**Expected Result**:
- All 600 samples <10µs correlation accuracy
- Cumulative drift <100 PPM (10µs per 100ms)

**Code Example**:
```cpp
TEST_F(CrossTimestampVVTest, CorrelationAccuracyOverTime) {
    HANDLE hDevice = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(INVALID_HANDLE_VALUE, hDevice);
    
    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    
    CROSS_TIMESTAMP_DATA prevXstamp = {0};
    
    for (int i = 0; i < 600; i++) {
        CROSS_TIMESTAMP_DATA xstamp;
        DWORD bytesReturned = 0;
        
        DeviceIoControl(hDevice, IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                       NULL, 0, &xstamp, sizeof(xstamp), &bytesReturned, NULL);
        
        EXPECT_LT(xstamp.correlationAccuracyNs, 10000); // <10µs
        
        if (i > 0) {
            INT64 phcDelta = xstamp.phcTimestampNs - prevXstamp.phcTimestampNs;
            INT64 sysDelta = (xstamp.systemTimestampTicks - prevXstamp.systemTimestampTicks) 
                            * 1000000000LL / qpf.QuadPart;
            double drift = (double)llabs(phcDelta - sysDelta) / phcDelta;
            EXPECT_LT(drift, 0.0001); // <100 PPM
        }
        
        prevXstamp = xstamp;
        Sleep(100);
    }
    
    CloseHandle(hDevice);
}
```

---

### VV-XSTAMP-002: High-Frequency Correlation Sampling

**Objective**: Verify cross-timestamp IOCTL supports high-frequency sampling (1 kHz for 1 minute).

**Test Steps**:
1. Read cross-timestamps at 1 kHz (1000 samples/second) for 60 seconds (60,000 samples total)
2. Verify:
   - All reads complete successfully (no timeouts or errors)
   - Mean correlation accuracy <5µs
   - Max correlation accuracy <10µs
   - No performance degradation over time (first 1000 vs last 1000 samples)

**Expected Result**:
- 60,000 successful reads
- Consistent accuracy (<5µs mean) throughout test

**Code Example**:
```cpp
TEST_F(CrossTimestampVVTest, HighFrequencyCorrelationSampling) {
    HANDLE hDevice = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(INVALID_HANDLE_VALUE, hDevice);
    
    std::vector<UINT32> accuracies;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 60000; i++) {
        auto loopStart = std::chrono::high_resolution_clock::now();
        
        CROSS_TIMESTAMP_DATA xstamp;
        DWORD bytesReturned = 0;
        BOOL success = DeviceIoControl(hDevice, IOCTL_AVB_PHC_CROSSTIMESTAMP, 
                                       NULL, 0, &xstamp, sizeof(xstamp), 
                                       &bytesReturned, NULL);
        
        EXPECT_TRUE(success);
        EXPECT_LT(xstamp.correlationAccuracyNs, 10000); // <10µs
        accuracies.push_back(xstamp.correlationAccuracyNs);
        
        // Maintain 1 kHz rate (1ms period)
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - loopStart);
        if (elapsed.count() < 1000) {
            std::this_thread::sleep_for(std::chrono::microseconds(1000 - elapsed.count()));
        }
    }
    
    // Verify mean accuracy <5µs
    EXPECT_LT(CalculateMean(accuracies), 5000);
    
    // Verify no performance degradation (first 1000 vs last 1000)
    double meanFirst1000 = CalculateMean(
        std::vector<UINT32>(accuracies.begin(), accuracies.begin() + 1000));
    double meanLast1000 = CalculateMean(
        std::vector<UINT32>(accuracies.end() - 1000, accuracies.end()));
    EXPECT_NEAR(meanFirst1000, meanLast1000, 1000); // Within 1µs
    
    CloseHandle(hDevice);
}
```

**Expected Results (Level 3)**:
- ✅ 600 samples over 10 minutes, all <10µs accuracy
- ✅ Cumulative drift <100 PPM
- ✅ 60,000 high-frequency samples, mean <5µs, no degradation

---

## 🎯 Acceptance Criteria

1. **Functional Correctness**:
   - ✅ Cross-timestamp query returns valid PHC and system timestamps
   - ✅ Correlation accuracy <10µs
   - ✅ PHC and system time advance consistently (drift <100 PPM)

2. **Error Handling**:
   - ✅ NULL buffer → STATUS_INVALID_PARAMETER
   - ✅ Invalid buffer size → STATUS_BUFFER_TOO_SMALL
   - ✅ Hardware failure → STATUS_DEVICE_HARDWARE_ERROR
   - ✅ Uninitialized adapter → STATUS_DEVICE_NOT_READY

3. **Security**:
   - ✅ No privilege requirements (read-only, accessible to all users)

4. **Performance**:
   - ✅ Correlation accuracy <10µs (all samples)
   - ✅ Mean accuracy <5µs (typical)
   - ✅ Supports high-frequency sampling (1 kHz for 60 seconds)

5. **Concurrency**:
   - ✅ Thread-safe (10 threads × 100 reads = 1000 successful reads)
   - ✅ Multi-process safe (4 processes × 4 threads × 100 reads = 1600 successful reads)

6. **Traceability**:
   - ✅ All tests reference requirement #48
   - ✅ Test results logged with issue references

---

## 📊 Test Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Correlation Accuracy | <10µs (all samples) | Output field `correlationAccuracyNs` |
| Mean Accuracy | <5µs | Average of 1000 samples |
| Max Accuracy | <10µs | Max of 1000 samples |
| Correlation Drift | <100 PPM | `|(phcDelta - sysDelta) / phcDelta|` |
| High-Frequency Sampling | 1 kHz for 60s | 60,000 samples, no errors |
| Concurrent Reads | 1000 successful | 10 threads × 100 reads |
| Multi-Process Reads | 1600 successful | 4 processes × 4 threads × 100 reads |

---

## 🛠️ Test Implementation

### Data Structures

```cpp
// Cross-Timestamp Output Structure
typedef struct _CROSS_TIMESTAMP_DATA {
    UINT64 phcTimestampNs;         // PHC timestamp in nanoseconds
    UINT64 systemTimestampTicks;   // System time in QPC ticks
    UINT32 correlationAccuracyNs;  // Estimated correlation accuracy in ns
} CROSS_TIMESTAMP_DATA;

// IOCTL Code
#define IOCTL_AVB_PHC_CROSSTIMESTAMP \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x804, METHOD_BUFFERED, FILE_READ_DATA)
```

### Key Implementation Points

```cpp
// Atomic Cross-Timestamp Capture
NTSTATUS CapturePhcSystemCorrelation(CROSS_TIMESTAMP_DATA* pOutput) {
    UINT64 phcBefore, phcAfter, systemTime;
    
    // Minimize latency: PHC → System → PHC
    phcBefore = ReadPhcTimestamp();
    QueryPerformanceCounter((LARGE_INTEGER*)&systemTime);
    phcAfter = ReadPhcTimestamp();
    
    // Calculate correlation accuracy (half the PHC delta during capture)
    UINT32 accuracyNs = (UINT32)((phcAfter - phcBefore) / 2);
    
    // Output averaged PHC timestamp
    pOutput->phcTimestampNs = (phcBefore + phcAfter) / 2;
    pOutput->systemTimestampTicks = systemTime;
    pOutput->correlationAccuracyNs = accuracyNs;
    
    return STATUS_SUCCESS;
}

// No Privilege Check (Read-Only Operation)
NTSTATUS IoctlCrossTimestamp(PIRP Irp) {
    // NO CallerPrivilege check (unlike PHC Set)
    
    CROSS_TIMESTAMP_DATA* pOutput = (CROSS_TIMESTAMP_DATA*)Irp->AssociatedIrp.SystemBuffer;
    if (pOutput == NULL || outputBufferLength < sizeof(CROSS_TIMESTAMP_DATA)) {
        return STATUS_INVALID_PARAMETER;
    }
    
    return CapturePhcSystemCorrelation(pOutput);
}
```

---

## 🔍 Related Documentation

- [REQ-F-IOCTL-PHC-004](https://github.com/zarfld/IntelAvbFilter/issues/48) - Cross-Timestamp IOCTL
- [REQ-F-PTP-001](https://github.com/zarfld/IntelAvbFilter/issues/2) - PHC Get/Set
- [REQ-NF-PERF-PHC-001](https://github.com/zarfld/IntelAvbFilter/issues/58) - PHC Read Latency <500ns
- [QA-SC-PERF-001](https://github.com/zarfld/IntelAvbFilter/issues/110) - PHC Performance Scenarios
- [QA-SC-USABILITY-002](https://github.com/zarfld/IntelAvbFilter/issues/104) - Clock Correlation Scenarios

---

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Restored**: 2025-12-30 (from corruption)  
**Owner**: QA Team + PHC Team  
**Blocked By**: #48 (REQ-F-IOCTL-PHC-004 implementation)  
**Estimated Effort**: 3-4 days (10 unit + 3 integration + 2 V&V tests)
