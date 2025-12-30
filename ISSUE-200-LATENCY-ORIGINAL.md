# Issue #200 - Original Content (Restored)

**Issue**: #200  
**Title**: [TEST] TEST-PERF-PHC-001: PHC Read Latency Verification  
**Test ID**: TEST-PERF-PHC-001  
**Priority**: P0 (Critical)  
**Test Type**: Unit, Integration, V&V  
**Phase**: 07-verification-validation  

---

## 🔗 Traceability
- Traces to: #58 (REQ-NF-PERF-PHC-001: PHC Read Latency <500ns)
- Verifies: #58
- Related Requirements: #2 (REQ-F-PTP-001: PHC Get/Set), #34 (REQ-F-IOCTL-PHC-001: PHC Query IOCTL)
- Related Quality Scenarios: #110 (QA-SC-PERF-001: PHC Performance)

---

## 📋 Test Objective

Validates **PHC read latency <500ns** requirement for kernel-mode PHC access. Verifies:

1. **Kernel-mode PHC read latency <500ns** (target: <200ns typical, <500ns worst-case)
2. **User-mode IOCTL latency <10µs** (includes kernel transition overhead)
3. **Latency stability**: Standard deviation <50ns (minimal jitter)
4. **No performance degradation** under concurrent access (10 threads)
5. **High-frequency sampling**: Sustained 1 MHz PHC read rate (1M reads/second)
6. **Cache effects**: First read vs subsequent reads (cold vs warm cache)

---

## 🎯 Test Coverage

### **10 Unit Tests** (Google Test, Mock NDIS)

#### UT-PERF-PHC-001: Single PHC Read Latency (Kernel-Mode)
**Objective**: Verify single kernel-mode PHC read latency <500ns.

**Test Steps**:
1. Warm up cache: Perform 100 PHC reads (discard results)
2. Measure 10,000 PHC reads with high-resolution timer:
   ```cpp
   start = QueryPerformanceCounter()
   phc = ReadPhcTimestamp()
   end = QueryPerformanceCounter()
   latency = (end - start) * 1e9 / QPF
   ```
3. Calculate statistics:
   - Mean latency
   - Standard deviation (jitter)
   - Min/Max latency
   - 99th percentile latency

**Expected Result**:
- Mean latency <200ns
- Max latency <500ns
- 99th percentile <300ns
- Standard deviation <50ns

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

// Warm-up
for (int i = 0; i < 100; i++) {
    ReadPhcTimestamp();
}

// Measure
for (int i = 0; i < 10000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
}

double mean = CalculateMean(latencies);
double stddev = CalculateStdDev(latencies);
double percentile99 = CalculatePercentile(latencies, 99);
double maxLatency = *std::max_element(latencies.begin(), latencies.end());

EXPECT_LT(mean, 200);          // Mean <200ns
EXPECT_LT(maxLatency, 500);    // Max <500ns
EXPECT_LT(percentile99, 300);  // 99th percentile <300ns
EXPECT_LT(stddev, 50);         // Jitter <50ns
```

---

#### UT-PERF-PHC-002: Cold Cache vs Warm Cache Latency
**Objective**: Measure PHC read latency difference between cold and warm cache.

**Test Steps**:
1. Flush CPU cache (use cache flush instruction or large memory allocation)
2. Measure first PHC read (cold cache): `latencyCold`
3. Measure 100 subsequent PHC reads (warm cache): `latenciesWarm[]`
4. Calculate mean warm cache latency: `meanWarm`
5. Compare: `latencyCold` vs `meanWarm`

**Expected Result**:
- Cold cache latency <500ns (still meets requirement)
- Warm cache latency <200ns (typical case)
- Cold cache penalty <300ns (acceptable for first read)

**Acceptance Criteria**:
```cpp
FlushCpuCache();
LARGE_INTEGER start, end, qpf;
QueryPerformanceFrequency(&qpf);

// Cold cache read
QueryPerformanceCounter(&start);
UINT64 phcCold = ReadPhcTimestamp();
QueryPerformanceCounter(&end);
double latencyCold = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;

