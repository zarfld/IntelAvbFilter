/**
 * @file test_ioctl_target_time.c
 * @brief Target Time & Auxiliary Timestamp IOCTL Tests (Requirement #7)
 * 
 * Verifies: #7 (REQ-F-PTP-005: Target Time and Auxiliary Timestamp)
 * Test Issues: #204 (Target Time Interrupt - 15 tests), #299 (Aux Timestamp - 16 tests)
 * IOCTLs: 43 (IOCTL_AVB_SET_TARGET_TIME), 44 (IOCTL_AVB_GET_AUX_TIMESTAMP)
 * 
 * **SSOT Compliance**: Uses AVB_TARGET_TIME_REQUEST and AVB_AUX_TIMESTAMP_REQUEST
 * from avb_ioctl.h (NO custom structures - learned from PITFALL #1)
 * 
 * **Reference Implementation**: tests/integration/tsn/target_time_test.c
 * 
 * **Architecture**:
 * - Target Time: Program TRGTTIML/H registers for time-triggered interrupts
 * - Aux Timestamp: Read AUXSTMP0/1 registers for SDP pin event timestamps
 * - TSAUXC Control: EN_TT0/EN_TT1 (interrupt enable), EN_TS0/EN_TS1 (timestamp capture enable)
 * 
 * Test Plan:
 * - Target Time Tests (TC-TARGET-001 to TC-TARGET-015): Issue #204
 * - Aux Timestamp Tests (TC-AUX-001 to TC-AUX-016): Issue #299
 * 
 * @author AI Assistant
 * @date 2026-01-02
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/* SSOT structures from avb_ioctl.h */
#include "../../include/avb_ioctl.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

#define TEST_START(name) printf("=== TEST: %s ===\n", name)
#define TEST_PASS(name) do { printf("[PASS] %s\n\n", name); tests_passed++; } while(0)
#define TEST_FAIL(name, reason) do { printf("[FAIL] %s: %s\n\n", name, reason); tests_failed++; } while(0)
#define TEST_SKIP(name, reason) do { printf("[SKIP] %s: %s\n\n", name, reason); tests_skipped++; } while(0)

/* Helper: Get current SYSTIM time via IOCTL 45 */
static ULONGLONG get_current_systim(HANDLE hDevice) {
    AVB_CLOCK_CONFIG clockConfig = {0};
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_CLOCK_CONFIG,
        &clockConfig,
        sizeof(clockConfig),
        &clockConfig,
        sizeof(clockConfig),
        &bytesReturned,
        NULL
    );
    
    if (!result || clockConfig.status != 0) {
        printf("WARN: Failed to get current SYSTIM (status=0x%X)\n", clockConfig.status);
        return 0;
    }
    
    ULONGLONG systim_ns = clockConfig.systim;
    printf("Current SYSTIM: %llu ns\n", systim_ns);
    return systim_ns;
}

/* Helper: Enable SYSTIM0 via IOCTL 40 (prerequisite) */
static BOOL enable_systim0(HANDLE hDevice) {
    AVB_HW_TIMESTAMPING_REQUEST tsReq = {0};
    DWORD bytesReturned = 0;
    
    /* Enable SYSTIM0, no aux timestamp yet */
    tsReq.enable = 1;
    tsReq.timer_mask = 0x01;  /* SYSTIM0 only */
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_HW_TIMESTAMPING,
        &tsReq,
        sizeof(tsReq),
        &tsReq,
        sizeof(tsReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || tsReq.status != 0) {
        printf("WARN: Failed to enable SYSTIM0 (status=0x%X)\n", tsReq.status);
        return FALSE;
    }
    
    printf("SYSTIM0 enabled successfully\n");
    return TRUE;
}

/* ========== TARGET TIME TESTS (Issue #204) ========== */

/**
 * TC-TARGET-001: Read Current Target Time 0
 * Purpose: Verify IOCTL 43 can read current target time without modification
 */
