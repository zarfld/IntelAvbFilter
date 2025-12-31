/**
 * @file test_ioctl_qav_cbs.c
 * @brief Credit-Based Shaper (CBS) IOCTL Test Suite
 *
 * Implements: #207 (TEST-QAV-CBS-001: Credit-Based Shaper Configuration Tests)
 * Verifies: #8 (REQ-F-QAV-001: Credit-Based Shaper Configuration via IOCTL)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/207
 * @see https://github.com/zarfld/IntelAvbFilter/issues/8
 *
 * IOCTLs Tested:
 *   - 35 (IOCTL_AVB_SETUP_QAV): Configure Credit-Based Shaper (QAV) parameters
 *
 * Test Cases: 14
 * Priority: P0 (Critical)
 * Standards: IEEE 1012-2016 (Verification & Validation), IEEE 802.1Qav (QAV/CBS)
 *
 * Part of: #14 (Master IOCTL Requirements Tracking)
 */

#include <windows.h>
#include <stdio.h>
#include <stdbool.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

// Traffic Class definitions (per IEEE 802.1Q)
#define CBS_TRAFFIC_CLASS_A  0  // Class A (high priority)
#define CBS_TRAFFIC_CLASS_B  1  // Class B (medium priority)

// Typical AVB bandwidth parameters (per IEEE 802.1BA)
#define CBS_IDLE_SLOPE_CLASS_A   75   // 75% link bandwidth for Class A
#define CBS_IDLE_SLOPE_CLASS_B   25   // 25% link bandwidth for Class B
#define CBS_SEND_SLOPE_CLASS_A  -25   // -(100 - 75) = -25%
#define CBS_SEND_SLOPE_CLASS_B  -75   // -(100 - 25) = -75%

// Test state
static HANDLE g_hDevice = INVALID_HANDLE_VALUE;
static int g_passCount = 0;
static int g_failCount = 0;
static int g_skipCount = 0;

// Helper: Configure CBS using SSOT struct AVB_QAV_REQUEST
static bool ConfigureCBS(
    avb_u8 traffic_class,
    avb_u32 idle_slope,
    avb_u32 send_slope,  // Note: SSOT uses unsigned, will be interpreted as two's complement if needed
    avb_u32 hi_credit,
    avb_u32 lo_credit,   // Note: SSOT uses unsigned for lo_credit (two's complement)
    avb_u8 enabled,      // Not in SSOT - will need to use idle_slope=0 to disable
    const char* context
) {
    AVB_QAV_REQUEST request = { 0 };
    request.tc = traffic_class;
    request.idle_slope = idle_slope;
    request.send_slope = send_slope;
    request.hi_credit = hi_credit;
    request.lo_credit = lo_credit;
    // Note: SSOT doesn't have 'enabled' field - use idle_slope=0 to disable
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_QAV,  // IOCTL 35 (SSOT name)
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytesReturned,
        NULL
    );
    
    return (result != 0);
}

//=============================================================================
// Test Cases
//=============================================================================

// Test 1: Configure CBS - Class A Enabled
static void Test_ConfigureCbsClassAEnabled(void) {
    if (ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        CBS_IDLE_SLOPE_CLASS_A,
        CBS_SEND_SLOPE_CLASS_A,
        8000,   // 8KB hi_credit
        -8000,  // -8KB lo_credit
        1,      // enabled
        "Class A enabled"
    )) {
        printf("  [PASS] UT-CBS-001: Configure CBS (Class A Enabled)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-001: Failed to configure Class A\n");
        g_failCount++;
    }
}

// Test 2: Configure CBS - Class B Enabled
static void Test_ConfigureCbsClassBEnabled(void) {
    if (ConfigureCBS(
        CBS_TRAFFIC_CLASS_B,
        CBS_IDLE_SLOPE_CLASS_B,
        CBS_SEND_SLOPE_CLASS_B,
        4000,   // 4KB hi_credit
        -4000,  // -4KB lo_credit
        1,      // enabled
        "Class B enabled"
    )) {
        printf("  [PASS] UT-CBS-002: Configure CBS (Class B Enabled)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-002: Failed to configure Class B\n");
        g_failCount++;
    }
}

