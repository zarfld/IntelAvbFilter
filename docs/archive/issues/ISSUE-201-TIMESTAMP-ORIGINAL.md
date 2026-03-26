# TEST-PERF-TS-001: Packet Timestamp Retrieval Latency Verification

**Test ID**: TEST-PERF-TS-001  
**Test Name**: Packet Timestamp Retrieval Latency Verification  
**Priority**: P0 (Critical)  
**Test Type**: Unit, Integration, V&V  
**Phase**: 07-verification-validation

---

## 🔗 Traceability

- Traces to: #65 (REQ-NF-PERF-TS-001: Packet Timestamp Retrieval Latency <1µs)
- Verifies: #65
- Related Requirements: #35 (REQ-F-IOCTL-TX-001: TX Timestamp Retrieval), #37 (REQ-F-IOCTL-RX-001: RX Timestamp Retrieval), #149 (REQ-F-PTP-007: Hardware Timestamp Correlation)
- Related Quality Scenarios: #110 (QA-SC-PERF-001: PHC Performance)

---

## 📋 Test Objective

Validates **packet timestamp retrieval latency <1µs** requirement for both TX and RX timestamps. Verifies:

1. **TX Timestamp Retrieval Latency <1µs** (target: <500ns typical, <1µs worst-case)
2. **RX Timestamp Retrieval Latency <1µs** (target: <500ns typical, <1µs worst-case)
3. **User-Mode IOCTL Latency <10µs** (TX/RX timestamp query via DeviceIoControl)
4. **Latency Stability**: Standard deviation <100ns (minimal jitter)
5. **High-Frequency Sampling**: Retrieve timestamps for 10,000 packets/second
6. **No Performance Degradation** under concurrent access (multiple flows)

---

## 🎯 Test Coverage

### **10 Unit Tests** (Google Test, Mock NDIS)

#### UT-PERF-TS-001: TX Timestamp Retrieval Latency

**Objective**: Verify TX timestamp retrieval latency <1µs from hardware descriptor.

**Test Steps**:
1. Transmit packet and store TX descriptor reference
2. Wait for TX completion (hardware timestamp written to descriptor)
3. Measure timestamp retrieval latency:
```cpp
start = QueryPerformanceCounter()
txTimestamp = ReadTxDescriptorTimestamp(descriptor)
end = QueryPerformanceCounter()
latency = (end - start) * 1e9 / QPF
```
4. Repeat for 1000 packets
5. Calculate statistics (mean, max, stddev)

**Expected Result**:
- Mean latency <500ns
- Max latency <1µs
- Standard deviation <100ns

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 1000; i++) {
    // Transmit packet
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    // Measure timestamp retrieval
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
    
    EXPECT_GT(txTimestamp, 0); // Valid timestamp
}

double mean = CalculateMean(latencies);
double maxLatency = *std::max_element(latencies.begin(), latencies.end());
double stddev = CalculateStdDev(latencies);

EXPECT_LT(mean, 500); // Mean <500ns
EXPECT_LT(maxLatency, 1000); // Max <1µs
EXPECT_LT(stddev, 100); // Jitter <100ns
```

---

#### UT-PERF-TS-002: RX Timestamp Retrieval Latency

**Objective**: Verify RX timestamp retrieval latency <1µs from hardware descriptor.

**Test Steps**:
1. Receive packet with hardware timestamp
2. Measure timestamp retrieval latency:
```cpp
start = QueryPerformanceCounter()
rxTimestamp = ReadRxDescriptorTimestamp(descriptor)
end = QueryPerformanceCounter()
latency = (end - start) * 1e9 / QPF
```
3. Repeat for 1000 packets
4. Calculate statistics

**Expected Result**:
- Mean latency <500ns
- Max latency <1µs
- Standard deviation <100ns

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 1000; i++) {
    // Receive packet
    PNET_BUFFER_LIST nbl = ReceivePacket();
    
    // Measure timestamp retrieval
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 rxTimestamp = ReadRxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
    
    EXPECT_GT(rxTimestamp, 0); // Valid timestamp
}

double mean = CalculateMean(latencies);
double maxLatency = *std::max_element(latencies.begin(), latencies.end());
double stddev = CalculateStdDev(latencies);

EXPECT_LT(mean, 500); // Mean <500ns
EXPECT_LT(maxLatency, 1000); // Max <1µs
EXPECT_LT(stddev, 100); // Jitter <100ns
```

