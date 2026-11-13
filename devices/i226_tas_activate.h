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
 * hardware is in: see disable_tas in intel_i226_impl.c. A failure after the mode write can therefore need a NIC cycle.
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

/* The writes up to and including the commit point. Nonzero when one failed */
static __inline int i226_tas_write_schedule(const I226_TAS_IO *io, const AVB_TAS_ACTIVATE_REQUEST *r, avb_u32 tqavctrl)
{
    static const avb_u32 start[AVB_TAS_QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
    static const avb_u32 end[AVB_TAS_QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};
    avb_u32 mode = tqavctrl | (avb_u32)I226_TQAVCTRL_TRANSMIT_MODE_MASK | (avb_u32)I226_TQAVCTRL_ENHANCED_QAV_MASK, q;
    avb_u32 base_ns = (avb_u32)(r->base_time_ns % NSEC_PER_SEC);

    if (io->write(io->ctx, I226_TQAVCTRL, mode) != 0) {
        return 1;
    }
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
 * schedule behind */
static __inline int i226_tas_activate(const I226_TAS_IO *io, AVB_TAS_ACTIVATE_REQUEST *r, int armed)
{
    I226_TAS_SAVED saved[I226_TAS_SAVED_COUNT];
    avb_u64 phc = 0, after = 0;
    I226_TAS_FAULT fault;
    I226_TAS_IO faulty;
    avb_u32 valid;

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
    if (i226_tas_write_schedule(io, r, saved[0].old_value) != 0) {
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
