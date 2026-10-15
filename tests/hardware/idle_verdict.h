/**
 * @file idle_verdict.h
 * @brief Pure observational classifier for TC-LCY-IDLE-001 (idle/power regression).
 *
 * Shared between test_device_idle_regression.c (HIL) and
 * tests/unit/lifecycle/test_idle_verdict.c (deterministic unit tests).
 *
 * No Windows API calls. avb_u16/avb_u32/avb_u64 come from the including file's
 * avb_ioctl.h, which MUST precede this header. All functions are static.
 *
 * Implements: TC-LCY-IDLE-001 observational classification
 * Verifies:   #328 (IntelAvbFilter absent after NIC power-management transition)
 *
 * Verdict architecture
 * --------------------
 * The executable is OBSERVATIONAL ONLY and NEVER returns TC_PASS.
 *
 *   Dimension A  NDIS lifecycle (this header): FilterPause/Restart/Detach/Attach
 *                counters prove that NDIS callbacks ran on an adapter.  They do
 *                not identify the cause (cable event, admin disable, power).
 *   Dimension B  Power cause (NOT in this header): a completed D0->low-power->D0
 *                round trip for the same physical NIC inside the Phase B window.
 *                The only per-NIC source is the Analytic channel
 *                Microsoft-Windows-NDIS/Diagnostic, which cannot be read safely
 *                in-process.  It is evaluated post-run by
 *                tools/test/Evaluate-IdleTransition.ps1, which consumes the
 *                [EVIDENCE] lines this executable prints and issues the final
 *                #328 verdict WITHOUT rewriting the executable's own result.
 */

#ifndef IDLE_VERDICT_H
#define IDLE_VERDICT_H

#include <stdio.h>
#include <string.h>

/* ── Exit-code contract ──────────────────────────────────────────────────── */
/* 0 = TC_PASS          reserved; never returned by the executable            */
/* 1 = TC_FAIL          control device inaccessible after Phase B             */
/* 2 = TC_SKIP          baseline unavailable (Phase A)                        */
/* 3 = TC_INCONCLUSIVE  observational result; final verdict is post-run       */
#define TC_PASS         0
#define TC_FAIL         1
#define TC_SKIP         2
#define TC_INCONCLUSIVE 3

#define IDLE_MAX_ADAPTERS 8

/* ── Per-adapter lifecycle counter snapshot ──────────────────────────────── */
/*
 * stats_valid == 0 means OPEN_ADAPTER/GET_STATISTICS failed; entry not compared.
 * Phase A/C entries are matched by (vendor_id, device_id, enum_index).  Fail
 * closed: no match -> LIFECYCLE_AMBIGUOUS -> TC_INCONCLUSIVE.
 */
typedef struct {
    avb_u16 vendor_id;
    avb_u16 device_id;
    avb_u32 enum_index;
    int     stats_valid;
    avb_u64 FilterAttachCount;
    avb_u64 FilterPauseCount;
    avb_u64 FilterRestartCount;
    avb_u64 FilterDetachCount;
    avb_u64 PauseRestartGeneration;
    avb_u64 OidRequestCount;
} AdapterSnapshot;

typedef enum {
    LIFECYCLE_NOT_OBSERVED = 0, /* all matched counters unchanged             */
    LIFECYCLE_CONFIRMED,        /* a lifecycle counter increased (cause unknown) */
    LIFECYCLE_AMBIGUOUS         /* counter decreased, or adapter unmatched    */
} LifecycleEvidence;

typedef struct {
    int             phase_a_ok;
    int             phase_c_ok;
    avb_u32         phase_c_win32_err;
    int             adapter_count_a;
    int             adapter_count_c;
    AdapterSnapshot snap_a[IDLE_MAX_ADAPTERS];
    AdapterSnapshot snap_c[IDLE_MAX_ADAPTERS];
} IdleVerdictInput;

/*
 * ComputeLifecycleEvidence
 * A counter DECREASE means the context was recreated or RESET_STATISTICS ran;
 * it is classified AMBIGUOUS and is never treated as power-management activity.
 */