---

#### UT-PERF-TS-003: Timestamp Cache Hit vs Miss Performance

**Objective**: Measure latency difference between cached and uncached timestamp retrieval.

**Test Steps**:
1. Retrieve TX timestamp for first time (uncached): `latencyMiss`
2. Retrieve same TX timestamp again (cached): `latencyHit`
3. Compare latencies
4. Repeat for 100 packets

**Expected Result**:
- Cache miss <1µs (meets requirement)
- Cache hit <200ns (faster)
- Cache provides measurable speedup

**Acceptance Criteria**:
```cpp
std::vector<double> missLatencies, hitLatencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 100; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    FlushTimestampCache();
    
    // Cache miss
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 ts1 = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    missLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    
    // Cache hit (same timestamp)
    QueryPerformanceCounter(&start);
    UINT64 ts2 = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    hitLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    
    EXPECT_EQ(ts1, ts2); // Same timestamp
}

double meanMiss = CalculateMean(missLatencies);
double meanHit = CalculateMean(hitLatencies);

EXPECT_LT(meanMiss, 1000); // Miss <1µs
EXPECT_LT(meanHit, 200); // Hit <200ns
EXPECT_LT(meanHit, meanMiss); // Cache speedup
```

---

#### UT-PERF-TS-004: Concurrent Timestamp Retrieval (10 Threads)

**Objective**: Verify timestamp retrieval latency <1µs under concurrent access from 10 threads.

**Test Steps**:
1. Spawn 10 threads
2. Each thread:
   - Transmits 100 packets
   - Retrieves TX timestamps with latency measurement
3. Aggregate all measurements
4. Verify no lock contention degradation

**Expected Result**:
- All threads: mean <500ns, max <1µs
- No thread starvation

**Acceptance Criteria**:
```cpp
std::vector<std::vector<double>> threadLatencies(10);
std::vector<std::thread> threads;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int t = 0; t < 10; t++) {
    threads.emplace_back([&, t]() {
        for (int i = 0; i < 100; i++) {
            PNET_BUFFER_LIST nbl = TransmitPacket();
            WaitForTxCompletion(nbl);
            
            LARGE_INTEGER start, end;
            QueryPerformanceCounter(&start);
            UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
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
    
    EXPECT_LT(mean, 500); // Mean <500ns
    EXPECT_LT(maxLatency, 1000); // Max <1µs
}
```

---

#### UT-PERF-TS-005: High-Packet-Rate Timestamp Retrieval

**Objective**: Verify timestamp retrieval keeps up with 10,000 packets/second (100µs inter-packet gap).

**Test Steps**:
1. Generate 10,000 TX packets at 10 kpps rate
2. Retrieve TX timestamp for each packet
3. Measure retrieval latency for random sample of 1000 packets
4. Verify no queue buildup or dropped timestamps

**Expected Result**:
- All timestamps retrieved successfully
- Sampled latencies <1µs
- No packet drops due to timestamp processing

**Acceptance Criteria**:
```cpp
std::vector<double> sampledLatencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 10000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    if (i % 10 == 0) { // Sample every 10th packet
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
        QueryPerformanceCounter(&end);
        
        double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
        sampledLatencies.push_back(latencyNs);
        EXPECT_GT(txTimestamp, 0); // Valid timestamp
    }
    
    SleepMicroseconds(100); // 10 kpps
}

// Verify sampled latencies
for (double latency : sampledLatencies) {
    EXPECT_LT(latency, 1000); // All <1µs
}
EXPECT_EQ(1000, sampledLatencies.size()); // All samples collected
```

---

#### UT-PERF-TS-006: TX vs RX Timestamp Latency Comparison

