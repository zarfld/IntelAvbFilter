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
#include "../common/avb_ntstatus.h"   /* exact NTSTATUS for the TC-TARGET-015 verdicts (#299) */

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

/**
 * TC-TARGET-005: Past Target Time Detection (Missed-Deadline)
 *
 * Implements gap closure for Issue #209: Cat.3 — "missed-deadline detection" not covered.
 *
 * Sets target_time = now - 1,000,000,000 ns (1 second in the past) and verifies that
 * the driver handles the late-time scenario gracefully. Two acceptable outcomes:
 *   (a) IOCTL returns an error status (driver detects past time and rejects it), OR
 *   (b) IOCTL succeeds but sets a status field indicating the deadline was already missed.
 * What MUST NOT happen: silent acceptance that looks identical to a future target time.
 *
 * Traces to: #209 (REQ-F-PTP-005 — gate-window enforcement)
 */
static void test_target_005_past_time_detection(HANDLE hDevice) {
    TEST_START("TC-TARGET-005: Past Target Time Detection (1s in past)");

    ULONGLONG current_ns = get_current_systim(hDevice);
    if (current_ns == 0) {
        TEST_SKIP("TC-TARGET-005", "Could not read current SYSTIM");
        return;
    }

    /* Target time is 1 second in the past */
    ULONGLONG past_ns = (current_ns > 1000000000ULL) ? (current_ns - 1000000000ULL) : 1ULL;
    printf("  Current SYSTIM: %llu ns\n", current_ns);
    printf("  Past target:    %llu ns (-%llu ns)\n", past_ns, current_ns - past_ns);

    AVB_TARGET_TIME_REQUEST req = {0};
    DWORD bytesRet = 0;
    req.timer_index = 0;
    req.target_time = past_ns;
    req.enable_interrupt = 0;
    req.enable_sdp_output = 0;

    BOOL ok = DeviceIoControl(hDevice, IOCTL_AVB_SET_TARGET_TIME,
                               &req, sizeof(req),
                               &req, sizeof(req),
                               &bytesRet, NULL);

    /* Acceptable outcome 1: IOCTL fails with Win32 error (e.g. ERROR_INVALID_PARAMETER) */
    if (!ok) {
        DWORD err = GetLastError();
        printf("  IOCTL rejected past target time (Win32 error=%lu) — PASS\n", err);
        TEST_PASS("TC-TARGET-005");
        return;
    }

    /* Acceptable outcome 2: IOCTL succeeds but status field signals missed deadline */
    if (req.status != 0) {
        printf("  IOCTL returned status=0x%X (non-zero = past-time indicated) — PASS\n",
               req.status);
        TEST_PASS("TC-TARGET-005");
        return;
    }

    /* Unacceptable: both ok AND status==0 and no distinguishing signal */
    /* Allow it if previous_target differs from requested target (driver rewrote to 0) */
    if (req.previous_target != past_ns && req.previous_target == 0) {
        printf("  WARN: IOCTL succeeded silently for past time — driver did not reject or signal\n");
        printf("  previous_target=%llu (driver zeroed the target)\n",
               (ULONGLONG)req.previous_target);
        TEST_FAIL("TC-TARGET-005",
                  "Past target time accepted without any error signal — missed-deadline not detected");
        return;
    }

    /* Any other silent acceptance is also a failure */
    printf("  WARN: IOCTL succeeded for past time with status=0 (no rejection signalled)\n");
    TEST_FAIL("TC-TARGET-005",
              "Past target time accepted silently — missed-deadline detection absent");
}

/**
 * TC-TARGET-006: Imminent Deadline (Target < 1ms in Future)
 *
 * Implements gap closure for Issue #209: very short gate-window tolerance.
 *
 * Sets target_time = now + 500,000 ns (0.5 ms). By the time the IOCTL returns, the
 * target time may already have elapsed. Driver must either:
 *   (a) Report it as missed (non-zero status), or
 *   (b) Accept it and arm the interrupt (target fires within next few ms).
 * This test does NOT wait for the interrupt — it only verifies the IOCTL does not
 * return an unexpected fatal error for a valid near-future time.
 *
 * Traces to: #209 (REQ-F-PTP-005 — gate-window enforcement)
 */
static void test_target_006_imminent_deadline(HANDLE hDevice) {
    TEST_START("TC-TARGET-006: Imminent Deadline (target +0.5ms in future)");

    ULONGLONG current_ns = get_current_systim(hDevice);
    if (current_ns == 0) {
        TEST_SKIP("TC-TARGET-006", "Could not read current SYSTIM");
        return;
    }

    ULONGLONG soon_ns = current_ns + 500000ULL;  /* +0.5 ms */
    printf("  Current SYSTIM: %llu ns\n", current_ns);
    printf("  Imminent target: %llu ns (+0.5ms)\n", soon_ns);

    AVB_TARGET_TIME_REQUEST req = {0};
    DWORD bytesRet = 0;
    req.timer_index = 1;  /* Use timer 1 to avoid clobbering any active timer 0 */
    req.target_time = soon_ns;
    req.enable_interrupt = 0;
    req.enable_sdp_output = 0;

    BOOL ok = DeviceIoControl(hDevice, IOCTL_AVB_SET_TARGET_TIME,
                               &req, sizeof(req),
                               &req, sizeof(req),
                               &bytesRet, NULL);

    if (!ok) {
        DWORD err = GetLastError();
        /* Non-zero Win32 error is acceptable only if it's "invalid parameter"
         * (driver rejected the near-future time as already-missed).             */
        if (err == ERROR_INVALID_PARAMETER) {
            printf("  Driver rejected imminent target as already-missed (Win32=%lu) — acceptable\n",
                   err);
            TEST_PASS("TC-TARGET-006");
        } else {
            char reason[128];
            sprintf(reason, "Unexpected IOCTL failure for near-future target (Win32 err=%lu)", err);
            TEST_FAIL("TC-TARGET-006", reason);
        }
        return;
    }

    /* IOCTL succeeded — either timer armed or already missed; both are acceptable */
    if (req.status == 0) {
        printf("  Driver accepted imminent target (armed or may fire immediately) — PASS\n");
    } else {
        printf("  Driver signalled past-deadline status=0x%X — PASS (detected near-miss)\n",
               req.status);
    }
    TEST_PASS("TC-TARGET-006");
}

/**
 * TC-TARGET-007: Consecutive Target Times — Gate-Window Collision
 *
 * Implements gap closure for Issue #209: two timers set with overlapping gate-windows.
 *
 * Sets timer 0 target = now + 2s, then immediately sets timer 1 target = now + 2s + 1ms.
 * Both are valid and non-overlapping (different timers). Verifies:
 *   - Both SET_TARGET_TIME calls succeed independently.
 *   - Timer indices are independent (one does not clobber the other).
 *
 * Traces to: #209 (REQ-F-PTP-005 — independent dual-timer gate-window)
 */
static void test_target_007_dual_timer_window(HANDLE hDevice) {
    TEST_START("TC-TARGET-007: Dual-Timer Gate-Window (timers 0 and 1 near-simultaneous)");

    ULONGLONG current_ns = get_current_systim(hDevice);
    if (current_ns == 0) {
        TEST_SKIP("TC-TARGET-007", "Could not read current SYSTIM");
        return;
    }

    ULONGLONG base_ns    = current_ns + 2000000000ULL;  /* +2s */
    ULONGLONG offset_ns  = base_ns   + 1000000ULL;      /* +2.001s */

    printf("  Timer 0 target: %llu ns (+2s)\n", base_ns);
    printf("  Timer 1 target: %llu ns (+2.001s)\n", offset_ns);

    /* Set timer 0 */
    AVB_TARGET_TIME_REQUEST req0 = {0};
    DWORD bytesRet = 0;
    req0.timer_index = 0;
    req0.target_time = base_ns;
    req0.enable_interrupt = 0;

    BOOL ok0 = DeviceIoControl(hDevice, IOCTL_AVB_SET_TARGET_TIME,
                                &req0, sizeof(req0),
                                &req0, sizeof(req0),
                                &bytesRet, NULL);
    if (!ok0 || req0.status != 0) {
        char reason[128];
        sprintf(reason, "Timer 0 SET_TARGET_TIME failed (ok=%d, status=0x%X)", ok0, req0.status);
        TEST_FAIL("TC-TARGET-007", reason);
        return;
    }
    printf("  Timer 0 set: OK\n");

    /* Set timer 1 (should not interfere) */
    AVB_TARGET_TIME_REQUEST req1 = {0};
    bytesRet = 0;
    req1.timer_index = 1;
    req1.target_time = offset_ns;
    req1.enable_interrupt = 0;

    BOOL ok1 = DeviceIoControl(hDevice, IOCTL_AVB_SET_TARGET_TIME,
                                &req1, sizeof(req1),
                                &req1, sizeof(req1),
                                &bytesRet, NULL);
    if (!ok1 || req1.status != 0) {
        char reason[128];
        sprintf(reason, "Timer 1 SET_TARGET_TIME failed (ok=%d, status=0x%X)", ok1, req1.status);
        TEST_FAIL("TC-TARGET-007", reason);
        return;
    }
    printf("  Timer 1 set: OK (independent)\n");

    /* Verify timer 0 previous_target reflects two separate writes */
    printf("  Timer 0 previous_target: %llu\n", (ULONGLONG)req0.previous_target);
    printf("  Timer 1 previous_target: %llu\n", (ULONGLONG)req1.previous_target);

    TEST_PASS("TC-TARGET-007");
}

/* ========== TASK 7: TARGET TIME EVENT DELIVERY TEST ========== */

