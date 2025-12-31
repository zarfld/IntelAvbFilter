/**
 * @file test_ioctl_mdio_phy.c
 * @brief MDIO/PHY Register Access Verification Tests
 * 
 * Implements: #312 (TEST-MDIO-PHY-001)
 * Verifies: #10 (REQ-F-MDIO-001: MDIO/PHY Register Access via IOCTL)
 * 
 * Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md
 * IOCTLs: 29 (IOCTL_AVB_MDIO_READ), 30 (IOCTL_AVB_MDIO_WRITE)
 * Test Cases: 15
 * Priority: P1
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/312
 * @see https://github.com/zarfld/IntelAvbFilter/issues/10
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

/* PHY Standard Registers (IEEE 802.3 Clause 22) */
#define PHY_REG_CONTROL        0   /* Basic Control Register */
#define PHY_REG_STATUS         1   /* Basic Status Register */
#define PHY_REG_PHY_ID1        2   /* PHY Identifier 1 */
#define PHY_REG_PHY_ID2        3   /* PHY Identifier 2 */
#define PHY_REG_AUTONEG_ADV    4   /* Auto-Negotiation Advertisement */
#define PHY_REG_LINK_PARTNER   5   /* Link Partner Ability */

/* Test configuration */
#define TEST_PHY_ADDR          0   /* PHY address (typically 0-31) */
#define MDIO_TIMEOUT_MS        10  /* MDIO bus timeout */

/* Test state */
typedef struct {
    HANDLE adapter;
    UINT16 saved_control;
    UINT16 saved_page;
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/* MDIO request/response structures */
typedef struct {
    UINT8  phy_addr;
    UINT8  reg_addr;
    UINT16 value;
    UINT32 status;
} MDIO_REQUEST, *PMDIO_REQUEST;

/*==============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Open first available AVB adapter
 */
HANDLE OpenAdapter(void) {
    HANDLE h;
    
    /* Try symbolic link (typical driver interface) */
    h = CreateFileA("\\\\.\\IntelAvbFilter",
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        printf("  [WARN] Could not open device: error %lu\n", GetLastError());
    }
    
    return h;
}

/**
 * @brief Read PHY register via IOCTL 29
 */
BOOL ReadPHYReg(HANDLE adapter, UINT8 phy_addr, UINT8 reg_addr, UINT16 *value) {
    MDIO_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    request.phy_addr = phy_addr;
    request.reg_addr = reg_addr;
    
    result = DeviceIoControl(
        adapter,
        IOCTL_AVB_MDIO_READ,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    if (result && request.status == 0) {
        *value = request.value;
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief Write PHY register via IOCTL 30
 */
BOOL WritePHYReg(HANDLE adapter, UINT8 phy_addr, UINT8 reg_addr, UINT16 value) {
    MDIO_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    request.phy_addr = phy_addr;
    request.reg_addr = reg_addr;
    request.value = value;
    
    result = DeviceIoControl(
        adapter,
        IOCTL_AVB_MDIO_WRITE,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    return (result && request.status == 0);
}

/**
 * @brief Save PHY state for rollback
 */
void SavePHYState(TestContext *ctx) {
    ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, &ctx->saved_control);
    /* Note: Page select register is vendor-specific */
}

/**
 * @brief Restore PHY state after tests
 */
void RestorePHYState(TestContext *ctx) {
    WritePHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, ctx->saved_control);
}

/**
 * @brief Print test result
 */
void PrintTestResult(TestContext *ctx, const char *test_name, int result, const char *reason) {
    ctx->test_count++;
    
    switch (result) {
        case TEST_PASS:
            printf("  [PASS] %s\n", test_name);
            ctx->pass_count++;
            break;
        case TEST_FAIL:
            printf("  [FAIL] %s: %s\n", test_name, reason);
            ctx->fail_count++;
            break;
        case TEST_SKIP:
            printf("  [SKIP] %s: %s\n", test_name, reason);
            ctx->skip_count++;
            break;
    }
}

/*==============================================================================
 * Test Cases (Issue #312 - 15 test cases)
 *============================================================================*/

/**
 * UT-MDIO-001: Basic MDIO Read Operation
 */
void Test_BasicMDIORead(TestContext *ctx) {
    UINT16 value = 0;
    BOOL result;
    
    result = ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, &value);
    
    PrintTestResult(ctx, "UT-MDIO-001: Basic MDIO Read", 
                    result ? TEST_PASS : TEST_FAIL,
                    result ? NULL : "IOCTL failed or status error");
}

/**
 * UT-MDIO-002: Basic MDIO Write Operation
 */
void Test_BasicMDIOWrite(TestContext *ctx) {
    UINT16 original, written = 0x1000, readback = 0;
    BOOL write_ok, read_ok;
    
    /* Save original value */
    if (!ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, &original)) {
        PrintTestResult(ctx, "UT-MDIO-002: Basic MDIO Write", TEST_FAIL, "Failed to read original");
        return;
    }
    
    /* Write test pattern */
    write_ok = WritePHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, written);
    
    /* Readback */
    read_ok = ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, &readback);
    
    /* Restore */
    WritePHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, original);
    
    if (write_ok && read_ok && (readback == written)) {
        PrintTestResult(ctx, "UT-MDIO-002: Basic MDIO Write", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-MDIO-002: Basic MDIO Write", TEST_FAIL, 
                        "Write or readback mismatch");
    }
}

