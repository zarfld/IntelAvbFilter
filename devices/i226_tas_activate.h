/*
 * i226_tas_activate.h - first gate schedule on a clean I226 (#357).
 *
 * Shared by the driver and its offline cases (TC-MOCK-028 to 033 in tests/unit/test_tas_restore_contract.c), so the order and
 * the refusals tested there are the production code. Register access goes through I226_TAS_IO. Register definitions come
 * from the SSOT only, as #357 requires.
 *
 * The sequence is the one that ran on I226 0x125B REV_04 through tas_gate_investigation: mode, windows, shadow cycle, cycle,
 * BaseT high, BaseT low. Writing BaseT low is the commit point. PASS adds FutScdDis after the windows and writes BaseT low
 * as zero first (ADR-TSN-003, M1). HOLD uses no reserved bit.
 *
 * Fault path (Q 2026-11-10): when io->fault_injection is set, which only Debug drivers do, a nonzero reserved word N makes
 * schedule write N and the first restore write fail without reaching the hardware. That is the only way a registered HIL
 * test can reach FAILED_NOT_RESTORED. Without it, which is every Release driver, a nonzero reserved word is INVALID.
 *
 * Restoring TQAVCTRL writes the old value back and checks the readback. A readback does not prove the transmit mode the
 * hardware is in: see disable_tas in intel_i226_impl.c. A failure after the mode write can therefore need a NIC cycle, so
 * i226_tas_activate reports whether that write reached the adapter and the driver then records the adapter as dirty
 * (avb_tas_state_after, PR #397 R8).
 */
#pragma once

#include "../include/avb_ioctl.h"
#include "../intel-ethernet-regs/gen/i226_regs.h"
#include "../src/avb_tas_activate_state.h"

typedef struct {
    void *ctx;
    int (*read)(void *ctx, avb_u32 offset, avb_u32 *value);
    int (*write)(void *ctx, avb_u32 offset, avb_u32 value);
    int (*phc)(void *ctx, avb_u64 *ns);
    int fault_injection;   /* nonzero in Debug drivers only: the reserved word selects a failing write */
} I226_TAS_IO;

#define I226_TAS_MIN_FRAME_BYTES  64u
#define I226_TAS_MAX_FRAME_BYTES  9728u
#define I226_TAS_WINDOW_FLOOR     512u          /* bytes: the window rule never goes below this frame size */
#define I226_TAS_WINDOW_FACTOR    8800u         /* 1.1 x 8 bits x 1000: ns x Mbit/s per byte */

typedef struct {
    avb_u32 offset, old_value;
} I226_TAS_SAVED;

/* What the activation may overwrite, in write order: TQAVCTRL, STQT and ENDQT of every queue, QBVCYCLET_S, QBVCYCLET,
 * BASET_H, BASET_L. i226_tas_read_clean fills saved[] in exactly this order */
#define I226_TAS_SAVED_COUNT (1u + 2u * AVB_TAS_QUEUES + 4u)

/* i226_tas_write_schedule writes each saved register once for HOLD; PASS adds its FutScdDis write and the BASET_L zero write.
 * TC-MOCK-049 checks both counts against the writes that actually happen */
#define I226_TAS_HOLD_WRITES  I226_TAS_SAVED_COUNT
#define I226_TAS_PASS_WRITES  (I226_TAS_HOLD_WRITES + 2u)

/* The value written to a register whose one written field is FIELD: the SSOT field encoding, every other bit clear.
 * Validation keeps each value inside its field, which TC-MOCK-054 checks at the field limits */
#define I226_TAS_FIELD(REG, FIELD, value) ((avb_u32)REG##_SET(0, REG##_##FIELD##_MASK, REG##_##FIELD##_SHIFT, (value)))