/**
 * TC-TARGET-EVENT-001: Target Time Event Delivery (Task 7 Validation)
 * Purpose: Verify TS_EVENT_TARGET_TIME (0x04) events are posted when SYSTIM >= TRGTTIML0
 * 
 * Steps:
 * 1. Subscribe to TS_EVENT_TARGET_TIME events
 * 2. Map ring buffer
 * 3. Get current SYSTIM
 * 4. Set target time +2 seconds in future
 * 5. Poll ring buffer for 3 seconds
 * 6. Verify event delivered with correct type and timestamp
 * 7. Verify AUTT0 flag was cleared
 */
static void test_task7_target_time_event(HANDLE hDevice) {
    TEST_START("TC-TARGET-EVENT-001: Target Time Event Delivery (Task 7)");
    
    AVB_TS_SUBSCRIBE_REQUEST subReq = {0};
    AVB_TS_RING_MAP_REQUEST mapReq = {0};
    AVB_TARGET_TIME_REQUEST targetReq = {0};
    AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
    DWORD bytesReturned = 0;
    BOOL result;
    
    /* Step 1: Subscribe to target time events */
    subReq.types_mask = TS_EVENT_TARGET_TIME;  /* 0x04 */
    subReq.vlan = 0xFFFF;  /* No VLAN filtering */
    subReq.pcp = 0xFF;     /* No PCP filtering */
    subReq.ring_id = 0;    /* Auto-assign ring ID */
    // NOTE: ring_size parameter removed from new API - driver determines size
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_TS_SUBSCRIBE,
        &subReq,
        sizeof(subReq),
        &subReq,
        sizeof(subReq),
        &bytesReturned,
        NULL
    );
    
    if (!result || subReq.status != 0 || subReq.ring_id == 0) {
        TEST_FAIL("TC-TARGET-EVENT-001", "Failed to subscribe to target time events");
        return;
    }
    
    printf("✓ Subscribed to target time events (ring_id=%u)\n", subReq.ring_id);
    
    /* Step 2: Map ring buffer */
    mapReq.ring_id = subReq.ring_id;
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_TS_RING_MAP,
        &mapReq,
        sizeof(mapReq),
        &mapReq,
        sizeof(mapReq),
        &bytesReturned,
        NULL
    );
    
    // TODO: New API returns shm_token (HANDLE) instead of direct pointer
    // Need to implement MapViewOfFile() to map the shared memory section
    // For now, skip ring buffer tests
    if (!result || mapReq.status != 0 || mapReq.shm_token == 0) {
        TEST_FAIL("TC-TARGET-EVENT-001", "Failed to map ring buffer (TODO: implement MapViewOfFile)");
        goto cleanup_subscription;
    }
    
    printf("✓ Ring buffer token obtained: 0x%llx (TODO: map with MapViewOfFile)\n", mapReq.shm_token);
    
    /* TODO: Map shared memory using MapViewOfFile(mapReq.shm_token, ...) */
    // For now, skip the ring buffer event polling test until MapViewOfFile is implemented
    TEST_SKIP("TC-TARGET-EVENT-001", "Ring buffer mapping not yet implemented (need MapViewOfFile for shm_token)");
    goto cleanup_subscription;
    
    // The code below would work once MapViewOfFile is implemented:
    #if 0  // Disabled until shared memory mapping is implemented
    AVB_TIMESTAMP_RING_HEADER *hdr = NULL;  // Would get from MapViewOfFile
    AVB_TIMESTAMP_EVENT *events = (AVB_TIMESTAMP_EVENT *)(hdr + 1);
    
    /* Step 3: Get current SYSTIM */
    ULONGLONG current_systim = get_current_systim(hDevice);
    if (current_systim == 0) {
        TEST_SKIP("TC-TARGET-EVENT-001", "Cannot read SYSTIM");
        goto cleanup_map;
    }
    
    /* Step 4: Set target time +2 seconds in future */
    ULONGLONG target_time_ns = current_systim + 2000000000ULL;  /* +2 seconds */
    
    targetReq.timer_index = 0;
    targetReq.target_time = target_time_ns;
    targetReq.enable_interrupt = 1;  /* Enable EN_TT0 */
    
    result = DeviceIoControl(
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
        TEST_FAIL("TC-TARGET-EVENT-001", "Failed to set target time");
        goto cleanup_map;
    }
    
    printf("✓ Target time set to %llu ns (current + 2.0s)\n", target_time_ns);
    printf("  Polling ring buffer for 3 seconds...\n");
    
    /* Step 5: Poll ring buffer for 3 seconds */
    BOOL event_received = FALSE;
    ULONGLONG event_timestamp = 0;
    DWORD start_tick = GetTickCount();
    DWORD consumer_index = hdr->consumer_index;
    
    while ((GetTickCount() - start_tick) < 3000) {  /* 3 second timeout */
        /* Check if new events available */
        DWORD producer_index = hdr->producer_index;
        
        if (consumer_index != producer_index) {
            /* Read event from ring */
            DWORD idx = consumer_index & hdr->mask;
            AVB_TIMESTAMP_EVENT *evt = &events[idx];
            
            printf("  Event received: type=0x%02X, timestamp=%llu ns, trigger=%u\n",
                   evt->event_type, evt->timestamp_ns, evt->trigger_source);
            
            /* Verify this is our target time event */
            if (evt->event_type == TS_EVENT_TARGET_TIME) {
                event_received = TRUE;
                event_timestamp = evt->timestamp_ns;
                
                /* Verify timestamp is near target time (within 2ms for polling) */
                LONGLONG delta = (LONGLONG)(evt->timestamp_ns - target_time_ns);
                if (delta < 0) delta = -delta;
                
                if (delta > 5000000) {  /* 5ms tolerance */
                    printf("  WARNING: Timestamp delta too large: %lld ns (%lld ms)\n", 
                           delta, delta / 1000000);
                }
                
                /* Verify trigger_source is 0 (timer 0) */
                if (evt->trigger_source != 0) {
                    printf("  WARNING: Expected trigger_source=0, got %u\n", evt->trigger_source);
                }
                
                break;
            }
            
            consumer_index++;
        }
        
        Sleep(10);  /* Poll every 10ms */
    }
    
    if (!event_received) {
        TEST_FAIL("TC-TARGET-EVENT-001", "No target time event received within 3 seconds");
        goto cleanup_map;
    }
    
    printf("✓ Target time event received at %llu ns\n", event_timestamp);
    
    /* Step 7: Verify AUTT0 flag was cleared */
    auxReq.timer_index = 0;
    auxReq.clear_flag = 0;  /* Just read */
    
    result = DeviceIoControl(
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
        if (auxReq.valid) {
            printf("  WARNING: AUTT0 flag still set (should have been cleared)\n");
        } else {
            printf("✓ AUTT0 flag correctly cleared after event\n");
        }
    }
    
    TEST_PASS("TC-TARGET-EVENT-001");
    
cleanup_map:
    #endif  // End of disabled ring buffer code
    
    /* Unmap ring buffer */
    // TODO: Implement UnmapViewOfFile when MapViewOfFile is added
    // if (hdr != NULL) { UnmapViewOfFile(hdr); }
    
cleanup_subscription:
    /* Unsubscribe */
    AVB_TS_UNSUBSCRIBE_REQUEST unsubReq = {0};
    unsubReq.ring_id = subReq.ring_id;
    
    DeviceIoControl(
        hDevice,
        IOCTL_AVB_TS_UNSUBSCRIBE,
        &unsubReq,
        sizeof(unsubReq),
        &unsubReq,
        sizeof(unsubReq),
        &bytesReturned,
        NULL
    );
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

/* =====================================================================================================================
 * TC-TARGET-015 (#299): timer-0 target-time event on every enumerated adapter - characterization step
 *                       (selected with --case TC-TARGET-015; the default run is unchanged)
 *
 * Question: when IOCTL_AVB_SET_TARGET_TIME programs timer 0 with a target in NANOSECONDS (the public contract of
 * AVB_TARGET_TIME_REQUEST.target_time) and enable_interrupt = 1, does the driver deliver a new TS_EVENT_TARGET_TIME
 * event for timer 0 after the PHC has reached that target?
 *
 * - Every adapter the driver enumerates is bound separately by NET_LUID (no capability stand-in, no first-adapter pick).
 *   Applicability follows the operation itself: there is no per-operation target-time capability contract, so a
 *   STATUS_NOT_SUPPORTED is INCONCLUSIVE, never SKIP (docs/testing/test-verdict-contract.md).
 * - PHC time comes only from IOCTL_AVB_PHC_CROSSTIMESTAMP_V2 with AVB_PHC_SOURCE_REGISTER_DERIVED; a software
 *   fallback sample does not count.
 * - The target is the contract value (PHC ns + lead).  The test does NOT convert it into any register format.  The
 *   requested target, the register content read back through QUERY_HW_SNAPSHOT version 2 (raw, not decoded) and the
 *   event are reported separately.  The event's stored timestamp is what the driver read from the registers; it
 *   identifies the event, it is not a trigger time and not functional evidence.
 * - Measurements, ATTRIBUTION, FUNCTION and RESTORATION are reported separately per adapter.
 *   Measurements: baseline events (a window before the request), every new event, the posting interval of the first
 *   timer-0 event (PHC before the previous ring check .. PHC after the ring check that saw it; it bounds the driver's
 *   posting, not the hardware trigger) and the event's timestamp PATTERN (target registers as read, or a PHC at
 *   detection) - a pattern names a driver posting path, it is not a request identifier.
 *   ATTRIBUTION: this test cannot exclude an independent source of timer-0 target-time events (for example an already
 *   active clock output on the same target time).  An observed event is therefore never attributed to the request:
 *   INCONCLUSIVE; "none" when no timer-0 event arrived.
 *   FUNCTION: FAIL only for a request-independent finding - the required event is missing although the PHC passed the
 *   requested target by TT15_OBSERVE_AFTER_NS, established by a PHC sample taken BEFORE a terminal ring read (the ring
 *   is reconciled after that sample; otherwise INCONCLUSIVE).  A present event yields neither PASS nor a request-specific timing FAIL
 *   (INCONCLUSIVE, with its interval reported); PHC sample gaps and ring overflow are INCONCLUSIVE.  PASS is not
 *   reachable in this version.  TT15_OBSERVE_AFTER_NS is a diagnostic observation bound only - it does not replace or
 *   relax any accuracy requirement of #299.
 * - Timer 1 is never programmed here.  Register values are printed raw; their bit meaning is evaluated outside the
 *   test against the documented sources.
 * - Cleanup: unsubscribe; then the observable state (TSAUXC, TRGTTIML0/H0) is compared with the pre-test snapshot.
 *   No IOCTL restores target-time state; state the operation may change but no IOCTL reads back is NOT_VERIFIED.
 * ===================================================================================================================*/
