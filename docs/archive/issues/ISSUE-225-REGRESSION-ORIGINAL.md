# TEST-REGRESSION-001: Performance Baseline and Regression Verification

**Issue**: #225
**Type**: Performance Testing, Regression Testing
**Priority**: P0 (Critical - Performance Baseline)
**Phase**: Phase 07 - Verification & Validation

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #59 (REQ-NF-PERFORMANCE-001: Performance and Scalability)
- Related Requirements: #14, #37, #38, #39
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Performance Baseline)

## 📋 Test Objective

Establish performance baselines and validate that no regression occurs across driver updates. Measure throughput, latency, CPU utilization, memory usage, and ensure all metrics meet or exceed requirements.

## 🧪 Test Coverage

### 10 Unit Tests

1. **Maximum throughput baseline** (measure max Gbps: 1000BASE-T target ≥950 Mbps)
2. **Minimum latency baseline** (Class A target <2ms, Class B <50ms)
3. **CPU utilization baseline** (driver overhead target <5% @ 1 Gbps)
4. **Memory footprint baseline** (driver memory <10 MB non-paged, <50 MB paged)
5. **PHC sync overhead** (PTP packet processing <10µs, sync accuracy ±100ns)
6. **TAS overhead** (gate control <5µs per cycle, <1% CPU)
7. **CBS overhead** (credit calculations <2µs per frame, <1% CPU)
8. **Interrupt rate** (ISR frequency <10K/sec @ 1 Gbps, <5µs per ISR)
9. **DPC overhead** (deferred processing <50µs per DPC)
10. **Lock contention** (spinlock hold time <1µs, zero deadlocks)

### 3 Integration Tests

1. **Full-load regression test** (1 Gbps sustained, all TSN features active, no performance drop vs. baseline)
2. **Multi-stream regression test** (4 Class A + 4 Class B + best-effort, latency within spec)
3. **Power efficiency regression** (EEE enabled, power reduction ≥20%, no throughput impact)

### 2 V&V Tests

1. **48-hour stress test** (continuous 1 Gbps traffic, monitor for performance degradation, CPU/memory stable)
2. **Version-to-version comparison** (compare v1.0 vs. v1.1 metrics, flag >5% regressions)

## 🔧 Implementation Notes

### Performance Metrics Collection

```c
typedef struct _PERF_METRICS {
    // Throughput
    UINT64 TxBytesPerSecond;      // Transmit throughput (bytes/sec)
    UINT64 RxBytesPerSecond;      // Receive throughput
    UINT32 TxFramesPerSecond;     // Transmit rate (frames/sec)
    UINT32 RxFramesPerSecond;     // Receive rate
    
    // Latency
    UINT64 MinLatencyNs;          // Minimum latency (nanoseconds)
    UINT64 MaxLatencyNs;          // Maximum latency
    UINT64 AvgLatencyNs;          // Average latency
    UINT64 P99LatencyNs;          // 99th percentile latency
    
    // CPU
    UINT32 CpuUsagePercent;       // Driver CPU usage (0-100%)
    UINT64 IsrTimeNs;             // Total ISR time per second
    UINT64 DpcTimeNs;             // Total DPC time per second
    
    // Memory
    SIZE_T NonPagedMemoryBytes;   // Non-paged pool allocated
    SIZE_T PagedMemoryBytes;      // Paged pool allocated
    SIZE_T StackMemoryBytes;      // Stack usage peak
    
    // Locks
    UINT64 SpinlockContentions;   // Contention events
    UINT64 SpinlockMaxHoldNs;     // Longest spinlock hold time
} PERF_METRICS;

VOID CollectPerfMetrics(ADAPTER_CONTEXT* adapter, PERF_METRICS* metrics) {
    // Throughput calculation (bytes per second)
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    UINT64 elapsedMs = ((now.QuadPart - adapter->PerfBaseline.LastSample.QuadPart) * 1000) / g_PerformanceFrequency.QuadPart;
    
    if (elapsedMs >= 1000) { // Sample every second
        UINT64 txBytes = adapter->Stats.TxBytes - adapter->PerfBaseline.LastTxBytes;
        UINT64 rxBytes = adapter->Stats.RxBytes - adapter->PerfBaseline.LastRxBytes;
        
        metrics->TxBytesPerSecond = (txBytes * 1000) / elapsedMs;
        metrics->RxBytesPerSecond = (rxBytes * 1000) / elapsedMs;
        
        metrics->TxFramesPerSecond = (UINT32)(((adapter->Stats.TxFrames - adapter->PerfBaseline.LastTxFrames) * 1000) / elapsedMs);
        metrics->RxFramesPerSecond = (UINT32)(((adapter->Stats.RxFrames - adapter->PerfBaseline.LastRxFrames) * 1000) / elapsedMs);
        
        // Update baseline
        adapter->PerfBaseline.LastSample = now;
        adapter->PerfBaseline.LastTxBytes = adapter->Stats.TxBytes;
        adapter->PerfBaseline.LastRxBytes = adapter->Stats.RxBytes;
        adapter->PerfBaseline.LastTxFrames = adapter->Stats.TxFrames;
        adapter->PerfBaseline.LastRxFrames = adapter->Stats.RxFrames;
    }
    
    // Latency statistics (from histogram)
    metrics->MinLatencyNs = adapter->LatencyHist.MinLatency;
    metrics->MaxLatencyNs = adapter->LatencyHist.MaxLatency;
    metrics->AvgLatencyNs = adapter->LatencyHist.TotalSamples > 0 ? 
        adapter->LatencyHist.SumLatency / adapter->LatencyHist.TotalSamples : 0;
    
    // 99th percentile (from histogram buckets)
    UINT64 p99Threshold = (adapter->LatencyHist.TotalSamples * 99) / 100;
    UINT64 cumulative = 0;
    for (UINT32 i = 0; i < 10; i++) {
        cumulative += adapter->LatencyHist.Buckets[i];
        if (cumulative >= p99Threshold) {
            metrics->P99LatencyNs = adapter->LatencyHist.BucketRanges[i];
            break;
        }
    }
    
    // CPU usage (simplified estimation)
    metrics->IsrTimeNs = adapter->PerfCounters.TotalIsrTimeNs;
    metrics->DpcTimeNs = adapter->PerfCounters.TotalDpcTimeNs;
    metrics->CpuUsagePercent = (UINT32)(((metrics->IsrTimeNs + metrics->DpcTimeNs) * 100) / (elapsedMs * 1000000));
    
    // Memory usage (query pool tags)
    metrics->NonPagedMemoryBytes = adapter->MemoryStats.NonPagedBytes;
    metrics->PagedMemoryBytes = adapter->MemoryStats.PagedBytes;
}
```