/**
 * UT-MDIO-003: Multi-Page PHY Access
 */
void Test_MultiPagePHYAccess(TestContext *ctx) {
    /* Multi-page access is vendor-specific (Marvell, Broadcom, etc.) */
    PrintTestResult(ctx, "UT-MDIO-003: Multi-Page PHY Access", TEST_SKIP, 
                    "Vendor-specific, requires PHY identification");
}

/**
 * UT-MDIO-004: Invalid PHY Address Rejection
 */
void Test_InvalidPHYAddressRejection(TestContext *ctx) {
    UINT16 value = 0;
    BOOL result;
    
    /* PHY addresses are 0-31, try out-of-range */
    result = ReadPHYReg(ctx->adapter, 32, PHY_REG_CONTROL, &value);
    
    /* Should fail */
    PrintTestResult(ctx, "UT-MDIO-004: Invalid PHY Address Rejection", 
                    !result ? TEST_PASS : TEST_FAIL,
                    !result ? NULL : "Invalid PHY address accepted");
}

/**
 * UT-MDIO-005: Out-of-Range Register Rejection
 */
void Test_OutOfRangeRegisterRejection(TestContext *ctx) {
    UINT16 value = 0;
    BOOL result;
    
    /* Clause 22 registers are 0-31, try out-of-range */
    result = ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, 32, &value);
    
    /* Should fail */
    PrintTestResult(ctx, "UT-MDIO-005: Out-of-Range Register Rejection", 
                    !result ? TEST_PASS : TEST_FAIL,
                    !result ? NULL : "Invalid register address accepted");
}

/**
 * UT-MDIO-006: Read-Only Register Write Protection
 */
void Test_ReadOnlyRegisterWriteProtection(TestContext *ctx) {
    UINT16 original, written = 0xFFFF, readback;
    
    /* PHY Status Register (reg 1) is read-only */
    ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_STATUS, &original);
    
    /* Attempt write */
    WritePHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_STATUS, written);
    
    /* Readback should match original (write ignored) */
    ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_STATUS, &readback);
    
    PrintTestResult(ctx, "UT-MDIO-006: Read-Only Register Write Protection", 
                    (readback == original) ? TEST_PASS : TEST_FAIL,
                    (readback == original) ? NULL : "Read-only register was modified");
}

/**
 * UT-MDIO-007: MDIO Bus Timeout Handling
 */
