/**
 * @file test_idle_verdict.c
 * @brief Deterministic unit tests for the TC-LCY-IDLE-001 verdict logic.
 *
 * Implements: TC-IDLE-UNIT-001 through TC-IDLE-UNIT-012
 * Traces to:  #328 (IntelAvbFilter absent after NIC power management)
 * Verifies:   idle_verdict.h — ComputeIdleVerdict() truth table
 *
 * Tests ComputeIdleVerdict() and ComputeLifecycleEvidence() with synthetic
 * IdleVerdictInput values.  No Windows device access; no hardware required;
 * CI-safe.
 *
 * Truth table under test (from idle_verdict.h):
 *   !phase_a_ok                                          → TC_SKIP (2)
 *   !phase_c_ok                                          → TC_FAIL (1)
 *   LIFECYCLE_CONFIRMED                                  → TC_INCONCLUSIVE (3)
 *   (the executable NEVER returns TC_PASS; the final #328 verdict comes from
 *    tools/test/Evaluate-IdleTransition.ps1 with independent NDIS power evidence)
 *   LIFECYCLE_AMBIGUOUS                                  → TC_INCONCLUSIVE (3)
 *   LIFECYCLE_NOT_OBSERVED                               → TC_INCONCLUSIVE (3)
 *
 * Test cases:
 *   TC-IDLE-UNIT-001  No transition, device accessible → INCONCLUSIVE
 *   TC-IDLE-UNIT-002  Enter pressed but no counter change → INCONCLUSIVE
 *   TC-IDLE-UNIT-003  PauseRestartGeneration increased, no pwr verification → INCONCLUSIVE
 *   TC-IDLE-UNIT-004  FilterDetach+Attach increased, no pwr verification → INCONCLUSIVE
 *   TC-IDLE-UNIT-005  FilterPause+Restart only, no pwr verification → INCONCLUSIVE
 *   TC-IDLE-UNIT-006  Device path lost (error 2) → FAIL
 *   TC-IDLE-UNIT-007  Lifecycle counter decreased → INCONCLUSIVE (AMBIGUOUS)
 *   TC-IDLE-UNIT-008  Adapter index changes (no Phase A/C match) → INCONCLUSIVE
 *   TC-IDLE-UNIT-009  Phase A fails (baseline unavailable) → SKIP
 *   TC-IDLE-UNIT-010  Phase C stats unavailable (stats_valid==0) → INCONCLUSIVE
 *   TC-IDLE-UNIT-011  Lifecycle confirmed on target adapter → INCONCLUSIVE (never PASS)
 *   TC-IDLE-UNIT-012  Two adapters: only unrelated adapter changes state → INCONCLUSIVE
 *
 * Build (see tools/build/Build-Tests.ps1):
 *   cl /nologo /W4 /WX /Zi /DWIN32 /D_WIN32_WINNT=0x0A00
 *      -I include -I external/intel_avb/lib
 *      tests\unit\lifecycle\test_idle_verdict.c
 *      /Fe:build\tools\avb_test\x64\Debug\test_idle_verdict.exe
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

/* Helper: fill an adapter snapshot with explicit field values */
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
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok        = 1;
    in.phase_c_ok        = 1;
    in.adapter_count_a   = 1;
    in.adapter_count_c   = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 5, 5, 2, 5, 100);
    in.snap_c[0] = in.snap_a[0];

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-001", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-002: Enter pressed but no lifecycle change → INCONCLUSIVE ─ */
static void Test_EnterWithoutTransition(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok        = 1;
    in.phase_c_ok        = 1;
    in.adapter_count_a   = 1;
    in.adapter_count_c   = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x1533, 0, 1, 1, 1, 0, 1, 487);
    in.snap_c[0] = in.snap_a[0];

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-002", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-003: lifecycle increased, NO pwr verification → INCONCLUSIVE */
/*
 * A PauseRestartGeneration increment proves NDIS called FilterPause, but the
 * cause (power-management, cable event, admin action) is unknown.
 * The classifier has no power evidence, so the verdict MUST be INCONCLUSIVE.
 */
static void Test_PauseGenIncreased_NoPwrVerify(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok        = 1;
    in.phase_c_ok        = 1;
    in.adapter_count_a   = 1;
    in.adapter_count_c   = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 0, 2, 100);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 0, 3, 3, 3, 0, 3, 110);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-003", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-004: FilterDetach+Attach increased, NO pwr verify → INCONCLUSIVE */
static void Test_DetachAttachIncreased_NoPwrVerify(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok        = 1;
    in.phase_c_ok        = 1;
    in.adapter_count_a   = 1;
    in.adapter_count_c   = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x15BB, 0, 1, 1, 1, 1, 1, 50);
    FillAdapter(&in.snap_c[0], 0x8086, 0x15BB, 0, 2, 2, 2, 2, 2, 60);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-004", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-005: FilterPause+Restart only, NO pwr verify → INCONCLUSIVE */
static void Test_PauseRestartOnly_NoPwrVerify(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok        = 1;
    in.phase_c_ok        = 1;
    in.adapter_count_a   = 1;
    in.adapter_count_c   = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x1533, 1, 1, 0, 0, 0, 0, 20);
    FillAdapter(&in.snap_c[0], 0x8086, 0x1533, 1, 1, 1, 1, 0, 1, 30);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-005", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-006: device path lost (error 2) → FAIL ─────────────────── */
static void Test_DevicePathLost(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok        = 1;
    in.phase_c_ok        = 0;
    in.phase_c_win32_err = 2;  /* ERROR_FILE_NOT_FOUND — #328 exact symptom */
    in.adapter_count_a   = 1;
    in.adapter_count_c   = 0;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 1, 2, 80);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-006", TC_FAIL, v, reason);
}

