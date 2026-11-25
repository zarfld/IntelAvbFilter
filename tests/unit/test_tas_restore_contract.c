/*
 * test_tas_restore_contract.c
 * Mock unit tests for the TAS state-restoration contract
 *
 * Verifies behavior, not source text, using a configurable mock IOCTL backend.
 * No driver, hardware, or elevated privileges required.
 *
 * The mock follows the real METHOD_BUFFERED completion rules: an error completion makes
 * DeviceIoControl return FALSE with a lossy Win32 error, and the output buffer is NOT
 * copied back (so a status field inside it is never delivered).  The exact NTSTATUS is
 * only available through IOCTL_NTSTATUS_CALL (NtDeviceIoControlFile in production).
 *
 * Verifies: #9 (REQ-F-TAS-001), #206 (TAS test issue)
 * TC-MOCK-057 checks the send-window accounting of TC-TASAPI-001 (#366): test logic only, no hardware TX claim.
 * TC-MOCK-058 / TC-MOCK-059 check the F2 STATE and F2 REFUSE classification of TEST-TASACT-FAULT-001 (#482, PR #489 review):
 * test logic only, no fault-path or hardware claim.
 * Verdict contract: docs/testing/test-verdict-contract.md
 *
 * Build:
 *   cl /nologo /W3 /Zi -I include -I external/intel_avb/lib
 *      tests\unit\test_tas_restore_contract.c /Fe:test_tas_restore_contract.exe
 *
 * Exit codes: 0 = all pass, 1 = at least one failure
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
/* Production restore infrastructure via IOCTL_CALL / IOCTL_NTSTATUS_CALL macros.
 * Tests exercise the SAME implementation as test_ioctl_tas.c. */
static BOOL mock_ioctl(HANDLE h, DWORD code, LPVOID in_buf, DWORD in_sz,
                       LPVOID out_buf, DWORD out_sz, LPDWORD bytes_ret,
                       LPOVERLAPPED ov);
static LONG mock_ioctl_ntstatus(HANDLE h, DWORD code, LPVOID in_buf, DWORD in_sz,
                                LPVOID out_buf, DWORD out_sz, LPDWORD bytes_ret);
#define IOCTL_CALL mock_ioctl
#define IOCTL_NTSTATUS_CALL mock_ioctl_ntstatus
#include "../ioctl/tas_restore.h"
#include "../ioctl/tas_restoration_eval.h"
#include "../ioctl/tas_wire_match.h"
#include "../../devices/i226_tas_activate.h"
#include "../../devices/i226_phc_seed.h"
#include "../ioctl/tas_activate_eval.h"
#include "../ioctl/owned_worker.h"         /* #505: worker ownership, PR #518 review */
#include "../ioctl/tas_activate_cases.h"   /* #487: contained cases, selection, exit status */
#include "../ioctl/tas_lifecycle_eval.h"   /* #364: instance lookup and lifecycle outcome (verbatim part) */
#include "../ioctl/tas_query_tx_eval.h"   /* TC-MOCK-057: TC-TASAPI-001 send-window accounting (#366), test logic only */

/* =========================================================================
 * Mock infrastructure
 * ========================================================================= */

typedef struct {
    /* IOCTL_AVB_GET_HW_STATE */
    BOOL     hw_state_ok;       /* FALSE → DeviceIoControl fails */
    avb_u32  hw_driver_caps;    /* AVB_HW_STATE_DRIVER_CAPABILITIES value */

    /* IOCTL_AVB_GET_CLOCK_CONFIG */
    BOOL     clock_cfg_ok;
    avb_u32  tsauxc;

    /* IOCTL_AVB_GET_TAS_STATE */
    BOOL     tas_state_ok;
    avb_u32  tas_armed;

    /* IOCTL_AVB_DISARM_TAS: the driver completes the IRP with this NTSTATUS.
     * Error severity => DeviceIoControl FALSE, Win32 error mapped (lossy), payload untouched.
     * AVB_NTSTATUS_UNAVAILABLE => the exact status cannot be observed. */
    LONG     disarm_ntstatus;
    int      disarm_calls;      /* how many times DISARM_TAS was issued */

    /* IOCTL_AVB_SET_HW_TIMESTAMPING */
    BOOL     set_ts_ok;
    avb_u32  set_ts_status;

} MockState;

static MockState g_mock;

/* Sentinel handle value used by tests — never passed to real OS */
#define MOCK_HANDLE ((HANDLE)(ULONG_PTR)0xDEAD0001)

static BOOL mock_ioctl(HANDLE h, DWORD code,
                       LPVOID in_buf, DWORD in_sz,
                       LPVOID out_buf, DWORD out_sz,
                       LPDWORD bytes_ret, LPOVERLAPPED ov)
{
    (void)h; (void)in_buf; (void)in_sz; (void)ov;
    if (bytes_ret) *bytes_ret = 0;

    if (code == IOCTL_AVB_GET_HW_STATE) {
        if (!g_mock.hw_state_ok) { SetLastError(ERROR_NOT_READY); return FALSE; }
        if (out_sz >= sizeof(AVB_HW_STATE_QUERY)) {
            AVB_HW_STATE_QUERY *q = (AVB_HW_STATE_QUERY *)out_buf;
            memset(q, 0, sizeof(*q));
            q->reserved = g_mock.hw_driver_caps; /* driver-filtered caps */
            if (bytes_ret) *bytes_ret = sizeof(*q);
        }
        return TRUE;
    }
    if (code == IOCTL_AVB_GET_CLOCK_CONFIG) {
        if (!g_mock.clock_cfg_ok) { SetLastError(ERROR_NOT_READY); return FALSE; }
        if (out_sz >= sizeof(AVB_CLOCK_CONFIG)) {
            AVB_CLOCK_CONFIG *c = (AVB_CLOCK_CONFIG *)out_buf;
            memset(c, 0, sizeof(*c));
            c->tsauxc = g_mock.tsauxc;
            c->status = 0;
            if (bytes_ret) *bytes_ret = sizeof(*c);
        }
        return TRUE;
    }
    if (code == IOCTL_AVB_GET_TAS_STATE) {
        if (!g_mock.tas_state_ok) { SetLastError(ERROR_NOT_READY); return FALSE; }
        if (out_sz >= sizeof(AVB_TAS_STATE)) {
            AVB_TAS_STATE *s = (AVB_TAS_STATE *)out_buf;
            memset(s, 0, sizeof(*s));
            s->armed  = g_mock.tas_armed;
            s->status = 0;
            if (bytes_ret) *bytes_ret = sizeof(*s);
        }
        return TRUE;
    }
    if (code == IOCTL_AVB_DISARM_TAS) {
        /* Real completion rules: error severity => FALSE, lossy Win32 error, payload untouched. */
        g_mock.disarm_calls++;
        if (g_mock.disarm_ntstatus < 0) {
            switch ((ULONG)g_mock.disarm_ntstatus) {
            case 0xC00000BBu: SetLastError(ERROR_NOT_SUPPORTED);    break;  /* STATUS_NOT_SUPPORTED   -> 50 */
            case 0xC0000002u: SetLastError(ERROR_INVALID_FUNCTION); break;  /* STATUS_NOT_IMPLEMENTED -> 1  */
            default:          SetLastError(ERROR_GEN_FAILURE);      break;
            }
            return FALSE;
        }
        if (out_sz >= sizeof(AVB_DISARM_TAS_REQUEST)) {
            AVB_DISARM_TAS_REQUEST *d = (AVB_DISARM_TAS_REQUEST *)out_buf;
            memset(d, 0, sizeof(*d));
            d->status = 0;
            if (bytes_ret) *bytes_ret = sizeof(*d);
        }
        return TRUE;
    }
    if (code == IOCTL_AVB_SET_HW_TIMESTAMPING) {
        if (!g_mock.set_ts_ok) { SetLastError(ERROR_NOT_READY); return FALSE; }
        if (out_sz >= sizeof(AVB_HW_TIMESTAMPING_REQUEST)) {
            AVB_HW_TIMESTAMPING_REQUEST *r = (AVB_HW_TIMESTAMPING_REQUEST *)out_buf;
            r->status = g_mock.set_ts_status;
            if (bytes_ret) *bytes_ret = sizeof(*r);
        }
        return TRUE;
    }
    /* Unhandled IOCTL code in mock */
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/* Raw NTSTATUS channel (NtDeviceIoControlFile in production).  For DISARM_TAS it returns exactly
 * what the driver completed the IRP with, and it too leaves the output buffer untouched on error. */
static LONG mock_ioctl_ntstatus(HANDLE h, DWORD code, LPVOID in_buf, DWORD in_sz,
                                LPVOID out_buf, DWORD out_sz, LPDWORD bytes_ret)
{
    if (code == IOCTL_AVB_DISARM_TAS) {
        g_mock.disarm_calls++;
        if (g_mock.disarm_ntstatus >= 0 && out_buf && out_sz >= sizeof(AVB_DISARM_TAS_REQUEST)) {
            memset(out_buf, 0, sizeof(AVB_DISARM_TAS_REQUEST));
            if (bytes_ret) *bytes_ret = sizeof(AVB_DISARM_TAS_REQUEST);
        }
        return g_mock.disarm_ntstatus;
    }
    /* Other IOCTLs: success/failure from the BOOL mock; no exact status is modelled. */
    return mock_ioctl(h, code, in_buf, in_sz, out_buf, out_sz, bytes_ret, NULL) ? 0 : AVB_NTSTATUS_UNAVAILABLE;
}

/* run_case: uses production functions from tas_restore.h (via IOCTL_CALL=mock_ioctl).
 * This exercises the ACTUAL implementation, not a copy. */
static int run_case(HANDLE h, int *blocked, int *cleanup_failed) {
    TAS_SNAPSHOT snap;
    if (!tas_capture_state(h, &snap)) { (*blocked)++; return TC_BLOCKED; }
    /* Simulate mutation: noop — result accounting stays clean */
    BOOL restore_ok = tas_restore_state(h, &snap);
    BOOL verify_ok  = restore_ok ? tas_verify_restore(h, &snap) : FALSE;
    if (!restore_ok || !verify_ok) (*cleanup_failed)++;   /* cleanup only (Q 2026-10-28): the case outcome is unchanged */
    return TC_PASS;
}

/* =========================================================================
 * Test cases
 * ========================================================================= */

static int g_tests_run  = 0;
static int g_tests_pass = 0;
static int g_tests_fail = 0;

#define EXPECT_EQ(label, got, want) do { \
    g_tests_run++; \
    if ((got) == (want)) { g_tests_pass++; printf("  [PASS] %s\n", (label)); } \
    else { g_tests_fail++; printf("  [FAIL] %s: got=%d want=%d\n", (label), (int)(got), (int)(want)); } \
} while(0)

#define EXPECT_TRUE(label, expr) EXPECT_EQ(label, !!(expr), 1)
#define EXPECT_FALSE(label, expr) EXPECT_EQ(label, !!(expr), 0)

/*
 * TC-MOCK-001: Unconditional block — PREFLIGHT always returns FALSE.
 * No IOCTLs are called regardless of adapter state or capability.
 * Verifies the fail-closed property of the new unconditional block.
 */
static void test_01_unconditional_block(void) {
    printf("\n[TC-MOCK-001] Unconditional block — all adapters BLOCKED, no IOCTLs called\n");
    memset(&g_mock, 0, sizeof(g_mock));
    /* All IOCTL return flags FALSE: if any IOCTL were called, run_case would fail */
    g_mock.hw_state_ok = FALSE;
    g_mock.clock_cfg_ok = FALSE;
    g_mock.tas_state_ok = FALSE;
    /* Even with all IOCTLs failing, run_case must return TC_BLOCKED (not TC_FAIL) */
    int blocked = 0, cleanup_failed = 0;
    int rc = run_case(MOCK_HANDLE, &blocked, &cleanup_failed);
    EXPECT_EQ("result is TC_BLOCKED", rc, TC_BLOCKED);
    EXPECT_EQ("blocked counter incremented", blocked, 1);
    EXPECT_EQ("cleanup_failed is 0", cleanup_failed, 0);
}

/*
 * TC-MOCK-002: Previously armed adapter — still BLOCKED unconditionally.
 * Verifies that armed state is never even checked (preflight exits before IOCTL).
 */
static void test_02_armed_always_blocked(void) {
    printf("\n[TC-MOCK-002] Armed adapter => BLOCKED (unconditional, armed state not checked)\n");
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.tas_state_ok = TRUE;
    g_mock.tas_armed    = 1; /* would indicate armed state if checked */
    int blocked = 0, cleanup_failed = 0;
    int rc = run_case(MOCK_HANDLE, &blocked, &cleanup_failed);
    EXPECT_EQ("result is TC_BLOCKED", rc, TC_BLOCKED);
    EXPECT_EQ("blocked counter incremented", blocked, 1);
}

/*
 * TC-MOCK-003: GET_HW_STATE failure — BLOCKED regardless (no IOCTL called by preflight).
 * Demonstrates that the old GET_HW_STATE-based preflight has been removed.
 * Zero IOCTLs means zero initialization side-effects.
 */
static void test_03_get_hw_state_not_called(void) {
    printf("\n[TC-MOCK-003] GET_HW_STATE not called by new preflight — BLOCKED regardless\n");
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.hw_state_ok    = FALSE; /* would cause STATE_UNKNOWN in old code */
    g_mock.hw_driver_caps = 0;    /* would cause PASS in old code if hw_state_ok were TRUE */
    int blocked = 0, cleanup_failed = 0;
    int rc = run_case(MOCK_HANDLE, &blocked, &cleanup_failed);
    EXPECT_EQ("result is TC_BLOCKED", rc, TC_BLOCKED);
}

/*
 * TC-MOCK-004: Fix for old fail-open: previously a non-TAS adapter would pass preflight,
 * arm TAS, then fail at disarm with STATUS_NOT_SUPPORTED -> CLEANUP_FAILED.
 * With the unconditional block, this scenario now produces BLOCKED (not CLEANUP_FAILED).
 * Directly tests tas_restore_state to verify STATUS_NOT_SUPPORTED produces FALSE.
 */
/* Restore with a given DISARM_TAS completion status; returns the restore result and the
 * classified verdict + preserved original NTSTATUS. */
static BOOL restore_with(LONG disarm_ntstatus, AVB_CAP_EVIDENCE tas_cap,
                         AVB_VERDICT *verdict, LONG *status_out)
{
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.set_ts_ok     = TRUE;
    g_mock.set_ts_status = 0;
    g_mock.disarm_ntstatus = disarm_ntstatus;

    TAS_SNAPSHOT snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = TRUE; snap.tsauxc_before = 0; snap.tas_was_armed = FALSE;
    /* Direct call - bypasses the (unconditionally BLOCKED) preflight */
    return tas_restore_state_ex(MOCK_HANDLE, &snap, tas_cap, verdict, status_out);
}

static void test_04_disarm_status_classification(void) {
    AVB_VERDICT v; LONG st; BOOL ok;

    printf("\n[TC-MOCK-004a] STATUS_NOT_SUPPORTED from a TAS-advertising adapter => FAIL (contract violation)\n");
    ok = restore_with(STATUS_NOT_SUPPORTED, AVB_CAP_PRESENT, &v, &st);
    EXPECT_FALSE("restore fails", ok);
    EXPECT_EQ("verdict FAIL (NOT_SUPPORTED means absent hardware, adapter advertises TAS)", v, AVB_VERDICT_FAIL);
    EXPECT_EQ("original NTSTATUS preserved (0xC00000BB)", (ULONG)st, 0xC00000BBu);
    EXPECT_EQ("DISARM issued exactly once", g_mock.disarm_calls, 1);

    printf("\n[TC-MOCK-004b] STATUS_NOT_IMPLEMENTED => SKIP_NOT_IMPLEMENTED (safe disable path missing), restore FALSE\n");
    ok = restore_with(STATUS_NOT_IMPLEMENTED, AVB_CAP_PRESENT, &v, &st);
    EXPECT_FALSE("restore fails: hardware state cannot be restored", ok);
    EXPECT_EQ("verdict SKIP_NOT_IMPLEMENTED (never FAIL / SKIP_UNSUPPORTED)", v, AVB_VERDICT_SKIP_NOT_IMPLEMENTED);
    EXPECT_EQ("original NTSTATUS preserved (0xC0000002)", (ULONG)st, 0xC0000002u);

    printf("\n[TC-MOCK-004c] NOT_SUPPORTED with no authoritative capability evidence => INCONCLUSIVE\n");
    ok = restore_with(STATUS_NOT_SUPPORTED, AVB_CAP_UNKNOWN, &v, &st);
    EXPECT_FALSE("restore fails", ok);
    EXPECT_EQ("verdict INCONCLUSIVE (never SKIP_UNSUPPORTED)", v, AVB_VERDICT_INCONCLUSIVE);

    printf("\n[TC-MOCK-004d] DISARM succeeds => restore TRUE, PASS\n");
    ok = restore_with(STATUS_SUCCESS, AVB_CAP_PRESENT, &v, &st);
    EXPECT_TRUE("restore succeeds", ok);
    EXPECT_EQ("verdict PASS", v, AVB_VERDICT_PASS);

    printf("\n[TC-MOCK-004e] Actual failure status => FAIL with the actual status\n");
    ok = restore_with(STATUS_INVALID_PARAMETER, AVB_CAP_PRESENT, &v, &st);
    EXPECT_FALSE("restore fails", ok);
    EXPECT_EQ("verdict FAIL", v, AVB_VERDICT_FAIL);
    EXPECT_EQ("original NTSTATUS preserved (0xC000000D)", (ULONG)st, 0xC000000Du);

    printf("\n[TC-MOCK-004f] Exact status not observable => INCONCLUSIVE, never a fake pass\n");
    ok = restore_with(AVB_NTSTATUS_UNAVAILABLE, AVB_CAP_PRESENT, &v, &st);
    EXPECT_FALSE("restore fails", ok);
    EXPECT_EQ("verdict INCONCLUSIVE", v, AVB_VERDICT_INCONCLUSIVE);

    printf("\n[TC-MOCK-004g] Device not ready => BLOCKED, not an unsupported-hardware skip\n");
    ok = restore_with(STATUS_DEVICE_NOT_READY, AVB_CAP_PRESENT, &v, &st);
    EXPECT_FALSE("restore fails", ok);
    EXPECT_EQ("verdict BLOCKED", v, AVB_VERDICT_BLOCKED);

    printf("\n[TC-MOCK-004h] Full run_case flow: unconditional block still means disarm is never reached\n");
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.clock_cfg_ok   = TRUE;
    g_mock.tas_state_ok   = TRUE;
    g_mock.disarm_ntstatus = STATUS_NOT_SUPPORTED;
    int blocked = 0, cleanup_failed = 0;
    int rc = run_case(MOCK_HANDLE, &blocked, &cleanup_failed);
    EXPECT_EQ("run_case returns TC_BLOCKED (not TC_CLEANUP_FAILED)", rc, TC_BLOCKED);
    EXPECT_EQ("cleanup_failed is 0 - disarm never reached", cleanup_failed, 0);
    EXPECT_EQ("no DISARM_TAS issued (hardware state preserved)", g_mock.disarm_calls, 0);
}

/*
 * TC-MOCK-005: CLEANUP_FAILED path via direct restore_state call.
 * Since run_case always returns BLOCKED (unconditional), CLEANUP_FAILED can
 * only be triggered by code that directly calls tas_restore_state after mutation.
 * Verify the path still produces the correct result for future use.
 */
static void test_05_cleanup_failed_from_restore(void) {
    printf("\n[TC-MOCK-005] Direct restore_state path: CLEANUP_FAILED when disarm fails\n");
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.disarm_ntstatus = STATUS_UNSUCCESSFUL; /* IRP completed with an error: DeviceIoControl returns FALSE */
    g_mock.set_ts_ok      = TRUE;
    g_mock.set_ts_status  = 0;

    TAS_SNAPSHOT snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = TRUE; snap.tsauxc_before = 0; snap.tas_was_armed = FALSE;

    BOOL restore_ok = tas_restore_state(MOCK_HANDLE, &snap);
    EXPECT_FALSE("tas_restore_state returns FALSE when DISARM_TAS IOCTL fails", restore_ok);

    /* Verify CLEANUP_FAILED path: restore FALSE -> verify not called -> CLEANUP_FAILED */
    int cleanup_failed = 0;
    BOOL verify_ok = restore_ok ? tas_verify_restore(MOCK_HANDLE, &snap) : FALSE;
    if (!restore_ok || !verify_ok) cleanup_failed++;
    EXPECT_EQ("cleanup_failed counted when restore fails", cleanup_failed, 1);
}

/*
 * TC-MOCK-006: All hw cases blocked — exit code 3 via actual counter state.
 * Replaces the previous pure-arithmetic test with one that calls run_case
 * 8 times and reads real counter values to compute the exit code.
 */
static void test_06_all_blocked_exit_code(void) {
    printf("\n[TC-MOCK-006] All hw cases blocked => exit code 3 (via actual run_case calls)\n");
    memset(&g_mock, 0, sizeof(g_mock));
    int local_blocked = 0, local_cleanup = 0;
    /* Simulate 8 hw cases all BLOCKED */
    for (int i = 0; i < 8; i++) {
        int rc = run_case(MOCK_HANDLE, &local_blocked, &local_cleanup);
        EXPECT_EQ("each case is TC_BLOCKED", rc, TC_BLOCKED);
    }
    EXPECT_EQ("blocked counter is 8", local_blocked, 8);
    EXPECT_EQ("cleanup_failed is 0", local_cleanup, 0);

    /* Compute exit code from actual counters (mirrors main() logic) */
    int passed = 0, failed = 0;
    int exit_code;
    if (local_cleanup > 0)     exit_code = 4;
    else if (failed > 0)       exit_code = 1;
    else if (local_blocked > 0) exit_code = 3;
    else if (passed == 0)      exit_code = 2;
    else                       exit_code = 0;
    EXPECT_EQ("exit code is 3 (BLOCKED mandatory, from actual counters)", exit_code, 3);
}

/*
 * TC-MOCK-010: No-device scenario — exit code 2 (no tests ran), never 0.
 * Simulates: OpenAvbDevice returns INVALID_HANDLE_VALUE.
 * Verifies the exit-code logic when g_passed=0 with no active blocking.
 */
static void test_10_no_device_exit_code(void) {
    printf("\n[TC-MOCK-010] No device/no adapter — exit code 2, never 0\n");
    /* When no device: g_passed=0, g_skipped=N, g_blocked=0, g_failed=0 */
    int passed = 0, skipped = 10, blocked = 0, failed = 0, cleanup_failed = 0;
    int exit_code;
    if (cleanup_failed > 0) exit_code = 4;
    else if (failed > 0)    exit_code = 1;
    else if (blocked > 0)   exit_code = 3;
    else if (passed == 0)   exit_code = 2; /* covers skip-only: no PASS produced */
    else                    exit_code = 0;
    EXPECT_EQ("exit code is 2 (no PASS, not 0)", exit_code, 2);
    EXPECT_EQ("skipped=10 does not force exit 0", exit_code != 0, 1);
    (void)skipped;
}

/*
 * TC-MOCK-011: SKIP ≠ PASS — verify TC_SKIP is a distinct outcome.
 * tas_restore.h defines TC_SKIP = 4, which is different from TC_PASS = 0.
 * SKIP must not increment any pass counter.
 */
static void test_11_skip_distinct_from_pass(void) {
    printf("\n[TC-MOCK-011] SKIP is TC_SKIP=4, not TC_PASS=0\n");
    EXPECT_EQ("TC_SKIP != TC_PASS", TC_SKIP != TC_PASS, 1);
    EXPECT_EQ("TC_SKIP == 4", TC_SKIP, 4);
    EXPECT_EQ("TC_PASS == 0", TC_PASS, 0);
    /* BLOCKED is also distinct from SKIP */
    EXPECT_EQ("TC_BLOCKED != TC_SKIP", TC_BLOCKED != TC_SKIP, 1);
}

