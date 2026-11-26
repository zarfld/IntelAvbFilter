/*
 * avb_tas_retained.h - the schedule and commit bracket an adapter context retains from its last TAS commit (#350, C6 of #365).
 *
 * Pure rules, shared by the IOCTL handlers in avb_integration_fixed.c, the device query and their offline cases (TC-MOCK-073 to -075
 * in tests/unit/test_tas_restore_contract.c). One per adapter context; no per-operation store (#352 is separate). Not device-specific.
 *
 * Written only under tas_meta_order and tas_reg_lock exclusive, in the same lock section as the record (tas_armed) and the generation it
 * belongs to; read by the query under its shared hold. The register lock is never held across a PHC operation: the timing is marked lost in
 * a short section before the device write and the outcome is published in a second one, both inside one hold of tas_meta_order.
 */
#pragma once

#include "../include/avb_ioctl.h"

/* What a device operation on the PHC reports about its effect, apart from its result code. ISSUED: a SYSTIM write went through the platform
 * write path. The path does not read the value back, so this is no hardware proof of a completed time step. UNKNOWN: only a clock-control
 * write was issued, or the operation cannot tell; nothing is claimed about the clock. NONE: no write was issued. Never lowered */
#define AVB_PHC_EFFECT_NONE    0
#define AVB_PHC_EFFECT_UNKNOWN 1
#define AVB_PHC_EFFECT_ISSUED  2

#define AVB_TAS_RET_SCHEDULE      0x1u   /* schedule: as handed to the commit; the expected values of the compared fields (#365 12.4) */
#define AVB_TAS_RET_BRACKET       0x2u   /* both PHC readings around the commit write */
#define AVB_TAS_RET_IN_TIME       0x4u   /* the operation completed COMMITTED: the reading after the commit lies before BaseT */
#define AVB_TAS_RET_DISCONTINUOUS 0x8u   /* a PHC write was issued since the commit, forward or backward (#352 R1 L10) */
#define AVB_TAS_RET_TIMING_LOST   0x10u  /* the commit timing is not reliable (COMMIT_INFO_LOST) */
#define AVB_TAS_RET_EVENT_SEQ     0x20u  /* event_seq: the context's event sequence read before the commit's first register write */

/* Event sequence of an adapter context (#365 K8 events decision 2026-11-26, C). Advanced once per counted notification; this step counts
 * the link class only (every NDIS_STATUS_LINK_STATE, valid, repeated or malformed). A value the sequence can no longer represent is
 * AVB_TAS_EVSEQ_LOST, "tracking lost": never a counted value and never left again, so it can never look like an unchanged sequence */
#define AVB_TAS_EVSEQ_LOST        ((avb_u64)~(avb_u64)0)

typedef struct AVB_TAS_RETAINED {
    avb_u32 flags;                /* AVB_TAS_RET_*; 0 = nothing retained */
    avb_u32 reserved;
    AVB_TAS_SCHEDULE schedule;
    avb_u64 phc_before_commit_ns;
    avb_u64 phc_after_commit_ns;
    avb_u64 event_seq;            /* with AVB_TAS_RET_EVENT_SEQ: the event sequence before the commit's first register write */
} AVB_TAS_RETAINED;

/* The event sequence now. Interlocked, any IRQL, no lock */
static __inline avb_u64 avb_tas_event_read(volatile LONG64 *seq)
{
    return (avb_u64)InterlockedCompareExchange64(seq, 0, 0);
}

/* One counted notification. Interlocked only, so FilterStatus may call it at DISPATCH_LEVEL; it takes no lock. The step that would
 * reach AVB_TAS_EVSEQ_LOST leaves the sequence there: tracking is lost for good in this context */
static __inline void avb_tas_event_advance(volatile LONG64 *seq)
{
    LONG64 v = InterlockedCompareExchange64(seq, 0, 0);

    for (;;) {
        LONG64 prev;
        if ((avb_u64)v == AVB_TAS_EVSEQ_LOST) {
            return;
        }
        prev = InterlockedCompareExchange64(seq, (LONG64)((avb_u64)v + 1u), v);
        if (prev == v) {
            return;
        }
        v = prev;
    }
}

/* One NDIS_STATUS_LINK_STATE received by FilterStatus. well_formed: whether its payload could be read. Both count: a repeated payload is
 * no "no event", and a malformed indication establishes uncertainty (#365 K8 events decision 2026-11-26, B). It is no proof of a physical
 * change */
static __inline void avb_tas_event_link_indication(volatile LONG64 *seq, int well_formed)
{
    (void)well_formed;
    avb_tas_event_advance(seq);
}

/* EVENT_UNRECONCILED for the retained commit (#365 decision 2026-11-26, C): the sequence sampled at the end of the query's pass differs
 * from the one retained before the commit's first write, or either is lost, or the commit retained none. Without a retained schedule
 * there is no commit to invalidate. Nothing here ends it: only a cleared record (CLEAN) or a new context */
static __inline int avb_tas_event_unreconciled(const struct AVB_TAS_RETAINED *t, avb_u64 now)
{
    if (t == NULL || (t->flags & AVB_TAS_RET_SCHEDULE) == 0) {
        return 0;
    }
    return (t->flags & AVB_TAS_RET_EVENT_SEQ) == 0 || t->event_seq == AVB_TAS_EVSEQ_LOST || now == AVB_TAS_EVSEQ_LOST ||
           now != t->event_seq;
}