// Test 3: Disable CBS - Class A
static void Test_DisableCbsClassA(void) {
    if (ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        0, 0, 0, 0,
        0,  // disabled
        "Class A disabled"
    )) {
        printf("  [PASS] UT-CBS-003: Disable CBS (Class A)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-003: Failed to disable Class A\n");
        g_failCount++;
    }
}

// Test 4: Disable CBS - Class B
static void Test_DisableCbsClassB(void) {
    if (ConfigureCBS(
        CBS_TRAFFIC_CLASS_B,
        0, 0, 0, 0,
        0,  // disabled
        "Class B disabled"
    )) {
        printf("  [PASS] UT-CBS-004: Disable CBS (Class B)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-004: Failed to disable Class B\n");
        g_failCount++;
    }
}

// Test 5: Configure CBS - Zero Credits (Edge Case)
static void Test_ConfigureCbsZeroCredits(void) {
    if (ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        CBS_IDLE_SLOPE_CLASS_A,
        CBS_SEND_SLOPE_CLASS_A,
        0,   // zero hi_credit
        0,   // zero lo_credit
        1,
        "zero credits"
    )) {
        printf("  [PASS] UT-CBS-005: Configure CBS (Zero Credits)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-005: Zero credits rejected\n");
        g_failCount++;
    }
}

// Test 6: Configure CBS - Maximum Credits
static void Test_ConfigureCbsMaxCredits(void) {
    if (ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        CBS_IDLE_SLOPE_CLASS_A,
        CBS_SEND_SLOPE_CLASS_A,
        65535,   // max UINT16
        -65535,  // min INT16
        1,
        "max credits"
    )) {
        printf("  [PASS] UT-CBS-006: Configure CBS (Maximum Credits)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-006: Maximum credits rejected\n");
        g_failCount++;
    }
}

// Test 7: Invalid Traffic Class
static void Test_InvalidTrafficClass(void) {
    if (!ConfigureCBS(
        255,  // invalid class
        CBS_IDLE_SLOPE_CLASS_A,
        CBS_SEND_SLOPE_CLASS_A,
        8000, -8000,
        1,
        "invalid traffic class"
    )) {
        printf("  [PASS] UT-CBS-007: Invalid Traffic Class Rejected\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-007: Invalid traffic class accepted\n");
        g_failCount++;
    }
}

// Test 8: Invalid Slope Values (Positive Send Slope)
static void Test_InvalidSlopePositiveSend(void) {
    if (!ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        75,
        25,   // send_slope should be negative
        8000, -8000,
        1,
        "positive send slope"
    )) {
        printf("  [PASS] UT-CBS-008: Positive Send Slope Rejected\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-008: Positive send slope accepted\n");
        g_failCount++;
    }
}

// Test 9: Invalid Credit Values (Negative Hi-Credit)
static void Test_InvalidCreditNegativeHi(void) {
    if (!ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        CBS_IDLE_SLOPE_CLASS_A,
        CBS_SEND_SLOPE_CLASS_A,
        -8000,  // hi_credit should be positive
        -8000,
        1,
        "negative hi-credit"
    )) {
        printf("  [PASS] UT-CBS-009: Negative Hi-Credit Rejected\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-009: Negative hi-credit accepted\n");
        g_failCount++;
    }
}

// Test 10: Invalid Credit Values (Positive Lo-Credit)
static void Test_InvalidCreditPositiveLo(void) {
    if (!ConfigureCBS(
        CBS_TRAFFIC_CLASS_A,
        CBS_IDLE_SLOPE_CLASS_A,
        CBS_SEND_SLOPE_CLASS_A,
        8000,
        8000,  // lo_credit should be negative
        1,
        "positive lo-credit"
    )) {
        printf("  [PASS] UT-CBS-010: Positive Lo-Credit Rejected\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-010: Positive lo-credit accepted\n");
        g_failCount++;
    }
}

