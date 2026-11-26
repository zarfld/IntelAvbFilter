/*
 * owned_mutation.h - one temporary change of a hardware state and its restoration, ordered against an abort on another thread (#369,
 * PR #524 review).
 *
 * A console handler runs on a thread of its own, at any point of the writer. Excluding two restorers is not enough: a restoration before
 * the write ended restores nothing. So the write is published (owed), issued and ended in this order, and an abort
 *   - prevents a write that was not issued: it is never issued afterwards;
 *   - waits, up to a bound the caller defines, for the observed end of an issued one and only then restores;
 *   - otherwise reports the end as unconfirmed. The write stays owned and its writer restores once it has ended.
 * Exactly one thread restores; the other one waits for that result.
 * The accesses of write, observe and restore use a handle the change owns itself: a duplicate of the caller's, so the same file object and the
 * same binding, never a newly opened one. It is closed once, when the writer has left AND nothing is open any more, by whichever of the two is
 * the last. The caller may close its own handle at any time; neither a timeout nor a failed cleanup closes this one. Shared by tests/hardware/test_tas_activate.c (TC-TASACT-META-005, -006) and
 * its offline case TC-MOCK-077 in test_tas_restore_contract.c (TC-TASACT-META-006 calls the abort on hardware, after the write ended). Passing that case says nothing about hardware.
 *
 * Verifies: #369
 */
#pragma once

#include <windows.h>

typedef enum {
    OWNED_MUT_IDLE = 0,    /* nothing owned */
    OWNED_MUT_OWED,        /* published, not issued: can still be prevented */
    OWNED_MUT_IN_FLIGHT,   /* issued, its end is not established */
    OWNED_MUT_WRITTEN,     /* its end was observed: the restoration is owed */
    OWNED_MUT_RESTORING,   /* one thread restores */
    OWNED_MUT_SETTLED,     /* the restoration ended, its result is in restored */
    OWNED_MUT_CANCELLED    /* after an abort: nothing is issued any more in this process */
} OWNED_MUT_STATE;

typedef enum { OWNED_MUT_AT_PUBLISHED = 0, OWNED_MUT_AT_ISSUED, OWNED_MUT_AT_ENDED, OWNED_MUT_POINTS } OWNED_MUT_POINT;

/* Results of the restoration (ctx-specific values are positive) */
#define OWNED_MUT_NONE          0L    /* no restoration ran: nothing was issued */
#define OWNED_MUT_UNCONFIRMED (-1L)   /* it was not seen to end */

typedef struct {
    volatile LONG state;      /* OWNED_MUT_STATE */
    volatile LONG restored;   /* result of the restoration */
    DWORD writer, restorer;   /* thread ids */
    HANDLE write_ended, restore_ended;
    void (*write)(void *ctx);                        /* the change; returns when it ended */
    void (*observe)(void *ctx);                      /* under the change */
    LONG (*restore)(void *ctx);                      /* back, and read: > 0 */
    void (*at)(void *ctx, OWNED_MUT_POINT point);    /* only the offline case: a point of the writer; NULL otherwise */
    void *ctx;
    volatile LONG *stop;      /* not NULL and set: the process is ending, nothing is issued any more */
    HANDLE volatile handle;   /* the change's own handle, NULL: none taken or closed. Read by write, observe and restore only */
    volatile LONG taken;      /* a handle was taken: once per change */
    volatile LONG left;       /* the writer has left run (or the owner gave the change up): it uses the handle no more */
    volatile LONG closed;     /* how often the handle was closed */
} OWNED_MUTATION;

typedef enum {
    OWNED_ABORT_NOTHING = 0,   /* nothing was owned */
    OWNED_ABORT_PREVENTED,     /* the published write was not issued and is not issued any more */
    OWNED_ABORT_RESTORED,      /* after its observed end: the result is in restored */
    OWNED_ABORT_UNCONFIRMED    /* the end of the write or of the restoration was not observed within the bound: nothing was restored here */
} OWNED_ABORT;

static __inline BOOL owned_mutation_cas(OWNED_MUTATION *m, LONG from, LONG to)
{
    return InterlockedCompareExchange(&m->state, to, from) == from;
}

static __inline LONG owned_mutation_state(OWNED_MUTATION *m)
{
    return InterlockedCompareExchange(&m->state, 0, 0);
}

/* TRUE while the change may still be there or come: no final state can be judged. After an unconfirmed abort it stays TRUE */
static __inline BOOL owned_mutation_open(OWNED_MUTATION *m)
{
    LONG state = owned_mutation_state(m);

    return state != OWNED_MUT_IDLE && state != OWNED_MUT_SETTLED && state != OWNED_MUT_CANCELLED;
}

