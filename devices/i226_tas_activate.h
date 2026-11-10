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
 * Restoring TQAVCTRL writes the old value back and checks the readback. A readback does not prove the transmit mode the
 * hardware is in: see disable_tas in intel_i226_impl.c. A failure after the mode write can therefore need a NIC cycle.
 */
#pragma once

#include "../include/avb_ioctl.h"
#include "../intel-ethernet-regs/gen/i226_regs.h"

typedef struct {
    void *ctx;
    int (*read)(void *ctx, avb_u32 offset, avb_u32 *value);
    int (*write)(void *ctx, avb_u32 offset, avb_u32 value);
    int (*phc)(void *ctx, avb_u64 *ns);
} I226_TAS_IO;

#define I226_TAS_QBV_CYCLE_MAX    0x3FFFFFFFu   /* I226_QBVCYCLET field width */
#define I226_TAS_MIN_FRAME_BYTES  64u
#define I226_TAS_MAX_FRAME_BYTES  9728u
#define I226_TAS_WINDOW_FLOOR     512u          /* bytes: the window rule never goes below this frame size */
#define I226_TAS_WINDOW_FACTOR    8800u         /* 1.1 x 8 bits x 1000: ns x Mbit/s per byte */
#define I226_TAS_NS_PER_SEC       1000000000ULL

/* Every field of the request. Reads no hardware, so an invalid request changes nothing */
static __inline avb_u32 i226_tas_validate(const AVB_TAS_ACTIVATE_REQUEST *r)
{
    avb_u32 q, floor_bytes;

    if (r->version != AVB_TAS_ACTIVATE_VERSION || (r->pre_activation != AVB_TAS_PRE_HOLD && r->pre_activation != AVB_TAS_PRE_PASS)) {
        return AVB_TAS_ACT_INVALID;
    }
    if (r->cycle_ns == 0 || r->cycle_ns > I226_TAS_QBV_CYCLE_MAX ||
        r->base_time_ns / I226_TAS_NS_PER_SEC > 0xFFFFFFFFULL) {
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
    return AVB_TAS_ACT_COMMITTED;
}

typedef struct {
    avb_u32 offset, old_value;
} I226_TAS_SAVED;

/* What the activation may overwrite, in write order. tqavctrl comes first in saved[] */
#define I226_TAS_SAVED_COUNT (1u + 2u * AVB_TAS_QUEUES + 4u)

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
    avb_u32 base_ns = (avb_u32)(r->base_time_ns % I226_TAS_NS_PER_SEC);

    if (io->write(io->ctx, I226_TQAVCTRL, mode) != 0) {
        return 1;
    }
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        if (io->write(io->ctx, start[q], r->window_start_ns[q]) != 0 || io->write(io->ctx, end[q], r->window_end_ns[q]) != 0) {
            return 1;
        }
    }
    if (r->pre_activation == AVB_TAS_PRE_PASS && io->write(io->ctx, I226_TQAVCTRL, mode | (avb_u32)I226_TQAVCTRL_FUTSCDDIS_MASK) != 0) {
        return 1;
    }
    if (io->write(io->ctx, I226_QBVCYCLET_S, r->cycle_ns) != 0 || io->write(io->ctx, I226_QBVCYCLET, r->cycle_ns) != 0 ||
        io->write(io->ctx, I226_BASET_H, (avb_u32)(r->base_time_ns / I226_TAS_NS_PER_SEC)) != 0) {
        return 1;
    }
    if (r->pre_activation == AVB_TAS_PRE_PASS && io->write(io->ctx, I226_BASET_L, 0) != 0) {
        return 1;
    }
    return io->write(io->ctx, I226_BASET_L, base_ns) != 0;
}

/* The whole operation. r->result always says what happened, and only a committed result leaves a schedule behind */
static __inline void i226_tas_activate(const I226_TAS_IO *io, AVB_TAS_ACTIVATE_REQUEST *r, int armed)
{
    I226_TAS_SAVED saved[I226_TAS_SAVED_COUNT];
    avb_u64 phc = 0, after = 0;

    r->phc_at_commit_ns = 0;
    r->result = i226_tas_validate(r);
    if (r->result != AVB_TAS_ACT_COMMITTED) {
        return;
    }
    if (armed || !i226_tas_read_clean(io, saved) || io->phc(io->ctx, &phc) != 0) {
        r->result = AVB_TAS_ACT_NOT_CLEAN;
        return;
    }
    if (r->base_time_ns < phc + AVB_TAS_MIN_LEAD_NS) {
        r->result = AVB_TAS_ACT_INVALID;
        return;
    }
    if (i226_tas_write_schedule(io, r, saved[0].old_value) != 0) {
        r->result = i226_tas_restore(io, saved);
        return;
    }
    if (io->phc(io->ctx, &after) == 0) {
        r->phc_at_commit_ns = after;
        if (after >= r->base_time_ns) {
            r->result = AVB_TAS_ACT_BASE_PASSED;
        }
    }
}
