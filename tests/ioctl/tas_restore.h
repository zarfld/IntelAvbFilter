/*
 * tas_restore.h — TAS test-state-restoration contract infrastructure
 *
 * Shared between test_ioctl_tas.c (production runner) and
 * test_tas_restore_contract.c (mock unit tests).
 *
 * IOCTL_CALL macro: callers define this before including the header.
 * In production code:  #define IOCTL_CALL DeviceIoControl
 * In mock unit tests:  #define IOCTL_CALL mock_ioctl
 *
 * This ensures the mock tests exercise the SAME implementation as the
 * production runner, not a duplicate copy.
 *
 * Issue #328 / P1 test quality
 */
#pragma once

#ifndef IOCTL_CALL
#  error "Define IOCTL_CALL before including tas_restore.h (e.g. #define IOCTL_CALL DeviceIoControl)"
#endif

#include "../../include/avb_ioctl.h"

typedef struct {
    BOOL     valid;          /* 1 = snapshot was taken before any mutation */
    avb_u32  tsauxc_before;  /* TSAUXC register value at snapshot time */
    BOOL     tas_was_armed;  /* driver-reported armed state at snapshot time */
    struct tsn_tas_config tas_original_config; /* GCL if tas_was_armed */
} TAS_SNAPSHOT;

/* Result codes */
#ifndef TC_PASS
#define TC_PASS             0
#define TC_FAIL             1
#define TC_CLEANUP_FAILED   2
#define TC_BLOCKED          3
#define TC_SKIP             4   /* test ran, capability/hardware not available */
#endif

/*
 * tas_probe_restore_capability — unconditional BLOCK.
 *
 * TC-TAS-001..008 are BLOCKED on all adapters until a safe, miniport-
 * coordinated TAS disable path exists (#328 P0.2).
 *
 * No IOCTLs are called.  The previous capability-based approach was unsafe:
 *   - AvbGetLwfSupportedCapabilities() omits INTEL_CAP_TSN_TAS below BAR_MAPPED.
 *   - IOCTL_AVB_GET_HW_STATE can trigger BAR0 discovery, changing the response
 *     on subsequent calls (non-idempotent side effect).
 *
 * Returns FALSE always (until a safe restore path is implemented).
 */
static BOOL tas_probe_restore_capability(HANDLE hDevice) {
    (void)hDevice;
    printf("  [PREFLIGHT-BLOCKED] TC-TAS-001..008 unconditionally BLOCKED\n");
    printf("  [PREFLIGHT-BLOCKED]   No safe TAS restore path (#328 P0.2)\n");
    printf("  [PREFLIGHT-BLOCKED]   Required: miniport OID or link-cycle (separate task)\n");
    return FALSE;
}

/*
 * tas_capture_state — capture pre-mutation state.
 * Calls tas_probe_restore_capability() first; returns FALSE without any
 * hardware-mutating IOCTL if BLOCKED.
 */
static BOOL tas_capture_state(HANDLE hDevice, TAS_SNAPSHOT *snap) {
    DWORD br = 0;
    memset(snap, 0, sizeof(*snap));

    if (!tas_probe_restore_capability(hDevice))
        return FALSE;

    AVB_CLOCK_CONFIG clockCfg;
    memset(&clockCfg, 0, sizeof(clockCfg));
    if (!IOCTL_CALL(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG,
                    &clockCfg, sizeof(clockCfg),
                    &clockCfg, sizeof(clockCfg), &br, NULL) || clockCfg.status != 0) {
        printf("  [CAPTURE-FAIL] Cannot read TSAUXC (error=%lu status=0x%08X)\n",
               GetLastError(), clockCfg.status);
        return FALSE;
    }
    snap->tsauxc_before = clockCfg.tsauxc;

    /* Driver-tracked state (not hardware readback) */
    AVB_TAS_STATE tasState;
    memset(&tasState, 0, sizeof(tasState));
    if (!IOCTL_CALL(hDevice, IOCTL_AVB_GET_TAS_STATE,
                    NULL, 0, &tasState, sizeof(tasState), &br, NULL) || tasState.status != 0) {
        printf("  [CAPTURE-FAIL] Cannot read TAS state (error=%lu status=0x%08X)\n",
               GetLastError(), tasState.status);
        return FALSE;
    }
    snap->tas_was_armed = (tasState.armed != 0);
    if (snap->tas_was_armed) {
        printf("  [PREFLIGHT-BLOCKED] tas_armed=1 on non-TAS adapter — inconsistent STATE_UNKNOWN\n");
        return FALSE;
    }
    snap->valid = TRUE;
    printf("  [SNAPSHOT] tsauxc=0x%08X tas_armed=0 (driver-cache)\n", snap->tsauxc_before);
    return TRUE;
}