EXPECT_LT(latencyCold, 500); // Cold <500ns

// Warm cache reads
std::vector<double> latenciesWarm;
for (int i = 0; i < 100; i++) {
    QueryPerformanceCounter(&start);
    UINT64 phcWarm = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latenciesWarm.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double meanWarm = CalculateMean(latenciesWarm);
EXPECT_LT(meanWarm, 200);                      // Warm <200ns
EXPECT_LT(latencyCold - meanWarm, 300);        // Cold penalty <300ns
```

---

#### UT-PERF-PHC-003: Concurrent Access Latency (10 Threads)
**Objective**: Verify PHC read latency remains <500ns under concurrent access from 10 threads.

**Test Steps**:
1. Spawn 10 threads
2. Each thread measures 1000 PHC reads with latency timing
3. Aggregate all 10,000 measurements
4. Verify statistics:
   - Mean <200ns
   - Max <500ns
   - No thread starvation (each thread completes in reasonable time)

**Expected Result**:
- All threads achieve <200ns mean latency
- No thread exceeds 500ns max latency
- No lock contention degradation

**Acceptance Criteria**:
```cpp
std::vector<std::vector<double>> threadLatencies(10);
std::vector<std::thread> threads;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int t = 0; t < 10; t++) {
    threads.emplace_back([&, t]() {
        for (int i = 0; i < 1000; i++) {
            LARGE_INTEGER start, end;
            QueryPerformanceCounter(&start);
            UINT64 phc = ReadPhcTimestamp();
            QueryPerformanceCounter(&end);
            
            double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
            threadLatencies[t].push_back(latencyNs);
        }
    });
}

for (auto& th : threads) th.join();

// Verify each thread's performance
for (int t = 0; t < 10; t++) {
    double mean = CalculateMean(threadLatencies[t]);
    double maxLatency = *std::max_element(threadLatencies[t].begin(), threadLatencies[t].end());
    
    EXPECT_LT(mean, 200);       // Mean <200ns per thread
    EXPECT_LT(maxLatency, 500); // Max <500ns per thread
}
```

---

#### UT-PERF-PHC-004: High-Frequency Sampling (1 MHz Sustained)
**Objective**: Verify PHC can sustain 1 MHz read rate (1M reads/second) with latency <500ns.

**Test Steps**:
1. Perform 1,000,000 PHC reads in tight loop
2. Measure total elapsed time
3. Calculate average rate: `rate = 1,000,000 / elapsed_seconds`
4. Sample latency every 1000th read (1000 samples total)
5. Verify:
   - Sustained rate ≥ 1 MHz
   - Sampled latencies remain <500ns

**Expected Result**:
- Sustained rate ≥ 1 MHz (elapsed time ≤ 1 second)
- All sampled latencies <500ns (no degradation)

**Acceptance Criteria**:
```cpp
LARGE_INTEGER start, end, qpf;
QueryPerformanceFrequency(&qpf);
std::vector<double> sampledLatencies;

QueryPerformanceCounter(&start);
for (int i = 0; i < 1000000; i++) {
    LARGE_INTEGER readStart, readEnd;
    
    if (i % 1000 == 0) { // Sample every 1000th read
        QueryPerformanceCounter(&readStart);
    }
    
    UINT64 phc = ReadPhcTimestamp();
    
    if (i % 1000 == 0) {
        QueryPerformanceCounter(&readEnd);
        double latency = (double)(readEnd.QuadPart - readStart.QuadPart) * 1e9 / qpf.QuadPart;
        sampledLatencies.push_back(latency);
    }
}
QueryPerformanceCounter(&end);

// Verify sustained rate
double elapsedSeconds = (double)(end.QuadPart - start.QuadPart) / qpf.QuadPart;
double rateHz = 1000000.0 / elapsedSeconds;
EXPECT_GE(rateHz, 1000000); // ≥ 1 MHz

