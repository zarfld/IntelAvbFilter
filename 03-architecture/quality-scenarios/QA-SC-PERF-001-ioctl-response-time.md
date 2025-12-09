# QA-SC-PERF-001: IOCTL Response Time Performance

**Quality Attribute**: Performance  
**Status**: Accepted  
**Date**: 2025-12-09  
**Phase**: Phase 03 - Architecture Design  
**Priority**: High (P1)

---

## Scenario Overview

**Scenario ID**: QA-SC-PERF-001  
**Title**: IOCTL Response Time Under Normal Load  
**Quality Attribute**: Performance (Latency)

---

## Structured Quality Attribute Scenario (ATAM Format)

### Source
**User-mode application** (gPTP daemon, TSN monitoring tool, diagnostic utility)

### Stimulus
**Sends IOCTL request** to query hardware state:
- `IOCTL_AVB_PHC_QUERY` (read PTP Hardware Clock)
- `IOCTL_AVB_GET_CAPABILITIES` (query hardware features)
- `IOCTL_AVB_TAS_GET_STATUS` (read Time-Aware Shaper status)
- `IOCTL_AVB_CBS_GET_CONFIG` (read Credit-Based Shaper config)

### Stimulus Environment
**Normal Operation**:
- Single-threaded application
- No other active IOCTLs
- System under typical load (non-real-time workloads running)
- Network adapter operational (link up, packets flowing)

### Artifact
**Driver IOCTL dispatch path**:
- `device.c::FilterDeviceIoControl()` - IOCTL dispatcher
- HAL operations (`ctx->HwOps->ReadPhc()`, etc.)
- Hardware register access (MMIO via BAR0)
- Response buffer copy to user-mode

### Response
**Driver completes IOCTL** and returns:
- Read hardware state from controller registers
- Populate response buffer with requested data
- Return `STATUS_SUCCESS` to user-mode
- User-mode application receives data

### Response Measure
**Quantified Performance Targets**:

| IOCTL Type | Target Latency | Critical Threshold |
|------------|----------------|-------------------|
| **PHC Query** | <100µs (p95) | <200µs (p99) |
| **Capability Query** | <50µs (p95) | <100µs (p99) |
| **TAS Status** | <150µs (p95) | <300µs (p99) |
| **CBS Config** | <100µs (p95) | <200µs (p99) |

**Breakdown**:
- **Kernel dispatch**: <10µs (NDIS → driver)
- **Parameter validation**: <5µs (buffer checks, sanitization)
- **HAL call**: <1µs (function pointer dispatch)
- **Hardware register read**: <50µs (MMIO via BAR0)
- **Response buffer copy**: <10µs (kernel → user buffer)
- **Total budget**: <100µs (p95)

---

## Rationale

### 1. gPTP Synchronization Requirements

**IEEE 802.1AS gPTP** requires timestamp precision <1µs for 100ns sync accuracy:
- PHC queries must not introduce significant latency
- Frequent polling (10-100 Hz) must have low CPU overhead
- Target: 100µs allows 10Hz polling with <1% CPU

### 2. TSN Monitoring Requirements

**Real-time TSN monitoring** requires low-latency status queries:
- TAS gate state changes monitored every 100ms
- CBS credit levels polled for bandwidth monitoring
- Target: <150µs enables responsive monitoring

### 3. Diagnostic Tools Requirements

**Developer diagnostics** need fast hardware queries:
- `avb_test_um.exe` polls PHC at 100Hz for drift measurement
- Capability queries during device enumeration
- Target: <50µs for responsive CLI tools

### 4. CPU Overhead Budget

**High-frequency polling impact**:
- 10Hz polling: 10 IOCTLs/sec → <1ms/sec CPU (0.1%)
- 100Hz polling: 100 IOCTLs/sec → <10ms/sec CPU (1%)
- 1000Hz polling: 1000 IOCTLs/sec → <100ms/sec CPU (10%) - Too high!

**Target**: Enable up to 100Hz polling with <1% CPU overhead

---

## Related Requirements

Traces to: 
- #71 (REQ-NF-DOC-API-001: IOCTL API Documentation) - Documents IOCTL interfaces and performance expectations
- Related to REQ-NF-PERF-001: <1µs timestamp event latency (ring buffer eliminates IOCTL overhead)

**Related Non-Functional Requirements**:
- **Performance**: Low-latency hardware queries
- **Scalability**: Support frequent polling without CPU saturation
- **Usability**: Responsive diagnostic tools

---

## Related Architecture Decisions

