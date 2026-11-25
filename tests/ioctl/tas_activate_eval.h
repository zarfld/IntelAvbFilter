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
#include "../common/avb_verdict.h"   /* the SDK NTSTATUS values (#368 semantic cases) */

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
    BOOL operations_pending;  /* an operation this test issued on the adapter was not observed to have ended before the cycle */
} TAS_ACT_RESTORE_EVIDENCE;

typedef enum {
    TAS_ACT_RESTORE_COMPLETE = 0,
    TAS_ACT_RESTORE_INCOMPLETE,   /* an observed failure: residue, mode on, a dirty record, no TX, or no cycle */
    TAS_ACT_RESTORE_UNVERIFIED    /* nothing observed failed, but the registers or the wire were not measured, or an operation of the
                                     test may still change what was measured */
} TAS_ACT_RESTORE_CLASS;

static __inline TAS_ACT_RESTORE_CLASS tas_act_restore(const TAS_ACT_RESTORE_EVIDENCE *e)
{
    if (!e->cycle_completed || !e->adapter_returned || !e->driver_clean || (e->tx_probed && !e->tx_on_wire) ||
        (e->regs_measured && (e->regs_differing != 0 || !e->tsn_mode_off))) {
        return TAS_ACT_RESTORE_INCOMPLETE;
    }
    if (!e->regs_measured || !e->tx_probed || e->operations_pending) {
        return TAS_ACT_RESTORE_UNVERIFIED;
    }
    return TAS_ACT_RESTORE_COMPLETE;
}