// Verify sampled latencies
for (double latency : sampledLatencies) {
    EXPECT_LT(latency, 500); // All samples <500ns
}
```

---

#### UT-PERF-PHC-005: Latency Jitter Analysis
**Objective**: Verify PHC read latency jitter (standard deviation) <50ns.

**Test Steps**:
1. Perform 10,000 PHC reads with latency measurement
2. Calculate jitter metrics:
   - Standard deviation
   - Interquartile range (IQR)
   - Coefficient of variation: `CV = stddev / mean`
3. Verify low jitter indicates consistent performance

**Expected Result**:
- Standard deviation <50ns
- IQR <100ns
- CV <0.3 (30% variation)

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 10000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double mean = CalculateMean(latencies);
double stddev = CalculateStdDev(latencies);
double q1 = CalculatePercentile(latencies, 25);
double q3 = CalculatePercentile(latencies, 75);
double iqr = q3 - q1;
double cv = stddev / mean;

EXPECT_LT(stddev, 50);  // Jitter <50ns
EXPECT_LT(iqr, 100);    // IQR <100ns
EXPECT_LT(cv, 0.3);     // CV <30%
```

---

#### UT-PERF-PHC-006: Latency vs Hardware Access Pattern
**Objective**: Measure latency impact of different hardware access patterns.

**Test Steps**:
1. Test sequential reads: 1000 consecutive PHC reads
2. Test interleaved reads: PHC read + dummy register read (alternating)
3. Test burst reads: 10 PHC reads, 100µs delay, repeat
4. Compare latencies across patterns

**Expected Result**:
- All patterns achieve <500ns max latency
- Sequential reads fastest (<200ns mean)
- Interleaved reads slightly slower (register context switch overhead)
- Burst reads consistent (no warmup penalty after first read)

**Acceptance Criteria**:
```cpp
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

// Sequential reads
std::vector<double> seqLatencies;
for (int i = 0; i < 1000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    seqLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

// Interleaved reads (PHC + dummy register)
std::vector<double> interleavedLatencies;
for (int i = 0; i < 1000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    interleavedLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    
    ReadDummyRegister(); // Interleave with other register access
}

// Burst reads
std::vector<double> burstLatencies;
for (int burst = 0; burst < 100; burst++) {
    for (int i = 0; i < 10; i++) {
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        UINT64 phc = ReadPhcTimestamp();
        QueryPerformanceCounter(&end);
        burstLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    }
    SleepMicroseconds(100); // 100µs delay between bursts
}

// All patterns meet requirement
EXPECT_LT(*std::max_element(seqLatencies.begin(), seqLatencies.end()), 500);
EXPECT_LT(*std::max_element(interleavedLatencies.begin(), interleavedLatencies.end()), 500);
EXPECT_LT(*std::max_element(burstLatencies.begin(), burstLatencies.end()), 500);

// Sequential is fastest
EXPECT_LT(CalculateMean(seqLatencies), 200);
```

---

#### UT-PERF-PHC-007: Latency After Frequency Adjustment
**Objective**: Verify PHC read latency unaffected by frequency adjustment.

**Test Steps**:
1. Measure baseline latency (1000 reads): `latenciesBaseline[]`
2. Apply frequency adjustment: `SetPhcFrequencyAdjustment(+100 PPM)`
3. Measure latency during adjustment (1000 reads): `latenciesAdjusted[]`
4. Compare: `meanBaseline` vs `meanAdjusted`

**Expected Result**:
- No latency degradation during frequency adjustment
- Both means <200ns
- Difference <10ns (negligible impact)

**Acceptance Criteria**:
```cpp
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

// Baseline latency
std::vector<double> latenciesBaseline;
for (int i = 0; i < 1000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latenciesBaseline.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

// Apply frequency adjustment
SetPhcFrequencyAdjustment(100); // +100 PPM

// Adjusted latency
std::vector<double> latenciesAdjusted;
for (int i = 0; i < 1000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latenciesAdjusted.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double meanBaseline = CalculateMean(latenciesBaseline);
double meanAdjusted = CalculateMean(latenciesAdjusted);

EXPECT_LT(meanBaseline, 200);
EXPECT_LT(meanAdjusted, 200);
EXPECT_LT(fabs(meanAdjusted - meanBaseline), 10); // <10ns difference
```

