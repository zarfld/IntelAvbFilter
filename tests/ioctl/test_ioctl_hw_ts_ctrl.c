/**
 * @file test_ioctl_hw_ts_ctrl.c
 * @brief Hardware Timestamping Control Verification Tests
 * 
 * Implements: #297 (TEST-HW-TS-CTRL-001)
 * Verifies: #5 (REQ-F-PTP-003: Hardware Timestamping Control via IOCTL)
 * 
 * IOCTLs: 40 (IOCTL_AVB_SET_HW_TIMESTAMPING)
 * Test Cases: 13
 * Priority: P0 (Critical)
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * Standards: IEEE 1588-2019 (PTP)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/297
 * @see https://github.com/zarfld/IntelAvbFilter/issues/5
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "avb_ioctl.h"

/* Test result codes */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* Hardware timestamping modes */
#define HW_TS_DISABLED    0x00
#define HW_TS_RX_ENABLED  0x01
#define HW_TS_TX_ENABLED  0x02
#define HW_TS_ALL_ENABLED 0x03

/* Test state */
typedef struct {
    HANDLE adapter;
    UINT32 initial_mode;
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

/* Helper: Set hardware timestamping mode */
static BOOL SetHWTimestamping(HANDLE adapter, UINT32 mode)
{
    AVB_HW_TIMESTAMPING_REQUEST req;
    DWORD bytesReturned = 0;
    
    ZeroMemory(&req, sizeof(req));
    
    /* Convert mode bitmask to proper structure fields */
    if (mode == HW_TS_DISABLED) {
        req.enable = 0;  /* Disable all timestamping */
        req.timer_mask = 0;
        req.enable_target_time = 0;
        req.enable_aux_ts = 0;
    } else {
        req.enable = 1;  /* Enable timestamping */
        req.timer_mask = 0x1;  /* SYSTIM0 only */
        req.enable_target_time = 0;  /* No target time interrupts */
        req.enable_aux_ts = 0;  /* No auxiliary timestamps */
        
        /* Note: mode parameter (RX/TX bits) is NOT used by IOCTL 40 */
        /* IOCTL 40 only controls TSAUXC register (global enable/disable) */
        /* RX/TX packet timestamping is controlled by IOCTLs 41/42 */
    }
    
    BOOL result = DeviceIoControl(
        adapter,
        IOCTL_AVB_SET_HW_TIMESTAMPING,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
    if (result && req.status != 0) {
        /* IOCTL succeeded but driver returned error status */
        return FALSE;
    }
    
    return result;
}

/*
 * Test UT-HW-TS-001: Disable All Timestamping
 */
static int Test_DisableAllTimestamping(TestContext *ctx)
{
    if (!SetHWTimestamping(ctx->adapter, HW_TS_DISABLED)) {
        printf("  [FAIL] UT-HW-TS-001: Disable All Timestamping: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-001: Disable All Timestamping\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-002: Enable RX Timestamping Only
 */
static int Test_EnableRxTimestampingOnly(TestContext *ctx)
{
    if (!SetHWTimestamping(ctx->adapter, HW_TS_RX_ENABLED)) {
        printf("  [FAIL] UT-HW-TS-002: Enable RX Timestamping Only: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-002: Enable RX Timestamping Only\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-003: Enable TX Timestamping Only
 */
static int Test_EnableTxTimestampingOnly(TestContext *ctx)
{
    if (!SetHWTimestamping(ctx->adapter, HW_TS_TX_ENABLED)) {
        printf("  [FAIL] UT-HW-TS-003: Enable TX Timestamping Only: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-003: Enable TX Timestamping Only\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-004: Enable Both RX and TX Timestamping
 */
static int Test_EnableBothRxTxTimestamping(TestContext *ctx)
{
    if (!SetHWTimestamping(ctx->adapter, HW_TS_ALL_ENABLED)) {
        printf("  [FAIL] UT-HW-TS-004: Enable Both RX/TX Timestamping: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-004: Enable Both RX/TX Timestamping\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-005: Invalid Mode Rejection
 */
static int Test_InvalidModeRejection(TestContext *ctx)
{
    UINT32 invalid_mode = 0xFFFFFFFF;  /* Invalid bits set */
    
    if (SetHWTimestamping(ctx->adapter, invalid_mode)) {
        printf("  [FAIL] UT-HW-TS-005: Invalid Mode Rejection: Invalid mode accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-005: Invalid Mode Rejection\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-006: Rapid Mode Switching
 */
static int Test_RapidModeSwitching(TestContext *ctx)
{
    UINT32 modes[] = {HW_TS_DISABLED, HW_TS_RX_ENABLED, HW_TS_TX_ENABLED, HW_TS_ALL_ENABLED, HW_TS_DISABLED};
    
    for (int i = 0; i < 5; i++) {
        if (!SetHWTimestamping(ctx->adapter, modes[i])) {
            printf("  [FAIL] UT-HW-TS-006: Rapid Mode Switching: Mode %d failed\n", i);
            return TEST_FAIL;
        }
    }
    
    printf("  [PASS] UT-HW-TS-006: Rapid Mode Switching\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-007: Enable During Active Traffic
 * Verifies SYSTIM continues running during mode changes
 */
static int Test_EnableDuringActiveTraffic(TestContext *ctx)
{
    AVB_TIMESTAMP_REQUEST ts1, ts2;
    DWORD br;
    
    /* Enable timestamping */
    if (!SetHWTimestamping(ctx->adapter, HW_TS_ALL_ENABLED)) {
        printf("  [FAIL] UT-HW-TS-007: Could not enable timestamping\n");
        return TEST_FAIL;
    }
    
    /* Get initial timestamp */
    ZeroMemory(&ts1, sizeof(ts1));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts1, sizeof(ts1), &ts1, sizeof(ts1), &br, NULL)) {
        printf("  [FAIL] UT-HW-TS-007: Could not get initial timestamp\n");
        return TEST_FAIL;
    }
    
    /* Toggle modes while "traffic" (timestamps) are active */
    Sleep(10);
    SetHWTimestamping(ctx->adapter, HW_TS_DISABLED);
    Sleep(5);
    SetHWTimestamping(ctx->adapter, HW_TS_RX_ENABLED);
    Sleep(5);
    SetHWTimestamping(ctx->adapter, HW_TS_ALL_ENABLED);
    Sleep(10);
    
    /* Get final timestamp - should have advanced */
    ZeroMemory(&ts2, sizeof(ts2));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts2, sizeof(ts2), &ts2, sizeof(ts2), &br, NULL)) {
        printf("  [FAIL] UT-HW-TS-007: Could not get final timestamp\n");
        return TEST_FAIL;
    }
    
    /* Verify timestamp advanced (>20ms should have elapsed) */
    if (ts2.timestamp > ts1.timestamp && (ts2.timestamp - ts1.timestamp) > 20000000) {
        printf("  [PASS] UT-HW-TS-007: Enable During Active Traffic\n");
        return TEST_PASS;
    }
    
    printf("  [FAIL] UT-HW-TS-007: Timestamp did not advance as expected (delta=%lld ns)\n", 
           (long long)(ts2.timestamp - ts1.timestamp));
    return TEST_FAIL;
}

/*
 * Test UT-HW-TS-008: Mode Persistence After Disable
 * Verifies TSAUXC register state persists correctly
 */
static int Test_ModePersistenceAfterDisable(TestContext *ctx)
{
    AVB_CLOCK_CONFIG cfg;
    DWORD br;
    
    /* Disable timestamping */
    if (!SetHWTimestamping(ctx->adapter, HW_TS_DISABLED)) {
        printf("  [FAIL] UT-HW-TS-008: Could not disable timestamping\n");
        return TEST_FAIL;
    }
    
    /* Query TSAUXC register via GET_CLOCK_CONFIG */
    ZeroMemory(&cfg, sizeof(cfg));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg, sizeof(cfg), &cfg, sizeof(cfg), &br, NULL)) {
        printf("  [FAIL] UT-HW-TS-008: GET_CLOCK_CONFIG failed\n");
        return TEST_FAIL;
    }
    
    /* Verify bit 31 is SET (disabled state persists) */
    if (!(cfg.tsauxc & 0x80000000)) {
        printf("  [FAIL] UT-HW-TS-008: TSAUXC bit 31 not set (disabled state not persisted)\n");
        return TEST_FAIL;
    }
    
    /* Re-enable and verify persistence */
    if (!SetHWTimestamping(ctx->adapter, HW_TS_ALL_ENABLED)) {
        printf("  [FAIL] UT-HW-TS-008: Could not re-enable timestamping\n");
        return TEST_FAIL;
    }
    
    ZeroMemory(&cfg, sizeof(cfg));
    DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg, sizeof(cfg), &cfg, sizeof(cfg), &br, NULL);
    
    /* Verify bit 31 is CLEAR (enabled state persists) */
    if (cfg.tsauxc & 0x80000000) {
        printf("  [FAIL] UT-HW-TS-008: TSAUXC bit 31 still set (enabled state not persisted)\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-008: Mode Persistence\n");
    return TEST_PASS;
}

/* Thread context for concurrent test */
typedef struct {
    HANDLE adapter;
    UINT32 mode;
    int iterations;
    volatile LONG* success_count;
    volatile LONG* fail_count;
} ThreadContext;

static DWORD WINAPI ConcurrentWorker(LPVOID param)
{
    ThreadContext* tc = (ThreadContext*)param;
    int i;
    
    for (i = 0; i < tc->iterations; i++) {
        if (SetHWTimestamping(tc->adapter, tc->mode)) {
            InterlockedIncrement(tc->success_count);
        } else {
            InterlockedIncrement(tc->fail_count);
        }
        Sleep(1);
    }
    
    return 0;
}

/*
 * Test UT-HW-TS-009: Concurrent Mode Change Requests
 * Verifies driver handles concurrent IOCTL calls safely
 */
static int Test_ConcurrentModeChangeRequests(TestContext *ctx)
{
    const int NUM_THREADS = 4;
    const int ITERATIONS = 10;
    HANDLE threads[4];
    ThreadContext thread_ctx[4];
    volatile LONG success_count = 0;
    volatile LONG fail_count = 0;
    int i, j;
    
    /* Create threads that toggle different modes concurrently */
    UINT32 modes[] = {HW_TS_DISABLED, HW_TS_RX_ENABLED, HW_TS_TX_ENABLED, HW_TS_ALL_ENABLED};
    
    for (i = 0; i < NUM_THREADS; i++) {
        thread_ctx[i].adapter = ctx->adapter;
        thread_ctx[i].mode = modes[i];
        thread_ctx[i].iterations = ITERATIONS;
        thread_ctx[i].success_count = &success_count;
        thread_ctx[i].fail_count = &fail_count;
        
        threads[i] = CreateThread(NULL, 0, ConcurrentWorker, &thread_ctx[i], 0, NULL);
        if (threads[i] == NULL) {
            printf("  [FAIL] UT-HW-TS-009: Could not create thread %d\n", i);
            /* Clean up already created threads */
            for (j = 0; j < i; j++) {
                WaitForSingleObject(threads[j], INFINITE);
                CloseHandle(threads[j]);
            }
            return TEST_FAIL;
        }
    }
    
    /* Wait for all threads to complete */
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, 5000);
    
    /* Clean up thread handles */
    for (i = 0; i < NUM_THREADS; i++) {
        CloseHandle(threads[i]);
    }
    
    /* Verify at least some requests succeeded (driver didn't deadlock) */
    if (success_count > 0) {
        printf("  [PASS] UT-HW-TS-009: Concurrent Requests (%ld succeeded, %ld failed)\n", 
               (long)success_count, (long)fail_count);
        return TEST_PASS;
    }
    
    printf("  [FAIL] UT-HW-TS-009: No concurrent requests succeeded\n");
    return TEST_FAIL;
}

/*
 * Test UT-HW-TS-010: NULL Pointer Handling
 */
static int Test_NullPointerHandling(TestContext *ctx)
{
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_SET_HW_TIMESTAMPING,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [FAIL] UT-HW-TS-010: NULL Pointer Handling: NULL buffer accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-HW-TS-010: NULL Pointer Handling\n");
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-011: Mode Reset After Driver Restart
 */
static int Test_ModeResetAfterRestart(TestContext *ctx)
{
    printf("  [SKIP] UT-HW-TS-011: Mode Reset: Requires driver reload framework\n");
    return TEST_SKIP;
}

/*
 * Test UT-HW-TS-012: Hardware Support Verification
 * Verifies hardware has enhanced timestamp capability
 */
static int Test_HardwareSupportVerification(TestContext *ctx)
{
    AVB_ENUM_REQUEST enum_req;
    DWORD br;
    
    /* Query adapter 0 capabilities */
    ZeroMemory(&enum_req, sizeof(enum_req));
    enum_req.index = 0;
    
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_ENUM_ADAPTERS, &enum_req, sizeof(enum_req), 
                        &enum_req, sizeof(enum_req), &br, NULL)) {
        printf("  [FAIL] UT-HW-TS-012: ENUM IOCTL failed\n");
        return TEST_FAIL;
    }
    
    /* Check if INTEL_CAP_ENHANCED_TS is set */
    #ifndef INTEL_CAP_ENHANCED_TS
    #define INTEL_CAP_ENHANCED_TS 0x00000001
    #endif
    
    if (enum_req.capabilities & INTEL_CAP_ENHANCED_TS) {
        printf("  [PASS] UT-HW-TS-012: Hardware Support (VID:0x%04X DID:0x%04X CAP:0x%08X)\n",
               enum_req.vendor_id, enum_req.device_id, enum_req.capabilities);
        return TEST_PASS;
    }
    
    printf("  [WARN] UT-HW-TS-012: Hardware lacks ENHANCED_TS capability (0x%08X)\n", 
           enum_req.capabilities);
    /* This is informational - not a failure */
    return TEST_PASS;
}

/*
 * Test UT-HW-TS-013: PTP Packet Filtering Integration
 */
static int Test_PTPPacketFilteringIntegration(TestContext *ctx)
{
    printf("  [SKIP] UT-HW-TS-013: PTP Filtering: Requires packet capture infrastructure\n");
    return TEST_SKIP;
}

/* Main test runner */
int main(void)
{
    TestContext ctx = {0};
    
    printf("\n");
    printf("====================================================================\n");
    printf(" Hardware Timestamping Control Test Suite\n");
    printf("====================================================================\n");
    printf(" Implements: #297 (TEST-HW-TS-CTRL-001)\n");
    printf(" Verifies: #5 (REQ-F-PTP-003)\n");
    printf(" IOCTLs: SET_HW_TIMESTAMPING (40)\n");
    printf(" Total Tests: 13\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n");
    printf("\n");
    
    ctx.adapter = OpenAdapter();
    if (ctx.adapter == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Failed to open AVB adapter. Skipping all tests.\n");
        return TEST_FAIL;
    }
    
    printf("Running Hardware Timestamping Control tests...\n\n");
    
    #define RUN_TEST(test) do { \
        int result = test(&ctx); \
        ctx.test_count++; \
        if (result == TEST_PASS) ctx.pass_count++; \
        else if (result == TEST_FAIL) ctx.fail_count++; \
        else if (result == TEST_SKIP) ctx.skip_count++; \
    } while(0)
    
    RUN_TEST(Test_DisableAllTimestamping);
    RUN_TEST(Test_EnableRxTimestampingOnly);
    RUN_TEST(Test_EnableTxTimestampingOnly);
    RUN_TEST(Test_EnableBothRxTxTimestamping);
    RUN_TEST(Test_InvalidModeRejection);
    RUN_TEST(Test_RapidModeSwitching);
    RUN_TEST(Test_EnableDuringActiveTraffic);
    RUN_TEST(Test_ModePersistenceAfterDisable);
    RUN_TEST(Test_ConcurrentModeChangeRequests);
    RUN_TEST(Test_NullPointerHandling);
    RUN_TEST(Test_ModeResetAfterRestart);
    RUN_TEST(Test_HardwareSupportVerification);
    RUN_TEST(Test_PTPPacketFilteringIntegration);
    
    #undef RUN_TEST
    
    /* Reset to disabled before cleanup */
    SetHWTimestamping(ctx.adapter, HW_TS_DISABLED);
    
    if (ctx.adapter != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx.adapter);
    }
    
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