**Objective**: Compare TX and RX timestamp retrieval latency characteristics.

**Test Steps**:
1. Measure TX timestamp retrieval for 1000 packets
2. Measure RX timestamp retrieval for 1000 packets
3. Compare statistics (mean, max, stddev)
4. Verify both meet <1µs requirement

**Expected Result**:
- Both TX and RX <1µs max
- Similar performance characteristics (within 10%)

**Acceptance Criteria**:
```cpp
std::vector<double> txLatencies, rxLatencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

// TX timestamps
for (int i = 0; i < 1000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    txLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

// RX timestamps
for (int i = 0; i < 1000; i++) {
    PNET_BUFFER_LIST nbl = ReceivePacket();
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 rxTimestamp = ReadRxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    rxLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

// Compare
double txMean = CalculateMean(txLatencies);
double rxMean = CalculateMean(rxLatencies);
double txMax = *std::max_element(txLatencies.begin(), txLatencies.end());
double rxMax = *std::max_element(rxLatencies.begin(), rxLatencies.end());

EXPECT_LT(txMax, 1000); // TX max <1µs
EXPECT_LT(rxMax, 1000); // RX max <1µs
EXPECT_NEAR(txMean, rxMean, txMean * 0.1); // Within 10%
```

---

#### UT-PERF-TS-007: Timestamp Retrieval Jitter Analysis

**Objective**: Verify timestamp retrieval jitter (standard deviation) <100ns.

**Test Steps**:
1. Retrieve 10,000 TX timestamps with latency measurement
2. Calculate jitter metrics:
   - Standard deviation
   - Interquartile range (IQR)
   - Coefficient of variation
3. Verify low jitter

**Expected Result**:
- Standard deviation <100ns
- IQR <200ns
- CV <0.2 (20% variation)

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 10000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double mean = CalculateMean(latencies);
double stddev = CalculateStdDev(latencies);
double q1 = CalculatePercentile(latencies, 25);
double q3 = CalculatePercentile(latencies, 75);
double iqr = q3 - q1;
double cv = stddev / mean;

EXPECT_LT(stddev, 100); // Jitter <100ns
EXPECT_LT(iqr, 200); // IQR <200ns
EXPECT_LT(cv, 0.2); // CV <20%
```

---

#### UT-PERF-TS-008: Null Timestamp Handling Performance

**Objective**: Verify fast detection of unavailable timestamps (no hardware timestamp).

**Test Steps**:
1. Configure hardware to not provide timestamps (disabled)
2. Transmit packet
3. Measure timestamp retrieval attempt:
   - Should detect null/invalid timestamp quickly
   - Return 0 or error code
4. Verify detection latency <100ns (faster than valid timestamp)

**Expected Result**:
- Null detection <100ns (no hardware access)
- Returns 0 or STATUS_NOT_SUPPORTED

**Acceptance Criteria**:
```cpp
DisableHardwareTimestamps();

std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 1000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    
    EXPECT_EQ(0, txTimestamp); // Null timestamp
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double mean = CalculateMean(latencies);
EXPECT_LT(mean, 100); // Null detection <100ns
```

---

#### UT-PERF-TS-009: Latency Histogram Distribution

**Objective**: Verify timestamp retrieval latency follows tight distribution.

**Test Steps**:
1. Retrieve 10,000 timestamps with latency measurement
2. Build histogram with 50ns bins: [0-50ns, 50-100ns, ..., 950-1000ns]
3. Analyze distribution:
   - 90% of samples within 500ns
   - 99% of samples within 800ns
   - <0.1% outliers >1µs

**Expected Result**:
- Tight distribution centered around 300-500ns
- Minimal tail

**Acceptance Criteria**:
```cpp
std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 10000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

std::sort(latencies.begin(), latencies.end());
double p90 = latencies[9000]; // 90th percentile
double p99 = latencies[9900]; // 99th percentile
int countOver1000 = std::count_if(latencies.begin(), latencies.end(), [](double l) { return l > 1000; });

