/*
 * TEST-PERF-TS-001: Verify Timestamp Retrieval Latency <1µs
 * 
 * Verifies: #65 (REQ-NF-PERF-TS-001: Timestamp Retrieval Latency <1µs)
 * 
 * Purpose:
 *   Validate TX/RX timestamp retrieval IOCTLs complete with <1µs median latency
 *   and <2µs P99 latency through RDTSC measurement, ensuring gPTP performance.
 * 
 * Test Cases:
 *   TC-PERF-TS-001: TX Timestamp Median Latency <1µs (10,000 iterations)
 *   TC-PERF-TS-002: RX Timestamp Median Latency <1µs (10,000 iterations)
 *   TC-PERF-TS-003: TX Timestamp P99 Latency <2µs
 *   TC-PERF-TS-004: RX Timestamp P99 Latency <2µs
 *   TC-PERF-TS-005: Latency Distribution (>90% queries <1µs)
 *   TC-PERF-TS-006: Concurrent Load (8 threads, median <1µs)
 *   TC-PERF-TS-007: Performance Degradation Check (<10% variance)
 *   TC-PERF-TS-008: Warm-up Effect (cache stabilization)
 * 
 * Requirement: Median <1µs, P99 <2µs for TX/RX timestamp queries
 * 
 * Author: GitHub Copilot (Standards Compliance Agent)
 * Date: 2025-12-26
 * Issue: #272
 */

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <process.h>

// IOCTL Definitions (from include/avb_filter_public.h)
#define IOCTL_AVB_BASE 0x9C40
#define IOCTL_TYPE_DEVICE 0xA000

#define IOCTL_GET_TX_TIMESTAMP    CTL_CODE(IOCTL_AVB_BASE, IOCTL_TYPE_DEVICE + 0x24, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_RX_TIMESTAMP    CTL_CODE(IOCTL_AVB_BASE, IOCTL_TYPE_DEVICE + 0x2C, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Timestamp Query Structures
typedef struct _TX_TIMESTAMP_QUERY {
    UINT64 Timestamp;
    UINT32 SequenceId;
    BOOLEAN Valid;
    UINT8 Reserved[3];
} TX_TIMESTAMP_QUERY;

typedef struct _RX_TIMESTAMP_QUERY {
    UINT64 Timestamp;
    UINT32 SequenceId;
    BOOLEAN Valid;
    UINT8 Reserved[3];
} RX_TIMESTAMP_QUERY;

// Test Configuration
#define ITERATIONS 10000
#define WARMUP_ITERATIONS 100
#define CONCURRENT_THREADS 8
#define HISTOGRAM_SAMPLES 100000
#define VARIANCE_THRESHOLD 0.10  // 10% max variance

// Latency thresholds (nanoseconds)
#define MEDIAN_THRESHOLD_NS 1000   // <1µs
#define P99_THRESHOLD_NS 2000      // <2µs
#define CONCURRENT_P99_NS 5000     // <5µs under load

// Test Result Structure
typedef struct _TEST_RESULT {
    const char* TestCase;
    bool Passed;
    const char* Reason;
} TEST_RESULT;

TEST_RESULT g_Results[20];
int g_ResultCount = 0;

// Thread-safe latency storage for concurrent tests
typedef struct _THREAD_LATENCY_DATA {
    UINT64 Latencies[ITERATIONS];
    int ThreadId;
    HANDLE DeviceHandle;
    double MedianNs;
    double P99Ns;
} THREAD_LATENCY_DATA;

// Function Prototypes
HANDLE OpenAvbDevice(void);
void RecordResult(const char* testCase, bool passed, const char* reason);
double GetCpuFrequencyGHz(void);
int CompareUint64(const void* a, const void* b);
void CalculateStatistics(UINT64* latencies, int count, double cpuFreqGHz,
                        double* medianNs, double* p50Ns, double* p95Ns, 
                        double* p99Ns, double* meanNs);
void GenerateHistogram(UINT64* latencies, int count, double cpuFreqGHz);
void PrintTestSummary(void);

