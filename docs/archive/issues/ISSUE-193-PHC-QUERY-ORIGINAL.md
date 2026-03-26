# TEST-IOCTL-PHC-QUERY-001: PHC Time Query IOCTL Verification

## 🔗 Traceability

**Traces to**: #34 (REQ-F-IOCTL-PHC-001: PHC Time Query IOCTL)  
**Verifies**: #34 (REQ-F-IOCTL-PHC-001: PHC Time Query IOCTL)

**Related Requirements**:
- #2 (REQ-F-PTP-001: PTP Hardware Clock Get/Set Timestamp)
- #58 (REQ-NF-PERF-PHC-001: PHC Query Latency <500ns)

**Related Quality Scenarios**:
- #106 (QA-SC-PERF-001: PTP Clock Read Latency Under Peak Load)

---

## 🎯 Test Objective

Validates IOCTL_AVB_PHC_QUERY interface for querying PTP hardware clock time from user-mode applications. Verifies IOCTL code routing, input/output buffer validation, privilege checking, concurrent access handling, and p95 latency <500ns under 1000 calls/sec load.

---

## 📊 Level 1: Unit Tests (10 test cases)

**Test Framework**: Google Test  
**Test File**: `05-implementation/tests/unit/test_ioctl_phc_query.cpp`  
**Mock Dependencies**: MockNdisFilter, MockPhcManager, MockAdapterContext

### Scenario 1.1: Valid PHC Query IOCTL

**Given** driver loaded with PHC module initialized  
**When** DeviceIoControl(IOCTL_AVB_PHC_QUERY, NULL, 0, &timestamp, 8)  
**Then** output buffer validated (minimum 8 bytes)  
**And** timestamp written to output buffer  
**And** function returns STATUS_SUCCESS

### Scenario 1.2: Output Buffer Too Small

**Given** output buffer size = 4 bytes  
**When** IOCTL dispatch validates buffer size  
**Then** function returns STATUS_BUFFER_TOO_SMALL

### Scenario 1.3: NULL Output Buffer

**Given** output buffer pointer = NULL  
**When** IOCTL dispatch validates buffer  
**Then** function returns STATUS_INVALID_PARAMETER

### Scenario 1.4: Invalid IOCTL Code

**Given** incorrect IOCTL code  
**When** IOCTL dispatch validates code  
**Then** function returns STATUS_INVALID_DEVICE_REQUEST

### Scenario 1.5: Adapter Not Initialized

**Given** PHC module not initialized  
**When** IOCTL_AVB_PHC_QUERY is called  
**Then** function returns STATUS_DEVICE_NOT_READY

### Scenario 1.6: PHC Hardware Read Failure

**Given** hardware register read fails (MMIO error)  
**When** IOCTL attempts to read PHC  
**Then** function returns STATUS_DEVICE_HARDWARE_ERROR

### Scenario 1.7: Unprivileged User Access (Security)

**Given** normal user (non-admin)  
**When** IOCTL_AVB_PHC_QUERY called  
**Then** IOCTL succeeds (read-only operation)

### Scenario 1.8: Input Buffer Ignored (Defensive)

**Given** unexpected input buffer provided  
**When** IOCTL processes request  
**Then** input buffer ignored, IOCTL succeeds

### Scenario 1.9: Oversized Output Buffer (Defensive)

**Given** output buffer size = 16 bytes  
**When** IOCTL writes timestamp  
**Then** only 8 bytes written, bytesReturned = 8

### Scenario 1.10: IOCTL During Adapter Removal

**Given** adapter hot-unplug in progress  
**When** IOCTL_AVB_PHC_QUERY is called  
**Then** function returns STATUS_DEVICE_REMOVED

**Expected Results (Level 1)**:

- ✅ All 10 unit test cases pass
- ✅ Code coverage >90%
- ✅ Buffer validation comprehensive
- ✅ Security: Unprivileged users can query

---

## 🔗 Level 2: Integration Tests (4 test cases)

**Test Framework**: Google Test + Mock NDIS  
**Test File**: `05-implementation/tests/integration/test_ioctl_phc_integration.cpp`

### Scenario 2.1: User-Mode Application PHC Query

**Given** driver installed, Intel I226 adapter  
**When** user-mode app calls DeviceIoControl(IOCTL_AVB_PHC_QUERY)  
**Then** timestamp returned successfully

### Scenario 2.2: Concurrent IOCTL Queries (Multi-Threaded)

**Given** 10 threads calling IOCTL simultaneously  
**When** all threads issue IOCTLs  
**Then** all 10 succeed, no deadlocks

### Scenario 2.3: Multiple Adapters Concurrent Queries

**Given** 4 Intel I226 adapters  
**When** all 4 queried simultaneously  
**Then** all 4 succeed, no interference

### Scenario 2.4: IOCTL Query During Driver Unload

**Given** driver unload initiated  
**When** IOCTL_AVB_PHC_QUERY called  
**Then** fails gracefully (STATUS_DEVICE_REMOVED)

**Expected Results (Level 2)**:

- ✅ User-mode app succeeds
- ✅ 10/10 concurrent queries succeed
- ✅ 4/4 adapters succeed
- ✅ Graceful failure during unload

---

## 🎯 Level 3: V&V Tests (3 test cases)

**Test Framework**: User-mode harness  
**Test File**: `07-verification-validation/tests/ioctl_phc_vv_tests.cpp`

### Scenario 3.1: IOCTL Latency Benchmark (p95 <500ns)

**Given** 100,000 IOCTL queries  
**Then** p50 <500ns, p95 <500ns, p99 <1µs

### Scenario 3.2: Stress Test (1000 Queries/Sec Sustained)

**Given** 1000 QPS for 1 hour  
**Then** all queries succeed, latency stable

### Scenario 3.3: Multi-Process Concurrent Access

**Given** 5 processes, 100 QPS each for 10 minutes  
**Then** aggregate throughput ≥500 QPS

**Expected Results (Level 3)**:

- ✅ p95 <500ns, p99 <1µs
- ✅ 1000 QPS sustained, 0 failures
- ✅ Multi-process: no interference

---

## 📋 Test Data

**IOCTL Code Definition**:

```cpp
#define IOCTL_AVB_PHC_QUERY \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
```

**Buffer Sizes**:
- Input: 0 bytes (no input expected)
- Output: 8 bytes minimum (UINT64)

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

- Release mock NDIS filter contexts
- Reset mock PHC state
- Generate latency distribution histogram

---

## ✅ Test Case Checklist

- [x] Test verifies specific requirement(s) (#34, #2, #58)
- [x] Test has clear pass/fail criteria
- [x] Test is deterministic
- [x] Test is automated
- [x] Expected results quantified (p95 <500ns)
- [x] Traceability links complete
- [x] Security validated (unprivileged access permitted)
- [x] Concurrent access tested

---

**Test ID**: TEST-IOCTL-PHC-QUERY-001  
**Test Type**: Multi-Level (Unit + Integration + V&V)  
**Priority**: P0 (Critical) - Must pass for release  
**Automation Status**: Automated (CI/CD)

**Status**: Draft - Test defined, implementation pending  
**Created**: 2025-12-19  
**Owner**: QA Team + IOCTL Dispatch Team  
**Blocked By**: #34 (REQ-F-IOCTL-PHC-001 implementation)
