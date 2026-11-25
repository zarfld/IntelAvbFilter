/*
 * i226_tas_query.h - hardware part of IOCTL_AVB_TAS_QUERY_HW v1 on the I226 (#350; contract #365 "Query v1 contract").
 *
 * Read-only: only io->read and io->phc are called, never io->write. Every offset, field mask, scale and observed baseline
 * comes from intel-ethernet-regs/gen/i226_regs.h (SSOT); this file contains no register constant of its own.
 *
 * One bounded pass (#365 12.3, decision 3A): PHC before, the v1 register set in AVB_TAS_REGS bit order with BASET read
 * H, L, H (one repeat when the two H reads differ; still different: BASET_TORN), PHC after. A failed read leaves the value 0
 * and sets its read_failed bit; nothing is filled from the driver record. Without a Running filter, without MMIO access
 * or without the TAS register lock (contended: a TAS operation holds it or waits for it) nothing is read at all.
 *
 * Status (#365 12.4): S1 unobservable inputs give UNKNOWN. The driver record is the interim three-valued state of #362
 * decision 5 (CLEAN, ARMED, DIRTY), with the schedule and commit bracket the last commit left (C6, src/avb_tas_retained.h).
 * Under a record: i226_tas_query_record (R1, R2, S2 to S4). What cannot be established stays UNKNOWN. Without a record: R3
 * (TSN on, windows off their baseline or shadow non-zero) gives current INCONSISTENT HW_ARMED_NO_RECORD and next UNKNOWN
 * SLOT_UNATTRIBUTED; R4 gives NONE and NONE. BaseT is reported only; it is used for BASE_FAR_AHEAD in R3 and for
 * BASET_TORN, never compared with a target. HARDWARE_VERIFIED is never set (#381), INFERRED_ACTIVE not while K8 is unverified.
 *
 * Restoration (#365 12.5) is advisory, from the SSOT observed baselines only; ENDQT accepts its _ALT value only as the
 * DES-C-TAS-010 3.6 window alternative. DTXMXPKTSZ is read and decoded with its mask and _SCALE, never substituted.
 */
#pragma once
#include "../src/avb_tas_retained.h"
#include "i226_tas_activate.h"   /* I226_TAS_IO, include/avb_ioctl.h, intel-ethernet-regs/gen/i226_regs.h */

#define I226_TAS_QUERY_REGS      18u                 /* AVB_TAS_REGS read_failed bits 0 to 17 */
#define I226_TAS_QUERY_ALL_REGS  ((1u << I226_TAS_QUERY_REGS) - 1u)
#define I226_TAS_QUERY_SCHED     ((1u << 17) - 1u)   /* bits 0 to 16: every register but DTXMXPKTSZ, the status inputs */
#define I226_TAS_QUERY_BIT_DTX   (1u << 17)

#define I226_TAS_Q_FIELD(REG, FIELD, value) ((avb_u32)REG##_GET((value), REG##_##FIELD##_MASK, REG##_##FIELD##_SHIFT))

/* Reads one register into *slot; on failure *slot is 0 and its read_failed bit is set */
static __inline void i226_tas_query_read(const I226_TAS_IO *io, avb_u32 offset, avb_u32 bit, avb_u32 *slot, avb_u32 *failed)
{
    avb_u32 v = 0;
    if (io->read(io->ctx, offset, &v) != 0) {
        *slot = 0;
        *failed |= (1u << bit);
    } else {
        *slot = v;
    }
}

/** Windows at the family baseline on every queue: STQT at its baseline, ENDQT at its baseline or, only as the window
 *  alternative of DES-C-TAS-010 3.6, at _ALT */
static __inline int i226_tas_query_windows_baseline(const AVB_TAS_REGS *g)
{
    unsigned q;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        if (g->stqt[q] != I226_STQT_BASELINE || (g->endqt[q] != I226_ENDQT_BASELINE && g->endqt[q] != I226_ENDQT_BASELINE_ALT)) {
            return 0;
        }
    }
    return 1;
}

