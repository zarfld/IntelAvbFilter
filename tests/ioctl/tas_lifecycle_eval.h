/*
 * tas_lifecycle_eval.h - what a FilterPause/FilterRestart run may claim from its evidence (#364).
 *
 * Pure logic, shared by tests/diagnostic/tas_gate_investigation.c and its offline cases in test_tas_restore_contract.c
 * (TC-MOCK-014 to 017). Passing those cases says nothing about hardware. Output of the tool is LOCAL_DIAGNOSTIC.
 *
 * IOCTL_FILTER_RESTART_ONE_INSTANCE returns success when no instance matched and when NdisFRestartFilter failed, so its
 * result is not evidence. FilterPauseCount and FilterRestartCount count entries only, and FilterRestartCount moves before
 * the bring-up that FilterRestart runs, so they are not completion either. NDIS restarts the drivers above a filter after
 * it, so frames sent by a protocol, carried through the stack and seen on an observer after the Restart entry are the
 * only completion evidence the driver's interfaces allow. That is an inference from documented NDIS order: NDIS's own
 * state events are the independent check.
 *
 * Verifies: #364
 */
#pragma once

#include <windows.h>
#include <wchar.h>
#include "../../include/avb_ioctl.h"
#include "tas_restoration_eval.h"

typedef enum {
    TAS_INSTANCE_FOUND = 0,
    TAS_INSTANCE_NONE,
    TAS_INSTANCE_AMBIGUOUS,   /* the driver could restart a different instance than the one meant */
    TAS_INSTANCE_MALFORMED
} TAS_INSTANCE_RESULT;

typedef enum {
    TAS_LIFECYCLE_NOT_TRIGGERED = 0,  /* no counter moved: the trigger reached nothing */
    TAS_LIFECYCLE_PAUSE_ONLY,         /* Pause entered, Restart not entered within the bound */
    TAS_LIFECYCLE_RESTART_ENTERED,    /* both entered once, completion not shown */
    TAS_LIFECYCLE_RESUMED,            /* both entered once, then traffic through the whole stack */
    TAS_LIFECYCLE_CONTEXT_REPLACED,   /* detach, attach or a counter that fell: not a pause and restart of this context */
    TAS_LIFECYCLE_OVERLAPPED          /* counters cannot be attributed to one trigger */
} TAS_LIFECYCLE_OUTCOME;

typedef enum {
    TAS_PHC_UNMEASURED = 0,
    TAS_PHC_DISCONTINUOUS,    /* did not advance: needs no tolerance to claim */
    TAS_PHC_NOT_JUDGED,       /* advanced: continuity needs a hardware-derived tolerance, and none exists */
    TAS_PHC_CONTINUOUS        /* judged against a hardware-derived tolerance. tas_phc_fact() can never return this */
} TAS_PHC_FACT;

typedef struct {
    LONGLONG pause_delta;       /* FilterPauseCount */
    LONGLONG restart_delta;     /* FilterRestartCount */
    LONGLONG generation_delta;  /* PauseRestartGeneration */
    LONGLONG detach_delta;      /* FilterDetachCount */
    LONGLONG attach_delta;      /* FilterAttachCount */
    BOOL traffic_shown;         /* frames sent after the Restart entry reached the observer */
} TAS_LIFECYCLE_EVIDENCE;

/* Whether the instance belongs to the adapter. Layout {interface GUID}-{filter GUID}-0000, see TC-MOCK-014 for its evidence */
static __inline BOOL tas_name_is_adapter(const WCHAR *name, size_t name_chars, const WCHAR *adapter_guid)
{
    size_t guid_chars = wcslen(adapter_guid);

    return name_chars > guid_chars && _wcsnicmp(name, adapter_guid, guid_chars) == 0 && name[guid_chars] == L'-';
}

/*
 * Finds the one record of IOCTL_FILTER_ENUERATE_ALL_INSTANCES that names the adapter: USHORT byte length, then the name
 * in UTF-16 without a terminator, repeated. filterFindFilterModule stops at the first instance whose name is a prefix of
 * the request, so another record that is a prefix of the result would be restarted in its place.
 */
