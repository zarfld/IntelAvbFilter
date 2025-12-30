# TEST-PTP-FREQ-001: PTP Clock Frequency Adjustment Verification

## 🔗 Traceability

- Traces to: #3 (REQ-F-PTP-002: PTP Clock Frequency Adjustment)
- Verifies: #3 (REQ-F-PTP-002: PTP Clock Frequency Adjustment)
- Related Requirements: #39 (REQ-F-IOCTL-PHC-004: PHC Frequency Adjustment IOCTL)
- Related Quality Scenarios: #106 (QA-SC-PERF-001: PTP Clock Read Latency Under Peak Load)

---

## 🎯 Test Objective

Validates PTP hardware clock frequency adjustment via INCVAL register on Intel I210/I225/I226 adapters. Verifies parts-per-billion (ppb) to INCVAL conversion accuracy, device-specific calculations, frequency stability ±1 ppb, and long-term drift <1 ppm over 24 hours.

---

## 📊 Level 1: Unit Tests (8 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_phc_frequency.cpp`  
**Mock Dependencies**: MockHardwareAccessLayer (INCVAL register read/write)

### Scenario 1.1: Frequency Adjustment Calculation (Positive PPB)

**Given** target frequency adjustment +10 ppb (faster clock)  
**And** base INCVAL = 0x60000000 (1.536 GHz nominal I210)  
**When** PHC_CalculateFrequencyAdjustment(+10) is called  
**Then** function returns STATUS_SUCCESS  
**And** new INCVAL = 0x60000010 (calculated: base + (base × 10 / 1e9))

**Code Example**:

```cpp
TEST_F(PhcFrequencyTest, PositivePpbAdjustment) {
    const UINT32 baseIncval = 0x60000000;
    const INT32 ppbAdjustment = +10;
    
    UINT32 newIncval = 0;
    NTSTATUS status = phcManager->CalculateFrequencyAdjustment(
        baseIncval, ppbAdjustment, &newIncval);
    
    EXPECT_EQ(status, STATUS_SUCCESS);
    EXPECT_EQ(newIncval, 0x60000010);
}
```

### Scenario 1.2: Frequency Adjustment Calculation (Negative PPB)

**Given** target frequency adjustment -10 ppb (slower clock)  
**And** base INCVAL = 0x60000000  
**When** PHC_CalculateFrequencyAdjustment(-10) is called  
**Then** function returns STATUS_SUCCESS  
**And** new INCVAL = 0x5FFFFFF0

### Scenario 1.3: Device-Specific INCVAL Base Values

**Given** Intel I210 adapter (1.536 GHz base frequency)  
**When** PHC_GetBaseIncval(DEVICE_I210) is called  
**Then** function returns 0x60000000

**Given** Intel I225 adapter (1.6 GHz base frequency)  
**When** PHC_GetBaseIncval(DEVICE_I225) is called  
**Then** function returns 0x66666666

### Scenario 1.4: Maximum PPB Adjustment Range

**Given** maximum positive adjustment +999,999 ppb  
**When** PHC_CalculateFrequencyAdjustment(+999999) is called  
**Then** function returns STATUS_SUCCESS

### Scenario 1.5: Out-of-Range PPB Rejection

**Given** invalid adjustment +1,000,000 ppb (exceeds ±999,999 limit)  
**When** PHC_CalculateFrequencyAdjustment(+1000000) is called  
**Then** function returns STATUS_INVALID_PARAMETER

### Scenario 1.6: Frequency Adjustment Write to INCVAL Register

**Given** calculated INCVAL = 0x60000010  
**When** PHC_ApplyFrequencyAdjustment(0x60000010) is called  
**Then** driver writes 0x60000010 to INCVAL register (offset 0x0B608)

### Scenario 1.7: INCVAL Read-Back Verification

**Given** INCVAL = 0x60000010 written to register  
**When** driver reads back INCVAL register  
**Then** read value equals 0x60000010

### Scenario 1.8: Hardware Fault During Frequency Adjustment