---

#### UT-PERF-PHC-008: Latency After Offset Adjustment
**Objective**: Verify PHC read latency unaffected by offset adjustment.

**Test Steps**:
1. Measure baseline latency (1000 reads)
2. Apply offset adjustment: `AdjustPhcOffset(+1000000)` (+1ms)
3. Measure latency after adjustment (1000 reads)
4. Verify no degradation

**Expected Result**:
- No latency impact from offset adjustment
- Both means <200ns

**Acceptance Criteria**:
```cpp
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

// Baseline
std::vector<double> latenciesBaseline;
for (int i = 0; i < 1000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latenciesBaseline.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

// Offset adjustment
AdjustPhcOffset(1000000); // +1ms

// After adjustment
std::vector<double> latenciesAfter;
for (int i = 0; i < 1000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latenciesAfter.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double meanBaseline = CalculateMean(latenciesBaseline);
double meanAfter = CalculateMean(latenciesAfter);

EXPECT_LT(meanBaseline, 200);
EXPECT_LT(meanAfter, 200);
EXPECT_NEAR(meanBaseline, meanAfter, 10); // Negligible difference
```

---

#### UT-PERF-PHC-009: Latency Histogram Distribution
**Objective**: Verify PHC read latency follows tight distribution (minimal outliers).

**Test Steps**:
1. Perform 10,000 PHC reads with latency measurement
2. Build histogram with 10ns bins: [0-10ns, 10-20ns, ..., 490-500ns]
3. Analyze distribution:
   - 90% of samples within 200ns
   - 99% of samples within 300ns
   - <1% outliers >500ns (acceptable for anomalies)

**Expected Result**:
- Tight distribution centered around 150-200ns
- Minimal tail (few outliers)

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 10000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

// Sort for percentile calculation
std::sort(latencies.begin(), latencies.end());

double p90 = latencies[9000];  // 90th percentile
double p99 = latencies[9900];  // 99th percentile
int countOver500 = std::count_if(latencies.begin(), latencies.end(), [](double l) { return l > 500; });

EXPECT_LT(p90, 200);           // 90% within 200ns
EXPECT_LT(p99, 300);           // 99% within 300ns
EXPECT_LT(countOver500, 100);  // <1% outliers >500ns
```

---

#### UT-PERF-PHC-010: Latency Regression Test
**Objective**: Verify PHC read latency has not regressed compared to baseline.

**Test Steps**:
1. Load baseline performance data (from reference build)
2. Measure current build performance (10,000 reads)
3. Compare:
   - Mean latency: current vs baseline
   - Max latency: current vs baseline
4. Verify no regression (current ≤ baseline + 10% tolerance)

**Expected Result**:
- No performance regression
- Current performance matches or exceeds baseline

**Acceptance Criteria**:
```cpp
// Baseline data (from reference build)
const double BASELINE_MEAN_NS = 180.0;
const double BASELINE_MAX_NS = 450.0;

LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);
std::vector<double> latencies;