static __inline TAS_INSTANCE_RESULT tas_instance_find(const BYTE *list, DWORD list_bytes, const WCHAR *adapter_guid,
                                                      const WCHAR **name, USHORT *name_bytes)
{
    const BYTE *found = NULL;
    USHORT found_bytes = 0;
    DWORD at;

    for (at = 0; at < list_bytes;) {
        USHORT bytes;

        if (list_bytes - at < sizeof(USHORT)) {
            return TAS_INSTANCE_MALFORMED;
        }
        memcpy(&bytes, list + at, sizeof(bytes));
        at += sizeof(USHORT);
        if ((bytes % sizeof(WCHAR)) != 0 || bytes > list_bytes - at) {
            return TAS_INSTANCE_MALFORMED;
        }
        if (tas_name_is_adapter((const WCHAR *)(list + at), bytes / sizeof(WCHAR), adapter_guid)) {
            if (found != NULL) {
                return TAS_INSTANCE_AMBIGUOUS;
            }
            found = list + at;
            found_bytes = bytes;
        }
        at += bytes;
    }
    if (found == NULL) {
        return TAS_INSTANCE_NONE;
    }
    for (at = 0; at < list_bytes;) {
        USHORT bytes;

        memcpy(&bytes, list + at, sizeof(bytes));
        at += sizeof(USHORT);
        if (list + at != found && bytes <= found_bytes && memcmp(list + at, found, bytes) == 0) {
            return TAS_INSTANCE_AMBIGUOUS;
        }
        at += bytes;
    }
    *name = (const WCHAR *)found;
    *name_bytes = found_bytes;
    return TAS_INSTANCE_FOUND;
}

static __inline TAS_LIFECYCLE_OUTCOME tas_lifecycle_outcome(const TAS_LIFECYCLE_EVIDENCE *e)
{
    if (e->detach_delta != 0 || e->attach_delta != 0 || e->pause_delta < 0 || e->restart_delta < 0 || e->generation_delta < 0) {
        return TAS_LIFECYCLE_CONTEXT_REPLACED;
    }
    if (e->pause_delta == 0 && e->restart_delta == 0 && e->generation_delta == 0) {
        return TAS_LIFECYCLE_NOT_TRIGGERED;
    }
    if (e->pause_delta != 1 || e->generation_delta != 1 || e->restart_delta > 1) {
        return TAS_LIFECYCLE_OVERLAPPED;
    }
    if (e->restart_delta == 0) {
        return TAS_LIFECYCLE_PAUSE_ONLY;
    }
    return e->traffic_shown ? TAS_LIFECYCLE_RESUMED : TAS_LIFECYCLE_RESTART_ENTERED;
}

static __inline void tas_lifecycle_evidence(const AVB_DRIVER_STATISTICS *before, const AVB_DRIVER_STATISTICS *after, BOOL traffic_shown,
                                            TAS_LIFECYCLE_EVIDENCE *e)
{
    e->pause_delta = (LONGLONG)(after->FilterPauseCount - before->FilterPauseCount);
    e->restart_delta = (LONGLONG)(after->FilterRestartCount - before->FilterRestartCount);
    e->generation_delta = (LONGLONG)(after->PauseRestartGeneration - before->PauseRestartGeneration);
    e->detach_delta = (LONGLONG)(after->FilterDetachCount - before->FilterDetachCount);
    e->attach_delta = (LONGLONG)(after->FilterAttachCount - before->FilterAttachCount);
    e->traffic_shown = traffic_shown;
}

static __inline const char *tas_lifecycle_name(TAS_LIFECYCLE_OUTCOME o)
{
    switch (o) {
    case TAS_LIFECYCLE_NOT_TRIGGERED:    return "NOT_TRIGGERED";
    case TAS_LIFECYCLE_PAUSE_ONLY:       return "PAUSE_ONLY";
    case TAS_LIFECYCLE_RESTART_ENTERED:  return "RESTART_ENTERED_COMPLETION_NOT_SHOWN";
    case TAS_LIFECYCLE_RESUMED:          return "RESUMED_BY_TRAFFIC";
    case TAS_LIFECYCLE_CONTEXT_REPLACED: return "CONTEXT_REPLACED";
    default:                             return "OVERLAPPED";
    }
}

static __inline TAS_PHC_FACT tas_phc_fact(BOOL before_ok, BOOL after_ok, ULONGLONG before, ULONGLONG after)
{
    if (!before_ok || !after_ok) {
        return TAS_PHC_UNMEASURED;
    }
    return after <= before ? TAS_PHC_DISCONTINUOUS : TAS_PHC_NOT_JUDGED;
}