/* The events of the order. FALSE: nothing may be written */
static __inline BOOL owned_mutation_prepare(OWNED_MUTATION *m)
{
    if (m->write_ended == NULL) { m->write_ended = CreateEventA(NULL, TRUE, FALSE, NULL); }
    if (m->restore_ended == NULL) { m->restore_ended = CreateEventA(NULL, TRUE, FALSE, NULL); }
    return m->write_ended != NULL && m->restore_ended != NULL;
}

/* A change that was never run: what prepare created, also in part */
static __inline void owned_mutation_unprepare(OWNED_MUTATION *m)
{
    if (m->write_ended != NULL) { CloseHandle(m->write_ended); }
    if (m->restore_ended != NULL) { CloseHandle(m->restore_ended); }
    m->write_ended = NULL;
    m->restore_ended = NULL;
}

/* BEGIN handle lifetime */
/* Takes a duplicate of the caller's handle, before run. FALSE: none is owned, nothing may be written. INVALID_HANDLE_VALUE is refused because
 * as a pseudo handle it would duplicate the process */
static __inline BOOL owned_mutation_own(OWNED_MUTATION *m, HANDLE h)
{
    HANDLE dup = NULL;

    if (h == NULL || h == INVALID_HANDLE_VALUE || InterlockedCompareExchange(&m->left, 0, 0) != 0 ||
        InterlockedCompareExchange(&m->taken, 1, 0) != 0) {
        return FALSE;
    }
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        InterlockedExchange(&m->taken, 0);
        return FALSE;
    }
    InterlockedExchangePointer((PVOID volatile *)&m->handle, dup);
    return TRUE;
}

/* Closes the handle when nobody can use it any more. Called by the writer after left was set and by a restorer after SETTLED was set: with
 * these orders the last one closes, and only one takes it out */
static __inline void owned_mutation_drop(OWNED_MUTATION *m)
{
    HANDLE h;

    if (InterlockedCompareExchange(&m->left, 0, 0) == 0 || owned_mutation_open(m)) {
        return;
    }
    h = (HANDLE)InterlockedExchangePointer((PVOID volatile *)&m->handle, NULL);
    if (h != NULL) {
        CloseHandle(h);
        InterlockedIncrement(&m->closed);
    }
}
/* END handle lifetime */

/* The writer has left its run, or the owner gives a change up that was never run */
static __inline void owned_mutation_leave(OWNED_MUTATION *m)
{
    InterlockedExchange(&m->left, 1);
    owned_mutation_drop(m);
}

/* What is left of the bound that started at t0 */
static __inline DWORD owned_mutation_left(DWORD t0, DWORD ms)
{
    DWORD used = GetTickCount() - t0;
    return used >= ms ? 0 : ms - used;
}

/* TRUE: the event was set within what is left of the bound. The caller reads the state again; an event without the state that belongs to
 * it ends with the bound, too */
static __inline BOOL owned_mutation_wait(HANDLE event, DWORD t0, DWORD ms)
{
    DWORD left = owned_mutation_left(t0, ms);

    if (WaitForSingleObject(event, left) != WAIT_OBJECT_0 || left == 0) {
        return FALSE;
    }
    SwitchToThread();
    return TRUE;
}

/* WRITTEN to SETTLED on this thread. FALSE: another thread did that or is doing it */
static __inline BOOL owned_mutation_restore_here(OWNED_MUTATION *m)
{
    LONG r;

    if (!owned_mutation_cas(m, OWNED_MUT_WRITTEN, OWNED_MUT_RESTORING)) {
        return FALSE;
    }
    m->restorer = GetCurrentThreadId();
    r = m->restore(m->ctx);
    InterlockedExchange(&m->restored, r);
    SetEvent(m->restore_ended);   /* before SETTLED: a next change resets it only after SETTLED was seen */
    InterlockedExchange(&m->state, OWNED_MUT_SETTLED);
    owned_mutation_drop(m);
    return TRUE;
}

/* The writer's own way out: restores, or waits up to ms for the thread that does */
static __inline LONG owned_mutation_finish(OWNED_MUTATION *m, DWORD ms)
{
    DWORD t0 = GetTickCount();

    for (;;) {
        switch (owned_mutation_state(m)) {
        case OWNED_MUT_OWED:        /* left before it was issued */
            if (owned_mutation_cas(m, OWNED_MUT_OWED, OWNED_MUT_SETTLED)) { return OWNED_MUT_NONE; }
            break;
        case OWNED_MUT_IN_FLIGHT:   /* only the writer comes here: it has left its own write */
            (void)owned_mutation_cas(m, OWNED_MUT_IN_FLIGHT, OWNED_MUT_WRITTEN);
            break;
        case OWNED_MUT_WRITTEN:
            (void)owned_mutation_restore_here(m);
            break;
        case OWNED_MUT_RESTORING:
            if (!owned_mutation_wait(m->restore_ended, t0, ms)) { return OWNED_MUT_UNCONFIRMED; }
            break;
        default:
            return InterlockedCompareExchange(&m->restored, 0, 0);
        }
    }
}