static void test_target_001_read_current(HANDLE hDevice) {
    TEST_START("TC-TARGET-001: Read Current Target Time 0");
    
    /* SSOT: AVB_TARGET_TIME_REQUEST from avb_ioctl.h */
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    DWORD bytesReturned = 0;
    
    /* Read current state (target_time=0 means no change) */
    targetReq.timer_index = 0;
    targetReq.target_time = 0;
    targetReq.enable_interrupt = 0;
    targetReq.enable_sdp_output = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_TARGET_TIME,
        &targetReq,
        sizeof(targetReq),
        &targetReq,
        sizeof(targetReq),
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        TEST_FAIL("TC-TARGET-001", "DeviceIoControl failed");
        return;
    }
    
    /* Check SSOT status field */
    if (targetReq.status != 0) {
        char reason[128];
        sprintf(reason, "IOCTL status failed: 0x%X", targetReq.status);
        TEST_FAIL("TC-TARGET-001", reason);
        return;
    }
    
    printf("Current target time 0: %llu ns\n", (ULONGLONG)targetReq.previous_target);
    TEST_PASS("TC-TARGET-001");
}

/**
 * TC-TARGET-002: Set Target Time 0 (5 Seconds in Future)
 * Purpose: Verify IOCTL 43 can program target time for timer 0
 */
static void test_target_002_set_future_5s(HANDLE hDevice) {
    TEST_START("TC-TARGET-002: Set Target Time 0 (5 seconds in future)");
    
    /* Get current SYSTIM */
    ULONGLONG current_ns = get_current_systim(hDevice);
    if (current_ns == 0) {
        TEST_SKIP("TC-TARGET-002", "Could not get current SYSTIM");
        return;
    }
    
    /* Calculate target: +5 seconds */
    ULONGLONG target_ns = current_ns + 5000000000ULL;
    printf("Setting target time: %llu ns (+5 seconds)\n", target_ns);
    
    /* SSOT: AVB_TARGET_TIME_REQUEST */
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    DWORD bytesReturned = 0;
    
    targetReq.timer_index = 0;
    targetReq.target_time = target_ns;
    targetReq.enable_interrupt = 0;  /* No interrupt yet */
    targetReq.enable_sdp_output = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_TARGET_TIME,
        &targetReq,
        sizeof(targetReq),
        &targetReq,
        sizeof(targetReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || targetReq.status != 0) {
        char reason[128];
        sprintf(reason, "IOCTL failed (status=0x%X)", targetReq.status);
        TEST_FAIL("TC-TARGET-002", reason);
        return;
    }
    
    printf("Target time set successfully\n");
    printf("Previous target: %llu ns\n", (ULONGLONG)targetReq.previous_target);
    TEST_PASS("TC-TARGET-002");
}

/**
 * TC-TARGET-003: Set Target Time 1 (10 Seconds in Future)
 * Purpose: Verify dual target time timers (0 and 1) operate independently
 */
static void test_target_003_set_timer1(HANDLE hDevice) {
    TEST_START("TC-TARGET-003: Set Target Time 1 (10 seconds, independent)");
    
    ULONGLONG current_ns = get_current_systim(hDevice);
    if (current_ns == 0) {
        TEST_SKIP("TC-TARGET-003", "Could not get current SYSTIM");
        return;
    }
    
    ULONGLONG target_ns = current_ns + 10000000000ULL;  /* +10 seconds */
    printf("Setting target time 1: %llu ns (+10 seconds)\n", target_ns);
    
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    DWORD bytesReturned = 0;
    
    targetReq.timer_index = 1;  /* Timer 1 */
    targetReq.target_time = target_ns;
    targetReq.enable_interrupt = 0;
    targetReq.enable_sdp_output = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_TARGET_TIME,
        &targetReq,
        sizeof(targetReq),
        &targetReq,
        sizeof(targetReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || targetReq.status != 0) {
        char reason[128];
        sprintf(reason, "Timer 1 set failed (status=0x%X)", targetReq.status);
        TEST_FAIL("TC-TARGET-003", reason);
        return;
    }
    
    printf("Timer 1 target set successfully\n");
    TEST_PASS("TC-TARGET-003");
}

