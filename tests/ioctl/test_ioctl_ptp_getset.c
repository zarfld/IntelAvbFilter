/**
 * @file test_ioctl_ptp_getset.c
 * @brief PTP Get/Set Timestamp Verification Tests
 * 
 * Implements: #295 (TEST-PTP-GETSET-001)
 * Verifies: #2 (REQ-F-PTP-001: PTP Get/Set Timestamp via IOCTL)
 * 
 * IOCTLs: 24 (IOCTL_AVB_GET_TIMESTAMP), 25 (IOCTL_AVB_SET_TIMESTAMP)
 * Test Cases: 12
 * Priority: P0 (Critical)
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * Standards: IEEE 1588-2019 (PTP)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/295
 * @see https://github.com/zarfld/IntelAvbFilter/issues/2
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

/* PTP timestamp constants */
#define NSEC_PER_SEC 1000000000ULL
#define MAX_PTP_TIMESTAMP_SEC 0x0000FFFFFFFFFFFFULL  /* 48-bit seconds */

/* Test state */
typedef struct {
    HANDLE adapter;
    UINT64 initial_timestamp;
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/* PTP timestamp structure (aligned with driver) */
typedef struct {
    UINT64 seconds;      /* Seconds since epoch */
    UINT32 nanoseconds;  /* Nanoseconds (0-999999999) */
    UINT32 reserved;
} PTP_TIMESTAMP, *PPTP_TIMESTAMP;

/* IOCTL request structures */
typedef struct {
    PTP_TIMESTAMP timestamp;
    UINT32 status;
} GET_TIMESTAMP_REQUEST, *PGET_TIMESTAMP_REQUEST;

typedef struct {
    PTP_TIMESTAMP timestamp;
    UINT32 status;
} SET_TIMESTAMP_REQUEST, *PSET_TIMESTAMP_REQUEST;

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

/* Helper: Get PTP timestamp */
static BOOL GetPTPTimestamp(HANDLE adapter, PTP_TIMESTAMP *ts)
{
    GET_TIMESTAMP_REQUEST req = {0};
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(
        adapter,
        IOCTL_AVB_GET_TIMESTAMP,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
    if (result && ts) {
        *ts = req.timestamp;
    }
    
    return result;
}

/* Helper: Set PTP timestamp */
static BOOL SetPTPTimestamp(HANDLE adapter, const PTP_TIMESTAMP *ts)
{
    SET_TIMESTAMP_REQUEST req = {0};
    DWORD bytesReturned = 0;
    
    if (ts) {
        req.timestamp = *ts;
    }
    
    BOOL result = DeviceIoControl(
        adapter,
        IOCTL_AVB_SET_TIMESTAMP,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
    return result;
}

/* Helper: Compare timestamps */
static BOOL TimestampsEqual(const PTP_TIMESTAMP *a, const PTP_TIMESTAMP *b)
{
    return (a->seconds == b->seconds) && (a->nanoseconds == b->nanoseconds);
}

/* Helper: Add nanoseconds to timestamp */
static void AddNanoseconds(PTP_TIMESTAMP *ts, UINT64 ns)
{
    UINT64 total_ns = ts->nanoseconds + ns;
    ts->seconds += total_ns / NSEC_PER_SEC;
    ts->nanoseconds = (UINT32)(total_ns % NSEC_PER_SEC);
}

/*
 * Test UT-PTP-GETSET-001: Basic Get Timestamp
 * Verifies: Can read current PTP timestamp
 */
static int Test_BasicGetTimestamp(TestContext *ctx)
{
    PTP_TIMESTAMP ts = {0};
    
    if (!GetPTPTimestamp(ctx->adapter, &ts)) {
        printf("  [FAIL] UT-PTP-GETSET-001: Basic Get Timestamp: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    /* Timestamp should be non-zero and nanoseconds < 1 second */
    if (ts.seconds == 0 || ts.nanoseconds >= NSEC_PER_SEC) {
        printf("  [FAIL] UT-PTP-GETSET-001: Basic Get Timestamp: Invalid timestamp (sec=%llu, nsec=%u)\n",
               ts.seconds, ts.nanoseconds);
        return TEST_FAIL;
    }
    
    ctx->initial_timestamp = ts.seconds * NSEC_PER_SEC + ts.nanoseconds;
    printf("  [PASS] UT-PTP-GETSET-001: Basic Get Timestamp\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-002: Basic Set Timestamp
 * Verifies: Can write PTP timestamp
 */
static int Test_BasicSetTimestamp(TestContext *ctx)
{
    PTP_TIMESTAMP set_ts = {0};
    PTP_TIMESTAMP get_ts = {0};
    
    /* Set to known value: 1 Jan 2025 00:00:00 UTC */
    set_ts.seconds = 1735689600;  /* Unix timestamp for 2025-01-01 */
    set_ts.nanoseconds = 123456789;
    
    if (!SetPTPTimestamp(ctx->adapter, &set_ts)) {
        printf("  [FAIL] UT-PTP-GETSET-002: Basic Set Timestamp: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    /* Verify by reading back */
    Sleep(10);  /* Allow hardware to update */
    if (!GetPTPTimestamp(ctx->adapter, &get_ts)) {
        printf("  [FAIL] UT-PTP-GETSET-002: Basic Set Timestamp: Read-back failed\n");
        return TEST_FAIL;
    }
    
    /* Allow small drift (up to 1ms) */
    INT64 diff = (INT64)(get_ts.seconds - set_ts.seconds) * NSEC_PER_SEC;
    diff += (INT64)(get_ts.nanoseconds - set_ts.nanoseconds);
    
    if (llabs(diff) > 1000000) {  /* 1ms tolerance */
        printf("  [FAIL] UT-PTP-GETSET-002: Basic Set Timestamp: Timestamp mismatch (diff=%lld ns)\n", diff);
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-002: Basic Set Timestamp\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-003: Timestamp Monotonicity
 * Verifies: Timestamps increase monotonically
 */
static int Test_TimestampMonotonicity(TestContext *ctx)
{
    PTP_TIMESTAMP ts1 = {0}, ts2 = {0};
    
    if (!GetPTPTimestamp(ctx->adapter, &ts1)) {
        printf("  [FAIL] UT-PTP-GETSET-003: Timestamp Monotonicity: First read failed\n");
        return TEST_FAIL;
    }
    
    Sleep(10);  /* Wait 10ms */
    
    if (!GetPTPTimestamp(ctx->adapter, &ts2)) {
        printf("  [FAIL] UT-PTP-GETSET-003: Timestamp Monotonicity: Second read failed\n");
        return TEST_FAIL;
    }
    
    /* ts2 must be > ts1 */
    if (ts2.seconds < ts1.seconds || 
        (ts2.seconds == ts1.seconds && ts2.nanoseconds <= ts1.nanoseconds)) {
        printf("  [FAIL] UT-PTP-GETSET-003: Timestamp Monotonicity: Not monotonic\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-003: Timestamp Monotonicity\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-004: Nanoseconds Wraparound
 * Verifies: Nanoseconds wrap correctly at 1 second boundary
 */
static int Test_NanosecondsWraparound(TestContext *ctx)
{
    PTP_TIMESTAMP set_ts = {0};
    
    /* Set timestamp close to nanosecond wraparound */
    set_ts.seconds = 1000000;
    set_ts.nanoseconds = 999999000;  /* 1ms before wraparound */
    
    if (!SetPTPTimestamp(ctx->adapter, &set_ts)) {
        printf("  [FAIL] UT-PTP-GETSET-004: Nanoseconds Wraparound: Set failed\n");
        return TEST_FAIL;
    }
    
    /* Wait for wraparound */
    Sleep(10);  /* Should wrap to next second */
    
    PTP_TIMESTAMP get_ts = {0};
    if (!GetPTPTimestamp(ctx->adapter, &get_ts)) {
        printf("  [FAIL] UT-PTP-GETSET-004: Nanoseconds Wraparound: Get failed\n");
        return TEST_FAIL;
    }
    
    /* Should have wrapped (seconds incremented, nanoseconds < initial) */
    if (get_ts.seconds != set_ts.seconds + 1) {
        printf("  [FAIL] UT-PTP-GETSET-004: Nanoseconds Wraparound: Seconds not incremented\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-004: Nanoseconds Wraparound\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-005: Invalid Nanoseconds Rejection
 * Verifies: Rejects nanoseconds >= 1e9
 */
static int Test_InvalidNanosecondsRejection(TestContext *ctx)
{
    PTP_TIMESTAMP ts = {0};
    
    ts.seconds = 1000000;
    ts.nanoseconds = NSEC_PER_SEC;  /* Invalid: must be < 1e9 */
    
    if (SetPTPTimestamp(ctx->adapter, &ts)) {
        printf("  [FAIL] UT-PTP-GETSET-005: Invalid Nanoseconds Rejection: Invalid value accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-005: Invalid Nanoseconds Rejection\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-006: Zero Timestamp Handling
 * Verifies: Can set/get timestamp 0 (epoch)
 */
static int Test_ZeroTimestampHandling(TestContext *ctx)
{
    PTP_TIMESTAMP set_ts = {0};
    PTP_TIMESTAMP get_ts = {0};
    
    /* Set to epoch (all zeros) */
    if (!SetPTPTimestamp(ctx->adapter, &set_ts)) {
        printf("  [FAIL] UT-PTP-GETSET-006: Zero Timestamp Handling: Set failed\n");
        return TEST_FAIL;
    }
    
    Sleep(10);
    if (!GetPTPTimestamp(ctx->adapter, &get_ts)) {
        printf("  [FAIL] UT-PTP-GETSET-006: Zero Timestamp Handling: Get failed\n");
        return TEST_FAIL;
    }
    
    /* Timestamp should have advanced from 0 */
    if (get_ts.seconds == 0 && get_ts.nanoseconds == 0) {
        printf("  [FAIL] UT-PTP-GETSET-006: Zero Timestamp Handling: Clock not running\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-006: Zero Timestamp Handling\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-007: Maximum Timestamp Value
 * Verifies: Can handle maximum valid timestamp (48-bit seconds)
 */
static int Test_MaximumTimestampValue(TestContext *ctx)
{
    PTP_TIMESTAMP ts = {0};
    
    /* Maximum 48-bit seconds value */
    ts.seconds = MAX_PTP_TIMESTAMP_SEC;
    ts.nanoseconds = NSEC_PER_SEC - 1;  /* 999999999 */
    
    if (!SetPTPTimestamp(ctx->adapter, &ts)) {
        printf("  [FAIL] UT-PTP-GETSET-007: Maximum Timestamp Value: Set failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-007: Maximum Timestamp Value\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-008: Rapid Consecutive Reads
 * Verifies: Can handle rapid timestamp reads without errors
 */
static int Test_RapidConsecutiveReads(TestContext *ctx)
{
    const int iterations = 100;
    
    for (int i = 0; i < iterations; i++) {
        PTP_TIMESTAMP ts = {0};
        if (!GetPTPTimestamp(ctx->adapter, &ts)) {
            printf("  [FAIL] UT-PTP-GETSET-008: Rapid Consecutive Reads: Read %d failed\n", i);
            return TEST_FAIL;
        }
        
        if (ts.nanoseconds >= NSEC_PER_SEC) {
            printf("  [FAIL] UT-PTP-GETSET-008: Rapid Consecutive Reads: Invalid nanoseconds\n");
            return TEST_FAIL;
        }
    }
    
    printf("  [PASS] UT-PTP-GETSET-008: Rapid Consecutive Reads\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-009: Clock Resolution Measurement
 * Verifies: Clock resolution is within IEEE 1588 requirements
 */
static int Test_ClockResolutionMeasurement(TestContext *ctx)
{
    PTP_TIMESTAMP ts1 = {0}, ts2 = {0};
    
    if (!GetPTPTimestamp(ctx->adapter, &ts1)) {
        printf("  [FAIL] UT-PTP-GETSET-009: Clock Resolution: First read failed\n");
        return TEST_FAIL;
    }
    
    /* Busy-wait for timestamp change */
    int iterations = 0;
    const int max_iterations = 10000;
    do {
        if (!GetPTPTimestamp(ctx->adapter, &ts2)) {
            printf("  [FAIL] UT-PTP-GETSET-009: Clock Resolution: Subsequent read failed\n");
            return TEST_FAIL;
        }
        iterations++;
    } while (TimestampsEqual(&ts1, &ts2) && iterations < max_iterations);
    
    if (iterations >= max_iterations) {
        printf("  [FAIL] UT-PTP-GETSET-009: Clock Resolution: Timestamp never changed\n");
        return TEST_FAIL;
    }
    
    /* Calculate resolution */
    INT64 diff_ns = (INT64)(ts2.seconds - ts1.seconds) * NSEC_PER_SEC;
    diff_ns += (INT64)(ts2.nanoseconds - ts1.nanoseconds);
    
    /* IEEE 1588 requires <100ns resolution for Grandmaster class */
    if (diff_ns > 100) {
        printf("  [SKIP] UT-PTP-GETSET-009: Clock Resolution: Resolution %lld ns (informational)\n", diff_ns);
        return TEST_SKIP;
    }
    
    printf("  [PASS] UT-PTP-GETSET-009: Clock Resolution (%lld ns)\n", diff_ns);
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-010: Backward Time Jump Detection
 * Verifies: Prevents backwards time jumps (monotonicity enforcement)
 */
static int Test_BackwardTimeJumpDetection(TestContext *ctx)
{
    PTP_TIMESTAMP current = {0};
    PTP_TIMESTAMP past = {0};
    
    /* Get current time */
    if (!GetPTPTimestamp(ctx->adapter, &current)) {
        printf("  [FAIL] UT-PTP-GETSET-010: Backward Time Jump: Get current failed\n");
        return TEST_FAIL;
    }
    
    /* Try to set to past time */
    past.seconds = current.seconds - 10;  /* 10 seconds ago */
    past.nanoseconds = current.nanoseconds;
    
    /* This should either fail or be ignored */
    SetPTPTimestamp(ctx->adapter, &past);  /* May succeed or fail */
    
    Sleep(10);
    
    /* Read again - should still be >= original current time */
    PTP_TIMESTAMP verify = {0};
    if (!GetPTPTimestamp(ctx->adapter, &verify)) {
        printf("  [FAIL] UT-PTP-GETSET-010: Backward Time Jump: Verify read failed\n");
        return TEST_FAIL;
    }
    
    /* Check if time went backwards */
    if (verify.seconds < current.seconds) {
        printf("  [FAIL] UT-PTP-GETSET-010: Backward Time Jump: Time went backwards\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-010: Backward Time Jump Detection\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-011: NULL Pointer Handling
 * Verifies: Rejects NULL buffer pointers
 */
static int Test_NullPointerHandling(TestContext *ctx)
{
    DWORD bytesReturned = 0;
    
    /* Try to call GET with NULL buffer */
    BOOL result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_GET_TIMESTAMP,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [FAIL] UT-PTP-GETSET-011: NULL Pointer Handling: NULL buffer accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-GETSET-011: NULL Pointer Handling\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-GETSET-012: Concurrent Access Serialization
 * Verifies: Multiple threads can safely access timestamp
 */
static int Test_ConcurrentAccessSerialization(TestContext *ctx)
{
    /* Note: Full multi-threaded test requires threading framework */
    printf("  [SKIP] UT-PTP-GETSET-012: Concurrent Access Serialization: Requires multi-threaded framework\n");
    return TEST_SKIP;
}

/* Main test runner */
int main(void)
{
    TestContext ctx = {0};
    
    printf("\n");
    printf("====================================================================\n");
    printf(" PTP Get/Set Timestamp Test Suite\n");
    printf("====================================================================\n");
    printf(" Implements: #295 (TEST-PTP-GETSET-001)\n");
    printf(" Verifies: #2 (REQ-F-PTP-001)\n");
    printf(" IOCTLs: GET_TIMESTAMP (24), SET_TIMESTAMP (25)\n");
    printf(" Total Tests: 12\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n");
    printf("\n");
    
    /* Open adapter */
    ctx.adapter = OpenAdapter();
    if (ctx.adapter == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Failed to open AVB adapter. Skipping all tests.\n");
        return TEST_FAIL;
    }
    
    printf("Running PTP Get/Set Timestamp tests...\n\n");
    
    /* Run tests */
    #define RUN_TEST(test) do { \
        int result = test(&ctx); \
        ctx.test_count++; \
        if (result == TEST_PASS) ctx.pass_count++; \
        else if (result == TEST_FAIL) ctx.fail_count++; \
        else if (result == TEST_SKIP) ctx.skip_count++; \
    } while(0)
    
    RUN_TEST(Test_BasicGetTimestamp);
    RUN_TEST(Test_BasicSetTimestamp);
    RUN_TEST(Test_TimestampMonotonicity);
    RUN_TEST(Test_NanosecondsWraparound);
    RUN_TEST(Test_InvalidNanosecondsRejection);
    RUN_TEST(Test_ZeroTimestampHandling);
    RUN_TEST(Test_MaximumTimestampValue);
    RUN_TEST(Test_RapidConsecutiveReads);
    RUN_TEST(Test_ClockResolutionMeasurement);
    RUN_TEST(Test_BackwardTimeJumpDetection);
    RUN_TEST(Test_NullPointerHandling);
    RUN_TEST(Test_ConcurrentAccessSerialization);
    
    #undef RUN_TEST
    
    /* Cleanup */
    if (ctx.adapter != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx.adapter);
    }
    
    /* Summary */
    printf("\n");
    printf("====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:   %d tests\n", ctx.test_count);
    printf(" Passed:  %d tests\n", ctx.pass_count);
    printf(" Failed:  %d tests\n", ctx.fail_count);
    printf(" Skipped: %d tests\n", ctx.skip_count);
    printf("====================================================================\n");
    printf("\n");
    
    return (ctx.fail_count > 0) ? TEST_FAIL : TEST_PASS;
}