/* Every field of the request. Reads no hardware, so an invalid request changes nothing. fault_injection: see the top */
static __inline avb_u32 i226_tas_validate_with(const AVB_TAS_ACTIVATE_REQUEST *r, int fault_injection)
{
    avb_u32 q, floor_bytes;
    struct tsn_tas_config gcl;

    if (r->version != AVB_TAS_ACTIVATE_VERSION || (r->pre_activation != AVB_TAS_PRE_HOLD && r->pre_activation != AVB_TAS_PRE_PASS)) {
        return AVB_TAS_ACT_INVALID;
    }
    if (r->reserved != 0 &&
        (!fault_injection || r->reserved > (r->pre_activation == AVB_TAS_PRE_PASS ? I226_TAS_PASS_WRITES : I226_TAS_HOLD_WRITES))) {
        return AVB_TAS_ACT_INVALID;
    }
    /* The cycle must fit QBVCYCLET.QBV_CYCLE_TIME, as the SSOT defines that field; the BaseT seconds must fit the
     * 32-bit BASET_H register (the SSOT defines its offset only, no field) */
    if (r->cycle_ns == 0 || r->cycle_ns > (I226_QBVCYCLET_QBV_CYCLE_TIME_MASK >> I226_QBVCYCLET_QBV_CYCLE_TIME_SHIFT) ||
        r->base_time_ns / NSEC_PER_SEC > (avb_u64)INTEL_MASK_32BIT) {
        return AVB_TAS_ACT_INVALID;
    }
    if (r->link_mbps != 10 && r->link_mbps != 100 && r->link_mbps != 1000 && r->link_mbps != 2500) {
        return AVB_TAS_ACT_INVALID;
    }
    if (r->max_frame_bytes < I226_TAS_MIN_FRAME_BYTES || r->max_frame_bytes > I226_TAS_MAX_FRAME_BYTES) {
        return AVB_TAS_ACT_INVALID;
    }
    floor_bytes = r->max_frame_bytes > I226_TAS_WINDOW_FLOOR ? r->max_frame_bytes : I226_TAS_WINDOW_FLOOR;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        avb_u32 start = r->window_start_ns[q], end = r->window_end_ns[q];

        if (start >= end || end > r->cycle_ns) {
            return AVB_TAS_ACT_INVALID;
        }
        if ((avb_u64)(end - start) * r->link_mbps <= (avb_u64)floor_bytes * I226_TAS_WINDOW_FACTOR) {
            return AVB_TAS_ACT_INVALID;
        }
    }
    /* GET_TAS_STATE must be able to report what is written: more intervals than gate list entries is refused */
    if (!avb_tas_gcl_from_windows(r, &gcl)) {
        return AVB_TAS_ACT_INVALID;
    }
    return AVB_TAS_ACT_COMMITTED;
}

/* The rule every Release driver applies */
static __inline avb_u32 i226_tas_validate(const AVB_TAS_ACTIVATE_REQUEST *r)
{
    return i226_tas_validate_with(r, 0);
}

/* The fault path: write number fail_at and the one after it fail. The schedule stops at its first failure, so the one
 * after it is the first restore write */
typedef struct {
    const I226_TAS_IO *inner;
    avb_u32 fail_at, writes;
} I226_TAS_FAULT;

static __inline int i226_tas_fault_read(void *ctx, avb_u32 offset, avb_u32 *value)
{
    const I226_TAS_FAULT *f = (const I226_TAS_FAULT *)ctx;

    return f->inner->read(f->inner->ctx, offset, value);
}

static __inline int i226_tas_fault_write(void *ctx, avb_u32 offset, avb_u32 value)
{
    I226_TAS_FAULT *f = (I226_TAS_FAULT *)ctx;

    f->writes++;
    if (f->writes == f->fail_at || f->writes == f->fail_at + 1u) {
        return -1;
    }
    return f->inner->write(f->inner->ctx, offset, value);
}

static __inline int i226_tas_fault_phc(void *ctx, avb_u64 *ns)
{
    const I226_TAS_FAULT *f = (const I226_TAS_FAULT *)ctx;

    return f->inner->phc(f->inner->ctx, ns);
}

/* Clean means no schedule state at all, so nothing here overwrites one. Anything unreadable is not clean */
static __inline int i226_tas_read_clean(const I226_TAS_IO *io, I226_TAS_SAVED *saved)
{
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    avb_u32 i = 0, q;

    saved[i++].offset = I226_TQAVCTRL;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        saved[i++].offset = start[q];
        saved[i++].offset = end[q];
    }
    saved[i++].offset = I226_QBVCYCLET_S;
    saved[i++].offset = I226_QBVCYCLET;
    saved[i++].offset = I226_BASET_H;
    saved[i++].offset = I226_BASET_L;
    for (i = 0; i < I226_TAS_SAVED_COUNT; i++) {
        if (io->read(io->ctx, saved[i].offset, &saved[i].old_value) != 0) {
            return 0;
        }
        if (saved[i].offset != I226_QBVCYCLET && saved[i].offset != I226_QBVCYCLET_S && saved[i].old_value != 0) {
            return 0;
        }
    }
    return 1;
}

