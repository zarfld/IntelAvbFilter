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
#include <stdio.h>
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

/*
 * Restoration after the test's NIC cycle, as #361 states it: complete only when every schedule register reads its baseline and
 * TSN mode is off, with TX shown on the wire; PHC discontinuity is a separate outcome and never decides restoration. Offline
 * cases TC-MOCK-053. The older rule in tas_restoration_eval.h (#328, test_tas_isolated_arm) counts a PHC reset as a failed
 * restoration and is not used here.
 */
typedef enum { TAS_ACT_PHC_UNMEASURED = 0, TAS_ACT_PHC_CONTINUOUS, TAS_ACT_PHC_DISCONTINUOUS } TAS_ACT_PHC_FACT;

typedef struct {
    BOOL cycle_completed;     /* disable and enable both returned success within their bounds */
    BOOL adapter_returned;    /* same NET_LUID, device ID and MAC bound again */
    BOOL driver_clean;        /* GET_TAS_STATE reports CLEAN */
    BOOL regs_measured;       /* every schedule register was read back by a dedicated read-only query */
    unsigned regs_differing;  /* schedule registers that differ from the pre-test baseline */
    BOOL tsn_mode_off;        /* TQAVCTRL transmit mode reads legacy; meaningful only when regs_measured */
    BOOL tx_probed;           /* tagged frames were sent and the observer was alive */
    BOOL tx_on_wire;          /* the baseline share arrived, none held */
    TAS_ACT_PHC_FACT phc;     /* reported, never used below */
} TAS_ACT_RESTORE_EVIDENCE;

typedef enum {
    TAS_ACT_RESTORE_COMPLETE = 0,
    TAS_ACT_RESTORE_INCOMPLETE,   /* an observed failure: residue, mode on, a dirty record, no TX, or no cycle */
    TAS_ACT_RESTORE_UNVERIFIED    /* nothing observed failed, but the registers or the wire were not measured */
} TAS_ACT_RESTORE_CLASS;

static __inline TAS_ACT_RESTORE_CLASS tas_act_restore(const TAS_ACT_RESTORE_EVIDENCE *e)
{
    if (!e->cycle_completed || !e->adapter_returned || !e->driver_clean || (e->tx_probed && !e->tx_on_wire) ||
        (e->regs_measured && (e->regs_differing != 0 || !e->tsn_mode_off))) {
        return TAS_ACT_RESTORE_INCOMPLETE;
    }
    if (!e->regs_measured || !e->tx_probed) {
        return TAS_ACT_RESTORE_UNVERIFIED;
    }
    return TAS_ACT_RESTORE_COMPLETE;
}

/* The PHC across the cycle against wall time. A NIC cycle reseeds it, which #361 reports as a discontinuity */
static __inline TAS_ACT_PHC_FACT tas_act_phc(BOOL read, long long phc_delta_ns, long long wall_ns, long long tolerance_ns)
{
    long long drift = phc_delta_ns - wall_ns;

    if (!read) {
        return TAS_ACT_PHC_UNMEASURED;
    }
    return (drift > -tolerance_ns && drift < tolerance_ns) ? TAS_ACT_PHC_CONTINUOUS : TAS_ACT_PHC_DISCONTINUOUS;
}

static __inline const char *tas_act_phc_name(TAS_ACT_PHC_FACT p)
{
    return p == TAS_ACT_PHC_CONTINUOUS ? "CONTINUOUS" : (p == TAS_ACT_PHC_DISCONTINUOUS ? "DISCONTINUOUS" : "UNMEASURED");
}

/* ---- #482 TEST-TASACT-FAULT-001 F2 classification (pure; TC-MOCK-058 / TC-MOCK-059 in tests/unit/test_tas_restore_contract.c).
 * The caller (tests/hardware/test_tas_activate.c --fault-versioned) maps each class to its verdict line and reason. ---- */
typedef enum {
    TAS_FV_STATE_DIRTY = 0,          /* PASS: the fault ran and the record is DIRTY */
    TAS_FV_STATE_NOT_DIRTY,          /* FAIL: the fault ran and the record is not DIRTY */
    TAS_FV_STATE_UNREAD,             /* INCONCLUSIVE: the record (GET_TAS_STATE) was not read */
    TAS_FV_STATE_CHANGED_UNREACHED,  /* FAIL: the fault path was not reached, yet the record or a restoration-set register changed */
    TAS_FV_STATE_NOT_EXERCISED       /* INCONCLUSIVE: the fault path was not reached and nothing measured changed */
} TAS_FV_STATE_CLASS;

/* fault_ran: the F2 fault completion was observed; record_read/armed: GET_TAS_STATE after the request; differing: restoration-set
 * registers measured different from the pre-state query. A measured change while the fault path was not reached is a violation
 * before anything else is considered: an unread record must not hide it (PR #489 review r4631805746). */