static __inline const char *tas_phc_fact_name(TAS_PHC_FACT f)
{
    switch (f) {
    case TAS_PHC_DISCONTINUOUS: return "DISCONTINUOUS";
    case TAS_PHC_NOT_JUDGED:    return "NOT_JUDGED_NO_TOLERANCE";
    default:                    return "UNMEASURED";
    }
}

/* A register the family does not define is not compared. One that could not be read is unmeasured, never a difference */
static __inline void tas_snapshot_compare(const AVB_HW_SNAPSHOT_REQUEST *before, const AVB_HW_SNAPSHOT_REQUEST *after,
                                          unsigned *compared, unsigned *changed, unsigned *unreadable)
{
    unsigned i;

    *compared = *changed = *unreadable = 0;
    for (i = 0; i < AVB_HWREG_COUNT; i++) {
        unsigned was = i < before->count ? before->entries[i].state : AVB_HWREG_STATE_READ_FAILED;
        unsigned now = i < after->count ? after->entries[i].state : AVB_HWREG_STATE_READ_FAILED;

        if (was == AVB_HWREG_STATE_NOT_IN_SSOT || was == AVB_HWREG_STATE_NO_HAL) {
            if (now != was) {
                (*unreadable)++;
            }
        } else if (was != AVB_HWREG_STATE_VALID || now != AVB_HWREG_STATE_VALID) {
            (*unreadable)++;
        } else {
            (*compared)++;
            if (before->entries[i].value != after->entries[i].value) {
                (*changed)++;
            }
        }
    }
}

/*
 * Timing of the Pause-to-Restart interval from statistics polls.
 *
 * Each poll is bracketed by QPC reads, and the counter was read somewhere inside that bracket, so an edge is an interval,
 * never a moment: it lies after the start of the last poll that read the old value and before the end of the first that
 * read the new one. These counters mark entry only, so this bounds entry to entry, not the Paused state itself: it starts
 * at FilterPause entry, while the filter is still Pausing.
 *
 * Counters are cumulative, so an interval the polls fell between still shows in the deltas. NOT_RESOLVED therefore never
 * means there was no interval.
 *
 * #384 rule: a call counts as inside the interval only when a poll that finished before it started shows the interval open,
 * and a poll that started after it finished shows it still open. A call without that proof says nothing about Paused
 * behaviour, whatever it returned. tas_window_proven_calls() applies the rule to the polls themselves, so a run shows
 * whether the interval is reachable at all before any assertion depends on it.
 */
typedef struct {
    LONGLONG qpc_start, qpc_end;   /* QPC just before and just after the statistics call */
    ULONGLONG pause, restart;      /* FilterPauseCount, FilterRestartCount */
} TAS_POLL;

typedef struct {
    BOOL seen;
    LONGLONG last_old;   /* start of the last poll that read the old value */
    LONGLONG first_new;  /* end of the first poll that read the new value */
} TAS_EDGE;

typedef enum {
    TAS_WINDOW_UNMEASURED = 0,  /* no attributable pair of edges */
    TAS_WINDOW_NOT_RESOLVED,    /* the edges overlap, so only an upper bound exists. Never means absent */
    TAS_WINDOW_BOUNDED          /* the edges are apart: a lower and an upper bound */
} TAS_WINDOW_CLASS;

typedef struct {
    ULONGLONG base_pause, base_restart;
    TAS_POLL last;
    BOOL have_last, unattributable;
    TAS_EDGE pause, restart;
    unsigned polls;
    unsigned inside_samples;   /* polls that read the interval open */
    LONGLONG max_gap;          /* worst start-to-start gap between polls */
} TAS_WINDOW_TRACK;

static __inline void tas_window_init(TAS_WINDOW_TRACK *t, ULONGLONG base_pause, ULONGLONG base_restart)
{
    ZeroMemory(t, sizeof(*t));
    t->base_pause = base_pause;
    t->base_restart = base_restart;
}

static __inline void tas_edge_note(TAS_WINDOW_TRACK *t, TAS_EDGE *edge, const TAS_POLL *p)
{
    if (!t->have_last) {
        t->unattributable = TRUE;   /* no old read bounds it */
    } else {
        edge->last_old = t->last.qpc_start;
    }
    edge->seen = TRUE;
    edge->first_new = p->qpc_end;
}

