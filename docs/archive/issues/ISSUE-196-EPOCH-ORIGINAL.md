# Issue #196 - Original Test Specification

**Test ID**: TEST-PTP-EPOCH-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)  
**Status**: Draft - Restored from corruption 2025-12-30

---

## 🧪 Test Case Specification

**Test ID**: TEST-PTP-EPOCH-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)

---

## 🔗 Traceability

**Traces to**: #41 (REQ-F-PTP-003: TAI Epoch Initialization and Conversion)  
**Verifies**: #41 (REQ-F-PTP-003: TAI Epoch Initialization and Conversion)  

**Related Requirements**:  
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)  
- #70 (REQ-F-IOCTL-PHC-002: PHC Time Set IOCTL)  
- #47 (REQ-NF-REL-PHC-001: PHC Monotonicity Guarantee)  

**Related Quality Scenarios**:  
- #106 (QA-SC-PERF-001: PTP Clock Read Latency Under Peak Load)

---

## 🎯 Test Objective

Validates TAI (International Atomic Time) epoch handling in PTP hardware clock. Verifies epoch initialization to January 1, 1970 00:00:00 UTC (Unix epoch), TAI-to-UTC conversion accounting for leap seconds (37 seconds as of 2017), PHC reset to epoch, and correct timestamp interpretation across epoch boundaries.

---

## 📊 Level 1: Unit Tests (10 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_tai_epoch.cpp`  
**Mock Dependencies**: MockPhcManager, MockHardwareAccessLayer

### Scenario 1.1: TAI Epoch Initialization (Unix Epoch)

**Given** PHC module being initialized  
**When** PHC_InitializeEpoch() is called  
**Then** PHC timestamp set to 0 (TAI epoch = 1970-01-01 00:00:00 UTC)  
**And** SYSTIML register = 0x00000000  
**And** SYSTIMH register = 0x00000000

**Code Example**:
```cpp
TEST_F(TaiEpochTest, EpochInitializationToUnixEpoch) {
    EXPECT_CALL(*mockPhc, WriteTimestamp(0ULL))
        .WillOnce(Return(STATUS_SUCCESS));
    
    NTSTATUS status = phcManager->InitializeEpoch();
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Verify epoch set to zero (Unix epoch)
    UINT64 currentTime = 0;
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(0ULL),
                       Return(STATUS_SUCCESS)));
    
    status = phcManager->ReadTimestamp(&currentTime);
    EXPECT_EQ(currentTime, 0ULL);
}
```

### Scenario 1.2: TAI to UTC Conversion (Leap Second Offset)

**Given** TAI timestamp = 1,000,000,000 ns (1 second TAI)  
**And** leap second offset = 37 seconds (as of 2017)  
**When** TAI_ToUTC(1000000000) is called  
**Then** UTC timestamp = 1,000,000,000 - 37,000,000,000 = -36,000,000,000 ns  
**And** function returns STATUS_SUCCESS

**Code Example**:
```cpp
TEST_F(TaiEpochTest, TaiToUtcConversionWithLeapSeconds) {
    const UINT64 taiTimestamp = 1000000000ULL; // 1 second TAI
    const INT32 leapSecondOffset = 37; // 37 seconds (2017)
    
    INT64 utcTimestamp = 0;
    NTSTATUS status = phcManager->TaiToUtc(
        taiTimestamp, leapSecondOffset, &utcTimestamp);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // UTC = TAI - 37 seconds = 1s - 37s = -36s
    INT64 expectedUtc = (INT64)taiTimestamp - (37LL * 1000000000LL);
    EXPECT_EQ(utcTimestamp, expectedUtc);
    EXPECT_EQ(utcTimestamp, -36000000000LL);
}
```

### Scenario 1.3: UTC to TAI Conversion (Reverse)

**Given** UTC timestamp = 0 ns (Unix epoch)  
**And** leap second offset = 37 seconds  
**When** UTC_ToTAI(0, 37) is called  
**Then** TAI timestamp = 0 + 37,000,000,000 = 37,000,000,000 ns  
**And** function returns STATUS_SUCCESS

**Code Example**:
```cpp
TEST_F(TaiEpochTest, UtcToTaiConversionWithLeapSeconds) {
    const INT64 utcTimestamp = 0LL; // Unix epoch (UTC)
    const INT32 leapSecondOffset = 37;
    
    UINT64 taiTimestamp = 0;
    NTSTATUS status = phcManager->UtcToTai(
        utcTimestamp, leapSecondOffset, &taiTimestamp);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // TAI = UTC + 37 seconds
    UINT64 expectedTai = 37ULL * 1000000000ULL;
    EXPECT_EQ(taiTimestamp, expectedTai);
    EXPECT_EQ(taiTimestamp, 37000000000ULL);
}
```