for (int i = 0; i < 10000; i++) {
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double currentMean = CalculateMean(latencies);
double currentMax = *std::max_element(latencies.begin(), latencies.end());

// No regression (10% tolerance)
EXPECT_LT(currentMean, BASELINE_MEAN_NS * 1.1);  // Mean within 10%
EXPECT_LT(currentMax, BASELINE_MAX_NS * 1.1);    // Max within 10%
```

---

### **3 Integration Tests** (Google Test + Mock NDIS + User-Mode Harness)

#### IT-PERF-PHC-001: User-Mode IOCTL Latency
**Objective**: Verify user-mode IOCTL_AVB_PHC_QUERY latency <10µs (includes kernel transition).

**Test Steps**:
1. Open device handle: `\\.\IntelAvbFilter0`
2. Measure 1000 DeviceIoControl(IOCTL_AVB_PHC_QUERY) calls:
   ```cpp
   start = QueryPerformanceCounter()
   DeviceIoControl(IOCTL_AVB_PHC_QUERY, ...)
   end = QueryPerformanceCounter()
   latency = (end - start) * 1e9 / QPF
   ```
3. Calculate statistics (mean, max, 99th percentile)

**Expected Result**:
- Mean latency <5µs
- Max latency <10µs
- 99th percentile <7µs

**Acceptance Criteria**:
```cpp
HANDLE hDevice = CreateFile(L"\\\\.\\IntelAvbFilter0", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);
std::vector<double> latencies;

for (int i = 0; i < 1000; i++) {
    UINT64 phcTimestamp;
    DWORD bytesReturned;
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    BOOL success = DeviceIoControl(hDevice, IOCTL_AVB_PHC_QUERY, NULL, 0, &phcTimestamp, sizeof(phcTimestamp), &bytesReturned, NULL);
    QueryPerformanceCounter(&end);
    
    EXPECT_TRUE(success);
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
}

double mean = CalculateMean(latencies);
double maxLatency = *std::max_element(latencies.begin(), latencies.end());
double p99 = CalculatePercentile(latencies, 99);

EXPECT_LT(mean, 5000);       // Mean <5µs
EXPECT_LT(maxLatency, 10000); // Max <10µs
EXPECT_LT(p99, 7000);        // 99th percentile <7µs

CloseHandle(hDevice);
```

---

#### IT-PERF-PHC-002: gPTP Stack PHC Query Performance
**Objective**: Verify gPTP stack can query PHC at 8 Hz (125ms intervals) with latency <500ns.

**Test Steps**:
1. Configure gPTP slave mode (sync rate: 8 Hz)
2. For each gPTP sync interval (100 iterations):
   - Measure PHC query latency during sync processing
   - Verify latency <500ns (doesn't delay gPTP sync)
3. Verify gPTP synchronization accuracy <1µs (latency doesn't impact sync quality)

**Expected Result**:
- All PHC queries <500ns
- gPTP sync accuracy <1µs (unaffected by PHC latency)

**Acceptance Criteria**:
```cpp
EnableGptpSlave();
std::vector<double> phcLatencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 100; i++) {
    // Wait for gPTP sync interval
    WaitForGptpSyncEvent();
    
    // Measure PHC query during sync processing
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    phcLatencies.push_back(latencyNs);
    
    // Verify gPTP sync accuracy
    INT64 offset = GetGptpOffset();
    if (i > 10) { // After settling
        EXPECT_LT(llabs(offset), 1000); // <1µs
    }
}

// Verify PHC latency
for (double latency : phcLatencies) {
    EXPECT_LT(latency, 500); // All <500ns
}
```

---

#### IT-PERF-PHC-003: Multi-Adapter PHC Read Performance
**Objective**: Verify PHC read latency <500ns across 4 independent adapters.

**Test Steps**:
1. Configure 4 adapters with independent PHCs
2. For each adapter:
   - Measure 1000 PHC reads
   - Calculate mean latency
3. Verify all adapters achieve <200ns mean, <500ns max

**Expected Result**:
- All 4 adapters: mean <200ns, max <500ns
- No cross-adapter interference

**Acceptance Criteria**:
```cpp
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int adapter = 0; adapter < 4; adapter++) {
    std::vector<double> latencies;
    
    for (int i = 0; i < 1000; i++) {
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        UINT64 phc = ReadAdapterPhcTimestamp(adapter);
        QueryPerformanceCounter(&end);
        latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    }
    
    double mean = CalculateMean(latencies);
    double maxLatency = *std::max_element(latencies.begin(), latencies.end());
    
    EXPECT_LT(mean, 200);       // Mean <200ns per adapter
    EXPECT_LT(maxLatency, 500); // Max <500ns per adapter
}
```

---

### **2 V&V Tests** (User-Mode Harness, Quantified Metrics)

#### VV-PERF-PHC-001: Production Workload Performance (24 Hours)
**Objective**: Verify PHC read latency <500ns maintained over 24-hour production workload.

**Test Steps**:
1. Run production gPTP synchronization for 24 hours
2. Sample PHC read latency every 10 seconds (8640 samples)
3. Verify statistics:
   - Mean latency <200ns (no degradation)
   - Max latency <500ns (no spikes)
   - Standard deviation <50ns (stable)
   - No performance drift (first hour vs last hour)

**Expected Result**:
- Latency stable over 24 hours
- No memory leaks or resource exhaustion

**Acceptance Criteria**:
```cpp
EnableGptpSlave();
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

