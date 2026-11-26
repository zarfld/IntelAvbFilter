/* tas_link_event_eval.h - judgement of the EVENT samples of TC-TASAPI-021 (#472). Pure: no I/O, no globals.
 *
 * Used by tests/hardware/test_tas_query_clean.c (--link-speed) and checked offline by tests/unit/test_tas_restore_contract.c
 * (TC-MOCK-076). The offline check is test-logic evidence only, never a link event.
 *
 * Rules (#472):
 *   - one sample is OS reading, query, OS reading. When both OS readings agree (state and rate), the reply must equal them: 0 for
 *     disconnected, the rate for connected. This holds in both directions and for any duration: a reply ahead of the OS is a
 *     disagreement exactly like a reply behind it. There is no allowance and no tolerance;
 *   - a sample whose OS readings differ (OS change inside the bracket) is not compared. An OS error, an unknown or fractional rate, a
 *     failed query and a reply of another adapter context are missing measurements: not compared either, and never agreement;
 *   - samples before the write establish the initial state only. They are judged on their own and never count for the event, its
 *     phases or their comparisons;
 *   - the event is a link-down after the write, starting from a connected link measured immediately before it, and a link-up after
 *     that. Without that connected start a disconnected reading is the old state, not an event. Each OS reading counts for the event,
 *     also one inside a bracket that is not compared.
 */
#ifndef TAS_LINK_EVENT_EVAL_H
#define TAS_LINK_EVENT_EVAL_H

#include <windows.h>

/* NET_IF_MEDIA_CONNECT_STATE (ifdef.h); the hardware test asserts the values at compile time */
#define EV_IF_CONNECTED        1
#define EV_IF_DISCONNECTED     2
#define EV_BPS_PER_MBIT        1000000ULL

/** One OS - query - OS sample of one NET_LUID */
typedef struct {
    double t0, t1, t2, t3;                /* ms: first OS reading t0..t1, query t1..t2, second OS reading t2..t3 */
    unsigned long e1, e2;                 /* raw GetIfEntry2 return codes */
    int s1, s2;                           /* connection states */
    unsigned long long tx1, tx2;          /* bit/s */
    LONG qstatus;
    unsigned long qgot;
    BOOL qok;                             /* valid v1 reply for the target NET_LUID */
    unsigned mbps;
    unsigned long long epoch, generation;
} EV_SAMPLE;

/** The event as the OS readings show it, one sample after the other */
typedef enum { EV_PHASE_BEFORE = 0, EV_PHASE_DOWN, EV_PHASE_UP } EV_PHASE;

typedef struct {
    BOOL armed;                           /* the last sample before the write showed a connected link in both readings */
    BOOL saw_down, saw_up;
    EV_PHASE phase;                       /* BEFORE until the OS reports the link-down of the event */
} EV_TRACK;

/** The samples of one phase */
typedef struct {
    unsigned match, mismatch;             /* compared replies */
    unsigned first_mismatch;              /* sample index, valid when mismatch != 0 */
    double longest_ms;                    /* longest run of consecutive mismatches, from its first to its last query start */
    unsigned split;                       /* not compared: the OS state or rate changed inside the bracket */
    unsigned os_error, rate_unknown, state_unknown, other_context, failed;   /* not compared: missing measurements */
    LONG first_failed;                    /* raw NTSTATUS, valid when failed != 0 */
} EV_GROUP;

typedef struct {
    EV_GROUP before, down, up;            /* before: up to the link-down of the event (the samples before the write included) */
    EV_TRACK track;
} EV_RESULT;

typedef enum {
    EV_OUT_PASS = 0,
    EV_OUT_FAIL,                          /* a compared reply differs from its OS bracket */
    EV_OUT_QUERY_FAILED,                  /* no such difference, but a query failed: its status decides */
    EV_OUT_INCONCLUSIVE
} EV_OUT;

/** The agreeing state of both OS readings: 1 disconnected, 2 connected, 0 anything else */
static __inline int ev_os(const EV_SAMPLE *s)
{
    if (s->e1 != 0 || s->e2 != 0 || s->s1 != s->s2 || s->tx1 != s->tx2) {
        return 0;
    }
    return s->s1 == EV_IF_DISCONNECTED ? 1 : s->s1 == EV_IF_CONNECTED ? 2 : 0;
}