static __inline void tas_window_feed(TAS_WINDOW_TRACK *t, const TAS_POLL *p)
{
    LONGLONG pause_delta = (LONGLONG)(p->pause - t->base_pause);
    LONGLONG restart_delta = (LONGLONG)(p->restart - t->base_restart);

    t->polls++;
    if (t->have_last && p->qpc_start - t->last.qpc_start > t->max_gap) {
        t->max_gap = p->qpc_start - t->last.qpc_start;
    }
    if (pause_delta < 0 || restart_delta < 0 || pause_delta > 1 || restart_delta > 1) {
        t->unattributable = TRUE;
    }
    if (!t->pause.seen && pause_delta >= 1) {
        tas_edge_note(t, &t->pause, p);
    }
    if (!t->restart.seen && restart_delta >= 1) {
        tas_edge_note(t, &t->restart, p);
    }
    if (t->restart.seen && !t->pause.seen) {
        t->unattributable = TRUE;
    }
    if (pause_delta == 1 && restart_delta == 0) {
        t->inside_samples++;
    }
    t->last = *p;
    t->have_last = TRUE;
}

static __inline TAS_WINDOW_CLASS tas_window_result(const TAS_WINDOW_TRACK *t, LONGLONG *lower, LONGLONG *upper)
{
    LONGLONG gap;

    *lower = *upper = 0;
    if (t->unattributable || !t->pause.seen || !t->restart.seen) {
        return TAS_WINDOW_UNMEASURED;
    }
    *upper = t->restart.first_new - t->pause.last_old;
    gap = t->restart.last_old - t->pause.first_new;
    if (gap <= 0) {
        return TAS_WINDOW_NOT_RESOLVED;
    }
    *lower = gap;
    return TAS_WINDOW_BOUNDED;
}

static __inline const char *tas_window_name(TAS_WINDOW_CLASS c)
{
    switch (c) {
    case TAS_WINDOW_BOUNDED:      return "BOUNDED";
    case TAS_WINDOW_NOT_RESOLVED: return "NOT_RESOLVED_NEVER_ABSENT";
    default:                      return "UNMEASURED";
    }
}

/* Polls proven wholly inside, by the rule above: each needs an open poll on either side of it */
static __inline unsigned tas_window_proven_calls(const TAS_WINDOW_TRACK *t)
{
    return t->inside_samples > 2 ? t->inside_samples - 2 : 0;
}

/*
 * What a run may say about each transition. Entry comes from counters. Completion is inferred: Pause because NDIS restarts
 * only from Paused, Restart because traffic passed the whole stack. OBSERVED completion is not a value here, because this
 * tool cannot see it: only an NDIS state event read on this instance can establish it.
 */
typedef enum {
    TAS_CLAIM_NONE = 0,
    TAS_CLAIM_ENTRY_OBSERVED,
    TAS_CLAIM_COMPLETION_INFERRED
} TAS_CLAIM;

static __inline TAS_CLAIM tas_pause_claim(const TAS_LIFECYCLE_EVIDENCE *e)
{
    switch (tas_lifecycle_outcome(e)) {
    case TAS_LIFECYCLE_PAUSE_ONLY:      return TAS_CLAIM_ENTRY_OBSERVED;
    case TAS_LIFECYCLE_RESTART_ENTERED:
    case TAS_LIFECYCLE_RESUMED:         return TAS_CLAIM_COMPLETION_INFERRED;
    default:                            return TAS_CLAIM_NONE;
    }
}

static __inline TAS_CLAIM tas_restart_claim(const TAS_LIFECYCLE_EVIDENCE *e)
{
    switch (tas_lifecycle_outcome(e)) {
    case TAS_LIFECYCLE_RESTART_ENTERED: return TAS_CLAIM_ENTRY_OBSERVED;
    case TAS_LIFECYCLE_RESUMED:         return TAS_CLAIM_COMPLETION_INFERRED;
    default:                            return TAS_CLAIM_NONE;
    }
}

static __inline const char *tas_claim_name(TAS_CLAIM c)
{
    switch (c) {
    case TAS_CLAIM_ENTRY_OBSERVED:      return "ENTRY_OBSERVED";
    case TAS_CLAIM_COMPLETION_INFERRED: return "COMPLETION_INFERRED";
    default:                            return "NONE";
    }
}

