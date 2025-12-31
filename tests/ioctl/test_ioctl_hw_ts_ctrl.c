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
#include "../include/avb_ioctl.h"

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

/* IOCTL request structure */
typedef struct {
    UINT32 mode;      /* Bitmask: RX|TX enable */
    UINT32 status;
} HW_TIMESTAMPING_REQUEST, *PHW_TIMESTAMPING_REQUEST;

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
    HW_TIMESTAMPING_REQUEST req = {0};
    DWORD bytesReturned = 0;
    
    req.mode = mode;
    
    BOOL result = DeviceIoControl(
        adapter,
        IOCTL_AVB_SET_HW_TIMESTAMPING,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
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
 */
static int Test_EnableDuringActiveTraffic(TestContext *ctx)
{
    printf("  [SKIP] UT-HW-TS-007: Enable During Active Traffic: Requires traffic generator\n");
    return TEST_SKIP;
}

/*
 * Test UT-HW-TS-008: Mode Persistence After Disable
 */
static int Test_ModePersistenceAfterDisable(TestContext *ctx)
{
    printf("  [SKIP] UT-HW-TS-008: Mode Persistence: Requires state verification mechanism\n");
    return TEST_SKIP;
}

/*
 * Test UT-HW-TS-009: Concurrent Mode Change Requests
 */
static int Test_ConcurrentModeChangeRequests(TestContext *ctx)
{
    printf("  [SKIP] UT-HW-TS-009: Concurrent Requests: Requires multi-threaded framework\n");
    return TEST_SKIP;
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
 */
static int Test_HardwareSupportVerification(TestContext *ctx)
{
    printf("  [SKIP] UT-HW-TS-012: Hardware Support: Requires capability query IOCTL\n");
    return TEST_SKIP;
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