/** The value #365's K8 rule requires for this sample's OS bracket; FALSE: no reference */
static __inline BOOL ev_expected(const EV_SAMPLE *s, unsigned long long *mbps)
{
    int os = ev_os(s);

    *mbps = 0;
    if (os == 1) {
        return TRUE;
    }
    if (os != 2 || s->tx1 == 0 || s->tx1 == ~0ULL || s->tx1 % EV_BPS_PER_MBIT != 0 || s->tx1 / EV_BPS_PER_MBIT > 0xFFFFFFFFULL) {
        return FALSE;
    }
    *mbps = s->tx1 / EV_BPS_PER_MBIT;
    return TRUE;
}

/** The state from which an event can start. last: the last sample before the write, NULL when there is none */
static __inline void ev_track_start(EV_TRACK *t, const EV_SAMPLE *last)
{
    ZeroMemory(t, sizeof(*t));
    t->armed = last != NULL && ev_os(last) == 2;
}

static __inline void ev_track_reading(EV_TRACK *t, unsigned long error, int state)
{
    if (error != 0) {
        return;
    }
    if (state == EV_IF_DISCONNECTED && t->armed) {
        t->saw_down = TRUE;
        t->phase = EV_PHASE_DOWN;
    } else if (state == EV_IF_CONNECTED && t->phase == EV_PHASE_DOWN) {
        t->saw_up = TRUE;
        t->phase = EV_PHASE_UP;
    }
}

/** The next sample after the write */
static __inline void ev_track(EV_TRACK *t, const EV_SAMPLE *s)
{
    ev_track_reading(t, s->e1, s->s1);
    ev_track_reading(t, s->e2, s->s2);
}

/** Every sample in its phase, against its own OS bracket. iw: index of the first sample after the write */
static __inline void ev_judge(const EV_SAMPLE *s, unsigned n, unsigned iw, EV_RESULT *r)
{
    const EV_GROUP *run_group = NULL;     /* of the mismatch run that the previous compared sample belongs to */
    unsigned long long epoch = 0, expected;
    double run_start = 0;
    BOOL have_epoch = FALSE;
    unsigned k;

    ZeroMemory(r, sizeof(*r));
    ev_track_start(&r->track, iw != 0 && iw <= n ? &s[iw - 1] : NULL);
    for (k = 0; k < n; k++) {
        EV_GROUP *g;

        if (k >= iw) {
            ev_track(&r->track, &s[k]);
        }
        g = r->track.phase == EV_PHASE_DOWN ? &r->down : r->track.phase == EV_PHASE_UP ? &r->up : &r->before;
        if (!s[k].qok) {
            if (g->failed++ == 0) {
                g->first_failed = s[k].qstatus;
            }
            run_group = NULL;
            continue;
        }
        if (!have_epoch) {
            epoch = s[k].epoch;
            have_epoch = TRUE;
        }
        if (s[k].e1 != 0 || s[k].e2 != 0) {
            g->os_error++;
        } else if (s[k].s1 != s[k].s2 || s[k].tx1 != s[k].tx2) {
            g->split++;
        } else if (ev_os(&s[k]) == 0) {
            g->state_unknown++;
        } else if (!ev_expected(&s[k], &expected)) {
            g->rate_unknown++;
        } else if (s[k].epoch != epoch) {
            g->other_context++;
        } else if (s[k].mbps == expected) {
            g->match++;
        } else {
            if (g->mismatch++ == 0) {
                g->first_mismatch = k;
            }
            if (run_group != g) {
                run_group = g;
                run_start = s[k].t1;
            }
            if (s[k].t1 - run_start > g->longest_ms) {
                g->longest_ms = s[k].t1 - run_start;
            }
            continue;
        }
        run_group = NULL;
    }
}

/** The event itself: both transitions measured by the OS after an accepted write; the accepted write alone is nothing */
static __inline EV_OUT ev_cause(const EV_RESULT *r, BOOL write_accepted)
{
    return write_accepted && r->track.saw_down && r->track.saw_up ? EV_OUT_PASS : EV_OUT_INCONCLUSIVE;
}

/** One phase. A compared disagreement is FAIL on every path; a pass needs the proven path, a comparison and no missing measurement.
 *  A split bracket is a measured OS change: not compared, and no missing measurement */
static __inline EV_OUT ev_phase(const EV_GROUP *g, BOOL path_proven)
{
    if (g->mismatch != 0) {
        return EV_OUT_FAIL;
    }
    if (!path_proven) {
        return EV_OUT_INCONCLUSIVE;
    }
    if (g->failed != 0) {
        return EV_OUT_QUERY_FAILED;
    }
    return g->match == 0 || g->os_error != 0 || g->state_unknown != 0 || g->rate_unknown != 0 || g->other_context != 0
               ? EV_OUT_INCONCLUSIVE : EV_OUT_PASS;
}

#endif /* TAS_LINK_EVENT_EVAL_H */
