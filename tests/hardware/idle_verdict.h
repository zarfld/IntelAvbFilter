/**
 * @file idle_verdict.h
 * @brief Pure verdict logic for TC-LCY-IDLE-001 idle/power-transition regression.
 *
 * Shared between test_device_idle_regression.c (HIL) and
 * tests/unit/lifecycle/test_idle_verdict.c (deterministic unit tests).
 *
 * No Windows API calls. All integer types (avb_u16, avb_u32, avb_u64) come
 * from the including file's avb_ioctl.h inclusion, which MUST precede this
 * header in every translation unit.
 *
 * All functions are static; there is no compilation unit.
 *
 * Implements: TC-LCY-IDLE-001 verdict logic
 * Verifies:   #328 (IntelAvbFilter absent after NIC power-management transition)
 *
 * IMPORTANT — Two distinct evidence dimensions
 * ─────────────────────────────────────────────
 * Dimension A  NDIS lifecycle (ComputeLifecycleEvidence):
 *   FilterPause, FilterRestart, FilterDetach, FilterAttach, and
 *   PauseRestartGeneration counters change when NDIS calls the corresponding
 *   callbacks.  These callbacks run for MANY reasons: a cable disconnect, a
 *   manual NIC disable/enable, an admin NDIS operation, or an actual
 *   power-management transition.  A counter increment PROVES the callback ran
 *   but does NOT identify the cause.
 *
 * Dimension B  Power-management cause (pwr_event_verified in IdleVerdictInput):
 *   Independently set by the caller when it has corroborated evidence that the
 *   NDIS activity was caused by a NIC power-management event (e.g., a
 *   qualifying Windows event in the Kernel-Power or NDIS channel within the
 *   Phase B window, or a confirmed S0ix/connected-standby transition).
 *   Cannot be derived from NDIS counters alone.
 *   MUST NOT be set from elapsed time, a user pressing Enter, or a generic
 *   Kernel-Power heartbeat.
 *
 * If independent power-cause verification cannot be implemented without
 * substantial new infrastructure, leave pwr_event_verified = 0 and record
 * the lifecycle activity as diagnostic evidence.  Do not manufacture a PASS.
 */

#ifndef IDLE_VERDICT_H
#define IDLE_VERDICT_H

#include <stdio.h>
#include <string.h>

/* ── Exit-code contract ──────────────────────────────────────────────────── */
/* 0 = PASS          NDIS lifecycle confirmed + power-mgmt cause verified     */
/* 1 = FAIL          Device inaccessible after transition (#328 reproduced)   */
/* 2 = SKIP          Baseline unavailable; precondition not met               */
/* 3 = INCONCLUSIVE  No qualifying transition evidence, or ambiguous identity  */
#define TC_PASS         0
#define TC_FAIL         1
#define TC_SKIP         2
#define TC_INCONCLUSIVE 3

#define IDLE_MAX_ADAPTERS 8

/* ── Per-adapter lifecycle counter snapshot ──────────────────────────────── */
/*
 * Filled by SnapshotAdapters() in the HIL test; constructed directly in
 * unit tests.  stats_valid == 0 means the IOCTL failed and this entry
 * should not be compared.
 *
 * Adapter identity is matched by the triple (vendor_id, device_id, enum_index).
 * This is fail-closed: if two adapters share the same VID/DID (e.g., two
 * I226-LM cards) and enum_index changes between Phase A and Phase C, no match
 * is found → LIFECYCLE_AMBIGUOUS → TC_INCONCLUSIVE.  That is the correct safe
 * behaviour; the test must not guess which adapter is which.
 */
typedef struct {
    avb_u16 vendor_id;
    avb_u16 device_id;
    avb_u32 enum_index;
    int     stats_valid;            /* 1 = successfully retrieved */
    avb_u64 FilterAttachCount;
    avb_u64 FilterPauseCount;
    avb_u64 FilterRestartCount;
    avb_u64 FilterDetachCount;
    avb_u64 PauseRestartGeneration;
    avb_u64 OidRequestCount;        /* secondary sanity field */
} AdapterSnapshot;