#define TT15_ID                 "TC-TARGET-015"
#define TT15_LEAD_NS            2000000000ULL  /* requested target: 2 s after the PHC sample */
#define TT15_OBSERVE_AFTER_NS   1000000000ULL  /* diagnostic observation bound after the target; not an accuracy requirement */
#define TT15_BASELINE_MS        1000u          /* baseline window before the request: events here are not caused by it */
#define TT15_RUNNING_GAP_MS     50u            /* two PHC samples this far apart must differ (PHC advancing) */
#define TT15_POLL_MS            5u
#define TT15_HOST_DEADLINE_MS   8000u          /* hard host bound of the wait loop */
#define TT15_RING_EVENTS        1024u          /* the driver's subscription ring_count */

static int tt15_nonpass = 0;   /* FAIL, INCONCLUSIVE, BLOCKED, CLEANUP_FAILED, INCONCLUSIVE_RESTORATION lines */

typedef struct {
    LONG nt;
    BOOL ok;
    AVB_CROSS_TIMESTAMP_V2_REQUEST x;
} TT15_PHC;

static BOOL tt15_phc(HANDLE h, UINT32 index, TT15_PHC *p)
{
    DWORD br = 0;
    ZeroMemory(p, sizeof(*p));
    p->x.struct_version = AVB_XTSTAMP_V2_VERSION;
    p->x.struct_size = (avb_u32)sizeof(p->x);
    p->x.adapter_index = index;
    p->nt = avb_ioctl_ntstatus(h, IOCTL_AVB_PHC_CROSSTIMESTAMP_V2, &p->x, sizeof(p->x), &p->x, sizeof(p->x), &br);
    p->ok = (p->nt >= 0 && p->x.valid == 1u && p->x.avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED);
    return p->ok;
}

static void tt15_print_phc(const char *what, const TT15_PHC *p)
{
    printf("    PHC %s: nt=0x%08lX valid=%u source=%u rc=%d phc_time_ns=%llu%s\n", what, (unsigned long)p->nt,
           p->x.valid, p->x.avb_phc_source, p->x.get_systime_rc, (unsigned long long)p->x.phc_time_ns,
           p->ok ? "" : " (not a hardware-derived sample)");
}

static LONG tt15_snapshot(HANDLE h, AVB_HW_SNAPSHOT_REQUEST_V2 *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    s->version = AVB_HW_SNAPSHOT_VERSION_2;
    return avb_ioctl_ntstatus(h, IOCTL_AVB_QUERY_HW_SNAPSHOT, s, sizeof(*s), s, sizeof(*s), &br);
}

static BOOL tt15_snapshot_ok(LONG nt, const AVB_HW_SNAPSHOT_REQUEST_V2 *s, avb_u64 luid)
{
    return nt >= 0 && s->status == 0u && s->count == AVB_HWREG_COUNT_V2 && s->net_luid == luid;
}

/* TRUE and the raw value when the entry for id is VALID */
static BOOL tt15_reg(const AVB_HW_SNAPSHOT_REQUEST_V2 *s, UINT32 id, UINT32 *value)
{
    const AVB_HWREG_ENTRY *e = &s->entries[id - 1u];
    if (e->id != id || e->state != AVB_HWREG_STATE_VALID) return FALSE;
    *value = e->value;
    return TRUE;
}

static void tt15_print_snapshot(const char *what, LONG nt, const AVB_HW_SNAPSHOT_REQUEST_V2 *s)
{
    static const struct { UINT32 id; const char *name; } regs[] = {
        { AVB_HWREG_TSAUXC, "TSAUXC" }, { AVB_HWREG_TRGTTIML0, "TRGTTIML0" }, { AVB_HWREG_TRGTTIMH0, "TRGTTIMH0" },
        { AVB_HWREG_TRGTTIML1, "TRGTTIML1" }, { AVB_HWREG_TRGTTIMH1, "TRGTTIMH1" },
        { AVB_HWREG_SYSTIML, "SYSTIML" }, { AVB_HWREG_SYSTIMH, "SYSTIMH" } };
    UINT32 i;
    printf("    snapshot %s: nt=0x%08lX status=0x%08X count=%u did=0x%04X luid=0x%016llX (raw register values, not decoded)\n",
           what, (unsigned long)nt, s->status, s->count, s->device_id, (unsigned long long)s->net_luid);
    if (nt < 0 || s->count != AVB_HWREG_COUNT_V2) return;
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        const AVB_HWREG_ENTRY *e = &s->entries[regs[i].id - 1u];
        printf("      %-9s id=%-2u offset=0x%05X value=0x%08X state=%u\n", regs[i].name, e->id, e->offset, e->value, e->state);
    }
}

/* Validates the mapped ring against the ABI before any use; FALSE on any mismatch or access fault */
static BOOL tt15_ring_valid(const AVB_TS_RING_MAP_REQUEST *map, char *why, size_t whySize)
{
    const AVB_TIMESTAMP_RING_HEADER *hdr = (const AVB_TIMESTAMP_RING_HEADER *)(UINT_PTR)map->shm_token;
    BOOL ok = FALSE;
    if (map->shm_token == 0u) { sprintf_s(why, whySize, "shm_token is 0"); return FALSE; }
    __try {
        UINT32 count = hdr->count;
        if (count == 0u || (count & (count - 1u)) != 0u)        sprintf_s(why, whySize, "count %u is not a power of two", count);
        else if (hdr->mask != count - 1u)                        sprintf_s(why, whySize, "mask 0x%X != count-1", hdr->mask);
        else if ((UINT64)map->length < sizeof(*hdr) + (UINT64)count * sizeof(AVB_TIMESTAMP_EVENT))
                                                                 sprintf_s(why, whySize, "length %u below header + %u events", map->length, count);
        else if (hdr->event_mask != TS_EVENT_TARGET_TIME)        sprintf_s(why, whySize, "event_mask 0x%X", hdr->event_mask);
        else ok = TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sprintf_s(why, whySize, "access fault reading the ring header at 0x%llX", (unsigned long long)map->shm_token);
        ok = FALSE;
    }
    return ok;
}

/* Consumes the ring up to ONE producer snapshot, at most the ring size (bounded).  Counts new events; the first timer-0
 * TARGET_TIME event is kept with evLo as the lower bound of its posting interval.  TRUE when that first event was
 * found in this call.  Used by the wait loop and by the terminal reconciliation, so both apply the same rules. */
static BOOL tt15_drain(AVB_TIMESTAMP_RING_HEADER *hdr, const AVB_TIMESTAMP_EVENT *events, UINT32 *cons,
                       UINT32 *seen, UINT32 *candidates, UINT32 *others, AVB_TIMESTAMP_EVENT *hit, UINT64 *lo, UINT64 evLo)
{
    UINT32 prod, n = 0;
    BOOL firstHere = FALSE;
    MemoryBarrier();
    prod = hdr->producer_index;
    MemoryBarrier();
    while (*cons != prod && n < hdr->count) {
        AVB_TIMESTAMP_EVENT ev = events[*cons & hdr->mask];
        *cons = (*cons + 1u) & hdr->mask;
        hdr->consumer_index = *cons;
        (*seen)++;
        n++;
        printf("    event: seq=%u type=0x%X queue=%u trigger_source=%u stored_timestamp=0x%016llX\n", ev.sequence_num,
               ev.event_type, ev.queue, ev.trigger_source, (unsigned long long)ev.timestamp_ns);
        if (ev.event_type == TS_EVENT_TARGET_TIME && ev.queue == 0u && ev.trigger_source == 0u) {
            (*candidates)++;
            if (*candidates == 1u) { *hit = ev; *lo = evLo; firstHere = TRUE; }
        } else {
            (*others)++;
        }
    }
    return firstHere;
}

static void tt15_line(const char *verdict, const char *tag, const char *reason)
{
    printf("[%s] %s%s%s\n", verdict, tag, reason ? ": " : "", reason ? reason : "");
    if (strcmp(verdict, "PASS") == 0) tests_passed++;
    else { tests_failed++; tt15_nonpass++; }
}

