/**
 * @file test_ioctl_ptp_freq.c
 * @brief PTP Frequency Adjustment Verification Tests
 * 
 * Implements: #296 (TEST-PTP-FREQ-001)
 * Verifies: #3 (REQ-F-PTP-002: PTP Frequency Adjustment via IOCTL)
 * 
 * IOCTLs: 38 (IOCTL_AVB_ADJUST_FREQUENCY)
 * Test Cases: 15
 * Priority: P0 (Critical)
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * Standards: IEEE 1588-2019 (PTP)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/296
 * @see https://github.com/zarfld/IntelAvbFilter/issues/3
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Single Source of Truth for IOCTL definitions */
#include "../include/avb_ioctl.h"

/* Test result codes */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* Frequency adjustment constants (parts per billion - ppb) */
#define MAX_FREQ_ADJ_PPB  1000000000   /* ±1 second/second = ±1e9 ppb */
#define TYPICAL_FREQ_ADJ_PPB  100000   /* ±100 ppm = ±100000 ppb */
#define NSEC_PER_SEC 1000000000ULL

/* Test state */
typedef struct {
    HANDLE adapter;
    INT64 initial_frequency;
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/* IOCTL request structure */
typedef struct {
    INT64 frequency_ppb;  /* Frequency adjustment in ppb */
    UINT32 status;
} ADJUST_FREQUENCY_REQUEST, *PADJUST_FREQUENCY_REQUEST;

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

/* Helper: Adjust frequency */
static BOOL AdjustFrequency(HANDLE adapter, INT64 ppb)
{
    ADJUST_FREQUENCY_REQUEST req = {0};
    DWORD bytesReturned = 0;
    
    req.frequency_ppb = ppb;
    
    BOOL result = DeviceIoControl(
        adapter,
        IOCTL_AVB_ADJUST_FREQUENCY,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
    return result;
}

/*
 * Test UT-PTP-FREQ-001: Zero Frequency Adjustment
 * Verifies: Can set frequency adjustment to 0 (nominal)
 */
static int Test_ZeroFrequencyAdjustment(TestContext *ctx)
{
    if (!AdjustFrequency(ctx->adapter, 0)) {
        printf("  [FAIL] UT-PTP-FREQ-001: Zero Frequency Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-001: Zero Frequency Adjustment\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-002: Positive Frequency Adjustment
 * Verifies: Can set positive frequency adjustment (+100 ppm)
 */
static int Test_PositiveFrequencyAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = 100000;  /* +100 ppm */
    
    if (!AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-002: Positive Frequency Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-002: Positive Frequency Adjustment (+100 ppm)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-003: Negative Frequency Adjustment
 * Verifies: Can set negative frequency adjustment (-100 ppm)
 */
static int Test_NegativeFrequencyAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = -100000;  /* -100 ppm */
    
    if (!AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-003: Negative Frequency Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-003: Negative Frequency Adjustment (-100 ppm)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-004: Maximum Positive Adjustment
 * Verifies: Can set maximum positive frequency adjustment
 */
static int Test_MaximumPositiveAdjustment(TestContext *ctx)
{
    /* Intel I210/I225 typically supports up to ±999999999 ppb */
    INT64 adj_ppb = 999999999;
    
    if (!AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-004: Maximum Positive Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-004: Maximum Positive Adjustment\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-005: Maximum Negative Adjustment
 * Verifies: Can set maximum negative frequency adjustment
 */
static int Test_MaximumNegativeAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = -999999999;
    
    if (!AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-005: Maximum Negative Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-005: Maximum Negative Adjustment\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-006: Out-of-Range Rejection (Positive)
 * Verifies: Rejects adjustments > +1e9 ppb
 */
static int Test_OutOfRangeRejectionPositive(TestContext *ctx)
{
    INT64 adj_ppb = 1000000001;  /* > +1e9 ppb (invalid) */
    
    if (AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-006: Out-of-Range Rejection (Positive): Invalid value accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-006: Out-of-Range Rejection (Positive)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-007: Out-of-Range Rejection (Negative)
 * Verifies: Rejects adjustments < -1e9 ppb
 */
static int Test_OutOfRangeRejectionNegative(TestContext *ctx)
{
    INT64 adj_ppb = -1000000001;  /* < -1e9 ppb (invalid) */
    
    if (AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-007: Out-of-Range Rejection (Negative): Invalid value accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-007: Out-of-Range Rejection (Negative)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-008: Small Adjustment (+1 ppb)
 * Verifies: Can set very small frequency adjustments
 */
static int Test_SmallAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = 1;  /* +1 ppb */
    
    if (!AdjustFrequency(ctx->adapter, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-008: Small Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-008: Small Adjustment (+1 ppb)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-009: Rapid Frequency Changes
 * Verifies: Can handle rapid frequency adjustment changes
 */
static int Test_RapidFrequencyChanges(TestContext *ctx)
{
    INT64 adjustments[] = {100000, -100000, 50000, -50000, 0};
    int count = sizeof(adjustments) / sizeof(adjustments[0]);
    
    for (int i = 0; i < count; i++) {
        if (!AdjustFrequency(ctx->adapter, adjustments[i])) {
            printf("  [FAIL] UT-PTP-FREQ-009: Rapid Frequency Changes: Adjustment %d failed\n", i);
            return TEST_FAIL;
        }
    }
    
    printf("  [PASS] UT-PTP-FREQ-009: Rapid Frequency Changes\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-010: Frequency Adjustment Persistence
 * Verifies: Frequency adjustment persists until changed
 */
static int Test_FrequencyAdjustmentPersistence(TestContext *ctx)
{
    printf("  [SKIP] UT-PTP-FREQ-010: Frequency Adjustment Persistence: Requires timestamp measurement infrastructure\n");
    return TEST_SKIP;
}

/*
 * Test UT-PTP-FREQ-011: Fractional PPB Precision
 * Verifies: Hardware resolution for fractional ppb values
 */
static int Test_FractionalPPBPrecision(TestContext *ctx)
{
    /* Test sub-ppb precision (hardware-dependent) */
    printf("  [SKIP] UT-PTP-FREQ-011: Fractional PPB Precision: Requires hardware capability query\n");
    return TEST_SKIP;
}

/*
 * Test UT-PTP-FREQ-012: Concurrent Adjustment Requests
 * Verifies: Serializes concurrent frequency adjustment requests
 */
static int Test_ConcurrentAdjustmentRequests(TestContext *ctx)
{
    printf("  [SKIP] UT-PTP-FREQ-012: Concurrent Adjustment Requests: Requires multi-threaded framework\n");
    return TEST_SKIP;
}

/*
 * Test UT-PTP-FREQ-013: Adjustment During Active Sync
 * Verifies: Can adjust frequency while PTP sync is active
 */
static int Test_AdjustmentDuringActiveSync(TestContext *ctx)
{
    printf("  [SKIP] UT-PTP-FREQ-013: Adjustment During Active Sync: Requires PTP daemon integration\n");
    return TEST_SKIP;
}

/*
 * Test UT-PTP-FREQ-014: Null Pointer Handling
 * Verifies: Rejects NULL buffer pointers
 */
static int Test_NullPointerHandling(TestContext *ctx)
{
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_ADJUST_FREQUENCY,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [FAIL] UT-PTP-FREQ-014: Null Pointer Handling: NULL buffer accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-014: Null Pointer Handling\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-015: Adjustment Reset on Driver Restart
 * Verifies: Frequency adjustment resets to 0 after driver restart
 */
static int Test_AdjustmentResetOnRestart(TestContext *ctx)
{
    printf("  [SKIP] UT-PTP-FREQ-015: Adjustment Reset on Restart: Requires driver reload framework\n");
    return TEST_SKIP;
}

/* Main test runner */
int main(void)
{
    TestContext ctx = {0};
    
    printf("\n");
    printf("====================================================================\n");
    printf(" PTP Frequency Adjustment Test Suite\n");
    printf("====================================================================\n");
    printf(" Implements: #296 (TEST-PTP-FREQ-001)\n");
    printf(" Verifies: #3 (REQ-F-PTP-002)\n");
    printf(" IOCTLs: ADJUST_FREQUENCY (38)\n");
    printf(" Total Tests: 15\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n");
    printf("\n");
    
    /* Open adapter */
    ctx.adapter = OpenAdapter();
    if (ctx.adapter == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Failed to open AVB adapter. Skipping all tests.\n");
        return TEST_FAIL;
    }
    
    printf("Running PTP Frequency Adjustment tests...\n\n");
    
    /* Run tests */
    #define RUN_TEST(test) do { \
        int result = test(&ctx); \
        ctx.test_count++; \
        if (result == TEST_PASS) ctx.pass_count++; \
        else if (result == TEST_FAIL) ctx.fail_count++; \
        else if (result == TEST_SKIP) ctx.skip_count++; \
    } while(0)
    
    RUN_TEST(Test_ZeroFrequencyAdjustment);
    RUN_TEST(Test_PositiveFrequencyAdjustment);
    RUN_TEST(Test_NegativeFrequencyAdjustment);
    RUN_TEST(Test_MaximumPositiveAdjustment);
    RUN_TEST(Test_MaximumNegativeAdjustment);
    RUN_TEST(Test_OutOfRangeRejectionPositive);
    RUN_TEST(Test_OutOfRangeRejectionNegative);
    RUN_TEST(Test_SmallAdjustment);
    RUN_TEST(Test_RapidFrequencyChanges);
    RUN_TEST(Test_FrequencyAdjustmentPersistence);
    RUN_TEST(Test_FractionalPPBPrecision);
    RUN_TEST(Test_ConcurrentAdjustmentRequests);
    RUN_TEST(Test_AdjustmentDuringActiveSync);
    RUN_TEST(Test_NullPointerHandling);
    RUN_TEST(Test_AdjustmentResetOnRestart);
    
    #undef RUN_TEST
    
    /* Reset to nominal before cleanup */
    AdjustFrequency(ctx.adapter, 0);
    
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