**Given** INCVAL register write fails (MMIO error)  
**When** PHC_ApplyFrequencyAdjustment() is called  
**Then** function returns STATUS_DEVICE_HARDWARE_ERROR

**Expected Results (Level 1)**:

- ✅ All 8 unit test cases pass
- ✅ Code coverage >90%
- ✅ PPB to INCVAL conversion accurate to ±1 ppb
- ✅ Device-specific base values validated
- ✅ Out-of-range adjustments rejected

---

## 🔗 Level 2: Integration Tests (3 test cases)

**Test Framework**: Google Test + Mock NDIS  
**Test File**: `05-implementation/tests/integration/test_ioctl_frequency.cpp`

### Scenario 2.1: IOCTL Frequency Adjustment End-to-End

**Given** Intel I210 adapter  
**When** DeviceIoControl(IOCTL_AVB_PHC_FREQ_ADJUST, +10 ppb)  
**Then** new INCVAL = 0x60000010 written

### Scenario 2.2: Concurrent Frequency Adjustments (Multi-Adapter)

**Given** 4 Intel I226 adapters  
**When** 4 threads simultaneously call IOCTL_AVB_PHC_FREQ_ADJUST  
**Then** all 4 succeed

### Scenario 2.3: Frequency Adjustment During Active gPTP Sync

**Given** gPTP sync active  
**When** frequency adjusted via IOCTL  
**Then** sync continues operating

**Expected Results (Level 2)**:

- ✅ IOCTL routing correct
- ✅ 4/4 adapters succeed
- ✅ gPTP sync unaffected

---

## 🎯 Level 3: V&V Tests (3 test cases)

**Test Framework**: User-mode harness  
**Test File**: `07-verification-validation/tests/frequency_vv_tests.cpp`

### Scenario 3.1: Frequency Stability Benchmark (±1 ppb Accuracy)

**Given** +100 ppb adjustment applied  
**When** measured over 1 hour  
**Then** frequency = 1.6000001 GHz ± 1.6 Hz

### Scenario 3.2: Long-Term Frequency Drift (<1 ppm over 24 hours)

**Given** +50 ppb adjustment  
**When** measured over 24 hours  
**Then** cumulative drift <1 ppm (86.4 ms)

### Scenario 3.3: gPTP Sync Error Validation (<1 µs Sustained)

**Given** two adapters (master/slave)  
**When** gPTP sync runs for 1 hour  
**Then** sync error <1 µs sustained

**Expected Results (Level 3)**:

- ✅ Frequency stability: ±1 ppb
- ✅ Long-term drift: <1 ppm
- ✅ gPTP sync error: <1 µs

---

## 📋 Test Data

**INCVAL Register (0x0B608) Values**:

```cpp
// I210: Base frequency 1.536 GHz
INCVAL_BASE_I210 = 0x60000000

// I225/I226: Base frequency 1.6 GHz
INCVAL_BASE_I225 = 0x66666666
INCVAL_BASE_I226 = 0x66666666

// Example Adjustments:
+10 ppb → INCVAL = 0x60000010 (I210)
-10 ppb → INCVAL = 0x5FFFFFF0 (I210)
```

---

## 🛠️ Implementation Notes

**Test Framework**: Google Test 1.14.0  
**CI Integration**: GitHub Actions (every commit)  
**Build Command**:

```bash
msbuild 05-implementation/tests/unit/unit_tests.vcxproj /p:Configuration=Release /p:Platform=x64
```

---

## 🧹 Postconditions / Cleanup

- Release mock adapter contexts
- Reset mock hardware state (INCVAL = base value)
- Generate coverage report

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement(s) (#3, #39)
- [x] Test has clear pass/fail criteria (ppb accuracy, stability thresholds)
- [x] Test is deterministic
- [x] Test is automated
- [x] Test data documented
- [x] Expected results quantified (±1 ppb accuracy, <1 ppm drift, <1 µs sync error)
- [x] Traceability links complete

---

**Test ID**: TEST-PTP-FREQ-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Owner**: QA Team + PHC Team  
**Blocked By**: #3 (REQ-F-PTP-002 implementation)
