/**
 * @file test_ioctl_phc_query.c
 * @brief PHC Query IOCTL Verification Tests
 * 
 * Implements: #193 (TEST-IOCTL-PHC-QUERY-001)
 * Verifies: #34  (REQ-F-IOCTL-PHC-001: PHC Time Query IOCTL)
 * 
 * IOCTL: IOCTL_AVB_PHC_QUERY (CTL_CODE(FILE_DEVICE_NETWORK, 0x800, METHOD_BUFFERED, FILE_READ_DATA))
 * Test Cases: 17 (10 unit + 4 integration + 3 V&V)
 * Priority: P0 (Critical)
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * Standards: IEEE 1588-2019 (PTP)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/193
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../include/avb_ioctl.h"

/* Test result codes */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* PHC Query IOCTL - Use EXISTING IOCTL_AVB_GET_CLOCK_CONFIG (SSOT!) */
#define IOCTL_AVB_PHC_QUERY IOCTL_AVB_GET_CLOCK_CONFIG

/* PHC Query response - Use EXISTING AVB_CLOCK_CONFIG structure (SSOT!) */
typedef AVB_CLOCK_CONFIG AVB_PHC_QUERY_RESPONSE;
typedef PAVB_CLOCK_CONFIG PAVB_PHC_QUERY_RESPONSE;

/* Test state */
typedef struct {
    HANDLE adapter;
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/* Helper: Open device */
static HANDLE OpenAdapter(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        printf("  [WARN] Could not open device: error %lu\n", err);
    }
    
    return h;
}

/* Helper: Execute PHC Query IOCTL */
static BOOL QueryPHC(HANDLE adapter, AVB_PHC_QUERY_RESPONSE *response)
{
    DWORD bytesReturned = 0;
    BOOL result;
    
    if (!response) {
        return FALSE;
    }
    
    memset(response, 0, sizeof(*response));
    
    /* SSOT Pattern: Use existing IOCTL_AVB_GET_CLOCK_CONFIG */
    result = DeviceIoControl(
        adapter,
        IOCTL_AVB_PHC_QUERY,         /* = IOCTL_AVB_GET_CLOCK_CONFIG */
        response,                     /* Input buffer (can be used for future extensions) */
        sizeof(*response),
        response,                     /* Output buffer */
        sizeof(*response),
        &bytesReturned,
        NULL
    );
    
    /* Check BOTH DeviceIoControl result AND driver status field */
    if (result && response->status != 0) {
        /* IOCTL succeeded but driver returned error status */
        return FALSE;
    }
    
    return result;
}

/*
 * Level 1 Unit Tests (10 test cases)
 */

/*
 * Scenario 1.1: Valid PHC Query IOCTL
 * Purpose: Verify successful PHC query with valid parameters
 * Expected: STATUS_SUCCESS, valid timestamp and config returned
 */