#define I226_TAS_QUERY_INCONSISTENT (AVB_TAS_SLOT_RSN_HW_ARMED_NO_RECORD | AVB_TAS_SLOT_RSN_DRIVER_ARMED_HW_OFF | \
        AVB_TAS_SLOT_RSN_BASE_FAR_AHEAD | AVB_TAS_SLOT_RSN_WINDOW_MISMATCH | AVB_TAS_SLOT_RSN_CYCLE_MISMATCH | \
        AVB_TAS_SLOT_RSN_TXQCTL_MISMATCH | AVB_TAS_SLOT_RSN_LINK_SPEED_CHANGED | AVB_TAS_SLOT_RSN_PHC_RESET_SINCE_COMMIT)

/* The fields compared under a record (#365 12.4): windows and cycle, through the SSOT masks. Never BASET, the shadow cycle or TXQCTL */
static __inline avb_u32 i226_tas_query_mismatch(const AVB_TAS_REGS *g, const AVB_TAS_SCHEDULE *s)
{
    avb_u32 reasons = 0;
    unsigned q;

    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        if (I226_TAS_Q_FIELD(I226_STQT, START_TIME, g->stqt[q]) != s->queue[q].start_ns ||
            I226_TAS_Q_FIELD(I226_ENDQT, END_TIME, g->endqt[q]) != s->queue[q].end_ns) {
            reasons |= AVB_TAS_SLOT_RSN_WINDOW_MISMATCH;
        }
    }
    if (I226_TAS_Q_FIELD(I226_QBVCYCLET, QBV_CYCLE_TIME, g->qbvcyclet) != s->cycle_ns) {
        reasons |= AVB_TAS_SLOT_RSN_CYCLE_MISMATCH;
    }
    return reasons;
}

/*
 * The slots and the commit information under a record (ARMED or DIRTY). retained: one published state with the record, NULL when the caller
 * has no coherent one. Precedence #365 12.4 S1 to S4, every applicable reason bit; nothing is written (the query is an observer).
 * - A dirty record, or one without a retained schedule (SETUP_TAS on a clean adapter), has no expected windows: UNKNOWN COMMIT_INFO_LOST.
 * - PHC_RESET_SINCE_COMMIT: a PHC write issued since the commit (either direction), or this pass read the PHC below the commit's own
 *   reading or moving backward. The target is then not compared with this PHC.
 * - Past the target: UNKNOWN LINK_EVENTS_UNOBSERVED, never INFERRED_ACTIVE while K8 is unverified. HARDWARE_VERIFIED is never set (#381).
 */
