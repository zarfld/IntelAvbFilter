# ISSUE #232 - CORRUPTED BACKUP (2025-12-31)

**Corruption Event**: 41st corruption in continuous range #192-232
**Pattern**: Performance Benchmark suite replaced with generic performance benchmarking test
**Analysis**: Content mismatch, wrong traceability, missing comprehensive NFR validation scope

---

## Test Description

**Test ID**: TEST-BENCHMARK-001
**Test Type**: Performance Benchmarking
**Test Level**: Tier 3 - System-Wide Performance
**Priority**: P1 (High)
**IEEE 1012-2016 Category**: Performance Testing

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #59 (REQ-NF-PERFORMANCE-001: Performance and Latency Requirements)
- Related Requirements: #2, #3, #5, #6, #7, #8, #9

## Test Objective

Establish comprehensive performance baseline and benchmark suite for AVB/TSN driver operations across all supported hardware platforms and configurations.

## Test Scope

### In Scope
- **PTP Clock Operations**: Get/set time, frequency adjustment (<10µs)
- **Timestamping Performance**: Tx/Rx hardware timestamping (<5µs)
- **IOCTL Latency**: All 25 IOCTLs under normal and peak load
- **TAS/CBS Throughput**: Queue management performance (>1Gbps)
- **Multi-Adapter Scaling**: Performance with 1-8 adapters
- **Memory Bandwidth**: Register access patterns and optimization
- **CPU Overhead**: Driver CPU usage (<5% per adapter)
- **Interrupt Latency**: ISR execution time (<50µs)

### Out of Scope
- Application-level AVB/TSN stack performance
- Network infrastructure performance (external switches)
- User-mode application overhead

## Prerequisites

**Hardware**: I210, I211, I219, I225, I226 adapters
**Configuration**: All adapters in AVB/TSN mode
**Tools**: Performance counters, ETW tracing, Windows Performance Analyzer
**Load**: Traffic generators (100Mbps - 10Gbps rates)

## Test Environment

### System Configuration
```yaml
OS: Windows 10/11 (latest)
CPU: Multi-core (4+ cores) with TSC support
Memory: 16GB+ RAM
NICs: Multiple Intel AVB-capable adapters
Tools:
  - Performance Monitor (perfmon)
  - Windows Performance Toolkit (WPT)
  - Intel VTune Profiler
  - Custom benchmark harness
```

### Traffic Patterns
```yaml
Baseline:
  - Idle system (no traffic)
  - Sustained 1Gbps AVB traffic
  - Burst traffic (0-10Gbps spikes)
  - Mixed PTP + AVB + best-effort

Stress:
  - Maximum hardware rate (line rate)
  - Multi-stream (8+ AVB streams)
  - High PTP message rate (>100 msg/s)
```

## Test Procedure

### Phase 1: Baseline Measurements

**Step 1.1**: PTP Clock Operations
```
FOR each adapter IN [I210, I219, I225, I226]:
  MEASURE GetSystemTime latency (1M iterations)
  MEASURE SetSystemTime latency (1K iterations)
  MEASURE FrequencyAdjustment latency (1K iterations)
  RECORD min/avg/max/p95/p99
```

**Step 1.2**: Timestamping Latency
```
FOR each adapter:
  CONFIGURE Tx/Rx timestamping
  SEND 100K packets with timestamps
  MEASURE timestamp retrieval latency
  VERIFY <5µs requirement
```

**Step 1.3**: IOCTL Performance Matrix
```
FOR each IOCTL IN [all 25 IOCTLs]:
  MEASURE execution time (cold cache)
  MEASURE execution time (warm cache)
  MEASURE concurrent IOCTL handling
  VERIFY no blocking operations
```

### Phase 2: Throughput and Scaling

**Step 2.1**: TAS/CBS Performance
```
CONFIGURE 8 priority queues
FOR traffic_rate IN [100Mbps, 500Mbps, 1Gbps, 10Gbps]:
  MEASURE queue latency per priority
  MEASURE bandwidth allocation accuracy
  MEASURE shaper overhead
  VERIFY >99% bandwidth utilization
```

**Step 2.2**: Multi-Adapter Scaling
```
FOR adapter_count IN [1, 2, 4, 8]:
  MEASURE aggregate throughput
  MEASURE CPU overhead per adapter
  MEASURE memory usage per adapter
  VERIFY linear scaling up to 4 adapters
```

### Phase 3: Resource Utilization

**Step 3.1**: CPU Overhead
```
ENABLE all AVB features (PTP, TAS, CBS, timestamping)
RUN sustained 1Gbps traffic for 1 hour
MEASURE:
  - Driver CPU time (kernel mode)
  - ISR execution time
  - DPC queue depth
  - Context switch overhead
VERIFY <5% CPU per adapter
```

**Step 3.2**: Memory Bandwidth
```
PROFILE register access patterns:
  - Memory-mapped I/O frequency
  - DMA descriptor updates
  - Shared memory ring buffer usage
IDENTIFY hotspots and optimization opportunities
```

### Phase 4: Interrupt Performance

**Step 4.1**: ISR Latency
```
MEASURE interrupt-to-ISR latency (ETW tracing)
MEASURE ISR execution time (all interrupt types):
  - Tx completion
  - Rx packet arrival
  - PTP timestamp capture
  - TAS gate control
VERIFY <50µs for soft real-time requirement
```