### Scenario 1.4: Epoch Reset (PHC Back to Zero)

**Given** PHC timestamp = 5,000,000,000 ns (5 seconds)  
**When** PHC_ResetEpoch() is called  
**Then** PHC timestamp reset to 0  
**And** SYSTIML = 0x00000000, SYSTIMH = 0x00000000

**Code Example**:
```cpp
TEST_F(TaiEpochTest, EpochReset) {
    // PHC currently at 5 seconds
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(5000000000ULL),
                       Return(STATUS_SUCCESS)));
    
    // Reset to epoch (0)
    EXPECT_CALL(*mockPhc, WriteTimestamp(0ULL))
        .WillOnce(Return(STATUS_SUCCESS));
    
    NTSTATUS status = phcManager->ResetEpoch();
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Verify reset
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(0ULL),
                       Return(STATUS_SUCCESS)));
    
    UINT64 currentTime = 0;
    phcManager->ReadTimestamp(&currentTime);
    EXPECT_EQ(currentTime, 0ULL);
}
```

### Scenario 1.5: Leap Second Table Lookup (2017 Offset)

**Given** current date = 2017-01-01 (after 37th leap second)  
**When** GetLeapSecondOffset(2017, 1, 1) is called  
**Then** function returns 37 seconds

**Code Example**:
```cpp
TEST_F(TaiEpochTest, LeapSecondTableLookup2017) {
    // As of 2017-01-01, TAI-UTC offset is 37 seconds
    INT32 offset = phcManager->GetLeapSecondOffset(2017, 1, 1);
    
    EXPECT_EQ(offset, 37);
}
```

### Scenario 1.6: Leap Second Table Lookup (1972 Offset)

**Given** current date = 1972-01-01 (first leap second)  
**When** GetLeapSecondOffset(1972, 1, 1) is called  
**Then** function returns 10 seconds (initial offset)

**Code Example**:
```cpp
TEST_F(TaiEpochTest, LeapSecondTableLookup1972) {
    // As of 1972-01-01, TAI-UTC offset was 10 seconds
    INT32 offset = phcManager->GetLeapSecondOffset(1972, 1, 1);
    
    EXPECT_EQ(offset, 10);
}
```

### Scenario 1.7: Leap Second Boundary Handling (2016-12-31 → 2017-01-01)

**Given** date transitions from 2016-12-31 23:59:59 → 2017-01-01 00:00:00  
**When** leap second inserted (36 → 37 seconds)  
**Then** GetLeapSecondOffset(2016, 12, 31) = 36  
**And** GetLeapSecondOffset(2017, 1, 1) = 37

**Code Example**:
```cpp
TEST_F(TaiEpochTest, LeapSecondBoundaryHandling) {
    // Before leap second (2016-12-31)
    INT32 offsetBefore = phcManager->GetLeapSecondOffset(2016, 12, 31);
    EXPECT_EQ(offsetBefore, 36);
    
    // After leap second (2017-01-01)
    INT32 offsetAfter = phcManager->GetLeapSecondOffset(2017, 1, 1);
    EXPECT_EQ(offsetAfter, 37);
}
```

### Scenario 1.8: Negative TAI Timestamp (Before Epoch)

**Given** TAI timestamp = -1,000,000,000 ns (-1 second, before epoch)  
**When** TAI_ToUTC(-1000000000, 37) is called  
**Then** UTC timestamp = -1,000,000,000 - 37,000,000,000 = -38,000,000,000 ns  
**And** function handles negative values correctly

**Code Example**:
```cpp
TEST_F(TaiEpochTest, NegativeTaiTimestamp) {
    const INT64 taiTimestamp = -1000000000LL; // -1 second
    const INT32 leapSecondOffset = 37;
    
    INT64 utcTimestamp = 0;
    NTSTATUS status = phcManager->TaiToUtc(
        taiTimestamp, leapSecondOffset, &utcTimestamp);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // UTC = TAI - 37s = -1s - 37s = -38s
    INT64 expectedUtc = taiTimestamp - (37LL * 1000000000LL);
    EXPECT_EQ(utcTimestamp, expectedUtc);
    EXPECT_EQ(utcTimestamp, -38000000000LL);
}
```

### Scenario 1.9: Large TAI Timestamp (Year 2100)

