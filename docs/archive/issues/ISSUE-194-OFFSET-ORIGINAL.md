# Issue #194 - Original Test Specification

**Test ID**: TEST-IOCTL-OFFSET-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)  
**Status**: Draft - Restored from corruption 2025-12-30

---

## 🧪 Test Case Specification

**Test ID**: TEST-IOCTL-OFFSET-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)

---

## 🔗 Traceability

**Traces to**: #38 (REQ-F-IOCTL-PHC-003: PHC Time Offset Adjustment IOCTL)  
**Verifies**: #38 (REQ-F-IOCTL-PHC-003: PHC Time Offset Adjustment IOCTL)  

**Related Requirements**:  
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)  
- #47 (REQ-NF-REL-PHC-001: PHC Monotonicity Guarantee)  

**Related Quality Scenarios**:  
- #106 (QA-SC-PERF-001: PTP Clock Read Latency Under Peak Load)

---

## 🎯 Test Objective

Validates IOCTL_AVB_PHC_OFFSET_ADJUST interface for applying time offset corrections to PTP hardware clock. Verifies nanosecond-precision offset application, positive/negative offset handling, overflow protection, monotonicity preservation, and privilege checking (Administrator-only operation).

---

## 📊 Level 1: Unit Tests (10 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_ioctl_phc_offset.cpp`  
**Mock Dependencies**: MockNdisFilter, MockPhcManager, MockAdapterContext

### Scenario 1.1: Valid Positive Offset Adjustment

**Given** PHC current time = 1,000,000,000 ns (1 second)  
**And** offset adjustment = +10,000 ns (+10 µs)  
**When** IOCTL_AVB_PHC_OFFSET_ADJUST is called with +10000  
**Then** PHC timestamp adjusted by +10,000 ns  
**And** new PHC time = 1,000,010,000 ns  
**And** function returns STATUS_SUCCESS

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, ValidPositiveOffset) {
    const UINT64 currentTime = 1000000000ULL; // 1 second
    const INT64 offsetNs = +10000; // +10 µs
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(currentTime + offsetNs))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.2: Valid Negative Offset Adjustment

**Given** PHC current time = 1,000,000,000 ns  
**And** offset adjustment = -5,000 ns (-5 µs)  
**When** IOCTL applies offset  
**Then** new PHC time = 999,995,000 ns  
**And** function returns STATUS_SUCCESS

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, ValidNegativeOffset) {
    const UINT64 currentTime = 1000000000ULL;
    const INT64 offsetNs = -5000; // -5 µs
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(currentTime + offsetNs))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.3: Large Offset (+1 Second)

**Given** PHC current time = 500,000,000 ns  
**And** offset = +1,000,000,000 ns (+1 second)  
**When** IOCTL applies large offset  
**Then** new PHC time = 1,500,000,000 ns  
**And** no overflow or corruption

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, LargePositiveOffset) {
    const UINT64 currentTime = 500000000ULL;
    const INT64 offsetNs = 1000000000LL; // +1 second
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(1500000000ULL))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.4: Offset Causing Underflow (Negative Time)

**Given** PHC current time = 1,000 ns  
**And** offset = -2,000 ns (would result in negative time)  
**When** IOCTL validates offset  
**Then** function returns STATUS_INVALID_PARAMETER  
**And** PHC time unchanged  
**And** error logged (monotonicity violation)

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, OffsetCausingUnderflow) {
    const UINT64 currentTime = 1000ULL; // 1 µs
    const INT64 offsetNs = -2000LL; // Would go negative
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    // WriteTimestamp should NOT be called
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0);
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_INVALID_PARAMETER);
}
```

### Scenario 1.5: Input Buffer Too Small

**Given** input buffer size = 4 bytes (insufficient for INT64)  
**When** IOCTL dispatch validates buffer  
**Then** function returns STATUS_BUFFER_TOO_SMALL  
**And** no PHC read/write attempted

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, InputBufferTooSmall) {
    INT32 invalidBuffer = 1000; // Only 4 bytes
    ULONG bytesReturned = 0;
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_)).Times(0);
    EXPECT_CALL(*mockPhc, WriteTimestamp(_)).Times(0);
    
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &invalidBuffer, sizeof(INT32), // Too small
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_BUFFER_TOO_SMALL);
}
```