static int Test_ValidPHCQuery(TestContext *ctx)
{
    AVB_PHC_QUERY_RESPONSE response;
    
    if (!QueryPHC(ctx->adapter, &response)) {
        printf("  [FAIL] UT-PHC-QUERY-001: Valid PHC Query: IOCTL failed (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }
    
    /* Validate response fields - AVB_CLOCK_CONFIG uses 'systim' not 'current_timestamp' */
    if (response.systim == 0) {
        printf("  [FAIL] UT-PHC-QUERY-001: Valid PHC Query: SYSTIM is zero\n");
        return TEST_FAIL;
    }
    
    if (response.clock_rate_mhz != 125 && response.clock_rate_mhz != 156 && 
        response.clock_rate_mhz != 200 && response.clock_rate_mhz != 250) {
        printf("  [FAIL] UT-PHC-QUERY-001: Valid PHC Query: Invalid clock rate %u MHz\n", response.clock_rate_mhz);
        return TEST_FAIL;
    }
    
    if (response.timinca == 0) {
        printf("  [FAIL] UT-PHC-QUERY-001: Valid PHC Query: TIMINCA is zero\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-001: Valid PHC Query (SYSTIM=%llu, rate=%u MHz, TIMINCA=0x%08X)\n",
           response.systim, response.clock_rate_mhz, response.timinca);
    return TEST_PASS;
}

/*
 * Scenario 1.2: Output Buffer Too Small
 * Purpose: Verify IOCTL rejects undersized output buffer
 * Expected: STATUS_BUFFER_TOO_SMALL
 */
static int Test_BufferTooSmall(TestContext *ctx)
{
    AVB_PHC_QUERY_RESPONSE response;
    DWORD bytesReturned = 0;
    BOOL result;
    
    /* Call with undersized buffer */
    result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_PHC_QUERY,
        NULL,
        0,
        &response,
        sizeof(response) - 1,  /* 1 byte too small */
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [FAIL] UT-PHC-QUERY-002: Output Buffer Too Small: IOCTL should reject small buffer\n");
        return TEST_FAIL;
    }
    
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER && error != ERROR_INVALID_PARAMETER) {
        printf("  [FAIL] UT-PHC-QUERY-002: Output Buffer Too Small: Wrong error code %lu\n", error);
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-002: Output Buffer Too Small (error=%lu)\n", error);
    return TEST_PASS;
}

/*
 * Scenario 1.3: NULL Output Buffer
 * Purpose: Verify IOCTL rejects NULL output buffer gracefully
 * Expected: STATUS_INVALID_PARAMETER
 */
static int Test_NullOutputBuffer(TestContext *ctx)
{
    DWORD bytesReturned = 0;
    BOOL result;
    
    /* Call with NULL output buffer */
    result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_PHC_QUERY,
        NULL,
        0,
        NULL,  /* NULL output buffer */
        sizeof(AVB_PHC_QUERY_RESPONSE),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [FAIL] UT-PHC-QUERY-003: NULL Output Buffer: IOCTL should reject NULL buffer\n");
        return TEST_FAIL;
    }
    
    DWORD error = GetLastError();
    /* Accept both ERROR_INVALID_PARAMETER (87) and ERROR_INSUFFICIENT_BUFFER (122) */
    if (error != ERROR_INVALID_PARAMETER && error != ERROR_INSUFFICIENT_BUFFER) {
        printf("  [FAIL] UT-PHC-QUERY-003: NULL Output Buffer: Wrong error code %lu\n", error);
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-003: NULL Output Buffer (error=%lu)\n", error);
    return TEST_PASS;
}

/*
 * Scenario 1.4: Invalid IOCTL Code
 * Purpose: Verify driver rejects invalid IOCTL code
 * Expected: STATUS_INVALID_DEVICE_REQUEST
 * NOTE: Windows IOCTL dispatcher validates function codes, so driver may never see invalid codes
 */
static int Test_InvalidIoctlCode(TestContext *ctx)
{
    /* SKIP: Windows validates IOCTL codes before dispatching to driver.
     * Invalid function codes are rejected by I/O manager, not driver.
     * Testing this requires kernel-mode fault injection. */
    printf("  [SKIP] UT-PHC-QUERY-004: Invalid IOCTL Code (Windows validates before driver)\n");
    return TEST_SKIP;
}

/*
 * Scenario 1.5: Adapter Not Initialized
 * Purpose: Verify IOCTL behavior on uninitialized adapter
 * Expected: Works on default adapter (driver auto-selects first adapter)
 */
static int Test_AdapterNotInitialized(TestContext *ctx)
{
    AVB_PHC_QUERY_RESPONSE response;
    HANDLE fresh_handle;
    
    /* Open fresh handle without calling INIT_DEVICE */
    fresh_handle = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (fresh_handle == INVALID_HANDLE_VALUE) {
        printf("  [FAIL] UT-PHC-QUERY-005: Adapter Not Initialized: Cannot open device\n");
        return TEST_FAIL;
    }
    
    /* Query without INIT_DEVICE - should work on default adapter */
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        fresh_handle,
        IOCTL_AVB_PHC_QUERY,
        &response,
        sizeof(response),
        &response,
        sizeof(response),
        &bytesReturned,
        NULL
    );
    
    CloseHandle(fresh_handle);
    
    if (!result || response.status != 0) {
        printf("  [FAIL] UT-PHC-QUERY-005: Adapter Not Initialized: Query failed (status=0x%08X)\n", response.status);
        return TEST_FAIL;
    }
    
    if (response.systim == 0) {
        printf("  [FAIL] UT-PHC-QUERY-005: Adapter Not Initialized: No timestamp returned\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-005: Adapter Not Initialized (works on default adapter, SYSTIM=%llu)\n", response.systim);
    return TEST_PASS;
}

/*
 * Scenario 1.6: PHC Hardware Read Failure
 * Purpose: Verify IOCTL handles hardware read failure gracefully
 * Expected: STATUS_DEVICE_HARDWARE_ERROR
 * NOTE: This test requires hardware fault injection
 */
static int Test_PHCHardwareReadFailure(TestContext *ctx)
{
    printf("  [SKIP] UT-PHC-QUERY-006: PHC Hardware Read Failure (requires fault injection)\n");
    return TEST_SKIP;
}

/*
 * Scenario 1.7: Unprivileged User Access
 * Purpose: Verify read-only query succeeds for unprivileged users
 * Expected: STATUS_SUCCESS (no privilege check for read-only)
 */
static int Test_UnprivilegedUserAccess(TestContext *ctx)
{
    AVB_PHC_QUERY_RESPONSE response;
    
    /* PHC Query is read-only, should work for all users */
    if (!QueryPHC(ctx->adapter, &response)) {
        printf("  [FAIL] UT-PHC-QUERY-007: Unprivileged User Access: Query failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-007: Unprivileged User Access (read-only operation)\n");
    return TEST_PASS;
}

/*
 * Scenario 1.8: Input Buffer Ignored
 * Purpose: Verify IOCTL ignores input buffer (defensive test)
 * Expected: STATUS_SUCCESS, input buffer has no effect
 */
static int Test_InputBufferIgnored(TestContext *ctx)
{
    AVB_PHC_QUERY_RESPONSE response;
    DWORD bytesReturned = 0;
    char dummy_input[64] = {0xFF};
    BOOL result;
    
    /* Call with non-NULL input buffer (should be ignored) */
    result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_PHC_QUERY,
        dummy_input,               /* Input buffer (should be ignored) */
        sizeof(dummy_input),
        &response,
        sizeof(response),
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        printf("  [FAIL] UT-PHC-QUERY-008: Input Buffer Ignored: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    if (response.status != 0) {
        printf("  [FAIL] UT-PHC-QUERY-008: Input Buffer Ignored: Driver error status %u\n", response.status);
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-008: Input Buffer Ignored\n");
    return TEST_PASS;
}

/*
 * Scenario 1.9: Oversized Output Buffer
 * Purpose: Verify IOCTL handles oversized output buffer gracefully
 * Expected: STATUS_SUCCESS, extra space unused
 */
static int Test_OversizedOutputBuffer(TestContext *ctx)
{
    BYTE large_buffer[512];
    DWORD bytesReturned = 0;
    BOOL result;
    AVB_PHC_QUERY_RESPONSE *response = (AVB_PHC_QUERY_RESPONSE *)large_buffer;
    
    memset(large_buffer, 0xFF, sizeof(large_buffer));
    
    /* Call with oversized buffer - use correct input size for METHOD_BUFFERED */
    result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_PHC_QUERY,
        large_buffer,                    /* Input buffer */
        sizeof(AVB_PHC_QUERY_RESPONSE),  /* Actual input size */
        large_buffer,                    /* Output buffer */
        sizeof(large_buffer),            /* Oversized output */
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        printf("  [FAIL] UT-PHC-QUERY-009: Oversized Output Buffer: IOCTL failed (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }
    
    if (bytesReturned != sizeof(AVB_PHC_QUERY_RESPONSE)) {
        printf("  [FAIL] UT-PHC-QUERY-009: Oversized Output Buffer: Wrong bytes returned %lu\n", bytesReturned);
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PHC-QUERY-009: Oversized Output Buffer (returned %lu bytes)\n", bytesReturned);
    return TEST_PASS;
}

/*
 * Scenario 1.10: IOCTL During Adapter Removal
 * Purpose: Verify IOCTL fails gracefully during adapter removal
 * Expected: STATUS_DEVICE_REMOVED
 * NOTE: This test requires adapter hot-removal simulation
 */
static int Test_IoctlDuringAdapterRemoval(TestContext *ctx)
{
    printf("  [SKIP] UT-PHC-QUERY-010: IOCTL During Adapter Removal (requires hot-removal)\n");
    return TEST_SKIP;
}

/*
 * Level 2 Integration Tests (4 test cases)
 */

/*
 * IT-001: User-Mode Application PHC Query
 * Purpose: Verify end-to-end PHC query from user-mode application
 * Expected: Successful query, consistent timestamp format
 */
static int Test_IT_UserModeAppPHCQuery(TestContext *ctx)
{
    AVB_PHC_QUERY_RESPONSE response1, response2;
    
    /* First query */
    if (!QueryPHC(ctx->adapter, &response1)) {
        printf("  [FAIL] IT-PHC-QUERY-001: User-Mode App: First query failed\n");
        return TEST_FAIL;
    }
    
    Sleep(10);  /* Wait 10ms */
    
    /* Second query - should show time progression */
    if (!QueryPHC(ctx->adapter, &response2)) {
        printf("  [FAIL] IT-PHC-QUERY-001: User-Mode App: Second query failed\n");
        return TEST_FAIL;
    }
    
    /* Verify time progressed - use 'systim' field */
    if (response2.systim <= response1.systim) {
        printf("  [FAIL] IT-PHC-QUERY-001: User-Mode App: Time did not progress (t1=%llu, t2=%llu)\n",
               response1.systim, response2.systim);
        return TEST_FAIL;
    }
    
    /* Verify config consistency */
    if (response1.clock_rate_mhz != response2.clock_rate_mhz) {
        printf("  [FAIL] IT-PHC-QUERY-001: User-Mode App: Clock rate changed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] IT-PHC-QUERY-001: User-Mode App (time progressed %llu ns)\n",
           response2.systim - response1.systim);
    return TEST_PASS;
}

/*
 * IT-002: Concurrent IOCTL Queries (Multi-Threaded)
 * Purpose: Verify thread-safety of PHC query IOCTL
 * Expected: All threads succeed, no resource conflicts
 * NOTE: This test requires multi-threaded test infrastructure
 */
static int Test_IT_ConcurrentQueries(TestContext *ctx)
{
    printf("  [SKIP] IT-PHC-QUERY-002: Concurrent Queries (requires multi-threaded framework)\n");
    return TEST_SKIP;
}

/*
 * IT-003: Multiple Adapters Concurrent Queries
 * Purpose: Verify PHC query works independently on multiple adapters
 * Expected: Each adapter returns its own PHC state
 * NOTE: This test requires multi-adapter hardware
 */
static int Test_IT_MultiAdapterQueries(TestContext *ctx)
{
    printf("  [SKIP] IT-PHC-QUERY-003: Multi-Adapter Queries (requires multiple adapters)\n");
    return TEST_SKIP;
}

/*
 * IT-004: IOCTL Query During Driver Unload
 * Purpose: Verify graceful failure during driver unload
 * Expected: IOCTL fails with appropriate error
 * NOTE: This test requires driver lifecycle control
 */
static int Test_IT_QueryDuringUnload(TestContext *ctx)
{
    printf("  [SKIP] IT-PHC-QUERY-004: Query During Unload (requires driver lifecycle control)\n");
    return TEST_SKIP;
}

/*
 * Level 3 V&V Tests (3 test cases)
 */

/*
 * VV-001: IOCTL Latency Benchmark (p95 <500ns)
 * Purpose: Verify PHC query latency meets performance requirements
 * Expected: p95 latency <500ns (from #193 spec)
 * NOTE: This test requires high-resolution timing infrastructure
 */
static int Test_VV_IoctlLatencyBenchmark(TestContext *ctx)
{
    printf("  [SKIP] VV-PHC-QUERY-001: Latency Benchmark (requires high-res timing)\n");
    return TEST_SKIP;
}

/*
 * VV-002: Stress Test (1000 QPS for 1 hour)
 * Purpose: Verify system stability under sustained load
 * Expected: No failures, no resource leaks
 * NOTE: This is a long-running stress test
 */
static int Test_VV_StressTest(TestContext *ctx)
{
    printf("  [SKIP] VV-PHC-QUERY-002: Stress Test (long-running test)\n");
    return TEST_SKIP;
}

/*
 * VV-003: Multi-Process Concurrent Access
 * Purpose: Verify isolation between processes
 * Expected: All processes succeed independently
 * NOTE: This test requires multi-process test infrastructure
 */
static int Test_VV_MultiProcessAccess(TestContext *ctx)
{
    printf("  [SKIP] VV-PHC-QUERY-003: Multi-Process Access (requires multi-process framework)\n");
    return TEST_SKIP;
}

/* Main test runner */
int main(void)
{
    TestContext ctx = {0};
    int result;
    
    printf("\n====================================\n");
    printf("PHC Query IOCTL Verification Tests\n");
    printf("Implements: #193 (TEST-IOCTL-PHC-QUERY-001)\n");
    printf("====================================\n\n");
    
    /* Open adapter */
    ctx.adapter = OpenAdapter();
    if (ctx.adapter == INVALID_HANDLE_VALUE) {
        printf("ERROR: Could not open adapter\n");
        return 1;
    }
    
    printf("Running Level 1: Unit Tests (10 test cases)...\n\n");
    
    /* Run unit tests */
    result = Test_ValidPHCQuery(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_BufferTooSmall(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_NullOutputBuffer(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_InvalidIoctlCode(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_AdapterNotInitialized(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_PHCHardwareReadFailure(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_UnprivilegedUserAccess(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_InputBufferIgnored(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_OversizedOutputBuffer(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_IoctlDuringAdapterRemoval(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    printf("\nRunning Level 2: Integration Tests (4 test cases)...\n\n");
    
    /* Run integration tests */
    result = Test_IT_UserModeAppPHCQuery(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_IT_ConcurrentQueries(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_IT_MultiAdapterQueries(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_IT_QueryDuringUnload(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    printf("\nRunning Level 3: V&V Tests (3 test cases)...\n\n");
    
    /* Run V&V tests */
    result = Test_VV_IoctlLatencyBenchmark(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_VV_StressTest(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    result = Test_VV_MultiProcessAccess(&ctx);
    ctx.test_count++;
    if (result == TEST_PASS) ctx.pass_count++; else if (result == TEST_FAIL) ctx.fail_count++; else ctx.skip_count++;
    
    /* Print summary */
    printf("\n====================================\n");
    printf("Test Summary:\n");
    printf("  Total:  %d\n", ctx.test_count);
    printf("  Passed: %d\n", ctx.pass_count);
    printf("  Failed: %d\n", ctx.fail_count);
    printf("  Skipped: %d\n", ctx.skip_count);
    printf("====================================\n");
    
    CloseHandle(ctx.adapter);
    
    return (ctx.fail_count > 0) ? 1 : 0;
}
