/*
 * tas_activate_eval.h - what the wire shows after IOCTL_AVB_TAS_ACTIVATE (#357, TEST-TAS-API-004 #369, TEST-TAS-API-005 #370).
 *
 * Pure rules, shared by tests/hardware/test_tas_activate.c and its offline cases TC-MOCK-050 to 052 in
 * test_tas_restore_contract.c. Passing those cases says nothing about hardware.
 *
 * Capture times are host times; the schedule runs on the PHC. Two bracketed PHC reads, one before the activation and one
 * after the frames drained, define the line between them. The PHC rate differs from the host's (1.0293 on I226 0x125B
 * REV_04, 3 runs), so one anchor is not enough.
 *
 * The edge margin: on the E6 PASS run (LOCAL_DIAGNOSTIC, 2026-11-05, I226 0x125B REV_04, Debug) every one of 168 arrivals
 * after BaseT mapped to 200.40 to 398.85 ms of a 200 to 400 ms window, held frames 0.40 ms after the window opened. An
 * arrival within 20 ms of an edge decides nothing, except that a held frame released at the window start counts as
 * released: leaving at the start is exactly what holding means.
 *
 * Verifies: #357
 */
#pragma once

#include <windows.h>
#include "../../include/avb_ioctl.h"

#define TAS_ACT_EDGE_MARGIN_NS     20000000LL
#define TAS_ACT_SEEN_MIN_PERCENT   90u            /* the same share the baseline must reach before any mutation */
#define TAS_ACT_ANCHOR_SPAN_MAX_US 100000000LL    /* 100 s: keeps the products below in 64 bits */

typedef struct {
    long long host_us;   /* midpoint of the host reads around the PHC read */
    long long phc_ns;
} TAS_ACT_ANCHOR;

/* PHC time of a host time on the line through a and b. FALSE when the anchors define no usable line */
static __inline BOOL tas_act_phc_at(const TAS_ACT_ANCHOR *a, const TAS_ACT_ANCHOR *b, long long host_us, long long *phc_ns)
{
    long long span = b->host_us - a->host_us, from = host_us - a->host_us;

    if (span <= 0 || b->phc_ns <= a->phc_ns || span > TAS_ACT_ANCHOR_SPAN_MAX_US || from > TAS_ACT_ANCHOR_SPAN_MAX_US ||
        from < -TAS_ACT_ANCHOR_SPAN_MAX_US || b->phc_ns - a->phc_ns > 2 * span * 1000LL) {
        return FALSE;
    }
    long long delta = b->phc_ns - a->phc_ns;
    *phc_ns = a->phc_ns + from * (delta / span) + from * (delta % span) / span;
    return TRUE;
}

/* One tagged frame sent after the activation returned. PHC times */
typedef struct {
    long long sent_ns, arrival_ns;
    BOOL seen;
} TAS_ACT_FRAME;

typedef struct {
    unsigned frames, seen;
    unsigned early;             /* HOLD: arrived before the first window after BaseT */
    unsigned held_before_base;  /* PASS: sent well before BaseT, yet arrived after it */
    unsigned flowed;            /* PASS: arrived before BaseT */
    unsigned released;          /* HOLD: sent before BaseT, arrived in the first window */
    unsigned inside, near_edge, outside;   /* after BaseT, apart from the two above */
} TAS_ACT_WIRE_COUNTS;

typedef enum {
    TAS_ACT_WIRE_GATED = 0,          /* the expected behaviour */
    TAS_ACT_WIRE_UNOBSERVED,         /* nothing to judge: no frames, or none seen on a capture that saw nothing at all */
    TAS_ACT_WIRE_EARLY,
    TAS_ACT_WIRE_HELD_BEFORE_BASE,
    TAS_ACT_WIRE_OUTSIDE,            /* a frame outside every window after BaseT */
    TAS_ACT_WIRE_LOST,               /* fewer seen than the baseline share on a live capture: stalled or dropped */
    TAS_ACT_WIRE_NOT_RELEASED,       /* HOLD: no held frame left in the first window */
    TAS_ACT_WIRE_NOT_FLOWING,        /* PASS: no frame passed before BaseT */
    TAS_ACT_WIRE_NO_WINDOW           /* no arrival inside a window after BaseT, so gating is not shown */
} TAS_ACT_WIRE_CLASS;

static __inline TAS_ACT_WIRE_CLASS tas_act_wire(avb_u32 pre_activation, const TAS_ACT_FRAME *f, unsigned n, BOOL capture_alive,
                                                long long base, long long cycle, long long start, long long end, long long margin,
                                                TAS_ACT_WIRE_COUNTS *c)
{
    unsigned i;
    BOOL hold = pre_activation == AVB_TAS_PRE_HOLD;

    ZeroMemory(c, sizeof(*c));
    c->frames = n;
    for (i = 0; i < n; i++) {
        long long since, phase, index;

        if (!f[i].seen) {
            continue;
        }
        c->seen++;
        if (f[i].arrival_ns < base) {
            if (hold) { c->early++; } else { c->flowed++; }
            continue;
        }
        since = f[i].arrival_ns - base;
        index = cycle > 0 ? since / cycle : 0;
        phase = cycle > 0 ? since % cycle : since;
        if (!hold && f[i].sent_ns < base - margin) {
            c->held_before_base++;
            continue;
        }
        if (hold && index == 0 && phase < start - margin) {
            c->early++;
            continue;
        }
        if (hold && index == 0 && f[i].sent_ns < base && phase < end + margin) {
            c->released++;
        }
        if ((phase + margin > start && phase < start + margin) || (phase + margin > end && phase < end + margin)) {
            c->near_edge++;
        } else if (phase >= start && phase < end) {
            c->inside++;
        } else {
            c->outside++;
        }
    }
    if (n == 0 || (c->seen == 0 && !capture_alive)) {
        return TAS_ACT_WIRE_UNOBSERVED;
    }
    if (c->early != 0) {
        return TAS_ACT_WIRE_EARLY;
    }
    if (c->held_before_base != 0) {
        return TAS_ACT_WIRE_HELD_BEFORE_BASE;
    }
    if (c->outside != 0) {
        return TAS_ACT_WIRE_OUTSIDE;
    }
    if ((unsigned long long)c->seen * 100u < (unsigned long long)n * TAS_ACT_SEEN_MIN_PERCENT) {
        return TAS_ACT_WIRE_LOST;
    }
    if (hold ? c->released == 0 : c->flowed == 0) {
        return hold ? TAS_ACT_WIRE_NOT_RELEASED : TAS_ACT_WIRE_NOT_FLOWING;
    }
    return c->inside == 0 ? TAS_ACT_WIRE_NO_WINDOW : TAS_ACT_WIRE_GATED;
}

static __inline const char *tas_act_wire_name(TAS_ACT_WIRE_CLASS w)
{
    switch (w) {
    case TAS_ACT_WIRE_GATED:            return "GATED";
    case TAS_ACT_WIRE_UNOBSERVED:       return "UNOBSERVED";
    case TAS_ACT_WIRE_EARLY:            return "EARLY";
    case TAS_ACT_WIRE_HELD_BEFORE_BASE: return "HELD_BEFORE_BASE";
    case TAS_ACT_WIRE_OUTSIDE:          return "OUTSIDE";
    case TAS_ACT_WIRE_LOST:             return "LOST";
    case TAS_ACT_WIRE_NOT_RELEASED:     return "NOT_RELEASED";
    case TAS_ACT_WIRE_NOT_FLOWING:      return "NOT_FLOWING";
    default:                            return "NO_WINDOW";
    }
}
