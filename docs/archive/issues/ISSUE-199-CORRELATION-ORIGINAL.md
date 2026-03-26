# Issue #199 - Original Content (Reconstructed)

**Test ID**: TEST-PTP-CORR-001  
**Title**: PTP Hardware Correlation Verification  
**Date Created**: 2025-12-19  
**Restored**: 2025-12-30

---

# 🧪 Test Case Specification

**Test ID**: TEST-PTP-CORR-001  
**Test Name**: PTP Hardware Correlation Verification  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical)  
**Phase**: 07-verification-validation

---

## 🔗 Traceability

**Traces to**: #49 (REQ-F-PTP-CORRELATION-001: Hardware Correlation Accuracy)  
**Verifies**: #49 (REQ-F-PTP-CORRELATION-001)  

**Related Requirements**:  
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)  
- #58 (REQ-NF-PERF-PHC-001: PHC Read Latency <500ns)  

**Related Quality Scenarios**:  
- #110 (QA-SC-PERF-001: PHC Clock Read Latency Under Peak Load)  
- #107 (QA-SC-ACCURACY-001: Time Synchronization Accuracy)

---

## 📋 Test Objective

Validates **PTP hardware correlation accuracy** - the ability of the PHC (Precision Hardware Clock) to accurately correlate hardware timestamps with external PTP time references. Verifies:

1. **PHC-to-PTP Grandmaster correlation** within ±1µs accuracy
2. **Hardware timestamp accuracy** for ingress/egress packets
3. **Multi-adapter PHC synchronization** across multiple NICs
4. **Correlation metrics** (offset, drift, jitter)
5. **Long-term stability** of correlation accuracy
6. **Hardware vs. software timestamp correlation** for hybrid deployments

**Key Use Case**: Applications requiring precise time correlation between local PHC and external PTP references (e.g., gPTP in automotive AVB, IEEE 802.1AS sync, multi-device orchestration).

---

## 📊 Level 1: Unit Tests (10 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_ptp_correlation.cpp`  
**Mock Dependencies**: MockPhcManager, MockPtpEngine, MockHardwareAccessLayer

### Scenario 1.1: Valid PHC-PTP Correlation Read

**Given** PHC synchronized to PTP grandmaster  
**When** correlation metrics queried  
**Then** returns STATUS_SUCCESS with offset <1µs, jitter <100ns

### Scenario 1.2: Hardware Timestamp Correlation

**Given** hardware Tx/Rx timestamps enabled  
**When** packet transmitted and timestamp correlation queried  
**Then** hardware timestamp matches PHC within ±500ns

### Scenario 1.3: Multi-Adapter PHC Synchronization

**Given** 2 adapters both synchronized to same PTP grandmaster  
**When** PHC correlation between adapters queried  
**Then** offset <2µs, drift <100 PPB

### Scenario 1.4: Correlation Offset Calculation

**Given** PHC at 1,000,000,000 ns, PTP reference at 1,000,001,200 ns  
**When** correlation offset calculated  
**Then** returns +1,200 ns offset (PHC behind PTP)

### Scenario 1.5: Correlation Drift Measurement

**Given** two correlation samples 10 seconds apart  
**When** drift calculated from offset change  
**Then** drift <50 PPB (parts per billion)

### Scenario 1.6: Correlation Jitter Statistics

**Given** 1000 correlation samples over 1 second  
**When** jitter (standard deviation) calculated  
**Then** jitter <100ns, max deviation <500ns

### Scenario 1.7: Hardware Correlation Failure Handling

**Given** PTP grandmaster disconnected  
**When** correlation queried  
**Then** returns STATUS_PTP_NOT_SYNCED with last known metrics

### Scenario 1.8: Concurrent Correlation Queries

**Given** 10 threads querying correlation simultaneously  
**When** all threads read correlation metrics  
**Then** 1000 successful reads, consistent metrics across threads

### Scenario 1.9: Correlation History Buffer

**Given** correlation history buffer storing last 100 samples  
**When** history queried  
**Then** returns 100 samples with timestamps, offsets, drift

### Scenario 1.10: Correlation Accuracy Over Temperature

**Given** temperature change from 20°C to 60°C  
**When** correlation accuracy monitored  
**Then** drift compensated, accuracy maintained <1µs

**Expected Results (Level 1)**:
- ✅ All 10 unit test cases pass
- ✅ Correlation accuracy <1µs
- ✅ Jitter <100ns typical, <500ns max
- ✅ Thread-safe concurrent access

---

## 🔗 Level 2: Integration Tests (3 test cases)

**Test Framework**: Google Test + Real Hardware  
**Test File**: `05-implementation/tests/integration/test_ptp_correlation_integration.cpp`

### IT-CORR-001: PHC-to-PTP Grandmaster Correlation End-to-End

**Given** adapter synchronized to PTP grandmaster (IEEE 1588 or 802.1AS)  
**When** correlation metrics queried via IOCTL  
**Then** offset <1µs, drift <50 PPB, jitter <100ns

### IT-CORR-002: Multi-Adapter PHC Synchronization

**Given** 3 Intel I226 adapters synchronized to same PTP grandmaster  
**When** PHC values read simultaneously from all adapters  
**Then** maximum PHC delta between any two adapters <2µs