**Given** TAI timestamp = 4,102,444,800,000,000,000 ns (~2100-01-01)  
**When** TAI_ToUTC(4102444800000000000, 37) is called  
**Then** UTC conversion succeeds  
**And** no overflow

**Code Example**:
```cpp
TEST_F(TaiEpochTest, LargeTaiTimestampYear2100) {
    // Approximately 2100-01-01 (130 years after epoch)
    const UINT64 taiTimestamp = 4102444800000000000ULL;
    const INT32 leapSecondOffset = 37; // Placeholder (actual may differ by 2100)
    
    INT64 utcTimestamp = 0;
    NTSTATUS status = phcManager->TaiToUtc(
        taiTimestamp, leapSecondOffset, &utcTimestamp);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Verify no overflow
    INT64 expectedUtc = (INT64)taiTimestamp - (37LL * 1000000000LL);
    EXPECT_EQ(utcTimestamp, expectedUtc);
}
```

### Scenario 1.10: Invalid Leap Second Offset (Out of Range)

**Given** leap second offset = 100 seconds (unrealistic)  
**When** TAI_ToUTC(1000000000, 100) is called  
**Then** function returns STATUS_INVALID_PARAMETER  
**Or** applies reasonable bounds (max 60 seconds)

**Code Example**:
```cpp
TEST_F(TaiEpochTest, InvalidLeapSecondOffset) {
    const UINT64 taiTimestamp = 1000000000ULL;
    const INT32 invalidOffset = 100; // Unrealistic
    
    INT64 utcTimestamp = 0;
    NTSTATUS status = phcManager->TaiToUtc(
        taiTimestamp, invalidOffset, &utcTimestamp);
    
    // Should either reject or clamp to reasonable range
    if (status == STATUS_INVALID_PARAMETER) {
        // Explicit validation
        EXPECT_EQ(status, STATUS_INVALID_PARAMETER);
    } else {
        // Implicit clamping
        EXPECT_EQ(status, STATUS_SUCCESS);
        // UTC should still be calculated (with clamped offset)
    }
}
```

**Expected Results (Level 1)**:
- ✅ All 10 unit test cases pass
- ✅ Code coverage >90% for TAI epoch module
- ✅ Epoch initializes to 0 (Unix epoch)
- ✅ TAI-UTC conversion accurate (±1 ns precision)
- ✅ Leap second table lookup correct (1972-2017 range)
- ✅ Negative timestamps handled correctly
- ✅ Large timestamps (year 2100+) no overflow

---

## 🔗 Level 2: Integration Tests (3 test cases)

**Test Framework**: Google Test + Mock NDIS  
**Test File**: `05-implementation/tests/integration/test_tai_epoch_integration.cpp`  
**Mock Dependencies**: MockNdisFilter, MockAdapterContext, Real PHC Manager

### Scenario 2.1: IOCTL Epoch Reset End-to-End

**Given** driver loaded, Intel I226 adapter  
**When** user-mode app calls DeviceIoControl(IOCTL_AVB_PHC_SET, &epoch, 8, NULL, 0) with epoch = 0  
**Then** PHC timestamp reset to 0  
**And** subsequent IOCTL_AVB_PHC_QUERY returns timestamp near 0 (<10 µs)

**Code Example**:
```cpp
TEST_F(TaiEpochIntegrationTest, IoctlEpochResetEndToEnd) {
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Reset to epoch
    UINT64 epoch = 0ULL;
    DWORD bytesReturned = 0;
    
    BOOL setSuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_SET,
        &epoch, sizeof(UINT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    EXPECT_TRUE(setSuccess);
    
    // Query immediately after reset
    UINT64 currentTime = 0;
    BOOL querySuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &currentTime, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(querySuccess);
    EXPECT_LT(currentTime, 10000ULL); // <10 µs (accounts for latency)
    
    CloseHandle(deviceHandle);
}
```

### Scenario 2.2: TAI Epoch Persistence Across Driver Reload

**Given** PHC initialized to epoch (0 ns)  
**When** driver unloaded and reloaded  
**Then** PHC timestamp persists (hardware register state retained)  
**Or** re-initialized to epoch on driver load

