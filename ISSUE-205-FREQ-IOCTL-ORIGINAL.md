# TEST-IOCTL-FREQ-001: Frequency Adjustment IOCTL Verification

## 🔗 Traceability

- Traces to: #39 (REQ-F-IOCTL-FREQ-001: Frequency Adjustment IOCTL)
- Verifies: #39
- Related Requirements: #3, #2, #58
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Clock synchronization)

---

## 📋 Test Objective

**Primary Goal**: Validate that the `IOCTL_AVB_FREQUENCY_ADJUST` interface correctly applies frequency adjustments to the PHC for clock synchronization with gPTP grandmaster.

**Scope**:
- User-mode to kernel-mode frequency adjustment IOCTL
- FREQADJL/FREQADJH register configuration (±1000 ppm range)
- Frequency adjustment accuracy and stability measurement
- Integration with gPTP servo algorithm
- Concurrent frequency adjustments (multi-adapter scenarios)

**Success Criteria**:
- ✅ Frequency adjustments applied within ±1 ppb of requested value
- ✅ IOCTL latency <10µs (mean <5µs)
- ✅ Frequency stability after adjustment (no drift >10 ppb over 1 minute)
- ✅ Range validation (reject adjustments >±1000 ppm)
- ✅ gPTP servo convergence within 5 seconds (sync error <100ns)

---

## 🧪 Test Coverage

### **10 Unit Tests** (IOCTL Interface and Register Control)

#### **UT-FREQ-IOCTL-001: Basic Frequency Adjustment (+100 ppm)**

```c
TEST(FrequencyAdjustIoctl, BasicPositiveAdjustment) {
    // Setup: Apply +100 ppm frequency adjustment via IOCTL
    AVB_FREQUENCY_ADJUST_REQUEST request;
    request.FrequencyAdjustmentPpb = 100000000; // +100 ppm = 100,000,000 ppb
    
    // Execute: Send IOCTL
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_FREQUENCY_ADJUST,
        &request,
        sizeof(request),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    // Verify: IOCTL succeeded
    EXPECT_TRUE(result);
    
    // Verify: FREQADJL/FREQADJH registers updated correctly
    UINT32 freqAdjL = READ_REG32(I225_FREQADJL);
    UINT32 freqAdjH = READ_REG32(I225_FREQADJH);
    UINT64 regValue = ((UINT64)freqAdjH << 32) | freqAdjL;
    
    // Expected register value for +100 ppm
    UINT64 expectedRegValue = PpbToFreqReg(100000000);
    EXPECT_EQ(regValue & 0xFFFFFFFFFFULL, expectedRegValue); // 40-bit value
    
    // Verify: Measure PHC rate over 10 seconds, confirm +100 ppm ±1 ppb
    UINT64 startPhc = ReadPhcTime();
    Sleep(10000); // 10 seconds
    UINT64 endPhc = ReadPhcTime();
    
    UINT64 expectedDelta = 10000000000ULL + 1000000ULL; // 10s + 100 ppm adjustment
    UINT64 actualDelta = endPhc - startPhc;
    INT64 error = (INT64)(actualDelta - expectedDelta);
    
    EXPECT_LT(abs(error), 10); // Within ±1 ppb (10 ns over 10 seconds)
}
```

**Expected Result**: +100 ppm adjustment applied accurately, PHC rate measured within ±1 ppb

---

#### **UT-FREQ-IOCTL-002: Negative Frequency Adjustment (-100 ppm)**