### Scenario 1.6: NULL Input Buffer

**Given** input buffer pointer = NULL  
**When** IOCTL dispatch validates buffer  
**Then** function returns STATUS_INVALID_PARAMETER  
**And** no PHC modification attempted

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, NullInputBuffer) {
    ULONG bytesReturned = 0;
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_)).Times(0);
    EXPECT_CALL(*mockPhc, WriteTimestamp(_)).Times(0);
    
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        NULL, 0, // NULL input buffer
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_INVALID_PARAMETER);
}
```

### Scenario 1.7: Administrator Privilege Required

**Given** user-mode application running as normal user (non-admin)  
**When** application calls IOCTL_AVB_PHC_OFFSET_ADJUST  
**Then** function returns STATUS_ACCESS_DENIED  
**And** no PHC modification attempted  
**And** security event logged

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, NonAdminAccessDenied) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_NORMAL_USER);
    
    const INT64 offsetNs = +10000;
    ULONG bytesReturned = 0;
    
    // PHC should NOT be modified
    EXPECT_CALL(*mockPhc, ReadTimestamp(_)).Times(0);
    EXPECT_CALL(*mockPhc, WriteTimestamp(_)).Times(0);
    
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_ACCESS_DENIED);
}
```

### Scenario 1.8: Administrator Privilege Succeeds

**Given** user-mode application running as Administrator  
**When** application calls IOCTL_AVB_PHC_OFFSET_ADJUST  
**Then** privilege check passes  
**And** offset applied successfully

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, AdminAccessGranted) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 currentTime = 1000000000ULL;
    const INT64 offsetNs = +50000; // +50 µs
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(currentTime + offsetNs))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.9: Hardware Write Failure

**Given** valid offset adjustment  
**And** PHC timestamp write fails (MMIO error)  
**When** IOCTL attempts to apply offset  
**Then** function returns STATUS_DEVICE_HARDWARE_ERROR  
**And** error logged

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, HardwareWriteFailure) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 currentTime = 1000000000ULL;
    const INT64 offsetNs = +10000;
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .WillOnce(Return(STATUS_DEVICE_HARDWARE_ERROR));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_DEVICE_HARDWARE_ERROR);
}
```

### Scenario 1.10: Zero Offset (No-Op)

**Given** offset adjustment = 0 ns  
**When** IOCTL is called with zero offset  
**Then** PHC read occurs (for validation)  
**And** no write performed (optimization)  
**And** function returns STATUS_SUCCESS

**Code Example**:
```cpp
TEST_F(IoctlPhcOffsetTest, ZeroOffsetNoOp) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 currentTime = 1000000000ULL;
    const INT64 offsetNs = 0; // Zero offset
    
    EXPECT_CALL(*mockPhc, ReadTimestamp(_))
        .WillOnce(DoAll(SetArgPointee<0>(currentTime),
                        Return(STATUS_SUCCESS)));
    
    // WriteTimestamp should NOT be called (optimization)
    EXPECT_CALL(*mockPhc, WriteTimestamp(_)).Times(0);
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

**Expected Results (Level 1)**:
- ✅ All 10 unit test cases pass
- ✅ Code coverage >90% for offset adjustment module
- ✅ Positive/negative offsets handled correctly
- ✅ Underflow protection (no negative time)
- ✅ Security: Administrator privilege enforced
- ✅ Buffer validation comprehensive

---

## 🔗 Level 2: Integration Tests (3 test cases)

