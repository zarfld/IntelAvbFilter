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
 * IOCTL_NTSTATUS_CALL: callers also define this.  It returns the EXACT NTSTATUS an IOCTL was
 * completed with.  Needed because IOCTLs are METHOD_BUFFERED (an error completion drops the
 * output buffer, so a status field inside it is never delivered) and GetLastError() is lossy.
 * In production:       #define IOCTL_NTSTATUS_CALL avb_ioctl_ntstatus   (tests/common/avb_ntstatus.h)
 * In mock unit tests:  #define IOCTL_NTSTATUS_CALL mock_ioctl_ntstatus
 *
 * Verdicts follow docs/testing/test-verdict-contract.md.
 *
 * Verifies: #9 (REQ-F-TAS-001); Test Issue: #206
 */
#pragma once

#ifndef IOCTL_CALL
#  error "Define IOCTL_CALL before including tas_restore.h (e.g. #define IOCTL_CALL DeviceIoControl)"
#endif
#ifndef IOCTL_NTSTATUS_CALL
#  error "Define IOCTL_NTSTATUS_CALL before including tas_restore.h (e.g. #define IOCTL_NTSTATUS_CALL avb_ioctl_ntstatus)"
#endif

#include "../../include/avb_ioctl.h"
#include "../common/avb_verdict.h"

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
 * coordinated TAS disable path exists (P0.2; requirement #9, test issue #206).
 * BLOCKED = unmet safety precondition. It is NOT evidence that the hardware lacks TAS.
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
    printf("  [PREFLIGHT-BLOCKED]   No safe TAS restore path (P0.2; #9 / #206)\n");
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
 * tas_restore_state_ex — restore captured baseline unconditionally.
 * Returns FALSE if any step fails; caller must report CLEANUP_FAILED.
 *
 * DISARM_TAS is classified from the EXACT NTSTATUS (see IOCTL_NTSTATUS_CALL above) per the
 * verdict contract.  `tas_cap` is the evidence that THIS adapter supports TAS:
 *   NOT_IMPLEMENTED                 -> SKIP_NOT_IMPLEMENTED (hardware ok, safe disable missing)
 *   NOT_SUPPORTED + cap PRESENT     -> FAIL        (adapter advertises TAS yet says "not supported")
 *   NOT_SUPPORTED + cap UNKNOWN     -> INCONCLUSIVE (never SKIP_UNSUPPORTED without evidence)
 *   DEVICE_NOT_READY etc.           -> BLOCKED
 *   other error                     -> FAIL
 * Whatever the verdict, a failed DISARM leaves the hardware state UNKNOWN: returns FALSE.
 * `verdict_out` / `status_out` (optional) report the classification and the original NTSTATUS.
 */
static BOOL tas_restore_state_ex(HANDLE hDevice, const TAS_SNAPSHOT *snap,
                                 AVB_CAP_EVIDENCE tas_cap,
                                 AVB_VERDICT *verdict_out, LONG *status_out) {
    DWORD br = 0;
    AVB_VERDICT disarm_v  = AVB_VERDICT_BLOCKED;
    LONG        disarm_st = AVB_NTSTATUS_UNAVAILABLE;
    BOOL ok = TRUE;

    if (!snap->valid) {
        printf("  [RESTORE-FAIL] No valid snapshot\n");
        if (verdict_out) *verdict_out = AVB_VERDICT_BLOCKED;
        if (status_out)  *status_out  = AVB_NTSTATUS_UNAVAILABLE;
        return FALSE;
    }

    AVB_DISARM_TAS_REQUEST disarm;
    memset(&disarm, 0, sizeof(disarm));
    disarm_st = IOCTL_NTSTATUS_CALL(hDevice, IOCTL_AVB_DISARM_TAS, NULL, 0,
                                    &disarm, sizeof(disarm), &br);
    /* On an error completion `disarm` was never written back (METHOD_BUFFERED), so the verdict
     * is derived from the NTSTATUS; the payload is only consulted on success. */
    disarm_v = avb_verdict_from_status(disarm_st, tas_cap, disarm.status == 0, 1);
    switch (disarm_v) {
    case AVB_VERDICT_PASS:
        printf("  [RESTORE] TAS disarmed\n");
        break;
    case AVB_VERDICT_SKIP_NOT_IMPLEMENTED:
        printf("  [RESTORE-FAIL-BLOCKED] DISARM_TAS ntstatus=0x%08X NOT_IMPLEMENTED - safe TAS disable missing; hw state UNKNOWN\n",
               (unsigned)disarm_st);
        ok = FALSE;
        break;
    default:
        if (disarm_st == AVB_NTSTATUS_UNAVAILABLE)
            printf("  [RESTORE-FAIL] DISARM_TAS exact NTSTATUS unavailable (%s; error=%lu); hw state UNKNOWN\n",
                   avb_verdict_tag(disarm_v), GetLastError());
        else
            printf("  [RESTORE-FAIL] DISARM_TAS ntstatus=0x%08X verdict=%s; hw state UNKNOWN\n",
                   (unsigned)disarm_st, avb_verdict_tag(disarm_v));
        ok = FALSE;
        break;
    }
    if (verdict_out) *verdict_out = disarm_v;
    if (status_out)  *status_out  = disarm_st;

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
 * tas_restore_state — compatible wrapper.  Restore only follows arming TAS, which requires
 * the TAS capability, so the adapter is treated as TAS-capable (a NOT_SUPPORTED answer is
 * then a contract violation, not missing hardware).
 */
static BOOL tas_restore_state(HANDLE hDevice, const TAS_SNAPSHOT *snap) {
    AVB_VERDICT v; LONG st;
    return tas_restore_state_ex(hDevice, snap, AVB_CAP_PRESENT, &v, &st);
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