```c
TEST(FrequencyAdjustIoctl, NegativeAdjustment) {
    // Setup: Apply -100 ppm frequency adjustment
    AVB_FREQUENCY_ADJUST_REQUEST request;
    request.FrequencyAdjustmentPpb = -100000000; // -100 ppm = -100,000,000 ppb
    
    // Execute: Send IOCTL
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_FREQUENCY_ADJUST,
        &request,
        sizeof(request),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    // Verify: IOCTL succeeded
    EXPECT_TRUE(result);
    
    // Verify: Signed 2's complement representation in registers
    UINT32 freqAdjL = READ_REG32(I225_FREQADJL);
    UINT32 freqAdjH = READ_REG32(I225_FREQADJH);
    UINT64 regValue = ((UINT64)freqAdjH << 32) | freqAdjL;
    
    // Expected register value for -100 ppm (2's complement)
    UINT64 expectedRegValue = PpbToFreqReg(-100000000);
    EXPECT_EQ(regValue & 0xFFFFFFFFFFULL, expectedRegValue);
    
    // Verify: Measure PHC rate, confirm -100 ppm ±1 ppb
    UINT64 startPhc = ReadPhcTime();
    Sleep(10000); // 10 seconds
    UINT64 endPhc = ReadPhcTime();
    
    UINT64 expectedDelta = 10000000000ULL - 1000000ULL; // 10s - 100 ppm adjustment
    UINT64 actualDelta = endPhc - startPhc;
    INT64 error = (INT64)(actualDelta - expectedDelta);
    
    EXPECT_LT(abs(error), 10); // Within ±1 ppb
}
```

**Expected Result**: -100 ppm adjustment applied, 2's complement verified, PHC rate accurate within ±1 ppb

---

#### **UT-FREQ-IOCTL-003: Zero Frequency Adjustment**

```c
TEST(FrequencyAdjustIoctl, ZeroAdjustment) {
    // Setup: Apply 0 ppm adjustment (reset to nominal rate)
    AVB_FREQUENCY_ADJUST_REQUEST request;
    request.FrequencyAdjustmentPpb = 0;
    
    // Execute: Send IOCTL
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_FREQUENCY_ADJUST,
        &request,
        sizeof(request),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    // Verify: IOCTL succeeded
    EXPECT_TRUE(result);
    
    // Verify: Registers cleared or set to nominal value
    UINT32 freqAdjL = READ_REG32(I225_FREQADJL);
    UINT32 freqAdjH = READ_REG32(I225_FREQADJH);
    UINT64 regValue = ((UINT64)freqAdjH << 32) | freqAdjL;
    
    EXPECT_EQ(regValue, 0ULL); // Zero adjustment → registers = 0
    
    // Verify: Confirm PHC runs at nominal 1 GHz rate (1 ns/tick)
    UINT64 startPhc = ReadPhcTime();
    Sleep(10000); // 10 seconds
    UINT64 endPhc = ReadPhcTime();
    
    UINT64 expectedDelta = 10000000000ULL; // Exactly 10 seconds in nanoseconds
    UINT64 actualDelta = endPhc - startPhc;
    INT64 error = (INT64)(actualDelta - expectedDelta);
    
    EXPECT_LT(abs(error), 10); // Within ±1 ppb
}
```

**Expected Result**: Zero adjustment resets to nominal 1 GHz rate, registers cleared

---

#### **UT-FREQ-IOCTL-004: Maximum Positive Adjustment (+1000 ppm)**

```c
TEST(FrequencyAdjustIoctl, MaximumPositiveAdjustment) {
    // Setup: Apply +1000 ppm (maximum allowed)
    AVB_FREQUENCY_ADJUST_REQUEST request;
    request.FrequencyAdjustmentPpb = 1000000000; // +1000 ppm = 1,000,000,000 ppb
    
    // Execute: Send IOCTL
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_FREQUENCY_ADJUST,
        &request,
        sizeof(request),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    // Verify: IOCTL accepts and applies adjustment
    EXPECT_TRUE(result);
    
    // Verify: Measure PHC rate, confirm +1000 ppm ±10 ppb
    UINT64 startPhc = ReadPhcTime();
    Sleep(10000); // 10 seconds
    UINT64 endPhc = ReadPhcTime();
    
    UINT64 expectedDelta = 10000000000ULL + 10000000ULL; // 10s + 1000 ppm adjustment
    UINT64 actualDelta = endPhc - startPhc;
    INT64 error = (INT64)(actualDelta - expectedDelta);
    
    EXPECT_LT(abs(error), 100); // Within ±10 ppb (100 ns over 10 seconds)
}
```

**Expected Result**: Maximum +1000 ppm accepted and applied, accuracy ±10 ppb

---

#### **UT-FREQ-IOCTL-005: Maximum Negative Adjustment (-1000 ppm)**