/* One change: published, issued unless an abort came first, observed and restored on every way out. *issued: the write was issued */
static __inline LONG owned_mutation_run(OWNED_MUTATION *m, DWORD ms, volatile BOOL *issued)
{
    LONG r = OWNED_MUT_NONE;

    *issued = FALSE;
    if (m->write_ended == NULL || m->restore_ended == NULL) {
        owned_mutation_leave(m);
        return OWNED_MUT_NONE;
    }
    ResetEvent(m->write_ended);
    ResetEvent(m->restore_ended);
    if (!owned_mutation_cas(m, OWNED_MUT_IDLE, OWNED_MUT_OWED) && !owned_mutation_cas(m, OWNED_MUT_SETTLED, OWNED_MUT_OWED)) {
        owned_mutation_leave(m);
        return OWNED_MUT_NONE;   /* an abort came before */
    }
    __try {
        InterlockedExchange(&m->restored, OWNED_MUT_NONE);
        if (m->at) { m->at(m->ctx, OWNED_MUT_AT_PUBLISHED); }
        m->writer = GetCurrentThreadId();
        /* published before stop is read: an abort that sets stop first is either seen here or finds this change */
        if ((m->stop == NULL || InterlockedCompareExchange(m->stop, 0, 0) == 0) && owned_mutation_cas(m, OWNED_MUT_OWED, OWNED_MUT_IN_FLIGHT)) {
            *issued = TRUE;
            if (m->at) { m->at(m->ctx, OWNED_MUT_AT_ISSUED); }
            m->write(m->ctx);
            (void)owned_mutation_cas(m, OWNED_MUT_IN_FLIGHT, OWNED_MUT_WRITTEN);
            SetEvent(m->write_ended);
            if (m->at) { m->at(m->ctx, OWNED_MUT_AT_ENDED); }
            m->observe(m->ctx);
        }
    } __finally {
        r = owned_mutation_finish(m, ms);
        owned_mutation_leave(m);
    }
    return r;
}

/* From any thread, also the writer's own (unhandled exception: a thread that runs this is not inside its own synchronous request any
 * more). ms bounds every wait; the restoration itself starts only after the write was seen to end */
static __inline OWNED_ABORT owned_mutation_abort(OWNED_MUTATION *m, DWORD ms)
{
    DWORD t0 = GetTickCount();
    BOOL owned = FALSE;

    for (;;) {
        LONG state = owned_mutation_state(m);

        switch (state) {
        case OWNED_MUT_IDLE:
        case OWNED_MUT_SETTLED:
            if (owned_mutation_cas(m, state, OWNED_MUT_CANCELLED)) { return owned ? OWNED_ABORT_RESTORED : OWNED_ABORT_NOTHING; }
            break;
        case OWNED_MUT_OWED:
            if (owned_mutation_cas(m, OWNED_MUT_OWED, OWNED_MUT_CANCELLED)) { return OWNED_ABORT_PREVENTED; }
            break;
        case OWNED_MUT_IN_FLIGHT:
            owned = TRUE;
            if (m->writer == GetCurrentThreadId()) {
                (void)owned_mutation_cas(m, OWNED_MUT_IN_FLIGHT, OWNED_MUT_WRITTEN);
            } else if (!owned_mutation_wait(m->write_ended, t0, ms)) {
                return OWNED_ABORT_UNCONFIRMED;
            }
            break;
        case OWNED_MUT_WRITTEN:
            owned = TRUE;
            (void)owned_mutation_restore_here(m);
            break;
        case OWNED_MUT_RESTORING:
            owned = TRUE;
            if (m->restorer == GetCurrentThreadId() || !owned_mutation_wait(m->restore_ended, t0, ms)) {
                return OWNED_ABORT_UNCONFIRMED;
            }
            break;
        default:
            return owned ? OWNED_ABORT_RESTORED : OWNED_ABORT_NOTHING;
        }
    }
}

/* The lines of TC-TASACT-META-005 and -006 */
typedef enum { M5_RESTORE_VERIFIED = 1, M5_RESTORE_DIFFERS, M5_RESTORE_UNREAD } M5_RESTORE;

static __inline const char *m5_restore_tag(LONG r)
{
    return r == M5_RESTORE_VERIFIED ? "PASS" : (r == M5_RESTORE_DIFFERS ? "CLEANUP_FAILED" : "INCONCLUSIVE_RESTORATION");
}

/* An aborted case passes nothing; a restoration that was not seen stays INCONCLUSIVE_RESTORATION */
static __inline const char *m5_abort_tag(OWNED_ABORT a, LONG r)
{
    return a == OWNED_ABORT_PREVENTED || (a == OWNED_ABORT_RESTORED && r == M5_RESTORE_VERIFIED) ? "INCONCLUSIVE" :
           (a == OWNED_ABORT_RESTORED && r == M5_RESTORE_DIFFERS ? "CLEANUP_FAILED" : "INCONCLUSIVE_RESTORATION");
}