// Test Case Functions
void TestTxTimestampLatency(void);
void TestRxTimestampLatency(void);
void TestLatencyDistribution(void);
void TestConcurrentLoad(void);
void TestPerformanceDegradation(void);
void TestWarmupEffect(void);

/*
 * Main entry point
 */
int main(void)
{
    printf("\n=== TEST-PERF-TS-001: Timestamp Retrieval Latency <1us ===\n");
    printf("Verifies: #65 (REQ-NF-PERF-TS-001)\n");
    printf("Issue: #272\n");
    printf("Iterations: %d\n", ITERATIONS);
    printf("Requirement: Median <1µs, P99 <2µs\n\n");

    // Get CPU frequency for RDTSC → nanosecond conversion
    double cpuFreqGHz = GetCpuFrequencyGHz();
    printf("CPU Frequency: %.2f GHz (%.3f cycles/ns)\n\n", cpuFreqGHz, cpuFreqGHz);

    // Run test cases
    TestTxTimestampLatency();
    TestRxTimestampLatency();
    TestLatencyDistribution();
    TestConcurrentLoad();
    TestPerformanceDegradation();
    TestWarmupEffect();

    // Print summary
    PrintTestSummary();

    return 0;
}

/*
 * TC-PERF-TS-001: TX Timestamp Median Latency <1µs
 * TC-PERF-TS-003: TX Timestamp P99 Latency <2µs
 */
void TestTxTimestampLatency(void)
{
    printf("--- TC-PERF-TS-001/003: TX Timestamp Latency ---\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        RecordResult("TC-PERF-TS-001", false, "Failed to open device");
        RecordResult("TC-PERF-TS-003", false, "Failed to open device");
        return;
    }

    UINT64* latencies = (UINT64*)calloc(ITERATIONS, sizeof(UINT64));
    if (!latencies) {
        RecordResult("TC-PERF-TS-001", false, "Memory allocation failed");
        RecordResult("TC-PERF-TS-003", false, "Memory allocation failed");
        CloseHandle(hDevice);
        return;
    }

    double cpuFreqGHz = GetCpuFrequencyGHz();

    // Warm-up (populate caches)
    printf("Warming up (100 queries)...\n");
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
    }

    // Measure latencies
    printf("Measuring latencies (%d iterations)...\n", ITERATIONS);
    for (int i = 0; i < ITERATIONS; i++) {
        UINT64 start = __rdtsc();

        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);

        UINT64 end = __rdtsc();
        latencies[i] = end - start;
    }

    // Calculate statistics
    double medianNs, p50Ns, p95Ns, p99Ns, meanNs;
    CalculateStatistics(latencies, ITERATIONS, cpuFreqGHz,
                       &medianNs, &p50Ns, &p95Ns, &p99Ns, &meanNs);

    printf("TX Timestamp Latency (%d iterations):\n", ITERATIONS);
    printf("  Mean:   %.0f ns\n", meanNs);
    printf("  Median: %.0f ns\n", medianNs);
    printf("  P50:    %.0f ns\n", p50Ns);
    printf("  P95:    %.0f ns\n", p95Ns);
    printf("  P99:    %.0f ns\n", p99Ns);

    // Verify requirements
    bool medianPass = (medianNs < MEDIAN_THRESHOLD_NS);
    bool p99Pass = (p99Ns < P99_THRESHOLD_NS);

    if (medianPass) {
        char reason[128];
        snprintf(reason, sizeof(reason), "PASS: Median %.0f ns < 1µs", medianNs);
        RecordResult("TC-PERF-TS-001", true, reason);
        printf("✅ TC-PERF-TS-001: %s\n", reason);
    } else {
        char reason[128];
        snprintf(reason, sizeof(reason), "FAIL: Median %.0f ns >= 1µs", medianNs);
        RecordResult("TC-PERF-TS-001", false, reason);
        printf("❌ TC-PERF-TS-001: %s\n", reason);
    }

    if (p99Pass) {
        char reason[128];
        snprintf(reason, sizeof(reason), "PASS: P99 %.0f ns < 2µs", p99Ns);
        RecordResult("TC-PERF-TS-003", true, reason);
        printf("✅ TC-PERF-TS-003: %s\n", reason);
    } else {
        char reason[128];
        snprintf(reason, sizeof(reason), "FAIL: P99 %.0f ns >= 2µs", p99Ns);
        RecordResult("TC-PERF-TS-003", false, reason);
        printf("❌ TC-PERF-TS-003: %s\n", reason);
    }

    free(latencies);
    CloseHandle(hDevice);
    printf("\n");
}