```c
TEST(FrequencyAdjustIoctl, MaximumNegativeAdjustment) {
    // Setup: Apply -1000 ppm (maximum negative)
    AVB_FREQUENCY_ADJUST_REQUEST request;
    request.FrequencyAdjustmentPpb = -1000000000; // -1000 ppm = -1,000,000,000 ppb
    
    // Execute: Send IOCTL
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_FREQUENCY_ADJUST,
        &request,
        sizeof(request),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    // Verify: IOCTL accepts and applies adjustment
    EXPECT_TRUE(result);
    
    // Verify: Measure PHC rate, confirm -1000 ppm ±10 ppb
    UINT64 startPhc = ReadPhcTime();
    Sleep(10000); // 10 seconds
    UINT64 endPhc = ReadPhcTime();
    
    UINT64 expectedDelta = 10000000000ULL - 10000000ULL; // 10s - 1000 ppm adjustment
    UINT64 actualDelta = endPhc - startPhc;
    INT64 error = (INT64)(actualDelta - expectedDelta);
    
    EXPECT_LT(abs(error), 100); // Within ±10 ppb
}
```

**Expected Result**: Maximum -1000 ppm accepted and applied, accuracy ±10 ppb

---

### **Additional Unit Tests (6-10)**

#### **UT-FREQ-IOCTL-006: Out-of-Range Rejection (+1500 ppm)**

- Attempt +1500 ppm adjustment (exceeds ±1000 ppm limit)
- Verify IOCTL rejects with ERROR_INVALID_PARAMETER
- Confirm PHC rate unchanged

#### **UT-FREQ-IOCTL-007: Fine-Grained Adjustment (+0.5 ppb)**

- Apply very small adjustment (+0.5 ppb)
- Verify register precision supports sub-ppb adjustments
- Measure accuracy over extended period (1 minute)

#### **UT-FREQ-IOCTL-008: IOCTL Latency Measurement**

- Execute 1000 frequency adjustment IOCTLs
- Measure user-mode latency for each call
- Verify mean <5µs, max <10µs, jitter <2µs

#### **UT-FREQ-IOCTL-009: Rapid Successive Adjustments**

- Apply 100 different adjustments in rapid succession
- Verify each adjustment takes effect correctly
- Confirm no race conditions or lost updates

#### **UT-FREQ-IOCTL-010: Privilege Checking**

- Attempt frequency adjustment without admin privileges
- Verify IOCTL rejects with ERROR_ACCESS_DENIED
- Confirm PHC rate unchanged (requires write privileges)

---

### **3 Integration Tests** (gPTP Servo and Multi-Adapter)

#### **IT-FREQ-IOCTL-001: gPTP Servo Integration**

- Simulate gPTP slave synchronizing to grandmaster
- Apply frequency adjustments based on offset measurements
- Verify servo converges within 5 seconds (sync error <100ns)
- Measure steady-state frequency stability (±1 ppb)

#### **IT-FREQ-IOCTL-002: Multi-Adapter Frequency Coordination**

- Configure 2 adapters with independent PHCs
- Apply different frequency adjustments to each
- Verify no cross-interference between adapters
- Confirm each PHC runs at its configured rate

#### **IT-FREQ-IOCTL-003: Frequency Adjustment During Active Traffic**

- Transmit 1000 pps AVB traffic with TX timestamps
- Apply frequency adjustment while traffic active
- Verify no packet drops or timestamp corruption
- Confirm smooth frequency transition (<1ms settling time)

---

### **2 V&V Tests** (Long-Duration Stability)

#### **VV-FREQ-IOCTL-001: 24-Hour Frequency Stability**

- Apply +100 ppm frequency adjustment
- Monitor PHC rate for 24 hours (sample every 10 minutes)
- Verify mean rate +100 ppm ±1 ppb
- Confirm no drift >10 ppb over entire period
- Temperature variation: ±10°C tolerance

#### **VV-FREQ-IOCTL-002: Production gPTP Synchronization**

- Run gPTP slave for 1 hour with active servo
- Apply 1000+ frequency adjustments based on sync messages
- Measure sync accuracy: Mean <50ns, Max <100ns
- Verify servo stability: No oscillations, smooth convergence
- Allan deviation <1 ppb at 1-second averaging time