static void tt15_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    char tag[96], why[256];
    HANDLE h;
    AVB_OPEN_BY_LUID_REQUEST bind;
    AVB_HW_SNAPSHOT_REQUEST_V2 pre, written, after;
    LONG nt, ntPre, ntWritten = -1, ntAfter;
    TT15_PHC p0, p1;
    AVB_TS_SUBSCRIBE_REQUEST sub;
    AVB_TS_RING_MAP_REQUEST map;
    AVB_TARGET_TIME_REQUEST tgt;
    AVB_TIMESTAMP_RING_HEADER *hdr = NULL;
    AVB_TIMESTAMP_EVENT *events = NULL;
    DWORD br = 0;
    BOOL subscribed = FALSE, setSent = FALSE, haveWritten = FALSE;
    UINT64 requested = 0;
    UINT32 wL = 0, wH = 0;

    sprintf_s(tag, sizeof(tag), "%s/adapter:%u did=0x%04X", TT15_ID, index, id->device_id);
    printf("\n--- %s vid=0x%04X luid=0x%016llX ifIndex=%u capabilities=0x%08X (reported only; not used for applicability)\n",
           tag, id->vendor_id, (unsigned long long)id->net_luid, id->if_index, id->capabilities);

    h = CreateFileA("\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        sprintf_s(why, sizeof(why), "setup: open failed (Win32 %lu); nothing sent", GetLastError());
        tt15_line("BLOCKED", tag, why);
        return;
    }
    ZeroMemory(&bind, sizeof(bind));
    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = id->net_luid;
    nt = avb_ioctl_ntstatus(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &br);
    printf("    OPEN_ADAPTER_BY_LUID: nt=0x%08lX status=0x%08X matches=%u\n", (unsigned long)nt, bind.status, bind.matches);
    if (nt < 0 || bind.status != 0u || bind.matches != 1u) {
        tt15_line("BLOCKED", tag, "setup: per-handle binding by NET_LUID failed; nothing sent");
        CloseHandle(h);
        return;
    }

    /* ---- characterization, read-only ---- */
    ntPre = tt15_snapshot(h, &pre);
    tt15_print_snapshot("pre-test", ntPre, &pre);
    if (!tt15_snapshot_ok(ntPre, &pre, id->net_luid)) {
        tt15_line("BLOCKED", tag, "setup: no pre-test snapshot of this adapter (restoration could not be judged); nothing sent");
        CloseHandle(h);
        return;
    }
    tt15_phc(h, index, &p0);
    tt15_print_phc("sample 1", &p0);
    Sleep(TT15_RUNNING_GAP_MS);
    tt15_phc(h, index, &p1);
    tt15_print_phc("sample 2", &p1);
    if (!p0.ok || !p1.ok) {
        tt15_line("BLOCKED", tag, "setup: no hardware-derived PHC sample (software fallback does not count); nothing sent");
        CloseHandle(h);
        return;
    }
    if (p1.x.phc_time_ns <= p0.x.phc_time_ns) {
        tt15_line("BLOCKED", tag, "setup: PHC not advancing across the two samples; prerequisite not established by this step; nothing sent");
        CloseHandle(h);
        return;
    }

    /* ---- event path: subscribe and map ---- */
    ZeroMemory(&sub, sizeof(sub));
    sub.types_mask = TS_EVENT_TARGET_TIME;
    sub.vlan = 0xFFFF;   /* no VLAN filter */
    sub.pcp = 0xFF;      /* no PCP filter */
    nt = avb_ioctl_ntstatus(h, IOCTL_AVB_TS_SUBSCRIBE, &sub, sizeof(sub), &sub, sizeof(sub), &br);
    printf("    TS_SUBSCRIBE: nt=0x%08lX status=0x%08X ring_id=%u\n", (unsigned long)nt, sub.status, sub.ring_id);
    if (nt < 0 || sub.status != 0u || sub.ring_id == 0u) {
        tt15_line("INCONCLUSIVE", tag, "measurement chain: TS_SUBSCRIBE failed; SET_TARGET_TIME not sent");
        CloseHandle(h);
        return;
    }
    subscribed = TRUE;
    ZeroMemory(&map, sizeof(map));
    map.ring_id = sub.ring_id;
    map.length = (avb_u32)(sizeof(AVB_TIMESTAMP_RING_HEADER) + TT15_RING_EVENTS * sizeof(AVB_TIMESTAMP_EVENT));
    nt = avb_ioctl_ntstatus(h, IOCTL_AVB_TS_RING_MAP, &map, sizeof(map), &map, sizeof(map), &br);
    printf("    TS_RING_MAP: nt=0x%08lX status=0x%08X length=%u shm_token=0x%llX "
           "(used as a user address, as the driver returns it; the ABI comment calls it an opaque token / HANDLE)\n",
           (unsigned long)nt, map.status, map.length, (unsigned long long)map.shm_token);
    if (nt < 0 || map.status != 0u || !tt15_ring_valid(&map, why, sizeof(why))) {
        char r[320];
        sprintf_s(r, sizeof(r), "measurement chain: ring not usable (%s); SET_TARGET_TIME not sent",
                  (nt < 0 || map.status != 0u) ? "TS_RING_MAP failed" : why);
        tt15_line("INCONCLUSIVE", tag, r);
        goto cleanup;
    }
    hdr = (AVB_TIMESTAMP_RING_HEADER *)(UINT_PTR)map.shm_token;
    events = (AVB_TIMESTAMP_EVENT *)(hdr + 1);
    printf("    ring: count=%u producer=%u consumer=%u total_events=%llu overflow=%u\n", hdr->count, hdr->producer_index,
           hdr->consumer_index, (unsigned long long)hdr->total_events, hdr->overflow_count);

    /* ---- the operation: timer 0, contract nanoseconds (after a baseline window) ---- */
    {
        UINT32 cons = hdr->producer_index, seen = 0, candidates = 0, others = 0, baselineEvents = 0;
        UINT32 overflowStart = hdr->overflow_count;
        BOOL phcGap = FALSE, ringLoss = FALSE, byRegs = FALSE, byPhc = FALSE, pastWindow = FALSE;
        UINT64 lo = 0, hi = 0, prevA = 0, lastPhc = 0;
        AVB_TIMESTAMP_EVENT hit;
        DWORD t0;
        const char *fv = NULL, *fr = NULL;
        ZeroMemory(&hit, sizeof(hit));
        hdr->consumer_index = cons;

        /* Baseline window BEFORE the request: a TARGET_TIME event here was not caused by this test, so any event of the
         * window that follows could have the same other cause - attribution is then ambiguous. */
        t0 = GetTickCount();
        while (GetTickCount() - t0 < TT15_BASELINE_MS) {
            UINT32 prod = hdr->producer_index;
            MemoryBarrier();
            while (cons != prod) {
                AVB_TIMESTAMP_EVENT ev = events[cons & hdr->mask];
                cons = (cons + 1u) & hdr->mask;
                hdr->consumer_index = cons;
                baselineEvents++;
                printf("    baseline event (before the request): seq=%u type=0x%X queue=%u trigger_source=%u stored_timestamp=0x%016llX\n",
                       ev.sequence_num, ev.event_type, ev.queue, ev.trigger_source, (unsigned long long)ev.timestamp_ns);
            }
            Sleep(TT15_POLL_MS);
        }
        printf("    baseline window %u ms before the request: %u event(s)\n", TT15_BASELINE_MS, baselineEvents);
        if (!tt15_phc(h, index, &p0)) {
            tt15_print_phc("before target", &p0);
            tt15_line("BLOCKED", tag, "setup: no hardware-derived PHC sample for the target; SET_TARGET_TIME not sent");
            goto cleanup;
        }
        requested = p0.x.phc_time_ns + TT15_LEAD_NS;
        printf("    requested target (contract, ns): %llu = PHC %llu + %llu  [= %llu s + %llu ns; 64-bit halves high=0x%08X low=0x%08X]\n",
               (unsigned long long)requested, (unsigned long long)p0.x.phc_time_ns, (unsigned long long)TT15_LEAD_NS,
               (unsigned long long)(requested / 1000000000ULL), (unsigned long long)(requested % 1000000000ULL),
               (unsigned)(UINT32)(requested >> 32), (unsigned)(UINT32)requested);
        prevA = lastPhc = p0.x.phc_time_ns;

        ZeroMemory(&tgt, sizeof(tgt));
        tgt.timer_index = 0;
        tgt.target_time = requested;
        tgt.enable_interrupt = 1;
        tgt.enable_sdp_output = 0;
        tgt.sdp_mode = 0;
        nt = avb_ioctl_ntstatus(h, IOCTL_AVB_SET_TARGET_TIME, &tgt, sizeof(tgt), &tgt, sizeof(tgt), &br);
        setSent = TRUE;   /* the driver may have written registers even when it then failed */
        printf("    SET_TARGET_TIME(timer 0, enable_interrupt 1): nt=0x%08lX status=0x%08X previous_target=0x%08X "
               "(32-bit ABI field)\n", (unsigned long)nt, tgt.status, tgt.previous_target);
        if (nt < 0) {
            AVB_VERDICT v = avb_verdict_from_status((NTSTATUS)nt, AVB_CAP_UNKNOWN, 0, 1);
            sprintf_s(why, sizeof(why), "SET_TARGET_TIME refused nt=0x%08lX (no per-operation target-time capability contract exists)",
                      (unsigned long)nt);
            tt15_line(avb_verdict_tag(v), tag, why);
            goto cleanup;
        }
        ntWritten = tt15_snapshot(h, &written);
        tt15_print_snapshot("after SET_TARGET_TIME", ntWritten, &written);
        haveWritten = tt15_snapshot_ok(ntWritten, &written, id->net_luid) &&
                      tt15_reg(&written, AVB_HWREG_TRGTTIML0, &wL) && tt15_reg(&written, AVB_HWREG_TRGTTIMH0, &wH);
        if (haveWritten)
            printf("    target registers as written: TRGTTIMH0=0x%08X TRGTTIML0=0x%08X (read-back only; not functional evidence)\n", wH, wL);

        /* Every new event until the PHC is past the diagnostic window (not only the first), so that several candidates
         * or other events stay visible.  Bracket of an event first seen at poll k: [PHC before the ring check of poll k-1,
         * PHC after the ring check of poll k] - it bounds the driver's posting, not the hardware trigger. */
        t0 = GetTickCount();
        while (GetTickCount() - t0 < TT15_HOST_DEADLINE_MS) {
            TT15_PHC a, b;
            UINT64 evLo = prevA;
            BOOL aok = tt15_phc(h, index, &a);
            BOOL firstHere = tt15_drain(hdr, events, &cons, &seen, &candidates, &others, &hit, &lo, evLo);
            if (tt15_phc(h, index, &b)) lastPhc = b.x.phc_time_ns; else phcGap = TRUE;
            if (firstHere) hi = lastPhc;
            if (aok) prevA = a.x.phc_time_ns; else phcGap = TRUE;
            /* The window boundary counts only when THIS iteration's terminal PHC sample succeeded and crossed it. */
            if (lastPhc > requested + TT15_OBSERVE_AFTER_NS) { pastWindow = TRUE; break; }
            Sleep(TT15_POLL_MS);
        }
        /* Terminal reconciliation (PR #514 review): the ring check above precedes the terminal PHC sample, so an event
         * published while that sample was taken would stay unread.  One more producer read AFTER the terminal sample
         * (bounded: one snapshot, at most the ring size) closes the observation window before absence is judged; the
         * loss check follows it.  An event found here is reported like any other (posting interval up to a PHC sample
         * taken after this read); without a successful sample there the interval is open and the result INCONCLUSIVE. */
        {
            TT15_PHC c;
            UINT32 seenBefore = seen;
            BOOL firstHere = tt15_drain(hdr, events, &cons, &seen, &candidates, &others, &hit, &lo, prevA);
            BOOL cok = tt15_phc(h, index, &c);
            if (firstHere) { if (cok) hi = c.x.phc_time_ns; else { hi = lastPhc; phcGap = TRUE; } }
            printf("    terminal ring reconciliation after the terminal PHC sample: %u further event(s); window boundary %s\n",
                   seen - seenBefore, pastWindow ? "established by that sample" : "NOT established (host bound or sample gap)");
        }
        ringLoss = (hdr->overflow_count != overflowStart);
        printf("    ring after wait: producer=%u total_events=%llu overflow=%u (start %u); new events=%u, timer-0 candidates=%u, others=%u; "
               "PHC sample gaps=%s; last PHC %llu ns\n", hdr->producer_index, (unsigned long long)hdr->total_events,
               hdr->overflow_count, overflowStart, seen, candidates, others, phcGap ? "yes" : "no", (unsigned long long)lastPhc);

        /* ---- assessment 1: event attribution (separate from the functional verdict) ----
         * Two driver posting paths exist; each leaves a timestamp PATTERN in the event: the target registers as read
         * (H<<32|L), or a PHC value at detection.  A pattern names the posting path, it is not a request identifier.
         * This test cannot exclude an independent source of timer-0 target-time events (for example an already active
         * clock output on the same target time), so a present event is never attributed to the request. */
        if (candidates > 0u) {
            byRegs = haveWritten && hit.timestamp_ns == (((UINT64)wH << 32) | wL);
            byPhc  = hit.timestamp_ns >= lo && hit.timestamp_ns <= hi;
            printf("    first timer-0 event: posting interval PHC [%llu, %llu] ns; requested target %llu ns; interval - target = "
                   "[%lld, %lld] ns; timestamp pattern: %s\n",
                   (unsigned long long)lo, (unsigned long long)hi, (unsigned long long)requested,
                   (long long)(lo - requested), (long long)(hi - requested),
                   byRegs ? "target registers as read (register posting path)" :
                   byPhc ? "PHC inside the posting interval (detection posting path)" : "neither posting-path pattern");
        }
        printf("    assessment ATTRIBUTION: %s (baseline events %u, timer-0 events %u, other events %u, ring overflow %s)\n",
               candidates == 0u ? "none (no timer-0 event)"
                                : "INCONCLUSIVE - an independent timer-0 event source is not excluded by this test",
               baselineEvents, candidates, others, ringLoss ? "yes" : "no");

        /* ---- assessment 2: function ----
         * The required event missing after the PHC passed the target is judged (FAIL).  A present event is reported with
         * its interval, but neither a PASS nor a request-specific timing FAIL follows from it while an independent source
         * is not excluded: INCONCLUSIVE.  PASS is therefore not reachable in this version of the case. */
        if (phcGap || ringLoss) {
            fv = "INCONCLUSIVE"; fr = phcGap ? "measurement gap: PHC sample(s) missing in the wait loop"
                                             : "measurement gap: ring overflow during the wait";
        } else if (candidates == 0u) {
            if (pastWindow) {
                fv = "FAIL"; fr = "required timer-0 TARGET_TIME event missing although the PHC passed the requested target by the diagnostic window "
                                  "(ring reconciled after the terminal PHC sample)";
            } else {
                fv = "INCONCLUSIVE"; fr = "no event, but the window boundary was not established by a PHC sample before the terminal ring read";
            }
        } else {
            sprintf_s(why, sizeof(why), "timer-0 event present (first posting interval %s the requested target); request-specific timing "
                      "not judged: an independent event source is not excluded", hi < requested ? "entirely BEFORE" :
                      lo >= requested ? "entirely after" : "overlapping");
            fv = "INCONCLUSIVE"; fr = why;
        }
        printf("    assessment FUNCTION: %s\n", fv);
        tt15_line(fv, tag, fr);
    }

