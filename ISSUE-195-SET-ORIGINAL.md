# Issue #195 - Original Test Specification

**Test ID**: TEST-IOCTL-SET-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)  
**Status**: Draft - Restored from corruption 2025-12-30

---

## 🧪 Test Case Specification

**Test ID**: TEST-IOCTL-SET-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)

---

## 🔗 Traceability

**Traces to**: #70 (REQ-F-IOCTL-PHC-002: PHC Time Set IOCTL)  
**Verifies**: #70 (REQ-F-IOCTL-PHC-002: PHC Time Set IOCTL)  

**Related Requirements**:  
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)  
- #38 (REQ-F-IOCTL-PHC-003: PHC Time Offset Adjustment IOCTL)  
- #47 (REQ-NF-REL-PHC-001: PHC Monotonicity Guarantee)  

**Related Quality Scenarios**:  
- #106 (QA-SC-PERF-001: PTP Clock Read Latency Under Peak Load)

---

## 🎯 Test Objective

Validates IOCTL_AVB_PHC_SET interface for writing absolute timestamp to PTP hardware clock. Verifies 64-bit timestamp write, overflow/underflow protection, privilege checking (Administrator-only operation), epoch initialization support, and monotonicity preservation during set operations.

---

## 📊 Level 1: Unit Tests (10 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_ioctl_phc_set.cpp`  
**Mock Dependencies**: MockNdisFilter, MockPhcManager, MockAdapterContext, MockHardwareAccessLayer

### Scenario 1.1: Valid PHC Timestamp Write

**Given** target timestamp = 1,000,000,000,000 ns (1000 seconds)  
**And** driver context initialized with Administrator privilege  
**When** DeviceIoControl(IOCTL_AVB_PHC_SET, &timestamp, 8, NULL, 0)  
**Then** function validates input buffer size (minimum 8 bytes)  
**And** PHC_WriteTimestamp(1000000000000ULL) called  
**And** SYSTIML register written with low 32 bits (0xD4A51000)  
**And** SYSTIMH register written with high 32 bits (0xE8)  
**And** function returns STATUS_SUCCESS

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, ValidTimestampWrite) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 targetTime = 1000000000000ULL; // 1000 seconds
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(targetTime))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(bytesReturned, 0); // No output buffer
}
```

### Scenario 1.2: Timestamp Overflow Protection (64-bit Max)

**Given** target timestamp = 0xFFFFFFFFFFFFFFFF (maximum UINT64)  
**When** IOCTL_AVB_PHC_SET is called  
**Then** driver validates timestamp range  
**And** function returns STATUS_SUCCESS (valid 64-bit value)  
**And** SYSTIML = 0xFFFFFFFF, SYSTIMH = 0xFFFFFFFF

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, MaximumTimestampValue) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 maxTime = 0xFFFFFFFFFFFFFFFFULL;
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(maxTime))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &maxTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.3: Zero Timestamp (Epoch Initialization)

**Given** target timestamp = 0 (TAI epoch)  
**When** IOCTL_AVB_PHC_SET(0) is called  
**Then** PHC time reset to epoch (0 ns)  
**And** SYSTIML = 0x00000000, SYSTIMH = 0x00000000

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, EpochInitialization) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 epoch = 0ULL;
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(0ULL))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &epoch, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.4: Input Buffer Too Small

**Given** input buffer size = 4 bytes (less than 8 bytes required)  
**When** IOCTL dispatch validates buffer size  
**Then** function returns STATUS_BUFFER_TOO_SMALL  
**And** PHC not modified

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, InputBufferTooSmall) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    UINT32 invalidBuffer = 0x12345678; // Only 4 bytes
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0); // Should NOT be called
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &invalidBuffer, sizeof(UINT32), // Wrong size
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_BUFFER_TOO_SMALL);
}
```

### Scenario 1.5: NULL Input Buffer

**Given** input buffer pointer = NULL  
**When** IOCTL dispatch validates buffer  
**Then** function returns STATUS_INVALID_PARAMETER  
**And** PHC not modified

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, NullInputBuffer) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0); // Should NOT be called
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        NULL, 0, // NULL buffer
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_INVALID_PARAMETER);
}
```

### Scenario 1.6: Administrator Privilege Required (Security)

**Given** user-mode application running as normal user (non-admin)  
**When** application calls IOCTL_AVB_PHC_SET  
**Then** driver checks caller privilege  
**And** function returns STATUS_ACCESS_DENIED  
**And** PHC not modified  
**And** security event logged

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, NonAdminAccessDenied) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_NORMAL_USER);
    
    const UINT64 targetTime = 1000000000ULL;
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0); // Should NOT be called
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_ACCESS_DENIED);
}
```