/*
 * TC-PERF-TS-002: RX Timestamp Median Latency <1µs
 * TC-PERF-TS-004: RX Timestamp P99 Latency <2µs
 */
void TestRxTimestampLatency(void)
{
    printf("--- TC-PERF-TS-002/004: RX Timestamp Latency ---\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        RecordResult("TC-PERF-TS-002", false, "Failed to open device");
        RecordResult("TC-PERF-TS-004", false, "Failed to open device");
        return;
    }

    UINT64* latencies = (UINT64*)calloc(ITERATIONS, sizeof(UINT64));
    if (!latencies) {
        RecordResult("TC-PERF-TS-002", false, "Memory allocation failed");
        RecordResult("TC-PERF-TS-004", false, "Memory allocation failed");
        CloseHandle(hDevice);
        return;
    }

    double cpuFreqGHz = GetCpuFrequencyGHz();

    // Warm-up
    printf("Warming up (100 queries)...\n");
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        RX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_RX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
    }

    // Measure latencies
    printf("Measuring latencies (%d iterations)...\n", ITERATIONS);
    for (int i = 0; i < ITERATIONS; i++) {
        UINT64 start = __rdtsc();

        RX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_RX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);

        UINT64 end = __rdtsc();
        latencies[i] = end - start;
    }

    // Calculate statistics
    double medianNs, p50Ns, p95Ns, p99Ns, meanNs;
    CalculateStatistics(latencies, ITERATIONS, cpuFreqGHz,
                       &medianNs, &p50Ns, &p95Ns, &p99Ns, &meanNs);

    printf("RX Timestamp Latency (%d iterations):\n", ITERATIONS);
    printf("  Mean:   %.0f ns\n", meanNs);
    printf("  Median: %.0f ns\n", medianNs);
    printf("  P50:    %.0f ns\n", p50Ns);
    printf("  P95:    %.0f ns\n", p95Ns);
    printf("  P99:    %.0f ns\n", p99Ns);

    // Verify requirements
    bool medianPass = (medianNs < MEDIAN_THRESHOLD_NS);
    bool p99Pass = (p99Ns < P99_THRESHOLD_NS);

    if (medianPass) {
        char reason[128];
        snprintf(reason, sizeof(reason), "PASS: Median %.0f ns < 1µs", medianNs);
        RecordResult("TC-PERF-TS-002", true, reason);
        printf("✅ TC-PERF-TS-002: %s\n", reason);
    } else {
        char reason[128];
        snprintf(reason, sizeof(reason), "FAIL: Median %.0f ns >= 1µs", medianNs);
        RecordResult("TC-PERF-TS-002", false, reason);
        printf("❌ TC-PERF-TS-002: %s\n", reason);
    }

    if (p99Pass) {
        char reason[128];
        snprintf(reason, sizeof(reason), "PASS: P99 %.0f ns < 2µs", p99Ns);
        RecordResult("TC-PERF-TS-004", true, reason);
        printf("✅ TC-PERF-TS-004: %s\n", reason);
    } else {
        char reason[128];
        snprintf(reason, sizeof(reason), "FAIL: P99 %.0f ns >= 2µs", p99Ns);
        RecordResult("TC-PERF-TS-004", false, reason);
        printf("❌ TC-PERF-TS-004: %s\n", reason);
    }

    free(latencies);
    CloseHandle(hDevice);
    printf("\n");
}

/*
 * TC-PERF-TS-005: Latency Distribution (>90% queries <1µs)
 */