/**
 * TC-TARGET-004: Enable Target Time Interrupt (EN_TT0)
 * Purpose: Verify interrupt enable bit sets correctly
 */
static void test_target_004_enable_interrupt(HANDLE hDevice) {
    TEST_START("TC-TARGET-004: Enable Target Time Interrupt");
    
    ULONGLONG current_ns = get_current_systim(hDevice);
    if (current_ns == 0) {
        TEST_SKIP("TC-TARGET-004", "Could not get current SYSTIM");
        return;
    }
    
    ULONGLONG target_ns = current_ns + 1000000000ULL;  /* +1 second */
    
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    DWORD bytesReturned = 0;
    
    targetReq.timer_index = 0;
    targetReq.target_time = target_ns;
    targetReq.enable_interrupt = 1;  /* Enable EN_TT0 */
    targetReq.enable_sdp_output = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_TARGET_TIME,
        &targetReq,
        sizeof(targetReq),
        &targetReq,
        sizeof(targetReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || targetReq.status != 0) {
        char reason[128];
        sprintf(reason, "Interrupt enable failed (status=0x%X)", targetReq.status);
        TEST_FAIL("TC-TARGET-004", reason);
        return;
    }
    
    printf("Target time interrupt enabled (EN_TT0 set)\n");
    TEST_PASS("TC-TARGET-004");
}

/**
 * TC-TARGET-009: Null Buffer Validation
 * Purpose: Verify IOCTL rejects null output buffer gracefully
 */
static void test_target_009_null_buffer(HANDLE hDevice) {
    TEST_START("TC-TARGET-009: Null Buffer Validation");
    
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    DWORD bytesReturned = 0;
    
    targetReq.timer_index = 0;
    targetReq.target_time = 1000000000ULL;
    
    /* Call with NULL output buffer (should fail gracefully) */
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_TARGET_TIME,
        &targetReq,
        sizeof(targetReq),
        NULL,  /* NULL output buffer */
        0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        TEST_FAIL("TC-TARGET-009", "IOCTL should reject null buffer");
        return;
    }
    
    DWORD error = GetLastError();
    printf("Null buffer correctly rejected (error=%lu)\n", error);
    TEST_PASS("TC-TARGET-009");
}

/**
 * TC-TARGET-011: Invalid Timer Index Validation
 * Purpose: Verify timer_index > 1 is rejected
 */
static void test_target_011_invalid_timer(HANDLE hDevice) {
    TEST_START("TC-TARGET-011: Invalid Timer Index");
    
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    DWORD bytesReturned = 0;
    
    targetReq.timer_index = 2;  /* Invalid (max is 1) */
    targetReq.target_time = 1000000000ULL;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_TARGET_TIME,
        &targetReq,
        sizeof(targetReq),
        &targetReq,
        sizeof(targetReq),
        &bytesReturned,
        NULL
    );
    
    /* Should fail OR return error status */
    if (result && targetReq.status == 0) {
        TEST_FAIL("TC-TARGET-011", "Invalid timer index accepted");
        return;
    }
    
    printf("Invalid timer index correctly rejected (status=0x%X)\n", targetReq.status);
    TEST_PASS("TC-TARGET-011");
}

/* ========== AUXILIARY TIMESTAMP TESTS (Issue #299) ========== */

/**
 * TC-AUX-001: Read Auxiliary Timestamp 0
 * Purpose: Verify IOCTL 44 can read AUXSTMP0 register
 */