auto startTime = std::chrono::system_clock::now();
while (std::chrono::duration_cast<std::chrono::hours>(
    std::chrono::system_clock::now() - startTime).count() < 24) {
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 phc = ReadPhcTimestamp();
    QueryPerformanceCounter(&end);
    
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
    
    Sleep(10000); // 10-second sampling
}

EXPECT_EQ(8640, latencies.size()); // 24 hours worth
EXPECT_LT(CalculateMean(latencies), 200); // Mean <200ns
EXPECT_LT(*std::max_element(latencies.begin(), latencies.end()), 500); // Max <500ns
EXPECT_LT(CalculateStdDev(latencies), 50); // Jitter <50ns

// No drift
double meanFirst360 = CalculateMean(std::vector<double>(latencies.begin(), latencies.begin() + 360)); // First hour
double meanLast360 = CalculateMean(std::vector<double>(latencies.end() - 360, latencies.end())); // Last hour
EXPECT_NEAR(meanFirst360, meanLast360, 10); // <10ns drift
```

---

#### VV-PERF-PHC-002: Stress Test (10M Reads, High Concurrency)
**Objective**: Verify PHC performance under extreme stress (10M reads, 100 threads).

**Test Steps**:
1. Spawn 100 threads
2. Each thread performs 100,000 PHC reads (10M total)
3. Measure:
   - Total time (should complete in <10 seconds for 1M reads/sec aggregate)
   - Per-thread latency statistics
4. Verify:
   - All threads: mean <200ns, max <500ns
   - No deadlocks or crashes
   - No resource exhaustion

**Expected Result**:
- 10M reads complete successfully
- All threads meet latency requirements
- System remains stable

**Acceptance Criteria**:
```cpp
std::vector<std::vector<double>> threadLatencies(100);
std::vector<std::thread> threads;
std::atomic<int> successCount{0};
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

auto startTime = std::chrono::high_resolution_clock::now();

for (int t = 0; t < 100; t++) {
    threads.emplace_back([&, t]() {
        for (int i = 0; i < 100000; i++) {
            LARGE_INTEGER start, end;
            QueryPerformanceCounter(&start);
            UINT64 phc = ReadPhcTimestamp();
            QueryPerformanceCounter(&end);
            
            double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
            
            if (i % 1000 == 0) { // Sample every 1000th
                threadLatencies[t].push_back(latencyNs);
            }
            
            if (latencyNs < 500) {
                successCount++;
            }
        }
    });
}

for (auto& th : threads) th.join();
auto endTime = std::chrono::high_resolution_clock::now();

double elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

// Verify throughput
EXPECT_LT(elapsedSeconds, 10); // Complete in <10 seconds

// Verify per-thread latency
for (int t = 0; t < 100; t++) {
    if (!threadLatencies[t].empty()) {
        double mean = CalculateMean(threadLatencies[t]);
        EXPECT_LT(mean, 200); // Mean <200ns
    }
}