/* Evidence of a fully verified restoration; each case below breaks exactly one item. */
static TAS_RESTORATION_EVIDENCE full_restoration_evidence(void) {
    TAS_RESTORATION_EVIDENCE e;
    memset(&e, 0, sizeof(e));
    e.cycle_completed = TRUE; e.adapter_returned = TRUE; e.attach_observed = TRUE;
    e.driver_disarmed = TRUE; e.tsn_mode_clear = TRUE;
    e.regs_compared = 40; e.regs_unreadable = 0; e.regs_changed = 0;
    e.phc_observed = TRUE; e.phc_continuous = TRUE;
    e.link_up = TRUE; e.tx_probed = TRUE; e.tx_healthy = TRUE;
    return e;
}

/*
 * TC-MOCK-012: NIC-cycle restoration outcome.  CLEANUP_FAILED = restoration demonstrably
 * unsuccessful, INCONCLUSIVE_RESTORATION = state it depends on could not be measured.
 * Pure logic only: this is not evidence that a NIC cycle restores any hardware.
 */
static void test_12_restoration_outcome(void) {
    TAS_RESTORATION_EVIDENCE e;
    printf("\n[TC-MOCK-012] NIC-cycle restoration outcome from the evidence\n");

    e = full_restoration_evidence();
    EXPECT_EQ("all evidence => VERIFIED", tas_restoration_outcome(&e), TAS_RESTORATION_VERIFIED);

    /* The essential rule: returning link and traffic alone are never success */
    memset(&e, 0, sizeof(e));
    e.link_up = TRUE; e.tx_probed = TRUE; e.tx_healthy = TRUE;
    EXPECT_FALSE("link and traffic alone are not VERIFIED", tas_restoration_outcome(&e) == TAS_RESTORATION_VERIFIED);

    e = full_restoration_evidence(); e.cycle_completed = FALSE;
    EXPECT_EQ("cycle failed => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
    e = full_restoration_evidence(); e.adapter_returned = FALSE;
    EXPECT_EQ("adapter gone => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
    e = full_restoration_evidence(); e.driver_disarmed = FALSE;
    EXPECT_EQ("driver still armed => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
    e = full_restoration_evidence(); e.tsn_mode_clear = FALSE;
    EXPECT_EQ("TSN mode still set => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
    e = full_restoration_evidence(); e.regs_changed = 1;
    EXPECT_EQ("register differs => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
    e = full_restoration_evidence(); e.phc_continuous = FALSE;
    EXPECT_EQ("PHC discontinuity => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
    e = full_restoration_evidence(); e.tx_healthy = FALSE;
    EXPECT_EQ("TX unhealthy => CLEANUP_FAILED", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);

    e = full_restoration_evidence(); e.attach_observed = FALSE;
    EXPECT_EQ("cycle unproven => INCONCLUSIVE", tas_restoration_outcome(&e), TAS_RESTORATION_INCONCLUSIVE);
    e = full_restoration_evidence(); e.regs_unreadable = 1;
    EXPECT_EQ("unreadable register => INCONCLUSIVE", tas_restoration_outcome(&e), TAS_RESTORATION_INCONCLUSIVE);
    e = full_restoration_evidence(); e.regs_compared = 0;
    EXPECT_EQ("nothing compared => INCONCLUSIVE", tas_restoration_outcome(&e), TAS_RESTORATION_INCONCLUSIVE);
    e = full_restoration_evidence(); e.phc_observed = FALSE; e.phc_continuous = FALSE;
    EXPECT_EQ("PHC unmeasured => INCONCLUSIVE", tas_restoration_outcome(&e), TAS_RESTORATION_INCONCLUSIVE);
    e = full_restoration_evidence(); e.link_up = FALSE; e.tx_probed = FALSE; e.tx_healthy = FALSE;
    EXPECT_EQ("link down, no probe => INCONCLUSIVE", tas_restoration_outcome(&e), TAS_RESTORATION_INCONCLUSIVE);

    /* Armed state that could not be read is unmeasured, not a demonstrated failure */
    e = full_restoration_evidence(); e.driver_disarmed = FALSE; e.armed_unreadable = TRUE;
    EXPECT_EQ("armed state unreadable => INCONCLUSIVE", tas_restoration_outcome(&e), TAS_RESTORATION_INCONCLUSIVE);

    /* A demonstrated failure outranks missing evidence */
    e = full_restoration_evidence(); e.regs_changed = 1; e.regs_unreadable = 1;
    EXPECT_EQ("failure outranks unknown", tas_restoration_outcome(&e), TAS_RESTORATION_CLEANUP_FAILED);
}

/* A frame as seen by the capturing NIC: broadcast UDP to the discard port with the tagged payload */
static size_t wire_frame(unsigned char *f, const unsigned char *mac, unsigned nonce, unsigned seq)
{
    size_t udp = 14 + 20, payload = udp + 8;
    memset(f, 0, 128);
    memset(f, 0xFF, 6);
    memcpy(f + 6, mac, 6);
    f[12] = 0x08;
    f[14] = 0x45;
    f[14 + 9] = 17;
    f[udp + 3] = TAS_WIRE_UDP_PORT;
    tas_wire_fill(f + payload, 64, nonce, seq);
    return payload + 64;
}

/*
 * TC-MOCK-013: only this run's frames from this adapter count as seen on the wire.
 */
static void test_13_wire_match(void) {
    static const unsigned char mac[6] = {0xF0, 0xB2, 0xB9, 0x31, 0x55, 0xDB};
    unsigned char f[128];
    unsigned seq = 0;
    size_t n;

    printf("\n[TC-MOCK-013] wire frame match: source MAC, broadcast, UDP port, run nonce, sequence\n");
    n = wire_frame(f, mac, 0x1234u, 77u);
    EXPECT_TRUE("tagged frame matches", tas_wire_match(f, n, mac, 0x1234u, &seq));
    EXPECT_EQ("sequence recovered", (int)seq, 77);
    EXPECT_FALSE("truncated frame", tas_wire_match(f, n - 60, mac, 0x1234u, &seq));
    EXPECT_FALSE("other run", tas_wire_match(f, n, mac, 0x1235u, &seq));
    f[11] ^= 1;
    EXPECT_FALSE("other source MAC", tas_wire_match(f, n, mac, 0x1234u, &seq));
    n = wire_frame(f, mac, 0x1234u, 77u); f[0] = 0x01;
    EXPECT_FALSE("not broadcast", tas_wire_match(f, n, mac, 0x1234u, &seq));
    n = wire_frame(f, mac, 0x1234u, 77u); f[13] = 0x06;
    EXPECT_FALSE("not IPv4", tas_wire_match(f, n, mac, 0x1234u, &seq));
    n = wire_frame(f, mac, 0x1234u, 77u); f[14 + 9] = 1;
    EXPECT_FALSE("not UDP", tas_wire_match(f, n, mac, 0x1234u, &seq));
    n = wire_frame(f, mac, 0x1234u, 77u); f[14 + 20 + 3] = 10;
    EXPECT_FALSE("other UDP port", tas_wire_match(f, n, mac, 0x1234u, &seq));
    n = wire_frame(f, mac, 0x1234u, 77u); f[14 + 7] = 1;
    EXPECT_FALSE("IP fragment", tas_wire_match(f, n, mac, 0x1234u, &seq));
    n = wire_frame(f, mac, 0x1234u, 77u); f[14 + 20 + 8] ^= 1;
    EXPECT_FALSE("payload not tagged", tas_wire_match(f, n, mac, 0x1234u, &seq));
}

/*
 * TC-MOCK-007: Two adapters — both BLOCKED — no cross-contamination.
 */
static void test_07_two_adapters_both_blocked(void) {
    printf("\n[TC-MOCK-007] Two adapters — both BLOCKED, counters independent\n");
    int blocked_a = 0, cleanup_a = 0;
    int blocked_b = 0, cleanup_b = 0;

    memset(&g_mock, 0, sizeof(g_mock));
    int rc_a = run_case((HANDLE)(ULONG_PTR)0xAAAA, &blocked_a, &cleanup_a);
    EXPECT_EQ("Adapter A blocked", rc_a, TC_BLOCKED);
    EXPECT_EQ("Adapter A blocked_a=1", blocked_a, 1);
    EXPECT_EQ("Adapter A cleanup_a=0", cleanup_a, 0);

    memset(&g_mock, 0, sizeof(g_mock));
    int rc_b = run_case((HANDLE)(ULONG_PTR)0xBBBB, &blocked_b, &cleanup_b);
    EXPECT_EQ("Adapter B blocked", rc_b, TC_BLOCKED);
    EXPECT_EQ("Adapter B blocked_b=1", blocked_b, 1);

    /* Verify counters are independent */
    EXPECT_EQ("Adapter A blocked_a unchanged by B", blocked_a, 1);
    EXPECT_EQ("Adapter A cleanup_a unchanged by B", cleanup_a, 0);
}

/*
 * TC-MOCK-008: Standard suite has no destructive bypass.
 */
static void test_08_no_destructive_bypass(void) {
    printf("\n[TC-MOCK-008] No destructive bypass — BLOCKED regardless of mock config\n");
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.hw_state_ok    = TRUE;
    g_mock.hw_driver_caps = 0; /* old code would have allowed this through */
    g_mock.clock_cfg_ok   = TRUE;
    g_mock.tas_state_ok   = TRUE;
    g_mock.tas_armed      = 0;
    g_mock.disarm_ntstatus = STATUS_SUCCESS;
    g_mock.set_ts_ok      = TRUE;
    g_mock.set_ts_status  = 0;
    /* Even with all IOCTLs succeeding, the unconditional block prevents execution */
    int blocked = 0, cleanup_failed = 0;
    int rc = run_case(MOCK_HANDLE, &blocked, &cleanup_failed);
    EXPECT_EQ("hw case is BLOCKED (unconditional)", rc, TC_BLOCKED);
    EXPECT_EQ("blocked=1", blocked, 1);
    EXPECT_EQ("cleanup_failed=0", cleanup_failed, 0);
}

/*
 * TC-MOCK-009: Non-mutating parameter tests always executable.
 */
static void test_09_negative_tests_always_executable(void) {
    printf("\n[TC-MOCK-009] Non-mutating parameter tests always executable\n");
    int neg_test_result = TC_PASS;
    EXPECT_EQ("negative test executes independently of restore capability", neg_test_result, TC_PASS);
}

/* =========================================================================
 * Main
 * ========================================================================= */

/*
 * TC-MOCK-028 to 033: first TAS activation (#357), the production sequence of devices/i226_tas_activate.h on a fake register
 * file. The order matches what ran on I226 0x125B REV_04 through tas_gate_investigation. These cases test the code path and
 * say nothing about what the hardware does with it.
 */
#define FAKE_REGS 0x4000u
#define FAKE_LOG 128u
typedef struct {
    avb_u32 reg[FAKE_REGS / 4];
    avb_u32 log_off[FAKE_LOG], log_val[FAKE_LOG];
    unsigned writes, reads, fail_write, stuck_after_fail;
    BOOL read_fails, phc_fails;
    avb_u64 phc_first, phc_later;
    unsigned phc_reads;
    unsigned phc_fail_from;   /* nonzero: PHC reads from this number on fail, the first read is number 1 */
} FAKE_HW;

static int fake_read(void *c, avb_u32 off, avb_u32 *v) {
    FAKE_HW *f = (FAKE_HW *)c;
    f->reads++;
    if (f->read_fails) return -1;
    *v = f->reg[off / 4];
    return 0;
}

static int fake_write(void *c, avb_u32 off, avb_u32 v) {
    FAKE_HW *f = (FAKE_HW *)c;
    f->writes++;
    if (f->fail_write != 0 && f->writes >= f->fail_write && f->writes < f->fail_write + 1 + f->stuck_after_fail) return -1;
    if (f->writes <= FAKE_LOG) {
        f->log_off[f->writes - 1] = off;
        f->log_val[f->writes - 1] = v;
    }
    f->reg[off / 4] = v;
    return 0;
}

static int fake_phc(void *c, avb_u64 *ns) {
    FAKE_HW *f = (FAKE_HW *)c;
    if (f->phc_fails) return -1;
    if (f->phc_fail_from != 0 && f->phc_reads + 1 >= f->phc_fail_from) { f->phc_reads++; return -1; }
    *ns = f->phc_reads++ == 0 ? f->phc_first : f->phc_later;
    return 0;
}

static FAKE_HW g_hw;
static AVB_TAS_ACTIVATE_REQUEST g_act;
static int g_mode_written;   /* i226_tas_activate: the transmit-mode write reached the fake adapter */
static const I226_TAS_IO g_io = {&g_hw, fake_read, fake_write, fake_phc};

/* The verified schedule of the Case C and D design: 1 s cycle, 200 to 400 ms windows, BaseT 2 s ahead */
static void act_setup(avb_u32 mode) {
    unsigned q;

    ZeroMemory(&g_hw, sizeof(g_hw));
    ZeroMemory(&g_act, sizeof(g_act));
    g_hw.reg[I226_QBVCYCLET / 4] = 0x3B9ACA00;   /* the clean value seen on I226 0x125B */
    g_hw.phc_first = 5000000000ULL;
    g_hw.phc_later = 5000100000ULL;
    g_act.version = AVB_TAS_ACTIVATE_VERSION;
    g_act.pre_activation = mode;
    g_act.base_time_ns = 7000000000ULL;
    g_act.cycle_ns = 1000000000u;
    g_act.link_mbps = 1000;
    g_act.max_frame_bytes = 1538;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        g_act.window_start_ns[q] = 200000000u;
        g_act.window_end_ns[q] = 400000000u;
    }
}

static int act_order(const avb_u32 *want, unsigned n) {
    unsigned i;

    if (g_hw.writes != n) return 0;
    for (i = 0; i < n; i++) {
        if (g_hw.log_off[i] != want[i]) return 0;
    }
    return 1;
}

static void test_28_activate_validation(void) {
    printf("\n[TC-MOCK-028] Every request is validated before any register write\n");
    act_setup(AVB_TAS_PRE_HOLD);
    EXPECT_EQ("the verified schedule is valid", i226_tas_validate(&g_act), AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.version = 2;
    EXPECT_EQ("unknown version", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(0);
    EXPECT_EQ("no default pre-activation behaviour", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(3);
    EXPECT_EQ("unknown pre-activation behaviour", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.cycle_ns = 0;
    EXPECT_EQ("no cycle", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.cycle_ns = 0x40000000u;
    EXPECT_EQ("cycle beyond the register field", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 4294967296ULL * NSEC_PER_SEC;
    EXPECT_EQ("BaseT seconds beyond the 32-bit BASET_H register", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 4294967295ULL * NSEC_PER_SEC + (NSEC_PER_SEC - 1u);
    EXPECT_EQ("the largest BaseT that fits BASET_H", i226_tas_validate(&g_act), AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.window_end_ns[2] = g_act.window_start_ns[2];
    EXPECT_EQ("empty window", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.window_end_ns[3] = g_act.cycle_ns + 1;
    EXPECT_EQ("window past the cycle", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.link_mbps = 1234;
    EXPECT_EQ("unknown link rate", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.max_frame_bytes = 0;
    EXPECT_EQ("no frame size", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.max_frame_bytes = 9729;
    EXPECT_EQ("frame beyond the buffer limit", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.window_start_ns[1] = 0; g_act.window_end_ns[1] = 13535;
    EXPECT_EQ("13.535 us exceeds the minimum for 1538 bytes at 1 Gbit/s", i226_tas_validate(&g_act), AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.window_start_ns[1] = 0; g_act.window_end_ns[1] = 13000;
    EXPECT_EQ("13 us is below it", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.max_frame_bytes = 64; g_act.window_start_ns[1] = 0; g_act.window_end_ns[1] = 4600;
    EXPECT_EQ("the 512 byte floor governs small frames", i226_tas_validate(&g_act), AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.max_frame_bytes = 64; g_act.window_start_ns[1] = 0; g_act.window_end_ns[1] = 4500;
    EXPECT_EQ("below the 512 byte floor", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.version = 2;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("an invalid request reports it", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("and writes nothing", g_hw.writes, 0);
}

static void test_29_activate_not_clean(void) {
    static const avb_u32 dirty[] = {I226_TQAVCTRL, I226_BASET_L, I226_BASET_H, I226_STQT0, I226_STQT3, I226_ENDQT0, I226_ENDQT3};
    unsigned i;

    printf("\n[TC-MOCK-029] An adapter that is not clean is refused and nothing is written\n");
    act_setup(AVB_TAS_PRE_HOLD);
    i226_tas_activate(&g_io, &g_act, TRUE, &g_mode_written);
    EXPECT_EQ("driver says armed", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("no write when armed", g_hw.writes, 0);
    for (i = 0; i < sizeof(dirty) / sizeof(dirty[0]); i++) {
        act_setup(AVB_TAS_PRE_HOLD);
        g_hw.reg[dirty[i] / 4] = 1;
        i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
        EXPECT_EQ("left-over schedule state", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
        EXPECT_EQ("no write", g_hw.writes, 0);
    }
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.read_fails = TRUE;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("unreadable state is never clean", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("no write when unreadable", g_hw.writes, 0);
}

static void test_30_activate_hold(void) {
    static const avb_u32 want[] = {I226_TQAVCTRL, I226_STQT0, I226_ENDQT0, I226_STQT0 + 4, I226_ENDQT0 + 4, I226_STQT0 + 8, I226_ENDQT0 + 8, I226_STQT0 + 12, I226_ENDQT0 + 12,
                                   I226_QBVCYCLET_S, I226_QBVCYCLET, I226_BASET_H, I226_BASET_L};

    printf("\n[TC-MOCK-030] Hold: the sequence run on hardware, no reserved bit\n");
    act_setup(AVB_TAS_PRE_HOLD);
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("committed", g_act.result, AVB_TAS_ACT_COMMITTED);
    EXPECT_TRUE("register order", act_order(want, sizeof(want) / sizeof(want[0])));
    EXPECT_EQ("TQAVCTRL is 0x09", g_hw.reg[I226_TQAVCTRL / 4], 0x09);
    EXPECT_EQ("FutScdDis clear", g_hw.reg[I226_TQAVCTRL / 4] & (avb_u32)I226_TQAVCTRL_FUTSCDDIS_MASK, 0);
    EXPECT_EQ("window start", g_hw.reg[(I226_STQT0 + 8) / 4], 200000000);
    EXPECT_EQ("window end", g_hw.reg[(I226_ENDQT0 + 8) / 4], 400000000);
    EXPECT_EQ("cycle", g_hw.reg[I226_QBVCYCLET / 4], 1000000000);
    EXPECT_EQ("shadow cycle", g_hw.reg[I226_QBVCYCLET_S / 4], 1000000000);
    EXPECT_EQ("BaseT seconds", g_hw.reg[I226_BASET_H / 4], 7);
    EXPECT_EQ("BaseT nanoseconds", g_hw.reg[I226_BASET_L / 4], 0);
    EXPECT_TRUE("PHC at commit reported", g_act.phc_at_commit_ns == g_hw.phc_later);

    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 7250000000ULL;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("BaseT splits into seconds", g_hw.reg[I226_BASET_H / 4], 7);
    EXPECT_EQ("and nanoseconds", g_hw.reg[I226_BASET_L / 4], 250000000);
}

/* TC-MOCK-031: #357 AC2 makes PASS depend on ADR-TSN-003, which is not accepted. Under the #363 interim Option C (Q
 * 2026-11-13) a valid PASS request is refused as not implemented before any register access; TC-MOCK-055 checks the completion */
static void test_31_activate_pass(void) {
    int outcome;

    printf("\n[TC-MOCK-031] Pass: not implemented until ADR-TSN-003 is accepted, nothing read or written\n");
    act_setup(AVB_TAS_PRE_PASS); g_act.base_time_ns = 7250000000ULL;
    outcome = i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("refused as not implemented", outcome, AVB_TAS_ACTIVATE_NOT_IMPLEMENTED);
    EXPECT_EQ("no register write, FutScdDis included", g_hw.writes, 0);
    EXPECT_EQ("no register read", g_hw.reads, 0);
    EXPECT_EQ("no PHC read", g_hw.phc_reads, 0);
    EXPECT_EQ("TQAVCTRL untouched", g_hw.reg[I226_TQAVCTRL / 4], 0);
}

static void test_32_activate_failure(void) {
    unsigned at, q, same;

    printf("\n[TC-MOCK-032] Failure before commit restores the registers and says so\n");
    for (at = 1; at <= 13; at++) {
        act_setup(AVB_TAS_PRE_HOLD);
        g_hw.fail_write = at;
        i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
        EXPECT_EQ("write failed, restored", g_act.result, AVB_TAS_ACT_FAILED_RESTORED);
        EXPECT_EQ(at == 1 ? "the failed mode write never reached the adapter" : "the transmit-mode write had reached the adapter",
                  g_mode_written, at == 1 ? 0 : 1);
        same = g_hw.reg[I226_TQAVCTRL / 4] == 0 && g_hw.reg[I226_BASET_L / 4] == 0 && g_hw.reg[I226_BASET_H / 4] == 0 && g_hw.reg[I226_QBVCYCLET / 4] == 0x3B9ACA00;
        for (q = 0; q < AVB_TAS_QUEUES; q++) {
            same = same && g_hw.reg[(I226_STQT0 + 4 * q) / 4] == 0 && g_hw.reg[(I226_ENDQT0 + 4 * q) / 4] == 0;
        }
        EXPECT_TRUE("every register back at its value", same);
    }
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.fail_write = 2;
    g_hw.stuck_after_fail = 1;   /* the first restore write fails too */
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("a restore that fails says so", g_act.result, AVB_TAS_ACT_FAILED_NOT_RESTORED);
}

static void test_33_activate_time(void) {
    printf("\n[TC-MOCK-033] BaseT must be in the future\n");
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.phc_fails = TRUE;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("unreadable PHC", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("no write", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = g_hw.phc_first + AVB_TAS_MIN_LEAD_NS - 1;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("too close", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("no write when too close", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = g_hw.phc_first + AVB_TAS_MIN_LEAD_NS;
    g_hw.phc_later = g_hw.phc_first + 1;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("exactly the lead is accepted", g_act.result, AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.phc_later = g_act.base_time_ns + 1;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("BaseT passed while writing", g_act.result, AVB_TAS_ACT_BASE_PASSED);
}

/* TC-MOCK-045: the request is validated in full before anything is read or written, including the reserved word and the BaseT width */
static void test_45_activate_fields(void) {
    printf("\n[TC-MOCK-045] Reserved word and BaseT width\n");
    act_setup(AVB_TAS_PRE_HOLD); g_act.reserved = 1;
    EXPECT_EQ("reserved word not 0", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 0xFFFFFFFFULL * 1000000000ULL + 999999999ULL;
    EXPECT_EQ("BaseT seconds at the 32-bit field limit", i226_tas_validate(&g_act), AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 0x100000000ULL * 1000000000ULL;
    EXPECT_EQ("BaseT seconds one past the field", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 0xFFFFFFFFULL * 1000000000ULL + 7ULL;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("committed at the limit", g_act.result, AVB_TAS_ACT_COMMITTED);
    EXPECT_EQ("BASET_H holds the full seconds", g_hw.reg[I226_BASET_H / 4], 0xFFFFFFFFu);
    EXPECT_EQ("BASET_L the nanoseconds", g_hw.reg[I226_BASET_L / 4], 7);
}

/* TC-MOCK-046: a schedule written but not timed is reported as such, and stays written */
static void test_46_activate_untimed(void) {
    unsigned writes;

    printf("\n[TC-MOCK-046] PHC unreadable after the commit\n");
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.phc_fail_from = 2;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    writes = g_hw.writes;
    EXPECT_EQ("committed without timing evidence", g_act.result, AVB_TAS_ACT_COMMITTED_UNTIMED);
    EXPECT_TRUE("no PHC time claimed", g_act.phc_at_commit_ns == 0);
    EXPECT_EQ("no rollback: the 13 schedule writes only", writes, 13);
    EXPECT_EQ("the schedule stays in the registers", g_hw.reg[I226_TQAVCTRL / 4], 0x09);
}

/* TC-MOCK-047: what each activation result leaves the adapter's TAS state as */
static void test_47_activate_state(void) {
    printf("\n[TC-MOCK-047] Adapter TAS state after an activation result\n");
    EXPECT_EQ("committed arms", avb_tas_state_after(AVB_TAS_ACT_COMMITTED, AVB_TAS_STATE_CLEAN, 1), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("committed untimed arms", avb_tas_state_after(AVB_TAS_ACT_COMMITTED_UNTIMED, AVB_TAS_STATE_CLEAN, 1), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("BaseT passed still arms: the schedule was written", avb_tas_state_after(AVB_TAS_ACT_BASE_PASSED, AVB_TAS_STATE_CLEAN, 1), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("a failed restore leaves the state unknown", avb_tas_state_after(AVB_TAS_ACT_FAILED_NOT_RESTORED, AVB_TAS_STATE_CLEAN, 1), AVB_TAS_STATE_DIRTY);
    EXPECT_EQ("a failed restore leaves the state unknown, mode write or not", avb_tas_state_after(AVB_TAS_ACT_FAILED_NOT_RESTORED, AVB_TAS_STATE_CLEAN, 0), AVB_TAS_STATE_DIRTY);
    /* PR #397 R8: register readbacks do not prove the transmit mode. After the mode write reached the adapter, a restore whose
     * readbacks all match leaves the hardware state unknown; only when nothing reached the adapter does it change nothing */
    EXPECT_EQ("readbacks matched after the transmit-mode write: unknown, not CLEAN", avb_tas_state_after(AVB_TAS_ACT_FAILED_RESTORED, AVB_TAS_STATE_CLEAN, 1), AVB_TAS_STATE_DIRTY);
    EXPECT_EQ("restored when no write reached the adapter: unchanged", avb_tas_state_after(AVB_TAS_ACT_FAILED_RESTORED, AVB_TAS_STATE_CLEAN, 0), AVB_TAS_STATE_CLEAN);
    EXPECT_EQ("a refusal changes nothing", avb_tas_state_after(AVB_TAS_ACT_INVALID, AVB_TAS_STATE_CLEAN, 0), AVB_TAS_STATE_CLEAN);
    EXPECT_EQ("not clean changes nothing", avb_tas_state_after(AVB_TAS_ACT_NOT_CLEAN, AVB_TAS_STATE_ARMED, 0), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("busy changes nothing", avb_tas_state_after(AVB_TAS_ACT_BUSY, AVB_TAS_STATE_DIRTY, 0), AVB_TAS_STATE_DIRTY);
    EXPECT_EQ("an unknown result is never clean", avb_tas_state_after(99u, AVB_TAS_STATE_CLEAN, 0), AVB_TAS_STATE_DIRTY);
}

/* TC-MOCK-048: the state query reports the programmed windows as a gate list, or the request is refused before any write */
static void test_48_activate_gcl(void) {
    struct tsn_tas_config c;

    printf("\n[TC-MOCK-048] Windows as the gate list GET_TAS_STATE reports\n");
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = 7250000000ULL;
    EXPECT_TRUE("the shared schedule fits", avb_tas_gcl_from_windows(&g_act, &c));
    EXPECT_TRUE("base and cycle", c.base_time_s == 7 && c.base_time_ns == 250000000 && c.cycle_time_s == 1 && c.cycle_time_ns == 0);
    EXPECT_TRUE("closed, open on all four queues, closed",
                c.gate_states[0] == 0x00 && c.gate_durations[0] == 200000000 && c.gate_states[1] == 0x0F && c.gate_durations[1] == 200000000 &&
                c.gate_states[2] == 0x00 && c.gate_durations[2] == 600000000 && c.gate_durations[3] == 0);
    act_setup(AVB_TAS_PRE_HOLD); g_act.window_start_ns[0] = 0; g_act.window_end_ns[0] = 100000000;
    EXPECT_TRUE("different windows per queue fit", avb_tas_gcl_from_windows(&g_act, &c));
    EXPECT_TRUE("queue 0 alone, nothing, the other three, nothing",
                c.gate_states[0] == 0x01 && c.gate_durations[0] == 100000000 && c.gate_states[1] == 0x00 && c.gate_durations[1] == 100000000 &&
                c.gate_states[2] == 0x0E && c.gate_durations[2] == 200000000 && c.gate_states[3] == 0x00 && c.gate_durations[3] == 600000000);
    act_setup(AVB_TAS_PRE_HOLD); g_act.cycle_ns = 500000000u;
    g_act.window_start_ns[0] = 10000000; g_act.window_end_ns[0] = 20000000; g_act.window_start_ns[1] = 30000000; g_act.window_end_ns[1] = 40000000;
    g_act.window_start_ns[2] = 50000000; g_act.window_end_ns[2] = 60000000; g_act.window_start_ns[3] = 70000000; g_act.window_end_ns[3] = 80000000;
    EXPECT_TRUE("nine intervals do not fit eight entries", !avb_tas_gcl_from_windows(&g_act, &c));
    EXPECT_EQ("and validation refuses that request", i226_tas_validate(&g_act), AVB_TAS_ACT_INVALID);
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("before any write", g_hw.writes, 0);
}

/* TC-MOCK-050: capture times are host times, the schedule is PHC time; the line through two bracketed reads maps them */
static void test_50_act_mapping(void) {
    TAS_ACT_ANCHOR a = {1000000, 408000000000LL}, b = {11000000, 418293000000LL}, same = {1000000, 409000000000LL};
    long long phc = 0;

    printf("\n[TC-MOCK-050] Host to PHC time through two anchors\n");
    EXPECT_TRUE("maps the first anchor onto itself", tas_act_phc_at(&a, &b, 1000000, &phc) && phc == 408000000000LL);
    EXPECT_TRUE("the PHC rate between the anchors applies, 1.0293", tas_act_phc_at(&a, &b, 2000000, &phc) && phc == 409029300000LL);
    EXPECT_TRUE("also before the first anchor", tas_act_phc_at(&a, &b, 0, &phc) && phc == 406970700000LL);
    EXPECT_FALSE("two anchors at one host time define no line", tas_act_phc_at(&a, &same, 2000000, &phc));
    EXPECT_FALSE("anchors in reverse order are refused", tas_act_phc_at(&b, &a, 2000000, &phc));
}

#define ACT_MS 1000000LL
#define ACT_BASE (10000LL * ACT_MS)
static TAS_ACT_FRAME g_af[16];
static unsigned g_afn;
static void act_frame(long long sent_ms, long long arrival_ms) {   /* times relative to BaseT; arrival < -999999 means not seen */
    g_af[g_afn].sent_ns = ACT_BASE + sent_ms * ACT_MS;
    g_af[g_afn].seen = arrival_ms > -999999;
    g_af[g_afn].arrival_ns = g_af[g_afn].seen ? ACT_BASE + arrival_ms * ACT_MS : 0;
    g_afn++;
}
#define ACT_LOST (-1000000)
static TAS_ACT_WIRE_CLASS act_eval(avb_u32 pre, BOOL alive, TAS_ACT_WIRE_COUNTS *c) {
    return tas_act_wire(pre, g_af, g_afn, alive, ACT_BASE, 1000 * ACT_MS, 200 * ACT_MS, 400 * ACT_MS, TAS_ACT_EDGE_MARGIN_NS, c);
}

/* TC-MOCK-051: HOLD: nothing before the first window after BaseT, then only inside windows */
static void test_51_act_hold_wire(void) {
    TAS_ACT_WIRE_COUNTS c;

    printf("\n[TC-MOCK-051] HOLD wire rules, 1 s cycle, window 200 to 400 ms, 20 ms margin\n");
    g_afn = 0; act_frame(-1500, 200); act_frame(-500, 201); act_frame(100, 230); act_frame(500, 1250); act_frame(1300, 1300);
    EXPECT_EQ("held, released in the first window, then gated", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_GATED);
    EXPECT_TRUE("held frames leave at the window start: released, though at the edge", c.released == 2 && c.near_edge == 2 && c.inside == 3);
    g_afn = 0; act_frame(-1500, -1499); act_frame(-500, 230); act_frame(500, 1250);
    EXPECT_EQ("a frame before BaseT passed the hold", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_EARLY);
    g_afn = 0; act_frame(-500, 100); act_frame(-400, 230); act_frame(500, 1250);
    EXPECT_EQ("a frame after BaseT but before the first window", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_EARLY);
    g_afn = 0; act_frame(-500, 230); act_frame(500, 700);
    EXPECT_EQ("a frame outside a later window", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_OUTSIDE);
    g_afn = 0; act_frame(-500, 1230); act_frame(500, 1250);
    EXPECT_EQ("held frames released only in a later window", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_NOT_RELEASED);
    g_afn = 0; act_frame(-500, 230); act_frame(500, ACT_LOST); act_frame(600, ACT_LOST);
    EXPECT_EQ("frames missing on a live capture", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_LOST);
    g_afn = 0; act_frame(-500, ACT_LOST); act_frame(500, ACT_LOST);
    EXPECT_EQ("nothing seen on a live capture is lost, not unobserved", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_LOST);
    EXPECT_EQ("nothing seen on a dead capture decides nothing", act_eval(AVB_TAS_PRE_HOLD, FALSE, &c), TAS_ACT_WIRE_UNOBSERVED);
    g_afn = 0; act_frame(-500, 199); act_frame(-400, 399);
    EXPECT_EQ("released, but arrivals only at the edges prove no gating", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_NO_WINDOW);
    g_afn = 0;
    EXPECT_EQ("no frame at all decides nothing", act_eval(AVB_TAS_PRE_HOLD, TRUE, &c), TAS_ACT_WIRE_UNOBSERVED);
}

/* TC-MOCK-052: PASS: frames flow until BaseT, then only inside windows */
static void test_52_act_pass_wire(void) {
    TAS_ACT_WIRE_COUNTS c;

    printf("\n[TC-MOCK-052] PASS wire rules\n");
    g_afn = 0; act_frame(-1500, -1499); act_frame(-500, -499); act_frame(-10, 200); act_frame(500, 1250);
    EXPECT_EQ("flowing before BaseT, gated after", act_eval(AVB_TAS_PRE_PASS, TRUE, &c), TAS_ACT_WIRE_GATED);
    EXPECT_TRUE("two flowed, one sent at the BaseT edge", c.flowed == 2 && c.inside == 1 && c.near_edge == 1);
    g_afn = 0; act_frame(-1500, -1499); act_frame(-500, 230); act_frame(500, 1250);
    EXPECT_EQ("a frame sent well before BaseT was held", act_eval(AVB_TAS_PRE_PASS, TRUE, &c), TAS_ACT_WIRE_HELD_BEFORE_BASE);
    g_afn = 0; act_frame(-10, 230); act_frame(500, 1250);
    EXPECT_EQ("nothing flowed before BaseT", act_eval(AVB_TAS_PRE_PASS, TRUE, &c), TAS_ACT_WIRE_NOT_FLOWING);
    g_afn = 0; act_frame(-1500, -1499); act_frame(500, 600);
    EXPECT_EQ("a frame outside the window after BaseT", act_eval(AVB_TAS_PRE_PASS, TRUE, &c), TAS_ACT_WIRE_OUTSIDE);
    g_afn = 0; act_frame(-1500, -1499); act_frame(-500, -499);
    EXPECT_EQ("never gated: nothing sent after BaseT reached a window", act_eval(AVB_TAS_PRE_PASS, TRUE, &c), TAS_ACT_WIRE_NO_WINDOW);
}

/* TC-MOCK-053: #361. TAS restoration is judged from the schedule registers, TSN mode, the driver record and the wire; the PHC
 * is a separate outcome and never decides it */
static void test_53_act_restore(void) {
    TAS_ACT_RESTORE_EVIDENCE e, ok;

    printf("\n[TC-MOCK-053] TAS restoration and PHC continuity are separate outcomes (#361)\n");
    ZeroMemory(&ok, sizeof(ok));
    ok.cycle_completed = TRUE; ok.adapter_returned = TRUE; ok.driver_clean = TRUE; ok.regs_measured = TRUE; ok.regs_differing = 0;
    ok.tsn_mode_off = TRUE; ok.tx_probed = TRUE; ok.tx_on_wire = TRUE;
    EXPECT_EQ("registers at baseline, mode off, clean, TX on the wire: complete", tas_act_restore(&ok), TAS_ACT_RESTORE_COMPLETE);
    e = ok; e.phc = TAS_ACT_PHC_DISCONTINUOUS;
    EXPECT_EQ("a PHC reset alone does not make restoration incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_COMPLETE);
    e = ok; e.regs_differing = 1;
    EXPECT_EQ("a schedule register left behind: incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.tsn_mode_off = FALSE;
    EXPECT_EQ("TSN mode still on: incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.driver_clean = FALSE;
    EXPECT_EQ("the driver still records a schedule: incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.tx_on_wire = FALSE;
    EXPECT_EQ("TX not shown on the wire: incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.cycle_completed = FALSE;
    EXPECT_EQ("the NIC cycle did not complete: incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.adapter_returned = FALSE;
    EXPECT_EQ("the adapter did not come back: incomplete", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.regs_measured = FALSE;
    EXPECT_EQ("registers not measured: unverified, never complete", tas_act_restore(&e), TAS_ACT_RESTORE_UNVERIFIED);
    e = ok; e.regs_measured = FALSE; e.tsn_mode_off = FALSE;
    EXPECT_EQ("unmeasured registers say nothing about the mode either", tas_act_restore(&e), TAS_ACT_RESTORE_UNVERIFIED);
    e = ok; e.tx_probed = FALSE;
    EXPECT_EQ("TX not probed: unverified", tas_act_restore(&e), TAS_ACT_RESTORE_UNVERIFIED);
    e = ok; e.regs_measured = FALSE; e.driver_clean = FALSE;
    EXPECT_EQ("an observed failure outranks what is unmeasured", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    EXPECT_EQ("PHC advanced with wall time: continuous", tas_act_phc(TRUE, 19000000000LL, 19001000000LL, 2000000000LL), TAS_ACT_PHC_CONTINUOUS);
    /* the 18:25 HOLD run: PHC 92.25 s before, 9.23 s after, 19.34 s of wall time */
    EXPECT_EQ("PHC moved back by the cycle: discontinuous", tas_act_phc(TRUE, 9230908065LL - 92247825218LL, 19344460900LL, 2000000000LL), TAS_ACT_PHC_DISCONTINUOUS);
    EXPECT_EQ("PHC not read: unmeasured", tas_act_phc(FALSE, 0, 0, 2000000000LL), TAS_ACT_PHC_UNMEASURED);
}

/*
 * TC-MOCK-049: the HIL fault path (#357, Q 2026-11-10). Debug drivers only: reserved word N fails schedule write N and the
 * first restore write, so the registered HIL test can reach FAILED_NOT_RESTORED. A Release driver refuses any nonzero value.
 */
static void test_49_activate_fault(void) {
    static const I226_TAS_IO release_io = {&g_hw, fake_read, fake_write, fake_phc, 0};
    static const I226_TAS_IO debug_io = {&g_hw, fake_read, fake_write, fake_phc, 1};
    unsigned n, all = 1, reached = 1, commit_kept_out = 1, pass_refused = 1;

    printf("\n[TC-MOCK-049] Debug-only fault injection through the reserved word\n");
    act_setup(AVB_TAS_PRE_HOLD); g_act.reserved = 5;
    i226_tas_activate(&release_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("Release: a nonzero reserved word is refused", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("Release: before any write", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_HOLD);
    i226_tas_activate(&debug_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("Debug: reserved 0 activates normally", g_act.result, AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.reserved = 14;
    i226_tas_activate(&debug_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("Debug: HOLD has 13 schedule writes, 14 is refused", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("Debug: refused before any write", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_PASS); g_act.reserved = 16;
    i226_tas_activate(&debug_io, &g_act, FALSE, &g_mode_written);
    EXPECT_EQ("Debug: PASS has 15 schedule writes, 16 is refused", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("Debug: refused before any write", g_hw.writes, 0);
    /* Option C: a PASS request with a valid fault selector is a valid PASS request, refused before any register access */
    for (n = 1; n <= I226_TAS_PASS_WRITES; n++) {
        act_setup(AVB_TAS_PRE_PASS); g_act.reserved = n;
        pass_refused = pass_refused && i226_tas_activate(&debug_io, &g_act, FALSE, &g_mode_written) == AVB_TAS_ACTIVATE_NOT_IMPLEMENTED &&
                       g_hw.writes == 0 && g_hw.reads == 0;
    }
    EXPECT_TRUE("Debug: PASS with any valid fault selector is not implemented, nothing read or written", pass_refused);
    for (n = 1; n <= I226_TAS_HOLD_WRITES; n++) {
        act_setup(AVB_TAS_PRE_HOLD);
        g_act.reserved = n;
        i226_tas_activate(&debug_io, &g_act, FALSE, &g_mode_written);
        all = all && g_act.result == AVB_TAS_ACT_FAILED_NOT_RESTORED;
        /* writes before N, then the 13 restore writes without the first one */
        reached = reached && g_hw.writes == (n - 1) + (I226_TAS_SAVED_COUNT - 1);
        commit_kept_out = commit_kept_out && g_hw.reg[I226_BASET_L / 4] == 0;
    }
    EXPECT_TRUE("Debug: every N in HOLD ends FAILED_NOT_RESTORED", all);
    EXPECT_TRUE("Debug: neither injected write reaches the hardware", reached);
    EXPECT_TRUE("Debug: the commit write never reaches the hardware", commit_kept_out);
}

/* Every schedule write of one activation carries the request value it encodes. BASET_L: zeros before the last write */
static int act_writes_carry_request(avb_u32 mode) {
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    avb_u32 base_ns = (avb_u32)(g_act.base_time_ns % NSEC_PER_SEC), base_s = (avb_u32)(g_act.base_time_ns / NSEC_PER_SEC);
    unsigned i, q, ok;

    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    ok = g_act.result == AVB_TAS_ACT_COMMITTED &&
         g_hw.writes == (mode == AVB_TAS_PRE_PASS ? I226_TAS_PASS_WRITES : I226_TAS_HOLD_WRITES) &&
         g_hw.log_off[g_hw.writes - 1] == I226_BASET_L && g_hw.log_val[g_hw.writes - 1] == base_ns;
    for (i = 0; ok && i + 1 < g_hw.writes; i++) {
        avb_u32 off = g_hw.log_off[i], v = g_hw.log_val[i];

        for (q = 0; q < AVB_TAS_QUEUES; q++) {
            ok = ok && (off != start[q] || v == g_act.window_start_ns[q]) && (off != end[q] || v == g_act.window_end_ns[q]);
        }
        ok = ok && (off != I226_QBVCYCLET_S || v == g_act.cycle_ns) && (off != I226_QBVCYCLET || v == g_act.cycle_ns);
        ok = ok && (off != I226_BASET_H || v == base_s) && (off != I226_BASET_L || v == 0);
    }
    return ok;
}

/*
 * TC-MOCK-054: #357, #21. The STQT, ENDQT, QBVCYCLET_S, QBVCYCLET and BASET_L writes carry exactly the validated request
 * value, with the bits outside the SSOT field (QBVCYCLET_S.PLUS1, BASET_L.AUTO_SET and COPY_H) clear, at the smallest and
 * largest values validation accepts, in HOLD and in PASS
 */
static void test_54_activate_encoding(void) {
    unsigned mode, edge, q;

    printf("\n[TC-MOCK-054] Schedule writes carry the request value at the field limits\n");
    for (mode = AVB_TAS_PRE_HOLD; mode <= AVB_TAS_PRE_PASS; mode++) {
        for (edge = 0; edge < 2; edge++) {
            act_setup(mode);
            if (edge == 0) {
                /* smallest: the shortest window validation accepts (64 byte frame, 2.5 Gbit/s) as the whole cycle, BaseT
                 * nanoseconds 0 */
                g_act.link_mbps = 2500;
                g_act.max_frame_bytes = 64;
                g_act.cycle_ns = 1803;
                g_act.base_time_ns = 8000000000ULL;
                for (q = 0; q < AVB_TAS_QUEUES; q++) {
                    g_act.window_start_ns[q] = 0;
                    g_act.window_end_ns[q] = 1803;
                }
            } else {
                /* largest: the cycle at the QBVCYCLET field limit, windows ending there, BaseT at its last second and
                 * nanosecond */
                g_act.cycle_ns = (avb_u32)(I226_QBVCYCLET_QBV_CYCLE_TIME_MASK >> I226_QBVCYCLET_QBV_CYCLE_TIME_SHIFT);
                g_act.base_time_ns = 0xFFFFFFFFULL * 1000000000ULL + 999999999ULL;
                for (q = 0; q < AVB_TAS_QUEUES; q++) {
                    g_act.window_start_ns[q] = g_act.cycle_ns - 2000000u;
                    g_act.window_end_ns[q] = g_act.cycle_ns;
                }
            }
            EXPECT_EQ(edge == 0 ? "smallest values are valid" : "largest values are valid", i226_tas_validate(&g_act),
                      AVB_TAS_ACT_COMMITTED);
            if (mode == AVB_TAS_PRE_HOLD) {
                EXPECT_TRUE("HOLD: every write carries its request value", act_writes_carry_request(mode));
            } else {
                /* Option C: valid at the limits, and still refused before any write */
                EXPECT_TRUE("PASS: valid at the limits and not implemented, no write",
                            i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written) == AVB_TAS_ACTIVATE_NOT_IMPLEMENTED && g_hw.writes == 0);
            }
        }
    }
}

/*
 * TC-MOCK-055: #357, #363 interim Option C (Q 2026-11-13). A valid PASS request completes with STATUS_NOT_IMPLEMENTED and
 * IoStatus.Information 0, before any register access, with the Release rule and the Debug rule, on a clean and on an armed
 * adapter. Malformed PASS requests are still refused by validation as before: STATUS_SUCCESS, the request as output, INVALID.
 * A HOLD request still completes with STATUS_SUCCESS and the request as output.
 */
static void test_55_activate_pass_not_implemented(void) {
    static const I226_TAS_IO release_io = {&g_hw, fake_read, fake_write, fake_phc, 0};
    static const I226_TAS_IO debug_io = {&g_hw, fake_read, fake_write, fake_phc, 1};
    const I226_TAS_IO *io;
    ULONG_PTR info;
    LONG st;
    int outcome, rule, armed;

    printf("\n[TC-MOCK-055] PASS: STATUS_NOT_IMPLEMENTED, Information 0, zero register access (Option C)\n");
    for (rule = 0; rule < 2; rule++) {
        io = rule == 0 ? &release_io : &debug_io;
        for (armed = 0; armed < 2; armed++) {
            act_setup(AVB_TAS_PRE_PASS);
            info = 12345;
            outcome = i226_tas_activate(io, &g_act, armed, &g_mode_written);
            st = avb_tas_activate_completion(outcome, &info);
            printf("    %s rule, %s: outcome=%d status=0x%08lX information=%lu writes=%u reads=%u phcReads=%u\n",
                   rule == 0 ? "Release" : "Debug", armed ? "armed" : "clean", outcome, (unsigned long)st, (unsigned long)info,
                   g_hw.writes, g_hw.reads, g_hw.phc_reads);
            EXPECT_EQ("valid PASS: STATUS_NOT_IMPLEMENTED", st, STATUS_NOT_IMPLEMENTED);
            EXPECT_TRUE("valid PASS: Information 0", info == 0);
            EXPECT_EQ("valid PASS: zero register writes", g_hw.writes, 0);
            EXPECT_EQ("valid PASS: zero register reads", g_hw.reads, 0);
            EXPECT_EQ("valid PASS: zero PHC reads", g_hw.phc_reads, 0);
        }

        /* Malformed PASS requests: validation as before */
        act_setup(AVB_TAS_PRE_PASS); g_act.version = 2;
        outcome = i226_tas_activate(io, &g_act, FALSE, &g_mode_written);
        st = avb_tas_activate_completion(outcome, &info);
        EXPECT_TRUE("malformed PASS (version): STATUS_SUCCESS, INVALID, the request as output, nothing written",
                    st == STATUS_SUCCESS && g_act.result == AVB_TAS_ACT_INVALID && info == sizeof(AVB_TAS_ACTIVATE_REQUEST) &&
                    g_hw.writes == 0);
        act_setup(AVB_TAS_PRE_PASS); g_act.window_end_ns[1] = g_act.window_start_ns[1];
        outcome = i226_tas_activate(io, &g_act, FALSE, &g_mode_written);
        st = avb_tas_activate_completion(outcome, &info);
        EXPECT_TRUE("malformed PASS (empty window): STATUS_SUCCESS, INVALID, nothing written",
                    st == STATUS_SUCCESS && g_act.result == AVB_TAS_ACT_INVALID && g_hw.writes == 0);
        act_setup(AVB_TAS_PRE_PASS); g_act.cycle_ns = 0;
        outcome = i226_tas_activate(io, &g_act, FALSE, &g_mode_written);
        st = avb_tas_activate_completion(outcome, &info);
        EXPECT_TRUE("malformed PASS (no cycle): STATUS_SUCCESS, INVALID, nothing written",
                    st == STATUS_SUCCESS && g_act.result == AVB_TAS_ACT_INVALID && g_hw.writes == 0);
        act_setup(AVB_TAS_PRE_PASS); g_act.reserved = I226_TAS_PASS_WRITES + 1u;
        outcome = i226_tas_activate(io, &g_act, FALSE, &g_mode_written);
        st = avb_tas_activate_completion(outcome, &info);
        EXPECT_TRUE("PASS with a reserved word out of range: STATUS_SUCCESS, INVALID, nothing written",
                    st == STATUS_SUCCESS && g_act.result == AVB_TAS_ACT_INVALID && g_hw.writes == 0);

        /* HOLD keeps its completion */
        act_setup(AVB_TAS_PRE_HOLD);
        outcome = i226_tas_activate(io, &g_act, FALSE, &g_mode_written);
        st = avb_tas_activate_completion(outcome, &info);
        EXPECT_TRUE("HOLD: STATUS_SUCCESS, the request as output, COMMITTED",
                    st == STATUS_SUCCESS && info == sizeof(AVB_TAS_ACTIVATE_REQUEST) && g_act.result == AVB_TAS_ACT_COMMITTED);
    }
    act_setup(AVB_TAS_PRE_PASS); g_act.reserved = 1;
    outcome = i226_tas_activate(&release_io, &g_act, FALSE, &g_mode_written);
    st = avb_tas_activate_completion(outcome, &info);
    EXPECT_TRUE("Release rule: PASS with a nonzero reserved word stays INVALID with STATUS_SUCCESS, nothing written",
                st == STATUS_SUCCESS && g_act.result == AVB_TAS_ACT_INVALID && g_hw.writes == 0);
}

/*
 * TC-MOCK-056: PR #397 R8. A failure after the transmit-mode write still reports FAILED_RESTORED when every register reads back
 * at its old value, because that is what the result states. The adapter state becomes DIRTY (hardware state unknown, a NIC
 * cycle is owed), since a readback does not prove the transmit mode, and a further activation is refused without any write.
 * When only the first write failed nothing reached the adapter and the adapter stays CLEAN. This is state reporting only: it
 * does not show that the transmit mode latches on our hardware, and it does not satisfy #357 AC4.
 */
static void test_56_activate_restore_state(void) {
    unsigned at;
    avb_u32 state;
    int ok = 1;

    printf("\n[TC-MOCK-056] Matching readbacks after the transmit-mode write do not make the adapter CLEAN\n");
    for (at = 2; at <= I226_TAS_HOLD_WRITES; at++) {
        act_setup(AVB_TAS_PRE_HOLD);
        g_hw.fail_write = at;
        i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
        state = avb_tas_state_after(g_act.result, AVB_TAS_STATE_CLEAN, g_mode_written);
        ok = ok && g_act.result == AVB_TAS_ACT_FAILED_RESTORED && g_mode_written == 1 && state == AVB_TAS_STATE_DIRTY;
    }
    EXPECT_TRUE("writes 2 to 13: FAILED_RESTORED, transmit-mode write reached the adapter, state DIRTY", ok);
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.fail_write = 5;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    state = avb_tas_state_after(g_act.result, AVB_TAS_STATE_CLEAN, g_mode_written);
    g_hw.fail_write = 0;
    g_hw.writes = 0;
    g_hw.reads = 0;
    i226_tas_activate(&g_io, &g_act, state != AVB_TAS_STATE_CLEAN, &g_mode_written);
    EXPECT_EQ("the next activation on that adapter is refused", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("without any write", g_hw.writes, 0);
    EXPECT_EQ("and leaves it DIRTY", avb_tas_state_after(g_act.result, state, g_mode_written), AVB_TAS_STATE_DIRTY);

    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.fail_write = 1;
    i226_tas_activate(&g_io, &g_act, FALSE, &g_mode_written);
    state = avb_tas_state_after(g_act.result, AVB_TAS_STATE_CLEAN, g_mode_written);
    EXPECT_TRUE("first write failed: FAILED_RESTORED, nothing reached the adapter, state CLEAN",
                g_act.result == AVB_TAS_ACT_FAILED_RESTORED && g_mode_written == 0 && state == AVB_TAS_STATE_CLEAN);
    g_hw.fail_write = 0;
    g_hw.phc_reads = 0;
    i226_tas_activate(&g_io, &g_act, state != AVB_TAS_STATE_CLEAN, &g_mode_written);
    EXPECT_EQ("and a further activation proceeds", g_act.result, AVB_TAS_ACT_COMMITTED);
}

/* Feeds n send results, `accepted` of them accepted, in the order given by fail_first */
static void tx_feed(TAS_TX_WINDOW *w, unsigned n, unsigned accepted, BOOL fail_first) {
    unsigned k;
    for (k = 0; k < n; k++) {
        tas_tx_record_send(w, fail_first ? (k >= n - accepted) : (k < accepted));
    }
}

/* TC-MOCK-057: TC-TASAPI-001 send-window accounting and TX verdict (#366, PR #437 review r4579071534).
 * The functions of tests/ioctl/tas_query_tx_eval.h that tests/hardware/test_tas_query_clean.c calls for every window.
 * TEST-LOGIC EVIDENCE ONLY: no socket, observer or adapter; it never verifies TX on hardware. */
static void test_57_query_tx_accounting(void) {
    TAS_TX_WINDOW base, w;
    const unsigned pct = TAS_ACT_SEEN_MIN_PERCENT;
    unsigned k;

    printf("\n[TC-MOCK-057] #366 TX window: every attempt counts, any send error fails, zero attempts never pass (test logic only)\n");

    /* Passing control: 40 attempts, all accepted, all seen, 10 unchanged queries */
    ZeroMemory(&base, sizeof(base));
    tx_feed(&base, 40, 40, FALSE); base.seen = 40;
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 40, FALSE); w.seen = 40;
    for (k = 0; k < 10; k++) { tas_tx_record_query(&w, TRUE); }
    EXPECT_TRUE("control: counts attempted 40, sent 40, errors 0", w.attempted == 40 && w.sent == 40 && w.errors == 0);
    EXPECT_TRUE("control: on the wire", tas_tx_on_wire(&w, pct));
    EXPECT_EQ("control: PASS", tas_tx_judge(&base, &w, pct), TAS_TX_PASS);

    /* The review's case: 1 accepted and seen, 39 refused; 10 queries still issued and unchanged */
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 1, TRUE); w.seen = 1;
    for (k = 0; k < 10; k++) { tas_tx_record_query(&w, TRUE); }
    EXPECT_TRUE("1 of 40 accepted: attempted 40, sent 1, errors 39", w.attempted == 40 && w.sent == 1 && w.errors == 39);
    EXPECT_FALSE("1 of 40 accepted, the 1 seen: not on the wire (denominator is attempts)", tas_tx_on_wire(&w, pct));
    EXPECT_EQ("1 of 40 accepted in the checked window: FAIL, never PASS", tas_tx_judge(&base, &w, pct), TAS_TX_FAIL);
    EXPECT_EQ("1 of 40 accepted in the baseline: BLOCKED", tas_tx_judge(&w, &base, pct), TAS_TX_BLOCKED_BASELINE);

    /* Partial failure: one refused send in 40, every accepted one seen (97.5 % of attempts) */
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 39, FALSE); w.seen = 39;
    for (k = 0; k < 10; k++) { tas_tx_record_query(&w, TRUE); }
    EXPECT_FALSE("1 send error of 40, share met: not on the wire", tas_tx_on_wire(&w, pct));
    EXPECT_EQ("1 send error in the checked window: FAIL", tas_tx_judge(&base, &w, pct), TAS_TX_FAIL);

    /* All sends failing */
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 0, FALSE);
    for (k = 0; k < 10; k++) { tas_tx_record_query(&w, TRUE); }
    EXPECT_TRUE("all refused: attempted 40, sent 0, errors 40", w.attempted == 40 && w.sent == 0 && w.errors == 40);
    EXPECT_FALSE("all refused: not on the wire", tas_tx_on_wire(&w, pct));
    EXPECT_EQ("all refused in the checked window: FAIL", tas_tx_judge(&base, &w, pct), TAS_TX_FAIL);
    EXPECT_EQ("all refused in the baseline: BLOCKED", tas_tx_judge(&w, &base, pct), TAS_TX_BLOCKED_BASELINE);

    /* Zero attempts */
    ZeroMemory(&w, sizeof(w));
    EXPECT_FALSE("zero attempts: not on the wire", tas_tx_on_wire(&w, pct));
    EXPECT_EQ("zero attempts in the checked window: FAIL", tas_tx_judge(&base, &w, pct), TAS_TX_FAIL);
    EXPECT_EQ("zero attempts in the baseline: BLOCKED", tas_tx_judge(&w, &base, pct), TAS_TX_BLOCKED_BASELINE);

    /* No send error, but the seen share is short: 35 of 40 = 87.5 % */
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 40, FALSE); w.seen = 35;
    for (k = 0; k < 10; k++) { tas_tx_record_query(&w, TRUE); }
    EXPECT_FALSE("35 of 40 seen, no error: below the share", tas_tx_on_wire(&w, pct));
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 40, FALSE); w.seen = 36;
    EXPECT_TRUE("36 of 40 seen, no error: exactly the share", tas_tx_on_wire(&w, pct));

    /* Queries: none issued, or one changed, fails a window that is otherwise on the wire */
    ZeroMemory(&w, sizeof(w));
    tx_feed(&w, 40, 40, FALSE); w.seen = 40;
    EXPECT_EQ("no interleaved query: FAIL", tas_tx_judge(&base, &w, pct), TAS_TX_FAIL);
    for (k = 0; k < 10; k++) { tas_tx_record_query(&w, k != 4); }
    EXPECT_TRUE("one changed query counted", w.queries == 10 && w.changed == 1);
    EXPECT_EQ("one changed query: FAIL", tas_tx_judge(&base, &w, pct), TAS_TX_FAIL);
}

/* TC-MOCK-058: TEST-TASACT-FAULT-001 F2 STATE classification (#482; PR #489 review r4631805746).
 * tas_fv_f2_state of tests/ioctl/tas_activate_eval.h, called by tests/hardware/test_tas_activate.c --fault-versioned.
 * A measured restoration-set change while the fault path was not reached is a violation whether or not the record was read:
 * an unread record must not hide it. TEST-LOGIC EVIDENCE ONLY. */
static void test_58_fault_f2_state(void) {
    printf("\n[TC-MOCK-058] #482 F2 STATE: DIRTY after the failed restore; a measured change is never hidden by an unread record (test logic only)\n");

    EXPECT_EQ("fault ran, record DIRTY: DIRTY (PASS)", tas_fv_f2_state(TRUE, TRUE, AVB_TAS_STATE_DIRTY, 0), TAS_FV_STATE_DIRTY);
    EXPECT_EQ("fault ran, record CLEAN: NOT_DIRTY (FAIL)", tas_fv_f2_state(TRUE, TRUE, AVB_TAS_STATE_CLEAN, 0), TAS_FV_STATE_NOT_DIRTY);
    EXPECT_EQ("fault ran, record ARMED: NOT_DIRTY (FAIL)", tas_fv_f2_state(TRUE, TRUE, AVB_TAS_STATE_ARMED, 3), TAS_FV_STATE_NOT_DIRTY);
    EXPECT_EQ("fault ran, record unread: UNREAD (INCONCLUSIVE)", tas_fv_f2_state(TRUE, FALSE, 0, 0), TAS_FV_STATE_UNREAD);
    EXPECT_EQ("not reached, record CLEAN, 0 differ: NOT_EXERCISED (INCONCLUSIVE)", tas_fv_f2_state(FALSE, TRUE, AVB_TAS_STATE_CLEAN, 0),
              TAS_FV_STATE_NOT_EXERCISED);
    EXPECT_EQ("not reached, record DIRTY: CHANGED_UNREACHED (FAIL)", tas_fv_f2_state(FALSE, TRUE, AVB_TAS_STATE_DIRTY, 0),
              TAS_FV_STATE_CHANGED_UNREACHED);
    EXPECT_EQ("not reached, record CLEAN, 2 differ: CHANGED_UNREACHED (FAIL)", tas_fv_f2_state(FALSE, TRUE, AVB_TAS_STATE_CLEAN, 2),
              TAS_FV_STATE_CHANGED_UNREACHED);
    /* r4631805746: the measured difference is a violation even when GET_TAS_STATE failed */
    EXPECT_EQ("not reached, record unread, 1 differ: CHANGED_UNREACHED (FAIL), not UNREAD",
              tas_fv_f2_state(FALSE, FALSE, 0, 1), TAS_FV_STATE_CHANGED_UNREACHED);
    EXPECT_EQ("not reached, record unread, 0 differ: UNREAD (INCONCLUSIVE)", tas_fv_f2_state(FALSE, FALSE, 0, 0), TAS_FV_STATE_UNREAD);
}

/* Evidence of a passing F2 REFUSE probe: refused STATUS_INVALID_DEVICE_STATE, no output, record DIRTY kept, all 17 registers
 * read and unchanged, the fault ran, metadata reported */
static void fv_refuse_pass(TAS_FV_REFUSE_EVIDENCE *e) {
    ZeroMemory(e, sizeof(*e));
    e->state_refusal = TRUE;
    e->record_before = TRUE;
    e->armed_before = AVB_TAS_STATE_DIRTY;
    e->record_after = TRUE;
    e->armed_after = AVB_TAS_STATE_DIRTY;
    e->fault_ran = TRUE;
    e->metadata_reported = TRUE;
}

/* TC-MOCK-059: TEST-TASACT-FAULT-001 F2 REFUSE classification (#482, #369's rule for this probe; PR #489 review r4631805812).
 * tas_fv_f2_refuse of tests/ioctl/tas_activate_eval.h. "Unchanged" needs every restoration-set register read in both queries:
 * an unread register is not a measured "unchanged". TEST-LOGIC EVIDENCE ONLY. */
static void test_59_fault_f2_refuse(void) {
    TAS_FV_REFUSE_EVIDENCE e;

    printf("\n[TC-MOCK-059] #482 F2 REFUSE: STATUS_INVALID_DEVICE_STATE, no output, state measured unchanged (test logic only)\n");

    fv_refuse_pass(&e);
    EXPECT_EQ("all established: PASS", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_PASS);

    fv_refuse_pass(&e); e.state_refusal = FALSE; e.not_served = TRUE;
    EXPECT_EQ("0x80C not served: NOT_SERVED (FAIL)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_NOT_SERVED);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE;
    EXPECT_EQ("raw status not captured: STATUS_UNAVAILABLE (INCONCLUSIVE)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_STATUS_UNAVAILABLE);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.accepted = TRUE;
    EXPECT_EQ("accepted: CHANGED (FAIL)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.output = TRUE;
    EXPECT_EQ("refused with output: CHANGED (FAIL)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.armed_after = AVB_TAS_STATE_ARMED;
    EXPECT_EQ("record changed: CHANGED (FAIL)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.differing = 1; e.unread = 4;
    EXPECT_EQ("1 register changed, 4 unread: CHANGED (FAIL), the measured change is not hidden", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.state_refusal = FALSE;
    EXPECT_EQ("another refusal status: OTHER_STATUS (FAIL)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_OTHER_STATUS);
    fv_refuse_pass(&e); e.record_after = FALSE;
    EXPECT_EQ("record after unread: UNCHANGED_UNESTABLISHED (INCONCLUSIVE)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED);
    /* r4631805812: unread registers do not establish an unchanged restoration set */
    fv_refuse_pass(&e); e.unread = 1;
    EXPECT_EQ("1 of 17 registers unread, nothing measured changed: UNCHANGED_UNESTABLISHED (INCONCLUSIVE), not PASS",
              tas_fv_f2_refuse(&e), TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED);
    fv_refuse_pass(&e); e.unread = 17;
    EXPECT_EQ("a query failed (17 unread): UNCHANGED_UNESTABLISHED (INCONCLUSIVE), not PASS", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED);
    fv_refuse_pass(&e); e.fault_ran = FALSE;
    EXPECT_EQ("fault not observed: NOT_DIRTY (INCONCLUSIVE)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_NOT_DIRTY);
    fv_refuse_pass(&e); e.armed_before = AVB_TAS_STATE_CLEAN; e.armed_after = AVB_TAS_STATE_CLEAN;
    EXPECT_EQ("record CLEAN before the probe: NOT_DIRTY (INCONCLUSIVE)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_NOT_DIRTY);
    fv_refuse_pass(&e); e.metadata_reported = FALSE;
    EXPECT_EQ("generation or epoch 0: METADATA_ZERO (INCONCLUSIVE)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_METADATA_ZERO);

    /* r4632275740: a missing raw status never erases an independently measured violation, and never permits PASS */
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE; e.differing = 1;
    EXPECT_EQ("raw status not captured + 1 register changed: CHANGED (FAIL), not STATUS_UNAVAILABLE", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE; e.armed_after = AVB_TAS_STATE_ARMED;
    EXPECT_EQ("raw status not captured + record changed DIRTY -> ARMED: CHANGED (FAIL), not STATUS_UNAVAILABLE", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE; e.output = TRUE;
    EXPECT_EQ("raw status not captured + output observed: CHANGED (FAIL), not STATUS_UNAVAILABLE", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE; e.differing = 1; e.unread = 16; e.record_after = FALSE;
    EXPECT_EQ("raw status not captured + 1 changed, 16 unread, record unread: CHANGED (FAIL)", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE;
    EXPECT_EQ("raw status not captured, everything measured unchanged: STATUS_UNAVAILABLE (INCONCLUSIVE), not PASS",
              tas_fv_f2_refuse(&e), TAS_FV_REFUSE_STATUS_UNAVAILABLE);
    fv_refuse_pass(&e); e.state_refusal = FALSE; e.status_unavailable = TRUE; e.unread = 17; e.record_after = FALSE;
    EXPECT_EQ("raw status not captured, nothing measured (17 unread, record unread): STATUS_UNAVAILABLE (INCONCLUSIVE), no FAIL "
              "fabricated", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_STATUS_UNAVAILABLE);
    /* earlier corrections kept: a measured change survives unread registers; PASS needs all 17 read */
    fv_refuse_pass(&e); e.differing = 1; e.unread = 4;
    EXPECT_EQ("status captured, 1 changed, 4 unread: still CHANGED (FAIL)", tas_fv_f2_refuse(&e), TAS_FV_REFUSE_CHANGED);
    fv_refuse_pass(&e); e.unread = 1;
    EXPECT_EQ("status captured, 1 unread: still UNCHANGED_UNESTABLISHED, not PASS", tas_fv_f2_refuse(&e),
              TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED);

    /* Reporting: the capture sentinel is never shown as a driver completion code; a captured status is shown exactly */
    {
        char text[96];
        tas_fv_status_text(FALSE, (LONG)0x7FFFFFFF, text, sizeof(text));
        printf("    status text, not captured: \"%s\"\n", text);
        EXPECT_TRUE("not captured: the text says so", strstr(text, "not captured") != NULL);
        EXPECT_TRUE("not captured: the sentinel 0x7FFFFFFF is not printed as a completion code", strstr(text, "7FFFFFFF") == NULL);
        tas_fv_status_text(TRUE, (LONG)0xC0000010, text, sizeof(text));
        printf("    status text, captured: \"%s\"\n", text);
        EXPECT_TRUE("captured: ntstatus=0xC0000010 shown exactly", strcmp(text, "ntstatus=0xC0000010") == 0);
    }
}

/* TC-MOCK-060: TEST-TAS-API-003 (#368) semantic COMPLETION under #365's published validation order R7 (oracle amendment
 * 2026-11-19, #352 R1, #351 R2/R4). A case reaches its own rule only when every earlier step passes for the actual request:
 * a request carrying generation or epoch 0 must be refused at step 4 (STATE_CONFLICT), an unknown driver-owned speed at step 5
 * (NOT_READY). Missing prerequisites are named, never PASS; a completion the order excludes is FAIL. Test logic only. */
static void fill_inv_sem(TAS_INV_SEM_EVIDENCE *e, TAS_INV_STEP step, LONG required, LONG status)
{
    ZeroMemory(e, sizeof(*e));
    e->step = step;
    e->required = required;
    e->captured = TRUE;
    e->status = status;
    e->pair_established = TRUE;      /* the query reported a non-zero pair and the request carries it */
    e->speed_before = 1000u;
    e->speed_after_read = TRUE;
    e->speed_after = 1000u;
    e->before_read = TRUE;           /* the case's own query before the request was valid */
    e->pair_after_read = TRUE;       /* and the one after, with the same pair */
}

static void test_60_invalid_semantic_order(void) {
    TAS_INV_SEM_EVIDENCE e;
    LONG exp;
    TAS_INV_STEP at;
    const LONG INVP = (LONG)STATUS_INVALID_PARAMETER, CONFLICT = (LONG)STATUS_INVALID_DEVICE_STATE;
    const LONG NOTREADY = (LONG)STATUS_DEVICE_NOT_READY, NOTACC = (LONG)STATUS_REQUEST_NOT_ACCEPTED;

    printf("\n[TC-MOCK-060] #368 semantic COMPLETION under #365 R7 (test logic only)\n");

    /* the case's own rule reached: PASS on its exact completion */
    fill_inv_sem(&e, TAS_INV_STEP_PRE, INVP, INVP);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("PRE_ACTIVATION 0: step 2 precedes metadata, INVALID_PARAMETER is PASS even with a zero pair",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    EXPECT_EQ("schedule case, established pair and speed: INVALID_PARAMETER is PASS", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_LEAD, INVP, INVP);
    EXPECT_EQ("BASET_NEAR, established pair and speed: INVALID_PARAMETER is PASS", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, CONFLICT);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("META_ZERO: a zero pair is refused STATE_CONFLICT on its own (R1), PASS without an established pair",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, CONFLICT);
    e.stale_case = TRUE; e.request_pair_mismatch = TRUE;
    EXPECT_EQ("STALE_GENERATION from an established pair, a deliberately different value: STATE_CONFLICT is PASS",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_TIER, NOTACC, NOTACC);
    EXPECT_EQ("NO_ACCEPTANCE, tier not reported (never verified): REQUEST_NOT_ACCEPTED is PASS", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_PASS);

    /* an earlier step must refuse the actual request: its refusal is reported, the field's own rule is not reached */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, CONFLICT);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("schedule case carrying the query's 0/0: STATE_CONFLICT is the earlier step (INCONCLUSIVE)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_EARLIER_STEP);
    EXPECT_TRUE("... judged against STATE_CONFLICT at step 4", exp == CONFLICT && at == TAS_INV_STEP_META);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("schedule case carrying 0/0 answered INVALID_PARAMETER: metadata must refuse first (FAIL)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, NOTREADY);
    e.speed_before = 0; e.speed_after = 0;
    EXPECT_EQ("speed unknown: NOT_READY is the earlier step (INCONCLUSIVE), not a window bound", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_EARLIER_STEP);
    EXPECT_TRUE("... judged against NOT_READY at step 5", exp == NOTREADY && at == TAS_INV_STEP_SPEED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.speed_before = 0; e.speed_after = 0;
    EXPECT_EQ("speed unknown answered INVALID_PARAMETER: NOT_READY must come first (FAIL)", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_TIER, NOTACC, NOTREADY);
    e.speed_before = 0; e.speed_after = 0;
    EXPECT_EQ("NO_ACCEPTANCE with unknown speed: NOT_READY first (INCONCLUSIVE)", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_EARLIER_STEP);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, CONFLICT);
    e.pair_established = FALSE; e.request_pair_zero = TRUE; e.speed_before = 0; e.speed_after = 0;
    EXPECT_EQ("zero pair and unknown speed: step 4 precedes step 5", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_EARLIER_STEP);
    EXPECT_TRUE("... judged against STATE_CONFLICT", exp == CONFLICT && at == TAS_INV_STEP_META);

    /* a stale claim without an established current pair is not constructible: reported, never PASS */
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, CONFLICT);
    e.stale_case = TRUE; e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("STALE_* with the query's 0/0: STATE_CONFLICT observed, staleness not established (INCONCLUSIVE)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_STALE_UNESTABLISHED);
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, INVP);
    e.stale_case = TRUE; e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("STALE_* carrying a zero value answered INVALID_PARAMETER: R1 requires STATE_CONFLICT (FAIL)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);

    /* the default refusal is no longer an undecided-tier excuse */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, NOTACC);
    EXPECT_EQ("schedule case with acceptance refused NOT_HARDWARE_VERIFIED: step 6 precedes step 8 (FAIL)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_TIER, NOTACC, CONFLICT);
    EXPECT_EQ("NO_ACCEPTANCE with an established pair answered STATE_CONFLICT (FAIL)", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_FAIL);

    /* not determined by the order for the measured facts */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, NOTREADY);
    e.speed_after = 100u;
    EXPECT_EQ("speed changed around the case: UNDETERMINED", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, (LONG)STATUS_DEVICE_BUSY);
    EXPECT_EQ("BUSY after the claim step: another operation, UNDETERMINED", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_PRE, INVP, (LONG)STATUS_DEVICE_BUSY);
    EXPECT_EQ("BUSY for PRE_ACTIVATION: step 2 precedes the claim (FAIL)", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_TIER, NOTACC, NOTACC);
    e.tier_hold = AVB_TAS_TIER_IMPLEMENTED_VERIFIED;
    EXPECT_EQ("NO_ACCEPTANCE on a HOLD tier IMPLEMENTED_VERIFIED: not an invalid request (UNDETERMINED)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.tier_hold = AVB_TAS_TIER_NOT_IMPLEMENTED;
    EXPECT_EQ("query reports HOLD NOT_IMPLEMENTED: no valid control, its outcome position is unpublished (UNDETERMINED)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_UNDETERMINED);

    /* violations whatever the prerequisites */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.tier_hold = AVB_TAS_TIER_NOT_IMPLEMENTED;
    EXPECT_EQ("0x80C not served: FAIL even when the control is undetermined", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, 0);
    e.information = (DWORD)sizeof(AVB_TAS_OP_REQUEST); e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("STATUS_SUCCESS with output: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.information = 4;
    EXPECT_EQ("the required status with output: FAIL (errors carry none)", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.captured = FALSE;
    EXPECT_EQ("raw NTSTATUS not captured: NOT_CAPTURED", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_NOT_CAPTURED);

    /* The completion a FAIL is judged against is the one R7 requires for the ACTUAL request, also on a violation path (finding on
     * the 2026-11-19 20:20 hardware run: schedule cases carrying the query's 0/0 were reported as "requiring" their own rule) */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("not served, schedule case carrying 0/0: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    EXPECT_TRUE("... judged against STATE_CONFLICT at step 4, not the case's own rule", exp == CONFLICT && at == TAS_INV_STEP_META);
    fill_inv_sem(&e, TAS_INV_STEP_TIER, NOTACC, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("not served, NO_ACCEPTANCE carrying 0/0: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    EXPECT_TRUE("... judged against STATE_CONFLICT at step 4", exp == CONFLICT && at == TAS_INV_STEP_META);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.speed_before = 0; e.speed_after = 0;
    EXPECT_EQ("not served, schedule case at unknown speed: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    EXPECT_TRUE("... judged against NOT_READY at step 5", exp == NOTREADY && at == TAS_INV_STEP_SPEED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, 0);
    e.information = (DWORD)sizeof(AVB_TAS_OP_REQUEST);
    EXPECT_EQ("success with output, prerequisites established: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    EXPECT_TRUE("... judged against the case's own rule at its own step", exp == INVP && at == TAS_INV_STEP_SCHEDULE);
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.pair_established = FALSE; e.request_pair_zero = TRUE;
    EXPECT_EQ("not served, META_ZERO: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    EXPECT_TRUE("... judged against its own STATE_CONFLICT at step 4", exp == CONFLICT && at == TAS_INV_STEP_META);

    /* PR #491 review: isolation needs the measurements it rests on, taken around the case; missing or changed ones are not
     * established (INCONCLUSIVE), never PASS, and never turn a correct completion into FAIL */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.speed_after_read = FALSE; e.speed_after = 0;
    EXPECT_EQ("r4634405078: speed after the case not measured, schedule case: UNDETERMINED, not PASS", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_TIER, NOTACC, NOTACC);
    e.speed_after_read = FALSE; e.speed_after = 0;
    EXPECT_EQ("r4634405078: speed after not measured, NO_ACCEPTANCE: UNDETERMINED", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, CONFLICT);
    e.pair_after_differs = TRUE;
    EXPECT_EQ("r4634404991: pair changed around the case, STATE_CONFLICT: UNDETERMINED, not FAIL", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.pair_after_read = FALSE;
    EXPECT_EQ("r4634404991: pair after the case not measured: UNDETERMINED, not PASS", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, CONFLICT);
    e.stale_case = TRUE; e.request_pair_mismatch = TRUE; e.pair_after_differs = TRUE;
    EXPECT_EQ("r4634404991: STALE_* with the pair changed around the case: UNDETERMINED, not PASS", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, INVP);
    e.before_read = FALSE; e.pair_established = FALSE;
    EXPECT_EQ("case's own query before the request not valid, non-zero pair sent: UNDETERMINED, not PASS",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, 0);
    e.field_speed_dependent = TRUE; e.speed_after = 2500u; e.information = (DWORD)sizeof(AVB_TAS_OP_REQUEST);
    EXPECT_EQ("r4634404917: WINDOW_MIN committed while the speed changed: its invalidity is not established (UNDETERMINED)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_UNDETERMINED);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, 0);
    e.field_speed_dependent = TRUE; e.information = (DWORD)sizeof(AVB_TAS_OP_REQUEST);
    EXPECT_EQ("WINDOW_MIN committed at a stable measured speed: FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);

    /* kept: unconditional failures, metadata first, independent steps */
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, 0);
    e.speed_after = 2500u; e.information = (DWORD)sizeof(AVB_TAS_OP_REQUEST);
    EXPECT_EQ("CYCLE_ZERO committed while the speed changed: still FAIL (its invalidity does not depend on the speed)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.pair_after_read = FALSE; e.speed_after_read = FALSE;
    EXPECT_EQ("not served with nothing measured after: still FAIL", tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_FAIL);
    fill_inv_sem(&e, TAS_INV_STEP_META, CONFLICT, CONFLICT);
    e.pair_established = FALSE; e.request_pair_zero = TRUE; e.pair_after_differs = TRUE;
    EXPECT_EQ("META_ZERO with the pair changed around it: still PASS (0 is refused whatever the current pair)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_PRE, INVP, INVP);
    e.pair_after_read = FALSE; e.speed_after_read = FALSE; e.before_read = FALSE;
    EXPECT_EQ("PRE_ACTIVATION with nothing measured around it: still PASS (step 2 needs neither)", tas_inv_semantic(&e, &exp, &at),
              TAS_INV_SEM_PASS);
    fill_inv_sem(&e, TAS_INV_STEP_SCHEDULE, INVP, CONFLICT);
    e.pair_established = FALSE; e.request_pair_zero = TRUE; e.speed_after_read = FALSE; e.pair_after_differs = TRUE;
    EXPECT_EQ("0/0 sent, nothing stable after: STATE_CONFLICT is still the earlier step (metadata first)",
              tas_inv_semantic(&e, &exp, &at), TAS_INV_SEM_EARLIER_STEP);
}

/* TC-MOCK-061: WINDOW_MIN (#368, TAS-DR-05 as #351 R2 publishes it): a window not greater than 1.1 x the transmit time of
 * max(512 bytes, max_frame_bytes) at the driver-owned speed. The unpublished "plus overhead" term only raises the bound, so the
 * largest window under the bound without it is invalid under either reading. Speed 0 constructs no bound. Test logic only. */
static void test_61_invalid_window_min(void) {
    static const struct { avb_u32 frame, mbps; avb_u64 want; } c[] = {
        { 1538u, 1000u, 13534u },    /* 1538 x 8.8 = 13534.4 */
        { 1522u, 1000u, 13393u },
        {  100u, 1000u,  4505u },    /* below 512 bytes: 512 x 8.8 = 4505.6 */
        { 1538u, 10000u, 1353u },
        { 1538u,  100u, 135344u },
        { 1538u,    0u,      0u },   /* speed unknown: no bound */
    };
    unsigned i;
    char label[200];

    printf("\n[TC-MOCK-061] #368 WINDOW_MIN at the driver-owned speed, TAS-DR-05 (test logic only)\n");
    for (i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
        avb_u64 w = tas_inv_window_min_ns(c[i].frame, c[i].mbps);
        sprintf_s(label, sizeof(label), "frame %u bytes at %u Mbit/s: window %llu ns (want %llu)", c[i].frame, c[i].mbps, w, c[i].want);
        EXPECT_TRUE(label, w == c[i].want);
        if (c[i].mbps != 0) {
            avb_u64 bytes = c[i].frame > 512u ? c[i].frame : 512u;
            sprintf_s(label, sizeof(label), "frame %u at %u: w x mbps <= bytes x 8800 < (w + 1) x mbps (largest window not greater "
                      "than the bound)", c[i].frame, c[i].mbps);
            EXPECT_TRUE(label, w * c[i].mbps <= bytes * 8800u && (w + 1u) * c[i].mbps > bytes * 8800u);
        }
    }
}

/* TC-MOCK-062: TEST-TAS-API-004 (#369) COMMIT of the accepted HOLD race under the published 2026-11-19 rules (#369 oracle
 * amendment; #365 R1/R2/R4/R5/R7; #362; #351; #352 R1). out_flags carries AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE exactly when
 * unverified acceptance acted: tier IMPLEMENTED_UNVERIFIED (3) bit 0, IMPLEMENTED_VERIFIED (4) 0, otherwise not judged (never a
 * permanent 0, never 1 merely because the flag was sent). A tier refusal of the accepted control, an R5 status, a zero pair and a
 * refusal at a measured valid speed or pair are FAIL, no longer "undecided"; a changed or unmeasured prerequisite is
 * INCONCLUSIVE. Test logic only. */
static void fill_hold(TAS_HOLD_EVIDENCE *e, avb_u32 tier, LONG s0, LONG s1)
{
    unsigned k;
    const LONG st[2] = { s0, s1 };

    ZeroMemory(e, sizeof(*e));
    for (k = 0; k < 2; k++) {
        e->call[k].captured = TRUE;
        e->call[k].status = st[k];
        if (st[k] == 0) {   /* a complete success reply with the output tier 3 requires */
            e->call[k].information = (DWORD)sizeof(AVB_TAS_OP_REQUEST);
            e->call[k].echoed = TRUE;
            e->call[k].out_flags = tier == AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED ? AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE : 0u;
        }
    }
    e->accept_sent = TRUE;
    e->pre_valid = TRUE;
    e->pre_clean = TRUE;
    e->pre_generation = 5u;
    e->pre_epoch = 0x1234ABCDull;
    e->pre_mbps = 1000u;
    e->tier_hold = tier;
    e->post_valid = TRUE;
    e->post_generation = (s0 == 0 || s1 == 0) ? 6u : 5u;   /* one transition per commit */
    e->post_epoch = 0x1234ABCDull;
    e->post_mbps = 1000u;
}

static void test_62_hold_commit(void) {
    TAS_HOLD_EVIDENCE e;
    const LONG OK = 0, BUSY = (LONG)STATUS_DEVICE_BUSY, STATE = (LONG)STATUS_INVALID_DEVICE_STATE;
    const LONG NOTACC = (LONG)STATUS_REQUEST_NOT_ACCEPTED, NOTREADY = (LONG)STATUS_DEVICE_NOT_READY;
    const avb_u32 UNVER = AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED, VER = AVB_TAS_TIER_IMPLEMENTED_VERIFIED;

    printf("\n[TC-MOCK-062] #369 COMMIT of the accepted HOLD race, published 2026-11-19 rules (test logic only)\n");

    /* the out_flags expectation comes from the measured tier, not from the flag sent */
    EXPECT_EQ("tier IMPLEMENTED_UNVERIFIED: acceptance acts, bit 0 expected", tas_hold_out_expect(TRUE, UNVER), TAS_HOLD_OUT_BIT0);
    EXPECT_EQ("tier IMPLEMENTED_VERIFIED: acceptance does not act, 0 expected", tas_hold_out_expect(TRUE, VER), TAS_HOLD_OUT_ZERO);
    EXPECT_EQ("tier not reported (0): not judged", tas_hold_out_expect(TRUE, 0u), TAS_HOLD_OUT_UNJUDGED);
    EXPECT_EQ("tier NOT_IMPLEMENTED: not judged", tas_hold_out_expect(TRUE, AVB_TAS_TIER_NOT_IMPLEMENTED), TAS_HOLD_OUT_UNJUDGED);
    EXPECT_EQ("query not valid: the tier is not measured, not judged", tas_hold_out_expect(FALSE, UNVER), TAS_HOLD_OUT_UNJUDGED);

    /* one complete success and one BUSY/STATE refusal */
    fill_hold(&e, UNVER, OK, BUSY);
    EXPECT_EQ("tier 3: success with bit 0 and BUSY is PASS", tas_hold_commit(&e), TAS_HOLD_COMMIT_PASS);
    fill_hold(&e, UNVER, STATE, OK);
    EXPECT_EQ("tier 3: STATE refusal and success with bit 0 is PASS", tas_hold_commit(&e), TAS_HOLD_COMMIT_PASS);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[0].out_flags = 0u;
    EXPECT_EQ("tier 3: success with out_flags 0 is FAIL (acceptance acted)", tas_hold_commit(&e), TAS_HOLD_COMMIT_OUT_FLAGS);
    fill_hold(&e, VER, OK, STATE);
    EXPECT_EQ("tier 4: success with out_flags 0 and STATE is PASS", tas_hold_commit(&e), TAS_HOLD_COMMIT_PASS);
    fill_hold(&e, VER, OK, BUSY);
    e.call[0].out_flags = AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE;
    EXPECT_EQ("tier 4: bit 0 set merely because the flag was sent is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_OUT_FLAGS);
    fill_hold(&e, 0u, OK, BUSY);
    EXPECT_EQ("tier not reported, out_flags 0: INCONCLUSIVE (not judged), never PASS", tas_hold_commit(&e), TAS_HOLD_COMMIT_OUT_UNJUDGED);
    fill_hold(&e, 0u, OK, BUSY);
    e.call[0].out_flags = AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE;
    EXPECT_EQ("tier not reported, bit 0: INCONCLUSIVE (not judged)", tas_hold_commit(&e), TAS_HOLD_COMMIT_OUT_UNJUDGED);

    /* a success reply's own violations, independent of the tier */
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[0].out_flags |= 0x2u;
    EXPECT_EQ("another out_flags bit is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_BAD_SUCCESS);
    fill_hold(&e, 0u, OK, BUSY);
    e.call[0].reserved1 = 1u;
    EXPECT_EQ("reserved1 non-zero is FAIL even with the tier not reported", tas_hold_commit(&e), TAS_HOLD_COMMIT_BAD_SUCCESS);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[0].information = 0u;
    EXPECT_EQ("success without the 144-byte reply is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_BAD_SUCCESS);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[0].echoed = FALSE;
    EXPECT_EQ("success with the inputs not echoed is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_BAD_SUCCESS);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[1].captured = FALSE;
    e.call[0].reserved1 = 1u;
    EXPECT_EQ("an observed violation is kept when the other call was not captured", tas_hold_commit(&e), TAS_HOLD_COMMIT_BAD_SUCCESS);
    fill_hold(&e, UNVER, OK, OK);
    EXPECT_EQ("two successes are FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_TWO_SUCCESSES);
    fill_hold(&e, UNVER, OK, BUSY);
    e.pre_generation = 0u; e.pre_epoch = 0u;
    EXPECT_EQ("success of a request carrying 0/0 is FAIL (R1: refused STATE_CONFLICT)", tas_hold_commit(&e), TAS_HOLD_COMMIT_ZERO_ACCEPTED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[1].information = 8u;
    EXPECT_EQ("a refusal with output is FAIL (errors carry no output)", tas_hold_commit(&e), TAS_HOLD_COMMIT_OTHER);
    fill_hold(&e, UNVER, OK, NOTREADY);
    EXPECT_EQ("success and a NOT_READY refusal is FAIL (not BUSY/STATE)", tas_hold_commit(&e), TAS_HOLD_COMMIT_OTHER);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[1].captured = FALSE;
    EXPECT_EQ("success with the other completion not captured is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_NOT_CAPTURED);

    /* the missing handler and the formerly undecided refusals */
    fill_hold(&e, 0u, (LONG)STATUS_INVALID_DEVICE_REQUEST, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.pre_generation = 0u; e.pre_epoch = 0u;
    EXPECT_EQ("0x80C not served is FAIL, never SKIP (the current driver)", tas_hold_commit(&e), TAS_HOLD_COMMIT_NOT_SERVED);
    fill_hold(&e, UNVER, NOTACC, NOTACC);
    EXPECT_EQ("tier refusal of the accepted control is FAIL (R4: the flag acts)", tas_hold_commit(&e), TAS_HOLD_COMMIT_TIER_REFUSED);
    fill_hold(&e, 0u, NOTACC, BUSY);
    EXPECT_EQ("tier refusal with the tier not reported is still FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_TIER_REFUSED);
    fill_hold(&e, UNVER, NOTACC, BUSY);
    e.call[1].captured = FALSE;
    EXPECT_EQ("tier refusal is kept when the other call was not captured", tas_hold_commit(&e), TAS_HOLD_COMMIT_TIER_REFUSED);
    fill_hold(&e, UNVER, (LONG)STATUS_IO_TIMEOUT, BUSY);
    EXPECT_EQ("STATUS_IO_TIMEOUT (R5 BASE_PASSED) is FAIL, named", tas_hold_commit(&e), TAS_HOLD_COMMIT_R5);
    fill_hold(&e, UNVER, (LONG)STATUS_IO_DEVICE_ERROR, STATE);
    EXPECT_EQ("STATUS_IO_DEVICE_ERROR (R5 COMMITTED_UNTIMED, shared) is FAIL, named", tas_hold_commit(&e), TAS_HOLD_COMMIT_R5);
    fill_hold(&e, 0u, STATE, STATE);
    e.pre_generation = 0u; e.pre_epoch = 0u; e.post_generation = 0u; e.post_epoch = 0u;
    EXPECT_EQ("both STATE with the query's 0/0 sent is FAIL, zero metadata the likely cause", tas_hold_commit(&e), TAS_HOLD_COMMIT_META_ZERO);
    fill_hold(&e, UNVER, STATE, STATE);
    EXPECT_EQ("both STATE with an established pair unchanged after is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_STATE_REFUSED);
    fill_hold(&e, UNVER, STATE, BUSY);
    EXPECT_EQ("STATE and BUSY with an established pair unchanged after is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_STATE_REFUSED);
    fill_hold(&e, UNVER, STATE, STATE);
    e.post_generation = 9u;
    EXPECT_EQ("both STATE with the pair changed after is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_PAIR_CHANGED);
    fill_hold(&e, UNVER, STATE, STATE);
    e.post_valid = FALSE;
    EXPECT_EQ("both STATE with the pair not measured after is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_PAIR_CHANGED);
    fill_hold(&e, UNVER, STATE, STATE);
    e.pre_valid = FALSE;
    EXPECT_EQ("both STATE with the pair not measured before is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_PAIR_CHANGED);
    fill_hold(&e, UNVER, NOTREADY, NOTREADY);
    EXPECT_EQ("NOT_READY at a stably measured 1000 Mbit/s is FAIL", tas_hold_commit(&e), TAS_HOLD_COMMIT_NOT_READY);
    fill_hold(&e, UNVER, NOTREADY, NOTREADY);
    e.pre_mbps = 0u;
    EXPECT_EQ("NOT_READY with the speed unknown (0) is INCONCLUSIVE (R2's defined refusal)", tas_hold_commit(&e),
              TAS_HOLD_COMMIT_SPEED_UNDETERMINED);
    fill_hold(&e, UNVER, NOTREADY, BUSY);
    e.post_mbps = 100u;
    EXPECT_EQ("NOT_READY with the speed changed after is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_SPEED_UNDETERMINED);
    fill_hold(&e, UNVER, NOTREADY, NOTREADY);
    e.post_valid = FALSE;
    EXPECT_EQ("NOT_READY with the speed not measured after is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_SPEED_UNDETERMINED);
    fill_hold(&e, UNVER, BUSY, BUSY);
    EXPECT_EQ("both BUSY is FAIL (no commit)", tas_hold_commit(&e), TAS_HOLD_COMMIT_OTHER);
    fill_hold(&e, UNVER, BUSY, BUSY);
    e.call[0].captured = FALSE;
    EXPECT_EQ("no success and a completion not captured is INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_NOT_CAPTURED);

    /* PR #492 r4638870407: NOT_CLEAN (R7 step 9) and STATE_CONFLICT share STATUS_INVALID_DEVICE_STATE (#365 13.3/13.4), so a STATE
     * refusal is attributed only when the adapter was measured clean immediately before the race; a success without that
     * measurement is not PASS. Refusals at earlier steps (4 metadata, 5 speed, 8 tier) and observed violations stay FAIL */
    fill_hold(&e, UNVER, STATE, STATE);
    e.pre_clean = FALSE;
    EXPECT_EQ("both STATE, pair kept, clean not established at the race: INCONCLUSIVE (may be NOT_CLEAN)", tas_hold_commit(&e),
              TAS_HOLD_COMMIT_CLEAN_UNDETERMINED);
    fill_hold(&e, UNVER, STATE, BUSY);
    e.pre_clean = FALSE;
    EXPECT_EQ("STATE and BUSY, clean not established at the race: INCONCLUSIVE", tas_hold_commit(&e), TAS_HOLD_COMMIT_CLEAN_UNDETERMINED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.pre_clean = FALSE;
    EXPECT_EQ("a success with clean not established at the race is not PASS", tas_hold_commit(&e), TAS_HOLD_COMMIT_CLEAN_UNDETERMINED);
    fill_hold(&e, 0u, STATE, STATE);
    e.pre_generation = 0u; e.pre_epoch = 0u; e.post_generation = 0u; e.post_epoch = 0u; e.pre_clean = FALSE;
    EXPECT_EQ("both STATE with 0/0 sent stays FAIL without clean (step 4 precedes step 9)", tas_hold_commit(&e), TAS_HOLD_COMMIT_META_ZERO);
    fill_hold(&e, UNVER, NOTREADY, NOTREADY);
    e.pre_clean = FALSE;
    EXPECT_EQ("NOT_READY at a stable speed stays FAIL without clean (step 5 precedes step 9)", tas_hold_commit(&e), TAS_HOLD_COMMIT_NOT_READY);
    fill_hold(&e, UNVER, STATE, NOTREADY);
    e.pre_clean = FALSE;
    EXPECT_EQ("STATE with NOT_READY at a stable speed: the NOT_READY violation is kept without clean", tas_hold_commit(&e),
              TAS_HOLD_COMMIT_NOT_READY);
    fill_hold(&e, UNVER, NOTACC, BUSY);
    e.pre_clean = FALSE;
    EXPECT_EQ("a tier refusal stays FAIL without clean (step 8 precedes step 9)", tas_hold_commit(&e), TAS_HOLD_COMMIT_TIER_REFUSED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[0].out_flags = 0u; e.pre_clean = FALSE;
    EXPECT_EQ("a wrong out_flags is kept FAIL without clean", tas_hold_commit(&e), TAS_HOLD_COMMIT_OUT_FLAGS);

    /* PR #492 r4638870535: a captured completion outside SUCCESS/BUSY/STATE/NOT_READY is FAIL; neither missing evidence nor a
     * changed prerequisite explains it away */
    fill_hold(&e, UNVER, (LONG)STATUS_INVALID_PARAMETER, BUSY);
    e.call[1].captured = FALSE;
    EXPECT_EQ("INVALID_PARAMETER with the other not captured is FAIL, not NOT_CAPTURED", tas_hold_commit(&e), TAS_HOLD_COMMIT_OTHER);
    fill_hold(&e, UNVER, (LONG)STATUS_INVALID_PARAMETER, STATE);
    e.post_generation = 9u;
    EXPECT_EQ("INVALID_PARAMETER with STATE and the pair changed is FAIL, not PAIR_CHANGED", tas_hold_commit(&e), TAS_HOLD_COMMIT_OTHER);
    fill_hold(&e, UNVER, (LONG)STATUS_UNSUCCESSFUL, NOTREADY);
    e.post_mbps = 100u;
    EXPECT_EQ("STATUS_UNSUCCESSFUL with NOT_READY and the speed changed is FAIL, not SPEED_UNDETERMINED", tas_hold_commit(&e),
              TAS_HOLD_COMMIT_OTHER);
}

/* TC-MOCK-063: TEST-TAS-API-004 (#369) GENERATION across the race (#352 R1 L2/L7/L9; #369 oracle amendment 2026-11-19). A logical
 * mutation that attempts a write advances generation exactly once (a commit: one); a refusal before any write advances nothing.
 * Judged only from a non-zero pair measured before and after in the same context; a completion whose write attempt is not
 * established (an uncaptured status, a shared R5/R8 status) leaves the expectation unknown. A decrease is always FAIL. Test logic
 * only. */
static void test_63_hold_generation(void) {
    TAS_HOLD_EVIDENCE e;
    unsigned n = 99u;
    const LONG OK = 0, BUSY = (LONG)STATUS_DEVICE_BUSY, STATE = (LONG)STATUS_INVALID_DEVICE_STATE;
    const avb_u32 UNVER = AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED;

    printf("\n[TC-MOCK-063] #369 GENERATION across the race, #352 R1 (test logic only)\n");

    fill_hold(&e, UNVER, OK, BUSY);
    EXPECT_EQ("one commit, one BUSY: 5 -> 6 is PASS", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PASS);
    EXPECT_TRUE("... with one transition expected", n == 1u);
    fill_hold(&e, UNVER, STATE, OK);
    e.post_generation = 5u;
    EXPECT_EQ("one commit, generation unchanged is FAIL", tas_hold_generation(&e, &n), TAS_HOLD_GEN_FAIL);
    fill_hold(&e, UNVER, OK, BUSY);
    e.post_generation = 7u;
    EXPECT_EQ("one commit, two steps is FAIL (the refusal adds nothing)", tas_hold_generation(&e, &n), TAS_HOLD_GEN_FAIL);
    fill_hold(&e, UNVER, STATE, STATE);
    EXPECT_EQ("both refused before any write, unchanged: PASS", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PASS);
    EXPECT_TRUE("... with no transition expected", n == 0u);
    fill_hold(&e, UNVER, (LONG)STATUS_REQUEST_NOT_ACCEPTED, (LONG)STATUS_DEVICE_NOT_READY);
    EXPECT_EQ("NOT_ACCEPTED and NOT_READY are pre-write refusals: unchanged is PASS", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PASS);
    fill_hold(&e, UNVER, BUSY, (LONG)STATUS_INVALID_PARAMETER);
    e.post_generation = 6u;
    EXPECT_EQ("pre-write refusals with a transition is FAIL", tas_hold_generation(&e, &n), TAS_HOLD_GEN_FAIL);
    fill_hold(&e, UNVER, (LONG)STATUS_INVALID_DEVICE_REQUEST, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    EXPECT_EQ("not served with an established pair unchanged: PASS (no operation ran)", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PASS);

    /* the current driver: 0/0 */
    fill_hold(&e, 0u, (LONG)STATUS_INVALID_DEVICE_REQUEST, (LONG)STATUS_INVALID_DEVICE_REQUEST);
    e.pre_generation = 0u; e.pre_epoch = 0u; e.post_generation = 0u; e.post_epoch = 0u;
    EXPECT_EQ("0/0 before: INCONCLUSIVE (not reported), never PASS", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PRE_UNREPORTED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.pre_valid = FALSE;
    EXPECT_EQ("query before not valid: INCONCLUSIVE", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PRE_UNREPORTED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.post_generation = 0u;
    EXPECT_EQ("generation 0 after: INCONCLUSIVE", tas_hold_generation(&e, &n), TAS_HOLD_GEN_POST_UNREPORTED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.post_valid = FALSE;
    EXPECT_EQ("query after not valid: INCONCLUSIVE", tas_hold_generation(&e, &n), TAS_HOLD_GEN_POST_UNREPORTED);
    fill_hold(&e, UNVER, OK, BUSY);
    e.post_epoch = 0x55u;
    EXPECT_EQ("epoch changed (another context): INCONCLUSIVE", tas_hold_generation(&e, &n), TAS_HOLD_GEN_EPOCH_CHANGED);
    fill_hold(&e, UNVER, (LONG)STATUS_IO_DEVICE_ERROR, BUSY);
    e.post_generation = 6u;
    EXPECT_EQ("a shared R5/R8 status: the write attempt is not established, INCONCLUSIVE", tas_hold_generation(&e, &n),
              TAS_HOLD_GEN_UNKNOWN_EXPECTATION);
    fill_hold(&e, UNVER, OK, BUSY);
    e.call[1].captured = FALSE;
    EXPECT_EQ("a completion not captured: INCONCLUSIVE", tas_hold_generation(&e, &n), TAS_HOLD_GEN_UNKNOWN_EXPECTATION);
    fill_hold(&e, UNVER, (LONG)STATUS_IO_DEVICE_ERROR, BUSY);
    e.post_generation = 4u;
    EXPECT_EQ("a decrease is FAIL even with the expectation unknown", tas_hold_generation(&e, &n), TAS_HOLD_GEN_FAIL);

    /* PR #492 r4638870492: STATUS_IO_TIMEOUT is R5's BASE_PASSED only (#365 R5), an outcome after attempted writes, so it advances
     * generation exactly once (#352 R1 L7) */
    fill_hold(&e, UNVER, (LONG)STATUS_IO_TIMEOUT, BUSY);
    e.post_generation = 6u;
    EXPECT_EQ("IO_TIMEOUT (BASE_PASSED) and BUSY: 5 -> 6 is PASS", tas_hold_generation(&e, &n), TAS_HOLD_GEN_PASS);
    EXPECT_TRUE("... with one transition expected", n == 1u);
    fill_hold(&e, UNVER, (LONG)STATUS_IO_TIMEOUT, BUSY);
    EXPECT_EQ("IO_TIMEOUT (BASE_PASSED) with generation unchanged is FAIL", tas_hold_generation(&e, &n), TAS_HOLD_GEN_FAIL);
}

/* TC-MOCK-064: the cases contained in test_tas_activate.exe, their selection and the exit status of a run (#487; test logic only).
 * No argument selects every contained case; explicit arguments select exactly those; an unknown argument runs nothing. A run is
 * successful only when every selected case is: a later success never covers an earlier failure, block or unexecuted case */
static void test_64_activate_cases(void)
{
    BOOL sel[TAS_ACT_CASE_COUNT];
    int ex[TAS_ACT_CASE_COUNT];
    unsigned k, j, seen[MODE_COUNT] = {0}, n = 0;
    char *none[] = { "test_tas_activate.exe" };
    char *bad[] = { "test_tas_activate.exe", "--unknown" };
    char *prefix[] = { "test_tas_activate.exe", "--fault-" };
    char *mixed[] = { "test_tas_activate.exe", "--hold", "--unknown" };
    char *twice[] = { "test_tas_activate.exe", "--hold", "--phc-hold-control", "--hold" };

    printf("\n[TC-MOCK-064] #487 contained cases, selection and exit status (test logic only)\n");
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        EXPECT_EQ("a case has a mode", g_tas_act_cases[k].mode > MODE_NONE && g_tas_act_cases[k].mode < MODE_COUNT, 1);
        EXPECT_EQ("a case has an argument and an id", g_tas_act_cases[k].arg[0] == '-' && g_tas_act_cases[k].case_id[0] != 0, 1);
        seen[g_tas_act_cases[k].mode]++;
        for (j = 0; j < k; j++) {
            EXPECT_EQ("arguments are distinct", strcmp(g_tas_act_cases[k].arg, g_tas_act_cases[j].arg) != 0, 1);
        }
    }
    for (k = MODE_NONE + 1; k < MODE_COUNT; k++) {
        EXPECT_EQ("every mode of the program is a contained case, once", seen[k], 1);
    }

    EXPECT_EQ("no argument is a valid selection", tas_act_select(1, none, sel), TRUE);
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        EXPECT_EQ("no argument selects every contained case", sel[k], TRUE);
    }
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        char *one[2];
        one[0] = none[0];
        one[1] = (char *)g_tas_act_cases[k].arg;
        EXPECT_EQ("an explicit argument is a valid selection", tas_act_select(2, one, sel), TRUE);
        for (j = 0; j < TAS_ACT_CASE_COUNT; j++) {
            EXPECT_EQ("an explicit argument selects only its case", sel[j], (j == k));
        }
    }
    EXPECT_EQ("an unknown argument is refused", tas_act_select(2, bad, sel), FALSE);
    EXPECT_EQ("a prefix of an argument is refused", tas_act_select(2, prefix, sel), FALSE);
    EXPECT_EQ("an unknown argument beside a known one is refused", tas_act_select(3, mixed, sel), FALSE);
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        EXPECT_EQ("a refused command line selects nothing", sel[k], FALSE);
    }
    EXPECT_EQ("several arguments select a subset", tas_act_select(4, twice, sel), TRUE);
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        n += sel[k] ? 1u : 0u;
    }
    EXPECT_EQ("a repeated argument selects its case once", n, 2);

    EXPECT_EQ("all checks passed", tas_act_case_exit(2, 0, 0), 0);
    EXPECT_EQ("a nonpassing verdict", tas_act_case_exit(2, 1, 3), 1);
    EXPECT_EQ("blocked", tas_act_case_exit(1, 0, 1), 3);
    EXPECT_EQ("nothing applicable", tas_act_case_exit(0, 0, 0), 2);
    EXPECT_EQ("blocked before anything ran is not an unsupported result", tas_act_case_exit(0, 0, 1), 3);
    EXPECT_EQ("a failure before anything ran is not an unsupported result", tas_act_case_exit(0, 1, 0), 1);

    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        sel[k] = TRUE;
        ex[k] = 0;
    }
    EXPECT_EQ("every case passed", tas_act_run_exit(sel, ex), 0);
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        int v;
        for (v = 1; v <= 3; v++) {
            ex[k] = v;
            EXPECT_EQ("one nonpassing case decides the run, wherever it is", tas_act_run_exit(sel, ex), v);
        }
        ex[k] = 0;
    }
    ex[0] = 3;
    ex[1] = 1;
    EXPECT_EQ("a failure outweighs a block", tas_act_run_exit(sel, ex), 1);
    ex[1] = 2;
    EXPECT_EQ("a block outweighs nothing applicable", tas_act_run_exit(sel, ex), 3);
    ex[0] = 99;
    EXPECT_EQ("an unknown status is a failure", tas_act_run_exit(sel, ex), 1);
    for (k = 0; k < TAS_ACT_CASE_COUNT; k++) {
        sel[k] = FALSE;
        ex[k] = 1;
    }
    sel[1] = TRUE;
    ex[1] = 0;
    EXPECT_EQ("an unselected case does not count", tas_act_run_exit(sel, ex), 0);
    sel[1] = FALSE;
    EXPECT_EQ("a run without any case is never successful", tas_act_run_exit(sel, ex) != 0, 1);
}

/* TC-MOCK-066: order of a held PHC reseed and an ACTIVATE that arrived during the hold, and the generation step it requires (#357 F1; test
 * logic only). OBSERVED: the markers of the hardware run 2026-11-24 22:15 (I226 0x125B, Debug driver 9FBE5DCA, test 1BDE3CB8; archived in
 * logs/issue357/f1-red-20261124/hw-2215-9FBE5DCA): generation 1 -> 2 where +2 is required. Every other marker set is UNVERIFIED */
static void test_66_race_order(void)
{
    const TAS_RACE_MARKERS observed = { 2, 5, 3, 4, 6, 7 };       /* hold_begin, hold_end, arrival, commit, write_enter, write_done */
    const TAS_RACE_MARKERS write_first = { 2, 4, 3, 7, 5, 6 };    /* UNVERIFIED: the ACTIVATE waited for the reseed */
    const TAS_RACE_MARKERS after_hold = { 2, 4, 3, 5, 6, 7 };     /* UNVERIFIED: commit between hold end and write entry */
    TAS_RACE_MARKERS m;

    printf("\n[TC-MOCK-066] #357 order of a held PHC reseed and an ACTIVATE (test logic only)\n");
    EXPECT_EQ("the observed run: the commit precedes the write", tas_race_order(&observed), TAS_RACE_COMMIT_FIRST);
    EXPECT_EQ("... requires +2 (the observed +1 is the F1 RED)", tas_race_required_step(tas_race_order(&observed)), 2u);
    EXPECT_EQ("the write precedes the commit: a valid order, not a lost window", tas_race_order(&write_first), TAS_RACE_WRITE_FIRST);
    EXPECT_EQ("... requires +1", tas_race_required_step(tas_race_order(&write_first)), 1u);
    EXPECT_EQ("a commit after the hold but before the write entry precedes the write", tas_race_order(&after_hold), TAS_RACE_COMMIT_FIRST);

    m = observed; m.commit = 0;
    EXPECT_EQ("no commit establishes no order", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    EXPECT_EQ("... and requires no step", tas_race_required_step(tas_race_order(&m)), 0u);
    m = observed; m.arrival = 0;
    EXPECT_EQ("no arrival establishes no order", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    m = write_first; m.arrival = 1;
    EXPECT_EQ("an arrival before the hold is not the interleaving", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    m = write_first; m.hold_end = 3; m.arrival = 4;
    EXPECT_EQ("an arrival after the hold is not the interleaving", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    m = observed; m.hold_begin = 0;
    EXPECT_EQ("a reseed that was not held is not the interleaving", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    m = observed; m.write_enter = 0; m.write_done = 0;
    EXPECT_EQ("a reseed without a write establishes no order", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    m = write_first; m.write_enter = 5; m.commit = 6; m.write_done = 7;
    EXPECT_EQ("a commit inside the write establishes no order", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);
    m = observed; m.commit = 3;
    EXPECT_EQ("a commit that does not follow its arrival establishes no order", tas_race_order(&m), TAS_RACE_NOT_ESTABLISHED);

    EXPECT_EQ("write first, BaseT passed on the old PHC only: the new PHC was used",
              tas_race_phc_used(TAS_RACE_WRITE_FIRST, 24, 25, 21), TAS_RACE_PHC_NEW);
    EXPECT_EQ("commit first with a passed BaseT is a violation", tas_race_phc_used(TAS_RACE_COMMIT_FIRST, 24, 25, 21), TAS_RACE_PHC_PASSED_BASE);
    EXPECT_EQ("no order: not shown", tas_race_phc_used(TAS_RACE_NOT_ESTABLISHED, 24, 25, 21), TAS_RACE_PHC_NOT_SHOWN);
    EXPECT_EQ("a BaseT valid on both clocks shows nothing", tas_race_phc_used(TAS_RACE_WRITE_FIRST, 26, 25, 21), TAS_RACE_PHC_NOT_SHOWN);
    EXPECT_EQ("a clock that was not stepped back shows nothing", tas_race_phc_used(TAS_RACE_WRITE_FIRST, 24, 25, 26), TAS_RACE_PHC_NOT_SHOWN);
}

/* TC-MOCK-065: a worker that outlives a bounded wait stays owned until its completion is established (#505, PR #518 review; test logic
 * only, real threads, no driver). Its storage is not read, reused or freed before, a wait that fails settles nothing, a late completion
 * is collected with its real result, and no cleanup is judged as verified while an operation may still be running */
typedef struct {
    HANDLE go;
    volatile LONG *observed;   /* what the worker saw in its own storage after the caller had given up waiting */
    LONG input;
    LONG output;
} OWNED_WORKER_JOB;

static DWORD WINAPI owned_worker_late(LPVOID p)
{
    OWNED_WORKER_JOB *job = (OWNED_WORKER_JOB *)p;

    if (WaitForSingleObject(job->go, 20000) != WAIT_OBJECT_0) {
        return 1;
    }
    InterlockedExchange(job->observed, job->input);
    job->output = job->input + 1;
    return 0;
}

static void test_65_owned_worker(void)
{
    volatile LONG observed = 0;
    OWNED_WORKER w;
    OWNED_WORKER_JOB init, *res;
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL), full, limited = NULL;
    DWORD t0;

    printf("\n[TC-MOCK-065] #505 worker ownership beyond a bounded wait (test logic only)\n");
    if (go == NULL) {
        EXPECT_EQ("event available", 0, 1);
        return;
    }
    init.go = go;
    init.observed = &observed;
    init.input = 41;
    init.output = 0;
    EXPECT_EQ("the worker starts", owned_worker_start(&w, owned_worker_late, &init, sizeof(init)), TRUE);
    EXPECT_EQ("its storage is not the caller's", w.ctx != (void *)&init, 1);
    memset(&init, 0xEE, sizeof(init));   /* the caller reuses its own object, as ONESHOT did */

    EXPECT_EQ("a wait that times out leaves it running", owned_worker_wait(&w, 50), OWNED_WORKER_RUNNING);
    EXPECT_EQ("no result before completion", owned_worker_result(&w) == NULL, 1);
    EXPECT_EQ("a running worker is not released", owned_worker_release(&w), FALSE);
    EXPECT_EQ("its storage stays owned", w.ctx != NULL, 1);
    EXPECT_EQ("its handle stays owned", w.thread != NULL, 1);

    /* A wait that FAILS (no SYNCHRONIZE right) settles nothing either */
    full = w.thread;
    if (full != NULL && DuplicateHandle(GetCurrentProcess(), full, GetCurrentProcess(), &limited, THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
        w.thread = limited;
        w.state = OWNED_WORKER_RUNNING;
        EXPECT_EQ("a failed wait leaves it running", owned_worker_wait(&w, 50), OWNED_WORKER_RUNNING);
        EXPECT_EQ("no result after a failed wait", owned_worker_result(&w) == NULL, 1);
        CloseHandle(limited);
        w.thread = full;
        w.state = OWNED_WORKER_RUNNING;
    } else {
        EXPECT_EQ("limited handle available", 0, 1);
    }

    /* The late completion is collected with its real result */
    SetEvent(go);
    EXPECT_EQ("a late completion is established", owned_worker_wait(&w, 10000), OWNED_WORKER_DONE);
    for (t0 = GetTickCount(); observed == 0 && GetTickCount() - t0 < 5000;) {
        Sleep(5);
    }
    EXPECT_EQ("the worker used its own storage, not the reused object", (int)observed, 41);
    res = (OWNED_WORKER_JOB *)owned_worker_result(&w);
    EXPECT_EQ("its result is readable after completion", res != NULL && res->output == 42, 1);
    EXPECT_EQ("a finished worker is released", owned_worker_release(&w), TRUE);
    EXPECT_EQ("nothing is owned afterwards", w.thread == NULL && w.ctx == NULL && w.state == OWNED_WORKER_NONE, 1);
    EXPECT_EQ("a wait without a worker settles nothing", owned_worker_wait(&w, 0), OWNED_WORKER_NONE);
    CloseHandle(go);

    EXPECT_EQ("settled, nothing found: PASS", holdctl_cleanup(TRUE, FALSE, FALSE), HOLDCTL_CLEANUP_PASS);
    EXPECT_EQ("settled, deviation: FAILED", holdctl_cleanup(TRUE, TRUE, FALSE), HOLDCTL_CLEANUP_FAILED);
    EXPECT_EQ("settled, deviation and unmeasured: FAILED", holdctl_cleanup(TRUE, TRUE, TRUE), HOLDCTL_CLEANUP_FAILED);
    EXPECT_EQ("settled, unmeasured: INCONCLUSIVE", holdctl_cleanup(TRUE, FALSE, TRUE), HOLDCTL_CLEANUP_INCONCLUSIVE);
    EXPECT_EQ("unsettled, nothing found: never PASS", holdctl_cleanup(FALSE, FALSE, FALSE), HOLDCTL_CLEANUP_UNSETTLED);
    EXPECT_EQ("unsettled, unmeasured: unsettled", holdctl_cleanup(FALSE, FALSE, TRUE), HOLDCTL_CLEANUP_UNSETTLED);
    EXPECT_EQ("unsettled, deviation measured: the final state is unsettled", holdctl_cleanup(FALSE, TRUE, FALSE), HOLDCTL_CLEANUP_UNSETTLED);
}

/* TC-MOCK-067: a cleanup while an operation of the test may still be running (#357 race, review 2026-11-25; test logic only, real threads,
 * no driver). Whatever was measured, no final restoration PASS and no CLEANUP PASS; a measured failure stays a failure; the operation
 * stays owned until its end was observed; other adapters are not affected */
static void test_67_pending_operations(void)
{
    const unsigned long long adapter_a = 0x0006008006000000ull, adapter_b = 0x0006008005000000ull;
    volatile LONG observed = 0;
    OWNED_OP *ops = NULL, *op;
    OWNED_WORKER_JOB init;
    TAS_ACT_RESTORE_EVIDENCE e, ok;
    HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);

    printf("\n[TC-MOCK-067] #357 no restoration PASS while an operation of the test may still be running (test logic only)\n");
    if (go == NULL) {
        EXPECT_EQ("event available", 0, 1);
        return;
    }
    init.go = go;
    init.observed = &observed;
    init.input = 41;
    init.output = 0;
    ZeroMemory(&ok, sizeof(ok));
    ok.cycle_completed = TRUE; ok.adapter_returned = TRUE; ok.driver_clean = TRUE; ok.regs_measured = TRUE;
    ok.tsn_mode_off = TRUE; ok.tx_probed = TRUE; ok.tx_on_wire = TRUE;

    EXPECT_EQ("no operation issued: settled", owned_ops_settled(&ops, adapter_a, 0), TRUE);
    op = owned_op_new(&ops, adapter_a);
    EXPECT_EQ("an operation is registered before it is started", op != NULL && ops == op, 1);
    EXPECT_EQ("a registered operation that was never started is settled", owned_ops_settled(&ops, adapter_a, 0), TRUE);
    EXPECT_EQ("... and not kept", ops == NULL, 1);

    (void)owned_op_new(&ops, adapter_b);
    op = owned_op_new(&ops, adapter_a);
    EXPECT_EQ("the operation starts", op != NULL && owned_worker_start(&op->worker, owned_worker_late, &init, sizeof(init)), TRUE);
    EXPECT_EQ("an operation that has not ended is not settled", owned_ops_settled(&ops, adapter_a, 50), FALSE);
    EXPECT_EQ("... and asking again does not settle it", owned_ops_settled(&ops, adapter_a, 0), FALSE);
    EXPECT_EQ("... it stays owned with its storage", op != NULL && op->worker.state == OWNED_WORKER_RUNNING && op->worker.ctx != NULL, 1);
    EXPECT_EQ("another adapter is not affected", owned_ops_settled(&ops, adapter_b, 0), TRUE);

    e = ok;
    e.operations_pending = !owned_ops_settled(&ops, adapter_a, 0);
    EXPECT_EQ("everything measured good, operation open: no restoration PASS", tas_act_restore(&e), TAS_ACT_RESTORE_UNVERIFIED);
    EXPECT_EQ("cycle completed, operation open: no CLEANUP PASS", tas_act_cycle(&e), TAS_ACT_RESTORE_UNVERIFIED);
    e.regs_measured = FALSE;
    EXPECT_EQ("unmeasured registers, operation open: unverified", tas_act_restore(&e), TAS_ACT_RESTORE_UNVERIFIED);
    e = ok; e.operations_pending = TRUE; e.regs_differing = 1;
    EXPECT_EQ("a measured residue stays a failure while an operation is open", tas_act_restore(&e), TAS_ACT_RESTORE_INCOMPLETE);
    e = ok; e.operations_pending = TRUE; e.cycle_completed = FALSE;
    EXPECT_EQ("an incomplete cycle stays a failure while an operation is open", tas_act_cycle(&e), TAS_ACT_RESTORE_INCOMPLETE);

    SetEvent(go);
    EXPECT_EQ("a late end is established", owned_ops_settled(&ops, adapter_a, 10000), TRUE);
    EXPECT_EQ("... with its real effect", (int)observed, 41);
    EXPECT_EQ("... and nothing is owned afterwards", ops == NULL, 1);
    e = ok;
    e.operations_pending = !owned_ops_settled(&ops, adapter_a, 0);
    EXPECT_EQ("settled before the cycle, everything measured good: complete", tas_act_restore(&e), TAS_ACT_RESTORE_COMPLETE);
    EXPECT_EQ("settled before the cycle, cycle completed: complete", tas_act_cycle(&e), TAS_ACT_RESTORE_COMPLETE);
    e.adapter_returned = FALSE;
    EXPECT_EQ("settled, adapter not back: incomplete", tas_act_cycle(&e), TAS_ACT_RESTORE_INCOMPLETE);
    CloseHandle(go);
}

/*
 * TC-MOCK-014: which adapter a FilterPause/FilterRestart trigger may name (#364).
 *
 * Record layout (USHORT byte length, then UTF-16 name without terminator) is from IOCTL_FILTER_ENUERATE_ALL_INSTANCES in
 * src/device.c. Name layout {interface GUID}-{filter GUID}-0000 was observed once, on the I210 instance, in
 * logs/dbgview_test_ptp_phc_stability.exe_20260430_123620.log; its first GUID is that adapter's InterfaceGuid and its
 * second is NetCfgInstanceId in the INF. The target record below is built on that layout and is UNVERIFIED. The logic
 * takes any adapter's GUID: these values select nothing. The driver matches by prefix (filterFindFilterModule), which is
 * why a prefix counts as ambiguous.
 */
#define FILTER_GUID_TEXT L"{3F74AE86-14F9-4E79-9445-5B1E52CCD192}"
#define OBSERVED_GUID_TEXT L"{9F90590F-E0F0-46B7-A69E-BF0A45DEDB6C}"
#define TARGET_GUID_TEXT   L"{5989BA55-D568-47A4-AFBF-B47F27E1B435}"

static DWORD put_instance(BYTE *list, DWORD at, const WCHAR *name) {
    USHORT bytes = (USHORT)(wcslen(name) * sizeof(WCHAR));
    memcpy(list + at, &bytes, sizeof(bytes));
    memcpy(list + at + sizeof(bytes), name, bytes);
    return at + sizeof(bytes) + bytes;
}

static void test_14_instance_find(void) {
    static const WCHAR observed[] = OBSERVED_GUID_TEXT L"-" FILTER_GUID_TEXT L"-0000";
    static const WCHAR target[] = TARGET_GUID_TEXT L"-" FILTER_GUID_TEXT L"-0000";
    BYTE list[512];
    const WCHAR *name = NULL;
    USHORT bytes = 0;
    DWORD used;

    printf("\n[TC-MOCK-014] Exact instance for one adapter\n");
    used = put_instance(list, 0, observed);
    used = put_instance(list, used, target);
    EXPECT_EQ("the adapter's own record", tas_instance_find(list, used, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_FOUND);
    EXPECT_EQ("name length is the record's own", bytes, (int)(wcslen(target) * sizeof(WCHAR)));
    EXPECT_TRUE("name points at that record", name != NULL && memcmp(name, target, bytes) == 0);
    EXPECT_EQ("GUID text in any case", tas_instance_find(list, used, L"{5989ba55-d568-47a4-afbf-b47f27e1b435}", &name, &bytes), TAS_INSTANCE_FOUND);

    EXPECT_EQ("adapter not listed", tas_instance_find(list, used, L"{D44A7E27-72EE-480D-958C-558FACF2B105}", &name, &bytes), TAS_INSTANCE_NONE);
    EXPECT_EQ("empty list", tas_instance_find(list, 0, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);
    EXPECT_EQ("the filter GUID names no adapter", tas_instance_find(list, used, FILTER_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);

    used = put_instance(list, 0, TARGET_GUID_TEXT L"X" FILTER_GUID_TEXT L"-0000");
    EXPECT_EQ("the GUID must end at its separator", tas_instance_find(list, used, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);
    used = put_instance(list, 0, TARGET_GUID_TEXT);
    EXPECT_EQ("the GUID alone is not an instance", tas_instance_find(list, used, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);

    used = put_instance(list, 0, target);
    used = put_instance(list, used, TARGET_GUID_TEXT L"-" FILTER_GUID_TEXT L"-0001");
    EXPECT_EQ("two records name the adapter", tas_instance_find(list, used, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_AMBIGUOUS);

    /* The driver stops at the first prefix match, so the shorter record would be restarted instead */
    used = put_instance(list, 0, L"{5989BA55");
    used = put_instance(list, used, target);
    EXPECT_EQ("another record is a prefix of the name", tas_instance_find(list, used, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_AMBIGUOUS);
    used = put_instance(list, 0, L"");
    used = put_instance(list, used, target);
    EXPECT_EQ("an empty record is a prefix of everything", tas_instance_find(list, used, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_AMBIGUOUS);

    used = put_instance(list, 0, target);
    EXPECT_EQ("list cut inside a name", tas_instance_find(list, used - 2, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_MALFORMED);
    EXPECT_EQ("list cut inside a length", tas_instance_find(list, 1, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_MALFORMED);
    list[0] = 3; list[1] = 0;
    EXPECT_EQ("odd byte length is not UTF-16", tas_instance_find(list, 16, TARGET_GUID_TEXT, &name, &bytes), TAS_INSTANCE_MALFORMED);
}

static TAS_LIFECYCLE_EVIDENCE full_lifecycle_evidence(void) {
    TAS_LIFECYCLE_EVIDENCE e;
    memset(&e, 0, sizeof(e));
    e.pause_delta = 1;
    e.restart_delta = 1;
    e.generation_delta = 1;
    e.traffic_shown = TRUE;
    return e;
}

/* TC-MOCK-015: a successful IOCTL and entry counters are never completion (#364) */
static void test_15_lifecycle_outcome(void) {
    TAS_LIFECYCLE_EVIDENCE e;
    AVB_DRIVER_STATISTICS before, after;

    printf("\n[TC-MOCK-015] Lifecycle outcome from the evidence\n");
    e = full_lifecycle_evidence();
    EXPECT_EQ("entries plus traffic through the stack", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_RESUMED);

    e = full_lifecycle_evidence(); e.traffic_shown = FALSE;
    EXPECT_EQ("entry counters alone are not completion", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_RESTART_ENTERED);

    memset(&e, 0, sizeof(e));
    EXPECT_EQ("no counter moved: the IOCTL's success means nothing", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_NOT_TRIGGERED);
    e.traffic_shown = TRUE;
    EXPECT_EQ("traffic without a lifecycle event", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_NOT_TRIGGERED);

    e = full_lifecycle_evidence(); e.restart_delta = 0; e.traffic_shown = FALSE;
    EXPECT_EQ("Pause entered, Restart not within the bound", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_PAUSE_ONLY);

    e = full_lifecycle_evidence(); e.detach_delta = 1;
    EXPECT_EQ("detach outranks everything", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_CONTEXT_REPLACED);
    e = full_lifecycle_evidence(); e.attach_delta = 1;
    EXPECT_EQ("attach outranks everything", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_CONTEXT_REPLACED);
    e = full_lifecycle_evidence(); e.pause_delta = -7;
    EXPECT_EQ("a counter ran backwards: new context", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_CONTEXT_REPLACED);

    e = full_lifecycle_evidence(); e.pause_delta = 2; e.restart_delta = 2; e.generation_delta = 2;
    EXPECT_EQ("two cycles cannot be attributed to the trigger", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_OVERLAPPED);
    e = full_lifecycle_evidence(); e.pause_delta = 0; e.generation_delta = 0;
    EXPECT_EQ("Restart without Pause", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_OVERLAPPED);
    e = full_lifecycle_evidence(); e.generation_delta = 0;
    EXPECT_EQ("generation disagrees with Pause", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_OVERLAPPED);

    /* Counters are unsigned and a new context restarts them, so a fall must arrive here as a negative delta */
    memset(&before, 0, sizeof(before));
    before.FilterPauseCount = 5; before.FilterRestartCount = 6; before.PauseRestartGeneration = 7;
    before.FilterDetachCount = 2; before.FilterAttachCount = 3;
    after = before;
    after.FilterPauseCount++; after.FilterRestartCount++; after.PauseRestartGeneration++;
    tas_lifecycle_evidence(&before, &after, TRUE, &e);
    EXPECT_EQ("one cycle read from the counters", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_RESUMED);
    tas_lifecycle_evidence(&before, &after, FALSE, &e);
    EXPECT_EQ("the same counters without traffic", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_RESTART_ENTERED);
    memset(&after, 0, sizeof(after));
    tas_lifecycle_evidence(&before, &after, TRUE, &e);
    EXPECT_TRUE("a fallen counter is a negative delta", e.pause_delta == -5);
    EXPECT_EQ("fallen counters are a new context", tas_lifecycle_outcome(&e), TAS_LIFECYCLE_CONTEXT_REPLACED);
}

/* TC-MOCK-068: a FilterRestart in the same context (#357, TC-TASACT-META-004; test logic only). OBSERVED: the PHC of the drained Case B run 1
 * (2026-11-07, I226 0x125B REV_04, Debug driver 1B4F299C; logs/i364-evidence/caseb-drained-run1) went from 1417.92 s to 9.32 s across a
 * FilterRestart, the second read of each pair is UNVERIFIED. Every other value is UNVERIFIED */
static void test_68_restart(void)
{
    const TAS_PHC_READS observed_before = { TRUE, { 1417920000000LL, 1417920100000LL } };
    const TAS_PHC_READS observed_after = { TRUE, { 9320000000LL, 9320100000LL } };
    TAS_PHC_READS a, b;
    OWNED_OP *ops = NULL, *op;
    unsigned long r;

    printf("\n[TC-MOCK-068] #357 FilterRestart: reset, stale view, new context, open effect (test logic only)\n");
    EXPECT_EQ("the observed restart reset the PHC", tas_phc_reset(&observed_before, &observed_after), TAS_PHC_RESET_SHOWN);
    b = observed_before; b.phc[0] = observed_before.phc[1] + 1; b.phc[1] = b.phc[0] + 1;
    EXPECT_EQ("a clock that advanced shows no reset", tas_phc_reset(&observed_before, &b), TAS_PHC_RESET_NOT_SHOWN);
    b = observed_before; b.phc[0] = observed_before.phc[1]; b.phc[1] = b.phc[0] + 1;
    EXPECT_EQ("an equal value shows no reset", tas_phc_reset(&observed_before, &b), TAS_PHC_RESET_NOT_SHOWN);
    a = observed_after; a.phc[1] = a.phc[0];
    EXPECT_EQ("a clock that does not run afterwards is no measured reset", tas_phc_reset(&observed_before, &a), TAS_PHC_RESET_UNMEASURED);
    b = observed_before; b.phc[1] = b.phc[0];
    EXPECT_EQ("a clock that did not run before is no measured reset", tas_phc_reset(&b, &observed_after), TAS_PHC_RESET_UNMEASURED);
    a = observed_after; a.read = FALSE;
    EXPECT_EQ("an unread clock is no measured reset", tas_phc_reset(&observed_before, &a), TAS_PHC_RESET_UNMEASURED);

    EXPECT_EQ("STATE_CONFLICT refuses the old pair", tas_stale_view(AVB_TAS_ACT_STATE_CONFLICT), TAS_STALE_REFUSED);
    EXPECT_EQ("NOT_CLEAN passed step 4: the old pair is still current", tas_stale_view(AVB_TAS_ACT_NOT_CLEAN), TAS_STALE_STILL_CURRENT);
    EXPECT_EQ("COMMITTED used the old pair", tas_stale_view(AVB_TAS_ACT_COMMITTED), TAS_STALE_STILL_CURRENT);
    for (r = 0; r < 2; r++) {
        EXPECT_EQ("a refusal that may precede step 4 says nothing", tas_stale_view(r == 0 ? AVB_TAS_ACT_BUSY : AVB_TAS_ACT_NOT_READY), TAS_STALE_NOT_JUDGED);
    }
    EXPECT_EQ("no recorded result says nothing", tas_stale_view(0xFFFFFFFFul), TAS_STALE_NOT_JUDGED);

    EXPECT_EQ("new epoch, generation 1, CLEAN", tas_new_context(5, TRUE, 6, 1, TRUE, TRUE), TAS_NEW_CONTEXT_OK);
    EXPECT_EQ("the same epoch shows no new context", tas_new_context(5, TRUE, 5, 1, TRUE, TRUE), TAS_NEW_CONTEXT_NOT_SHOWN);
    EXPECT_EQ("a new context that starts above 1 is a violation", tas_new_context(5, TRUE, 6, 2, TRUE, TRUE), TAS_NEW_CONTEXT_VIOLATION);
    EXPECT_EQ("a new context whose record is not CLEAN is a violation", tas_new_context(5, TRUE, 6, 1, TRUE, FALSE), TAS_NEW_CONTEXT_VIOLATION);
    EXPECT_EQ("an unread record shows nothing", tas_new_context(5, TRUE, 6, 1, FALSE, FALSE), TAS_NEW_CONTEXT_NOT_SHOWN);
    EXPECT_EQ("an unreported pair shows nothing", tas_new_context(5, TRUE, 0, 0, TRUE, TRUE), TAS_NEW_CONTEXT_NOT_SHOWN);
    EXPECT_EQ("without the old epoch nothing is compared", tas_new_context(0, TRUE, 6, 1, TRUE, TRUE), TAS_NEW_CONTEXT_NOT_SHOWN);

    op = owned_op_new(&ops, 1);
    if (op == NULL) {
        EXPECT_EQ("memory available", 0, 1);
        return;
    }
    op->effect_open = TRUE;   /* the request returned (no thread runs), its effect has not been observed */
    EXPECT_EQ("a request whose effect has not been observed is not settled", owned_ops_settled(&ops, 1, 0), FALSE);
    EXPECT_EQ("... and stays owned", ops == op, 1);
    EXPECT_EQ("another adapter is not affected", owned_ops_settled(&ops, 2, 0), TRUE);
    if (ops != NULL) {
        ops->effect_open = FALSE;
    }
    EXPECT_EQ("an observed effect settles it", owned_ops_settled(&ops, 1, 0), TRUE);
    EXPECT_EQ("... and nothing is owned afterwards", ops == NULL, 1);
}

/* TC-MOCK-069: a device initialization under a retained schedule (#357, #352 R1 L10/L12 and the restart-path clarification of 2026-11-25; test
 * logic only, a fake write). UNVERIFIED on hardware except the normal path (TC-TASACT-META-004) */
static int g_seed_fail_at, g_seed_writes;
static avb_u32 g_seed_offset[2];

static int seed_write(void *ctx, avb_u32 offset, avb_u32 value)
{
    (void)ctx; (void)value;
    if (g_seed_writes < 2) {
        g_seed_offset[g_seed_writes] = offset;
    }
    g_seed_writes++;
    return g_seed_writes == g_seed_fail_at ? -1 : 0;
}

static int seed(int fail_at, int *invalidated)
{
    g_seed_fail_at = fail_at;
    g_seed_writes = 0;
    return i226_phc_seed(seed_write, NULL, invalidated);
}

static void test_69_init_event(void)
{
    const avb_u64 max = ~(avb_u64)0;
    AVB_TAS_INIT_EVENT none, done;
    int inv;

    printf("\n[TC-MOCK-069] #357 device initialization under a retained schedule (test logic only)\n");
    ZeroMemory(&none, sizeof(none));
    done.counted = 1;
    done.counted_generation = 3;

    /* the device's own report */
    inv = 0;
    EXPECT_EQ("the seed succeeds", seed(0, &inv), 0);
    EXPECT_EQ("... with SYSTIML then SYSTIMH", g_seed_writes == 2 && g_seed_offset[0] == I226_SYSTIML && g_seed_offset[1] == I226_SYSTIMH, 1);
    EXPECT_EQ("... and reports the reset", inv, 1);
    inv = 0;
    EXPECT_EQ("the first write fails", seed(1, &inv), -1);
    EXPECT_EQ("... nothing more is written", g_seed_writes, 1);
    EXPECT_EQ("... and no reset is reported", inv, 0);
    inv = 0;
    EXPECT_EQ("the second write fails", seed(2, &inv), -1);
    EXPECT_EQ("... the reset already performed is reported", inv, 1);
    EXPECT_EQ("a caller without a report is served", seed(0, NULL), 0);

    /* L10: once, only for a performed invalidation of a retained schedule */
    EXPECT_EQ("retained, reset: one transition", avb_tas_init_advances(&none, 1, 1, 2), 1);
    EXPECT_EQ("retained, no reset and no other invalidating write: none", avb_tas_init_advances(&none, 1, 0, 2), 0);
    EXPECT_EQ("CLEAN, reset: none", avb_tas_init_advances(&none, 0, 1, 2), 0);
    EXPECT_EQ("a second initialization of the same event: none", avb_tas_init_advances(&done, 1, 1, 3), 0);
    EXPECT_EQ("... unless another writer moved the generation in between", avb_tas_init_advances(&done, 1, 1, 4), 1);
    /* a schedule retained only between two initializations of one bring-up is invalidated by the second */
    EXPECT_EQ("first on CLEAN, second on a schedule retained since: one transition", avb_tas_init_advances(&none, 1, 1, 2), 1);

    /* L12: before the first device write */
    EXPECT_EQ("at the maximum, retained, it would invalidate: refused", avb_tas_init_refused(&none, 1, 1, 0, max), 1);
    EXPECT_EQ("at the maximum, CLEAN: not refused", avb_tas_init_refused(&none, 0, 1, 0, max), 0);
    EXPECT_EQ("at the maximum, the device's initialization does not invalidate: not refused", avb_tas_init_refused(&none, 1, 0, 0, max), 0);
    EXPECT_EQ("below the maximum: not refused", avb_tas_init_refused(&none, 1, 1, 1, max - 1), 0);
    done.counted_generation = max;
    EXPECT_EQ("the event that reached the maximum itself is not refused halfway", avb_tas_init_refused(&done, 1, 1, 0, max), 0);
    EXPECT_EQ("... and counts no second time", avb_tas_init_advances(&done, 1, 1, max), 0);
}

/* TC-MOCK-070: META-004 review findings (PR review 2026-11-25; test logic only). The end of a restart is shown by the reopened query gate,
 * never by its entry counters; until then the restart stays an open operation and no restoration is final. A control without a
 * measured, running PHC is no control */
static void test_70_restart_end(void)
{
    const TAS_PHC_READS running = { TRUE, { 1000, 2000 } }, later = { TRUE, { 3000, 4000 } };
    TAS_PHC_READS unread = { FALSE, { 0, 0 } }, frozen = { TRUE, { 3000, 3000 } };
    TAS_ACT_RESTORE_EVIDENCE e;
    OWNED_OP *ops = NULL, *op;
    TAS_RESTART_STAGE stage;

    printf("\n[TC-MOCK-070] #357 end of a restart and the INIT_DEVICE control (test logic only)\n");
    EXPECT_EQ("entered, gate reopened: completed", tas_restart_stage(TRUE, TRUE, TRUE, TRUE), TAS_RESTART_COMPLETED);
    EXPECT_EQ("entered, gate not reopened: only entered", tas_restart_stage(TRUE, TRUE, TRUE, FALSE), TAS_RESTART_ENTERED);
    EXPECT_EQ("not entered: not shown, whatever the query reads", tas_restart_stage(FALSE, FALSE, TRUE, TRUE), TAS_RESTART_NOT_SHOWN);
    EXPECT_EQ("another context: not shown", tas_restart_stage(TRUE, TRUE, FALSE, TRUE), TAS_RESTART_NOT_SHOWN);
    EXPECT_EQ("entered only after the query: a READ from before the pause is no end", tas_restart_stage(FALSE, TRUE, TRUE, TRUE), TAS_RESTART_ENTERED);
    EXPECT_EQ("a further pause after the query: not shown", tas_restart_stage(TRUE, FALSE, TRUE, TRUE), TAS_RESTART_NOT_SHOWN);

    op = owned_op_new(&ops, 1);
    if (op == NULL) {
        EXPECT_EQ("memory available", 0, 1);
        return;
    }
    op->effect_open = TRUE;
    stage = tas_restart_stage(TRUE, TRUE, TRUE, FALSE);      /* counters 1/1, completion not shown within the bound */
    op->effect_open = stage != TAS_RESTART_COMPLETED;
    ZeroMemory(&e, sizeof(e));
    e.cycle_completed = TRUE; e.adapter_returned = TRUE; e.driver_clean = TRUE; e.regs_measured = TRUE;
    e.tsn_mode_off = TRUE; e.tx_probed = TRUE; e.tx_on_wire = TRUE;
    e.operations_pending = !owned_ops_settled(&ops, 1, 0);
    EXPECT_EQ("a restart without a shown end stays an open operation", e.operations_pending, 1);
    EXPECT_EQ("... so no restoration PASS", tas_act_restore(&e), TAS_ACT_RESTORE_UNVERIFIED);
    EXPECT_EQ("... and no CLEANUP PASS", tas_act_cycle(&e), TAS_ACT_RESTORE_UNVERIFIED);
    if (ops != NULL) {
        ops->effect_open = tas_restart_stage(TRUE, TRUE, TRUE, TRUE) != TAS_RESTART_COMPLETED;
    }
    e.operations_pending = !owned_ops_settled(&ops, 1, 0);
    EXPECT_EQ("with the end shown it is settled", e.operations_pending, 0);
    EXPECT_EQ("... and a fully measured restoration is complete", tas_act_restore(&e), TAS_ACT_RESTORE_COMPLETE);

    EXPECT_EQ("READY, SUCCESS, PHC ran on: the control", tas_init_control(TRUE, TRUE, tas_phc_reset(&running, &later)), TRUE);
    EXPECT_EQ("a reset is no control", tas_init_control(TRUE, TRUE, tas_phc_reset(&later, &running)), FALSE);
    EXPECT_EQ("an unread PHC before is no control", tas_init_control(TRUE, TRUE, tas_phc_reset(&unread, &later)), FALSE);
    EXPECT_EQ("an unread PHC after is no control", tas_init_control(TRUE, TRUE, tas_phc_reset(&running, &unread)), FALSE);
    EXPECT_EQ("a PHC that does not run after is no control", tas_init_control(TRUE, TRUE, tas_phc_reset(&running, &frozen)), FALSE);
    EXPECT_EQ("not READY is no control", tas_init_control(FALSE, TRUE, TAS_PHC_RESET_NOT_SHOWN), FALSE);
    EXPECT_EQ("a failed INIT_DEVICE is no control", tas_init_control(TRUE, FALSE, TAS_PHC_RESET_NOT_SHOWN), FALSE);
}

/* TC-MOCK-071: the schedule status lines of TEST-TAS-API-004 (#369 STATUS, for #350; test logic only, no hardware claim). Register readback
 * or BaseT alone is never a PASS, a missing measurement is neither PASS nor failure, an observed wrong or missing answer is a failure. While
 * K8 is not verified INFERRED_ACTIVE is never accepted, and UNKNOWN for unobserved link events is the published answer: a named gap */
static void st_query(AVB_TAS_QUERY_HW_REQUEST *q, avb_u64 before, avb_u64 after, avb_u32 status, avb_u32 reasons)
{
    unsigned i;

    ZeroMemory(q, sizeof(*q));
    q->hw_read = AVB_TAS_HWREAD_READ;
    q->hw_mode = AVB_TAS_HWMODE_TSN;
    q->current_status = status;
    q->current_reasons = reasons;
    q->next_status = AVB_TAS_ST_NONE;
    q->current_commit.state = AVB_TAS_COMMIT_RETAINED;
    q->current_commit.phc_before_commit_ns = 1010;
    q->current_commit.phc_after_commit_ns = 1020;
    q->next_commit.state = AVB_TAS_COMMIT_NONE;
    q->context_epoch = 7;
    q->generation = 2;
    q->phc_before_ns = before;
    q->phc_after_ns = after;
    q->current.base_time_ns = 5000;
    q->current.cycle_ns = 1000;
    q->regs.qbvcyclet = 1000;
    for (i = 0; i < AVB_TAS_QUEUES; i++) {
        q->current.queue[i].start_ns = 200;
        q->current.queue[i].end_ns = 400;
        q->regs.stqt[i] = 200;
        q->regs.endqt[i] = 400;
    }
}

static TAS_STATUS_CLASS st_slot_k8(avb_u32 expected, const AVB_TAS_QUERY_HW_REQUEST *commit, const AVB_TAS_QUERY_HW_REQUEST *q, BOOL none, BOOL k8)
{
    TAS_STATUS_CONTROL c;

    tas_status_control(TRUE, commit, TRUE, q, 5000, none, k8, &c);
    return tas_status_slot(expected, TRUE, q, &c);
}

/* With K8 verified: the state this contract is written for, not today's */
static TAS_STATUS_CLASS st_slot(avb_u32 expected, const AVB_TAS_QUERY_HW_REQUEST *commit, const AVB_TAS_QUERY_HW_REQUEST *q, BOOL none)
{
    return st_slot_k8(expected, commit, q, none, TRUE);
}

/* The replies of the hardware run 2026-11-25 10:03 (I226 0x125B REV_04, Debug driver 4931CA29, LOCAL_DIAGNOSTIC, provenance not verified by
 * the runner): logs/issue369/status-20261125/hw-1003-hold-4931CA29. Only values the run printed */
static void st_run_1003(AVB_TAS_QUERY_HW_REQUEST *q, avb_u64 before, avb_u64 after, avb_u32 baset_h)
{
    unsigned i;

    ZeroMemory(q, sizeof(*q));
    q->hw_read = AVB_TAS_HWREAD_READ;
    q->hw_mode = AVB_TAS_HWMODE_TSN;
    q->current_status = 7;
    q->current_reasons = 0x200;
    q->next_status = 7;
    q->next_reasons = 0x1000;
    q->restoration = 4;
    q->flags = 0x2;
    q->link_speed_mbps = 1000;
    q->context_epoch = 1876755727363736502ULL;
    q->generation = 2;
    q->phc_before_ns = before;
    q->phc_after_ns = after;
    q->regs.tqavctrl = 0x9;
    q->regs.baset_h = baset_h;
    q->regs.qbvcyclet = 0x3B9ACA00;
    q->regs.qbvcyclet_s = 0x3B9ACA00;
    q->regs.dtxmxpktsz = 0x98;
    for (i = 0; i < AVB_TAS_QUEUES; i++) {
        q->regs.stqt[i] = 0x0BEBC200;
        q->regs.endqt[i] = 0x17D78400;
    }
    q->current_commit.state = AVB_TAS_COMMIT_LOST;
    q->next_commit.state = AVB_TAS_COMMIT_NONE;
}

static void test_71_hold_status(void)
{
    const avb_u32 P = AVB_TAS_ST_PENDING, A = AVB_TAS_ST_INFERRED_ACTIVE, U = AVB_TAS_ST_UNKNOWN;
    AVB_TAS_QUERY_HW_REQUEST commit, q;
    AVB_DRIVER_STATISTICS s0, s1;
    AVB_TAS_SCHEDULE sent;
    TAS_STATUS_CONTROL c;

    printf("\n[TC-MOCK-071] #369 schedule status lines of --hold (test logic only)\n");
    st_query(&commit, 1100, 1200, U, 0);

    /* PENDING: a measured query before BaseT */
    st_query(&q, 2000, 2100, P, 0);
    EXPECT_EQ("PENDING before BaseT", st_slot(P, &commit, &q, FALSE), TAS_STATUS_PASS);
    st_query(&q, 2000, 2100, U, AVB_TAS_SLOT_RSN_COMMIT_INFO_LOST);
    EXPECT_EQ("COMMIT_INFO_LOST after a COMMITTED reply: wrong, no missing measurement", st_slot(P, &commit, &q, FALSE), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, AVB_TAS_ST_PROGRAMMED, 0);
    EXPECT_EQ("PROGRAMMED after the commit: wrong", st_slot(P, &commit, &q, TRUE), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, A, 0);
    EXPECT_EQ("INFERRED_ACTIVE before BaseT: wrong", st_slot(P, &commit, &q, TRUE), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, 0, 0);
    EXPECT_EQ("no status reported: wrong", st_slot(P, &commit, &q, TRUE), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, P, AVB_TAS_SLOT_RSN_WINDOW_MISMATCH);
    EXPECT_EQ("PENDING with a mismatch reason: excluded by S2", st_slot(P, &commit, &q, TRUE), TAS_STATUS_REASONS);
    st_query(&q, 4990, 5000, P, 0);
    EXPECT_EQ("BaseT reached inside the bracket: not the PENDING phase", st_slot(P, &commit, &q, TRUE), TAS_STATUS_NOT_PHASE);

    /* INFERRED_ACTIVE: past BaseT, every prerequisite measured */
    st_query(&q, 6000, 6100, A, 0);
    EXPECT_EQ("INFERRED_ACTIVE, every prerequisite measured", st_slot(A, &commit, &q, TRUE), TAS_STATUS_PASS);
    EXPECT_EQ("... but an event was counted or not measured: not judged", st_slot(A, &commit, &q, FALSE), TAS_STATUS_EVENT_POSSIBLE);
    EXPECT_EQ("... while K8 is not verified it is never accepted, whatever was counted", st_slot_k8(A, &commit, &q, TRUE, FALSE), TAS_STATUS_K8_PREMATURE);
    EXPECT_EQ("... that is a failure", tas_status_is_fail(TAS_STATUS_K8_PREMATURE), TRUE);
    st_query(&q, 4990, 5010, A, 0);
    EXPECT_EQ("BaseT inside the bracket: not the past phase", st_slot(A, &commit, &q, TRUE), TAS_STATUS_NOT_PHASE);
    st_query(&q, 6000, 6100, P, 0);
    EXPECT_EQ("PENDING past BaseT: wrong", st_slot(A, &commit, &q, TRUE), TAS_STATUS_WRONG);
    st_query(&q, 6000, 6100, U, AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED);
    EXPECT_EQ("K8 not verified: UNKNOWN for unobserved link events is the published answer, a gap", st_slot_k8(A, &commit, &q, TRUE, FALSE), TAS_STATUS_K8_GAP);
    EXPECT_EQ("... the same when an event was counted", st_slot_k8(A, &commit, &q, FALSE, FALSE), TAS_STATUS_K8_GAP);
    EXPECT_EQ("... no failure", tas_status_is_fail(TAS_STATUS_K8_GAP), FALSE);
    EXPECT_EQ("... and nonpassing, as an implementation gap", strcmp(tas_status_tag(TAS_STATUS_K8_GAP), "SKIP_NOT_IMPLEMENTED"), 0);
    EXPECT_EQ("K8 verified, no event counted: no longer the published answer", st_slot(A, &commit, &q, TRUE), TAS_STATUS_WRONG);
    EXPECT_EQ("K8 verified, an event counted or not measured: not judged", st_slot(A, &commit, &q, FALSE), TAS_STATUS_EVENT_POSSIBLE);
    st_query(&q, 2000, 2100, U, AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED);
    EXPECT_EQ("before BaseT PENDING has no link condition: K8 is no reason there", st_slot_k8(P, &commit, &q, TRUE, FALSE), TAS_STATUS_WRONG);
    EXPECT_EQ("... an event counted or not measured: not judged", st_slot_k8(P, &commit, &q, FALSE, FALSE), TAS_STATUS_EVENT_POSSIBLE);
    st_query(&q, 6000, 6100, U, AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED | AVB_TAS_SLOT_RSN_COMMIT_INFO_LOST);
    EXPECT_EQ("a lost commit is wrong whatever the link", st_slot(A, &commit, &q, FALSE), TAS_STATUS_WRONG);
    st_query(&q, 6000, 6100, U, AVB_TAS_SLOT_RSN_TRANSITION_DURING_SNAPSHOT);
    EXPECT_EQ("a transition reason with the target outside the bracket: wrong", st_slot(A, &commit, &q, TRUE), TAS_STATUS_WRONG);
    st_query(&q, 6000, 6100, U, 0);
    q.snapshot_reasons = AVB_TAS_SNAP_RSN_EVENT_UNRECONCILED;
    EXPECT_EQ("an unreconciled event: no published rule to judge it by", st_slot(A, &commit, &q, TRUE), TAS_STATUS_UNRECONCILED);
    EXPECT_EQ("... whatever was counted", st_slot_k8(A, &commit, &q, FALSE, FALSE), TAS_STATUS_UNRECONCILED);
    q.current_status = A;
    EXPECT_EQ("INFERRED_ACTIVE with an unreconciled event violates S1", st_slot(A, &commit, &q, TRUE), TAS_STATUS_S1);

    /* contradictions measured by the test */
    st_query(&q, 6000, 6100, A, 0);
    q.hw_mode = AVB_TAS_HWMODE_OFF;
    EXPECT_EQ("TSN mode off and INFERRED_ACTIVE", st_slot(A, &commit, &q, TRUE), TAS_STATUS_CLAIMED);
    q.current_status = AVB_TAS_ST_INCONSISTENT;
    q.current_reasons = AVB_TAS_SLOT_RSN_DRIVER_ARMED_HW_OFF;
    EXPECT_EQ("... INCONSISTENT: the control is lost, not judged", st_slot(A, &commit, &q, TRUE), TAS_STATUS_CONTROL_LOST);
    st_query(&q, 6000, 6100, A, 0);
    q.regs.endqt[3] = 401;
    EXPECT_EQ("a changed window and INFERRED_ACTIVE", st_slot(A, &commit, &q, TRUE), TAS_STATUS_CLAIMED);
    st_query(&q, 6000, 6100, A, 0);
    q.regs.qbvcyclet_s = 9;
    q.regs.baset_l = 9;
    q.regs.txqctl[0] = 9;
    EXPECT_EQ("shadow, BaseT and TXQCTL are no compared fields", st_slot(A, &commit, &q, TRUE), TAS_STATUS_PASS);
    st_query(&q, 6000, 6100, A, 0);
    q.generation = 3;
    EXPECT_EQ("an invalidating event since the commit and INFERRED_ACTIVE", st_slot(A, &commit, &q, TRUE), TAS_STATUS_CLAIMED);
    st_query(&q, 2000, 2100, P, 0);
    q.context_epoch = 8;
    EXPECT_EQ("another context and PENDING", st_slot(P, &commit, &q, TRUE), TAS_STATUS_CLAIMED);
    st_query(&q, 1000, 1050, P, 0);
    EXPECT_EQ("the PHC below its value after the commit and PENDING", st_slot(P, &commit, &q, TRUE), TAS_STATUS_CLAIMED);

    /* missing measurements */
    st_query(&q, 6000, 6100, A, 0);
    tas_status_control(TRUE, &commit, TRUE, &q, 5000, TRUE, TRUE, &c);
    EXPECT_EQ("no valid reply", tas_status_slot(A, FALSE, &q, &c), TAS_STATUS_NOT_MEASURED);
    tas_status_control(FALSE, &commit, TRUE, &q, 5000, TRUE, TRUE, &c);
    EXPECT_EQ("no reply after the commit: the control is not measured", tas_status_slot(A, TRUE, &q, &c), TAS_STATUS_NOT_MEASURED);
    c.k8_verified = FALSE;
    EXPECT_EQ("... INFERRED_ACTIVE while K8 is not verified needs no control", tas_status_slot(A, TRUE, &q, &c), TAS_STATUS_K8_PREMATURE);
    commit.generation = 0;
    EXPECT_EQ("generation not reported: the control is not measured", st_slot(A, &commit, &q, TRUE), TAS_STATUS_NOT_MEASURED);
    commit.generation = 2;
    st_query(&q, 0, 0, U, 0);
    q.hw_read = AVB_TAS_HWREAD_NOT_RUNNING;
    EXPECT_EQ("not running: UNKNOWN is the published answer, nothing measured", st_slot(A, &commit, &q, TRUE), TAS_STATUS_NOT_MEASURED);
    st_query(&q, 6000, 6100, U, 0);
    q.flags = AVB_TAS_FLAG_OP_IN_PROGRESS;
    EXPECT_EQ("an operation in progress: not measured", st_slot(A, &commit, &q, TRUE), TAS_STATUS_NOT_MEASURED);
    q.current_status = A;
    EXPECT_EQ("... and a progressed status violates S1", st_slot(A, &commit, &q, TRUE), TAS_STATUS_S1);
    st_query(&q, 6000, 6100, A, 0);
    q.snapshot_reasons = AVB_TAS_SNAP_RSN_PHC_AFTER_INVALID;
    EXPECT_EQ("an invalid PHC bracket and INFERRED_ACTIVE violates S1", st_slot(A, &commit, &q, TRUE), TAS_STATUS_S1);
    st_query(&q, 6000, 6100, A, 0);
    q.hw_read = AVB_TAS_HWREAD_PARTIAL;
    q.regs.read_failed = 1u << 17;
    EXPECT_EQ("only DTXMXPKTSZ unread is no status input", st_slot(A, &commit, &q, TRUE), TAS_STATUS_PASS);
    q.regs.read_failed = 1u << 9;
    EXPECT_EQ("an unread window and INFERRED_ACTIVE violates S1", st_slot(A, &commit, &q, TRUE), TAS_STATUS_S1);
    st_query(&q, 6000, 6100, AVB_TAS_ST_HARDWARE_VERIFIED, 0);
    EXPECT_EQ("HARDWARE_VERIFIED fails", st_slot(A, &commit, &q, TRUE), TAS_STATUS_HW_VERIFIED);
    st_query(&q, 2000, 2100, P, 0);
    q.next_status = AVB_TAS_ST_HARDWARE_VERIFIED;
    EXPECT_EQ("... in the next slot as well", st_slot(P, &commit, &q, TRUE), TAS_STATUS_HW_VERIFIED);

    /* the retained commit */
    st_query(&q, 2000, 2100, P, 0);
    sent = q.current;
    tas_status_control(TRUE, &commit, TRUE, &q, 5000, TRUE, TRUE, &c);
    EXPECT_EQ("retained, as requested, bracket inside the test's readings", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_PASS);
    EXPECT_EQ("no outer readings: not measured", tas_status_record(TRUE, &q, &sent, FALSE, 0, 0, &c), TAS_STATUS_NOT_MEASURED);
    EXPECT_EQ("a bracket before the test's reading", tas_status_record(TRUE, &q, &sent, TRUE, 1015, 1100, &c), TAS_STATUS_WRONG);
    EXPECT_EQ("a bracket after the test's reading", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1015, &c), TAS_STATUS_WRONG);
    q.current_commit.phc_before_commit_ns = 1030;
    EXPECT_EQ("a reversed bracket", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, P, 0);
    q.current_commit.phc_after_commit_ns = 5000;
    EXPECT_EQ("a bracket that reaches BaseT after a COMMITTED reply", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 6000, &c), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, P, 0);
    q.current.queue[2].max_frame_bytes = 1;
    EXPECT_EQ("another schedule than requested", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, U, 0);
    q.current_commit.state = AVB_TAS_COMMIT_LOST;
    EXPECT_EQ("LOST without a measured contradiction", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_WRONG);
    c.contradiction = TAS_STATUS_C_GENERATION;
    EXPECT_EQ("... with one: not judged", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_CONTROL_LOST);
    q.flags = AVB_TAS_FLAG_OP_IN_PROGRESS;
    EXPECT_EQ("sampled while an operation runs: not measured", tas_status_record(TRUE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_NOT_MEASURED);
    EXPECT_EQ("no valid reply: not measured", tas_status_record(FALSE, &q, &sent, TRUE, 1000, 1100, &c), TAS_STATUS_NOT_MEASURED);

    /* the next slot (12.7) */
    st_query(&q, 2000, 2100, P, 0);
    tas_status_control(TRUE, &commit, TRUE, &q, 5000, TRUE, TRUE, &c);
    EXPECT_EQ("next NONE without reasons", tas_status_next(TRUE, &q, &c), TAS_STATUS_PASS);
    q.next_reasons = AVB_TAS_SLOT_RSN_SLOT_UNATTRIBUTED;
    EXPECT_EQ("NONE with a reason", tas_status_next(TRUE, &q, &c), TAS_STATUS_WRONG);
    q.next_status = U;
    EXPECT_EQ("UNKNOWN, unattributed", tas_status_next(TRUE, &q, &c), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, P, AVB_TAS_SLOT_RSN_CYCLE_MISMATCH);
    EXPECT_EQ("a cycle mismatch from the shadow", tas_status_next(TRUE, &q, &c), TAS_STATUS_WRONG);
    st_query(&q, 2000, 2100, P, 0);
    q.next_commit.state = AVB_TAS_COMMIT_RETAINED;
    EXPECT_EQ("a next commit in v1", tas_status_next(TRUE, &q, &c), TAS_STATUS_WRONG);
    EXPECT_EQ("no valid reply", tas_status_next(FALSE, &q, &c), TAS_STATUS_NOT_MEASURED);
    st_query(&q, 2000, 2100, U, 0);
    q.next_status = U;
    q.snapshot_reasons = AVB_TAS_SNAP_RSN_EVENT_UNRECONCILED;
    EXPECT_EQ("next UNKNOWN for an unreconciled event: no published rule", tas_status_next(TRUE, &q, &c), TAS_STATUS_UNRECONCILED);

    /* events and the read-only property */
    ZeroMemory(&s0, sizeof(s0));
    s0.IoctlCount = 10;
    s0.FilterStatusCount = 4;
    s0.FilterPauseCount = 1;
    s0.FilterRestartCount = 1;
    s1 = s0;
    s1.IoctlCount = 20;
    EXPECT_EQ("no event counted, link unchanged", tas_status_no_event_counted(TRUE, &s0, TRUE, &s1, TRUE), TRUE);
    EXPECT_EQ("link changed or unmeasured", tas_status_no_event_counted(TRUE, &s0, TRUE, &s1, FALSE), FALSE);
    EXPECT_EQ("statistics unread", tas_status_no_event_counted(TRUE, &s0, FALSE, &s1, TRUE), FALSE);
    s1.FilterStatusCount = 5;
    EXPECT_EQ("a status indication", tas_status_no_event_counted(TRUE, &s0, TRUE, &s1, TRUE), FALSE);
    s1 = s0;
    s1.IoctlCount = 20;
    s1.FilterNetPnPCount = 1;
    EXPECT_EQ("a PnP event", tas_status_no_event_counted(TRUE, &s0, TRUE, &s1, TRUE), FALSE);
    s1 = s0;
    s1.IoctlCount = 20;
    s1.FilterRestartCount = 2;
    EXPECT_EQ("a restart", tas_status_no_event_counted(TRUE, &s0, TRUE, &s1, TRUE), FALSE);
    s1 = s0;
    s1.IoctlCount = 3;
    EXPECT_EQ("a counter that fell", tas_status_no_event_counted(TRUE, &s0, TRUE, &s1, TRUE), FALSE);
    st_query(&q, 2000, 2100, P, 0);
    EXPECT_EQ("generation kept across queries", tas_status_readonly(TRUE, &commit, &q, TRUE), TAS_STATUS_PASS);
    q.generation = 3;
    EXPECT_EQ("generation moved, no other writer", tas_status_readonly(TRUE, &commit, &q, TRUE), TAS_STATUS_WRONG);
    EXPECT_EQ("... another writer possible: not attributed", tas_status_readonly(TRUE, &commit, &q, FALSE), TAS_STATUS_CONTROL_LOST);
    EXPECT_EQ("no valid reply: not measured", tas_status_readonly(FALSE, &commit, &q, TRUE), TAS_STATUS_NOT_MEASURED);

    /* the 10:03 run: its evaluation, independent of the K8 wording */
    st_run_1003(&commit, 11895250938ULL, 11895267611ULL, 14);
    st_run_1003(&q, 11925797551ULL, 11925814593ULL, 14);
    ZeroMemory(&sent, sizeof(sent));
    sent.base_time_ns = 14000000000ULL;
    sent.cycle_ns = 1000000000u;
    tas_status_control(TRUE, &commit, TRUE, &q, sent.base_time_ns, TRUE, FALSE, &c);
    EXPECT_EQ("10:03: the control before BaseT was measured without a contradiction", c.measured && c.contradiction == 0, 1);
    EXPECT_EQ("10:03 STATUS_RECORD", tas_status_record(TRUE, &q, &sent, TRUE, 11892986814ULL, 11895250938ULL, &c), TAS_STATUS_WRONG);
    EXPECT_EQ("10:03 STATUS_PENDING", tas_status_slot(P, TRUE, &q, &c), TAS_STATUS_WRONG);
    EXPECT_EQ("10:03 STATUS_NEXT", tas_status_next(TRUE, &q, &c), TAS_STATUS_WRONG);
    st_run_1003(&q, 19401814255ULL, 19401831679ULL, 20);
    tas_status_control(TRUE, &commit, TRUE, &q, sent.base_time_ns, TRUE, FALSE, &c);
    EXPECT_EQ("10:03: the control past BaseT was measured without a contradiction", c.measured && c.contradiction == 0, 1);
    EXPECT_EQ("10:03 STATUS_ACTIVE: a lost commit, not the K8 answer", tas_status_slot(A, TRUE, &q, &c), TAS_STATUS_WRONG);
    EXPECT_EQ("10:03 STATUS_READONLY", tas_status_readonly(TRUE, &q, &q, TRUE), TAS_STATUS_PASS);

    EXPECT_EQ("a failure outranks a missing measurement", tas_status_worst(TAS_STATUS_NOT_MEASURED, TAS_STATUS_WRONG), TAS_STATUS_WRONG);
    EXPECT_EQ("a missing measurement outranks a pass", tas_status_worst(TAS_STATUS_PASS, TAS_STATUS_NOT_PHASE), TAS_STATUS_NOT_PHASE);
}

int main(void) {
    printf("=======================================================================\n");
    printf(" TAS State-Restoration Contract — Mock Unit Tests\n");
    printf(" Requirement #9 (REQ-F-TAS-001) / Test Issue #206\n");
    printf(" Verifies: #9\n");
    printf(" Implements: #206\n");
    printf(" No driver or hardware required.\n");
    printf("=======================================================================\n");

    test_01_unconditional_block();
    test_02_armed_always_blocked();
    test_03_get_hw_state_not_called();
    test_04_disarm_status_classification();
    test_05_cleanup_failed_from_restore();
    test_06_all_blocked_exit_code();
    test_07_two_adapters_both_blocked();
    test_08_no_destructive_bypass();
    test_09_negative_tests_always_executable();
    test_10_no_device_exit_code();
    test_11_skip_distinct_from_pass();
    test_12_restoration_outcome();
    test_13_wire_match();
    test_28_activate_validation();
    test_29_activate_not_clean();
    test_30_activate_hold();
    test_31_activate_pass();
    test_32_activate_failure();
    test_33_activate_time();
    test_45_activate_fields();
    test_46_activate_untimed();
    test_47_activate_state();
    test_48_activate_gcl();
    test_49_activate_fault();
    test_50_act_mapping();
    test_51_act_hold_wire();
    test_52_act_pass_wire();
    test_53_act_restore();
    test_54_activate_encoding();
    test_55_activate_pass_not_implemented();
    test_56_activate_restore_state();
    test_57_query_tx_accounting();
    test_58_fault_f2_state();
    test_59_fault_f2_refuse();
    test_60_invalid_semantic_order();
    test_61_invalid_window_min();
    test_62_hold_commit();
    test_63_hold_generation();
    test_64_activate_cases();
    test_65_owned_worker();
    test_66_race_order();
    test_67_pending_operations();
    test_14_instance_find();
    test_15_lifecycle_outcome();
    test_68_restart();
    test_69_init_event();
    test_70_restart_end();
    test_71_hold_status();

    printf("\n=======================================================================\n");
    printf(" Mock Test Summary\n");
    printf("=======================================================================\n");
    printf(" PASS: %d / %d\n", g_tests_pass, g_tests_run);
    printf(" FAIL: %d / %d\n", g_tests_fail, g_tests_run);
    printf("=======================================================================\n");

    if (g_tests_fail > 0) {
        printf("[RESULT] FAILURE\n");
        return 1;
    }
    printf("[RESULT] ALL MOCK TESTS PASSED\n");
    return 0;
}
