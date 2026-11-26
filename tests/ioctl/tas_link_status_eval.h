/* tas_link_status_eval.h - the query's answer after a link change (TC-TASACT-META-007) or a Pause (TC-TASACT-META-004) under a retained
 * schedule (#350, #362, #365 K8 events). Pure: no I/O.
 *
 * Used by tests/hardware/test_tas_activate.c (--metadata-events, META-007 and META-004) and checked offline by
 * tests/unit/test_tas_restore_contract.c (TC-MOCK-078, TC-MOCK-080). The offline check is test logic only, never a link event or a Pause.
 *
 * Published rules (#362 ADR-TSN-002 lifecycle table; #365 12.4, 12.10):
 *   - after a link change in the same context the record is kept and the driver reports unknown until re-established; a query never
 *     re-establishes. In v1: snapshot reason EVENT_UNRECONCILED, S1, both slots UNKNOWN, slot reasons 0, before and after BaseT;
 *   - S1 comes before S2 to S4: PENDING, INCONSISTENT or UNKNOWN with LINK_EVENTS_UNOBSERVED alone is a wrong answer after an attributed event;
 *   - without an attributed event (the link change, one context, no PHC reset) nothing is judged: INCONCLUSIVE, never a manufactured RED;
 *   - a reply that is not valid is a missing measurement, never agreement.
 */
#ifndef TAS_LINK_STATUS_EVAL_H
#define TAS_LINK_STATUS_EVAL_H

#include <windows.h>

typedef enum { TAS_EV_PASS = 0, TAS_EV_FAIL, TAS_EV_INCONCLUSIVE } TAS_EV_OUT;

/** One reply after an attributed link change */
static __inline TAS_EV_OUT tas_ev_unreconciled(BOOL valid, const AVB_TAS_QUERY_HW_REQUEST *q)
{
    if (!valid) {
        return TAS_EV_INCONCLUSIVE;
    }
    return (q->snapshot_reasons & AVB_TAS_SNAP_RSN_EVENT_UNRECONCILED) != 0 && q->current_status == AVB_TAS_ST_UNKNOWN &&
           q->next_status == AVB_TAS_ST_UNKNOWN && q->current_reasons == 0 && q->next_reasons == 0 ? TAS_EV_PASS : TAS_EV_FAIL;
}

/** The positive control before the link change: this commit PENDING without reasons, next NONE, no snapshot reason */
static __inline TAS_EV_OUT tas_ev_control(BOOL valid, const AVB_TAS_QUERY_HW_REQUEST *q)
{
    if (!valid) {
        return TAS_EV_INCONCLUSIVE;
    }
    return q->current_status == AVB_TAS_ST_PENDING && q->current_reasons == 0 && q->next_status == AVB_TAS_ST_NONE && q->next_reasons == 0 &&
           q->snapshot_reasons == 0 && q->current_commit.state == AVB_TAS_COMMIT_RETAINED ? TAS_EV_PASS : TAS_EV_FAIL;
}

/** For the evidence lines: which answer a reply is */
static __inline const char *tas_ev_class(const AVB_TAS_QUERY_HW_REQUEST *q)
{
    if ((q->snapshot_reasons & AVB_TAS_SNAP_RSN_EVENT_UNRECONCILED) != 0) {
        return "EVENT_UNRECONCILED";
    }
    if (q->current_status == AVB_TAS_ST_PENDING) {
        return "PENDING";
    }
    if (q->current_status == AVB_TAS_ST_UNKNOWN && (q->current_reasons & AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED) != 0) {
        return "LINK_EVENTS_UNOBSERVED";
    }
    return "other";
}

typedef struct { unsigned pass, fail, missing; } TAS_EV_COUNT;

static __inline void tas_ev_count(TAS_EV_COUNT *c, TAS_EV_OUT o)
{
    if (o == TAS_EV_PASS) { c->pass++; } else if (o == TAS_EV_FAIL) { c->fail++; } else { c->missing++; }
}

/** A phase (before or past BaseT). attributed: the link change was observed, one context, no PHC reset */
static __inline TAS_EV_OUT tas_ev_phase(const TAS_EV_COUNT *c, BOOL attributed)
{
    if (!attributed) {
        return TAS_EV_INCONCLUSIVE;
    }
    if (c->fail != 0) {
        return TAS_EV_FAIL;
    }
    return c->pass == 0 || c->missing != 0 ? TAS_EV_INCONCLUSIVE : TAS_EV_PASS;
}