static __inline TAS_FV_STATE_CLASS tas_fv_f2_state(BOOL fault_ran, BOOL record_read, avb_u32 armed, unsigned differing)
{
    if (!fault_ran && (differing != 0 || (record_read && armed != AVB_TAS_STATE_CLEAN))) {
        return TAS_FV_STATE_CHANGED_UNREACHED;
    }
    if (!record_read) {
        return TAS_FV_STATE_UNREAD;
    }
    if (fault_ran) {
        return armed == AVB_TAS_STATE_DIRTY ? TAS_FV_STATE_DIRTY : TAS_FV_STATE_NOT_DIRTY;
    }
    return TAS_FV_STATE_NOT_EXERCISED;
}

/* The further otherwise-valid activation after F2's failure (#482, #369's rule for this probe) */
typedef struct {
    BOOL not_served;           /* completed STATUS_INVALID_DEVICE_REQUEST */
    BOOL status_unavailable;   /* the raw NTSTATUS was not captured */
    BOOL accepted;             /* completed STATUS_SUCCESS */
    BOOL output;               /* Information != 0 */
    BOOL state_refusal;        /* completed STATUS_INVALID_DEVICE_STATE */
    BOOL record_before;        /* GET_TAS_STATE read after the fault request ... */
    avb_u32 armed_before;      /* ... and its value */
    BOOL record_after;         /* GET_TAS_STATE read after the further request ... */
    avb_u32 armed_after;       /* ... and its value */
    unsigned differing;        /* restoration-set registers measured different: post-failure query vs after the further request */
    unsigned unread;           /* restoration-set registers not read in either query (17 when a query failed) */
    BOOL fault_ran;            /* the F2 fault completion was observed */
    BOOL metadata_reported;    /* the further request carried a non-zero generation and epoch */
} TAS_FV_REFUSE_EVIDENCE;

typedef enum {
    TAS_FV_REFUSE_PASS = 0,                 /* PASS */
    TAS_FV_REFUSE_NOT_SERVED,               /* FAIL: 0x80C not served */
    TAS_FV_REFUSE_STATUS_UNAVAILABLE,       /* INCONCLUSIVE */
    TAS_FV_REFUSE_CHANGED,                  /* FAIL: accepted, output, or a measured change */
    TAS_FV_REFUSE_OTHER_STATUS,             /* FAIL: refused with another status */
    TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED,  /* INCONCLUSIVE: the unchanged state is not established by the measurement */
    TAS_FV_REFUSE_NOT_DIRTY,                /* INCONCLUSIVE: the DIRTY precondition was not established */
    TAS_FV_REFUSE_METADATA_ZERO             /* INCONCLUSIVE: generation or epoch 0, NOT_CLEAN cannot be told from STATE_CONFLICT */
} TAS_FV_REFUSE_CLASS;

/* The completion of one request as evidence text (TC-MOCK-059). captured: avb_ioctl_ntstatus returned a driver completion, not
 * its capture sentinel AVB_NTSTATUS_UNAVAILABLE (tests/common/avb_verdict.h), which is never shown as a driver completion code
 * (PR #489 review r4632275740) */
static __inline void tas_fv_status_text(BOOL captured, LONG status, char *buf, size_t n)
{
    if (!captured) {
        sprintf_s(buf, n, "raw NTSTATUS not captured (no driver completion observed)");
    } else {
        sprintf_s(buf, n, "ntstatus=0x%08lX", (unsigned long)status);
    }
}

static __inline TAS_FV_REFUSE_CLASS tas_fv_f2_refuse(const TAS_FV_REFUSE_EVIDENCE *e)
{
    BOOL records = e->record_before && e->record_after;
    /* "unchanged" is measured only when every restoration-set register was read in both queries (PR #489 review r4631805812) */
    BOOL kept = records && e->armed_after == e->armed_before && e->differing == 0 && e->unread == 0;

    if (e->not_served) {
        return TAS_FV_REFUSE_NOT_SERVED;
    }
    /* Independently established violations first: a missing raw status never erases one (PR #489 review r4632275740); the caller
     * reports the missing status with the violation */
    if (e->accepted || e->output || (records && e->armed_after != e->armed_before) || e->differing != 0) {
        return TAS_FV_REFUSE_CHANGED;
    }
    if (e->status_unavailable) {
        return TAS_FV_REFUSE_STATUS_UNAVAILABLE;
    }
    if (!e->state_refusal) {
        return TAS_FV_REFUSE_OTHER_STATUS;
    }
    if (!kept) {
        return TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED;
    }
    if (!e->fault_ran || e->armed_before != AVB_TAS_STATE_DIRTY) {
        return TAS_FV_REFUSE_NOT_DIRTY;
    }
    if (!e->metadata_reported) {
        return TAS_FV_REFUSE_METADATA_ZERO;
    }
    return TAS_FV_REFUSE_PASS;
}