/*
 * The cleanup verdict of a Case A run, on every way out, kept beside the lifecycle outcome and never replacing it.
 * Demonstrated failures outrank the unknown, as in tas_restoration_outcome. VERIFIED needs all four facts of #361, so it
 * cannot be reached yet: only a hardware-derived tolerance can supply TAS_PHC_CONTINUOUS, and none exists.
 */
typedef struct {
    BOOL lifecycle_shown;     /* so the facts below were measured */
    unsigned regs_compared, regs_changed, regs_unreadable;
    TAS_PHC_FACT phc;
    BOOL tx_probed;           /* frames were sent with the observer alive */
    BOOL tx_shown;
    BOOL consistent;          /* meaningful only with lifecycle_shown */
    unsigned others_changed;
} TAS_LIFECYCLE_CLEANUP;

static __inline TAS_RESTORATION_OUTCOME tas_lifecycle_cleanup(const TAS_LIFECYCLE_CLEANUP *c)
{
    if (c->regs_changed != 0 || c->phc == TAS_PHC_DISCONTINUOUS || c->others_changed != 0 || (c->tx_probed && !c->tx_shown) ||
        (c->lifecycle_shown && !c->consistent)) {
        return TAS_RESTORATION_CLEANUP_FAILED;
    }
    if (!c->lifecycle_shown || c->regs_compared == 0 || c->regs_unreadable != 0 || !c->tx_probed || c->phc != TAS_PHC_CONTINUOUS) {
        return TAS_RESTORATION_INCONCLUSIVE;
    }
    return TAS_RESTORATION_VERIFIED;
}

static __inline const char *tas_cleanup_name(TAS_RESTORATION_OUTCOME o)
{
    switch (o) {
    case TAS_RESTORATION_CLEANUP_FAILED: return "CLEANUP_FAILED";
    case TAS_RESTORATION_INCONCLUSIVE:   return "INCONCLUSIVE_RESTORATION";
    default:                             return "RESTORATION_VERIFIED";
    }
}

/*
 * Which adapters a Case A run covers. Every adapter the driver enumerates is classified, and every eligible one runs.
 *
 * TAS_ADAPTER_FACTS deliberately has no vendor, device or family field, so nothing here can depend on identity. The NET_LUID
 * correlates results and sorts candidates. It selects nothing.
 *
 * Two capability sets are read, because they answer different questions. GET_ADAPTER_IDENTITY gives what the hardware has.
 * GET_HW_STATE (AVB_HW_STATE_DRIVER_CAPABILITIES) gives what the driver implements. Hardware with TAS and a driver without it
 * is NOT_IMPLEMENTED, never unsupported. Below PTP_READY the driver reports a partial set, and an empty hardware word may
 * only mean bring-up has not run, so neither is evidence of absence: those are INCONCLUSIVE.
 *
 * INTEL_CAP_TSN_TAS in the driver set means only that a setup_tas operation exists. It says nothing about activating a
 * schedule. That gap belongs to #350 and #351.
 */
#define TAS_MAX_ADAPTERS 8u
#define TAS_CASE_A_DRIVER_CAPS ((avb_u32)(INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO))   /* PHC and registers must be measurable */

typedef struct {
    avb_u64 net_luid;
    BOOL identity_ok;               /* GET_ADAPTER_IDENTITY answered for this adapter */
    BOOL hw_state_ok;               /* GET_HW_STATE answered on a handle bound to it */
    avb_u32 hw_caps;                /* what the hardware has */
    avb_u32 driver_caps;            /* what the driver implements for it */
    avb_u32 hw_state;               /* AVB_HW_STATE */
    BOOL link_up;                   /* up, with an address to send from */
    BOOL separate_management_link;  /* another adapter keeps the machine reachable */
} TAS_ADAPTER_FACTS;

typedef enum {
    TAS_PLAN_ELIGIBLE = 0,
    TAS_PLAN_SKIP_UNSUPPORTED,      /* hardware evidenced without TAS */
    TAS_PLAN_SKIP_NOT_IMPLEMENTED,  /* hardware has it, the driver does not: the gap stays visible */
    TAS_PLAN_SKIP_NO_LINK,          /* runtime not-applicable */
    TAS_PLAN_INCONCLUSIVE,          /* capability not determinable */
    TAS_PLAN_BLOCKED                /* applicable, but a prerequisite is unmet */
} TAS_PLAN_CLASS;