/* Back to the values read, last written first. Every write is tried and read back, so one failure hides nothing */
static __inline avb_u32 i226_tas_restore(const I226_TAS_IO *io, const I226_TAS_SAVED *saved)
{
    avb_u32 i = I226_TAS_SAVED_COUNT, ok = 1;

    while (i-- != 0) {
        avb_u32 now = 0;

        if (io->write(io->ctx, saved[i].offset, saved[i].old_value) != 0 || io->read(io->ctx, saved[i].offset, &now) != 0 || now != saved[i].old_value) {
            ok = 0;
        }
    }
    return ok ? AVB_TAS_ACT_FAILED_RESTORED : AVB_TAS_ACT_FAILED_NOT_RESTORED;
}

/* The writes up to and including the commit point. Nonzero when one failed. *mode_written: the TQAVCTRL transmit-mode write,
 * always the first, was accepted by the adapter, so the transmit mode may have changed */
static __inline int i226_tas_write_schedule(const I226_TAS_IO *io, const AVB_TAS_ACTIVATE_REQUEST *r, avb_u32 tqavctrl, int *mode_written)
{
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    avb_u32 mode = tqavctrl | (avb_u32)I226_TQAVCTRL_TRANSMIT_MODE_MASK | (avb_u32)I226_TQAVCTRL_ENHANCED_QAV_MASK, q;
    avb_u32 base_ns = (avb_u32)(r->base_time_ns % NSEC_PER_SEC);

    if (io->write(io->ctx, I226_TQAVCTRL, mode) != 0) {
        return 1;
    }
    *mode_written = 1;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        if (io->write(io->ctx, start[q], I226_TAS_FIELD(I226_STQT, START_TIME, r->window_start_ns[q])) != 0 ||
            io->write(io->ctx, end[q], I226_TAS_FIELD(I226_ENDQT, END_TIME, r->window_end_ns[q])) != 0) {
            return 1;
        }
    }
    if (r->pre_activation == AVB_TAS_PRE_PASS && io->write(io->ctx, I226_TQAVCTRL, mode | (avb_u32)I226_TQAVCTRL_FUTSCDDIS_MASK) != 0) {
        return 1;
    }
    if (io->write(io->ctx, I226_QBVCYCLET_S, I226_TAS_FIELD(I226_QBVCYCLET_S, SHADOW_QBVCYCLE, r->cycle_ns)) != 0 ||
        io->write(io->ctx, I226_QBVCYCLET, I226_TAS_FIELD(I226_QBVCYCLET, QBV_CYCLE_TIME, r->cycle_ns)) != 0 ||
        io->write(io->ctx, I226_BASET_H, (avb_u32)(r->base_time_ns / NSEC_PER_SEC)) != 0) {
        return 1;
    }
    if (r->pre_activation == AVB_TAS_PRE_PASS && io->write(io->ctx, I226_BASET_L, 0) != 0) {
        return 1;
    }
    return io->write(io->ctx, I226_BASET_L, I226_TAS_FIELD(I226_BASET_L, BASE_TIME_L, base_ns)) != 0;
}

/* The whole operation. Returns AVB_TAS_ACTIVATE_RAN when r->result says what happened; only a committed result leaves a
 * schedule behind. *tsn_mode_written: the transmit-mode write reached the adapter (avb_tas_state_after, PR #397 R8) */