EXPECT_LT(p90, 500); // 90% within 500ns
EXPECT_LT(p99, 800); // 99% within 800ns
EXPECT_LT(countOver1000, 10); // <0.1% outliers >1µs
```

---

#### UT-PERF-TS-010: Latency Regression Test

**Objective**: Verify timestamp retrieval latency has not regressed vs baseline.

**Test Steps**:
1. Load baseline performance data (from reference build)
2. Measure current build performance (10,000 retrievals)
3. Compare mean and max latencies
4. Verify no regression (current ≤ baseline + 10% tolerance)

**Expected Result**:
- No performance regression
- Current matches or exceeds baseline

**Acceptance Criteria**:
```cpp
// Baseline data (from reference build)
const double BASELINE_MEAN_NS = 400.0;
const double BASELINE_MAX_NS = 950.0;

std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 10000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
    QueryPerformanceCounter(&end);
    latencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
}

double currentMean = CalculateMean(latencies);
double currentMax = *std::max_element(latencies.begin(), latencies.end());

// No regression (10% tolerance)
EXPECT_LT(currentMean, BASELINE_MEAN_NS * 1.1);
EXPECT_LT(currentMax, BASELINE_MAX_NS * 1.1);
```

---

### **3 Integration Tests** (Google Test + Mock NDIS + User-Mode Harness)

#### IT-PERF-TS-001: User-Mode TX Timestamp IOCTL Latency

**Objective**: Verify user-mode IOCTL_AVB_TX_TIMESTAMP latency <10µs.

**Test Steps**:
1. Open device handle: `\\.\IntelAvbFilter0`
2. Transmit packet
3. Measure DeviceIoControl(IOCTL_AVB_TX_TIMESTAMP) latency (1000 calls)
4. Calculate statistics

**Expected Result**:
- Mean latency <5µs
- Max latency <10µs
- 99th percentile <7µs

**Acceptance Criteria**:
```cpp
HANDLE hDevice = CreateFile(L"\\\\.\\IntelAvbFilter0", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

std::vector<double> latencies;

for (int i = 0; i < 1000; i++) {
    // Transmit packet (returns sequence number)
    UINT32 sequenceNumber = TransmitPacketUserMode(hDevice);
    Sleep(1); // Allow TX completion
    
    // Query TX timestamp
    UINT64 txTimestamp;
    DWORD bytesReturned;
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    BOOL success = DeviceIoControl(hDevice, IOCTL_AVB_TX_TIMESTAMP,
                                    &sequenceNumber, sizeof(sequenceNumber),
                                    &txTimestamp, sizeof(txTimestamp),
                                    &bytesReturned, NULL);
    QueryPerformanceCounter(&end);
    
    EXPECT_TRUE(success);
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
}

double mean = CalculateMean(latencies);
double maxLatency = *std::max_element(latencies.begin(), latencies.end());
double p99 = CalculatePercentile(latencies, 99);

EXPECT_LT(mean, 5000); // Mean <5µs
EXPECT_LT(maxLatency, 10000); // Max <10µs
EXPECT_LT(p99, 7000); // 99th percentile <7µs

CloseHandle(hDevice);
```

---

#### IT-PERF-TS-002: User-Mode RX Timestamp IOCTL Latency

**Objective**: Verify user-mode IOCTL_AVB_RX_TIMESTAMP latency <10µs.

**Test Steps**:
1. Open device handle
2. Receive packet
3. Measure DeviceIoControl(IOCTL_AVB_RX_TIMESTAMP) latency (1000 calls)
4. Calculate statistics

**Expected Result**:
- Mean latency <5µs
- Max latency <10µs

**Acceptance Criteria**:
```cpp
HANDLE hDevice = CreateFile(L"\\\\.\\IntelAvbFilter0", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

std::vector<double> latencies;

for (int i = 0; i < 1000; i++) {
    // Receive packet (returns sequence number)
    UINT32 sequenceNumber = ReceivePacketUserMode(hDevice);
    
    // Query RX timestamp
    UINT64 rxTimestamp;
    DWORD bytesReturned;
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    BOOL success = DeviceIoControl(hDevice, IOCTL_AVB_RX_TIMESTAMP,
                                    &sequenceNumber, sizeof(sequenceNumber),
                                    &rxTimestamp, sizeof(rxTimestamp),
                                    &bytesReturned, NULL);
    QueryPerformanceCounter(&end);
    
    EXPECT_TRUE(success);
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
}

double mean = CalculateMean(latencies);
double maxLatency = *std::max_element(latencies.begin(), latencies.end());

EXPECT_LT(mean, 5000); // Mean <5µs
EXPECT_LT(maxLatency, 10000); // Max <10µs

CloseHandle(hDevice);
```

---

#### IT-PERF-TS-003: gPTP Stack Timestamp Query Performance

**Objective**: Verify gPTP can query TX/RX timestamps at 8 Hz rate with latency <1µs.

**Test Steps**:
1. Configure gPTP slave mode (8 Hz sync rate)
2. For each sync interval (100 iterations):
   - Capture Sync message RX timestamp
   - Capture Delay_Req message TX timestamp
   - Measure retrieval latencies
3. Verify latencies don't impact gPTP sync quality

**Expected Result**:
- All timestamp retrievals <1µs
- gPTP sync accuracy <1µs (unaffected)

**Acceptance Criteria**:
```cpp
EnableGptpSlave();

std::vector<double> rxLatencies, txLatencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

for (int i = 0; i < 100; i++) {
    WaitForGptpSyncEvent();
    
    // Measure RX timestamp retrieval (Sync message)
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 rxTimestamp = GetGptpSyncRxTimestamp();
    QueryPerformanceCounter(&end);
    rxLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    
    // Measure TX timestamp retrieval (Delay_Req message)
    QueryPerformanceCounter(&start);
    UINT64 txTimestamp = GetGptpDelayReqTxTimestamp();
    QueryPerformanceCounter(&end);
    txLatencies.push_back((double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart);
    
    // Verify gPTP sync accuracy
    INT64 offset = GetGptpOffset();
    if (i > 10) { // After settling
        EXPECT_LT(llabs(offset), 1000); // <1µs
    }
}

// Verify retrieval latencies
for (double latency : rxLatencies) {
    EXPECT_LT(latency, 1000); // <1µs
}
for (double latency : txLatencies) {
    EXPECT_LT(latency, 1000); // <1µs
}
```

---

### **2 V&V Tests** (User-Mode Harness, Quantified Metrics)

#### VV-PERF-TS-001: Production Workload Performance (1 Hour)

**Objective**: Verify timestamp retrieval latency <1µs over 1-hour production gPTP workload.

**Test Steps**:
1. Run production gPTP synchronization for 1 hour
2. Sample timestamp retrieval latency every sync interval (28,800 samples)
3. Verify statistics:
   - Mean latency <500ns
   - Max latency <1µs
   - Standard deviation <100ns
   - No performance drift

**Expected Result**:
- Latency stable over 1 hour
- No degradation

**Acceptance Criteria**:
```cpp
EnableGptpSlave();

std::vector<double> latencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

auto startTime = std::chrono::system_clock::now();
while (std::chrono::duration_cast<std::chrono::hours>(
           std::chrono::system_clock::now() - startTime).count() < 1) {
    
    WaitForGptpSyncEvent();
    
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    UINT64 rxTimestamp = GetGptpSyncRxTimestamp();
    QueryPerformanceCounter(&end);
    
    double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
    latencies.push_back(latencyNs);
}

EXPECT_EQ(28800, latencies.size()); // 1 hour at 8 Hz
EXPECT_LT(CalculateMean(latencies), 500); // Mean <500ns
EXPECT_LT(*std::max_element(latencies.begin(), latencies.end()), 1000); // Max <1µs
EXPECT_LT(CalculateStdDev(latencies), 100); // Jitter <100ns

// No drift
double meanFirst720 = CalculateMean(std::vector<double>(latencies.begin(), latencies.begin() + 720)); // First 90 seconds
double meanLast720 = CalculateMean(std::vector<double>(latencies.end() - 720, latencies.end())); // Last 90 seconds
EXPECT_NEAR(meanFirst720, meanLast720, 50); // <50ns drift
```

---

#### VV-PERF-TS-002: High-Throughput Stress Test (100,000 Packets)

**Objective**: Verify timestamp retrieval performance under high packet throughput.

**Test Steps**:
1. Transmit 100,000 packets at maximum rate
2. Retrieve TX timestamp for each packet
3. Sample latency for every 100th packet (1000 samples)
4. Verify no queue buildup or dropped timestamps

**Expected Result**:
- All 100,000 timestamps retrieved successfully
- Sampled latencies <1µs
- No packet drops

**Acceptance Criteria**:
```cpp
std::vector<double> sampledLatencies;
LARGE_INTEGER qpf;
QueryPerformanceFrequency(&qpf);

int successCount = 0;

for (int i = 0; i < 100000; i++) {
    PNET_BUFFER_LIST nbl = TransmitPacket();
    WaitForTxCompletion(nbl);
    
    if (i % 100 == 0) { // Sample every 100th
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
        QueryPerformanceCounter(&end);
        
        double latencyNs = (double)(end.QuadPart - start.QuadPart) * 1e9 / qpf.QuadPart;
        sampledLatencies.push_back(latencyNs);
        
        if (txTimestamp > 0) {
            successCount++;
        }
    } else {
        UINT64 txTimestamp = ReadTxDescriptorTimestamp(nbl);
        if (txTimestamp > 0) {
            successCount++;
        }
    }
}

EXPECT_EQ(100000, successCount); // All timestamps retrieved
EXPECT_EQ(1000, sampledLatencies.size()); // All samples collected

for (double latency : sampledLatencies) {
    EXPECT_LT(latency, 1000); // All <1µs
}

double mean = CalculateMean(sampledLatencies);
EXPECT_LT(mean, 500); // Mean <500ns
```

---

## 🎯 Acceptance Criteria

1. **Kernel-Mode Performance**:
   - ✅ TX timestamp retrieval: mean <500ns, max <1µs
   - ✅ RX timestamp retrieval: mean <500ns, max <1µs
   - ✅ Standard deviation <100ns (jitter)

2. **User-Mode IOCTL Performance**:
   - ✅ TX timestamp IOCTL: mean <5µs, max <10µs
   - ✅ RX timestamp IOCTL: mean <5µs, max <10µs

3. **Concurrency**:
   - ✅ 10 threads: all achieve <500ns mean, <1µs max

4. **High-Throughput**:
   - ✅ 10,000 packets/second sustained
   - ✅ 100,000 packets stress test: all timestamps retrieved

5. **Stability**:
   - ✅ 1-hour production workload: no drift, consistent latency
   - ✅ Cache hit <200ns, miss <1µs

6. **Distribution**:
   - ✅ 90% of samples within 500ns
   - ✅ 99% of samples within 800ns
   - ✅ <0.1% outliers >1µs

7. **Robustness**:
   - ✅ Null timestamp detection <100ns
   - ✅ No regression vs baseline

8. **Traceability**:
   - ✅ All tests reference requirement #65
   - ✅ Test results logged with issue references

---

## 📊 Test Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| TX Timestamp Mean Latency | <500ns | Average of 1000 retrievals |
| TX Timestamp Max Latency | <1µs | Max of 1000 retrievals |
| RX Timestamp Mean Latency | <500ns | Average of 1000 retrievals |
| RX Timestamp Max Latency | <1µs | Max of 1000 retrievals |
| Jitter (Stddev) | <100ns | Standard deviation |
| User-Mode TX IOCTL Mean | <5µs | DeviceIoControl timing |
| User-Mode RX IOCTL Mean | <5µs | DeviceIoControl timing |
| Cache Hit Latency | <200ns | Cached timestamp retrieval |
| Cache Miss Latency | <1µs | Uncached retrieval |
| Null Detection Latency | <100ns | Disabled timestamp check |
| 1-Hour Stability | No drift >50ns | First 90s vs last 90s |
| High-Throughput | 100,000 packets | All timestamps retrieved |

---

## 🛠️ Test Implementation

### Timestamp Retrieval Wrappers

```cpp
// TX Timestamp Retrieval
UINT64 ReadTxDescriptorTimestamp(PNET_BUFFER_LIST NetBufferList) {
    // Read timestamp from TX descriptor metadata
    PTX_DESCRIPTOR txDesc = GetTxDescriptor(NetBufferList);
    if (txDesc == NULL || !txDesc->timestampValid) {
        return 0; // No timestamp available
    }
    
    // Hardware timestamp in nanoseconds
    UINT64 timestamp = ((UINT64)txDesc->timestampHigh << 32) | txDesc->timestampLow;
    return timestamp;
}

// RX Timestamp Retrieval
UINT64 ReadRxDescriptorTimestamp(PNET_BUFFER_LIST NetBufferList) {
    // Read timestamp from RX descriptor metadata
    PRX_DESCRIPTOR rxDesc = GetRxDescriptor(NetBufferList);
    if (rxDesc == NULL || !rxDesc->timestampValid) {
        return 0; // No timestamp available
    }
    
    UINT64 timestamp = ((UINT64)rxDesc->timestampHigh << 32) | rxDesc->timestampLow;
    return timestamp;
}
```

### High-Resolution Timing Wrapper

```cpp
class TimestampRetrievalTimer {
public:
    TimestampRetrievalTimer() {
        QueryPerformanceFrequency(&frequency_);
    }
    
    double MeasureTxTimestampRetrieval(PNET_BUFFER_LIST nbl) {
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        UINT64 timestamp = ReadTxDescriptorTimestamp(nbl);
        QueryPerformanceCounter(&end);
        
        return (double)(end.QuadPart - start.QuadPart) * 1e9 / frequency_.QuadPart;
    }
    
private:
    LARGE_INTEGER frequency_;
};
```

### Latency Statistics Helper

```cpp
struct LatencyStatistics {
    double mean;
    double stddev;
    double min;
    double max;
    double p50;
    double p90;
    double p99;
};

LatencyStatistics CalculateLatencyStats(const std::vector<double>& latencies) {
    LatencyStatistics stats;
    
    stats.mean = CalculateMean(latencies);
    stats.stddev = CalculateStdDev(latencies);
    stats.min = *std::min_element(latencies.begin(), latencies.end());
    stats.max = *std::max_element(latencies.begin(), latencies.end());
    
    std::vector<double> sorted = latencies;
    std::sort(sorted.begin(), sorted.end());
    stats.p50 = sorted[sorted.size() / 2];
    stats.p90 = sorted[(sorted.size() * 90) / 100];
    stats.p99 = sorted[(sorted.size() * 99) / 100];
    
    return stats;
}
```

---

## 🔍 Related Documentation

- [REQ-NF-PERF-TS-001](https://github.com/zarfld/IntelAvbFilter/issues/65) - Packet Timestamp Retrieval Latency <1µs
- [REQ-F-IOCTL-TX-001](https://github.com/zarfld/IntelAvbFilter/issues/35) - TX Timestamp Retrieval
- [REQ-F-IOCTL-RX-001](https://github.com/zarfld/IntelAvbFilter/issues/37) - RX Timestamp Retrieval
- [REQ-F-PTP-007](https://github.com/zarfld/IntelAvbFilter/issues/149) - Hardware Timestamp Correlation
- [QA-SC-PERF-001](https://github.com/zarfld/IntelAvbFilter/issues/110) - PHC Performance Scenarios

---

**Status**: Ready for implementation (after Phase 05 unblocked by 40% test linkage)  
**Assignee**: TBD  
**Estimated Effort**: 4-5 days (10 unit + 3 integration + 2 V&V tests)  
**Dependencies**: Hardware TX/RX timestamp support, descriptor metadata access