**Step 4.2**: Interrupt Coalescing
```
TEST various interrupt moderation settings
MEASURE:
  - Interrupt rate vs latency tradeoff
  - CPU overhead reduction
  - Timestamp precision impact
OPTIMIZE for AVB latency requirements
```

### Phase 5: Stress and Edge Cases

**Step 5.1**: Sustained Maximum Rate
```
RUN at line rate (10Gbps) for 1 hour
MONITOR:
  - Packet drop rate (<0.01%)
  - Timestamp accuracy (±100ns)
  - PTP sync stability (±50ns)
  - Memory leaks (no growth)
```

**Step 5.2**: Burst Traffic Handling
```
GENERATE bursty traffic pattern:
  - 10ms idle, 100ms full line rate (repeat)
VERIFY:
  - No buffer overflow
  - Timestamp continuity
  - PTP sync recovery <1s
```

## Expected Results

### Performance Baselines

| Operation | Target | Measurement |
|-----------|--------|-------------|
| PTP GetTime | <10µs | TBD |
| PTP SetTime | <100µs | TBD |
| Tx Timestamp | <5µs | TBD |
| Rx Timestamp | <5µs | TBD |
| IOCTL (avg) | <1ms | TBD |
| TAS Throughput | >1Gbps | TBD |
| CPU Overhead | <5%/adapter | TBD |
| ISR Latency | <50µs | TBD |

### Acceptance Criteria

✅ **PASS** if:
- All latency requirements met (95th percentile)
- Throughput >99% of theoretical maximum
- CPU overhead <5% per adapter
- No memory leaks over 1-hour stress test
- ISR execution time <50µs (99th percentile)
- Linear scaling up to 4 adapters (>80% efficiency)

❌ **FAIL** if:
- Any latency exceeds 2× target
- Throughput <95% of theoretical maximum
- CPU overhead >10% per adapter
- Memory leaks detected (>1MB/hour growth)
- ISR execution time >100µs (any measurement)

## Test Data Collection

### Metrics to Collect

```yaml
Latency Metrics:
  - Min/Avg/Max/P50/P95/P99/P999
  - Histogram distribution (1µs buckets)
  - Outlier analysis (>3σ)

Throughput Metrics:
  - Packets per second
  - Bytes per second
  - Goodput (application-level)
  - Hardware utilization %

Resource Metrics:
  - CPU time (user/kernel/ISR/DPC)
  - Memory usage (paged/non-paged)
  - DMA buffer utilization
  - Register access frequency
```

### Reporting Format

```markdown
# Performance Benchmark Report

## Executive Summary
- Hardware: [adapter models tested]
- Date: [test execution date]
- Pass/Fail: [overall result]

## Results Summary
[Table of all measurements vs targets]

## Detailed Analysis
[Per-operation breakdown with charts]

## Recommendations
[Optimization opportunities identified]
```

## Automation

**Test Harness**: `perf_benchmark_suite.exe`
**Execution**: Automated via CI/CD for release validation
**Duration**: ~4 hours full suite
**Frequency**: Weekly (continuous), per-release (full)

```powershell
# Example execution
.\perf_benchmark_suite.exe `
  --adapters I210,I219,I225,I226 `
  --duration 3600 `
  --output benchmark_results.json `
  --compare-baseline
```

## Dependencies

- Requires: Drivers installed and functional
- Requires: Performance counter infrastructure
- Requires: Traffic generation tools
- Requires: Baseline reference data (first run establishes baseline)

## Related Tests

- TEST-STRESS-7DAY-001: Long-term stability (builds on these benchmarks)
- TEST-PERF-REGRESSION-001: Regression detection (uses these baselines)
- TEST-MULTI-ADAPTER-001: Multi-adapter coordination (subset of these tests)

## Notes

- Baseline must be re-established after hardware/OS changes
- Performance may vary with background system load
- Isolate test system from network traffic during testing
- Disable power management for consistent results

---

**Status**: 🔴 Not Started
**Owner**: TBD
**Estimated Effort**: 3 days (initial setup) + 1 day (per-release execution)

---

## Corruption Analysis

**What was wrong**:
1. **Content mismatch**: Generic performance benchmarking test vs. comprehensive NFR validation suite
2. **Wrong traceability**: Links to #233 (TEST-PLAN-001) and #59 only, missing broader NFR coverage
3. **Missing scope**: Should validate ALL non-functional requirements (performance, security, compatibility, etc.)
4. **Missing labels**: Should have comprehensive-benchmark, nfr-validation, baseline labels
5. **Generic approach**: Detailed procedural steps vs. high-level benchmark objectives

**What should be there** (based on diff):
- Comprehensive performance benchmarking suite covering **all** NFRs
- Test objectives: Execute full benchmark suite, generate comprehensive report, validate against all NFRs
- Simplified 7-step process: latency benchmarks, CPU tests, memory footprint, interrupt latency, jitter, scalability, report compilation
- Expected results: All NFRs met/exceeded, performance documented, baseline established
- Acceptance criteria: Latency <1ms, CPU <5%, Memory <10MB, ISR <10µs, Jitter <2µs, Scaling validated
- Comprehensive automation and traceability to all NFRs

**Corruption pattern**: 41st corruption in continuous range #192-232