static __inline int i226_tas_activate(const I226_TAS_IO *io, AVB_TAS_ACTIVATE_REQUEST *r, int armed, int *tsn_mode_written)
{
    I226_TAS_SAVED saved[I226_TAS_SAVED_COUNT];
    avb_u64 phc = 0, after = 0;
    I226_TAS_FAULT fault;
    I226_TAS_IO faulty;
    avb_u32 valid;

    *tsn_mode_written = 0;
    r->phc_at_commit_ns = 0;
    valid = i226_tas_validate_with(r, io->fault_injection);
    /* #357 AC2: PASS only once ADR-TSN-003 is accepted. Until then (#363 interim Option C, Q 2026-11-13) a valid PASS request
     * is refused here, before any register or PHC access, in Debug and Release alike. A malformed one is still INVALID */
    if (valid == AVB_TAS_ACT_COMMITTED && r->pre_activation == AVB_TAS_PRE_PASS) {
        return AVB_TAS_ACTIVATE_NOT_IMPLEMENTED;
    }
    r->result = valid;
    if (r->result != AVB_TAS_ACT_COMMITTED) {
        return AVB_TAS_ACTIVATE_RAN;
    }
    if (r->reserved != 0) {   /* validation allowed it, so this is a Debug driver */
        fault.inner = io;
        fault.fail_at = r->reserved;
        fault.writes = 0;
        faulty.ctx = &fault;
        faulty.read = i226_tas_fault_read;
        faulty.write = i226_tas_fault_write;
        faulty.phc = i226_tas_fault_phc;
        faulty.fault_injection = io->fault_injection;
        io = &faulty;
    }
    if (armed || !i226_tas_read_clean(io, saved) || io->phc(io->ctx, &phc) != 0) {
        r->result = AVB_TAS_ACT_NOT_CLEAN;
        return AVB_TAS_ACTIVATE_RAN;
    }
    if (r->base_time_ns < phc + AVB_TAS_MIN_LEAD_NS) {
        r->result = AVB_TAS_ACT_INVALID;
        return AVB_TAS_ACTIVATE_RAN;
    }
    if (i226_tas_write_schedule(io, r, saved[0].old_value, tsn_mode_written) != 0) {
        r->result = i226_tas_restore(io, saved);
        return AVB_TAS_ACTIVATE_RAN;
    }
    /* The schedule is written either way. A failed read only removes the timing evidence, and never triggers a rollback */
    if (io->phc(io->ctx, &after) != 0) {
        r->result = AVB_TAS_ACT_COMMITTED_UNTIMED;
        return AVB_TAS_ACTIVATE_RAN;
    }
    r->phc_at_commit_ns = after;
    if (after >= r->base_time_ns) {
        r->result = AVB_TAS_ACT_BASE_PASSED;
    }
    return AVB_TAS_ACTIVATE_RAN;
}

/* ===================================================================================================================
 * Versioned HOLD activation (#357 producer; IOCTL_AVB_TAS_ACTIVATE 0x80C, AVB_TAS_OP_REQUEST; contract #365 13.3-13.5 and
 * its 2026-11-19 resolution R4/R7/R8, D-D 2026-11-20, D-A 2026-11-21). The generic handler has done R7 steps 1-5 (header,
 * pre_activation, claim, metadata, link speed); this is the device-owned part: steps 6-9, the D-8 writes with the R8 PHC
 * recheck, the commit, the post-commit classification, the read-back, and the numbered-write fault injection of 13.3.
 * The sequence above (i226_tas_activate, the withdrawn 80-byte request) stays for its offline cases only.
 * =================================================================================================================== */

/* The I226's HOLD tier (#351 R2/R4, D-A 2026-11-21): implemented without qualifying evidence. One source for the query's
 * limits.tier_hold and for R7 step 8, so the two never differ */
static __inline avb_u32 i226_tas_tier_hold(void)
{
    return AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED;
}

/* TAS-DR-07: BaseT seconds fit the 32-bit BASET_H register (the SSOT defines its offset only) */
#define I226_TAS_BASET_H_MAX ((avb_u64)INTEL_MASK_32BIT)

typedef AVB_TAS_OP_OUTCOME I226_TAS_OP_OUTCOME;   /* src/avb_tas_activate_state.h */

/* TAS-DR-05: the window is above 1.1 x the transmit time of max(512 bytes, max_frame_bytes) at this link speed (DOC 8.12.12), widened
 * arithmetic. One rule for the validation and for the query's LINK_SPEED_CHANGED (DES 4.3, #350). Expects start < end */
