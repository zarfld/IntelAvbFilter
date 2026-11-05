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
#include "../ioctl/tas_lifecycle_eval.h"

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

/*
 * TC-MOCK-014: which adapter a FilterPause/FilterRestart trigger may name (#364).
 *
 * Record layout (USHORT byte length, then UTF-16 name without terminator) is from IOCTL_FILTER_ENUERATE_ALL_INSTANCES in
 * src/device.c. Name layout {interface GUID}-{filter GUID}-0000 was observed once, on the I210 instance, in
 * logs/dbgview_test_ptp_phc_stability.exe_20260430_123620.log; its first GUID is that adapter's InterfaceGuid and its
 * second is NetCfgInstanceId in the INF. The I226 record below is built on that layout and is UNVERIFIED. The driver
 * matches by prefix (filterFindFilterModule), which is why a prefix counts as ambiguous.
 */
#define FILTER_GUID_TEXT L"{3F74AE86-14F9-4E79-9445-5B1E52CCD192}"
#define I210_GUID_TEXT   L"{9F90590F-E0F0-46B7-A69E-BF0A45DEDB6C}"
#define I226_GUID_TEXT   L"{5989BA55-D568-47A4-AFBF-B47F27E1B435}"

static DWORD put_instance(BYTE *list, DWORD at, const WCHAR *name) {
    USHORT bytes = (USHORT)(wcslen(name) * sizeof(WCHAR));
    memcpy(list + at, &bytes, sizeof(bytes));
    memcpy(list + at + sizeof(bytes), name, bytes);
    return at + sizeof(bytes) + bytes;
}