/* The CLEANUP line: the NIC cycle itself ran and the adapter came back, with no operation of the test left to undo it */
static __inline TAS_ACT_RESTORE_CLASS tas_act_cycle(const TAS_ACT_RESTORE_EVIDENCE *e)
{
    if (!e->cycle_completed || !e->adapter_returned) {
        return TAS_ACT_RESTORE_INCOMPLETE;
    }
    return e->operations_pending ? TAS_ACT_RESTORE_UNVERIFIED : TAS_ACT_RESTORE_COMPLETE;
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

/* ---- #368 --invalid (TEST-TAS-API-003): a semantic case's COMPLETION under the validation order (TC-MOCK-060, 061) ----
 * The R7 steps of #365 after the 13.3 header checks, numbered as published. */
typedef enum {
    TAS_INV_STEP_PRE = 2,     /* pre_activation */
    TAS_INV_STEP_CLAIM,       /* the per-adapter claim: BUSY */
    TAS_INV_STEP_META,        /* generation/epoch: STATE_CONFLICT */
    TAS_INV_STEP_SPEED,       /* driver-owned link speed known: NOT_READY */
    TAS_INV_STEP_SCHEDULE,    /* TAS-DR-03..09 */
    TAS_INV_STEP_LEAD,        /* PHC and the minimum BaseT margin */
    TAS_INV_STEP_TIER,        /* HOLD tier and explicit acceptance: NOT_HARDWARE_VERIFIED */
    TAS_INV_STEP_CLEAN        /* clean state: NOT_CLEAN */
} TAS_INV_STEP;

typedef struct {
    TAS_INV_STEP step;             /* the step that checks the case's own field */
    LONG required;                 /* the completion the case's own rule requires, with Information 0 */
    BOOL stale_case;               /* STALE_GENERATION / STALE_EPOCH: the claim needs an established current pair */
    BOOL captured;                 /* the raw NTSTATUS was captured */
    LONG status;
    DWORD information;
    BOOL pair_established;         /* the test's query reported a non-zero generation and epoch */
    BOOL request_pair_zero;        /* the request carries generation 0 or epoch 0 */
    BOOL request_pair_mismatch;    /* the request's pair differs from the established current pair */
    avb_u32 speed_before;          /* query link_speed_mbps before the case, 0 = unknown */
    BOOL speed_after_read;         /* the query after the case was read */
    avb_u32 speed_after;
    avb_u32 tier_hold;             /* query limits.tier_hold, AVB_TAS_TIER_*, 0 = not reported */
    BOOL before_read;              /* the case's own query before the request was valid (pair, speed, tier measured) */
    BOOL pair_after_read;          /* the query after the case was valid for generation and epoch */
    BOOL pair_after_differs;       /* that query's pair differs from the one before the request */
    BOOL field_speed_dependent;    /* the case's own invalidity depends on the speed (WINDOW_MIN) */
} TAS_INV_SEM_EVIDENCE;

typedef enum {
    TAS_INV_SEM_PASS = 0,             /* PASS */
    TAS_INV_SEM_FAIL,                 /* FAIL */
    TAS_INV_SEM_EARLIER_STEP,         /* INCONCLUSIVE */
    TAS_INV_SEM_STALE_UNESTABLISHED,  /* INCONCLUSIVE */
    TAS_INV_SEM_UNDETERMINED,         /* INCONCLUSIVE */
    TAS_INV_SEM_NOT_CAPTURED          /* INCONCLUSIVE */
} TAS_INV_SEM_CLASS;

/* The case's COMPLETION under #365 R7 (#368 oracle amendment 2026-11-19). The control carries AVB_TAS_F_ACCEPT_UNVERIFIED, a
 * valid schedule and lead, so before the case's own step only the measured metadata (step 4, #352 R1: 0 is never a wildcard)
 * and the driver-owned speed (step 5, #351 R2) can refuse it. expected/at: the completion and step the class was judged against.
 * PASS: the case's own rule reached with its exact completion. EARLIER_STEP: the actual request must be refused at an earlier
 * step and was, so the field's own rule is not reached. FAIL: a completion the order excludes. Never PASS on a missing
 * prerequisite. */
static __inline TAS_INV_SEM_CLASS tas_inv_semantic(const TAS_INV_SEM_EVIDENCE *e, LONG *expected, TAS_INV_STEP *at)
{
    BOOL earlier = FALSE, speed_matters, speed_stable, meta_matters;

    /* What R7 requires for the ACTUAL request, resolved first so that every class, a violation included, is reported against it
     * (finding on the 2026-11-19 20:20 hardware run) */
    *expected = e->required;
    *at = e->step;
    if (e->step > TAS_INV_STEP_META && (e->request_pair_zero || (e->pair_established && e->request_pair_mismatch))) {
        *expected = (LONG)STATUS_INVALID_DEVICE_STATE;
        *at = TAS_INV_STEP_META;
        earlier = TRUE;
    } else if (e->step > TAS_INV_STEP_SPEED && e->before_read && e->speed_before == 0) {
        *expected = (LONG)STATUS_DEVICE_NOT_READY;
        *at = TAS_INV_STEP_SPEED;
        earlier = TRUE;
    }
    /* PR #491 review: isolation rests on measurements around the case. The speed matters past step 5 unless the metadata refuses
     * first, and is stable only when measured before and after with the same value (r4634405078, r4634404917). The pair matters
     * from step 4 unless the request carries a 0, which is refused whatever the current pair; it must be measured before and
     * after, unchanged (r4634404991) */
    speed_matters = e->step > TAS_INV_STEP_SPEED && *at != TAS_INV_STEP_META;
    speed_stable = e->before_read && e->speed_after_read && e->speed_after == e->speed_before;
    meta_matters = e->step >= TAS_INV_STEP_META && !e->request_pair_zero;
    if (!e->captured) {
        return TAS_INV_SEM_NOT_CAPTURED;
    }
    /* violations whatever the prerequisites: no invalid request succeeds, the code is allocated (#362), errors carry no output.
     * Only a field whose invalidity depends on the speed (WINDOW_MIN) is not shown invalid by a success while the speed is not
     * stably measured (r4634404917) */
    if (e->status >= 0 || e->status == (LONG)STATUS_INVALID_DEVICE_REQUEST || e->information != 0) {
        if (e->field_speed_dependent && e->status >= 0 && speed_matters && !speed_stable) {
            return TAS_INV_SEM_UNDETERMINED;
        }
        return TAS_INV_SEM_FAIL;
    }
    /* a query reporting HOLD unsupported or not implemented leaves no valid control; that outcome's position is unpublished */
    if (e->tier_hold == AVB_TAS_TIER_HW_UNSUPPORTED || e->tier_hold == AVB_TAS_TIER_NOT_IMPLEMENTED) {
        return TAS_INV_SEM_UNDETERMINED;
    }
    /* without acceptance a HOLD IMPLEMENTED_VERIFIED request is valid: NO_ACCEPTANCE is then no invalid request */
    if (e->step == TAS_INV_STEP_TIER && e->tier_hold == AVB_TAS_TIER_IMPLEMENTED_VERIFIED) {
        return TAS_INV_SEM_UNDETERMINED;
    }
    /* BUSY after the claim step: another operation holds the adapter, not attributable to the case */
    if (e->step > TAS_INV_STEP_CLAIM && e->status == (LONG)STATUS_DEVICE_BUSY) {
        return TAS_INV_SEM_UNDETERMINED;
    }
    if (meta_matters && (!e->before_read || !e->pair_after_read || e->pair_after_differs)) {
        return TAS_INV_SEM_UNDETERMINED;   /* the pair the driver compared under its claim is not determined */
    }
    if (speed_matters && !speed_stable) {
        return TAS_INV_SEM_UNDETERMINED;   /* the speed the driver sampled under its claim is not determined */
    }
    if (earlier) {
        return e->status == *expected ? TAS_INV_SEM_EARLIER_STEP : TAS_INV_SEM_FAIL;
    }
    if (e->status != e->required) {
        return TAS_INV_SEM_FAIL;
    }
    return e->stale_case && !e->pair_established ? TAS_INV_SEM_STALE_UNESTABLISHED : TAS_INV_SEM_PASS;
}

/* TAS-DR-05 (DES-C-TAS-010 5.2, #351 R2): a window greater than 1.1 x the transmit time of max(512 bytes, max_frame_bytes plus
 * overhead) at the driver-owned speed. 1.1 x bits / Mbit/s in ns = bytes x 8 x 1000 x 1.1 / mbps = bytes x 8800 / mbps. */
#define TAS_DR05_MIN_BYTES        512u
#define TAS_DR05_NS_MBPS_PER_BYTE 8800u

/* WINDOW_MIN's window in ns: the largest one not greater than the bound computed without the unpublished overhead term, which
 * only raises the bound, so the window is invalid under either reading. 0: no window (speed unknown, or the bound is below 1 ns) */
static __inline avb_u64 tas_inv_window_min_ns(avb_u32 max_frame_bytes, avb_u32 link_mbps)
{
    avb_u64 bytes = max_frame_bytes > TAS_DR05_MIN_BYTES ? max_frame_bytes : TAS_DR05_MIN_BYTES;

    return link_mbps == 0 ? 0 : bytes * TAS_DR05_NS_MBPS_PER_BYTE / link_mbps;
}

/* ---- #369 --hold (TEST-TAS-API-004): the race's COMMIT and GENERATION (TC-MOCK-062, 063) ----
 * Two bound open instances race the same accepted HOLD control (#369 amendments 2026-11-18 and 2026-11-19). */
typedef struct {
    BOOL captured;          /* the raw NTSTATUS was captured */
    LONG status;
    DWORD information;
    BOOL echoed;            /* bytes 0..135 (up to out_flags) equal to what was sent */
    avb_u32 out_flags;
    avb_u32 reserved1;
} TAS_HOLD_CALL;

typedef struct {
    TAS_HOLD_CALL call[2];
    BOOL accept_sent;            /* both requests carried AVB_TAS_F_ACCEPT_UNVERIFIED */
    BOOL pre_valid;              /* the query immediately before the race was valid (pair, speed and tier measured) */
    BOOL pre_clean;              /* immediately before the race the adapter was measured clean as the proved pre-state is (GET_TAS_STATE
                                    CLEAN and a clean, readable TAS query); FALSE when not clean or not measured (#365 R7 step 9) */
    avb_u64 pre_generation;      /* the pair both requests carried (that query's, or the last observed one when it was not valid) */
    avb_u64 pre_epoch;
    avb_u32 pre_mbps;            /* that query's link_speed_mbps, 0 = unknown */
    avb_u32 tier_hold;           /* that query's limits.tier_hold, AVB_TAS_TIER_*, 0 = not reported */
    BOOL post_valid;             /* the query right after the race, before any further request, was valid */
    avb_u64 post_generation;
    avb_u64 post_epoch;
    avb_u32 post_mbps;
} TAS_HOLD_EVIDENCE;

typedef enum {
    TAS_HOLD_OUT_BIT0 = 0,   /* acceptance acts: AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE */
    TAS_HOLD_OUT_ZERO,       /* acceptance does not act: 0 */
    TAS_HOLD_OUT_UNJUDGED    /* not determined by a measurement: reported, not judged */
} TAS_HOLD_OUT_EXPECT;

typedef enum {
    TAS_HOLD_COMMIT_PASS = 0,             /* PASS */
    TAS_HOLD_COMMIT_NOT_SERVED,           /* FAIL */
    TAS_HOLD_COMMIT_BAD_SUCCESS,          /* FAIL */
    TAS_HOLD_COMMIT_OUT_FLAGS,            /* FAIL */
    TAS_HOLD_COMMIT_ZERO_ACCEPTED,        /* FAIL */
    TAS_HOLD_COMMIT_TWO_SUCCESSES,        /* FAIL */
    TAS_HOLD_COMMIT_TIER_REFUSED,         /* FAIL */
    TAS_HOLD_COMMIT_R5,                   /* FAIL */
    TAS_HOLD_COMMIT_META_ZERO,            /* FAIL */
    TAS_HOLD_COMMIT_STATE_REFUSED,        /* FAIL */
    TAS_HOLD_COMMIT_NOT_READY,            /* FAIL */
    TAS_HOLD_COMMIT_OTHER,                /* FAIL */
    TAS_HOLD_COMMIT_OUT_UNJUDGED,         /* INCONCLUSIVE */
    TAS_HOLD_COMMIT_PAIR_CHANGED,         /* INCONCLUSIVE */
    TAS_HOLD_COMMIT_SPEED_UNDETERMINED,   /* INCONCLUSIVE */
    TAS_HOLD_COMMIT_NOT_CAPTURED,         /* INCONCLUSIVE */
    TAS_HOLD_COMMIT_CLEAN_UNDETERMINED    /* INCONCLUSIVE */
} TAS_HOLD_COMMIT_CLASS;

typedef enum {
    TAS_HOLD_GEN_PASS = 0,              /* PASS */
    TAS_HOLD_GEN_FAIL,                  /* FAIL */
    TAS_HOLD_GEN_PRE_UNREPORTED,        /* INCONCLUSIVE */
    TAS_HOLD_GEN_POST_UNREPORTED,       /* INCONCLUSIVE */
    TAS_HOLD_GEN_EPOCH_CHANGED,         /* INCONCLUSIVE */
    TAS_HOLD_GEN_UNKNOWN_EXPECTATION    /* INCONCLUSIVE */
} TAS_HOLD_GEN_CLASS;

/* The out_flags a success must carry (#365 R4, #362, #369 oracle amendment 2026-11-19): AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE
 * when unverified acceptance actually acted, otherwise 0. Whether it acts is the measured HOLD tier of the query before the race:
 * IMPLEMENTED_UNVERIFIED needs the acceptance, IMPLEMENTED_VERIFIED does not. Any other value (0 = not reported, or a tier that
 * leaves no valid control) and a query that is not valid determine nothing: the output is reported, not judged. Never a
 * permanent 0, never 1 merely because the flag was sent. */
static __inline TAS_HOLD_OUT_EXPECT tas_hold_out_expect(BOOL pre_valid, avb_u32 tier_hold)
{
    if (!pre_valid) {
        return TAS_HOLD_OUT_UNJUDGED;
    }
    if (tier_hold == AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED) {
        return TAS_HOLD_OUT_BIT0;
    }
    return tier_hold == AVB_TAS_TIER_IMPLEMENTED_VERIFIED ? TAS_HOLD_OUT_ZERO : TAS_HOLD_OUT_UNJUDGED;
}

static __inline BOOL tas_hold_is(const TAS_HOLD_CALL *c, NTSTATUS status)
{
    return c->captured && c->status == (LONG)status;
}

/* Generation transitions one completion establishes (#352 R1 L2/L7/L9): a commit attempted writes (1), and so did BASE_PASSED, R5's
 * only STATUS_IO_TIMEOUT outcome (1, #352 R1 L7, PR #492); the published refusals of #365 R7 steps 1-9 (and an unserved code)
 * complete before any write (0). -1: not established - the status was not captured, or it is shared by outcomes before and after a
 * write (R5/R8: STATUS_IO_DEVICE_ERROR, STATUS_UNSUCCESSFUL) or unpublished */
static __inline int tas_hold_call_transitions(const TAS_HOLD_CALL *c)
{
    if (!c->captured) {
        return -1;
    }
    if (c->status == 0 || c->status == (LONG)STATUS_IO_TIMEOUT) {
        return 1;
    }
    if (c->status == (LONG)STATUS_DEVICE_BUSY || c->status == (LONG)STATUS_INVALID_DEVICE_STATE ||
        c->status == (LONG)STATUS_REQUEST_NOT_ACCEPTED || c->status == (LONG)STATUS_DEVICE_NOT_READY ||
        c->status == (LONG)STATUS_INVALID_PARAMETER || c->status == (LONG)STATUS_INVALID_DEVICE_REQUEST) {
        return 0;
    }
    return -1;
}

/* COMMIT of the race (#369 amendments; #365 R1/R2/R4/R5/R7): exactly one complete success (Information 144, inputs echoed,
 * reserved1 0, no output bit but bit 0, bit 0 as tas_hold_out_expect requires) and one BUSY or STATE refusal with no output.
 * Observed violations come first and are kept whatever else is missing. A tier refusal of the accepted control is FAIL (the flag
 * acts); an R5 status is FAIL and named (a shared status is no outcome detail); a STATE refusal of a request carrying 0 is FAIL
 * with the zero metadata as the likely cause. Any other captured status than SUCCESS, BUSY, STATE and NOT_READY is FAIL before
 * any INCONCLUSIVE branch (PR #492). A refusal attributable to a prerequisite that changed or was not measured around the race
 * (the pair under the claim, the driver-owned speed) is INCONCLUSIVE. NOT_CLEAN (R7 step 9) shares STATUS_INVALID_DEVICE_STATE
 * with STATE_CONFLICT (#365 13.3/13.4): a STATE refusal and a success are judged only on an adapter measured clean immediately
 * before the race, otherwise INCONCLUSIVE; refusals at earlier steps (metadata 4, speed 5, tier 8) do not depend on it (PR #492). */
static __inline TAS_HOLD_COMMIT_CLASS tas_hold_commit(const TAS_HOLD_EVIDENCE *e)
{
    const TAS_HOLD_OUT_EXPECT out = tas_hold_out_expect(e->pre_valid, e->tier_hold);
    const BOOL pair_zero = e->pre_generation == 0 || e->pre_epoch == 0;
    const BOOL pair_kept = e->pre_valid && e->post_valid && e->post_generation == e->pre_generation &&
                           e->post_epoch == e->pre_epoch;
    const BOOL speed_stable = e->pre_valid && e->post_valid && e->pre_mbps != 0 && e->post_mbps == e->pre_mbps;
    unsigned k, success = 0, state = 0, not_ready = 0;
    const TAS_HOLD_CALL *refusal = NULL;

    for (k = 0; k < 2; k++) {
        if (tas_hold_is(&e->call[k], STATUS_INVALID_DEVICE_REQUEST)) {
            return TAS_HOLD_COMMIT_NOT_SERVED;
        }
    }
    for (k = 0; k < 2; k++) {
        const TAS_HOLD_CALL *c = &e->call[k];
        if (!c->captured || c->status != 0) {
            refusal = c;
            continue;
        }
        success++;
        if (c->information != (DWORD)sizeof(AVB_TAS_OP_REQUEST) || !c->echoed || c->reserved1 != 0 ||
            (c->out_flags & ~AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE) != 0) {
            return TAS_HOLD_COMMIT_BAD_SUCCESS;
        }
        if (out != TAS_HOLD_OUT_UNJUDGED &&
            (c->out_flags & AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE) != (out == TAS_HOLD_OUT_BIT0 ? AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE : 0u)) {
            return TAS_HOLD_COMMIT_OUT_FLAGS;
        }
    }
    if (success != 0 && pair_zero) {
        return TAS_HOLD_COMMIT_ZERO_ACCEPTED;   /* #352 R1 L2: a zero pair reaching metadata validation is refused */
    }
    if (success == 2) {
        return TAS_HOLD_COMMIT_TWO_SUCCESSES;
    }
    for (k = 0; k < 2; k++) {
        if (tas_hold_is(&e->call[k], STATUS_REQUEST_NOT_ACCEPTED)) {
            return e->accept_sent ? TAS_HOLD_COMMIT_TIER_REFUSED : TAS_HOLD_COMMIT_OTHER;
        }
    }
    for (k = 0; k < 2; k++) {
        if (tas_hold_is(&e->call[k], STATUS_IO_TIMEOUT) || tas_hold_is(&e->call[k], STATUS_IO_DEVICE_ERROR)) {
            return TAS_HOLD_COMMIT_R5;
        }
    }
    for (k = 0; k < 2; k++) {
        const TAS_HOLD_CALL *c = &e->call[k];
        if (c->captured && c->status != 0 && c->information != 0) {
            return TAS_HOLD_COMMIT_OTHER;   /* errors carry no output */
        }
        if (c->captured && c->status != 0 && c->status != (LONG)STATUS_DEVICE_BUSY &&
            c->status != (LONG)STATUS_INVALID_DEVICE_STATE && c->status != (LONG)STATUS_DEVICE_NOT_READY) {
            return TAS_HOLD_COMMIT_OTHER;   /* an unexpected completion: no missing measurement explains it away */
        }
        state += tas_hold_is(c, STATUS_INVALID_DEVICE_STATE);
        not_ready += tas_hold_is(c, STATUS_DEVICE_NOT_READY);
    }
    if (success == 1) {
        if (!refusal->captured) {
            return TAS_HOLD_COMMIT_NOT_CAPTURED;
        }
        if (refusal->status != (LONG)STATUS_DEVICE_BUSY && refusal->status != (LONG)STATUS_INVALID_DEVICE_STATE) {
            return TAS_HOLD_COMMIT_OTHER;
        }
        if (!e->pre_clean) {
            return TAS_HOLD_COMMIT_CLEAN_UNDETERMINED;
        }
        return out == TAS_HOLD_OUT_UNJUDGED ? TAS_HOLD_COMMIT_OUT_UNJUDGED : TAS_HOLD_COMMIT_PASS;
    }
    if (!e->call[0].captured || !e->call[1].captured) {
        return TAS_HOLD_COMMIT_NOT_CAPTURED;
    }
    if (pair_zero) {
        return state == 2 ? TAS_HOLD_COMMIT_META_ZERO : TAS_HOLD_COMMIT_OTHER;   /* step 4 refuses a 0 first (#365 R7) */
    }
    if (state != 0) {
        if (!pair_kept) {
            return TAS_HOLD_COMMIT_PAIR_CHANGED;
        }
        if (e->pre_clean) {
            return TAS_HOLD_COMMIT_STATE_REFUSED;
        }
        /* the STATE refusal may be NOT_CLEAN (step 9); a NOT_READY refusal at a stable speed (step 5) is still a violation */
        return not_ready != 0 && speed_stable ? TAS_HOLD_COMMIT_NOT_READY : TAS_HOLD_COMMIT_CLEAN_UNDETERMINED;
    }
    if (not_ready != 0) {
        return speed_stable ? TAS_HOLD_COMMIT_NOT_READY : TAS_HOLD_COMMIT_SPEED_UNDETERMINED;
    }
    return TAS_HOLD_COMMIT_OTHER;
}

/* GENERATION across the race (#352 R1 L2/L7/L9, #369 oracle amendment 2026-11-19): post = pre + the transitions the two
 * completions establish (a commit one, a refusal before any write none), from a non-zero pair measured before and after in the same
 * context (epoch unchanged). 0 is never a value. A decrease is FAIL whatever else is unknown (no wrap, L12). */
static __inline TAS_HOLD_GEN_CLASS tas_hold_generation(const TAS_HOLD_EVIDENCE *e, unsigned *transitions)
{
    int t0 = tas_hold_call_transitions(&e->call[0]), t1 = tas_hold_call_transitions(&e->call[1]);

    *transitions = 0;
    if (!e->pre_valid || e->pre_generation == 0 || e->pre_epoch == 0) {
        return TAS_HOLD_GEN_PRE_UNREPORTED;
    }
    if (!e->post_valid || e->post_generation == 0 || e->post_epoch == 0) {
        return TAS_HOLD_GEN_POST_UNREPORTED;
    }
    if (e->post_epoch != e->pre_epoch) {
        return TAS_HOLD_GEN_EPOCH_CHANGED;
    }
    if (e->post_generation < e->pre_generation) {
        return TAS_HOLD_GEN_FAIL;
    }
    if (t0 < 0 || t1 < 0) {
        return TAS_HOLD_GEN_UNKNOWN_EXPECTATION;
    }
    *transitions = (unsigned)(t0 + t1);
    return e->post_generation - e->pre_generation == (avb_u64)*transitions ? TAS_HOLD_GEN_PASS : TAS_HOLD_GEN_FAIL;
}

/* ---- --metadata-events, TC-TASACT-META-002/-003 (#357 F1; #352 R1 L7/L8/L10; #505 markers): a PHC reseed held before its physical
 * write and an ACTIVATE that arrived while it was held. Both orders are valid; the order decides which generation step is required,
 * and it is taken from the driver's markers only. Pure rules, offline case TC-MOCK-066 */
typedef struct {
    unsigned long long hold_begin, hold_end, arrival, commit, write_enter, write_done;   /* 0 = marker not taken */
} TAS_RACE_MARKERS;

typedef enum { TAS_RACE_NOT_ESTABLISHED = 0, TAS_RACE_COMMIT_FIRST, TAS_RACE_WRITE_FIRST } TAS_RACE_ORDER;

/* The ACTIVATE arrived inside the hold, the write followed the hold, and the commit lies strictly before or after the write.
 * Anything else (no commit, outside the hold, inside the write) establishes no order */
static __inline TAS_RACE_ORDER tas_race_order(const TAS_RACE_MARKERS *m)
{
    if (m->hold_begin == 0 || m->arrival <= m->hold_begin || m->hold_end <= m->arrival || m->write_enter <= m->hold_end ||
        m->write_done <= m->write_enter || m->commit <= m->arrival) {
        return TAS_RACE_NOT_ESTABLISHED;
    }
    if (m->commit < m->write_enter) {
        return TAS_RACE_COMMIT_FIRST;
    }
    return m->commit > m->write_done ? TAS_RACE_WRITE_FIRST : TAS_RACE_NOT_ESTABLISHED;
}

/* Required step of the generation across both operations: commit +1, and +1 more when the reseed hit the retained schedule.
 * 0 = no order established, no step is required */
static __inline unsigned tas_race_required_step(TAS_RACE_ORDER order)
{
    return order == TAS_RACE_COMMIT_FIRST ? 2u : (order == TAS_RACE_WRITE_FIRST ? 1u : 0u);
}

/* Which PHC the activation used, when the held reseed steps the PHC back and BaseT had already passed on the old one (base <=
 * old_phc, read before the race; the old PHC refuses it, TAS-DR-08): only the new PHC permits the commit. phc_after < old_phc shows
 * independently that the clock was stepped back. A commit before the write is a commit of a passed BaseT */
typedef enum { TAS_RACE_PHC_NOT_SHOWN = 0, TAS_RACE_PHC_NEW, TAS_RACE_PHC_PASSED_BASE } TAS_RACE_PHC;

static __inline TAS_RACE_PHC tas_race_phc_used(TAS_RACE_ORDER order, long long base, long long old_phc, long long phc_after)
{
    if (base > old_phc || phc_after >= old_phc) {
        return TAS_RACE_PHC_NOT_SHOWN;
    }
    if (order == TAS_RACE_WRITE_FIRST) {
        return TAS_RACE_PHC_NEW;
    }
    return order == TAS_RACE_COMMIT_FIRST ? TAS_RACE_PHC_PASSED_BASE : TAS_RACE_PHC_NOT_SHOWN;
}

/* ---- --metadata-events, TC-TASACT-META-004 (#357, #352 R1 L10): a FilterRestart in the same adapter context while a schedule is
 * retained. Pure rules, offline case TC-MOCK-068 ---- */

/* A PHC reset without a tolerance: the clock ran before (two successive reads rising) and after, and the first value after is below the
 * last one before. Anything else shows no reset: a successful bring-up, a frozen clock or a clock that merely advanced */
typedef struct {
    BOOL read;            /* both reads succeeded */
    long long phc[2];     /* successive reads, oldest first */
} TAS_PHC_READS;

typedef enum { TAS_PHC_RESET_UNMEASURED = 0, TAS_PHC_RESET_SHOWN, TAS_PHC_RESET_NOT_SHOWN } TAS_PHC_RESET;

static __inline TAS_PHC_RESET tas_phc_reset(const TAS_PHC_READS *before, const TAS_PHC_READS *after)
{
    if (!before->read || !after->read || before->phc[1] <= before->phc[0] || after->phc[1] <= after->phc[0]) {
        return TAS_PHC_RESET_UNMEASURED;
    }
    return after->phc[0] < before->phc[1] ? TAS_PHC_RESET_SHOWN : TAS_PHC_RESET_NOT_SHOWN;
}

/* What the recorded result of an ACTIVATE that carried the pair from before the invalidating event says about that pair. Only
 * STATE_CONFLICT refuses it in step 4; NOT_CLEAN, INVALID, COMMITTED and every later result passed step 4, so the pair was still
 * current. BUSY, NOT_READY, NOT_SUPPORTED, NOT_IMPLEMENTED and "no result" may come before step 4 and say nothing */
typedef enum { TAS_STALE_NOT_JUDGED = 0, TAS_STALE_REFUSED, TAS_STALE_STILL_CURRENT } TAS_STALE_VIEW;

static __inline TAS_STALE_VIEW tas_stale_view(unsigned long result)
{
    switch (result) {
    case AVB_TAS_ACT_STATE_CONFLICT:
        return TAS_STALE_REFUSED;
    case AVB_TAS_ACT_BUSY:
    case AVB_TAS_ACT_NOT_READY:
    case AVB_TAS_ACT_NOT_SUPPORTED:
    case AVB_TAS_ACT_NOT_IMPLEMENTED:
    case AVB_TAS_ACT_NOT_BOUND:
    case AVB_TAS_ACT_ADAPTER_MISMATCH:
    case 0xFFFFFFFFul:   /* none recorded */
        return TAS_STALE_NOT_JUDGED;
    default:
        return TAS_STALE_STILL_CURRENT;
    }
}

/* The context after a detach and attach (#352 R1 L1-L4, L11): a non-zero epoch that differs from the old one, generation 1, record
 * CLEAN. An equal epoch is not a violation by itself (a random redraw is no proof of inequality, L4), it shows no new context */
typedef enum { TAS_NEW_CONTEXT_OK = 0, TAS_NEW_CONTEXT_VIOLATION, TAS_NEW_CONTEXT_NOT_SHOWN } TAS_NEW_CONTEXT;

static __inline TAS_NEW_CONTEXT tas_new_context(unsigned long long old_epoch, BOOL valid, unsigned long long epoch, unsigned long long generation,
                                                BOOL record_read, BOOL record_clean)
{
    if (old_epoch == 0 || !valid || epoch == 0 || generation == 0 || !record_read || epoch == old_epoch) {
        return TAS_NEW_CONTEXT_NOT_SHOWN;
    }
    return generation == 1 && record_clean ? TAS_NEW_CONTEXT_OK : TAS_NEW_CONTEXT_VIOLATION;
}