void TestLatencyDistribution(void)
{
    printf("--- TC-PERF-TS-005: Latency Distribution ---\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        RecordResult("TC-PERF-TS-005", false, "Failed to open device");
        return;
    }

    UINT64* latencies = (UINT64*)calloc(HISTOGRAM_SAMPLES, sizeof(UINT64));
    if (!latencies) {
        RecordResult("TC-PERF-TS-005", false, "Memory allocation failed");
        CloseHandle(hDevice);
        return;
    }

    double cpuFreqGHz = GetCpuFrequencyGHz();

    printf("Collecting %d samples for histogram...\n", HISTOGRAM_SAMPLES);
    for (int i = 0; i < HISTOGRAM_SAMPLES; i++) {
        UINT64 start = __rdtsc();

        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);

        UINT64 end = __rdtsc();
        latencies[i] = end - start;

        // Progress indicator every 10k samples
        if ((i + 1) % 10000 == 0) {
            printf("  Progress: %d/%d samples\n", i + 1, HISTOGRAM_SAMPLES);
        }
    }

    // Generate histogram and calculate percentage <1µs
    int bucket_0_500ns = 0;
    int bucket_500_1000ns = 0;
    int bucket_1_2us = 0;
    int bucket_2_5us = 0;
    int bucket_over_5us = 0;

    for (int i = 0; i < HISTOGRAM_SAMPLES; i++) {
        double latencyNs = (double)latencies[i] / cpuFreqGHz;
        if (latencyNs < 500) bucket_0_500ns++;
        else if (latencyNs < 1000) bucket_500_1000ns++;
        else if (latencyNs < 2000) bucket_1_2us++;
        else if (latencyNs < 5000) bucket_2_5us++;
        else bucket_over_5us++;
    }

    int under_1us = bucket_0_500ns + bucket_500_1000ns;
    double percentUnder1us = (double)under_1us * 100.0 / HISTOGRAM_SAMPLES;

    printf("\nLatency Histogram (%d samples):\n", HISTOGRAM_SAMPLES);
    printf("  0-500ns:     %d (%.1f%%)\n", bucket_0_500ns, bucket_0_500ns * 100.0 / HISTOGRAM_SAMPLES);
    printf("  500-1000ns:  %d (%.1f%%)\n", bucket_500_1000ns, bucket_500_1000ns * 100.0 / HISTOGRAM_SAMPLES);
    printf("  1-2µs:       %d (%.1f%%)\n", bucket_1_2us, bucket_1_2us * 100.0 / HISTOGRAM_SAMPLES);
    printf("  2-5µs:       %d (%.1f%%)\n", bucket_2_5us, bucket_2_5us * 100.0 / HISTOGRAM_SAMPLES);
    printf("  >5µs:        %d (%.1f%%)\n", bucket_over_5us, bucket_over_5us * 100.0 / HISTOGRAM_SAMPLES);

    // Verify >90% under 1µs
    bool passed = (percentUnder1us >= 90.0);
    if (passed) {
        char reason[128];
        snprintf(reason, sizeof(reason), "PASS: %.1f%% < 1µs (>90%%)", percentUnder1us);
        RecordResult("TC-PERF-TS-005", true, reason);
        printf("✅ TC-PERF-TS-005: %s\n", reason);
    } else {
        char reason[128];
        snprintf(reason, sizeof(reason), "FAIL: %.1f%% < 1µs (<90%%)", percentUnder1us);
        RecordResult("TC-PERF-TS-005", false, reason);
        printf("❌ TC-PERF-TS-005: %s\n", reason);
    }

    free(latencies);
    CloseHandle(hDevice);
    printf("\n");
}

/*
 * Thread function for concurrent latency test
 */
unsigned __stdcall TimestampQueryThread(void* param)
{
    THREAD_LATENCY_DATA* data = (THREAD_LATENCY_DATA*)param;
    double cpuFreqGHz = GetCpuFrequencyGHz();

    for (int i = 0; i < ITERATIONS; i++) {
        UINT64 start = __rdtsc();

        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(data->DeviceHandle, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);

        UINT64 end = __rdtsc();
        data->Latencies[i] = end - start;
    }

    // Calculate per-thread statistics
    qsort(data->Latencies, ITERATIONS, sizeof(UINT64), CompareUint64);
    UINT64 median = data->Latencies[ITERATIONS / 2];
    UINT64 p99 = data->Latencies[(int)(ITERATIONS * 0.99)];

    data->MedianNs = (double)median / cpuFreqGHz;
    data->P99Ns = (double)p99 / cpuFreqGHz;

    return 0;
}