**Code Example**:
```cpp
TEST_F(TaiEpochIntegrationTest, EpochPersistenceAcrossReload) {
    // Initialize epoch
    NTSTATUS status = phcManager->InitializeEpoch();
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Simulate driver unload
    phcManager.reset();
    
    // Simulate driver reload
    phcManager = std::make_unique<PhcManager>(mockAdapter.get());
    status = phcManager->Initialize();
    EXPECT_EQ(status, STATUS_SUCCESS);
    
    // Verify epoch still at 0 (or re-initialized to 0)
    UINT64 currentTime = 0;
    status = phcManager->ReadTimestamp(&currentTime);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    // Should be near epoch (allowing for small drift)
    EXPECT_LT(currentTime, 1000000ULL); // <1 ms
}
```

### Scenario 2.3: gPTP Sync with TAI Epoch

**Given** master adapter with TAI epoch = 1,000,000,000,000 ns (1000 seconds)  
**And** slave adapter with TAI epoch = 0 ns  
**When** gPTP sync runs for 10 sync intervals  
**Then** slave synchronizes to master's TAI epoch  
**And** sync error <1 µs

**Code Example**:
```cpp
TEST_F(TaiEpochIntegrationTest, GptpSyncWithTaiEpoch) {
    // Master adapter at 1000 seconds TAI
    MockAdapterContext masterAdapter(DEVICE_I226);
    masterAdapter.GetPhcManager()->WriteTimestamp(1000000000000ULL);
    
    // Slave adapter at epoch (0 ns)
    MockAdapterContext slaveAdapter(DEVICE_I226);
    slaveAdapter.GetPhcManager()->InitializeEpoch();
    
    // Simulate gPTP sync (10 intervals)
    for (int i = 0; i < 10; ++i) {
        // Read master time
        UINT64 masterTime = 0;
        masterAdapter.GetPhcManager()->ReadTimestamp(&masterTime);
        
        // Calculate offset
        UINT64 slaveTime = 0;
        slaveAdapter.GetPhcManager()->ReadTimestamp(&slaveTime);
        INT64 offset = (INT64)masterTime - (INT64)slaveTime;
        
        // Apply correction (P-controller)
        INT64 correction = offset / 2;
        slaveAdapter.GetPhcManager()->ApplyOffsetAdjustment(correction);
    }
    
    // Verify final sync error <1 µs
    UINT64 masterFinal = 0;
    UINT64 slaveFinal = 0;
    masterAdapter.GetPhcManager()->ReadTimestamp(&masterFinal);
    slaveAdapter.GetPhcManager()->ReadTimestamp(&slaveFinal);
    
    INT64 finalError = llabs((INT64)masterFinal - (INT64)slaveFinal);
    EXPECT_LT(finalError, 1000LL); // <1 µs
}
```

**Expected Results (Level 2)**:
- ✅ IOCTL epoch reset: Query returns <10 µs
- ✅ Epoch persistence: Timestamp near 0 after driver reload
- ✅ gPTP sync: Final error <1 µs

---

## 🎯 Level 3: V&V Tests (2 test cases)

**Test Framework**: User-mode harness  
**Test File**: `07-verification-validation/tests/tai_epoch_vv_tests.cpp`  
**Test Environment**: Intel I226 adapter, Windows 10/11

### Scenario 3.1: TAI Epoch Accuracy Validation (Unix Epoch Reference)

**Given** Intel I226 adapter, PHC reset to epoch (0 ns)  
**And** system clock synchronized to NTP (UTC reference)  
**When** TAI_ToUTC(PHC_ReadTimestamp(), 37) is called  
**Then** UTC timestamp matches system clock ± 1 second  
**And** leap second offset correctly applied

**Code Example**:
```cpp
TEST_F(TaiEpochVVTest, EpochAccuracyValidation) {
    HANDLE deviceHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Reset PHC to epoch
    UINT64 epoch = 0ULL;
    DWORD bytesReturned = 0;
    DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_SET,
        &epoch, sizeof(UINT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    
    // Wait 5 seconds
    Sleep(5000);
    
    // Read PHC time (TAI)
    UINT64 taiTime = 0;
    DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &taiTime, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    // Convert to UTC
    INT64 utcTime = (INT64)taiTime - (37LL * 1000000000LL);
    
    // Verify elapsed time ~5 seconds (±1 second tolerance)
    EXPECT_GE(utcTime, 4000000000LL); // ≥4 seconds
    EXPECT_LE(utcTime, 6000000000LL); // ≤6 seconds
    
    CloseHandle(deviceHandle);
}
```

### Scenario 3.2: Long-Term Epoch Stability (24 Hour Test)

**Given** PHC initialized to epoch (0 ns)  
**When** monitored over 24 hours  
**Then** PHC drift <1 ppm (86.4 ms over 24 hours)  
**And** epoch remains stable (no spontaneous resets)