### Regression Detection

```c
typedef struct _PERF_BASELINE {
    CHAR Version[32];             // Driver version (e.g., "1.0.0")
    PERF_METRICS Metrics;         // Baseline metrics
    LARGE_INTEGER Timestamp;      // When baseline was captured
} PERF_BASELINE;

typedef enum _REGRESSION_SEVERITY {
    REGRESSION_NONE,              // No regression
    REGRESSION_MINOR,             // <5% degradation (warning)
    REGRESSION_MODERATE,          // 5-10% degradation (error)
    REGRESSION_CRITICAL           // >10% degradation (fail test)
} REGRESSION_SEVERITY;

REGRESSION_SEVERITY DetectRegression(PERF_BASELINE* baseline, PERF_METRICS* current, CHAR* details, UINT32 detailsLen) {
    REGRESSION_SEVERITY worst = REGRESSION_NONE;
    
    // Throughput regression
    if (current->TxBytesPerSecond < baseline->Metrics.TxBytesPerSecond) {
        INT32 degradationPercent = (INT32)(((baseline->Metrics.TxBytesPerSecond - current->TxBytesPerSecond) * 100) / baseline->Metrics.TxBytesPerSecond);
        
        if (degradationPercent > 10) {
            worst = REGRESSION_CRITICAL;
            snprintf(details, detailsLen, "CRITICAL: Throughput degraded %d%% (baseline=%llu, current=%llu)",
                degradationPercent, baseline->Metrics.TxBytesPerSecond, current->TxBytesPerSecond);
        } else if (degradationPercent > 5) {
            worst = REGRESSION_MODERATE;
            snprintf(details, detailsLen, "MODERATE: Throughput degraded %d%%", degradationPercent);
        } else {
            worst = REGRESSION_MINOR;
        }
    }
    
    // Latency regression
    if (current->AvgLatencyNs > baseline->Metrics.AvgLatencyNs) {
        INT32 increasePercent = (INT32)(((current->AvgLatencyNs - baseline->Metrics.AvgLatencyNs) * 100) / baseline->Metrics.AvgLatencyNs);
        
        if (increasePercent > 10) {
            worst = max(worst, REGRESSION_CRITICAL);
            snprintf(details + strlen(details), detailsLen - strlen(details),
                " | Latency increased %d%% (baseline=%lluns, current=%lluns)",
                increasePercent, baseline->Metrics.AvgLatencyNs, current->AvgLatencyNs);
        }
    }
    
    // CPU regression
    if (current->CpuUsagePercent > baseline->Metrics.CpuUsagePercent + 2) { // Absolute +2% threshold
        worst = max(worst, REGRESSION_MODERATE);
        snprintf(details + strlen(details), detailsLen - strlen(details),
            " | CPU increased %d%% (baseline=%u%%, current=%u%%)",
            current->CpuUsagePercent - baseline->Metrics.CpuUsagePercent,
            baseline->Metrics.CpuUsagePercent, current->CpuUsagePercent);
    }
    
    return worst;
}
```