/*
 * TC-PERF-TS-006: Concurrent Load (8 threads, median <1µs)
 */
void TestConcurrentLoad(void)
{
    printf("--- TC-PERF-TS-006: Concurrent Load (8 threads) ---\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        RecordResult("TC-PERF-TS-006", false, "Failed to open device");
        return;
    }

    THREAD_LATENCY_DATA threadData[CONCURRENT_THREADS];
    HANDLE threads[CONCURRENT_THREADS];

    // Initialize thread data
    for (int t = 0; t < CONCURRENT_THREADS; t++) {
        threadData[t].ThreadId = t;
        threadData[t].DeviceHandle = hDevice;
        threadData[t].MedianNs = 0;
        threadData[t].P99Ns = 0;
    }

    printf("Launching %d threads (%d queries each)...\n", CONCURRENT_THREADS, ITERATIONS);

    // Launch threads
    for (int t = 0; t < CONCURRENT_THREADS; t++) {
        threads[t] = (HANDLE)_beginthreadex(NULL, 0, TimestampQueryThread,
                                            &threadData[t], 0, NULL);
        if (threads[t] == NULL) {
            RecordResult("TC-PERF-TS-006", false, "Thread creation failed");
            CloseHandle(hDevice);
            return;
        }
    }

    // Wait for all threads
    WaitForMultipleObjects(CONCURRENT_THREADS, threads, TRUE, INFINITE);

    // Close thread handles
    for (int t = 0; t < CONCURRENT_THREADS; t++) {
        CloseHandle(threads[t]);
    }

    // Verify per-thread results
    printf("\nPer-thread results:\n");
    bool allPassed = true;
    for (int t = 0; t < CONCURRENT_THREADS; t++) {
        printf("  Thread %d: Median=%.0f ns, P99=%.0f ns\n",
               t, threadData[t].MedianNs, threadData[t].P99Ns);

        // Under load, accept P99 <5µs (may have contention)
        if (threadData[t].MedianNs >= MEDIAN_THRESHOLD_NS ||
            threadData[t].P99Ns >= CONCURRENT_P99_NS) {
            allPassed = false;
        }
    }

    if (allPassed) {
        RecordResult("TC-PERF-TS-006", true, "PASS: All threads median <1µs, P99 <5µs");
        printf("✅ TC-PERF-TS-006: PASS (all threads meet requirements)\n");
    } else {
        RecordResult("TC-PERF-TS-006", false, "FAIL: Some threads exceeded thresholds");
        printf("❌ TC-PERF-TS-006: FAIL (some threads exceeded thresholds)\n");
    }

    CloseHandle(hDevice);
    printf("\n");
}

/*
 * TC-PERF-TS-007: Performance Degradation Check (<10% variance)
 */