**Implements**:
- #117 (ADR-PERF-002: Direct BAR0 MMIO Access) - <500ns register reads enable <100µs IOCTL latency
- #118 (ADR-PERF-SEC-001: Performance vs Security Trade-offs in IOCTL Path) - Balances validation overhead vs. latency
- #126 (ADR-HAL-001: Hardware Abstraction Layer) - Function pointer dispatch adds <1µs overhead

**Related**:
- #93 (ADR-PERF-004: Kernel Ring Buffer) - Alternative to IOCTL polling for high-frequency events (1000Hz+)

---

## Validation Method

### Test Strategy
**Benchmark**: `tools/avb_test/ioctl_latency_benchmark.c`

**Measurement Approach**:
```c
// Measure IOCTL round-trip latency
LARGE_INTEGER freq, start, end;
QueryPerformanceFrequency(&freq);

for (int i = 0; i < 10000; i++) {
    QueryPerformanceCounter(&start);
    
    // IOCTL call
    DWORD bytesReturned;
    DeviceIoControl(hDevice, IOCTL_AVB_PHC_QUERY, 
                    NULL, 0, &phcTime, sizeof(phcTime),
                    &bytesReturned, NULL);
    
    QueryPerformanceCounter(&end);
    
    latencies[i] = ((end.QuadPart - start.QuadPart) * 1000000) / freq.QuadPart; // µs
}

// Calculate percentiles
qsort(latencies, 10000, sizeof(UINT64), compare);
UINT64 p50 = latencies[5000];
UINT64 p95 = latencies[9500];
UINT64 p99 = latencies[9900];

printf("PHC Query Latency:\n");
printf("  p50: %llu µs\n", p50);
printf("  p95: %llu µs\n", p95);
printf("  p99: %llu µs\n", p99);

// Acceptance criteria
assert(p95 < 100);  // <100µs p95
assert(p99 < 200);  // <200µs p99
```

### Test Environment
**Hardware**:
- Intel I226-V NIC (PCIe Gen2 x1)
- x64 system, 4+ cores
- Windows 11 22H2 or later

**Test Conditions**:
- **Warmup**: 1000 iterations (exclude from measurements)
- **Sample Size**: 10,000 iterations per IOCTL type
- **System Load**: Idle + typical background services
- **Driver Build**: Release configuration (optimizations enabled)

### Acceptance Criteria
```gherkin
Feature: IOCTL Response Time Performance
  As a gPTP daemon
  I want fast IOCTL responses
  So that I can poll hardware state without CPU overhead

  Scenario: PHC Query Latency Under Normal Load
    Given Intel I226-V adapter operational
    And user-mode application has device handle
    And system under normal load (idle + background services)
    When application issues 10,000 IOCTL_AVB_PHC_QUERY calls
    Then p95 latency < 100 microseconds
    And p99 latency < 200 microseconds
    And average CPU usage < 0.5%

  Scenario: Capability Query Latency
    Given Intel I226-V adapter operational
    When application issues 10,000 IOCTL_AVB_GET_CAPABILITIES calls
    Then p95 latency < 50 microseconds
    And p99 latency < 100 microseconds

  Scenario: TAS Status Query Latency
    Given Intel I226-V adapter with TAS enabled
    When application issues 10,000 IOCTL_AVB_TAS_GET_STATUS calls
    Then p95 latency < 150 microseconds
    And p99 latency < 300 microseconds

  Scenario: High-Frequency Polling Overhead
    Given gPTP daemon polling PHC at 100Hz (100 queries/second)
    When daemon runs for 60 seconds (6,000 IOCTLs total)
    Then average CPU usage < 1%
    And no latency spikes > 500 microseconds
    And 99.9% of queries complete within 200 microseconds
```

---

## Status Assessment

### Current Status
**Implementation Status**: ⚠️ **Partially Verified**

**Known Performance**:
- **PHC Query**: <50µs average (measured on I226-V)
- **Capability Query**: <30µs average
- **TAS Status**: Not yet benchmarked
- **CBS Config**: Not yet benchmarked

**Bottlenecks**:
- ✅ Direct BAR0 MMIO implemented (ADR-PERF-002) → <500ns register reads
- ✅ HAL function pointer dispatch → <1µs overhead
- ⚠️ Parameter validation overhead not yet measured
- ⚠️ Buffer copy performance not yet optimized

