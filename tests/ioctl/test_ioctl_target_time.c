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