/* ── Evidence quality ────────────────────────────────────────────────────── */
typedef enum {
    LIFECYCLE_NOT_OBSERVED = 0, /* All counters unchanged across all adapters */
    LIFECYCLE_CONFIRMED,        /* At least one lifecycle counter increased    */
    LIFECYCLE_AMBIGUOUS,        /* Counter decreased, or no adapter matched    */
} LifecycleEvidence;

/* ── Verdict input ───────────────────────────────────────────────────────── */
typedef struct {
    int             phase_a_ok;         /* 1 = device accessible in Phase A          */
    int             phase_c_ok;         /* 1 = device accessible in Phase C          */
    avb_u32         phase_c_win32_err;  /* Win32 error code if !phase_c_ok           */
    int             adapter_count_a;    /* 0..IDLE_MAX_ADAPTERS                      */
    int             adapter_count_c;
    AdapterSnapshot snap_a[IDLE_MAX_ADAPTERS];
    AdapterSnapshot snap_c[IDLE_MAX_ADAPTERS];
    /*
     * pwr_event_verified — set to 1 by the caller ONLY when independent
     * evidence confirms the observed NDIS lifecycle activity was caused by a
     * qualifying NIC power-management event within the Phase B window.
     *
     * Acceptable evidence (set by caller after verifying):
     *   - Intel miniport link-down/up events (e1dexpress/e2fnexpress IDs 27/32)
     *     CORRELATED with a Kernel-Power/PnP power-state event in the same window
     *   - Kernel-Power EventID 42 (system sleep entering S3/S4) confirmed
     *   - NDIS PnP / Kernel-PnP device events for the specific NIC instance
     *
     * MUST NOT be set from:
     *   - Elapsed time alone
     *   - User pressing Enter
     *   - Generic Kernel-Power heartbeat (EventID 41)
     *   - NDIS counter changes alone (that is Dimension A, not B)
     *
     * Default: 0.  Most automated HIL runs leave this 0 because reliable
     * power-cause verification without substantial new infrastructure is
     * not available in this user-mode test.
     */
    int             pwr_event_verified;
} IdleVerdictInput;

/* ── ComputeLifecycleEvidence ────────────────────────────────────────────── */
/*
 * Matches Phase A and Phase C snapshots by (vendor_id, device_id, enum_index).
 *
 *   LIFECYCLE_CONFIRMED   — at least one lifecycle counter increased on any
 *                           matched adapter pair (PauseRestartGeneration,
 *                           FilterDetach, FilterPause, FilterRestart, or
 *                           FilterAttach).  Does NOT indicate the cause.
 *
 *   LIFECYCLE_AMBIGUOUS   — any lifecycle counter DECREASED (context recreated
 *                           or RESET_STATISTICS called), or no adapter matched.
 *
 *   LIFECYCLE_NOT_OBSERVED— all matched pairs have identical lifecycle counters.
 *
 * A counter decrease is classified as AMBIGUOUS rather than confirmed activity
 * because the driver may have been unloaded/reloaded, resetting counters, and
 * the delta direction cannot distinguish a genuine power-induced detach from a
 * RESET_STATISTICS call or an adapter replace.
 *
 * Writes a null-terminated diagnostic message to reason_buf.
 */
