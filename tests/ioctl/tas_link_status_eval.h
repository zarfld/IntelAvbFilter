/* tas_link_status_eval.h - the query's answer after a link change under a retained schedule (TC-TASACT-META-007, #350, #362). Pure: no I/O.
 *
 * Used by tests/hardware/test_tas_activate.c (--metadata-events, META-007) and checked offline by tests/unit/test_tas_restore_contract.c
 * (TC-MOCK-078). The offline check is test logic only, never a link event.
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

#endif /* TAS_LINK_STATUS_EVAL_H */