/* ---- TC-TASACT-META-004: the Pause class (#365 decision 2026-11-26 B, lifecycle row: FilterPause counts, whatever its reason; the
 * return of FilterRestart ends nothing; C: the association ends only with a removed record or a new context) ---- */

/** The positive control before a Pause, taken before or past BaseT: this commit RETAINED, no snapshot reason, next NONE, and the slot
 *  PENDING without reasons or, past BaseT, UNKNOWN with LINK_EVENTS_UNOBSERVED alone (#365 delivery boundary: no coverage) */
static __inline TAS_EV_OUT tas_ev_control_retained(BOOL valid, const AVB_TAS_QUERY_HW_REQUEST *q)
{
    if (!valid) {
        return TAS_EV_INCONCLUSIVE;
    }
    if (q->snapshot_reasons != 0 || q->current_commit.state != AVB_TAS_COMMIT_RETAINED || q->next_status != AVB_TAS_ST_NONE ||
        q->next_reasons != 0) {
        return TAS_EV_FAIL;
    }
    return (q->current_status == AVB_TAS_ST_PENDING && q->current_reasons == 0) ||
           (q->current_status == AVB_TAS_ST_UNKNOWN && q->current_reasons == AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED) ? TAS_EV_PASS : TAS_EV_FAIL;
}

/** The replies after one Pause of this context (counted by tas_ev_unreconciled). paused: after a positive control, FilterPause entered once,
 *  its Restart completed, one context. status_quiet: FilterStatus was not entered on this context from before the control to after the
 *  last reply, so the link class, the only other counted notification, cannot have produced the answer. A reply without the answer fails
 *  whatever else was counted: then neither notification was counted. The answer is attributed to the Pause only with status_quiet;
 *  otherwise it is INCONCLUSIVE, never a Pause GREEN */
static __inline TAS_EV_OUT tas_ev_pause_phase(const TAS_EV_COUNT *c, BOOL paused, BOOL status_quiet)
{
    if (!paused) {
        return TAS_EV_INCONCLUSIVE;
    }
    if (c->fail != 0) {
        return TAS_EV_FAIL;
    }
    return c->pass == 0 || c->missing != 0 || !status_quiet ? TAS_EV_INCONCLUSIVE : TAS_EV_PASS;
}

/** The record after the same event, judged apart from the status (#352 R1 L10, #365 12.2): the bring-up reset the PHC (reset: the restart
 *  completed and the reset measured by the caller), so the commit no longer claims RETAINED. It keeps the committed schedule and the
 *  commit's own bracket in the commit's context; PHC_DISCONTINUOUS, or HW_MISMATCH (no published priority between the two). before: the
 *  control, which must show this commit RETAINED in its context */
static __inline TAS_EV_OUT tas_ev_record_reset(BOOL before_valid, const AVB_TAS_QUERY_HW_REQUEST *before, BOOL valid,
                                               const AVB_TAS_QUERY_HW_REQUEST *q, const AVB_TAS_OP_REQUEST *op, BOOL reset)
{
    const AVB_TAS_COMMIT_INFO *b = &before->current_commit, *i = &q->current_commit;

    if (!reset || !before_valid || !valid || op->context_epoch == 0 || before->context_epoch != op->context_epoch ||
        b->state != AVB_TAS_COMMIT_RETAINED || b->phc_after_commit_ns == 0) {
        return TAS_EV_INCONCLUSIVE;
    }
    return q->context_epoch == op->context_epoch &&
           (i->state == AVB_TAS_COMMIT_PHC_DISCONTINUOUS || i->state == AVB_TAS_COMMIT_HW_MISMATCH) &&
           memcmp(&q->current, &op->schedule, sizeof(op->schedule)) == 0 && i->phc_before_commit_ns == b->phc_before_commit_ns &&
           i->phc_after_commit_ns == b->phc_after_commit_ns && q->next_commit.state == AVB_TAS_COMMIT_NONE ? TAS_EV_PASS : TAS_EV_FAIL;
}

#endif /* TAS_LINK_STATUS_EVAL_H */