static LifecycleEvidence ComputeLifecycleEvidence(
    const IdleVerdictInput *in,
    char *reason_buf, size_t reason_len)
{
    int i, j;
    int matched   = 0;
    int confirmed = 0;
    int ambiguous = 0;

    if (reason_len > 0) reason_buf[0] = '\0';

    if (in->adapter_count_a <= 0 || in->adapter_count_c <= 0) {
        snprintf(reason_buf, reason_len,
                 "adapter_count_a=%d adapter_count_c=%d — no adapters to compare",
                 in->adapter_count_a, in->adapter_count_c);
        return LIFECYCLE_AMBIGUOUS;
    }

    for (i = 0; i < in->adapter_count_a && i < IDLE_MAX_ADAPTERS; i++) {
        const AdapterSnapshot *a = &in->snap_a[i];
        const AdapterSnapshot *c = NULL;

        if (!a->stats_valid) continue;

        /* Match by (VID, DID, enum_index).  Fail-closed: if no match is found
         * the adapter is classified as missing in Phase C → AMBIGUOUS. */
        for (j = 0; j < in->adapter_count_c && j < IDLE_MAX_ADAPTERS; j++) {
            if (in->snap_c[j].vendor_id  == a->vendor_id  &&
                in->snap_c[j].device_id  == a->device_id  &&
                in->snap_c[j].enum_index == a->enum_index) {
                c = &in->snap_c[j];
                break;
            }
        }

        if (!c || !c->stats_valid) {
            snprintf(reason_buf, reason_len,
                     "VID=%04X DID=%04X idx=%u: present in Phase A; "
                     "missing or invalid in Phase C",
                     (unsigned)a->vendor_id, (unsigned)a->device_id,
                     (unsigned)a->enum_index);
            ambiguous++;
            continue;
        }

        matched++;

        /*
         * Counter DECREASE: the adapter context was recreated (FilterDetach +
         * FilterAttach with a new context) or RESET_STATISTICS was called.
         * Cannot reliably distinguish these → AMBIGUOUS.
         * Must NOT be interpreted as confirmed power-management activity.
         */
        if (c->FilterAttachCount      < a->FilterAttachCount      ||
            c->FilterPauseCount       < a->FilterPauseCount       ||
            c->FilterRestartCount     < a->FilterRestartCount     ||
            c->FilterDetachCount      < a->FilterDetachCount      ||
            c->PauseRestartGeneration < a->PauseRestartGeneration) {
            snprintf(reason_buf, reason_len,
                     "VID=%04X DID=%04X idx=%u: lifecycle counter decreased "
                     "(Gen A=%llu C=%llu; Detach A=%llu C=%llu) — "
                     "context recreated or RESET_STATISTICS called",
                     (unsigned)a->vendor_id, (unsigned)a->device_id,
                     (unsigned)a->enum_index,
                     (unsigned long long)a->PauseRestartGeneration,
                     (unsigned long long)c->PauseRestartGeneration,
                     (unsigned long long)a->FilterDetachCount,
                     (unsigned long long)c->FilterDetachCount);
            ambiguous++;
            continue;
        }

        /* Positive lifecycle evidence: any lifecycle counter increased.
         * Records the delta for diagnostics; does NOT determine cause. */
        if (c->PauseRestartGeneration > a->PauseRestartGeneration ||
            c->FilterDetachCount      > a->FilterDetachCount      ||
            c->FilterPauseCount       > a->FilterPauseCount       ||
            c->FilterRestartCount     > a->FilterRestartCount     ||
            c->FilterAttachCount      > a->FilterAttachCount) {
            snprintf(reason_buf, reason_len,
                     "VID=%04X DID=%04X idx=%u: NDIS lifecycle activity "
                     "(cause unverified) — "
                     "Gen+%llu Detach+%llu Pause+%llu Restart+%llu Attach+%llu",
                     (unsigned)a->vendor_id, (unsigned)a->device_id,
                     (unsigned)a->enum_index,
                     (unsigned long long)(c->PauseRestartGeneration
                                         - a->PauseRestartGeneration),
                     (unsigned long long)(c->FilterDetachCount
                                         - a->FilterDetachCount),
                     (unsigned long long)(c->FilterPauseCount
                                         - a->FilterPauseCount),
                     (unsigned long long)(c->FilterRestartCount
                                         - a->FilterRestartCount),
                     (unsigned long long)(c->FilterAttachCount
                                         - a->FilterAttachCount));
            confirmed++;
        }
    }

    if (ambiguous > 0) return LIFECYCLE_AMBIGUOUS;

    if (matched == 0) {
        snprintf(reason_buf, reason_len,
                 "no Phase A/C adapter pairs matched by VID/DID/index");
        return LIFECYCLE_AMBIGUOUS;
    }

    if (confirmed > 0) return LIFECYCLE_CONFIRMED;

    snprintf(reason_buf, reason_len,
             "%d matched adapter(s): all lifecycle counters unchanged "
             "between Phase A and Phase C",
             matched);
    return LIFECYCLE_NOT_OBSERVED;
}