### IT-CORR-003: Hardware Timestamp Correlation with gPTP

**Given** gPTP running, hardware Tx/Rx timestamps enabled  
**When** sync message transmitted with hardware timestamp  
**Then** timestamp correlates to PHC within ±500ns

**Expected Results (Level 2)**:
- ✅ PHC-to-PTP correlation <1µs
- ✅ Multi-adapter delta <2µs
- ✅ Hardware timestamp accuracy ±500ns

---

## 🎯 Level 3: V&V Tests (2 test cases)

**Test Framework**: User-Mode Harness  
**Test File**: `07-verification-validation/tests/ptp_correlation_vv_tests.cpp`  
**Test Environment**: Intel I226 adapter, PTP grandmaster (IEEE 1588 or 802.1AS)

### VV-CORR-001: Long-Term Correlation Stability

**Given** PHC synchronized to PTP grandmaster for 24 hours  
**When** correlation metrics logged every 60 seconds (1440 samples)  
**Then** all samples <1µs offset, drift <50 PPB, no divergence

### VV-CORR-002: Correlation Accuracy Under Load

**Given** network load at 1 Gbps, PTP sync messages every 125ms  
**When** correlation accuracy monitored for 10 minutes  
**Then** all samples <1µs offset, jitter <100ns, no sync loss

**Expected Results (Level 3)**:
- ✅ 24-hour stability with <1µs accuracy
- ✅ Accuracy maintained under 1 Gbps load

---

## 📋 Test Summary

**Total Test Cases**: 15 (10 unit + 3 integration + 2 V&V)

**Key Validation Points**:
- PHC-to-PTP correlation accuracy (<1µs offset)
- Hardware timestamp correlation (±500ns)
- Multi-adapter synchronization (<2µs delta)
- Drift compensation (<50 PPB)
- Jitter control (<100ns typical, <500ns max)
- Long-term stability (24 hours)
- Accuracy under load (1 Gbps)

**Critical Success Criteria**:
- ✅ Correlation offset <1µs (all samples)
- ✅ Jitter <100ns (typical operation)
- ✅ Drift <50 PPB (long-term stability)
- ✅ Hardware timestamp accuracy ±500ns

---

## 🛠️ Implementation Notes

### Data Structures

```cpp
typedef struct _PTP_CORRELATION_METRICS {
    INT64 offsetNs;           // PHC offset from PTP reference (ns)
    INT32 driftPpb;           // Clock drift in parts per billion
    UINT32 jitterNs;          // Jitter (standard deviation) in ns
    UINT64 ptpTimestampNs;    // PTP grandmaster timestamp
    UINT64 phcTimestampNs;    // Local PHC timestamp
    BOOLEAN isSynced;         // TRUE if synchronized to PTP
    UINT32 syncIntervalMs;    // Sync message interval (ms)
} PTP_CORRELATION_METRICS;

#define IOCTL_AVB_PTP_CORRELATION \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x805, METHOD_BUFFERED, FILE_READ_DATA)
```

### Key Implementation

```cpp
// Calculate PHC-PTP correlation offset
NTSTATUS CalculatePtpCorrelation(PTP_CORRELATION_METRICS* pMetrics) {
    UINT64 phcTime, ptpTime;
    
    // Read PHC and PTP reference atomically
    phcTime = ReadPhcTimestamp();
    ptpTime = GetPtpGrandmasterTime();  // From last PTP sync message
    
    pMetrics->phcTimestampNs = phcTime;
    pMetrics->ptpTimestampNs = ptpTime;
    pMetrics->offsetNs = (INT64)(phcTime - ptpTime);  // Signed offset
    
    // Calculate drift from historical offset changes
    pMetrics->driftPpb = CalculateDriftPpb();
    
    // Calculate jitter from last N samples
    pMetrics->jitterNs = CalculateJitter();
    
    pMetrics->isSynced = IsPtpSynchronized();
    pMetrics->syncIntervalMs = GetSyncInterval();
    
    return STATUS_SUCCESS;
}
```

### Correlation Accuracy Requirements

| Metric | Typical | Maximum | Failure Threshold |
|--------|---------|---------|-------------------|
| **Offset** | <500ns | <1µs | >5µs |
| **Jitter** | <50ns | <100ns | >500ns |
| **Drift** | <10 PPB | <50 PPB | >200 PPB |
| **Hardware Timestamp Accuracy** | ±200ns | ±500ns | >1µs |

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement(s) (#49, #2, #58)
- [x] Test has clear pass/fail criteria (<1µs offset)
- [x] Test is deterministic (hardware PTP sync)
- [x] Test is automated (Google Test + harness)
- [x] Test data documented (correlation metrics, thresholds)
- [x] Expected results quantified (offset, jitter, drift)
- [x] Traceability links complete (#49, #2, #58, #110, #107)
- [x] Long-term stability tested (24 hours)
- [x] Multi-adapter synchronization tested (3 adapters)
- [x] Hardware timestamp correlation verified

---

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Restored**: 2025-12-30 (from corruption)  
**Owner**: QA Team + PTP Team  
**Blocked By**: #49 (REQ-F-PTP-CORRELATION-001 implementation)
