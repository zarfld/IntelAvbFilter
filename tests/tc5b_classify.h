/**
 * @file tc5b_classify.h
 * @brief Pure TC-5b PHC/QPC coherence classification logic — shared by production
 *        and deterministic unit tests.
 *
 * Both ptp_clock_control_test.c (HIL) and test_ioctl_xstamp_v2.c (unit tests)
 * include this header.  All functions are static; there is no compilation unit.
 *
 * Design rules:
 *   - No hardware calls, no DeviceIoControl, no Sleep.
 *   - All inputs are pre-computed by the caller from V2 sample structs.
 *   - TC5b_Classify3() returns TC5B_TAKE_W3 when the caller must take a
 *     fourth sample (frozen-clock branch); TC5b_ClassifyW3() finishes that case.
 *   - Every code path returns a verdict; no silent fallthroughs.
 *   - PASS requires all three initial samples REGISTER_DERIVED (all_hw).
 *   - INCONCLUSIVE is a mandatory failure in the CI gate.
 *
 * Verifies: #340 (I219 PHC false-green eliminated)
 */

#ifndef TC5B_CLASSIFY_H
#define TC5B_CLASSIFY_H

#include <stdio.h>   /* snprintf */

/* avb_u32 and AVB_PHC_SOURCE_REGISTER_DERIVED come from the including file's
 * avb_ioctl.h inclusion, which precedes this header in every translation unit. */

/* ─── verdict ────────────────────────────────────────────────────────────── */
typedef enum {
    TC5B_PASS         = 0,  /* hardware coherence verified within [0.95,1.10] */
    TC5B_FAIL         = 1,  /* demonstrated hardware quality violation         */
    TC5B_INCONCLUSIVE = 2,  /* missing / insufficient hardware provenance      */
    TC5B_TAKE_W3      = 3   /* internal: caller should take a window-3 sample  */
} TC5b_V;

/* ─── helpers ────────────────────────────────────────────────────────────── */
static int TC5b_IsHW(avb_u32 src)
{
    return src == AVB_PHC_SOURCE_REGISTER_DERIVED;
}

static double TC5b_Rate(
    avb_u64 phc_a, avb_u64 phc_b,
    avb_u64 qpc_a, avb_u64 qpc_b,
    avb_u64 freq)
{
    avb_u64 qd_raw = (qpc_b > qpc_a) ? (qpc_b - qpc_a) : 0;
    if (qd_raw == 0 || freq == 0) return 0.0;
    /* Scale to nanoseconds without losing precision for typical QPC ranges */
    double qd_ns = (double)qd_raw * 1.0e9 / (double)freq;
    /* PHC delta: cast to signed to handle (unlikely) backward jumps */
    double phc_d = (double)(avb_i64)(phc_b - phc_a);
    return phc_d / qd_ns;
}

static int TC5b_RateOK(double r)
{
    return r >= 0.95 && r <= 1.10;
}

/* ─── TC5b_Classify3 ────────────────────────────────────────────────────── */
/**
 * Classify TC-5b using three V2 samples (r1/r2/r3).
 *
 * @param hw1..hw3  avb_phc_source == REGISTER_DERIVED for each sample
 * @param mono_ok   PHC monotone AND QPC monotone across r1->r2->r3
 * @param rate_12   PHC/QPC rate for window-1 (r1->r2)
 * @param rate_23   PHC/QPC rate for window-2 (r2->r3)
 * @param msg       caller-supplied buffer for human-readable explanation
 * @param msz       size of msg buffer
 * @return TC5B_PASS, TC5B_FAIL, TC5B_INCONCLUSIVE, or TC5B_TAKE_W3
 *
 * TC5B_TAKE_W3 means: caller should call IssueV2XTimestamp() to obtain r4,
 * then call TC5b_ClassifyW3() to reach a final verdict.
 */