cleanup:
    if (subscribed) {
        AVB_TS_UNSUBSCRIBE_REQUEST un;
        ZeroMemory(&un, sizeof(un));
        un.ring_id = sub.ring_id;
        nt = avb_ioctl_ntstatus(h, IOCTL_AVB_TS_UNSUBSCRIBE, &un, sizeof(un), &un, sizeof(un), &br);
        printf("    cleanup TS_UNSUBSCRIBE: nt=0x%08lX status=0x%08X\n", (unsigned long)nt, un.status);
        if (nt < 0 || un.status != 0u) {
            tt15_line("CLEANUP_FAILED", tag, "subscription not released");
        }
    }
    if (setSent) {
        static const struct { UINT32 id; const char *name; } obs[] = {
            { AVB_HWREG_TSAUXC, "TSAUXC" }, { AVB_HWREG_TRGTTIML0, "TRGTTIML0" }, { AVB_HWREG_TRGTTIMH0, "TRGTTIMH0" } };
        UINT32 i, changed = 0, unread = 0;
        ntAfter = tt15_snapshot(h, &after);
        tt15_print_snapshot("after cleanup", ntAfter, &after);
        for (i = 0; i < sizeof(obs) / sizeof(obs[0]); i++) {
            UINT32 vb = 0, va = 0;
            BOOL okb = tt15_reg(&pre, obs[i].id, &vb);
            BOOL oka = tt15_snapshot_ok(ntAfter, &after, id->net_luid) && tt15_reg(&after, obs[i].id, &va);
            if (!okb || !oka) { unread++; printf("    restoration %-9s NOT_OBSERVED\n", obs[i].name); }
            else if (va != vb) { changed++; printf("    restoration %-9s NOT_RESTORED 0x%08X -> 0x%08X\n", obs[i].name, vb, va); }
            else printf("    restoration %-9s unchanged 0x%08X\n", obs[i].name, va);
        }
        printf("    restoration NOT_VERIFIED: interrupt mask/enable and clock-output state the operation may change but no IOCTL "
               "reads back; the driver's target-time polling (no query exists); no IOCTL restores target-time state\n");
        printf("    assessment RESTORATION: %s\n", changed > 0 ? "CLEANUP_FAILED" : "INCONCLUSIVE_RESTORATION");
        if (changed > 0) tt15_line("CLEANUP_FAILED", tag, "observable target-time state differs from the pre-test snapshot (no restore path)");
        else tt15_line("INCONCLUSIVE_RESTORATION", tag, unread > 0 ? "observable state partly unreadable; unobservable state NOT_VERIFIED"
                                                                    : "observable state unchanged; unobservable state NOT_VERIFIED");
    } else {
        printf("    assessment RESTORATION: not required (SET_TARGET_TIME not sent; no target-time mutation by this test)\n");
    }
    CloseHandle(h);
}

