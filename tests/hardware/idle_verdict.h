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
 */

#ifndef IDLE_VERDICT_H
#define IDLE_VERDICT_H

#include <stdio.h>
#include <string.h>

/* ── Exit-code contract ──────────────────────────────────────────────────── */
/* 0 = PASS          NDIS lifecycle confirmed, device accessible             */
/* 1 = FAIL          Device inaccessible after transition (#328 reproduced)  */
/* 2 = SKIP          Baseline unavailable; precondition not met              */
/* 3 = INCONCLUSIVE  Transition not observed, adapter ambiguity, or timeout  */
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
    int             phase_a_ok;         /* 1 = device accessible in Phase A   */
    int             phase_c_ok;         /* 1 = device accessible in Phase C   */
    avb_u32         phase_c_win32_err;  /* Win32 error code if !phase_c_ok    */
    int             adapter_count_a;    /* 0..IDLE_MAX_ADAPTERS               */
    int             adapter_count_c;
    AdapterSnapshot snap_a[IDLE_MAX_ADAPTERS];
    AdapterSnapshot snap_c[IDLE_MAX_ADAPTERS];
} IdleVerdictInput;

/* ── ComputeLifecycleEvidence ────────────────────────────────────────────── */
/*
 * Matches Phase A and Phase C snapshots by (vendor_id, device_id, enum_index).
 *
 *   LIFECYCLE_CONFIRMED   — at least one lifecycle counter increased on any
 *                           matched adapter pair (PauseRestartGeneration,
 *                           FilterDetach, FilterPause, FilterRestart, or
 *                           FilterAttach)
 *   LIFECYCLE_AMBIGUOUS   — any lifecycle counter DECREASED (context recreated
 *                           or RESET_STATISTICS called), or no adapter matched
 *   LIFECYCLE_NOT_OBSERVED— all matched pairs have identical lifecycle counters
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

        /* Match by stable hardware identity (VID, DID, enumeration index) */
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
         * Counter DECREASE means the adapter context was recreated (FilterDetach
         * + FilterAttach with a new context) or RESET_STATISTICS was called.
         * We cannot reliably distinguish these, so classify as AMBIGUOUS.
         * Do NOT interpret a decrease as proof of power-management activity.
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

        /* Positive lifecycle evidence: any lifecycle counter increased */
        if (c->PauseRestartGeneration > a->PauseRestartGeneration ||
            c->FilterDetachCount      > a->FilterDetachCount      ||
            c->FilterPauseCount       > a->FilterPauseCount       ||
            c->FilterRestartCount     > a->FilterRestartCount     ||
            c->FilterAttachCount      > a->FilterAttachCount) {
            snprintf(reason_buf, reason_len,
                     "VID=%04X DID=%04X idx=%u: NDIS lifecycle activity — "
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
 * Truth table:
 *
 *   !phase_a_ok                        → TC_SKIP  (2) BASELINE_UNAVAILABLE
 *   !phase_c_ok                        → TC_FAIL  (1) DEVICE_FAILURE_OBSERVED
 *   LIFECYCLE_CONFIRMED                → TC_PASS  (0) VERIFIED_TRANSITION
 *   LIFECYCLE_AMBIGUOUS                → TC_INCONCLUSIVE (3)
 *   LIFECYCLE_NOT_OBSERVED             → TC_INCONCLUSIVE (3) TRANSITION_NOT_OBSERVED
 *
 * A timeout WITHOUT transition evidence maps to LIFECYCLE_NOT_OBSERVED → TC_INCONCLUSIVE.
 * A timeout that happens to coincide with lifecycle activity maps to
 * LIFECYCLE_CONFIRMED → TC_PASS.
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
        snprintf(reason_buf, reason_len,
                 "VERIFIED_TRANSITION: NDIS lifecycle event confirmed; "
                 "device accessible before and after — #328 NOT REPRODUCED. %s",
                 lce_reason);
        return TC_PASS;

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