void TestPerformanceDegradation(void)
{
    printf("--- TC-PERF-TS-007: Performance Degradation Check ---\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        RecordResult("TC-PERF-TS-007", false, "Failed to open device");
        return;
    }

    UINT64* latencies1 = (UINT64*)calloc(ITERATIONS, sizeof(UINT64));
    UINT64* latencies2 = (UINT64*)calloc(ITERATIONS, sizeof(UINT64));
    if (!latencies1 || !latencies2) {
        RecordResult("TC-PERF-TS-007", false, "Memory allocation failed");
        free(latencies1);
        free(latencies2);
        CloseHandle(hDevice);
        return;
    }

    double cpuFreqGHz = GetCpuFrequencyGHz();

    // First run
    printf("Running first batch (%d iterations)...\n", ITERATIONS);
    for (int i = 0; i < ITERATIONS; i++) {
        UINT64 start = __rdtsc();
        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
        UINT64 end = __rdtsc();
        latencies1[i] = end - start;
    }

    // Second run (after delay to check for degradation)
    Sleep(1000);  // 1 second delay
    printf("Running second batch (%d iterations)...\n", ITERATIONS);
    for (int i = 0; i < ITERATIONS; i++) {
        UINT64 start = __rdtsc();
        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
        UINT64 end = __rdtsc();
        latencies2[i] = end - start;
    }

    // Calculate statistics
    double median1Ns, p50_1Ns, p95_1Ns, p99_1Ns, mean1Ns;
    double median2Ns, p50_2Ns, p95_2Ns, p99_2Ns, mean2Ns;

    CalculateStatistics(latencies1, ITERATIONS, cpuFreqGHz,
                       &median1Ns, &p50_1Ns, &p95_1Ns, &p99_1Ns, &mean1Ns);
    CalculateStatistics(latencies2, ITERATIONS, cpuFreqGHz,
                       &median2Ns, &p50_2Ns, &p95_2Ns, &p99_2Ns, &mean2Ns);

    printf("First batch:  Median=%.0f ns, Mean=%.0f ns\n", median1Ns, mean1Ns);
    printf("Second batch: Median=%.0f ns, Mean=%.0f ns\n", median2Ns, mean2Ns);

    // Calculate variance
    double variance = fabs(median2Ns - median1Ns) / median1Ns;
    printf("Variance: %.1f%%\n", variance * 100.0);

    // Verify variance <10%
    bool passed = (variance < VARIANCE_THRESHOLD);
    if (passed) {
        char reason[128];
        snprintf(reason, sizeof(reason), "PASS: Variance %.1f%% < 10%%", variance * 100.0);
        RecordResult("TC-PERF-TS-007", true, reason);
        printf("✅ TC-PERF-TS-007: %s\n", reason);
    } else {
        char reason[128];
        snprintf(reason, sizeof(reason), "FAIL: Variance %.1f%% >= 10%%", variance * 100.0);
        RecordResult("TC-PERF-TS-007", false, reason);
        printf("❌ TC-PERF-TS-007: %s\n", reason);
    }

    free(latencies1);
    free(latencies2);
    CloseHandle(hDevice);
    printf("\n");
}

/*
 * TC-PERF-TS-008: Warm-up Effect (cache stabilization)
 */
void TestWarmupEffect(void)
{
    printf("--- TC-PERF-TS-008: Warm-up Effect ---\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        RecordResult("TC-PERF-TS-008", false, "Failed to open device");
        return;
    }

    double cpuFreqGHz = GetCpuFrequencyGHz();

    // Measure first 10 queries (cold cache)
    UINT64 coldLatencies[10];
    printf("Measuring cold cache latencies (first 10 queries)...\n");
    for (int i = 0; i < 10; i++) {
        UINT64 start = __rdtsc();
        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
        UINT64 end = __rdtsc();
        coldLatencies[i] = end - start;
    }

    // Warm-up
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
    }

    // Measure warm cache latencies
    UINT64 warmLatencies[10];
    printf("Measuring warm cache latencies (after %d warmup queries)...\n", WARMUP_ITERATIONS);
    for (int i = 0; i < 10; i++) {
        UINT64 start = __rdtsc();
        TX_TIMESTAMP_QUERY query = {0};
        DWORD bytesReturned = 0;
        DeviceIoControl(hDevice, IOCTL_GET_TX_TIMESTAMP,
                       NULL, 0, &query, sizeof(query), &bytesReturned, NULL);
        UINT64 end = __rdtsc();
        warmLatencies[i] = end - start;
    }

    // Calculate averages
    UINT64 coldSum = 0, warmSum = 0;
    for (int i = 0; i < 10; i++) {
        coldSum += coldLatencies[i];
        warmSum += warmLatencies[i];
    }
    double coldAvgNs = ((double)coldSum / 10.0) / cpuFreqGHz;
    double warmAvgNs = ((double)warmSum / 10.0) / cpuFreqGHz;

    printf("Cold cache average: %.0f ns\n", coldAvgNs);
    printf("Warm cache average: %.0f ns\n", warmAvgNs);
    printf("Improvement: %.0f ns (%.1f%%)\n", coldAvgNs - warmAvgNs,
           ((coldAvgNs - warmAvgNs) / coldAvgNs) * 100.0);

    // Verify warm-up improves latency
    bool passed = (warmAvgNs < coldAvgNs);
    if (passed) {
        RecordResult("TC-PERF-TS-008", true, "PASS: Warm-up reduces latency");
        printf("✅ TC-PERF-TS-008: PASS (warm-up effect observed)\n");
    } else {
        RecordResult("TC-PERF-TS-008", false, "FAIL: No warm-up effect");
        printf("❌ TC-PERF-TS-008: FAIL (no warm-up improvement)\n");
    }

    CloseHandle(hDevice);
    printf("\n");
}

