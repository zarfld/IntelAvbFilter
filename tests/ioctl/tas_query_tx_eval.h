/* tas_query_tx_eval.h - send-window accounting and TX verdict of TC-TASAPI-001 (#366). Pure: no I/O, no globals.
 *
 * Used by tests/hardware/test_tas_query_clean.c for every send window, and checked offline by
 * tests/unit/test_tas_restore_contract.c (TC-MOCK-057). The offline check is test-logic evidence only, never hardware TX.
 *
 * Rules (PR #437 review r4579071534):
 *   - every send try counts as attempted; a send the socket refused counts as an error, never as "not sent";
 *   - the seen share is judged against attempted sends, so failed sends never shrink the denominator;
 *   - any send error fails the window, whatever share was seen;
 *   - zero attempts is not a window on the wire;
 *   - a baseline window that is not on the wire gives BLOCKED (the wire proves nothing then); a checked window that is not on
 *     the wire, or whose interleaved queries are absent or changed, gives FAIL.
 */
#ifndef TAS_QUERY_TX_EVAL_H
#define TAS_QUERY_TX_EVAL_H

#include <windows.h>

typedef struct {
    unsigned attempted, sent, errors, seen;   /* sends tried, accepted by the socket, refused, and seen by the observer */
    unsigned queries, changed;                /* interleaved queries, and those that failed or reported other registers */
} TAS_TX_WINDOW;

typedef enum {
    TAS_TX_PASS = 0,
    TAS_TX_BLOCKED_BASELINE,   /* the baseline window was not on the wire: no TX verdict can be drawn */
    TAS_TX_FAIL                /* the checked window was not on the wire, or its queries were absent or changed */
} TAS_TX_RESULT;

static __inline void tas_tx_record_send(TAS_TX_WINDOW *w, BOOL accepted)
{
    w->attempted++;
    if (accepted) {
        w->sent++;
    } else {
        w->errors++;
    }
}

static __inline void tas_tx_record_query(TAS_TX_WINDOW *w, BOOL same)
{
    w->queries++;
    if (!same) {
        w->changed++;
    }
}

/** The observer saw at least min_percent of every attempted send, and no send failed */
static __inline BOOL tas_tx_on_wire(const TAS_TX_WINDOW *w, unsigned min_percent)
{
    return w->attempted != 0 && w->errors == 0 &&
           (unsigned long long)w->seen * 100u >= (unsigned long long)w->attempted * min_percent;
}

static __inline TAS_TX_RESULT tas_tx_judge(const TAS_TX_WINDOW *base, const TAS_TX_WINDOW *probe, unsigned min_percent)
{
    if (!tas_tx_on_wire(base, min_percent)) {
        return TAS_TX_BLOCKED_BASELINE;
    }
    if (tas_tx_on_wire(probe, min_percent) && probe->queries != 0 && probe->changed == 0) {
        return TAS_TX_PASS;
    }
    return TAS_TX_FAIL;
}

#endif /* TAS_QUERY_TX_EVAL_H */