// Verify success rate
EXPECT_GT(successCount.load(), 9900000); // >99% success rate
```

---

## 🎯 Acceptance Criteria

1. **Kernel-Mode Performance**:
   - ✅ Mean latency <200ns (typical)
   - ✅ Max latency <500ns (worst-case)
   - ✅ 99th percentile <300ns
   - ✅ Standard deviation <50ns (jitter)

2. **User-Mode IOCTL Performance**:
   - ✅ Mean latency <5µs
   - ✅ Max latency <10µs
   - ✅ 99th percentile <7µs

3. **Concurrency**:
   - ✅ 10 threads: all achieve <200ns mean
   - ✅ 100 threads: all achieve <500ns max (stress test)

4. **Sustained Throughput**:
   - ✅ 1 MHz sustained rate (1M reads/second)
   - ✅ No degradation over time

5. **Stability**:
   - ✅ 24-hour production workload: no drift, no leaks
   - ✅ Cold cache <500ns, warm cache <200ns

6. **Robustness**:
   - ✅ No impact from frequency/offset adjustments
   - ✅ Tight distribution (90% within 200ns)
   - ✅ No regression vs baseline

7. **Traceability**:
   - ✅ All tests reference requirement #58
   - ✅ Test results logged with issue references

---

## 📊 Test Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Kernel-Mode Mean Latency | <200ns | Average of 10,000 reads |
| Kernel-Mode Max Latency | <500ns | Max of 10,000 reads |
| Kernel-Mode 99th Percentile | <300ns | Sorted latencies[9900] |
| Kernel-Mode Jitter (Stddev) | <50ns | Standard deviation |
| User-Mode IOCTL Mean | <5µs | DeviceIoControl timing |
| User-Mode IOCTL Max | <10µs | Max IOCTL latency |
| Cold Cache Latency | <500ns | First read after cache flush |
| Warm Cache Latency | <200ns | Subsequent reads |
| Sustained Throughput | ≥1 MHz | 1M reads / elapsed time |
| 24-Hour Stability | No drift >10ns | First hour vs last hour |
| Stress Test (10M reads, 100 threads) | >99% success | Reads <500ns / total |

---

## 🛠️ Test Implementation

### High-Resolution Timing
```cpp
// Microsecond-precision timing wrapper
class HighResolutionTimer {
public:
    HighResolutionTimer() {
        QueryPerformanceFrequency(&frequency_);
    }
    
    void Start() {
        QueryPerformanceCounter(&start_);
    }
    
    double ElapsedNanoseconds() {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        return (double)(end.QuadPart - start_.QuadPart) * 1e9 / frequency_.QuadPart;
    }
    
private:
    LARGE_INTEGER frequency_;
    LARGE_INTEGER start_;
};

// Usage
HighResolutionTimer timer;
timer.Start();
UINT64 phc = ReadPhcTimestamp();
double latencyNs = timer.ElapsedNanoseconds();
```

### Statistical Analysis Helpers
```cpp
double CalculateMean(const std::vector<double>& values) {
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double CalculateStdDev(const std::vector<double>& values) {
    double mean = CalculateMean(values);
    double variance = 0.0;
    for (double v : values) {
        variance += (v - mean) * (v - mean);
    }
    return sqrt(variance / values.size());
}

double CalculatePercentile(std::vector<double> values, int percentile) {
    std::sort(values.begin(), values.end());
    size_t index = (values.size() * percentile) / 100;
    return values[index];
}
```

### Cache Flush Utility
```cpp
void FlushCpuCache() {
    // Allocate large memory block to flush cache
    const size_t CACHE_FLUSH_SIZE = 32 * 1024 * 1024; // 32 MB
    volatile char* buffer = new char[CACHE_FLUSH_SIZE];
    
    for (size_t i = 0; i < CACHE_FLUSH_SIZE; i += 64) { // Cache line size
        buffer[i] = 0;
    }
    
    delete[] buffer;
}
```

---

## 🔍 Related Documentation
- [REQ-NF-PERF-PHC-001](https://github.com/zarfld/IntelAvbFilter/issues/58) - PHC Read Latency <500ns
- [REQ-F-PTP-001](https://github.com/zarfld/IntelAvbFilter/issues/2) - PHC Get/Set
- [REQ-F-IOCTL-PHC-001](https://github.com/zarfld/IntelAvbFilter/issues/34) - PHC Query IOCTL
- [QA-SC-PERF-001](https://github.com/zarfld/IntelAvbFilter/issues/110) - PHC Performance Scenarios

---

**Status**: Ready for implementation (after Phase 05 unblocked by 40% test linkage)  
**Assignee**: TBD  
**Estimated Effort**: 4-5 days (10 unit + 3 integration + 2 V&V tests)  
**Dependencies**: High-resolution timing support, MMIO PHC register access