static int tt15_run(void)
{
    HANDLE h;
    UINT32 index, count = 1;
    printf("==============================================\n");
    printf("%s (#299): timer-0 target-time event, every enumerated adapter\n", TT15_ID);
    printf("Characterization step: timer 1 is not programmed; register values are raw\n");
    printf("==============================================\n");
    h = CreateFileA("\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] %s: open failed (Win32 %lu)\n", TT15_ID, GetLastError());
        return 1;
    }
    for (index = 0; index < count; index++) {
        AVB_ADAPTER_IDENTITY_REQUEST id;
        DWORD br = 0;
        LONG nt;
        ZeroMemory(&id, sizeof(id));
        id.version = AVB_ADAPTER_IDENTITY_VERSION;
        id.index = index;
        nt = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, &id, sizeof(id), &id, sizeof(id), &br);
        if (nt < 0 || id.status != 0u) {
            printf("[BLOCKED] %s/adapter:%u: GET_ADAPTER_IDENTITY nt=0x%08lX status=0x%08X\n", TT15_ID, index,
                   (unsigned long)nt, id.status);
            tests_failed++; tt15_nonpass++;
            if (index == 0) break;   /* the adapter count is unknown */
            continue;
        }
        count = id.count;
        tt15_adapter(index, &id);
    }
    CloseHandle(h);
    if (count == 0) { printf("[BLOCKED] %s: no adapter enumerated\n", TT15_ID); tt15_nonpass++; }
    printf("\n%s SUMMARY: adapters=%u PASS=%d non-pass=%d\n", TT15_ID, count, tests_passed, tt15_nonpass);
    return (tt15_nonpass > 0 || tests_passed == 0) ? 1 : 0;
}

/* =====================================================================================================================
 * TC-TARGET-V2-001 (#515 increment 1; test plan #299, requirement #7): versioned target-time QUERY, parameter validation and
 * access rights on every enumerated adapter, each bound by NET_LUID. Selected only by --case TC-TARGET-V2-001.
 *
 * Request sequence per adapter (no valid ARM is ever sent; every ARM request is invalid under the #515 contract):
 *   1. read/write handle, OPEN_ADAPTER_BY_LUID; QUERY_HW_SNAPSHOT v2 "pre-1", 100 ms, "pre-2" (movement control, before
 *      any V2 request);
 *   2. QUERY (0x00176034) on that handle: Q-T0, Q-T1 (valid, timer 0/1); Q-OUTIGN (valid, non-zero output-only fields,
 *      ignored per D-4); Q-BIG (valid, 56-byte buffers, bytes 48-55 must stay untouched); V-* (one invalid input each:
 *      input/output length 47, version 1, size 47/56, operation 0 or ARM, timer 2, flags 1, reserved0 1, reserved1 1,
 *      target_time_ns 1);
 *   3. ARM (0x0017A038) on that handle, invalid only: A-VER0 (version 0), A-OPQ (operation QUERY), A-IN47 (input 47),
 *      A-FLAGS (flags 1, D-3). Each is invalid in its header or length, so its status (BUFFER_TOO_SMALL or
 *      INVALID_PARAMETER, #515 comment 7130364452) does not depend on adapter, backend or PHC state: header validation
 *      precedes those phases (PR #516 re-review of 409f353). Every ARM also carries target 0 ns (TV2_ARM_PAST_NS) as a
 *      second invalid field, so the status alone does not show which rule rejected the request.
 *      NOT tested here: D-5 (target not after the hardware-derived PHC, more than 1 hour ahead). It stays required and
 *      belongs to the later semantic ARM test, which must first establish a ready adapter, the backend and a
 *      hardware-derived PHC;
 *   4. read-only handle bound to the same LUID: RO-Q (valid QUERY: read access suffices), RO-A (ARM version 0): an access
 *      probe answered by the I/O manager before dispatch - never evidence of a V2 implementation;
 *   5. QUERY_HW_SNAPSHOT v2 "post" on the read/write handle.
 *
 * Verdicts (docs/testing/test-verdict-contract.md via avb_verdict_from_status): each request has its own PROTOCOL line
 * (contract status and Information, output-field rules). A successful QUERY of timer 0/1 additionally gets a FUNCTION line
 * that stays INCONCLUSIVE here: a generic test decodes no device representation, so a VALID value is not checked against
 * independent device evidence (raw target words are printed for evaluation outside the test). States 2/3/4 must carry
 * result_target_ns 0; reporting them correctly is a protocol result, never a functional read of a valid target.
 * OBSERVATION: TSAUXC and the four target words (snapshot ids 2, 10-13) per class - unchanged, moving before any request
 * (autonomous, not attributable), stable then changed (not attributed), or not observed (entry state reported). Equal
 * snapshots are an observation only, not proof that no MMIO write occurred. Pre-test values are the device's current state,
 * possibly residue of earlier runs (e.g. TC-TARGET-015, #513): reported, never judged or repaired here. No restore path
 * exists; RESTORATION is assessed from these results, never assumed.
 * ===================================================================================================================*/
#define TV2_ID              "TC-TARGET-V2-001"
#define TV2_MOVE_GAP_MS     100u
#define TV2_SENTINEL_NS     0xA5A5A5A5A5A5A5A5ULL  /* above any decodable target ((2^32-1) s + 999999999 ns): never a valid result */
#define TV2_SENTINEL_STATE  0xFFFFFFFFu
#define TV2_PAD             0xCC
#define TV2_SIZE            48u
/* ARM target for the invalid-ARM cases: 0 ns. target_time_ns and the hardware PHC are both unsigned 64-bit nanoseconds, so
 * 0 <= PHC for every PHC value (including a PHC of 0 after a reset), and #515 D-5 rejects any target <= the hardware-derived
 * PHC. A non-zero value such as 1 ns would be a valid future target while the PHC is still below it. Rejecting these
 * requests before any write is required of a conforming driver; it is no guarantee about a defective implementation. */
#define TV2_ARM_PAST_NS     0ULL

typedef enum { TV2_QUERY_OK, TV2_REJECT, TV2_ACCESS_DENIED } TV2_KIND;

typedef struct {
    const char *name;
    DWORD code;
    BOOL readOnly;
    TV2_KIND kind;
    LONG expect;                       /* TV2_REJECT / TV2_ACCESS_DENIED: the contract status */
    avb_u32 version, size, operation, timer, flags, reserved0, reserved1;
    avb_u64 target;
    BOOL outputSentinel;               /* non-zero output-only fields in the input (D-4: ignored) */
    DWORD inLen, outLen;               /* actual buffer lengths */
    const char *what;
} TV2_CASE;

typedef struct {
    AVB_TARGET_TIME_V2_REQUEST r;
    unsigned char pad[8];
} TV2_BUF;

#define TV2_Q   IOCTL_AVB_TARGET_TIME_V2_QUERY
#define TV2_A   IOCTL_AVB_TARGET_TIME_V2_ARM
#define TV2_V   AVB_TARGET_TIME_V2_VERSION
#define TV2_OQ  AVB_TT_V2_OP_QUERY
#define TV2_OA  AVB_TT_V2_OP_ARM

static const TV2_CASE tv2_cases[] = {
    /* name       code   RO     kind               expect                       ver    size      op      tmr fl r0 r1 target sentinel in  out */
    { "Q-T0",     TV2_Q, FALSE, TV2_QUERY_OK,      0,                           TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 48, "valid QUERY, timer 0" },
    { "Q-T1",     TV2_Q, FALSE, TV2_QUERY_OK,      0,                           TV2_V, TV2_SIZE, TV2_OQ, 1, 0, 0, 0, 0, FALSE, 48, 48, "valid QUERY, timer 1" },
    { "Q-OUTIGN", TV2_Q, FALSE, TV2_QUERY_OK,      0,                           TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, TRUE,  48, 48, "valid QUERY, non-zero output-only fields (ignored, D-4)" },
    { "Q-BIG",    TV2_Q, FALSE, TV2_QUERY_OK,      0,                           TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 56, 56, "valid QUERY, 56-byte buffers (only 48 bytes written)" },
    { "V-IN47",   TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_BUFFER_TOO_SMALL,      TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 47, 48, "input buffer 47" },
    { "V-OUT47",  TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_BUFFER_TOO_SMALL,      TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 47, "output buffer 47" },
    { "V-VER1",   TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     1,     TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 48, "version 1" },
    { "V-SIZE47", TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, 47,       TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 48, "size 47" },
    { "V-SIZE56", TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, 56,       TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 48, "size 56" },
    { "V-OP0",    TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, 0,      0, 0, 0, 0, 0, FALSE, 48, 48, "operation 0 (unknown)" },
    { "V-OPARM",  TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OA, 0, 0, 0, 0, 0, FALSE, 48, 48, "operation ARM on the QUERY code" },
    { "V-TIMER2", TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OQ, 2, 0, 0, 0, 0, FALSE, 48, 48, "timer_index 2" },
    { "V-FLAGS1", TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OQ, 0, 1, 0, 0, 0, FALSE, 48, 48, "flags 1" },
    { "V-RES0",   TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 1, 0, 0, FALSE, 48, 48, "reserved0 1" },
    { "V-RES1",   TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 1, 0, FALSE, 48, 48, "reserved1 1" },
    { "V-TGT1",   TV2_Q, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 1, FALSE, 48, 48, "QUERY with target_time_ns 1" },
    { "A-VER0",   TV2_A, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     0,     TV2_SIZE, TV2_OA, 0, 0, 0, 0, TV2_ARM_PAST_NS, FALSE, 48, 48, "invalid ARM: version 0" },
    { "A-OPQ",    TV2_A, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 48, "invalid ARM: operation QUERY on the ARM code" },
    { "A-IN47",   TV2_A, FALSE, TV2_REJECT, (LONG)STATUS_BUFFER_TOO_SMALL,      TV2_V, TV2_SIZE, TV2_OA, 0, 0, 0, 0, TV2_ARM_PAST_NS, FALSE, 47, 48, "invalid ARM: input buffer 47" },
    { "A-FLAGS",  TV2_A, FALSE, TV2_REJECT, (LONG)STATUS_INVALID_PARAMETER,     TV2_V, TV2_SIZE, TV2_OA, 0, 1, 0, 0, TV2_ARM_PAST_NS, FALSE, 48, 48, "invalid ARM: flags 1 (D-3)" },
    { "RO-Q",     TV2_Q, TRUE,  TV2_QUERY_OK,      0,                           TV2_V, TV2_SIZE, TV2_OQ, 0, 0, 0, 0, 0, FALSE, 48, 48, "read-only handle: valid QUERY" },
    { "RO-A",     TV2_A, TRUE,  TV2_ACCESS_DENIED, (LONG)STATUS_ACCESS_DENIED,  0,     TV2_SIZE, TV2_OA, 0, 0, 0, 0, TV2_ARM_PAST_NS, FALSE, 48, 48, "read-only handle: ARM version 0 (access probe)" },
};