static __inline int i226_tas_window_fits(const AVB_TAS_QUEUE_WINDOW *w, avb_u32 link_mbps)
{
    avb_u32 floor_bytes = w->max_frame_bytes > I226_TAS_WINDOW_FLOOR ? w->max_frame_bytes : I226_TAS_WINDOW_FLOOR;

    return (avb_u64)(w->end_ns - w->start_ns) * (avb_u64)link_mbps > (avb_u64)floor_bytes * I226_TAS_WINDOW_FACTOR;
}

/* Step 6, TAS-DR-03 to TAS-DR-07 in numeric order (TAS-DR-08's PHC margin is step 7, TAS-DR-09's Strict flags step 8), only
 * device and SSOT ranges, at the driver-owned link speed sampled once under the claim (#351 R2). Reads no hardware */
static __inline avb_u32 i226_tas_op_validate(const AVB_TAS_SCHEDULE *s, avb_u32 link_mbps)
{
    avb_u32 q;

    /* TAS-DR-03: one window per queue, no gate list: the layout carries exactly that */
    /* TAS-DR-04: cycle within QBVCYCLET.QBV_CYCLE_TIME (SSOT field), 64-bit arithmetic */
    if (s->cycle_ns == 0 || (avb_u64)s->cycle_ns > (avb_u64)(I226_QBVCYCLET_QBV_CYCLE_TIME_MASK >> I226_QBVCYCLET_QBV_CYCLE_TIME_SHIFT)) {
        return AVB_TAS_ACT_INVALID;
    }
    /* TAS-DR-05: frame size within the device range, start < end <= cycle, window above 1.1 x the transmit time of
     * max(512 bytes, max_frame_bytes) at the current link speed (DOC 8.12.12), widened arithmetic */
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        const AVB_TAS_QUEUE_WINDOW *w = &s->queue[q];

        if (w->max_frame_bytes < I226_TAS_MIN_FRAME_BYTES || w->max_frame_bytes > I226_TAS_MAX_FRAME_BYTES) {
            return AVB_TAS_ACT_INVALID;
        }
        if (w->start_ns >= w->end_ns || w->end_ns > s->cycle_ns) {
            return AVB_TAS_ACT_INVALID;
        }
        if (!i226_tas_window_fits(w, link_mbps)) {
            return AVB_TAS_ACT_INVALID;
        }
    }
    /* TAS-DR-06: end < cycle with a gap (OPEN_ALL is REPLACE only and refused in the header checks) */
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        if (s->queue[q].end_ns >= s->cycle_ns) {
            return AVB_TAS_ACT_INVALID;
        }
    }
    /* TAS-DR-07: BaseT seconds fit BASET_H; the nanoseconds part (< 10^9) fits the 30-bit BASET_L field; base + n x cycle
     * does not overflow 64 bits */
    if (s->base_time_ns / NSEC_PER_SEC > I226_TAS_BASET_H_MAX ||
        (avb_u64)(s->base_time_ns % NSEC_PER_SEC) > (avb_u64)(I226_BASET_L_BASE_TIME_L_MASK >> I226_BASET_L_BASE_TIME_L_SHIFT) ||
        s->base_time_ns > ~(avb_u64)0 - (avb_u64)s->cycle_ns) {
        return AVB_TAS_ACT_INVALID;
    }
    /* GET_TAS_STATE must be able to report what is written: more intervals than gate-list entries is refused before any
     * write (carried from the former request, avb_tas_gcl_from_schedule) */
    {
        struct tsn_tas_config gcl;
        if (!avb_tas_gcl_from_schedule(s, &gcl)) {
            return AVB_TAS_ACT_INVALID;
        }
    }
    return AVB_TAS_ACT_COMMITTED;
}

/* Step 9: readable clean hardware. 0 clean, 1 not clean (schedule state present), 2 a read failed. Fills saved[] in the
 * order of i226_tas_read_clean (TQAVCTRL, STQT/ENDQT per queue, QBVCYCLET_S, QBVCYCLET, BASET_H, BASET_L) */