static __inline void i226_tas_query_record(AVB_TAS_QUERY_HW_REQUEST *q, avb_u32 record, const AVB_TAS_RETAINED *retained, int tsn, int s1)
{
    const int schedule = retained != NULL && record == AVB_TAS_STATE_ARMED && (retained->flags & AVB_TAS_RET_SCHEDULE) != 0;
    const int timed = schedule && avb_tas_retained_timed(retained);
    int reset = schedule && (retained->flags & AVB_TAS_RET_DISCONTINUOUS) != 0;
    avb_u32 mismatch = 0, reasons, k;

    if (!s1) {
        if (record == AVB_TAS_STATE_ARMED && !tsn) {
            mismatch |= AVB_TAS_SLOT_RSN_DRIVER_ARMED_HW_OFF;
        }
        if (schedule) {
            mismatch |= i226_tas_query_mismatch(&q->regs, &retained->schedule);
            reset = reset || q->phc_after_ns < q->phc_before_ns;
        }
        if (timed) {
            reset = reset || q->phc_before_ns < retained->phc_after_commit_ns || q->phc_after_ns < retained->phc_after_commit_ns;
        }
    }
    if (!timed) {
        q->current_commit.state = AVB_TAS_COMMIT_LOST;   /* bracket and schedule stay 0 (12.2) */
    } else {
        q->current_commit.state = mismatch != 0 ? AVB_TAS_COMMIT_HW_MISMATCH :
                                  (reset ? AVB_TAS_COMMIT_PHC_DISCONTINUOUS : AVB_TAS_COMMIT_RETAINED);
        q->current_commit.phc_before_commit_ns = retained->phc_before_commit_ns;
        q->current_commit.phc_after_commit_ns = retained->phc_after_commit_ns;
        q->current = retained->schedule;
    }
    if (s1) {
        q->current_status = AVB_TAS_ST_UNKNOWN;
        q->next_status = AVB_TAS_ST_UNKNOWN;
        return;
    }
    reasons = mismatch | (reset ? AVB_TAS_SLOT_RSN_PHC_RESET_SINCE_COMMIT : 0u);
    if (schedule && q->link_speed_mbps != 0) {   /* 4.3; an unknown speed contradicts nothing */
        for (k = 0; k < AVB_TAS_QUEUES; k++) {
            if (!i226_tas_window_fits(&retained->schedule.queue[k], q->link_speed_mbps)) {
                reasons |= AVB_TAS_SLOT_RSN_LINK_SPEED_CHANGED;
            }
        }
    }
    if (!timed) {
        reasons |= AVB_TAS_SLOT_RSN_COMMIT_INFO_LOST;
    } else {
        const avb_u64 target = retained->schedule.base_time_ns;

        if ((retained->flags & AVB_TAS_RET_IN_TIME) == 0) {
            reasons |= AVB_TAS_SLOT_RSN_ACTIVATION_AMBIGUOUS;
        }
        if (!reset && q->phc_after_ns >= target) {
            reasons |= q->phc_before_ns <= target ? AVB_TAS_SLOT_RSN_TRANSITION_DURING_SNAPSHOT : AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED;
        }
    }
    q->current_reasons = reasons;
    q->current_status = (reasons & I226_TAS_QUERY_INCONSISTENT) != 0 ? AVB_TAS_ST_INCONSISTENT :
                        (reasons != 0 ? AVB_TAS_ST_UNKNOWN : AVB_TAS_ST_PENDING);
    if (schedule && mismatch == 0) {
        q->next_status = AVB_TAS_ST_NONE;        /* R1 */
    } else {
        q->next_status = AVB_TAS_ST_UNKNOWN;     /* R2, or nothing to attribute the hardware to */
        q->next_reasons = AVB_TAS_SLOT_RSN_SLOT_UNATTRIBUTED;
    }
}

/**
 * Fills the hardware fields of q: hw_read, hw_mode, regs, phc bracket, dtx, limits, slot statuses and reasons,
 * snapshot_reasons, restoration, current_commit and next_commit. The caller has zeroed q and set version, size,
 * lookup_client_op_id, identity, flags, lookup_result and legacy_conflicts.
 * running: the filter is Running. mmio: the BAR is mapped and INTEL_CAP_MMIO is set. record: AVB_TAS_STATE_*.
 * op_in_progress: a TAS operation holds the adapter's claim (the slots are then not advanced, #362).
 * retained: the context's retained commit under the same shared hold as the record, NULL when there is no coherent one.
 * contended: the caller did not obtain the TAS register lock and never waits for it (#365 12.1 "Never blocking"; #45):
 * no register and no PHC value is read. The register-lock reply of #365 12.10 (Amendment 1): hw_read PARTIAL (incomplete
 * observation, no MMIO implied), every read_failed bit set (not attempted), PHC_BEFORE_INVALID and PHC_AFTER_INVALID,
 * READ_FAILED clear, DTXMXPKTSZ NOT_READ; with op_in_progress the slots are UNKNOWN (S1) and restoration UNKNOWN.
 * NOT_RUNNING and NO_MMIO also read nothing: both PHC values stay 0 with PHC_BEFORE_INVALID and PHC_AFTER_INVALID set,
 * READ_FAILED clear. Both PHC bits are derived on the common path from phc_before_ok and phc_after_ok (#365 12.2).
 */
