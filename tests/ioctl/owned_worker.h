/*
 * owned_worker.h - a worker thread and the storage it uses, owned by the caller until its completion is established (#505, PR #518).
 *
 * A bounded wait that ends without the thread having ended settles nothing: the worker still uses its storage and may still be
 * issuing its operation. Shared by tests/hardware/test_tas_activate.c (--phc-hold-control) and its offline case TC-MOCK-065 in
 * test_tas_restore_contract.c. Passing that case says nothing about hardware.
 *
 * Verifies: #505
 */
#pragma once

#include <windows.h>
#include <stdlib.h>
#include <string.h>

typedef enum { OWNED_WORKER_NONE = 0, OWNED_WORKER_RUNNING, OWNED_WORKER_DONE } OWNED_WORKER_STATE;

typedef struct {
    HANDLE thread;
    void *ctx;                  /* heap storage the worker uses: never the caller's stack */
    OWNED_WORKER_STATE state;   /* DONE only after the thread was observed to have ended */
} OWNED_WORKER;

/* Copies init (bytes) to heap storage and starts fn on it. FALSE: nothing runs and nothing is owned */
static __inline BOOL owned_worker_start(OWNED_WORKER *w, LPTHREAD_START_ROUTINE fn, const void *init, size_t bytes)
{
    w->thread = NULL;
    w->state = OWNED_WORKER_NONE;
    w->ctx = malloc(bytes);
    if (w->ctx == NULL) {
        return FALSE;
    }
    memcpy(w->ctx, init, bytes);
    w->thread = CreateThread(NULL, 0, fn, w->ctx, 0, NULL);
    if (w->thread == NULL) {
        free(w->ctx);
        w->ctx = NULL;
        return FALSE;
    }
    w->state = OWNED_WORKER_RUNNING;
    return TRUE;
}

/* Waits up to ms for the thread to end. Only an observed end is DONE: a timeout and a failed wait leave it RUNNING */
static __inline OWNED_WORKER_STATE owned_worker_wait(OWNED_WORKER *w, DWORD ms)
{
    if (w->state == OWNED_WORKER_RUNNING && WaitForSingleObject(w->thread, ms) == WAIT_OBJECT_0) {
        w->state = OWNED_WORKER_DONE;
    }
    return w->state;
}

/* The worker's storage for reading its result: NULL until its completion is established */
static __inline void *owned_worker_result(const OWNED_WORKER *w)
{
    return w->state == OWNED_WORKER_DONE ? w->ctx : NULL;
}

/* Frees a finished worker. FALSE: it may still be running - handle and storage stay valid until the process ends */
static __inline BOOL owned_worker_release(OWNED_WORKER *w)
{
    if (w->state == OWNED_WORKER_RUNNING) {
        return FALSE;
    }
    if (w->thread != NULL) {
        CloseHandle(w->thread);
    }
    free(w->ctx);
    w->thread = NULL;
    w->ctx = NULL;
    w->state = OWNED_WORKER_NONE;
    return TRUE;
}

/* CLEANUP verdict of the hold control. settled: no PHC operation this test sent can still be running */
typedef enum { HOLDCTL_CLEANUP_PASS = 0, HOLDCTL_CLEANUP_FAILED, HOLDCTL_CLEANUP_INCONCLUSIVE, HOLDCTL_CLEANUP_UNSETTLED } HOLDCTL_CLEANUP;

static __inline HOLDCTL_CLEANUP holdctl_cleanup(BOOL settled, BOOL failed, BOOL unmeasured)
{
    if (!settled) {
        return HOLDCTL_CLEANUP_UNSETTLED;   /* the final state is not established, whatever was measured so far */
    }
    return failed ? HOLDCTL_CLEANUP_FAILED : (unmeasured ? HOLDCTL_CLEANUP_INCONCLUSIVE : HOLDCTL_CLEANUP_PASS);
}