**Test Framework**: Google Test + Mock NDIS  
**Test File**: `05-implementation/tests/integration/test_ioctl_offset_integration.cpp`  
**Mock Dependencies**: MockNdisFilter, MockHardwareAccessLayer

### Scenario 2.1: User-Mode Application Offset Adjustment

**Given** driver loaded, Administrator privileges  
**And** Intel I226 adapter detected  
**When** user-mode app calls DeviceIoControl(IOCTL_AVB_PHC_OFFSET_ADJUST, +100µs)  
**Then** IOCTL routed through NDIS filter → dispatch → PHC module  
**And** PHC time adjusted by +100,000 ns  
**And** STATUS_SUCCESS returned

**Code Example**:
```cpp
TEST_F(IoctlOffsetIntegrationTest, UserModeOffsetAdjustment) {
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Read current time
    UINT64 timeBefore = 0;
    DWORD bytesReturned = 0;
    DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                   NULL, 0, &timeBefore, sizeof(UINT64), &bytesReturned, NULL);
    
    // Apply +100 µs offset
    INT64 offsetNs = 100000LL;
    BOOL success = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &offsetNs, sizeof(INT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(success);
    
    // Verify time changed
    UINT64 timeAfter = 0;
    DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                   NULL, 0, &timeAfter, sizeof(UINT64), &bytesReturned, NULL);
    
    EXPECT_EQ(timeAfter, timeBefore + 100000ULL);
    
    CloseHandle(deviceHandle);
}
```

### Scenario 2.2: Sequential Offset Adjustments

**Given** PHC initialized at epoch (0 ns)  
**When** application applies 5 sequential offsets: +10µs, +20µs, -5µs, +15µs, -8µs  
**Then** each adjustment accumulates correctly  
**And** final PHC time = 0 + 10,000 + 20,000 - 5,000 + 15,000 - 8,000 = 32,000 ns  
**And** all operations succeed

**Code Example**:
```cpp
TEST_F(IoctlOffsetIntegrationTest, SequentialOffsetAdjustments) {
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    std::vector<INT64> offsets = {+10000, +20000, -5000, +15000, -8000};
    INT64 expectedFinalTime = 0;
    
    for (INT64 offset : offsets) {
        DWORD bytesReturned = 0;
        BOOL success = DeviceIoControl(
            deviceHandle,
            IOCTL_AVB_PHC_OFFSET_ADJUST,
            &offset, sizeof(INT64),
            NULL, 0,
            &bytesReturned,
            NULL);
        
        EXPECT_TRUE(success);
        expectedFinalTime += offset;
    }
    
    // Verify final time
    UINT64 finalTime = 0;
    DWORD bytesReturned = 0;
    DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                   NULL, 0, &finalTime, sizeof(UINT64), &bytesReturned, NULL);
    
    EXPECT_EQ(finalTime, (UINT64)expectedFinalTime); // 32,000 ns
    
    CloseHandle(deviceHandle);
}
```

### Scenario 2.3: Concurrent Offset Adjustments Serialized

**Given** 4 threads attempting simultaneous offset adjustments  
**When** all threads call IOCTL_AVB_PHC_OFFSET_ADJUST concurrently  
**Then** driver serializes operations (mutual exclusion)  
**And** all 4 offsets applied (no lost updates)  
**And** final time = sum of all offsets

**Code Example**:
```cpp
TEST_F(IoctlOffsetIntegrationTest, ConcurrentOffsetsSerialized) {
    std::vector<std::thread> threads;
    std::vector<INT64> offsets = {+5000, +10000, +15000, +20000};
    std::atomic<int> successCount(0);
    
    for (INT64 offset : offsets) {
        threads.emplace_back([&, offset]() {
            HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
            if (deviceHandle == INVALID_HANDLE_VALUE) return;
            
            DWORD bytesReturned = 0;
            BOOL success = DeviceIoControl(
                deviceHandle,
                IOCTL_AVB_PHC_OFFSET_ADJUST,
                &offset, sizeof(INT64),
                NULL, 0,
                &bytesReturned,
                NULL);
            
            if (success) successCount++;
            CloseHandle(deviceHandle);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(successCount, 4); // All succeeded
    
    // Verify total offset applied
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    UINT64 finalTime = 0;
    DWORD bytesReturned = 0;
    DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                   NULL, 0, &finalTime, sizeof(UINT64), &bytesReturned, NULL);
    
    UINT64 expectedTotal = 5000 + 10000 + 15000 + 20000; // 50,000 ns
    EXPECT_EQ(finalTime, expectedTotal);
    
    CloseHandle(deviceHandle);
}
```