void Test_MDIOBusTimeoutHandling(TestContext *ctx) {
    PrintTestResult(ctx, "UT-MDIO-007: MDIO Bus Timeout Handling", TEST_SKIP, 
                    "Requires PHY simulation or hardware fault injection");
}

/**
 * UT-MDIO-008: Concurrent MDIO Access Serialization
 */
void Test_ConcurrentMDIOAccessSerialization(TestContext *ctx) {
    PrintTestResult(ctx, "UT-MDIO-008: Concurrent MDIO Access Serialization", TEST_SKIP, 
                    "Requires multi-threaded test framework");
}

/**
 * UT-MDIO-009: Extended Register Access (Clause 45)
 */
void Test_ExtendedRegisterAccessClause45(TestContext *ctx) {
    PrintTestResult(ctx, "UT-MDIO-009: Extended Register Access (Clause 45)", TEST_SKIP, 
                    "Requires Clause 45 PHY (10G Ethernet)");
}

/**
 * UT-MDIO-010: PHY Reset via MDIO
 */
void Test_PHYResetViaMDIO(TestContext *ctx) {
    UINT16 control;
    BOOL result;
    
    /* Set reset bit (bit 15 of Control Register) */
    result = WritePHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, 0x8000);
    
    if (!result) {
        PrintTestResult(ctx, "UT-MDIO-010: PHY Reset via MDIO", TEST_FAIL, "Reset write failed");
        return;
    }
    
    /* Wait for reset to complete (self-clearing bit) */
    Sleep(500);
    
    /* Verify reset bit cleared */
    ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_CONTROL, &control);
    
    PrintTestResult(ctx, "UT-MDIO-010: PHY Reset via MDIO", 
                    !(control & 0x8000) ? TEST_PASS : TEST_FAIL,
                    !(control & 0x8000) ? NULL : "Reset bit did not clear");
}

/**
 * UT-MDIO-011: Auto-Negotiation Status Read
 */
void Test_AutoNegotiationStatusRead(TestContext *ctx) {
    UINT16 status;
    BOOL result;
    
    result = ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_STATUS, &status);
    
    if (!result) {
        PrintTestResult(ctx, "UT-MDIO-011: Auto-Negotiation Status Read", TEST_FAIL, 
                        "Failed to read status register");
        return;
    }
    
    /* Check auto-neg complete bit (bit 5) */
    printf("    Auto-neg complete: %s\n", (status & 0x0020) ? "Yes" : "No");
    
    PrintTestResult(ctx, "UT-MDIO-011: Auto-Negotiation Status Read", TEST_PASS, NULL);
}

/**
 * UT-MDIO-012: Link Partner Ability Read
 */
void Test_LinkPartnerAbilityRead(TestContext *ctx) {
    UINT16 ability;
    BOOL result;
    
    result = ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, PHY_REG_LINK_PARTNER, &ability);
    
    if (!result) {
        PrintTestResult(ctx, "UT-MDIO-012: Link Partner Ability Read", TEST_FAIL, 
                        "Failed to read link partner register");
        return;
    }
    
    printf("    Link partner capabilities: 0x%04X\n", ability);
    
    PrintTestResult(ctx, "UT-MDIO-012: Link Partner Ability Read", TEST_PASS, NULL);
}

/**
 * UT-MDIO-013: Cable Diagnostics via MDIO
 */
void Test_CableDiagnosticsViaMDIO(TestContext *ctx) {
    PrintTestResult(ctx, "UT-MDIO-013: Cable Diagnostics via MDIO", TEST_SKIP, 
                    "Vendor-specific registers (Marvell VCT, Broadcom TDR)");
}

/**
 * UT-MDIO-014: MDIO Access During Low Power
 */
void Test_MDIOAccessDuringLowPower(TestContext *ctx) {
    PrintTestResult(ctx, "UT-MDIO-014: MDIO Access During Low Power", TEST_SKIP, 
                    "Requires power management test framework");
}

/**
 * UT-MDIO-015: Bulk Register Read Optimization
 */