### Automated Baseline Capture

```c
NTSTATUS CapturePerformanceBaseline(ADAPTER_CONTEXT* adapter, CHAR* version) {
    PERF_BASELINE baseline;
    RtlZeroMemory(&baseline, sizeof(PERF_BASELINE));
    
    strncpy(baseline.Version, version, sizeof(baseline.Version) - 1);
    
    // Run traffic generator for 60 seconds
    DbgPrint("Capturing baseline: Starting 60-second traffic run...\n");
    
    // Generate full-load traffic (requires external traffic generator)
    // Measure metrics during this period
    for (UINT32 i = 0; i < 60; i++) {
        Sleep(1000);
        CollectPerfMetrics(adapter, &baseline.Metrics);
    }
    
    KeQueryPerformanceCounter(&baseline.Timestamp);
    
    // Save baseline to registry or file
    SaveBaselineToRegistry(&baseline);
    
    DbgPrint("Baseline captured: Throughput=%llu Bps, Latency=%lluns, CPU=%u%%\n",
        baseline.Metrics.TxBytesPerSecond, baseline.Metrics.AvgLatencyNs, baseline.Metrics.CpuUsagePercent);
    
    return STATUS_SUCCESS;
}
```

## 📊 Performance Targets (Baseline Requirements)

| Metric                       | Target              | Measurement Method                        |
|------------------------------|---------------------|-------------------------------------------|
| Maximum Throughput           | ≥950 Mbps           | Sustained 1000BASE-T traffic              |
| Class A Latency              | <2 ms               | End-to-end measurement                    |
| Class B Latency              | <50 ms              | End-to-end measurement                    |
| CPU Utilization              | <5% @ 1 Gbps        | Driver-specific CPU time                  |
| Non-Paged Memory             | <10 MB              | Pool tag query                            |
| Paged Memory                 | <50 MB              | Pool tag query                            |
| ISR Frequency                | <10K/sec            | Interrupt count per second                |
| ISR Duration                 | <5µs                | GPIO toggle measurement                   |
| DPC Duration                 | <50µs               | High-resolution timer                     |
| Spinlock Hold Time           | <1µs                | Lock contention profiler                  |

## 📊 Regression Thresholds

| Severity      | Throughput   | Latency      | CPU Usage    | Action Required                |
|---------------|--------------|--------------|--------------|--------------------------------|
| **None**      | ±0-3%        | ±0-3%        | ±0-1%        | Pass (normal variation)        |
| **Minor**     | ±3-5%        | ±3-5%        | ±1-2%        | Warning (review changes)       |
| **Moderate**  | ±5-10%       | ±5-10%       | ±2-5%        | Error (requires investigation) |
| **Critical**  | >10%         | >10%         | >5%          | Fail test (block release)      |

## ✅ Acceptance Criteria

### Baseline Capture
- ✅ Baseline captured for each release version
- ✅ All metrics collected: throughput, latency, CPU, memory
- ✅ Baseline saved to persistent storage (registry/file)

### Performance Requirements
- ✅ Throughput ≥950 Mbps sustained
- ✅ Class A latency <2 ms
- ✅ Class B latency <50 ms
- ✅ CPU usage <5% @ 1 Gbps
- ✅ Memory footprint <10 MB non-paged

### Regression Detection
- ✅ Version-to-version comparison automated
- ✅ >10% degradation fails test
- ✅ 5-10% degradation logged as error
- ✅ <5% degradation accepted

### Stress Testing
- ✅ 48-hour test with zero crashes
- ✅ CPU/memory stable (no leaks)
- ✅ Throughput/latency within spec throughout

### Reporting
- ✅ Detailed metrics logged (CSV format)
- ✅ Regression report generated
- ✅ Charts comparing baseline vs. current

## 🔗 References

- #59: REQ-NF-PERFORMANCE-001 - Performance Requirements
- #37: REQ-F-CLASS-A-001 - Class A Stream Latency
- #38: REQ-F-CLASS-B-001 - Class B Stream Latency
- Windows Performance Toolkit (WPT) for profiling
