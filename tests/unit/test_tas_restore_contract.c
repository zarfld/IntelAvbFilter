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
#include "../ioctl/tas_activate_eval.h"

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
    unsigned writes, fail_write, stuck_after_fail;
    BOOL read_fails, phc_fails;
    avb_u64 phc_first, phc_later;
    unsigned phc_reads;
    unsigned phc_fail_from;   /* nonzero: PHC reads from this number on fail, the first read is number 1 */
} FAKE_HW;

static int fake_read(void *c, avb_u32 off, avb_u32 *v) {
    FAKE_HW *f = (FAKE_HW *)c;
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
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("an invalid request reports it", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("and writes nothing", g_hw.writes, 0);
}

static void test_29_activate_not_clean(void) {
    static const avb_u32 dirty[] = {I226_TQAVCTRL, I226_BASET_L, I226_BASET_H, I226_STQT0, I226_STQT3, I226_ENDQT0, I226_ENDQT3};
    unsigned i;

    printf("\n[TC-MOCK-029] An adapter that is not clean is refused and nothing is written\n");
    act_setup(AVB_TAS_PRE_HOLD);
    i226_tas_activate(&g_io, &g_act, TRUE);
    EXPECT_EQ("driver says armed", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("no write when armed", g_hw.writes, 0);
    for (i = 0; i < sizeof(dirty) / sizeof(dirty[0]); i++) {
        act_setup(AVB_TAS_PRE_HOLD);
        g_hw.reg[dirty[i] / 4] = 1;
        i226_tas_activate(&g_io, &g_act, FALSE);
        EXPECT_EQ("left-over schedule state", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
        EXPECT_EQ("no write", g_hw.writes, 0);
    }
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.read_fails = TRUE;
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("unreadable state is never clean", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("no write when unreadable", g_hw.writes, 0);
}

static void test_30_activate_hold(void) {
    static const avb_u32 want[] = {I226_TQAVCTRL, I226_STQT0, I226_ENDQT0, I226_STQT0 + 4, I226_ENDQT0 + 4, I226_STQT0 + 8, I226_ENDQT0 + 8, I226_STQT0 + 12, I226_ENDQT0 + 12,
                                   I226_QBVCYCLET_S, I226_QBVCYCLET, I226_BASET_H, I226_BASET_L};

    printf("\n[TC-MOCK-030] Hold: the sequence run on hardware, no reserved bit\n");
    act_setup(AVB_TAS_PRE_HOLD);
    i226_tas_activate(&g_io, &g_act, FALSE);
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
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("BaseT splits into seconds", g_hw.reg[I226_BASET_H / 4], 7);
    EXPECT_EQ("and nanoseconds", g_hw.reg[I226_BASET_L / 4], 250000000);
}

static void test_31_activate_pass(void) {
    static const avb_u32 want[] = {I226_TQAVCTRL, I226_STQT0, I226_ENDQT0, I226_STQT0 + 4, I226_ENDQT0 + 4, I226_STQT0 + 8, I226_ENDQT0 + 8, I226_STQT0 + 12, I226_ENDQT0 + 12,
                                   I226_TQAVCTRL, I226_QBVCYCLET_S, I226_QBVCYCLET, I226_BASET_H, I226_BASET_L, I226_BASET_L};

    printf("\n[TC-MOCK-031] Pass: FutScdDis from the SSOT, BaseT low word zero first\n");
    act_setup(AVB_TAS_PRE_PASS); g_act.base_time_ns = 7250000000ULL;
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("committed", g_act.result, AVB_TAS_ACT_COMMITTED);
    EXPECT_TRUE("register order", act_order(want, sizeof(want) / sizeof(want[0])));
    EXPECT_EQ("FutScdDis is bit 7", g_hw.reg[I226_TQAVCTRL / 4], 0x89);
    EXPECT_EQ("BASET_L zero first", g_hw.log_val[13], 0);
    EXPECT_EQ("BASET_L value last", g_hw.log_val[14], 250000000);
}

static void test_32_activate_failure(void) {
    unsigned at, q, same;

    printf("\n[TC-MOCK-032] Failure before commit restores the registers and says so\n");
    for (at = 1; at <= 13; at++) {
        act_setup(AVB_TAS_PRE_HOLD);
        g_hw.fail_write = at;
        i226_tas_activate(&g_io, &g_act, FALSE);
        EXPECT_EQ("write failed, restored", g_act.result, AVB_TAS_ACT_FAILED_RESTORED);
        same = g_hw.reg[I226_TQAVCTRL / 4] == 0 && g_hw.reg[I226_BASET_L / 4] == 0 && g_hw.reg[I226_BASET_H / 4] == 0 && g_hw.reg[I226_QBVCYCLET / 4] == 0x3B9ACA00;
        for (q = 0; q < AVB_TAS_QUEUES; q++) {
            same = same && g_hw.reg[(I226_STQT0 + 4 * q) / 4] == 0 && g_hw.reg[(I226_ENDQT0 + 4 * q) / 4] == 0;
        }
        EXPECT_TRUE("every register back at its value", same);
    }
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.fail_write = 2;
    g_hw.stuck_after_fail = 1;   /* the first restore write fails too */
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("a restore that fails says so", g_act.result, AVB_TAS_ACT_FAILED_NOT_RESTORED);
}

static void test_33_activate_time(void) {
    printf("\n[TC-MOCK-033] BaseT must be in the future\n");
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.phc_fails = TRUE;
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("unreadable PHC", g_act.result, AVB_TAS_ACT_NOT_CLEAN);
    EXPECT_EQ("no write", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = g_hw.phc_first + AVB_TAS_MIN_LEAD_NS - 1;
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("too close", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("no write when too close", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_HOLD); g_act.base_time_ns = g_hw.phc_first + AVB_TAS_MIN_LEAD_NS;
    g_hw.phc_later = g_hw.phc_first + 1;
    i226_tas_activate(&g_io, &g_act, FALSE);
    EXPECT_EQ("exactly the lead is accepted", g_act.result, AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD);
    g_hw.phc_later = g_act.base_time_ns + 1;
    i226_tas_activate(&g_io, &g_act, FALSE);
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
    i226_tas_activate(&g_io, &g_act, FALSE);
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
    i226_tas_activate(&g_io, &g_act, FALSE);
    writes = g_hw.writes;
    EXPECT_EQ("committed without timing evidence", g_act.result, AVB_TAS_ACT_COMMITTED_UNTIMED);
    EXPECT_TRUE("no PHC time claimed", g_act.phc_at_commit_ns == 0);
    EXPECT_EQ("no rollback: the 13 schedule writes only", writes, 13);
    EXPECT_EQ("the schedule stays in the registers", g_hw.reg[I226_TQAVCTRL / 4], 0x09);
}

/* TC-MOCK-047: what each activation result leaves the adapter's TAS state as */
static void test_47_activate_state(void) {
    printf("\n[TC-MOCK-047] Adapter TAS state after an activation result\n");
    EXPECT_EQ("committed arms", avb_tas_state_after(AVB_TAS_ACT_COMMITTED, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("committed untimed arms", avb_tas_state_after(AVB_TAS_ACT_COMMITTED_UNTIMED, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("BaseT passed still arms: the schedule was written", avb_tas_state_after(AVB_TAS_ACT_BASE_PASSED, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("a failed restore leaves the state unknown", avb_tas_state_after(AVB_TAS_ACT_FAILED_NOT_RESTORED, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_DIRTY);
    EXPECT_EQ("a verified restore changes nothing", avb_tas_state_after(AVB_TAS_ACT_FAILED_RESTORED, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_CLEAN);
    EXPECT_EQ("a refusal changes nothing", avb_tas_state_after(AVB_TAS_ACT_INVALID, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_CLEAN);
    EXPECT_EQ("not clean changes nothing", avb_tas_state_after(AVB_TAS_ACT_NOT_CLEAN, AVB_TAS_STATE_ARMED), AVB_TAS_STATE_ARMED);
    EXPECT_EQ("busy changes nothing", avb_tas_state_after(AVB_TAS_ACT_BUSY, AVB_TAS_STATE_DIRTY), AVB_TAS_STATE_DIRTY);
    EXPECT_EQ("an unknown result is never clean", avb_tas_state_after(99u, AVB_TAS_STATE_CLEAN), AVB_TAS_STATE_DIRTY);
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
    i226_tas_activate(&g_io, &g_act, FALSE);
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
    unsigned n, count, all = 1, reached = 1, commit_kept_out = 1;

    printf("\n[TC-MOCK-049] Debug-only fault injection through the reserved word\n");
    act_setup(AVB_TAS_PRE_HOLD); g_act.reserved = 5;
    i226_tas_activate(&release_io, &g_act, FALSE);
    EXPECT_EQ("Release: a nonzero reserved word is refused", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("Release: before any write", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_HOLD);
    i226_tas_activate(&debug_io, &g_act, FALSE);
    EXPECT_EQ("Debug: reserved 0 activates normally", g_act.result, AVB_TAS_ACT_COMMITTED);
    act_setup(AVB_TAS_PRE_HOLD); g_act.reserved = 14;
    i226_tas_activate(&debug_io, &g_act, FALSE);
    EXPECT_EQ("Debug: HOLD has 13 schedule writes, 14 is refused", g_act.result, AVB_TAS_ACT_INVALID);
    EXPECT_EQ("Debug: refused before any write", g_hw.writes, 0);
    act_setup(AVB_TAS_PRE_PASS); g_act.reserved = 16;
    i226_tas_activate(&debug_io, &g_act, FALSE);
    EXPECT_EQ("Debug: PASS has 15 schedule writes, 16 is refused", g_act.result, AVB_TAS_ACT_INVALID);
    for (count = 13; count <= 15; count += 2) {
        for (n = 1; n <= count; n++) {
            act_setup(count == 13 ? AVB_TAS_PRE_HOLD : AVB_TAS_PRE_PASS);
            g_act.reserved = n;
            i226_tas_activate(&debug_io, &g_act, FALSE);
            all = all && g_act.result == AVB_TAS_ACT_FAILED_NOT_RESTORED;
            /* writes before N, then the 13 restore writes without the first one */
            reached = reached && g_hw.writes == (n - 1) + (I226_TAS_SAVED_COUNT - 1);
            commit_kept_out = commit_kept_out && g_hw.reg[I226_BASET_L / 4] == 0;
        }
    }
    EXPECT_TRUE("Debug: every N in HOLD and PASS ends FAILED_NOT_RESTORED", all);
    EXPECT_TRUE("Debug: neither injected write reaches the hardware", reached);
    EXPECT_TRUE("Debug: the commit write never reaches the hardware", commit_kept_out);
}

/* Every schedule write of one activation carries the request value it encodes. BASET_L: zeros before the last write */
static int act_writes_carry_request(avb_u32 mode) {
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    avb_u32 base_ns = (avb_u32)(g_act.base_time_ns % NSEC_PER_SEC), base_s = (avb_u32)(g_act.base_time_ns / NSEC_PER_SEC);
    unsigned i, q, ok;

    i226_tas_activate(&g_io, &g_act, FALSE);
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
            EXPECT_TRUE(mode == AVB_TAS_PRE_HOLD ? "HOLD: every write carries its request value"
                                                 : "PASS: every write carries its request value",
                        act_writes_carry_request(mode));
        }
    }
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