**Expected Results (Level 2)**:
- ✅ User-mode application successfully adjusts PHC offset
- ✅ Sequential offsets accumulate correctly
- ✅ Concurrent offsets serialized (mutual exclusion, no lost updates)

---

## 🎯 Level 3: V&V Tests (2 test cases)

**Test Framework**: User-mode test harness  
**Test File**: `07-verification-validation/tests/offset_vv_tests.cpp`  
**Hardware Required**: Intel I226 adapter

### Scenario 3.1: gPTP Servo Offset Correction Accuracy

**Given** Intel I226 adapter synchronized via gPTP  
**And** slave clock offset detected = +50 µs (behind master)  
**When** gPTP servo applies offset correction via IOCTL_AVB_PHC_OFFSET_ADJUST  
**Then** slave clock offset reduced to <1 µs within 1 sync interval  
**And** sync error <100 ns after 10 intervals  
**And** no overshoot or oscillation

**Code Example**:
```cpp
TEST_F(OffsetVVTest, GptpServoOffsetCorrection) {
    HANDLE deviceHandle = CreateFile(
        L"\\\\.\\IntelAvbFilter0",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Simulate gPTP servo loop
    const int NUM_INTERVALS = 10;
    const INT64 INITIAL_OFFSET = 50000LL; // +50 µs
    INT64 currentOffset = INITIAL_OFFSET;
    
    for (int i = 0; i < NUM_INTERVALS; ++i) {
        // Apply proportional correction (P-controller)
        INT64 correction = -currentOffset / 2; // 50% correction per interval
        
        DWORD bytesReturned = 0;
        BOOL success = DeviceIoControl(
            deviceHandle,
            IOCTL_AVB_PHC_OFFSET_ADJUST,
            &correction, sizeof(INT64),
            NULL, 0,
            &bytesReturned,
            NULL);
        
        EXPECT_TRUE(success);
        
        currentOffset += correction;
        
        printf("Interval %d: Offset = %lld ns\n", i, currentOffset);
        
        // After first interval, offset should be <1 µs
        if (i == 0) {
            EXPECT_LT(llabs(currentOffset), 1000LL); // <1 µs
        }
    }
    
    // Final offset <100 ns
    EXPECT_LT(llabs(currentOffset), 100LL); // <100 ns
    
    CloseHandle(deviceHandle);
}
```

### Scenario 3.2: Large Offset Step Response (Robustness)

**Given** PHC synchronized, stable operation  
**And** large time offset applied (+1 second step)  
**When** offset adjustment executed  
**Then** PHC time jumps correctly by +1 second  
**And** no system instability (no crash, hang)  
**And** subsequent queries return monotonic timestamps  
**And** gPTP can re-synchronize within 10 intervals

