/**
 * @file test_idle_verdict.c
 * @brief Deterministic unit tests for the TC-LCY-IDLE-001 verdict logic.
 *
 * Implements: TC-IDLE-UNIT-001 through TC-IDLE-UNIT-010
 * Traces to:  #328 (IntelAvbFilter absent after NIC power management)
 * Verifies:   idle_verdict.h — ComputeIdleVerdict() truth table
 *
 * Tests ComputeIdleVerdict() and ComputeLifecycleEvidence() with synthetic
 * IdleVerdictInput values.  No Windows device access; no hardware required;
 * CI-safe.
 *
 * Truth table under test (from idle_verdict.h):
 *   !phase_a_ok                   → TC_SKIP  (2) BASELINE_UNAVAILABLE
 *   !phase_c_ok                   → TC_FAIL  (1) DEVICE_FAILURE_OBSERVED
 *   LIFECYCLE_CONFIRMED           → TC_PASS  (0) VERIFIED_TRANSITION
 *   LIFECYCLE_AMBIGUOUS           → TC_INCONCLUSIVE (3)
 *   LIFECYCLE_NOT_OBSERVED        → TC_INCONCLUSIVE (3) TRANSITION_NOT_OBSERVED
 *
 * Test cases:
 *   TC-IDLE-UNIT-001  No transition, device accessible → INCONCLUSIVE
 *   TC-IDLE-UNIT-002  Enter pressed but no counter change → INCONCLUSIVE
 *   TC-IDLE-UNIT-003  PauseRestartGeneration increased → PASS
 *   TC-IDLE-UNIT-004  FilterDetach + FilterAttach increased → PASS
 *   TC-IDLE-UNIT-005  FilterPauseCount + FilterRestartCount increased → PASS
 *   TC-IDLE-UNIT-006  Device path lost (error 2) → FAIL
 *   TC-IDLE-UNIT-007  Lifecycle counter decreased → INCONCLUSIVE (AMBIGUOUS)
 *   TC-IDLE-UNIT-008  Adapter index changes (no Phase A/C match) → INCONCLUSIVE
 *   TC-IDLE-UNIT-009  Phase A fails (baseline unavailable) → SKIP
 *   TC-IDLE-UNIT-010  Phase C stats unavailable (stats_valid==0) → INCONCLUSIVE
 *
 * Build (see tools/build/Build-Tests.ps1):
 *   cl /nologo /W4 /WX /Zi /DWIN32 /D_WIN32_WINNT=0x0A00
 *      -I include -I external/intel_avb/lib
 *      tests\unit\lifecycle\test_idle_verdict.c
 *      /Fe:build\x64\Debug\test_idle_verdict.exe
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avb_ioctl.h"
#include "../../hardware/idle_verdict.h"

/* ── Minimal test harness ────────────────────────────────────────────────── */
static int g_passed  = 0;
static int g_failed  = 0;

static void AssertVerdict(const char *tc_id, int expected, int actual,
                          const char *reason)
{
    const char *labels[] = { "TC_PASS(0)", "TC_FAIL(1)",
                              "TC_SKIP(2)", "TC_INCONCLUSIVE(3)" };
    const char *exp_label = (expected >= 0 && expected <= 3) ? labels[expected] : "?";
    const char *act_label = (actual   >= 0 && actual   <= 3) ? labels[actual]   : "?";

    if (expected == actual) {
        printf("[PASS] %s → %s\n", tc_id, act_label);
        g_passed++;
    } else {
        printf("[FAIL] %s → expected %s, got %s\n", tc_id, exp_label, act_label);
        printf("       reason: %s\n", reason);
        g_failed++;
    }
}

/* Helper: build a valid single-adapter Phase A snapshot */
static void FillAdapter(AdapterSnapshot *s,
                        avb_u16 vid, avb_u16 did, avb_u32 idx,
                        avb_u64 attach, avb_u64 pause, avb_u64 restart,
                        avb_u64 detach, avb_u64 gen, avb_u64 oid)
{
    memset(s, 0, sizeof(*s));
    s->vendor_id              = vid;
    s->device_id              = did;
    s->enum_index             = idx;
    s->stats_valid            = 1;
    s->FilterAttachCount      = attach;
    s->FilterPauseCount       = pause;
    s->FilterRestartCount     = restart;
    s->FilterDetachCount      = detach;
    s->PauseRestartGeneration = gen;
    s->OidRequestCount        = oid;
}

/* ── TC-IDLE-UNIT-001: no counter change, device accessible → INCONCLUSIVE ─ */
static void Test_NoTransition_Accessible(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 5, 5, 2, 5, 100);
    in.snap_c[0] = in.snap_a[0];  /* identical — no change */

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-001", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-002: Enter pressed but no lifecycle change → INCONCLUSIVE ─ */
/*
 * Simulates the user pressing Enter immediately without performing a power
 * transition.  Counters are identical; verdict must be INCONCLUSIVE.
 */