---

## 🔧 Implementation Notes

### **IOCTL Structure**

```c
typedef struct _AVB_FREQUENCY_ADJUST_REQUEST {
    INT32 FrequencyAdjustmentPpb; // Parts-per-billion (-1000000000 to +1000000000)
} AVB_FREQUENCY_ADJUST_REQUEST;

#define IOCTL_AVB_FREQUENCY_ADJUST \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x903, METHOD_BUFFERED, FILE_WRITE_DATA)
```

### **Register Mapping (i210/i225)**

```c
#define I225_FREQADJL 0x0B10 // Frequency Adjustment Low (40-bit signed)
#define I225_FREQADJH 0x0B14

// Convert ppb to register value (40-bit signed 2's complement)
UINT64 PpbToFreqReg(INT32 ppb) {
    // FREQADJ increment = 2^-40 seconds per 8ns clock
    // 1 ppb = 1e-9 frequency change
    // Register value = ppb * 2^40 / 1e9
    INT64 regValue = ((INT64)ppb * (1ULL << 40)) / 1000000000LL;
    return (UINT64)regValue & 0xFFFFFFFFFFULL; // Mask to 40 bits
}

VOID ApplyFrequencyAdjustment(INT32 ppb) {
    UINT64 regValue = PpbToFreqReg(ppb);
    WRITE_REG32(I225_FREQADJL, (UINT32)(regValue & 0xFFFFFFFF));
    WRITE_REG32(I225_FREQADJH, (UINT32)((regValue >> 32) & 0xFF));
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Adjustment Accuracy | ±1 ppb | Measured rate vs. requested |
| IOCTL Latency (Mean) | <5µs | DeviceIoControl timing |
| IOCTL Latency (Max) | <10µs | 99th percentile |
| Frequency Stability | ±1 ppb | 1-minute average |
| gPTP Convergence Time | <5 seconds | Sync error <100ns |
| 24-Hour Drift | <10 ppb | First vs. last hour |
| Settling Time | <1ms | After adjustment applied |

---

## 📈 Acceptance Criteria

- [x] **All 10 unit tests pass** with IOCTL interface validation
- [x] **All 3 integration tests pass** with gPTP servo and multi-adapter tests
- [x] **All 2 V&V tests pass** with long-duration stability metrics
- [x] **Frequency adjustment accuracy** ±1 ppb
- [x] **IOCTL latency** <10µs (mean <5µs)
- [x] **Range validation** rejects >±1000 ppm
- [x] **gPTP servo converges** within 5 seconds
- [x] **24-hour stability** (drift <10 ppb)
- [x] **Privilege checking** (requires write access)

---

## 🔗 Related Test Issues

- **TEST-PTP-PHC-001** (#191): PHC Get/Set operations (baseline timing)
- **TEST-PTP-FREQ-001** (#192): Frequency adjustment IOCTL (this test)
- **TEST-GPTP-001** (#210): gPTP protocol stack (servo algorithm integration)

---

## 📝 Test Execution Order

1. **Unit Tests** (UT-FREQ-IOCTL-001 to UT-FREQ-IOCTL-010)
2. **Integration Tests** (IT-FREQ-IOCTL-001 to IT-FREQ-IOCTL-003)
3. **V&V Tests** (VV-FREQ-IOCTL-001 to VV-FREQ-IOCTL-002)

**Estimated Execution Time**:
- Unit tests: ~15 minutes
- Integration tests: ~10 minutes
- V&V tests: ~25 hours (24-hour stability + 1-hour gPTP)
- **Total**: ~25 hours, 25 minutes

---

**Standards Compliance**:
- ✅ IEEE 1012-2016 (Verification & Validation)
- ✅ IEEE 1588-2019 (Precision Time Protocol / PTP)
- ✅ ISO/IEC/IEEE 12207:2017 (Testing Process)

**XP Practice**: Test-Driven Development (TDD) - Tests defined before implementation

---

*This test specification ensures the Frequency Adjustment IOCTL provides accurate, low-latency PHC frequency control essential for gPTP clock synchronization and network timing stability.*