static int tv2_nonpass = 0;

static void tv2_line(const char *verdict, const char *tag, const char *check, const char *reason)
{
    printf("[%s] %s %s: %s\n", verdict, tag, check, reason);
    if (strcmp(verdict, "PASS") == 0) tests_passed++;
    else { tests_failed++; tv2_nonpass++; }
}

static HANDLE tv2_open_bound(DWORD access, avb_u64 luid, LONG *ntBind, DWORD *win32)
{
    AVB_OPEN_BY_LUID_REQUEST bind;
    DWORD br = 0;
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter", access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    *ntBind = AVB_NTSTATUS_UNAVAILABLE;
    *win32 = (h == INVALID_HANDLE_VALUE) ? GetLastError() : 0;
    if (h == INVALID_HANDLE_VALUE) return h;
    ZeroMemory(&bind, sizeof(bind));
    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = luid;
    *ntBind = avb_ioctl_ntstatus(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &br);
    printf("    %s handle: OPEN_ADAPTER_BY_LUID nt=0x%08lX status=0x%08X matches=%u\n",
           (access & GENERIC_WRITE) ? "read/write" : "read-only", (unsigned long)*ntBind, bind.status, bind.matches);
    if (*ntBind < 0 || bind.status != 0u || bind.matches != 1u) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    return h;
}

/* One request with its PROTOCOL line (and FUNCTION line for a successful timer-0/1 QUERY).
 * *armAccepted is set when an ARM request completed with a success status. */
static void tv2_request(HANDLE h, const TV2_CASE *c, const char *tag, BOOL *armAccepted)
{
    TV2_BUF b;
    DWORD info = 0;
    LONG nt;
    char why[400], check[32];
    const char *verdict;
    size_t i;
    BOOL padOk = TRUE, functionLine = FALSE;

    FillMemory(&b, sizeof(b), TV2_PAD);
    ZeroMemory(&b.r, sizeof(b.r));
    b.r.version = c->version;
    b.r.size = c->size;
    b.r.operation = c->operation;
    b.r.timer_index = c->timer;
    b.r.flags = c->flags;
    b.r.reserved0 = c->reserved0;
    b.r.reserved1 = c->reserved1;
    b.r.target_time_ns = c->target;
    if (c->outputSentinel) { b.r.result_target_ns = TV2_SENTINEL_NS; b.r.result_state = TV2_SENTINEL_STATE; }

    nt = avb_ioctl_ntstatus(h, c->code, &b, c->inLen, &b, c->outLen, &info);
    for (i = 0; i < sizeof(b.pad); i++) if (b.pad[i] != TV2_PAD) padOk = FALSE;
    printf("    %-8s code=0x%08lX in=%lu out=%lu -> nt=0x%08lX information=%lu | result_target_ns=%llu result_state=%u "
           "reserved1=%u bytes48-55 %s\n", c->name, (unsigned long)c->code, c->inLen, c->outLen, (unsigned long)nt, info,
           (unsigned long long)b.r.result_target_ns, b.r.result_state, b.r.reserved1, padOk ? "untouched" : "MODIFIED");
    if (c->code == TV2_A && nt >= 0) *armAccepted = TRUE;

    if (c->kind == TV2_ACCESS_DENIED) {
        if (nt == c->expect && info == 0) {
            verdict = "PASS";
            sprintf_s(why, sizeof(why), "%s: STATUS_ACCESS_DENIED, Information 0 (I/O manager access check before dispatch; "
                      "not evidence of a V2 implementation)", c->what);
        } else {
            verdict = "FAIL";
            sprintf_s(why, sizeof(why), "%s: expected STATUS_ACCESS_DENIED with Information 0; raw nt=0x%08lX information=%lu",
                      c->what, (unsigned long)nt, info);
        }
    } else if (c->kind == TV2_REJECT) {
        if (nt == c->expect && info == 0) {
            verdict = "PASS";
            sprintf_s(why, sizeof(why), "%s: contract status 0x%08lX, Information 0", c->what, (unsigned long)c->expect);
        } else {
            verdict = (nt == c->expect) ? "FAIL" : avb_verdict_tag(avb_verdict_from_status((NTSTATUS)nt, AVB_CAP_UNKNOWN, 0, 1));
            sprintf_s(why, sizeof(why), "%s: contract status 0x%08lX with Information 0; raw nt=0x%08lX information=%lu%s",
                      c->what, (unsigned long)c->expect, (unsigned long)nt, info,
                      nt >= 0 ? " (a success where the contract requires rejection)" : "");
        }
    } else {
        BOOL stateOk = (b.r.result_state >= AVB_TT_V2_STATE_VALID && b.r.result_state <= AVB_TT_V2_STATE_COHERENCE_UNVERIFIED);
        BOOL valueOk = (b.r.result_state == AVB_TT_V2_STATE_VALID || b.r.result_target_ns == 0);
        BOOL sentinelOk = (!c->outputSentinel || b.r.result_target_ns != TV2_SENTINEL_NS);
        BOOL payload = (info == TV2_SIZE && stateOk && valueOk && sentinelOk && b.r.reserved1 == 0u && padOk);
        verdict = avb_verdict_tag(avb_verdict_from_status((NTSTATUS)nt, AVB_CAP_UNKNOWN, payload, 1));
        if (nt >= 0) {
            sprintf_s(why, sizeof(why), "%s: success; Information %lu (48 required), result_state %u (1-4 required), "
                      "result_target_ns %s, reserved1 %u (0 required), bytes 48-55 %s%s", c->what, info, b.r.result_state,
                      !valueOk ? "non-zero with a non-VALID state (0 required)"
                               : (sentinelOk ? "consistent with the state" : "still the injected output-only value"),
                      b.r.reserved1, padOk ? "untouched" : "modified", payload ? "" : " -> payload invalid");
        } else {
            sprintf_s(why, sizeof(why), "%s: contract SUCCESS with Information 48; raw nt=0x%08lX information=%lu",
                      c->what, (unsigned long)nt, info);
        }
        functionLine = (strcmp(verdict, "PASS") == 0 && !c->readOnly && !c->outputSentinel && c->inLen == TV2_SIZE);
    }
    sprintf_s(check, sizeof(check), "%s PROTOCOL", c->name);
    tv2_line(verdict, tag, check, why);
    if (functionLine) {
        if (b.r.result_state == AVB_TT_V2_STATE_VALID) {
            sprintf_s(why, sizeof(why), "VALID result_target_ns=%llu; not compared with independent device-representation "
                      "evidence in this generic test (raw target words in the snapshots; evaluated outside)",
                      (unsigned long long)b.r.result_target_ns);
        } else {
            sprintf_s(why, sizeof(why), "state %u with value 0: no valid target was read; a correct protocol state is not a "
                      "functional read of a valid target", b.r.result_state);
        }
        sprintf_s(check, sizeof(check), "%s FUNCTION", c->name);
        tv2_line("INCONCLUSIVE", tag, check, why);
    }
}