**Code Example**:
```cpp
TEST_F(OffsetVVTest, LargeOffsetStepResponse) {
    HANDLE deviceHandle = CreateFile(
        L"\\\\.\\IntelAvbFilter0",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Read time before large offset
    UINT64 timeBefore = 0;
    DWORD bytesReturned = 0;
    DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                   NULL, 0, &timeBefore, sizeof(UINT64), &bytesReturned, NULL);
    
    // Apply +1 second step
    INT64 largeOffset = 1000000000LL; // +1 second
    BOOL success = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        &largeOffset, sizeof(INT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(success);
    
    // Verify time jumped correctly
    UINT64 timeAfter = 0;
    DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                   NULL, 0, &timeAfter, sizeof(UINT64), &bytesReturned, NULL);
    
    EXPECT_EQ(timeAfter, timeBefore + 1000000000ULL); // +1 second
    
    // Verify monotonicity (100 subsequent reads)
    UINT64 lastTime = timeAfter;
    for (int i = 0; i < 100; ++i) {
        UINT64 currentTime = 0;
        DeviceIoControl(deviceHandle, IOCTL_AVB_PHC_QUERY,
                       NULL, 0, &currentTime, sizeof(UINT64), &bytesReturned, NULL);
        
        EXPECT_GE(currentTime, lastTime); // Monotonic
        lastTime = currentTime;
    }
    
    CloseHandle(deviceHandle);
}
```

**Expected Results (Level 3)**:
- ✅ gPTP servo: Offset corrected to <1 µs in 1 interval, <100 ns after 10 intervals
- ✅ Large offset step: Correct time jump, monotonicity preserved, system stable

---

## 📋 Test Data

### IOCTL Code Definition
```cpp
#define IOCTL_AVB_PHC_OFFSET_ADJUST \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x802, METHOD_BUFFERED, FILE_WRITE_DATA)
```

### Buffer Sizes
- **Input Buffer**: 8 bytes (sizeof(INT64) for offset in nanoseconds)
- **Output Buffer**: 0 bytes (no output, STATUS_SUCCESS indicates completion)

### Offset Test Values
```cpp
Positive: +1ns, +1000ns (1µs), +100000ns (100µs), +1000000000ns (+1s)
Negative: -1ns, -1000ns (-1µs), -100000ns (-100µs)
Zero: 0ns (no-op)
Underflow: Current time < |negative offset| (should fail)
```

### Privilege Levels
- **Administrator**: Required for IOCTL_AVB_PHC_OFFSET_ADJUST
- **Normal User**: ACCESS_DENIED

---

## 🛠️ Implementation Notes

**Test Framework**: Google Test 1.14.0  
**Coverage Tool**: OpenCppCoverage  
**CI Integration**: GitHub Actions (every commit)

**Mock Dependencies**:
- `MockNdisFilter`: IOCTL dispatch with privilege checking
- `MockPhcManager`: PHC read/write with underflow validation
- `MockAdapterContext`: Per-adapter state management

**Build Command**:
```bash
msbuild 05-implementation/tests/unit/unit_tests.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild 05-implementation/tests/integration/integration_tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

**V&V Test Execution**:
```bash
# gPTP servo test
.\offset_vv_tests.exe --gtest_filter=OffsetVVTest.GptpServoOffsetCorrection

# Large offset robustness
.\offset_vv_tests.exe --gtest_filter=OffsetVVTest.LargeOffsetStepResponse
```

---

## 🧹 Postconditions / Cleanup

**After Unit/Integration Tests**:
- Release mock contexts
- Reset PHC state to baseline
- Verify no memory leaks (Driver Verifier)

**After V&V Tests**:
- Reset PHC to initial synchronized state
- Close device handles
- Generate offset correction accuracy report (CSV)

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement (#38)
- [x] Test has clear pass/fail criteria (offset accuracy, underflow protection)
- [x] Test is deterministic (no flaky timing issues)
- [x] Test is automated (Google Test + CI/CD)
- [x] Test data documented (offset values, privilege levels)
- [x] Expected results quantified (<1µs convergence, <100ns final error)
- [x] Traceability links complete (#38, #2, #47, #106)
- [x] Security validated (Administrator privilege enforced)
- [x] Monotonicity guaranteed (no negative time allowed)

---

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Restored**: 2025-12-30 (from corruption)  
**Owner**: QA Team + PHC Team  
**Blocked By**: #38 (REQ-F-IOCTL-PHC-003 implementation)