static __inline int i226_tas_op_read_clean(const I226_TAS_IO *io, I226_TAS_SAVED *saved)
{
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    avb_u32 i = 0, q;
    int dirty = 0;

    saved[i++].offset = I226_TQAVCTRL;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        saved[i++].offset = start[q];
        saved[i++].offset = end[q];
    }
    saved[i++].offset = I226_QBVCYCLET_S;
    saved[i++].offset = I226_QBVCYCLET;
    saved[i++].offset = I226_BASET_H;
    saved[i++].offset = I226_BASET_L;
    for (i = 0; i < I226_TAS_SAVED_COUNT; i++) {
        if (io->read(io->ctx, saved[i].offset, &saved[i].old_value) != 0) {
            return 2;
        }
        if (saved[i].offset != I226_QBVCYCLET && saved[i].offset != I226_QBVCYCLET_S && saved[i].old_value != 0) {
            dirty = 1;
        }
    }
    return dirty;
}

/* The numbered-write injection of #365 13.3 (Debug drivers only; the handler refuses a non-zero debug_fault on Release):
 * bits 0-7 N fail schedule write N (1 = the first), bit 8 also fails the first restore write. An injected failure does not
 * reach the hardware. Every other access passes through */
typedef struct {
    const I226_TAS_IO *inner;
    avb_u32 fail_write;       /* N, 0 = none */
    int fail_restore;         /* fail the first restore write */
    avb_u32 writes;           /* schedule writes attempted so far */
    int restoring;            /* writes now are restore writes */
    avb_u32 restore_writes;   /* restore writes attempted so far */
} I226_TAS_OP_FAULT;

static __inline int i226_tas_op_write(I226_TAS_OP_FAULT *f, avb_u32 offset, avb_u32 value)
{
    if (f->restoring) {
        f->restore_writes++;
        if (f->fail_restore && f->restore_writes == 1u) {
            return -1;
        }
    } else {
        f->writes++;
        if (f->fail_write != 0 && f->writes == f->fail_write) {
            return -1;
        }
    }
    return f->inner->write(f->inner->ctx, offset, value);
}

/* Pre-commit rollback (R8): the registers whose write was attempted, last first, back to the values read in step 9, each
 * read back. BASET_L, the commit register, is never written here: writing it would commit. When its own write was attempted
 * and failed, it is only read back. FAILED_RESTORED when every one reads back its old value, otherwise FAILED_NOT_RESTORED */
static __inline avb_u32 i226_tas_op_rollback(I226_TAS_OP_FAULT *f, const I226_TAS_SAVED *saved, avb_u32 attempted)
{
    avb_u32 i = attempted, ok = 1;

    f->restoring = 1;
    while (i-- != 0) {
        avb_u32 now = 0;

        if (saved[i].offset != I226_BASET_L && i226_tas_op_write(f, saved[i].offset, saved[i].old_value) != 0) {
            ok = 0;
            continue;
        }
        if (f->inner->read(f->inner->ctx, saved[i].offset, &now) != 0 || now != saved[i].old_value) {
            ok = 0;
        }
    }
    return ok ? AVB_TAS_ACT_FAILED_RESTORED : AVB_TAS_ACT_FAILED_NOT_RESTORED;
}

/* After the commit (R8): every register written, read back and compared on the fields written (SSOT masks), never on
 * unrelated or reserved bits. TQAVCTRL: the transmit-mode and enhanced-QAV bits set. 0 when all match */