static __inline TAS_PLAN_CLASS tas_plan_classify(const TAS_ADAPTER_FACTS *f)
{
    if (!f->identity_ok || !f->hw_state_ok || f->hw_caps == 0) {
        return TAS_PLAN_INCONCLUSIVE;
    }
    if ((f->hw_caps & INTEL_CAP_TSN_TAS) == 0) {
        return TAS_PLAN_SKIP_UNSUPPORTED;
    }
    if ((int)f->hw_state < (int)AVB_HW_PTP_READY) {
        return TAS_PLAN_INCONCLUSIVE;
    }
    if ((f->driver_caps & INTEL_CAP_TSN_TAS) == 0) {
        return TAS_PLAN_SKIP_NOT_IMPLEMENTED;
    }
    if ((f->driver_caps & TAS_CASE_A_DRIVER_CAPS) != TAS_CASE_A_DRIVER_CAPS) {
        return TAS_PLAN_BLOCKED;
    }
    if (!f->link_up) {
        return TAS_PLAN_SKIP_NO_LINK;
    }
    return f->separate_management_link ? TAS_PLAN_ELIGIBLE : TAS_PLAN_BLOCKED;
}

static __inline const char *tas_plan_name(TAS_PLAN_CLASS c)
{
    switch (c) {
    case TAS_PLAN_ELIGIBLE:             return "ELIGIBLE";
    case TAS_PLAN_SKIP_UNSUPPORTED:     return "SKIP_UNSUPPORTED";
    case TAS_PLAN_SKIP_NOT_IMPLEMENTED: return "SKIP_NOT_IMPLEMENTED";
    case TAS_PLAN_SKIP_NO_LINK:         return "SKIP";
    case TAS_PLAN_BLOCKED:              return "BLOCKED";
    default:                            return "INCONCLUSIVE";
    }
}

/* Classifies every adapter and returns how many are eligible */
static __inline unsigned tas_plan_build(const TAS_ADAPTER_FACTS *a, unsigned n, TAS_PLAN_CLASS *classes)
{
    unsigned i, eligible = 0;

    for (i = 0; i < n; i++) {
        classes[i] = tas_plan_classify(&a[i]);
        eligible += classes[i] == TAS_PLAN_ELIGIBLE ? 1u : 0u;
    }
    return eligible;
}

/*
 * Who may be tried as the target's observer, lowest NET_LUID first, so the order does not follow enumeration. Observing needs
 * a link and a host-side capture, so no capability bit applies, and an adapter unable to be a target can still observe.
 * Being listed proves nothing: only seeing the target's baseline frames makes a candidate an observer.
 */
static __inline unsigned tas_observer_candidates(const TAS_ADAPTER_FACTS *a, unsigned n, unsigned target, unsigned *order)
{
    unsigned i, m = 0;

    for (i = 0; i < n; i++) {
        unsigned k;

        if (i == target || !a[i].identity_ok || !a[i].link_up) {
            continue;
        }
        for (k = m; k > 0 && a[order[k - 1]].net_luid > a[i].net_luid; k--) {
            order[k] = order[k - 1];
        }
        order[k] = i;
        m++;
    }
    return m;
}

/*
 * The run's exit from every adapter's class and result: 0 proved or clean, 1 failed or non-passing, 2 PHC discontinuous,
 * 3 not run. Mixed results are never collapsed into success, and a run with nothing eligible proves nothing.
 * Results of adapters that were not eligible are not read.
 */
static __inline int tas_plan_exit(const TAS_PLAN_CLASS *classes, const int *results, unsigned n)
{
    unsigned i, eligible = 0;
    BOOL failed = FALSE, not_run = FALSE, phc = FALSE;

    for (i = 0; i < n; i++) {
        switch (classes[i]) {
        case TAS_PLAN_ELIGIBLE:
            eligible++;
            failed = failed || results[i] == 1;
            phc = phc || results[i] == 2;
            not_run = not_run || results[i] == 3;
            break;
        case TAS_PLAN_SKIP_NOT_IMPLEMENTED:
        case TAS_PLAN_INCONCLUSIVE:
        case TAS_PLAN_BLOCKED:
            failed = TRUE;
            break;
        default:
            break;
        }
    }
    if (failed) {
        return 1;
    }
    if (eligible == 0 || not_run) {
        return 3;
    }
    return phc ? 2 : 0;
}