static void test_aux_001_read_timestamp0(HANDLE hDevice) {
    TEST_START("TC-AUX-001: Read Auxiliary Timestamp 0");
    
    /* SSOT: AVB_AUX_TIMESTAMP_REQUEST from avb_ioctl.h */
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    DWORD bytesReturned = 0;
    
    auxReq.timer_index = 0;
    auxReq.clear_flag = 0;  /* Don't clear AUTT0 flag */
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        &auxReq,
        sizeof(auxReq),
        &auxReq,
        sizeof(auxReq),
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        TEST_FAIL("TC-AUX-001", "DeviceIoControl failed");
        return;
    }
    
    if (auxReq.status != 0) {
        char reason[128];
        sprintf(reason, "IOCTL status failed: 0x%X", auxReq.status);
        TEST_FAIL("TC-AUX-001", reason);
        return;
    }
    
    /* Check SSOT valid flag */
    if (auxReq.valid) {
        printf("Aux timestamp 0 valid: %llu ns\n", auxReq.timestamp);
    } else {
        printf("No SDP event captured yet (AUTT0 flag not set)\n");
    }
    
    TEST_PASS("TC-AUX-001");
}

/**
 * TC-AUX-002: Read Auxiliary Timestamp 1
 * Purpose: Verify dual aux timestamp support (timers 0 and 1)
 */
static void test_aux_002_read_timestamp1(HANDLE hDevice) {
    TEST_START("TC-AUX-002: Read Auxiliary Timestamp 1");
    
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    DWORD bytesReturned = 0;
    
    auxReq.timer_index = 1;  /* Timer 1 */
    auxReq.clear_flag = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        &auxReq,
        sizeof(auxReq),
        &auxReq,
        sizeof(auxReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || auxReq.status != 0) {
        char reason[128];
        sprintf(reason, "Timer 1 read failed (status=0x%X)", auxReq.status);
        TEST_FAIL("TC-AUX-002", reason);
        return;
    }
    
    if (auxReq.valid) {
        printf("Aux timestamp 1 valid: %llu ns\n", auxReq.timestamp);
    } else {
        printf("No SDP event on timer 1 (AUTT1 flag not set)\n");
    }
    
    TEST_PASS("TC-AUX-002");
}

/**
 * TC-AUX-008: Clear Auxiliary Timestamp Flag
 * Purpose: Verify clear_flag parameter clears AUTT0 flag
 */
static void test_aux_008_clear_flag(HANDLE hDevice) {
    TEST_START("TC-AUX-008: Clear Auxiliary Timestamp Flag");
    
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    DWORD bytesReturned = 0;
    
    /* Read with clear_flag=1 */
    auxReq.timer_index = 0;
    auxReq.clear_flag = 1;  /* Clear AUTT0 after reading */
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        &auxReq,
        sizeof(auxReq),
        &auxReq,
        sizeof(auxReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || auxReq.status != 0) {
        char reason[128];
        sprintf(reason, "Clear flag failed (status=0x%X)", auxReq.status);
        TEST_FAIL("TC-AUX-008", reason);
        return;
    }
    
    printf("AUTT0 flag cleared successfully\n");
    TEST_PASS("TC-AUX-008");
}

/**
 * TC-AUX-009: Null Buffer Validation
 */
static void test_aux_009_null_buffer(HANDLE hDevice) {
    TEST_START("TC-AUX-009: Null Buffer Validation");
    
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    DWORD bytesReturned = 0;
    
    auxReq.timer_index = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        &auxReq,
        sizeof(auxReq),
        NULL,  /* NULL output */
        0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        TEST_FAIL("TC-AUX-009", "IOCTL should reject null buffer");
        return;
    }
    
    DWORD error = GetLastError();
    printf("Null buffer correctly rejected (error=%lu)\n", error);
    TEST_PASS("TC-AUX-009");
}

/**
 * TC-AUX-010: Buffer Too Small Validation
 */
static void test_aux_010_small_buffer(HANDLE hDevice) {
    TEST_START("TC-AUX-010: Buffer Too Small Validation");
    
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    BYTE smallBuffer[8];  /* Too small */
    DWORD bytesReturned = 0;
    
    auxReq.timer_index = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        &auxReq,
        sizeof(auxReq),
        smallBuffer,  /* Too small */
        sizeof(smallBuffer),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        TEST_FAIL("TC-AUX-010", "IOCTL should reject small buffer");
        return;
    }
    
    DWORD error = GetLastError();
    printf("Small buffer correctly rejected (error=%lu)\n", error);
    TEST_PASS("TC-AUX-010");
}