static __inline int i226_tas_op_readback(const I226_TAS_IO *io, const AVB_TAS_SCHEDULE *s)
{
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    const avb_u32 mode_bits = (avb_u32)I226_TQAVCTRL_TRANSMIT_MODE_MASK | (avb_u32)I226_TQAVCTRL_ENHANCED_QAV_MASK;
    avb_u32 v = 0, q;

    if (io->read(io->ctx, I226_TQAVCTRL, &v) != 0 || (v & mode_bits) != mode_bits) {
        return 1;
    }
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        if (io->read(io->ctx, start[q], &v) != 0 ||
            (v & (avb_u32)I226_STQT_START_TIME_MASK) != I226_TAS_FIELD(I226_STQT, START_TIME, s->queue[q].start_ns)) {
            return 1;
        }
        if (io->read(io->ctx, end[q], &v) != 0 ||
            (v & (avb_u32)I226_ENDQT_END_TIME_MASK) != I226_TAS_FIELD(I226_ENDQT, END_TIME, s->queue[q].end_ns)) {
            return 1;
        }
    }
    if (io->read(io->ctx, I226_QBVCYCLET_S, &v) != 0 ||
        (v & (avb_u32)I226_QBVCYCLET_S_SHADOW_QBVCYCLE_MASK) != I226_TAS_FIELD(I226_QBVCYCLET_S, SHADOW_QBVCYCLE, s->cycle_ns)) {
        return 1;
    }
    if (io->read(io->ctx, I226_QBVCYCLET, &v) != 0 ||
        (v & (avb_u32)I226_QBVCYCLET_QBV_CYCLE_TIME_MASK) != I226_TAS_FIELD(I226_QBVCYCLET, QBV_CYCLE_TIME, s->cycle_ns)) {
        return 1;
    }
    if (io->read(io->ctx, I226_BASET_H, &v) != 0 || v != (avb_u32)(s->base_time_ns / NSEC_PER_SEC)) {
        return 1;
    }
    if (io->read(io->ctx, I226_BASET_L, &v) != 0 ||
        (v & (avb_u32)I226_BASET_L_BASE_TIME_L_MASK) != I226_TAS_FIELD(I226_BASET_L, BASE_TIME_L, (avb_u32)(s->base_time_ns % NSEC_PER_SEC))) {
        return 1;
    }
    return 0;
}

/*
 * R7 steps 6-9 and the operation, under the claim and the TAS register lock. armed: the driver record is not CLEAN.
 * flags: the request's flags (AVB_TAS_F_ACCEPT_UNVERIFIED). debug_fault: already validated by the header checks (Release: 0).
 * min_margin_ns: the published enforced lead (AVB_TAS_MIN_LEAD_NS, NOT_ESTABLISHED). Never logs (called under the lock).
 */