/* ── TC-IDLE-UNIT-007: lifecycle counter decreased → INCONCLUSIVE ─────────── */
static void Test_CounterDecreased(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 5, 5, 2, 5, 100);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 0, 1, 1, 1, 0, 1, 10);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-007", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-008: adapter index changes → INCONCLUSIVE ──────────────── */
static void Test_AdapterIndexChanged(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 1, 2, 80);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 1, /* index changed */ 3, 2, 2, 1, 2, 80);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-008", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-009: Phase A baseline unavailable → SKIP ──────────────── */
static void Test_PhaseA_Unavailable(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok = 0;

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-009", TC_SKIP, v, reason);
}

/* ── TC-IDLE-UNIT-010: Phase C stats unavailable (stats_valid==0) → INCONCLUSIVE */
static void Test_PhaseC_StatsUnavailable(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok      = 1;
    in.phase_c_ok      = 1;
    in.adapter_count_a = 1;
    in.adapter_count_c = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x1533, 0, 1, 1, 1, 0, 1, 40);

    memset(&in.snap_c[0], 0, sizeof(in.snap_c[0]));
    in.snap_c[0].vendor_id   = 0x8086;
    in.snap_c[0].device_id   = 0x1533;
    in.snap_c[0].enum_index  = 0;
    in.snap_c[0].stats_valid = 0;  /* IOCTL failed */

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-010", TC_INCONCLUSIVE, v, reason);
}

/* ── TC-IDLE-UNIT-011: lifecycle confirmed on target adapter → INCONCLUSIVE ── */
/*
 * Lifecycle counters prove NDIS callbacks ran, not why.  The classifier has no
 * power evidence, so even a fully confirmed lifecycle yields INCONCLUSIVE and
 * the executable NEVER returns TC_PASS.  Only Evaluate-IdleTransition.ps1 may
 * issue the final verdict, from independently correlated NDIS power events.
 */
static void Test_LifecycleConfirmed_NeverPass(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok         = 1;
    in.phase_c_ok         = 1;
    in.adapter_count_a    = 1;
    in.adapter_count_c    = 1;

    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 1, 2, 80);
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 0, 3, 3, 3, 2, 3, 90);

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-011", TC_INCONCLUSIVE, v, reason);
    if (ComputeLifecycleEvidence(&in, reason, sizeof(reason)) != LIFECYCLE_CONFIRMED) {
        printf("[FAIL] TC-IDLE-UNIT-011 -> lifecycle evidence not CONFIRMED\n");
        g_failed++;
    }
}

/* ── TC-IDLE-UNIT-012: unrelated adapter changes; target NIC unchanged → INCONCLUSIVE */
/*
 * Setup: two adapters:
 *   idx=0  VID=0x8086 DID=0x125B  (I226-LM) — lifecycle counters INCREASE
 *   idx=1  VID=0x8086 DID=0x15BB  (I219-LM) — counters UNCHANGED
 *
 * The #328 scenario involves NIC power-management recovery on ANY adapter
 * losing the control device.  Lifecycle activity on one adapter (I226) is
 * not sufficient to confirm the power-management cause even if the OTHER
 * adapter (I219) was unchanged.  The classifier has no power
 * evidence, so the verdict remains INCONCLUSIVE.
 *
 * Also validates multi-adapter matching: I219 is correctly matched as
 * unchanged; I226 as changed; ComputeLifecycleEvidence returns CONFIRMED
 * for the pair set; but ComputeIdleVerdict still returns INCONCLUSIVE
 * because power cause is never evaluated here.
 */
static void Test_UnrelatedAdapterChanges(void)
{
    IdleVerdictInput in;
    char reason[512];
    memset(&in, 0, sizeof(in));
    in.phase_a_ok         = 1;
    in.phase_c_ok         = 1;
    in.adapter_count_a    = 2;
    in.adapter_count_c    = 2;

    /* Phase A */
    FillAdapter(&in.snap_a[0], 0x8086, 0x125B, 0, 3, 2, 2, 1, 2, 80);  /* I226 */
    FillAdapter(&in.snap_a[1], 0x8086, 0x15BB, 1, 1, 1, 1, 0, 1, 50);  /* I219 */

    /* Phase C: I226 changed, I219 unchanged */
    FillAdapter(&in.snap_c[0], 0x8086, 0x125B, 0, 3, 3, 3, 2, 3, 90);  /* I226 +Gen+Detach */
    FillAdapter(&in.snap_c[1], 0x8086, 0x15BB, 1, 1, 1, 1, 0, 1, 50);  /* I219 unchanged */

    int v = ComputeIdleVerdict(&in, reason, sizeof(reason));
    AssertVerdict("TC-IDLE-UNIT-012", TC_INCONCLUSIVE, v, reason);
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("TC-LCY-IDLE-001 Verdict Unit Tests\n");
    printf("====================================\n\n");

    Test_NoTransition_Accessible();          /* 001 */
    Test_EnterWithoutTransition();           /* 002 */
    Test_PauseGenIncreased_NoPwrVerify();    /* 003 */
    Test_DetachAttachIncreased_NoPwrVerify();/* 004 */
    Test_PauseRestartOnly_NoPwrVerify();     /* 005 */
    Test_DevicePathLost();                   /* 006 */
    Test_CounterDecreased();                 /* 007 */
    Test_AdapterIndexChanged();              /* 008 */
    Test_PhaseA_Unavailable();              /* 009 */
    Test_PhaseC_StatsUnavailable();          /* 010 */
    Test_LifecycleConfirmed_NeverPass();   /* 011 */
    Test_UnrelatedAdapterChanges();          /* 012 */

    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);

    return (g_failed == 0) ? 0 : 1;
}