void Test_BulkRegisterReadOptimization(TestContext *ctx) {
    UINT16 regs[8];
    int i;
    LARGE_INTEGER start, end, freq;
    double elapsed_ms;
    
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    /* Read first 8 PHY registers */
    for (i = 0; i < 8; i++) {
        if (!ReadPHYReg(ctx->adapter, TEST_PHY_ADDR, (UINT8)i, &regs[i])) {
            PrintTestResult(ctx, "UT-MDIO-015: Bulk Register Read Optimization", TEST_FAIL, 
                            "Failed to read register");
            return;
        }
    }
    
    QueryPerformanceCounter(&end);
    elapsed_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
    
    printf("    Bulk read latency: %.2f ms for 8 registers\n", elapsed_ms);
    
    /* MDIO @ 2.5MHz: ~3.2µs per read, 8 reads ~25µs + driver overhead */
    PrintTestResult(ctx, "UT-MDIO-015: Bulk Register Read Optimization", 
                    (elapsed_ms < 100.0) ? TEST_PASS : TEST_FAIL,
                    (elapsed_ms < 100.0) ? NULL : "Excessive latency (>100ms)");
}

/*==============================================================================
 * Main Test Harness
 *============================================================================*/

int main(int argc, char **argv) {
    TestContext ctx = {0};
    
    printf("\n");
    printf("====================================================================\n");
    printf(" MDIO/PHY Register Access Test Suite\n");
    printf("====================================================================\n");
    printf(" Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md\n");
    printf(" Issue: #312 (TEST-MDIO-PHY-001)\n");
    printf(" Requirement: #10 (REQ-F-MDIO-001)\n");
    printf(" IOCTLs: IOCTL_AVB_MDIO_READ (29), IOCTL_AVB_MDIO_WRITE (30)\n");
    printf(" Total Tests: 15\n");
    printf(" Priority: P1\n");
    printf("====================================================================\n");
    printf("\n");
    
    /* Open adapter */
    ctx.adapter = OpenAdapter();
    if (ctx.adapter == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Failed to open AVB adapter. Skipping all tests.\n\n");
        return 1;
    }
    
    /* Save PHY state */
    SavePHYState(&ctx);
    
    /* Run test cases */
    printf("Running MDIO/PHY tests...\n\n");
    
    Test_BasicMDIORead(&ctx);
    Test_BasicMDIOWrite(&ctx);
    Test_MultiPagePHYAccess(&ctx);
    Test_InvalidPHYAddressRejection(&ctx);
    Test_OutOfRangeRegisterRejection(&ctx);
    Test_ReadOnlyRegisterWriteProtection(&ctx);
    Test_MDIOBusTimeoutHandling(&ctx);
    Test_ConcurrentMDIOAccessSerialization(&ctx);
    Test_ExtendedRegisterAccessClause45(&ctx);
    Test_PHYResetViaMDIO(&ctx);
    Test_AutoNegotiationStatusRead(&ctx);
    Test_LinkPartnerAbilityRead(&ctx);
    Test_CableDiagnosticsViaMDIO(&ctx);
    Test_MDIOAccessDuringLowPower(&ctx);
    Test_BulkRegisterReadOptimization(&ctx);
    
    /* Restore PHY state */
    RestorePHYState(&ctx);
    
    /* Close adapter */
    CloseHandle(ctx.adapter);
    
    /* Print summary */
    printf("\n");
    printf("====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:  %d tests\n", ctx.test_count);
    printf(" Passed: %d tests\n", ctx.pass_count);
    printf(" Failed: %d tests\n", ctx.fail_count);
    printf(" Skipped: %d tests\n", ctx.skip_count);
    printf("====================================================================\n");
    printf("\n");
    
    /* Exit with pass rate */
    if (ctx.fail_count > 0) {
        return 1;  /* Failures detected */
    } else if (ctx.pass_count == 0) {
        return 2;  /* All tests skipped */
    } else {
        return 0;  /* Pass */
    }
}