/* ── ComputeIdleVerdict ──────────────────────────────────────────────────── */
/*
 * Truth table for #328 acceptance:
 *
 *  Condition                                          Exit  Label
 *  ─────────────────────────────────────────────────  ────  ─────────────────────────────────
 *  !phase_a_ok                                          2   BASELINE_UNAVAILABLE
 *  !phase_c_ok                                          1   DEVICE_FAILURE_OBSERVED (#328)
 *  LIFECYCLE_CONFIRMED  +  pwr_event_verified           0   PASS (both dimensions confirmed)
 *  LIFECYCLE_CONFIRMED  + !pwr_event_verified           3   INCONCLUSIVE/LIFECYCLE_OBSERVED
 *  LIFECYCLE_AMBIGUOUS                                  3   INCONCLUSIVE/AMBIGUOUS
 *  LIFECYCLE_NOT_OBSERVED                               3   INCONCLUSIVE/NO_TRANSITION
 *
 * Rationale for the LIFECYCLE_CONFIRMED/!pwr_event_verified → INCONCLUSIVE path:
 *   NDIS callbacks fire for many reasons beyond power management (cable events,
 *   admin disable/enable, AvbFilter re-registration, etc.).  Reporting PASS
 *   when only lifecycle counters changed would accept any of those triggers as
 *   proof of the #328 power-management scenario.  The test must NOT do that.
 *
 * A timeout WITHOUT any qualifying evidence maps to LIFECYCLE_NOT_OBSERVED →
 * TC_INCONCLUSIVE.  A timeout that coincidentally accompanies a verified power
 * event and lifecycle activity can still reach TC_PASS (caller sets
 * pwr_event_verified after consulting Windows Event Log post-run).
 */
static int ComputeIdleVerdict(
    const IdleVerdictInput *in,
    char *reason_buf, size_t reason_len)
{
    char lce_reason[512];
    LifecycleEvidence lce;

    if (!in->phase_a_ok) {
        snprintf(reason_buf, reason_len,
                 "BASELINE_UNAVAILABLE: device inaccessible in Phase A");
        return TC_SKIP;
    }

    if (!in->phase_c_ok) {
        snprintf(reason_buf, reason_len,
                 "DEVICE_FAILURE_OBSERVED: device inaccessible after transition "
                 "(Win32 error %lu) — #328 REPRODUCED",
                 (unsigned long)in->phase_c_win32_err);
        return TC_FAIL;
    }

    memset(lce_reason, 0, sizeof(lce_reason));
    lce = ComputeLifecycleEvidence(in, lce_reason, sizeof(lce_reason));

    switch (lce) {
    case LIFECYCLE_CONFIRMED:
        if (in->pwr_event_verified) {
            snprintf(reason_buf, reason_len,
                     "VERIFIED: NDIS lifecycle activity confirmed AND power-management "
                     "cause independently verified; device accessible before and after "
                     "— #328 NOT REPRODUCED. %s",
                     lce_reason);
            return TC_PASS;
        }
        snprintf(reason_buf, reason_len,
                 "LIFECYCLE_OBSERVED_UNVERIFIED: NDIS callbacks fired but "
                 "power-management cause not independently confirmed. "
                 "Device accessible. %s. "
                 "To obtain PASS: correlate these counter changes with a qualifying "
                 "power event in Windows Event Log (Kernel-Power, Kernel-PnP, "
                 "or Intel miniport channels) for the Phase B window.",
                 lce_reason);
        return TC_INCONCLUSIVE;

    case LIFECYCLE_AMBIGUOUS:
        snprintf(reason_buf, reason_len,
                 "INCONCLUSIVE/AMBIGUOUS: %s", lce_reason);
        return TC_INCONCLUSIVE;

    default: /* LIFECYCLE_NOT_OBSERVED */
        snprintf(reason_buf, reason_len,
                 "TRANSITION_NOT_OBSERVED: %s", lce_reason);
        return TC_INCONCLUSIVE;
    }
}

#endif /* IDLE_VERDICT_H */