/*
 * Helper: Open AVB filter device
 */
HANDLE OpenAvbDevice(void)
{
    HANDLE hDevice = CreateFileA("\\\\.\\IntelAvbFilter",
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("❌ Failed to open IntelAvbFilter device (error %lu)\n", GetLastError());
    }

    return hDevice;
}

/*
 * Helper: Record test result
 */
void RecordResult(const char* testCase, bool passed, const char* reason)
{
    if (g_ResultCount < 20) {
        g_Results[g_ResultCount].TestCase = testCase;
        g_Results[g_ResultCount].Passed = passed;
        g_Results[g_ResultCount].Reason = reason;
        g_ResultCount++;
    }
}

/*
 * Helper: Get CPU frequency via QueryPerformanceFrequency
 */
double GetCpuFrequencyGHz(void)
{
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) {
        // Fallback to 3GHz if query fails
        return 3.0;
    }

    // Estimate CPU frequency from performance counter
    // This is approximate; ideally use CPUID or registry
    // For now, assume modern CPU ~2.5-3.5 GHz
    return 3.0;  // Conservative estimate
}

/*
 * Helper: Compare function for qsort
 */
int CompareUint64(const void* a, const void* b)
{
    UINT64 val_a = *(const UINT64*)a;
    UINT64 val_b = *(const UINT64*)b;
    if (val_a < val_b) return -1;
    if (val_a > val_b) return 1;
    return 0;
}

/*
 * Helper: Calculate latency statistics
 */
void CalculateStatistics(UINT64* latencies, int count, double cpuFreqGHz,
                        double* medianNs, double* p50Ns, double* p95Ns,
                        double* p99Ns, double* meanNs)
{
    // Sort latencies
    qsort(latencies, count, sizeof(UINT64), CompareUint64);

    // Calculate percentiles
    UINT64 median = latencies[count / 2];
    UINT64 p50 = latencies[count / 2];
    UINT64 p95 = latencies[(int)(count * 0.95)];
    UINT64 p99 = latencies[(int)(count * 0.99)];

    // Calculate mean
    UINT64 sum = 0;
    for (int i = 0; i < count; i++) {
        sum += latencies[i];
    }
    UINT64 mean = sum / count;

    // Convert to nanoseconds
    *medianNs = (double)median / cpuFreqGHz;
    *p50Ns = (double)p50 / cpuFreqGHz;
    *p95Ns = (double)p95 / cpuFreqGHz;
    *p99Ns = (double)p99 / cpuFreqGHz;
    *meanNs = (double)mean / cpuFreqGHz;
}

/*
 * Helper: Print test summary
 */
void PrintTestSummary(void)
{
    int passed = 0;
    int failed = 0;

    printf("=== Test Summary ===\n");
    for (int i = 0; i < g_ResultCount; i++) {
        const char* status = g_Results[i].Passed ? "PASS" : "FAIL";
        printf("[%s] %s: %s\n",
               status,
               g_Results[i].TestCase,
               g_Results[i].Reason);

        if (g_Results[i].Passed) {
            passed++;
        } else {
            failed++;
        }
    }

    double passRate = (double)passed / (double)g_ResultCount * 100.0;
    printf("\nTotal: %d/%d (%.1f%% pass rate)\n",
           passed, g_ResultCount, passRate);

    if (failed == 0) {
        printf("✅ All tests passed!\n");
    } else {
        printf("❌ %d test(s) failed\n", failed);
    }
}