/*
 * tas_restore_state — restore captured baseline unconditionally.
 * Returns FALSE if any step fails; caller must report CLEANUP_FAILED.
 */
static BOOL tas_restore_state(HANDLE hDevice, const TAS_SNAPSHOT *snap) {
    DWORD br = 0;
    if (!snap->valid) {
        printf("  [RESTORE-FAIL] No valid snapshot\n");
        return FALSE;
    }
    BOOL ok = TRUE;

    AVB_DISARM_TAS_REQUEST disarm;
    memset(&disarm, 0, sizeof(disarm));
    if (!IOCTL_CALL(hDevice, IOCTL_AVB_DISARM_TAS, NULL, 0,
                    &disarm, sizeof(disarm), &br, NULL)) {
        printf("  [RESTORE-FAIL] DISARM_TAS IOCTL failed (error=%lu)\n", GetLastError());
        ok = FALSE;
    } else if (disarm.status == 0xC00000BBu) { /* STATUS_NOT_SUPPORTED */
        printf("  [RESTORE-FAIL-BLOCKED] DISARM_TAS STATUS_NOT_SUPPORTED — hw state UNKNOWN\n");
        ok = FALSE;
    } else if (disarm.status != 0) {
        printf("  [RESTORE-FAIL] DISARM_TAS status=0x%08X\n", disarm.status);
        ok = FALSE;
    } else {
        printf("  [RESTORE] TAS disarmed\n");
    }

    AVB_HW_TIMESTAMPING_REQUEST hwTs;
    memset(&hwTs, 0, sizeof(hwTs));
    hwTs.enable     = ((snap->tsauxc_before & 0x80000000u) == 0) ? 1 : 0;
    hwTs.timer_mask = 0x1;
    if (!IOCTL_CALL(hDevice, IOCTL_AVB_SET_HW_TIMESTAMPING,
                    &hwTs, sizeof(hwTs), &hwTs, sizeof(hwTs), &br, NULL) || hwTs.status != 0) {
        printf("  [RESTORE-FAIL] TSAUXC restore failed (error=%lu status=0x%08X)\n",
               GetLastError(), hwTs.status);
        ok = FALSE;
    } else {
        printf("  [RESTORE] TSAUXC restored (enable=%u current=0x%08X)\n",
               hwTs.enable, hwTs.current_tsauxc);
    }
    return ok;
}

/*
 * tas_verify_restore — post-restore verification.
 * Returns FALSE if driver state does not match the pre-test snapshot.
 */
static BOOL tas_verify_restore(HANDLE hDevice, const TAS_SNAPSHOT *snap) {
    DWORD br = 0;
    AVB_TAS_STATE tasState;
    memset(&tasState, 0, sizeof(tasState));
    if (!IOCTL_CALL(hDevice, IOCTL_AVB_GET_TAS_STATE,
                    NULL, 0, &tasState, sizeof(tasState), &br, NULL) || tasState.status != 0) {
        printf("  [VERIFY-FAIL] Cannot read TAS state after restore\n");
        return FALSE;
    }
    BOOL now_armed = (tasState.armed != 0);
    if (now_armed != snap->tas_was_armed) {
        printf("  [VERIFY-FAIL] armed mismatch: was %d before, is %d after\n",
               snap->tas_was_armed, now_armed);
        return FALSE;
    }
    printf("  [VERIFY-OK] TAS state matches snapshot (armed=%d)\n", now_armed);
    return TRUE;
}