static LifecycleEvidence ComputeLifecycleEvidence(
    const IdleVerdictInput *in,
    char *reason_buf, size_t reason_len)
{
    int i, j;
    int matched = 0, confirmed = 0, ambiguous = 0;

    if (reason_len > 0) reason_buf[0] = '\0';

    if (in->adapter_count_a <= 0 || in->adapter_count_c <= 0) {
        snprintf(reason_buf, reason_len,
                 "adapter_count_a=%d adapter_count_c=%d - no adapters to compare",
                 in->adapter_count_a, in->adapter_count_c);
        return LIFECYCLE_AMBIGUOUS;
    }

    for (i = 0; i < in->adapter_count_a && i < IDLE_MAX_ADAPTERS; i++) {
        const AdapterSnapshot *a = &in->snap_a[i];
        const AdapterSnapshot *c = NULL;

        if (!a->stats_valid) continue;

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

        if (c->FilterAttachCount      < a->FilterAttachCount      ||
            c->FilterPauseCount       < a->FilterPauseCount       ||
            c->FilterRestartCount     < a->FilterRestartCount     ||
            c->FilterDetachCount      < a->FilterDetachCount      ||
            c->PauseRestartGeneration < a->PauseRestartGeneration) {
            snprintf(reason_buf, reason_len,
                     "VID=%04X DID=%04X idx=%u: lifecycle counter decreased "
                     "(Gen A=%llu C=%llu; Detach A=%llu C=%llu) - "
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

        if (c->PauseRestartGeneration > a->PauseRestartGeneration ||
            c->FilterDetachCount      > a->FilterDetachCount      ||
            c->FilterPauseCount       > a->FilterPauseCount       ||
            c->FilterRestartCount     > a->FilterRestartCount     ||
            c->FilterAttachCount      > a->FilterAttachCount) {
            snprintf(reason_buf, reason_len,
                     "VID=%04X DID=%04X idx=%u: NDIS lifecycle activity "
                     "(cause unverified) - "
                     "Gen+%llu Detach+%llu Pause+%llu Restart+%llu Attach+%llu",
                     (unsigned)a->vendor_id, (unsigned)a->device_id,
                     (unsigned)a->enum_index,
                     (unsigned long long)(c->PauseRestartGeneration - a->PauseRestartGeneration),
                     (unsigned long long)(c->FilterDetachCount      - a->FilterDetachCount),
                     (unsigned long long)(c->FilterPauseCount       - a->FilterPauseCount),
                     (unsigned long long)(c->FilterRestartCount     - a->FilterRestartCount),
                     (unsigned long long)(c->FilterAttachCount      - a->FilterAttachCount));
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
             "between Phase A and Phase C", matched);
    return LIFECYCLE_NOT_OBSERVED;
}

/*
 * ComputeIdleVerdict - observational truth table (never TC_PASS):
 *
 *  !phase_a_ok                          TC_SKIP          BASELINE_UNAVAILABLE
 *  !phase_c_ok                          TC_FAIL          DEVICE_FAILURE_OBSERVED
 *                                                        (cause UNCONFIRMED)
 *  LIFECYCLE_CONFIRMED                  TC_INCONCLUSIVE  LIFECYCLE_OBSERVED,
 *                                                        power cause pending
 *  LIFECYCLE_AMBIGUOUS                  TC_INCONCLUSIVE
 *  LIFECYCLE_NOT_OBSERVED               TC_INCONCLUSIVE  TRANSITION_NOT_OBSERVED
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
                 "DEVICE_FAILURE_OBSERVED: device inaccessible after Phase B "
                 "(Win32 error %lu); power-management cause UNCONFIRMED",
                 (unsigned long)in->phase_c_win32_err);
        return TC_FAIL;
    }

    memset(lce_reason, 0, sizeof(lce_reason));
    lce = ComputeLifecycleEvidence(in, lce_reason, sizeof(lce_reason));

    switch (lce) {
    case LIFECYCLE_CONFIRMED:
        snprintf(reason_buf, reason_len,
                 "LIFECYCLE_OBSERVED: %s. Power-management cause is NOT evaluated "
                 "by this executable; run tools/test/Evaluate-IdleTransition.ps1 "
                 "for the final verdict.",
                 lce_reason);
        return TC_INCONCLUSIVE;

    case LIFECYCLE_AMBIGUOUS:
        snprintf(reason_buf, reason_len, "AMBIGUOUS: %s", lce_reason);
        return TC_INCONCLUSIVE;

    default:
        snprintf(reason_buf, reason_len,
                 "TRANSITION_NOT_OBSERVED: %s", lce_reason);
        return TC_INCONCLUSIVE;
    }
}

#endif /* IDLE_VERDICT_H */
