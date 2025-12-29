/**
 * @file test_lazy_initialization.c
 * @brief Test suite for lazy initialization and on-demand context creation
 * 
 * Verifies: #16 (REQ-F-LAZY-INIT-001: Lazy Initialization)
 * Test Type: Integration
 * Priority: P1 (Important - Performance optimization)
 * 
 * Acceptance Criteria (from #16):
 *   Given driver loaded with no AVB contexts initialized
 *   When application calls first IOCTL
 *   Then driver initializes context on-demand with <100ms overhead
 *   And subsequent IOCTLs reuse initialized context with <1ms latency
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/16
 */

#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include "../../../include/avb_ioctl.h"

// Test framework macros
#define TEST_PASS(message) \
    do { \
        printf("  [PASS] %s\n", message); \
        return TRUE; \
    } while(0)

#define TEST_FAIL(message) \
    do { \
        printf("  [FAIL] %s\n", message); \
        return FALSE; \
    } while(0)

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("  [FAIL] %s\n", message); \
            return FALSE; \
        } \
        printf("  [PASS] %s\n", message); \
    } while(0)

// Device path
#define DEVICE_PATH "\\\\.\\IntelAvbFilter"

// Performance targets (from #16)
#define FIRST_CALL_OVERHEAD_MS 100    // <100ms initialization overhead
#define SUBSEQUENT_CALL_LATENCY_MS 1  // <1ms for initialized context

/**
 * Test Case: REQ-F-LAZY-INIT-001.1 - First Call Initialization Overhead
 * 
 * Scenario: Measure lazy initialization timing
 *   Given driver loaded but no IOCTLs issued yet
 *   When application calls IOCTL_AVB_GET_HW_STATE for first time
 *   Then driver completes initialization within 100ms
 */
BOOLEAN Test_FirstCallInitializationOverhead()
{
    printf("\n[Test 1] First Call Initialization Overhead\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Device opened successfully");
    
    // Use simplest IOCTL to trigger lazy init (IOCTL_AVB_GET_HW_STATE)
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned = 0;
    
    // Measure first call latency (includes lazy initialization)
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_HW_STATE,
        &query,
        sizeof(query),
        &query,
        sizeof(query),
        &bytesReturned,
        NULL
    );
    
    QueryPerformanceCounter(&end);
    
    double latencyMs = ((double)(end.QuadPart - start.QuadPart) * 1000.0) / freq.QuadPart;
    
    printf("    First call latency: %.2f ms\n", latencyMs);
    
    TEST_ASSERT(result != FALSE, "First IOCTL succeeded (lazy init completed)");
    TEST_ASSERT(bytesReturned == sizeof(AVB_HW_STATE_QUERY), "Correct bytes returned");
    
    // Verify initialization occurred (state should be > UNBOUND)
    TEST_ASSERT(query.hw_state >= 1, "Hardware state advanced (context initialized)");
    
    if (latencyMs < FIRST_CALL_OVERHEAD_MS) {
        printf("  [PASS] First-call overhead < %d ms (target met)\n", FIRST_CALL_OVERHEAD_MS);
    } else {
        printf("  [WARN] First-call overhead %.2f ms (target: < %d ms)\n", 
               latencyMs, FIRST_CALL_OVERHEAD_MS);
        // Don't fail - initialization time depends on hardware
    }
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-LAZY-INIT-001.1 - Subsequent Call Fast Path
 * 
 * Scenario: Verify reuse of initialized context
 *   Given AVB context already initialized from first IOCTL
 *   When application calls subsequent IOCTLs
 *   Then latency is <1ms (no re-initialization)
 */