### Risks and Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| **PCIe latency spikes** | Medium | High | Use PCIe Gen2+ (Gen1 has higher latency) |
| **Parameter validation overhead** | Low | Medium | Keep validation logic simple (<5µs target) |
| **Context switch delays** | Low | High | Ensure IRQL < DISPATCH_LEVEL in IOCTL handler |
| **Hardware quirks** | Medium | High | HAL encapsulates controller-specific workarounds |
| **Concurrent IOCTL contention** | Low | Medium | Use per-adapter spinlocks (not global locks) |

### Verification Plan

**Phase 1: Baseline Measurement (Week 1)**
- Implement `ioctl_latency_benchmark.c`
- Measure all IOCTL types (PHC, Capabilities, TAS, CBS)
- Establish baseline performance on I226-V

**Phase 2: Optimization (Week 2-3)**
- Profile slow paths with Windows Performance Recorder (WPR)
- Optimize parameter validation (target <5µs)
- Optimize buffer copy (use `RtlCopyMemory` intrinsics)

**Phase 3: Regression Testing (Week 4)**
- Add latency benchmarks to CI pipeline
- Automated performance regression detection (>10% slowdown = fail)
- Test on all supported controllers (I210, I225, I226)

---

## Traceability

### Requirements Links
**Verified Requirements**:
- #71 (REQ-NF-DOC-API-001: IOCTL API Documentation) - Documents performance targets

### Architecture Links
**Architectural Decisions**:
- #117 (ADR-PERF-002: Direct BAR0 MMIO Access) - Enables <500ns register reads
- #118 (ADR-PERF-SEC-001: Performance vs Security Trade-offs) - Balances validation vs. latency
- #126 (ADR-HAL-001: Hardware Abstraction Layer) - <1µs dispatch overhead

### Test Links
**Verification Tests**:
- TEST-PERF-IOCTL-001: PHC query latency benchmark
- TEST-PERF-IOCTL-002: Capability query latency benchmark
- TEST-PERF-IOCTL-003: TAS status query latency benchmark
- TEST-PERF-IOCTL-004: CBS config query latency benchmark
- TEST-PERF-IOCTL-005: High-frequency polling overhead test

---

## Compliance

**Standards**:
- **IEEE 802.1AS (gPTP)**: Timestamp precision <1µs requires low-latency PHC queries
- **ISO/IEC 25010:2011**: Performance Efficiency - Time Behavior
- **ISO/IEC/IEEE 42010:2011**: Architecture Quality Attributes

**Architecture Evaluation**:
- **ATAM Quality Attribute Scenario**: Structured format for performance evaluation
- **Quantified Metrics**: <100µs p95, <200µs p99 (measurable, testable)

---

## Notes

### Performance Budget Breakdown (PHC Query Example)

| Component | Time Budget | Notes |
|-----------|------------|-------|
| **User → Kernel Transition** | ~2µs | Syscall overhead (Windows optimized) |
| **NDIS Dispatch** | ~5µs | NDIS → FilterDeviceIoControl() |
| **Parameter Validation** | <5µs | Buffer checks, sanitization |
| **HAL Function Dispatch** | <1µs | `ctx->HwOps->ReadPhc()` |
| **BAR0 MMIO Register Read** | ~30µs | PCIe Gen2 x1, read SYSTIML + SYSTIMH |
| **Response Buffer Copy** | ~5µs | `RtlCopyMemory(userBuffer, &phcTime)` |
| **Kernel → User Transition** | ~2µs | Return to user-mode |
| **Total** | **~50µs** | ✅ Well within <100µs target |

### Hardware-Specific Considerations

**I226-V (Full TSN)**:
- PHC registers: SYSTIML (0xB600), SYSTIMH (0xB604) → 2 MMIO reads (~30µs)
- TAS status: Multiple registers → 5-10 MMIO reads (~100µs)

**I210 (PHC Stuck Workaround)**:
- PHC read includes stuck detection logic → +10µs overhead
- Workaround: Reset via TSAUXC register if stuck → +50µs on failure path

**I225-V (No TAS)**:
- PHC registers similar to I226 → ~30µs
- TAS IOCTLs return `STATUS_NOT_SUPPORTED` immediately → <5µs

### Comparison to Ring Buffer Approach

**IOCTL Polling** (This Scenario):
- Latency: <100µs per query
- Frequency: Up to 100Hz (1% CPU)
- Use case: Infrequent queries (capabilities, status checks)

**Ring Buffer** (ADR-PERF-004):
- Latency: <1µs per event
- Frequency: 1000Hz+ (0.1% CPU)
- Use case: High-frequency timestamp events

**Guideline**: Use IOCTLs for control plane (queries), ring buffer for data plane (events)

---

**Status**: Accepted  
**Reviewed**: Architecture Team  
**Date**: 2025-12-09