static __inline void i226_tas_op_hold(const I226_TAS_IO *io, const AVB_TAS_SCHEDULE *s, avb_u32 link_mbps, avb_u32 flags,
                                      avb_u32 debug_fault, avb_u64 min_margin_ns, int armed, I226_TAS_OP_OUTCOME *out)
{
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    I226_TAS_SAVED saved[I226_TAS_SAVED_COUNT];
    I226_TAS_OP_FAULT f;
    avb_u64 phc = 0, recheck = 0, after = 0;
    avb_u32 mode, q, attempted = 0, strict = 0;
    int clean;

    RtlZeroMemory(out, sizeof(*out));
    f.inner = io;
    f.fail_write = debug_fault & AVB_TAS_DEBUG_FAULT_WRITE_MASK;
    f.fail_restore = (debug_fault & AVB_TAS_DEBUG_FAULT_RESTORE) != 0;
    f.writes = 0;
    f.restoring = 0;
    f.restore_writes = 0;

    /* Step 6 */
    out->result = i226_tas_op_validate(s, link_mbps);
    if (out->result != AVB_TAS_ACT_COMMITTED) {
        return;
    }
    /* Step 7: PHC read and the published minimum BaseT margin (TAS-DR-08; 100 ms enforced, NOT_ESTABLISHED, D.3) */
    if (io->phc(io->ctx, &phc) != 0) {
        out->result = AVB_TAS_ACT_READ_FAILED;
        return;
    }
    out->phc_before_ns = phc;
    if (s->base_time_ns < phc || s->base_time_ns - phc < min_margin_ns) {
        out->result = AVB_TAS_ACT_INVALID;
        return;
    }
    /* Step 8: feature and tier policy. Strict (TAS-DR-09) is not implemented (D-D: NOT_IMPLEMENTED, acceptance does not
     * enable it); the HOLD tier is IMPLEMENTED_UNVERIFIED, so it executes only under explicit acceptance (#365 R4) */
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        strict |= s->queue[q].flags & (AVB_TAS_Q_STRICT_END | AVB_TAS_Q_STRICT_CYCLE);
    }
    if (strict != 0) {
        out->result = AVB_TAS_ACT_NOT_IMPLEMENTED;
        return;
    }
    if (i226_tas_tier_hold() != AVB_TAS_TIER_IMPLEMENTED_VERIFIED && (flags & AVB_TAS_F_ACCEPT_UNVERIFIED) == 0) {
        out->result = AVB_TAS_ACT_NOT_HARDWARE_VERIFIED;
        return;
    }
    /* Step 9: the record and readable clean hardware */
    if (armed) {
        out->result = AVB_TAS_ACT_NOT_CLEAN;
        return;
    }
    clean = i226_tas_op_read_clean(io, saved);
    if (clean == 2) {
        out->result = AVB_TAS_ACT_READ_FAILED;
        return;
    }
    if (clean != 0) {
        out->result = AVB_TAS_ACT_NOT_CLEAN;
        return;
    }

    /* The D-8 writes (#365 13.5): TQAVCTRL (transmit mode, enhanced QAV, other bits preserved), STQTq then ENDQTq, QBVCYCLET_S,
     * QBVCYCLET; the R8 PHC recheck; BASET_H; BASET_L (commit). attempted counts the registers whose write was attempted, in
     * saved[] order, which is this write order */
    out->write_attempted = 1;
    mode = saved[0].old_value | (avb_u32)I226_TQAVCTRL_TRANSMIT_MODE_MASK | (avb_u32)I226_TQAVCTRL_ENHANCED_QAV_MASK;
    attempted = 1;
    if (i226_tas_op_write(&f, I226_TQAVCTRL, mode) != 0) {
        goto rollback;
    }
    out->mode_written = 1;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        attempted++;
        if (i226_tas_op_write(&f, start[q], I226_TAS_FIELD(I226_STQT, START_TIME, s->queue[q].start_ns)) != 0) {
            goto rollback;
        }
        attempted++;
        if (i226_tas_op_write(&f, end[q], I226_TAS_FIELD(I226_ENDQT, END_TIME, s->queue[q].end_ns)) != 0) {
            goto rollback;
        }
    }
    attempted++;
    if (i226_tas_op_write(&f, I226_QBVCYCLET_S, I226_TAS_FIELD(I226_QBVCYCLET_S, SHADOW_QBVCYCLE, s->cycle_ns)) != 0) {
        goto rollback;
    }
    attempted++;
    if (i226_tas_op_write(&f, I226_QBVCYCLET, I226_TAS_FIELD(I226_QBVCYCLET, QBV_CYCLE_TIME, s->cycle_ns)) != 0) {
        goto rollback;
    }
    /* R8: PHC re-read immediately before the BaseT commit writes; BaseT must still satisfy the margin, else no commit */
    if (io->phc(io->ctx, &recheck) != 0 || s->base_time_ns < recheck || s->base_time_ns - recheck < min_margin_ns) {
        out->result = i226_tas_op_rollback(&f, saved, attempted);   /* no write failed: failed_write stays 0 */
        return;
    }
    out->phc_recheck_ns = recheck;   /* t0 of the commit bracket (#350) */
    attempted++;
    if (i226_tas_op_write(&f, I226_BASET_H, (avb_u32)(s->base_time_ns / NSEC_PER_SEC)) != 0) {
        goto rollback;
    }
    attempted++;
    if (i226_tas_op_write(&f, I226_BASET_L, I226_TAS_FIELD(I226_BASET_L, BASE_TIME_L, (avb_u32)(s->base_time_ns % NSEC_PER_SEC))) != 0) {
        goto rollback;
    }

    /* Committed. R5: the post-commit PHC classifies the commit; a missing or late reading never rolls a schedule back */
    out->acceptance_acted = (flags & AVB_TAS_F_ACCEPT_UNVERIFIED) != 0 && i226_tas_tier_hold() == AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED;
    if (io->phc(io->ctx, &after) != 0) {
        out->result = AVB_TAS_ACT_COMMITTED_UNTIMED;
    } else {
        out->phc_at_commit_ns = after;
        out->result = after >= s->base_time_ns ? AVB_TAS_ACT_BASE_PASSED : AVB_TAS_ACT_COMMITTED;
    }
    /* R8 read-back after the commit and its classification: a failed read or a mismatch overrides it, with no second commit */
    if (i226_tas_op_readback(io, s) != 0) {
        out->result = AVB_TAS_ACT_FAILED_NOT_RESTORED;
    }
    return;

rollback:
    out->failed_write = f.writes;
    out->result = i226_tas_op_rollback(&f, saved, attempted);
}