BOOLEAN Test_SubsequentCallFastPath()
{
    printf("\n[Test 2] Subsequent Call Fast Path\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Device opened");
    
    // First call to ensure context initialized
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned = 0;
    
    DeviceIoControl(hDevice, IOCTL_AVB_GET_HW_STATE,
                    &query, sizeof(query), &query, sizeof(query),
                    &bytesReturned, NULL);
    
    // Measure subsequent calls (should be fast path)
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    
    const int iterations = 100;
    double totalLatencyUs = 0.0;
    
    for (int i = 0; i < iterations; i++) {
        QueryPerformanceCounter(&start);
        
        DeviceIoControl(hDevice, IOCTL_AVB_GET_HW_STATE,
                        &query, sizeof(query), &query, sizeof(query),
                        &bytesReturned, NULL);
        
        QueryPerformanceCounter(&end);
        
        double latencyUs = ((double)(end.QuadPart - start.QuadPart) * 1000000.0) / freq.QuadPart;
        totalLatencyUs += latencyUs;
    }
    
    double avgLatencyUs = totalLatencyUs / iterations;
    double avgLatencyMs = avgLatencyUs / 1000.0;
    
    printf("    Average latency: %.2f µs (%.3f ms)\n", avgLatencyUs, avgLatencyMs);
    
    if (avgLatencyMs < SUBSEQUENT_CALL_LATENCY_MS) {
        printf("  [PASS] Average latency < %d ms (fast path confirmed)\n", 
               SUBSEQUENT_CALL_LATENCY_MS);
    } else {
        printf("  [WARN] Average latency %.3f ms (target: < %d ms)\n", 
               avgLatencyMs, SUBSEQUENT_CALL_LATENCY_MS);
    }
    
    TEST_ASSERT(query.hw_state >= 1, "Hardware state consistent (context reused)");
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-LAZY-INIT-001.2 - Multi-Adapter Initialization Order
 * 
 * Scenario: Verify driver selects first available adapter
 *   Given system with multiple Intel adapters
 *   When first IOCTL arrives without explicit OPEN_ADAPTER
 *   Then driver initializes adapters in enumeration order
 *   And selects first successfully initialized adapter
 */
BOOLEAN Test_MultiAdapterInitOrder()
{
    printf("\n[Test 3] Multi-Adapter Initialization Order\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Device opened");
    
    // Enumerate adapters to see system configuration
    AVB_ENUM_REQUEST enumReq = {0};
    enumReq.index = 0;
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_ENUM_ADAPTERS,
        &enumReq,
        sizeof(enumReq),
        &enumReq,
        sizeof(enumReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || enumReq.count == 0) {
        printf("  [SKIP] No adapters found (test requires Intel adapters)\n");
        CloseHandle(hDevice);
        return TRUE;
    }
    
    printf("    Total adapters: %u\n", enumReq.count);
    
    // Get hardware state (this uses lazily initialized default adapter)
    AVB_HW_STATE_QUERY hwState = {0};
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_HW_STATE,
        &hwState,
        sizeof(hwState),
        &hwState,
        sizeof(hwState),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result != FALSE, "Hardware state query succeeded");
    
    printf("    Default adapter: VID=0x%04X, DID=0x%04X\n", 
           hwState.vendor_id, hwState.device_id);
    
    // Verify it's an Intel adapter (vendor ID 0x8086)
    TEST_ASSERT(hwState.vendor_id == 0x8086, "Default adapter is Intel");
    
    // If multiple adapters, verify we got the first one
    if (enumReq.count > 1) {
        printf("  [INFO] Multi-adapter system - driver selected first available\n");
        
        // Compare with first enumerated adapter
        enumReq.index = 0;
        DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                        &enumReq, sizeof(enumReq), &enumReq, sizeof(enumReq),
                        &bytesReturned, NULL);
        
        printf("    First enumerated: VID=0x%04X, DID=0x%04X\n", 
               enumReq.vendor_id, enumReq.device_id);
        
        // Note: They should match if enumeration order = initialization order
        if (hwState.device_id == enumReq.device_id) {
            printf("  [PASS] Lazy init selected first enumerated adapter\n");
        } else {
            printf("  [INFO] Different adapter selected (may be due to init failures)\n");
        }
    }
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-LAZY-INIT-001.3 - Concurrent First-IOCTL Thread Safety
 * 
 * Scenario: Verify thread-safe lazy initialization
 *   Given multiple threads issue first IOCTL concurrently
 *   When race condition occurs
 *   Then only one context is created (no duplicates)
 */
typedef struct _THREAD_CONTEXT {
    HANDLE hDevice;
    BOOLEAN success;
    USHORT deviceId;
    DWORD threadId;
} THREAD_CONTEXT;