static TC5b_V TC5b_Classify3(
    int hw1, int hw2, int hw3,
    int mono_ok,
    double rate_12, double rate_23,
    char *msg, int msz)
{
    int all_hw = hw1 && hw2 && hw3;
    int w1_hw  = hw1 && hw2;
    int w2_hw  = hw2 && hw3;
    int r12_ok = TC5b_RateOK(rate_12);
    int r23_ok = TC5b_RateOK(rate_23);

    /* Monotonicity is classified regardless of provenance — a clock that goes
     * backward is broken whether or not its source is hardware-derived. */
    if (!mono_ok) {
        snprintf(msg, (size_t)msz, "PHC not monotonically increasing");
        return TC5B_FAIL;
    }

    /* No hardware-derived window at all: rate measures wall-clock, not PHC. */
    if (!w1_hw && !w2_hw) {
        snprintf(msg, (size_t)msz,
                 "all samples SW_WALLCLOCK/UNAVAILABLE — hardware PHC rate unverifiable");
        return TC5B_INCONCLUSIVE;
    }

    /* Frozen-clock branch: window-1 is HW-derived but near-zero rate.
     * Try a third 200 ms window to check for delayed TIMINCA re-init. */
    if (!r12_ok && !r23_ok && w1_hw && rate_12 < 0.10) {
        snprintf(msg, (size_t)msz,
                 "frozen-clock (w1=%.6f w2=%.6f) — measuring window-3",
                 rate_12, rate_23);
        return TC5B_TAKE_W3;
    }

    /* Both windows bad and not near-frozen. */
    if (!r12_ok && !r23_ok) {
        if (w1_hw || w2_hw)
            snprintf(msg, (size_t)msz,
                     "both windows bad (w1=%.6f w2=%.6f) — hardware clock failure",
                     rate_12, rate_23);
        else
            snprintf(msg, (size_t)msz,
                     "both windows bad (w1=%.6f w2=%.6f), mixed provenance",
                     rate_12, rate_23);
        return TC5B_FAIL;
    }

    /* Window-1 rate anomalous. */
    if (!r12_ok) {
        if (!w1_hw) {
            /* r1 or r2 not REGISTER_DERIVED — provenance gap in window-1. */
            snprintf(msg, (size_t)msz,
                     "window-1 has non-HW endpoint — provenance gap;"
                     " hardware rate unverifiable");
            return TC5B_INCONCLUSIVE;
        }
        /* Window-1 endpoints both HW, rate anomalous — likely TIMINCA transient.
         * Fall back to window-2 only if both its endpoints are also HW-derived. */
        if (w2_hw && r23_ok) {
            snprintf(msg, (size_t)msz,
                     "PHC monotone, window-2 rate %.6f in [0.95,1.10]"
                     " (window-1 transient)", rate_23);
            return TC5B_PASS;
        }
        snprintf(msg, (size_t)msz,
                 "window-1 anomalous (%.6f), window-2 not a valid HW window",
                 rate_12);
        return TC5B_INCONCLUSIVE;
    }

    /* rate_12 in [0.95,1.10].  PASS requires ALL three initial samples to be
     * REGISTER_DERIVED.  A numerically good rate with any SW endpoint measures
     * wall-clock or mixed-domain accuracy, not hardware PHC coherence. */
    if (all_hw) {
        snprintf(msg, (size_t)msz,
                 "PHC monotone, rate %.6f in [0.95,1.10]", rate_12);
        return TC5B_PASS;
    }
    snprintf(msg, (size_t)msz,
             "rate %.6f in range but not all initial samples REGISTER_DERIVED"
             " — hardware coherence unverifiable", rate_12);
    return TC5B_INCONCLUSIVE;
}

/* ─── TC5b_ClassifyW3 ───────────────────────────────────────────────────── */
/**
 * Classify the frozen-clock window-3 result.
 * Called only when TC5b_Classify3() returned TC5B_TAKE_W3.
 *
 * @param hw3       provenance of r3 (already known from Classify3 call)
 * @param hw4       provenance of r4 (the new window-3 sample)
 * @param rate_23   rate from window-2 (r2->r3), used for improvement check
 * @param rate_34   rate from window-3 (r3->r4)
 * @param msg       caller-supplied buffer
 * @param msz       size of msg buffer
 * @return TC5B_PASS, TC5B_FAIL, or TC5B_INCONCLUSIVE (never TC5B_TAKE_W3)
 *
 * Classification:
 *   w3 not fully HW-derived               => INCONCLUSIVE
 *   rate_34 in [0.95,1.10]                => PASS  (approved recovery)
 *   rate_34 improving (>rate_23, >=0.85)  => INCONCLUSIVE (not yet in spec)
 *   all other cases (still frozen, etc.)  => FAIL
 */
static TC5b_V TC5b_ClassifyW3(
    int hw3, int hw4,
    double rate_23, double rate_34,
    char *msg, int msz)
{
    int w3_hw = hw3 && hw4;

    if (!w3_hw) {
        snprintf(msg, (size_t)msz,
                 "window-3 endpoint not REGISTER_DERIVED"
                 " — hardware recovery unverifiable");
        return TC5B_INCONCLUSIVE;
    }
    if (TC5b_RateOK(rate_34)) {
        snprintf(msg, (size_t)msz,
                 "w1+w2 transient, w3 rate %.6f in [0.95,1.10]"
                 " — approved recovery PASS", rate_34);
        return TC5B_PASS;
    }
    if (rate_34 > rate_23 && rate_34 >= 0.85) {
        /* Improving but still outside spec: INCONCLUSIVE, not PASS. */
        snprintf(msg, (size_t)msz,
                 "w3=%.6f improving but outside [0.95,1.10]"
                 " — hardware spec not met", rate_34);
        return TC5B_INCONCLUSIVE;
    }
    /* Frozen / worsening / still out-of-spec — hardware clock failure. */
    snprintf(msg, (size_t)msz,
             "all 3 windows bad (w1+w2 frozen, w3=%.6f)"
             " — hardware clock failure", rate_34);
    return TC5B_FAIL;
}

#endif /* TC5B_CLASSIFY_H */
