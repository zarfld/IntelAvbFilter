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

typedef struct AVB_TAS_RETAINED {
    avb_u32 flags;                /* AVB_TAS_RET_*; 0 = nothing retained */
    avb_u32 reserved;
    AVB_TAS_SCHEDULE schedule;
    avb_u64 phc_before_commit_ns;
    avb_u64 phc_after_commit_ns;
} AVB_TAS_RETAINED;

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
 * reading right before the BaseT writes (R8); phc_after_ns: the one after the commit write */
static __inline void avb_tas_retained_activate(AVB_TAS_RETAINED *t, avb_u32 result, int write_attempted, avb_u32 state,
                                               const AVB_TAS_SCHEDULE *s, avb_u64 phc_before_ns, avb_u64 phc_after_ns)
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
    avb_tas_retained_clear(t, AVB_TAS_RET_SCHEDULE);
    t->schedule = *s;
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