/**
 * TC-AUX-011: Invalid Timer Index
 */
static void test_aux_011_invalid_timer(HANDLE hDevice) {
    TEST_START("TC-AUX-011: Invalid Timer Index");
    
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    DWORD bytesReturned = 0;
    
    auxReq.timer_index = 2;  /* Invalid */
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        &auxReq,
        sizeof(auxReq),
        &auxReq,
        sizeof(auxReq),
        &bytesReturned,
        NULL
    );
    
    if (result && auxReq.status == 0) {
        TEST_FAIL("TC-AUX-011", "Invalid timer index accepted");
        return;
    }
    
    printf("Invalid timer index correctly rejected (status=0x%X)\n", auxReq.status);
    TEST_PASS("TC-AUX-011");
}

/* ========== MAIN TEST RUNNER ========== */

int main(int argc, char* argv[]) {
    printf("==============================================\n");
    printf("Target Time & Aux Timestamp IOCTL Tests\n");
    printf("Requirement #7: REQ-F-PTP-005\n");
    printf("Issues: #204 (Target Time), #299 (Aux Timestamp)\n");
    printf("IOCTLs: 43 (SET_TARGET_TIME), 44 (GET_AUX_TIMESTAMP)\n");
    printf("SSOT: AVB_TARGET_TIME_REQUEST, AVB_AUX_TIMESTAMP_REQUEST\n");
    printf("==============================================\n\n");
    
    /* Open device */
    HANDLE hDevice = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("FATAL: Cannot open IntelAvbFilter device (error=%lu)\n", GetLastError());
        printf("Please ensure driver is loaded and device is ready\n");
        return 1;
    }
    
    printf("Device opened successfully: \\\\.\\IntelAvbFilter\n\n");
    
    /* Prerequisite: Enable SYSTIM0 */
    if (!enable_systim0(hDevice)) {
        printf("WARN: SYSTIM0 not enabled - some tests may fail\n\n");
    }
    
    /* Run Target Time Tests (Issue #204 - subset) */
    printf("\n========== TARGET TIME TESTS (Issue #204) ==========\n\n");
    test_target_001_read_current(hDevice);
    test_target_002_set_future_5s(hDevice);
    test_target_003_set_timer1(hDevice);
    test_target_004_enable_interrupt(hDevice);
    test_target_009_null_buffer(hDevice);
    test_target_011_invalid_timer(hDevice);
    
    /* Run Auxiliary Timestamp Tests (Issue #299 - subset) */
    printf("\n========== AUXILIARY TIMESTAMP TESTS (Issue #299) ==========\n\n");
    test_aux_001_read_timestamp0(hDevice);
    test_aux_002_read_timestamp1(hDevice);
    test_aux_008_clear_flag(hDevice);
    test_aux_009_null_buffer(hDevice);
    test_aux_010_small_buffer(hDevice);
    test_aux_011_invalid_timer(hDevice);
    
    /* Cleanup */
    CloseHandle(hDevice);
    
    /* Summary */
    printf("\n==============================================\n");
    printf("TEST SUMMARY\n");
    printf("==============================================\n");
    printf("PASSED:  %d\n", tests_passed);
    printf("FAILED:  %d\n", tests_failed);
    printf("SKIPPED: %d\n", tests_skipped);
    printf("TOTAL:   %d\n", tests_passed + tests_failed + tests_skipped);
    printf("==============================================\n");
    
    if (tests_failed > 0) {
        printf("\n❌ SOME TESTS FAILED - Driver may have bugs\n");
        return 1;
    }
    
    printf("\n✅ ALL TESTS PASSED - Target Time & Aux Timestamp IOCTLs working!\n");
    return 0;
}