static __inline void i226_tas_query(const I226_TAS_IO *io, int running, int mmio, avb_u32 record, int op_in_progress,
                                    int contended, const AVB_TAS_RETAINED *retained, AVB_TAS_QUERY_HW_REQUEST *q)
{
    AVB_TAS_REGS *g = &q->regs;
    avb_u32 failed = 0, snap = 0, h2 = 0, tries, k;
    avb_u64 phc = 0;
    int phc_before_ok = 0, phc_after_ok = 0, read_done = 0, tsn = 0, s1;

    /* Constants of v1 (#365 12.2): one window per queue; cycle limits not reported (the SSOT field carries no units); the
     * lead the driver enforces now, not established; PASS not implemented (#363); HOLD IMPLEMENTED_UNVERIFIED (D-A
     * 2026-11-21, #351 R2/R4); the other tiers not reported until the #351 table */
    q->limits.queues = AVB_TAS_QUEUES;
    q->limits.windows_per_queue = 1u;
    q->limits.min_margin_ns = AVB_TAS_MIN_LEAD_NS;
    q->limits.min_margin_state = AVB_TAS_LIMIT_NOT_ESTABLISHED;
    q->limits.tier_pass = AVB_TAS_TIER_NOT_IMPLEMENTED;
    /* HOLD is implemented without qualifying evidence (#351 R2/R4, D-A 2026-11-21): the device-owned tier that R7 step 8 of
     * the activation applies, from the same source (i226_tas_tier_hold) */
    q->limits.tier_hold = i226_tas_tier_hold();
    q->next_commit.state = AVB_TAS_COMMIT_NONE;

    if (!running) {
        q->hw_read = AVB_TAS_HWREAD_NOT_RUNNING;
        snap |= AVB_TAS_SNAP_RSN_NOT_RUNNING;
        failed = I226_TAS_QUERY_ALL_REGS;
    } else if (!mmio) {
        q->hw_read = AVB_TAS_HWREAD_NO_MMIO;
        snap |= AVB_TAS_SNAP_RSN_NO_MMIO;
        failed = I226_TAS_QUERY_ALL_REGS;
    } else if (contended) {
        q->hw_read = AVB_TAS_HWREAD_PARTIAL;
        failed = I226_TAS_QUERY_ALL_REGS;
    } else {
        read_done = 1;
        if (io->phc(io->ctx, &phc) == 0) {
            q->phc_before_ns = phc;
            phc_before_ok = 1;
        }
        i226_tas_query_read(io, I226_TQAVCTRL, 0, &g->tqavctrl, &failed);
        /* BASET: H, L, H; one repeat when the H reads differ */
        for (tries = 0; tries < 2u; tries++) {
            avb_u32 f = 0;
            i226_tas_query_read(io, I226_BASET_H, 2, &g->baset_h, &f);
            i226_tas_query_read(io, I226_BASET_L, 1, &g->baset_l, &f);
            i226_tas_query_read(io, I226_BASET_H, 2, &h2, &f);
            if (f != 0) {
                failed |= f;
                g->baset_h = (f & (1u << 2)) ? 0 : g->baset_h;
                g->baset_l = (f & (1u << 1)) ? 0 : g->baset_l;
                break;
            }
            if (h2 == g->baset_h) {
                break;
            }
            if (tries == 1u) {
                g->consistency |= AVB_TAS_CONS_BASET_TORN;
                snap |= AVB_TAS_SNAP_RSN_READ_INCONSISTENT;
            }
        }
        i226_tas_query_read(io, I226_QBVCYCLET, 3, &g->qbvcyclet, &failed);
        i226_tas_query_read(io, I226_QBVCYCLET_S, 4, &g->qbvcyclet_s, &failed);
        i226_tas_query_read(io, I226_STQT0, 5, &g->stqt[0], &failed);
        i226_tas_query_read(io, I226_STQT1, 6, &g->stqt[1], &failed);
        i226_tas_query_read(io, I226_STQT2, 7, &g->stqt[2], &failed);
        i226_tas_query_read(io, I226_STQT3, 8, &g->stqt[3], &failed);
        i226_tas_query_read(io, I226_ENDQT0, 9, &g->endqt[0], &failed);
        i226_tas_query_read(io, I226_ENDQT1, 10, &g->endqt[1], &failed);
        i226_tas_query_read(io, I226_ENDQT2, 11, &g->endqt[2], &failed);
        i226_tas_query_read(io, I226_ENDQT3, 12, &g->endqt[3], &failed);
        i226_tas_query_read(io, I226_TXQCTL0, 13, &g->txqctl[0], &failed);
        i226_tas_query_read(io, I226_TXQCTL1, 14, &g->txqctl[1], &failed);
        i226_tas_query_read(io, I226_TXQCTL2, 15, &g->txqctl[2], &failed);
        i226_tas_query_read(io, I226_TXQCTL3, 16, &g->txqctl[3], &failed);
        i226_tas_query_read(io, I226_DTXMXPKTSZ, 17, &g->dtxmxpktsz, &failed);
        if (io->phc(io->ctx, &phc) == 0) {
            q->phc_after_ns = phc;
            phc_after_ok = 1;
        }
        if (phc_before_ok && phc_after_ok && q->phc_after_ns < q->phc_before_ns) {
            snap |= AVB_TAS_SNAP_RSN_PHC_DISCONTINUITY;   /* a backward PHC move observed within this pass */
        }
        if (failed != 0)    { snap |= AVB_TAS_SNAP_RSN_READ_FAILED; }
        q->hw_read = (failed == 0 && phc_before_ok && phc_after_ok) ? AVB_TAS_HWREAD_READ : AVB_TAS_HWREAD_PARTIAL;
    }
    /* Every path, including those that read nothing: a PHC value is valid iff its INVALID bit is clear (#365 12.2) */
    if (!phc_before_ok) { snap |= AVB_TAS_SNAP_RSN_PHC_BEFORE_INVALID; }
    if (!phc_after_ok)  { snap |= AVB_TAS_SNAP_RSN_PHC_AFTER_INVALID; }
    g->read_failed = failed;
    q->snapshot_reasons = snap;

    /* hw_mode from TQAVCTRL.TRANSMIT_MODE */
    if (failed & 1u) {
        q->hw_mode = AVB_TAS_HWMODE_UNREADABLE;
    } else {
        tsn = I226_TAS_Q_FIELD(I226_TQAVCTRL, TRANSMIT_MODE, g->tqavctrl) != 0;
        q->hw_mode = tsn ? AVB_TAS_HWMODE_TSN : AVB_TAS_HWMODE_OFF;
    }

    /* DTXMXPKTSZ: read and decoded with the SSOT mask and _SCALE (#362 decision 1) */
    if (!read_done) {
        q->dtx_max_frame_state = AVB_TAS_DTX_NOT_READ;
    } else if (failed & I226_TAS_QUERY_BIT_DTX) {
        q->dtx_max_frame_state = AVB_TAS_DTX_READ_FAILED;
    } else {
        q->dtx_max_frame_bytes = I226_TAS_Q_FIELD(I226_DTXMXPKTSZ, MAX_TPKT_SIZE, g->dtxmxpktsz) * I226_DTXMXPKTSZ_MAX_TPKT_SIZE_SCALE;
        q->dtx_max_frame_state = AVB_TAS_DTX_DECODED;
    }

    /* Restoration, advisory (#365 12.5): never a proof of a clean adapter, restoration or TX */
    if ((failed & I226_TAS_QUERY_SCHED) != 0) {
        q->restoration = AVB_TAS_RESTORE_UNKNOWN;
    } else if (!tsn) {
        int at_baseline = g->tqavctrl == I226_TQAVCTRL_BASELINE && g->baset_l == I226_BASET_L_BASELINE &&
                          g->baset_h == I226_BASET_H_BASELINE && g->qbvcyclet == I226_QBVCYCLET_BASELINE &&
                          g->qbvcyclet_s == I226_QBVCYCLET_S_BASELINE && i226_tas_query_windows_baseline(g);
        for (k = 0; at_baseline && k < AVB_TAS_QUEUES; k++) {
            at_baseline = g->txqctl[k] == I226_TXQCTL_BASELINE;
        }
        q->restoration = at_baseline ? AVB_TAS_RESTORE_COMPLETE : AVB_TAS_RESTORE_PARTIAL_RESIDUE;
    } else {
        avb_u32 cycle = I226_TAS_Q_FIELD(I226_QBVCYCLET, QBV_CYCLE_TIME, g->qbvcyclet);
        int open_all = cycle != 0;
        for (k = 0; open_all && k < AVB_TAS_QUEUES; k++) {
            open_all = I226_TAS_Q_FIELD(I226_STQT, START_TIME, g->stqt[k]) == 0 &&
                       I226_TAS_Q_FIELD(I226_ENDQT, END_TIME, g->endqt[k]) >= cycle;
        }
        q->restoration = open_all ? AVB_TAS_RESTORE_PARTIAL_OPEN_ALL : AVB_TAS_RESTORE_NONE;
    }

    /* Slot statuses (#365 12.4) */
    s1 = !read_done || (failed & I226_TAS_QUERY_SCHED) != 0 || (snap & (AVB_TAS_SNAP_RSN_READ_INCONSISTENT |
         AVB_TAS_SNAP_RSN_PHC_BEFORE_INVALID | AVB_TAS_SNAP_RSN_PHC_AFTER_INVALID | AVB_TAS_SNAP_RSN_EVENT_UNRECONCILED)) != 0 ||
         op_in_progress;
    if (record != AVB_TAS_STATE_CLEAN) {
        i226_tas_query_record(q, record, retained, tsn, s1);
        return;
    }
    q->current_commit.state = AVB_TAS_COMMIT_NONE;
    if (s1) {
        q->current_status = AVB_TAS_ST_UNKNOWN;
        q->next_status = AVB_TAS_ST_UNKNOWN;
    } else {
        int windows_base = i226_tas_query_windows_baseline(g);
        int shadow_zero = g->qbvcyclet_s == I226_QBVCYCLET_S_BASELINE;
        if (tsn || !windows_base || !shadow_zero) {
            /* R3: hardware state without a record */
            avb_u32 cycle = I226_TAS_Q_FIELD(I226_QBVCYCLET, QBV_CYCLE_TIME, g->qbvcyclet);
            avb_u64 baset = (avb_u64)g->baset_h * 1000000000ULL + I226_TAS_Q_FIELD(I226_BASET_L, BASE_TIME_L, g->baset_l);
            q->current_status = AVB_TAS_ST_INCONSISTENT;
            q->current_reasons = AVB_TAS_SLOT_RSN_HW_ARMED_NO_RECORD;
            if (cycle != 0 && baset > q->phc_after_ns + 2ULL * cycle) {
                q->current_reasons |= AVB_TAS_SLOT_RSN_BASE_FAR_AHEAD;
            }
            q->next_status = AVB_TAS_ST_UNKNOWN;
            q->next_reasons = AVB_TAS_SLOT_RSN_SLOT_UNATTRIBUTED;
        } else {
            /* R4: no record, TSN off, windows at baseline, shadow 0. BaseT may be non-zero: residue, reported only */
            q->current_status = AVB_TAS_ST_NONE;
            q->next_status = AVB_TAS_ST_NONE;
        }
    }
}