static void Test_EnterWithoutTransition(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x1533, 0, 1, 1, 1, 0, 1, 487);
    in.snap_c[0] = in.snap_a[0];  /* OID-only traffic; no lifecycle delta */

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-002", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-003: PauseRestartGeneration increased → PASS ──────────── */
static void Test_PauseGenIncreased(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 0, 2, 100);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 0,
                3,  /* attach unchanged */
                3,  /* pause +1 */
                3,  /* restart +1 */
                0,  /* detach unchanged */
                3,  /* gen +1 */
                110 /* OID +10 */);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-003", TC_PASS, v, reason);
}

/* ── TC-IDLE-UNIT-004: FilterDetach+Attach increased → PASS ─────────────── */
static void Test_DetachAttachIncreased(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x15BB, 0, 1, 1, 1, 1, 1, 50);
    FillAdapter(&in.snap_c[0], 0x8086, 0x15BB, 0,
                2,  /* attach +1 */
                2,  /* pause +1 */
                2,  /* restart +1 */
                2,  /* detach +1 */
                2,  /* gen +1 */
                60);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-004", TC_PASS, v, reason);
}

/* ── TC-IDLE-UNIT-005: FilterPause+Restart increased (no detach) → PASS ─── */
static void Test_PauseRestartOnly(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x1533, 1, 1, 0, 0, 0, 0, 20);
    FillAdapter(&in.snap_c[0], 0x8086, 0x1533, 1,
                1,  /* attach unchanged */
                1,  /* pause +1 */
                1,  /* restart +1 */
                0,  /* detach unchanged */
                1,  /* gen +1 */
                30);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-005", TC_PASS, v, reason);
}

/* ── TC-IDLE-UNIT-006: device path lost (error 2) → FAIL ─────────────────── */
static void Test_DevicePathLost(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok           = 1;
    in.phase_c_ok           = 0;
    in.phase_c_win32_err    = 2;  /* ERROR_FILE_NOT_FOUND — #328 exact symptom */
    in.adapter_count_a      = 1;
    in.adapter_count_c      = 0;  /* device gone; no adapters accessible */

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 1, 2, 80);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-006", TC_FAIL, v, reason);
}

/* ── TC-IDLE-UNIT-007: lifecycle counter decreased → INCONCLUSIVE ─────────── */
/*
 * A counter decrease indicates context recreation or RESET_STATISTICS.
 * Cannot interpret as confirmed power-management activity; must be AMBIGUOUS
 * → INCONCLUSIVE.  Must NOT return PASS.
 */
static void Test_CounterDecreased(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 5, 5, 2, 5, 100);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 0,
                1,  /* attach DECREASED (context recreated) */
                1,  /* pause DECREASED */
                1,  /* restart DECREASED */
                0,  /* detach DECREASED */
                1,  /* gen DECREASED */
                10);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-007", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-008: adapter index changes — no match → INCONCLUSIVE ───── */
/*
 * Phase A has adapter VID=0x8086 DID=0x125B at index 0.
 * Phase C enumerates the same VID/DID but at index 1 (enumeration order changed).
 * Since matching uses (VID, DID, enum_index), no pair matches → AMBIGUOUS.
 */
static void Test_AdapterIndexChanged(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 1, 2, 80);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 1, /* index changed from 0 to 1 */
                3, 2, 2, 1, 2, 80);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-008", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-009: Phase A baseline unavailable → SKIP ──────────────── */
static void Test_PhaseA_Unavailable(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok = 0;  /* device inaccessible at baseline */

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-009", TC_SKIP, v, reason);
}

/* ── TC-IDLE-UNIT-010: Phase C stats unavailable (stats_valid==0) → INCONCLUSIVE */
/*
 * Device is accessible in Phase C (CreateFile OK, version IOCTL OK) but
 * OPEN_ADAPTER or GET_STATISTICS failed for the adapter.  Without lifecycle
 * data the verdict cannot be PASS; it must be INCONCLUSIVE.
 */
static void Test_PhaseC_StatsUnavailable(void)
{
    IdleVerdictInput in;
    char reason[256];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x1533, 0, 1, 1, 1, 0, 1, 40);

    /* Phase C adapter present but stats failed */
    memset(&in.snap_c[0], 0, sizeof(in.snap_c[0]));
    in.snap_c[0].vendor_id  = 0x8086;
    in.snap_c[0].device_id  = 0x1533;
    in.snap_c[0].enum_index = 0;
    in.snap_c[0].stats_valid = 0;  /* IOCTL failed */

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-010", TC_INCONCLUSIVE, v, reason);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("TC-LCY-IDLE-001 Verdict Unit Tests\n");
    printf("====================================\n\n");

    Test_NoTransition_Accessible();     /* 001 */
    Test_EnterWithoutTransition();      /* 002 */
    Test_PauseGenIncreased();           /* 003 */
    Test_DetachAttachIncreased();       /* 004 */
    Test_PauseRestartOnly();            /* 005 */
    Test_DevicePathLost();              /* 006 */
    Test_CounterDecreased();            /* 007 */
    Test_AdapterIndexChanged();         /* 008 */
    Test_PhaseA_Unavailable();          /* 009 */
    Test_PhaseC_StatsUnavailable();     /* 010 */

    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);

    return (g_failed == 0) ? 0 : 1;
}