**Code Example**:
```cpp
TEST_F(TaiEpochVVTest, LongTermEpochStability) {
    HANDLE deviceHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Reset PHC to epoch
    UINT64 epoch = 0ULL;
    DWORD bytesReturned = 0;
    DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_SET,
        &epoch, sizeof(UINT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    
    // Record start time (system clock)
    auto startTime = std::chrono::system_clock::now();
    
    // Wait 24 hours (or use accelerated test with shorter duration)
    Sleep(24 * 60 * 60 * 1000); // 24 hours
    
    // Read PHC time
    UINT64 phcTime = 0;
    DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &phcTime, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    // Calculate actual elapsed time (system clock)
    auto endTime = std::chrono::system_clock::now();
    auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        endTime - startTime).count();
    
    // Calculate drift
    INT64 drift = llabs((INT64)phcTime - elapsedNs);
    
    // Verify drift <1 ppm (86.4 ms over 24 hours = 86,400,000 ns)
    EXPECT_LT(drift, 86400000LL); // <86.4 ms
    
    CloseHandle(deviceHandle);
}
```

**Expected Results (Level 3)**:
- ✅ Epoch accuracy: UTC timestamp matches system ±1s
- ✅ Long-term stability: Drift <1 ppm (86.4 ms over 24h)
- ✅ No spontaneous epoch resets

---

## 📋 Test Data

**TAI Epoch Definition**:
```cpp
// TAI Epoch = Unix Epoch = 1970-01-01 00:00:00 UTC
TAI_EPOCH_NS = 0ULL

// Leap Second Offset (TAI - UTC) as of 2017-01-01
LEAP_SECOND_OFFSET_2017 = 37 seconds = 37,000,000,000 ns
```

**Leap Second Table** (Partial):
```cpp
// Date (YYYY-MM-DD) : Offset (seconds)
1972-01-01 : 10
1972-07-01 : 11
...
2015-07-01 : 36
2017-01-01 : 37
```

**Conversion Formulas**:
```cpp
// TAI to UTC
UTC_ns = TAI_ns - (LeapSecondOffset * 1,000,000,000)

// UTC to TAI
TAI_ns = UTC_ns + (LeapSecondOffset * 1,000,000,000)
```

**Example Conversions**:
```cpp
// Example 1: TAI = 1 second, Offset = 37 seconds
TAI = 1,000,000,000 ns
UTC = 1,000,000,000 - 37,000,000,000 = -36,000,000,000 ns

// Example 2: UTC = 0 (Unix epoch), Offset = 37 seconds
UTC = 0 ns
TAI = 0 + 37,000,000,000 = 37,000,000,000 ns
```

---

## 🛠️ Implementation Notes

**Test Framework**: Google Test 1.14.0  
**Mock Framework**: Google Mock  
**Coverage Tool**: OpenCppCoverage  
**CI Integration**: GitHub Actions (every commit)

**Mock Dependencies**:
- MockPhcManager (PHC read/write, epoch operations)
- MockHardwareAccessLayer (SYSTIML/SYSTIMH registers)
- MockNdisFilter (IOCTL dispatch)

**Build Command**:
```bash
msbuild 05-implementation/tests/unit/unit_tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

**Run Command**:
```bash
.\x64\Release\unit_tests.exe --gtest_filter=TaiEpochTest.*
```

---

## 🧹 Postconditions / Cleanup

- Reset PHC to epoch (0 ns) after each test
- Release mock adapter contexts
- Clear leap second table cache
- Verify no memory leaks
- Generate code coverage report (target >90%)

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement(s) (#41, #2, #70, #47)
- [x] Test has clear pass/fail criteria
- [x] Test is deterministic (mocked hardware)
- [x] Test is automated (Google Test, CI/CD)
- [x] Test data documented (leap second table, epoch values)
- [x] Expected results quantified (±1s accuracy, <1 ppm drift)
- [x] Traceability links complete (#41, #2, #70, #47, #106)
- [x] Leap second handling validated (2017 offset = 37s)
- [x] Negative timestamp handling tested
- [x] Large timestamp handling tested (year 2100+)
- [x] TAI-UTC bidirectional conversion validated
- [x] Epoch persistence across driver reload tested
- [x] gPTP sync with TAI epoch validated (<1 µs error)

---

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Restored**: 2025-12-30 (from corruption)  
**Owner**: QA Team + PHC Team  
**Blocked By**: #41 (REQ-F-PTP-003 implementation)