static void tv2_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    static const struct { UINT32 id; const char *name; } obs[] = {
        { AVB_HWREG_TSAUXC, "TSAUXC" }, { AVB_HWREG_TRGTTIML0, "TRGTTIML0" }, { AVB_HWREG_TRGTTIMH0, "TRGTTIMH0" },
        { AVB_HWREG_TRGTTIML1, "TRGTTIML1" }, { AVB_HWREG_TRGTTIMH1, "TRGTTIMH1" } };
    char tag[96], why[400];
    HANDLE h, hro;
    AVB_HW_SNAPSHOT_REQUEST_V2 pre1, pre2, post;
    LONG nt1, nt2, ntPost, ntBind;
    DWORD win32 = 0;
    BOOL armAccepted = FALSE, postOk;
    UINT32 i, unchanged = 0, moving = 0, changed = 0, unobserved = 0;

    sprintf_s(tag, sizeof(tag), "%s/adapter:%u did=0x%04X", TV2_ID, index, id->device_id);
    printf("\n--- %s vid=0x%04X luid=0x%016llX ifIndex=%u capabilities=0x%08X (reported only; not used for applicability)\n",
           tag, id->vendor_id, (unsigned long long)id->net_luid, id->if_index, id->capabilities);

    h = tv2_open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid, &ntBind, &win32);
    if (h == INVALID_HANDLE_VALUE) {
        sprintf_s(why, sizeof(why), "read/write handle not opened and bound by NET_LUID (Win32 %lu, bind nt=0x%08lX); "
                  "nothing sent", win32, (unsigned long)ntBind);
        tv2_line("BLOCKED", tag, "SETUP", why);
        return;
    }
    nt1 = tt15_snapshot(h, &pre1);
    tt15_print_snapshot("pre-1", nt1, &pre1);
    Sleep(TV2_MOVE_GAP_MS);
    nt2 = tt15_snapshot(h, &pre2);
    tt15_print_snapshot("pre-2", nt2, &pre2);
    if (!tt15_snapshot_ok(nt1, &pre1, id->net_luid) || !tt15_snapshot_ok(nt2, &pre2, id->net_luid)) {
        tv2_line("BLOCKED", tag, "SETUP", "no pre-1/pre-2 snapshot of this adapter (accidental mutation could not be observed); "
                 "nothing sent");
        CloseHandle(h);
        return;
    }
    printf("    pre-test values are the device's current state (possibly residue of earlier runs, e.g. TC-TARGET-015, #513); "
           "not judged here\n");

    for (i = 0; i < sizeof(tv2_cases) / sizeof(tv2_cases[0]); i++) {
        if (!tv2_cases[i].readOnly) tv2_request(h, &tv2_cases[i], tag, &armAccepted);
    }

    hro = tv2_open_bound(GENERIC_READ, id->net_luid, &ntBind, &win32);
    if (hro == INVALID_HANDLE_VALUE) {
        sprintf_s(why, sizeof(why), "read-only handle not opened and bound by NET_LUID (Win32 %lu, bind nt=0x%08lX): missing "
                  "access-probe evidence; RO-Q and RO-A not sent", win32, (unsigned long)ntBind);
        tv2_line("BLOCKED", tag, "RO SETUP", why);
    } else {
        for (i = 0; i < sizeof(tv2_cases) / sizeof(tv2_cases[0]); i++) {
            if (tv2_cases[i].readOnly) tv2_request(hro, &tv2_cases[i], tag, &armAccepted);
        }
        CloseHandle(hro);
    }

    ntPost = tt15_snapshot(h, &post);
    tt15_print_snapshot("post", ntPost, &post);
    postOk = tt15_snapshot_ok(ntPost, &post, id->net_luid);
    for (i = 0; i < sizeof(obs) / sizeof(obs[0]); i++) {
        UINT32 a = 0, b = 0, c = 0;
        BOOL ra = tt15_reg(&pre1, obs[i].id, &a);
        BOOL rb = tt15_reg(&pre2, obs[i].id, &b);
        BOOL rc = postOk && tt15_reg(&post, obs[i].id, &c);
        if (!ra || !rb || !rc) {
            unobserved++;
            printf("    observation %-9s NOT_OBSERVED entry state pre-1=%u pre-2=%u post=%s\n", obs[i].name,
                   pre1.entries[obs[i].id - 1u].state, pre2.entries[obs[i].id - 1u].state,
                   !postOk ? "no snapshot" : (rc ? "1" : "not VALID"));
        } else if (a != b) {
            moving++;
            printf("    observation %-9s MOVING before any V2 request 0x%08X -> 0x%08X (autonomous; post 0x%08X not attributable)\n",
                   obs[i].name, a, b, c);
        } else if (b != c) {
            changed++;
            printf("    observation %-9s CHANGED 0x%08X -> 0x%08X (stable before the requests; cause not attributed)\n",
                   obs[i].name, b, c);
        } else {
            unchanged++;
            printf("    observation %-9s unchanged 0x%08X\n", obs[i].name, c);
        }
    }
    sprintf_s(why, sizeof(why), "ids 2,10-13: unchanged %u, moving before the requests %u, changed %u, not observed %u (state "
              "observation only; not proof that no MMIO write occurred; pending buffers, TSIM/EIMS/FREQOUT/SDP not observed)",
              unchanged, moving, changed, unobserved);
    if (changed > 0) tv2_line("CLEANUP_FAILED", tag, "OBSERVATION", why);
    else if (moving > 0 || unobserved > 0) tv2_line("INCONCLUSIVE_RESTORATION", tag, "OBSERVATION", why);
    else tv2_line("PASS", tag, "OBSERVATION", why);

    if (armAccepted) {
        printf("    assessment RESTORATION: an invalid ARM completed with a success status: programming may have occurred; no "
               "restore path exists (#513)\n");
        if (changed == 0) tv2_line("INCONCLUSIVE_RESTORATION", tag, "RESTORATION",
                                   "invalid ARM accepted; observed words unchanged, unobservable state NOT_VERIFIED");
    } else if (changed > 0) {
        printf("    assessment RESTORATION: observed state differs (see OBSERVATION); no restore path\n");
    } else {
        printf("    assessment RESTORATION: no ARM succeeded and no observed word changed: nothing observed to restore; "
               "unobservable state NOT_VERIFIED%s\n", (moving > 0 || unobserved > 0) ? "; observation incomplete" : "");
    }
    CloseHandle(h);
}

static int tv2_run(void)
{
    HANDLE h;
    UINT32 index, count = 1, enumerated = 0;
    printf("==============================================\n");
    printf("%s (#515; test plan #299; requirement #7): V2 target-time QUERY, validation, access\n", TV2_ID);
    printf("QUERY 0x%08lX, ARM 0x%08lX (invalid ARM requests only); every adapter bound by NET_LUID\n",
           (unsigned long)IOCTL_AVB_TARGET_TIME_V2_QUERY, (unsigned long)IOCTL_AVB_TARGET_TIME_V2_ARM);
    printf("==============================================\n");
    h = CreateFileA("\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] %s: open failed (Win32 %lu)\n", TV2_ID, GetLastError());
        return 1;
    }
    for (index = 0; index < count; index++) {
        AVB_ADAPTER_IDENTITY_REQUEST id;
        DWORD br = 0;
        LONG nt;
        ZeroMemory(&id, sizeof(id));
        id.version = AVB_ADAPTER_IDENTITY_VERSION;
        id.index = index;
        nt = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, &id, sizeof(id), &id, sizeof(id), &br);
        if (nt < 0 || id.status != 0u) {
            printf("[BLOCKED] %s/adapter:%u: GET_ADAPTER_IDENTITY nt=0x%08lX status=0x%08X\n", TV2_ID, index,
                   (unsigned long)nt, id.status);
            tests_failed++; tv2_nonpass++;
            if (index == 0) break;   /* the adapter count is unknown */
            continue;
        }
        count = id.count;
        enumerated++;
        tv2_adapter(index, &id);
    }
    CloseHandle(h);
    if (enumerated == 0) { printf("[BLOCKED] %s: no adapter enumerated\n", TV2_ID); tv2_nonpass++; }
    printf("\n%s SUMMARY: adapters=%u PASS=%d non-pass=%d\n", TV2_ID, enumerated, tests_passed, tv2_nonpass);
    return (tv2_nonpass > 0 || tests_passed == 0) ? 1 : 0;
}

int main(int argc, char* argv[]) {
    /* --case TC-TARGET-015 (#299): the isolated timer-0 characterization case; --case TC-TARGET-V2-001 (#515): the V2
     * QUERY/validation/access case; any other argument fails closed */
    if (argc > 1) {
        if (argc == 3 && strcmp(argv[1], "--case") == 0 && strcmp(argv[2], TT15_ID) == 0) return tt15_run();
        if (argc == 3 && strcmp(argv[1], "--case") == 0 && strcmp(argv[2], TV2_ID) == 0) return tv2_run();
        printf("usage: test_ioctl_target_time [--case %s | --case %s]\n", TT15_ID, TV2_ID);
        return 2;
    }
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

    /* Enumerate adapters and bind to first with INTEL_CAP_TSN_TAS (implies TRGTTIML).
     * Adapter ordering is configuration-dependent — do NOT hardcode index 0. */
    {
        BOOL bound = FALSE;
        for (UINT32 idx = 0; idx < 16; idx++) {
            AVB_ENUM_REQUEST enumReq;
            DWORD br = 0;
            ZeroMemory(&enumReq, sizeof(enumReq));
            enumReq.index = idx;
            if (!DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                                 &enumReq, sizeof(enumReq),
                                 &enumReq, sizeof(enumReq), &br, NULL))
                break;

            if (!(enumReq.capabilities & INTEL_CAP_TSN_TAS))
                continue;  /* no TRGTTIML on this adapter */

            AVB_OPEN_REQUEST openReq;
            ZeroMemory(&openReq, sizeof(openReq));
            openReq.vendor_id = enumReq.vendor_id;
            openReq.device_id = enumReq.device_id;
            openReq.index     = idx;
            if (!DeviceIoControl(hDevice, IOCTL_AVB_OPEN_ADAPTER,
                                 &openReq, sizeof(openReq),
                                 &openReq, sizeof(openReq), &br, NULL)
                    || openReq.status != 0)
                continue;

            printf("[INFO] Bound to adapter %u VID=0x%04X DID=0x%04X (INTEL_CAP_TSN_TAS/TRGTTIML)\n",
                   idx, enumReq.vendor_id, enumReq.device_id);
            bound = TRUE;
            break;
        }
        if (!bound) {
            printf("[SKIP] No adapter with INTEL_CAP_TSN_TAS (TRGTTIML) found\n");
            CloseHandle(hDevice);
            printf("\n==============================================\n");
            printf("TEST SUMMARY\n");
            printf("==============================================\n");
            printf("PASSED:  %d\nFAILED:  %d\nSKIPPED: %d\nTOTAL:   %d\n",
                   tests_passed, tests_failed, tests_skipped,
                   tests_passed + tests_failed + tests_skipped);
            printf("==============================================\n");
            return 0;  /* SKIP is not a failure */
        }
    }

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
    test_target_005_past_time_detection(hDevice);
    test_target_006_imminent_deadline(hDevice);
    test_target_007_dual_timer_window(hDevice);
    test_target_009_null_buffer(hDevice);
    test_target_011_invalid_timer(hDevice);
    
    /* Run Task 7 Test: Target Time Event Delivery */
    printf("\n========== TASK 7: TARGET TIME EVENT DELIVERY ==========\n\n");
    test_task7_target_time_event(hDevice);
    
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