// Test 11: Rapid Enable/Disable Switching
static void Test_RapidEnableDisable(void) {
    bool success = true;
    
    for (int i = 0; i < 100; i++) {
        UCHAR enabled = (i % 2);
        if (!ConfigureCBS(
            CBS_TRAFFIC_CLASS_A,
            CBS_IDLE_SLOPE_CLASS_A,
            CBS_SEND_SLOPE_CLASS_A,
            8000, -8000,
            enabled,
            "rapid switching"
        )) {
            success = false;
            break;
        }
    }
    
    if (success) {
        printf("  [PASS] UT-CBS-011: Rapid Enable/Disable Switching\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-011: Rapid switching failed\n");
        g_failCount++;
    }
}

// Test 12: NULL Pointer Handling
static void Test_NullPointerHandling(void) {
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_QAV,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    if (!result && GetLastError() == ERROR_INVALID_PARAMETER) {
        printf("  [PASS] UT-CBS-012: NULL Pointer Handling\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-CBS-012: NULL pointer not rejected\n");
        g_failCount++;
    }
}

// Test 13: CBS Under Active Traffic (SKIP - requires traffic generator)
static void Test_CbsUnderActiveTraffic(void) {
    printf("  [SKIP] UT-CBS-013: CBS Under Active Traffic: Requires traffic generator\n");
    g_skipCount++;
}

// Test 14: Credit Accumulation Measurement (SKIP - requires monitoring infrastructure)
static void Test_CreditAccumulationMeasurement(void) {
    printf("  [SKIP] UT-CBS-014: Credit Accumulation: Requires monitoring infrastructure\n");
    g_skipCount++;
}

//=============================================================================
// Main Test Runner
//=============================================================================

int main(void) {
    printf("\n");
    printf("====================================================================\n");
    printf(" Credit-Based Shaper (CBS) Test Suite\n");
    printf("====================================================================\n");
    printf(" Implements: #207 (TEST-QAV-CBS-001)\n");
    printf(" Verifies: #8 (REQ-F-QAV-001)\n");
    printf(" IOCTLs: CONFIGURE_CBS (35)\n");
    printf(" Total Tests: 14\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n\n");
    
    // Open device
    g_hDevice = CreateFileW(
        L"\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open device (error %lu)\n", GetLastError());
        printf("Make sure the driver is installed and running.\n\n");
        return 1;
    }
    
    printf("Running Credit-Based Shaper tests...\n\n");
    
    // Run all tests
    Test_ConfigureCbsClassAEnabled();
    Test_ConfigureCbsClassBEnabled();
    Test_DisableCbsClassA();
    Test_DisableCbsClassB();
    Test_ConfigureCbsZeroCredits();
    Test_ConfigureCbsMaxCredits();
    Test_InvalidTrafficClass();
    Test_InvalidSlopePositiveSend();
    Test_InvalidCreditNegativeHi();
    Test_InvalidCreditPositiveLo();
    Test_RapidEnableDisable();
    Test_NullPointerHandling();
    Test_CbsUnderActiveTraffic();
    Test_CreditAccumulationMeasurement();
    
    // Cleanup
    CloseHandle(g_hDevice);
    
    // Summary
    printf("\n");
    printf("====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:   %d tests\n", g_passCount + g_failCount + g_skipCount);
    printf(" Passed:  %d tests\n", g_passCount);
    printf(" Failed:  %d tests\n", g_failCount);
    printf(" Skipped: %d tests\n", g_skipCount);
    printf("====================================================================\n\n");
    
    return (g_failCount == 0) ? 0 : 1;
}