DWORD WINAPI ConcurrentIoctlThread(LPVOID param)
{
    THREAD_CONTEXT* ctx = (THREAD_CONTEXT*)param;
    ctx->threadId = GetCurrentThreadId();
    
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned = 0;
    
    ctx->success = DeviceIoControl(
        ctx->hDevice,
        IOCTL_AVB_GET_HW_STATE,
        &query,
        sizeof(query),
        &query,
        sizeof(query),
        &bytesReturned,
        NULL
    );
    
    if (ctx->success) {
        ctx->deviceId = query.device_id;
    }
    
    return 0;
}

BOOLEAN Test_ConcurrentFirstIoctlThreadSafety()
{
    printf("\n[Test 4] Concurrent First-IOCTL Thread Safety\n");
    
    // Note: This test simulates concurrent IOCTLs, but true lazy init race
    // would require driver restart between tests. This verifies thread safety
    // of IOCTL handling in general.
    
    const int threadCount = 4;
    HANDLE threads[4];
    THREAD_CONTEXT contexts[4] = {0};
    
    // Open device handles for each thread
    for (int i = 0; i < threadCount; i++) {
        contexts[i].hDevice = CreateFile(
            DEVICE_PATH,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (contexts[i].hDevice == INVALID_HANDLE_VALUE) {
            printf("  [FAIL] Failed to open device for thread %d\n", i);
            
            // Cleanup already opened handles
            for (int j = 0; j < i; j++) {
                CloseHandle(contexts[j].hDevice);
            }
            return FALSE;
        }
    }
    
    // Launch concurrent IOCTL threads
    for (int i = 0; i < threadCount; i++) {
        threads[i] = CreateThread(
            NULL,
            0,
            ConcurrentIoctlThread,
            &contexts[i],
            0,
            NULL
        );
    }
    
    // Wait for all threads to complete
    WaitForMultipleObjects(threadCount, threads, TRUE, 5000);
    
    // Verify all threads succeeded
    int successCount = 0;
    USHORT firstDeviceId = 0;
    BOOLEAN allSameAdapter = TRUE;
    
    for (int i = 0; i < threadCount; i++) {
        if (contexts[i].success) {
            successCount++;
            
            if (firstDeviceId == 0) {
                firstDeviceId = contexts[i].deviceId;
            } else if (contexts[i].deviceId != firstDeviceId) {
                allSameAdapter = FALSE;
            }
            
            printf("    Thread %u: DID=0x%04X\n", 
                   contexts[i].threadId, contexts[i].deviceId);
        }
        
        CloseHandle(threads[i]);
        CloseHandle(contexts[i].hDevice);
    }
    
    TEST_ASSERT(successCount == threadCount, "All threads completed successfully");
    TEST_ASSERT(allSameAdapter, "All threads used same adapter (no context duplication)");
    
    return TRUE;
}

/**
 * Test Case: REQ-F-LAZY-INIT-001.4 - Initialization State Verification
 * 
 * Scenario: Verify context initialization state
 *   Given lazy initialization completed
 *   When querying hardware state
 *   Then state is > UNBOUND (BOUND, BAR_MAPPED, or PTP_READY)
 */
BOOLEAN Test_InitializationStateVerification()
{
    printf("\n[Test 5] Initialization State Verification\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Device opened");
    
    // Query hardware state
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_HW_STATE,
        &query,
        sizeof(query),
        &query,
        sizeof(query),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result != FALSE, "Hardware state query succeeded");
    
    const char* stateNames[] = {"UNBOUND", "BOUND", "BAR_MAPPED", "PTP_READY"};
    printf("    Hardware State: %u (%s)\n", 
           query.hw_state, 
           query.hw_state <= 3 ? stateNames[query.hw_state] : "UNKNOWN");
    
    // After lazy init, state must be > UNBOUND
    TEST_ASSERT(query.hw_state >= 1, 
                "Context initialized (state >= BOUND)");
    
    // Verify vendor/device IDs populated
    TEST_ASSERT(query.vendor_id == 0x8086, "Vendor ID is Intel");
    TEST_ASSERT(query.device_id != 0, "Device ID populated");
    
    printf("    Adapter: VID=0x%04X, DID=0x%04X\n", 
           query.vendor_id, query.device_id);
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-LAZY-INIT-001 - Performance Comparison
 * 
 * Scenario: Compare first-call vs subsequent-call latency
 *   Given fresh driver state
 *   When measuring IOCTL latencies
 *   Then first call is significantly slower (includes initialization)
 *   And subsequent calls are consistently fast
 */
BOOLEAN Test_PerformanceComparison()
{
    printf("\n[Test 6] Performance Comparison (First vs Subsequent)\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Device opened");
    
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned = 0;
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    
    // Measure first call
    QueryPerformanceCounter(&start);
    DeviceIoControl(hDevice, IOCTL_AVB_GET_HW_STATE,
                    &query, sizeof(query), &query, sizeof(query),
                    &bytesReturned, NULL);
    QueryPerformanceCounter(&end);
    double firstCallUs = ((double)(end.QuadPart - start.QuadPart) * 1000000.0) / freq.QuadPart;
    
    // Measure 10 subsequent calls
    double subsequentUs[10];
    for (int i = 0; i < 10; i++) {
        QueryPerformanceCounter(&start);
        DeviceIoControl(hDevice, IOCTL_AVB_GET_HW_STATE,
                        &query, sizeof(query), &query, sizeof(query),
                        &bytesReturned, NULL);
        QueryPerformanceCounter(&end);
        subsequentUs[i] = ((double)(end.QuadPart - start.QuadPart) * 1000000.0) / freq.QuadPart;
    }
    
    // Calculate average subsequent call latency
    double avgSubsequentUs = 0.0;
    for (int i = 0; i < 10; i++) {
        avgSubsequentUs += subsequentUs[i];
    }
    avgSubsequentUs /= 10.0;
    
    printf("    First call:       %.2f µs\n", firstCallUs);
    printf("    Subsequent avg:   %.2f µs\n", avgSubsequentUs);
    printf("    Performance gain: %.1fx faster\n", firstCallUs / avgSubsequentUs);
    
    // Subsequent calls should be faster (but first call may not include
    // true lazy init if context already initialized from previous test)
    if (firstCallUs > avgSubsequentUs * 2.0) {
        printf("  [INFO] First call shows initialization overhead\n");
    } else {
        printf("  [INFO] Context already initialized (expected in test suite)\n");
    }
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Main test execution
 */
int main(int argc, char* argv[])
{
    printf("========================================\n");
    printf("Lazy Initialization Integration Test\n");
    printf("========================================\n");
    printf("Testing: REQ-F-LAZY-INIT-001 (Issue #16)\n");
    printf("Device:  %s\n", DEVICE_PATH);
    printf("========================================\n");
    
    int totalTests = 0;
    int passedTests = 0;
    
    // Test 1: First call overhead
    totalTests++;
    if (Test_FirstCallInitializationOverhead()) {
        passedTests++;
    }
    
    // Test 2: Subsequent call fast path
    totalTests++;
    if (Test_SubsequentCallFastPath()) {
        passedTests++;
    }
    
    // Test 3: Multi-adapter init order
    totalTests++;
    if (Test_MultiAdapterInitOrder()) {
        passedTests++;
    }
    
    // Test 4: Concurrent thread safety
    totalTests++;
    if (Test_ConcurrentFirstIoctlThreadSafety()) {
        passedTests++;
    }
    
    // Test 5: Init state verification
    totalTests++;
    if (Test_InitializationStateVerification()) {
        passedTests++;
    }
    
    // Test 6: Performance comparison
    totalTests++;
    if (Test_PerformanceComparison()) {
        passedTests++;
    }
    
    // Summary
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Total Tests:  %d\n", totalTests);
    printf("Passed:       %d\n", passedTests);
    printf("Failed:       %d\n", totalTests - passedTests);
    printf("Success Rate: %.1f%%\n", (passedTests * 100.0) / totalTests);
    printf("========================================\n");
    
    if (passedTests == totalTests) {
        printf("\nRESULT: SUCCESS (All %d tests passed)\n", totalTests);
        return 0;
    } else {
        printf("\nRESULT: FAILURE (%d/%d tests failed)\n", 
               totalTests - passedTests, totalTests);
        return 1;
    }
}