/* The schedule and its bracket can be reported (#365 12.2): known, and the timing was not lost since */
static __inline int avb_tas_retained_timed(const AVB_TAS_RETAINED *t)
{
    return (t->flags & (AVB_TAS_RET_SCHEDULE | AVB_TAS_RET_BRACKET | AVB_TAS_RET_TIMING_LOST)) == (AVB_TAS_RET_SCHEDULE | AVB_TAS_RET_BRACKET);
}

static __inline void avb_tas_retained_clear(AVB_TAS_RETAINED *t, avb_u32 flags)
{
    memset(t, 0, sizeof(*t));
    t->flags = flags;
}

/* TAS_ACTIVATE returned from the device. state: the record it leaves (avb_tas_state_after). A refusal, or a failure whose record is
 * unchanged, changes nothing here either. A dirty record retains no schedule: what the hardware holds is not known. phc_before_ns: the
 * reading right before the BaseT writes (R8); phc_after_ns: the one after the commit write; event_seq: the context's event sequence read
 * before the operation's first register write (avb_tas_event_read), so an event during the writes stays visible */
static __inline void avb_tas_retained_activate(AVB_TAS_RETAINED *t, avb_u32 result, int write_attempted, avb_u32 state,
                                               const AVB_TAS_SCHEDULE *s, avb_u64 phc_before_ns, avb_u64 phc_after_ns,
                                               avb_u64 event_seq)
{
    if (!write_attempted) {
        return;
    }
    if (state == AVB_TAS_STATE_DIRTY) {
        avb_tas_retained_clear(t, AVB_TAS_RET_TIMING_LOST);
        return;
    }
    if (state != AVB_TAS_STATE_ARMED || (result != AVB_TAS_ACT_COMMITTED && result != AVB_TAS_ACT_BASE_PASSED &&
                                         result != AVB_TAS_ACT_COMMITTED_UNTIMED)) {
        return;
    }
    avb_tas_retained_clear(t, AVB_TAS_RET_SCHEDULE | AVB_TAS_RET_EVENT_SEQ);
    t->schedule = *s;
    t->event_seq = event_seq;
    if (result == AVB_TAS_ACT_COMMITTED_UNTIMED) {
        t->flags |= AVB_TAS_RET_TIMING_LOST;
        return;
    }
    t->phc_before_commit_ns = phc_before_ns;
    t->phc_after_commit_ns = phc_after_ns;
    t->flags |= AVB_TAS_RET_BRACKET | (result == AVB_TAS_ACT_COMMITTED ? AVB_TAS_RET_IN_TIME : 0u);
}

/* SETUP_TAS that attempted a TAS write is no commit: a schedule known from one stays for the comparison (#365 4.3: an overwrite is a
 * mismatch), its timing is no longer that of the last write. A record that stays CLEAN retains nothing */
static __inline void avb_tas_retained_setup(AVB_TAS_RETAINED *t, int write_attempted, avb_u32 state)
{
    if (state == AVB_TAS_STATE_CLEAN) {
        avb_tas_retained_clear(t, 0u);
    } else if (state == AVB_TAS_STATE_DIRTY) {
        avb_tas_retained_clear(t, AVB_TAS_RET_TIMING_LOST);
    } else if (write_attempted) {
        t->flags |= AVB_TAS_RET_TIMING_LOST;
    }
}

/* DISARM_TAS: the removed state retains nothing; a device path that may have written without removing it leaves the timing lost; one that
 * writes nothing changes nothing (#352 R1 L9) */
static __inline void avb_tas_retained_disarm(AVB_TAS_RETAINED *t, int write_attempted, int disarmed)
{
    if (disarmed) {
        avb_tas_retained_clear(t, 0u);
    } else if (write_attempted) {
        t->flags |= AVB_TAS_RET_TIMING_LOST;
    }
}

/* Immediately before a device operation that may replace the PHC: until its effect is known the timing is lost, so no query reports PENDING
 * on a clock that is being replaced. Returns the flags for avb_tas_retained_phc_end */
static __inline avb_u32 avb_tas_retained_phc_begin(AVB_TAS_RETAINED *t)
{
    avb_u32 before = t->flags;

    t->flags |= AVB_TAS_RET_TIMING_LOST;
    return before;
}

/* After it. ISSUED: discontinuous, whatever the operation's result. NONE: the state from before, shown not to have been touched. UNKNOWN: the
 * timing stays lost; no event is made up */
static __inline void avb_tas_retained_phc_end(AVB_TAS_RETAINED *t, avb_u32 before, int effect)
{
    if (effect == AVB_PHC_EFFECT_ISSUED) {
        t->flags = before | ((before & AVB_TAS_RET_SCHEDULE) != 0 ? AVB_TAS_RET_DISCONTINUOUS : 0u);
    } else if (effect == AVB_PHC_EFFECT_NONE) {
        t->flags = before;
    } else {
        t->flags = before | AVB_TAS_RET_TIMING_LOST;
    }
}