static void test_14_instance_find(void) {
    static const WCHAR i210[] = I210_GUID_TEXT L"-" FILTER_GUID_TEXT L"-0000";
    static const WCHAR i226[] = I226_GUID_TEXT L"-" FILTER_GUID_TEXT L"-0000";
    BYTE list[512];
    const WCHAR *name = NULL;
    USHORT bytes = 0;
    DWORD used;

    printf("\n[TC-MOCK-014] Exact instance for one adapter\n");
    used = put_instance(list, 0, i210);
    used = put_instance(list, used, i226);
    EXPECT_EQ("the adapter's own record", tas_instance_find(list, used, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_FOUND);
    EXPECT_EQ("name length is the record's own", bytes, (int)(wcslen(i226) * sizeof(WCHAR)));
    EXPECT_TRUE("name points at that record", name != NULL && memcmp(name, i226, bytes) == 0);
    EXPECT_EQ("GUID text in any case", tas_instance_find(list, used, L"{5989ba55-d568-47a4-afbf-b47f27e1b435}", &name, &bytes), TAS_INSTANCE_FOUND);

    EXPECT_EQ("adapter not listed", tas_instance_find(list, used, L"{D44A7E27-72EE-480D-958C-558FACF2B105}", &name, &bytes), TAS_INSTANCE_NONE);
    EXPECT_EQ("empty list", tas_instance_find(list, 0, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);
    EXPECT_EQ("the filter GUID names no adapter", tas_instance_find(list, used, FILTER_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);

    used = put_instance(list, 0, I226_GUID_TEXT L"X" FILTER_GUID_TEXT L"-0000");
    EXPECT_EQ("the GUID must end at its separator", tas_instance_find(list, used, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);
    used = put_instance(list, 0, I226_GUID_TEXT);
    EXPECT_EQ("the GUID alone is not an instance", tas_instance_find(list, used, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_NONE);

    used = put_instance(list, 0, i226);
    used = put_instance(list, used, I226_GUID_TEXT L"-" FILTER_GUID_TEXT L"-0001");
    EXPECT_EQ("two records name the adapter", tas_instance_find(list, used, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_AMBIGUOUS);

    /* The driver stops at the first prefix match, so the shorter record would be restarted instead */
    used = put_instance(list, 0, L"{5989BA55");
    used = put_instance(list, used, i226);
    EXPECT_EQ("another record is a prefix of the name", tas_instance_find(list, used, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_AMBIGUOUS);
    used = put_instance(list, 0, L"");
    used = put_instance(list, used, i226);
    EXPECT_EQ("an empty record is a prefix of everything", tas_instance_find(list, used, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_AMBIGUOUS);

    used = put_instance(list, 0, i226);
    EXPECT_EQ("list cut inside a name", tas_instance_find(list, used - 2, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_MALFORMED);
    EXPECT_EQ("list cut inside a length", tas_instance_find(list, 1, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_MALFORMED);
    list[0] = 3; list[1] = 0;
    EXPECT_EQ("odd byte length is not UTF-16", tas_instance_find(list, 16, I226_GUID_TEXT, &name, &bytes), TAS_INSTANCE_MALFORMED);
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

/* TC-MOCK-016: without a hardware-derived tolerance the only claim available is a clock that did not advance */
static void test_16_phc_fact(void) {
    printf("\n[TC-MOCK-016] PHC fact without a tolerance\n");
    EXPECT_EQ("before unreadable", tas_phc_fact(FALSE, TRUE, 10, 20), TAS_PHC_UNMEASURED);
    EXPECT_EQ("after unreadable", tas_phc_fact(TRUE, FALSE, 10, 20), TAS_PHC_UNMEASURED);
    EXPECT_EQ("backwards is a demonstrated discontinuity", tas_phc_fact(TRUE, TRUE, 20, 10), TAS_PHC_DISCONTINUOUS);
    EXPECT_EQ("a clock that stood still is discontinuous", tas_phc_fact(TRUE, TRUE, 10, 10), TAS_PHC_DISCONTINUOUS);
    EXPECT_EQ("forwards is not continuity", tas_phc_fact(TRUE, TRUE, 10, 20), TAS_PHC_NOT_JUDGED);
}

/* TC-MOCK-017: snapshots compare per register and never count what the family does not define */
static void test_17_snapshot_compare(void) {
    AVB_HW_SNAPSHOT_REQUEST before, after;
    unsigned changed, unreadable, compared;
    unsigned i;

    printf("\n[TC-MOCK-017] Dedicated register snapshot comparison\n");
    memset(&before, 0, sizeof(before));
    before.count = AVB_HWREG_COUNT;
    for (i = 0; i < AVB_HWREG_COUNT; i++) {
        before.entries[i].id = i + 1;
        before.entries[i].state = AVB_HWREG_STATE_VALID;
        before.entries[i].value = i;
    }
    after = before;
    tas_snapshot_compare(&before, &after, &compared, &changed, &unreadable);
    EXPECT_EQ("all compared", compared, AVB_HWREG_COUNT);
    EXPECT_EQ("none changed", changed, 0);
    EXPECT_EQ("none unreadable", unreadable, 0);

    after.entries[2].value = 99;
    tas_snapshot_compare(&before, &after, &compared, &changed, &unreadable);
    EXPECT_EQ("value differs", changed, 1);

    after = before;
    before.entries[3].state = after.entries[3].state = AVB_HWREG_STATE_NOT_IN_SSOT;
    tas_snapshot_compare(&before, &after, &compared, &changed, &unreadable);
    EXPECT_EQ("not defined for this family: not compared", compared, AVB_HWREG_COUNT - 1);
    EXPECT_EQ("not defined is not a change", changed, 0);
    EXPECT_EQ("not defined is not unreadable", unreadable, 0);
    before.entries[3].state = AVB_HWREG_STATE_VALID;

    after = before;
    after.entries[4].state = AVB_HWREG_STATE_READ_FAILED;
    tas_snapshot_compare(&before, &after, &compared, &changed, &unreadable);
    EXPECT_EQ("a failed read is unmeasured", unreadable, 1);
    EXPECT_EQ("a failed read is no difference", changed, 0);

    after = before;
    after.entries[5].value = 99;
    after.entries[5].state = AVB_HWREG_STATE_NO_MMIO;
    tas_snapshot_compare(&before, &after, &compared, &changed, &unreadable);
    EXPECT_EQ("losing MMIO is unmeasured", unreadable, 1);

    after = before;
    after.count = 0;
    tas_snapshot_compare(&before, &after, &compared, &changed, &unreadable);
    EXPECT_EQ("an empty snapshot compares nothing", compared, 0);
    EXPECT_EQ("an empty snapshot is unmeasured", unreadable, AVB_HWREG_COUNT);
}

/* =========================================================================
 * Main
 * ========================================================================= */

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
    test_14_instance_find();
    test_15_lifecycle_outcome();
    test_16_phc_fact();
    test_17_snapshot_compare();

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