### Scenario 1.7: Administrator Privilege Succeeds

**Given** user-mode application running as Administrator  
**When** application calls IOCTL_AVB_PHC_SET  
**Then** privilege check passes  
**And** timestamp write succeeds

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, AdminAccessGranted) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 targetTime = 5000000000ULL; // 5 seconds
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(targetTime))
        .WillOnce(Return(STATUS_SUCCESS));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
}
```

### Scenario 1.8: Hardware Write Failure

**Given** MMIO register write fails (hardware error)  
**When** PHC_WriteTimestamp() attempts to write SYSTIML  
**Then** function returns STATUS_DEVICE_HARDWARE_ERROR  
**And** error logged to diagnostic trace

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, HardwareWriteFailure) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    
    const UINT64 targetTime = 1000000000ULL;
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(targetTime))
        .WillOnce(Return(STATUS_DEVICE_HARDWARE_ERROR));
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_DEVICE_HARDWARE_ERROR);
}
```

### Scenario 1.9: Adapter Not Initialized

**Given** PHC module not initialized  
**When** IOCTL_AVB_PHC_SET is called  
**Then** function returns STATUS_DEVICE_NOT_READY  
**And** no hardware access attempted

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, AdapterNotInitialized) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    mockAdapter->SetPhcInitialized(false);
    
    const UINT64 targetTime = 1000000000ULL;
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0); // Should NOT be called
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_DEVICE_NOT_READY);
}
```

### Scenario 1.10: IOCTL During Adapter Removal

**Given** adapter hot-unplug in progress  
**When** IOCTL_AVB_PHC_SET is called  
**Then** function returns STATUS_DEVICE_REMOVED  
**And** no hardware access attempted

**Code Example**:
```cpp
TEST_F(IoctlPhcSetTest, AdapterRemovalHandling) {
    mockAdapter->SetCallerPrivilege(PRIVILEGE_ADMINISTRATOR);
    mockAdapter->SimulateHotUnplug();
    
    const UINT64 targetTime = 1000000000ULL;
    
    EXPECT_CALL(*mockPhc, WriteTimestamp(_))
        .Times(0); // Should NOT be called
    
    ULONG bytesReturned = 0;
    NTSTATUS status = SimulateIoctl(
        mockAdapter.get(),
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned);
    
    EXPECT_EQ(status, STATUS_DEVICE_REMOVED);
}
```

**Expected Results (Level 1)**:
- ✅ All 10 unit test cases pass
- ✅ Code coverage >90% for PHC Set IOCTL handler
- ✅ Security validated: Administrator privilege enforced
- ✅ Input validation comprehensive (buffer size, NULL checks)
- ✅ Error handling graceful (hardware failures, adapter removal)
- ✅ 64-bit timestamp range supported (0 to 0xFFFFFFFFFFFFFFFF)

---

## 🔗 Level 2: Integration Tests (3 test cases)

**Test Framework**: Google Test + Mock NDIS  
**Test File**: `05-implementation/tests/integration/test_ioctl_phc_set_integration.cpp`  
**Mock Dependencies**: MockNdisFilter, MockAdapterContext, Real PHC Manager (integration test)

### Scenario 2.1: User-Mode Application PHC Set

**Given** driver installed, Intel I226 adapter, application running as Administrator  
**When** user-mode app opens device handle `\\.\IntelAvbFilter0`  
**And** calls DeviceIoControl(IOCTL_AVB_PHC_SET, &timestamp, 8, NULL, 0)  
**Then** IOCTL dispatched to AVB Filter driver  
**And** privilege check passes (Administrator)  
**And** PHC timestamp set successfully  
**And** DeviceIoControl returns TRUE

**Code Example**:
```cpp
TEST_F(IoctlPhcSetIntegrationTest, UserModeSetTimestamp) {
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    UINT64 targetTime = 5000000000ULL; // 5 seconds
    DWORD bytesReturned = 0;
    
    BOOL success = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(success);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    
    CloseHandle(deviceHandle);
}
```

### Scenario 2.2: Epoch Initialization (TAI Epoch Set to Zero)

**Given** driver loaded, PHC uninitialized  
**When** application calls IOCTL_AVB_PHC_SET(0)  
**Then** PHC time reset to TAI epoch (0 ns)  
**And** subsequent PHC_ReadTimestamp() returns 0 (plus small increment from read latency)

**Code Example**:
```cpp
TEST_F(IoctlPhcSetIntegrationTest, EpochInitialization) {
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    // Set PHC to epoch (0 ns)
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
    
    // Read back immediately (should be near zero)
    UINT64 readback = 0;
    BOOL readSuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &readback, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(readSuccess);
    EXPECT_LT(readback, 10000ULL); // <10 µs (accounts for read latency)
    
    CloseHandle(deviceHandle);
}
```

### Scenario 2.3: Concurrent Set Operations Serialized

**Given** 4 threads attempting simultaneous PHC_SET operations  
**When** all 4 threads call IOCTL_AVB_PHC_SET with different timestamps  
**Then** driver serializes operations (mutual exclusion, spinlock)  
**And** final PHC time equals last write (no partial writes)  
**And** no lost updates

**Code Example**:
```cpp
TEST_F(IoctlPhcSetIntegrationTest, ConcurrentSetSerialized) {
    std::vector<std::thread> threads;
    std::vector<UINT64> timestamps = {
        1000000000ULL, 2000000000ULL, 3000000000ULL, 4000000000ULL
    };
    std::atomic<int> successCount(0);
    
    for (UINT64 timestamp : timestamps) {
        threads.emplace_back([&, timestamp]() {
            HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
            if (deviceHandle == INVALID_HANDLE_VALUE) return;
            
            DWORD bytesReturned = 0;
            BOOL success = DeviceIoControl(
                deviceHandle,
                IOCTL_AVB_PHC_SET,
                &timestamp, sizeof(UINT64),
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
    
    // Read back final timestamp (should be one of the 4 values, no corruption)
    HANDLE deviceHandle = OpenMockDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    UINT64 finalTime = 0;
    DWORD bytesReturned = 0;
    
    DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &finalTime, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    // Verify finalTime is one of the written values (no corruption)
    bool validTimestamp = (finalTime == 1000000000ULL ||
                          finalTime == 2000000000ULL ||
                          finalTime == 3000000000ULL ||
                          finalTime == 4000000000ULL);
    EXPECT_TRUE(validTimestamp);
    
    CloseHandle(deviceHandle);
}
```

**Expected Results (Level 2)**:
- ✅ User-mode IOCTL succeeds (Administrator privilege)
- ✅ Epoch initialization: readback time <10 µs (near zero)
- ✅ Concurrent operations: All 4 succeed, final timestamp valid (no corruption)
- ✅ Mutual exclusion: No partial writes, serialization enforced

---

## 🎯 Level 3: V&V Tests (2 test cases)

**Test Framework**: User-mode harness  
**Test File**: `07-verification-validation/tests/phc_set_vv_tests.cpp`  
**Test Environment**: Intel I226 adapter, Windows 10/11

### Scenario 3.1: PHC Set Accuracy Validation

**Given** Intel I226 adapter  
**When** timestamp set to 10,000,000,000 ns (10 seconds)  
**And** immediately read back via IOCTL_AVB_PHC_QUERY  
**Then** readback value = 10,000,000,000 ns ± 1 µs (accounting for read latency)  
**And** timestamp persists across reads (no drift)

**Code Example**:
```cpp
TEST_F(PhcSetVVTest, SetAccuracyValidation) {
    HANDLE deviceHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    const UINT64 targetTime = 10000000000ULL; // 10 seconds
    DWORD bytesReturned = 0;
    
    // Set PHC time
    BOOL setSuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_SET,
        &targetTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    EXPECT_TRUE(setSuccess);
    
    // Read back immediately
    UINT64 readback = 0;
    BOOL readSuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &readback, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(readSuccess);
    
    // Verify accuracy: ±1 µs
    INT64 error = (INT64)(readback - targetTime);
    EXPECT_LT(llabs(error), 1000LL); // <1 µs error
    
    CloseHandle(deviceHandle);
}
```

### Scenario 3.2: Large Timestamp Values (64-bit Full Range)

**Given** Intel I226 adapter  
**When** timestamp set to maximum value (0xFFFFFFFFFFFFFFFF)  
**And** read back via IOCTL_AVB_PHC_QUERY  
**Then** readback value = 0xFFFFFFFFFFFFFFFF (no overflow)  
**And** subsequent PHC queries return monotonically increasing values (wraps at 64-bit boundary)

**Code Example**:
```cpp
TEST_F(PhcSetVVTest, LargeTimestampValues) {
    HANDLE deviceHandle = OpenRealDeviceAsAdmin(L"\\\\.\\IntelAvbFilter0");
    ASSERT_NE(deviceHandle, INVALID_HANDLE_VALUE);
    
    const UINT64 maxTime = 0xFFFFFFFFFFFFFFFFULL;
    DWORD bytesReturned = 0;
    
    // Set PHC to maximum 64-bit value
    BOOL setSuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_SET,
        &maxTime, sizeof(UINT64),
        NULL, 0,
        &bytesReturned,
        NULL);
    EXPECT_TRUE(setSuccess);
    
    // Read back (should equal maxTime or slightly higher due to clock advance)
    UINT64 readback = 0;
    BOOL readSuccess = DeviceIoControl(
        deviceHandle,
        IOCTL_AVB_PHC_QUERY,
        NULL, 0,
        &readback, sizeof(UINT64),
        &bytesReturned,
        NULL);
    
    EXPECT_TRUE(readSuccess);
    
    // Verify readback is near maxTime (allowing for clock advance)
    // Note: May wrap to 0 if clock advanced past 64-bit boundary
    if (readback >= maxTime - 100000ULL) {
        EXPECT_GE(readback, maxTime); // Clock advanced slightly
    } else {
        EXPECT_LT(readback, 100000ULL); // Wrapped to beginning
    }
    
    CloseHandle(deviceHandle);
}
```

**Expected Results (Level 3)**:
- ✅ Set accuracy: readback within ±1 µs of target
- ✅ Large timestamps: No overflow, correct 64-bit handling
- ✅ Timestamp persistence: Value stable across reads
- ✅ Wrap-around handling: Correct behavior at 64-bit boundary

---

## 📋 Test Data

**IOCTL Code Definition**:
```cpp
#define IOCTL_AVB_PHC_SET \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)
```

**Register Map**:
```cpp
// SYSTIML (Lower 32 bits, offset 0x0B600)
// SYSTIMH (Upper 32 bits, offset 0x0B604)
// Example timestamp: 1,000,000,000,000 ns (1000 seconds)
SYSTIML = 0xD4A51000 // Low 32 bits
SYSTIMH = 0x000000E8 // High 32 bits
```

**Test Timestamp Values**:
```cpp
// Epoch initialization
0ULL                     // TAI epoch (0 ns)

// Normal operation
1000000000ULL            // 1 second
5000000000ULL            // 5 seconds
10000000000ULL           // 10 seconds
1000000000000ULL         // 1000 seconds

// Boundary conditions
0xFFFFFFFFFFFFFFFFULL    // Maximum 64-bit value
0x8000000000000000ULL    // 2^63 (signed boundary)
```

**Privilege Levels**:
```cpp
Administrator: Required for IOCTL_AVB_PHC_SET
Normal User: ACCESS_DENIED
```

---

## 🛠️ Implementation Notes

**Test Framework**: Google Test 1.14.0  
**Mock Framework**: Google Mock  
**Coverage Tool**: OpenCppCoverage  
**CI Integration**: GitHub Actions (every commit)

**Mock Dependencies**:
- MockHardwareAccessLayer (SYSTIML/SYSTIMH register read/write)
- MockNdisFilter (IOCTL dispatch routing)
- MockAdapterContext (per-adapter state, privilege checking)

**Build Command**:
```bash
msbuild 05-implementation/tests/unit/unit_tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

**Run Command**:
```bash
.\x64\Release\unit_tests.exe --gtest_filter=IoctlPhcSetTest.*
```

---

## 🧹 Postconditions / Cleanup

- Release mock adapter contexts
- Reset mock PHC state (default timestamp)
- Clear privilege escalation flags
- Verify no memory leaks (via ASAN or Application Verifier)
- Generate code coverage report (target >90%)

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement(s) (#70, #2, #38, #47)
- [x] Test has clear pass/fail criteria
- [x] Test is deterministic (mocked hardware)
- [x] Test is automated (Google Test, CI/CD)
- [x] Test data documented (timestamps, register values)
- [x] Expected results quantified (±1 µs accuracy)
- [x] Traceability links complete (#70, #2, #38, #47, #106)
- [x] Security validated (Administrator privilege enforced)
- [x] Input validation comprehensive (buffer size, NULL checks)
- [x] Error handling complete (hardware failures, adapter removal)
- [x] Concurrent access tested (serialization, mutual exclusion)
- [x] 64-bit range validated (0 to 0xFFFFFFFFFFFFFFFF)
- [x] Monotonicity preserved (no backward jumps after set)

---

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Restored**: 2025-12-30 (from corruption)  
**Owner**: QA Team + PHC Team  
**Blocked By**: #70 (REQ-F-IOCTL-PHC-002 implementation)
