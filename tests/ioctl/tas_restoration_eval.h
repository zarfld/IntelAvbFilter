/*
 * tas_restoration_eval.h - outcome of restoring an adapter by NIC cycle after SETUP_TAS.
 *
 * Pure logic, shared by test_tas_isolated_arm.c and its offline contract cases in
 * test_tas_restore_contract.c (TC-MOCK-012).  Terms follow docs/testing/test-verdict-contract.md:
 *   CLEANUP_FAILED            restoration demonstrably unsuccessful
 *   INCONCLUSIVE_RESTORATION  state the restoration depends on could not be measured
 * A returning link or resumed traffic is never sufficient: VERIFIED needs every item below.
 * Registers do not expose internal scheduler or FIFO state, so VERIFIED covers observed state only.
 *
 * Verifies: #328
 */
#pragma once

#include <windows.h>

typedef enum {
    TAS_RESTORATION_VERIFIED = 0,
    TAS_RESTORATION_CLEANUP_FAILED,
    TAS_RESTORATION_INCONCLUSIVE
} TAS_RESTORATION_OUTCOME;

typedef struct {
    BOOL cycle_completed;     /* disable and enable both returned success within their bounds */
    BOOL adapter_returned;    /* same NET_LUID, device ID and MAC bound again */
    BOOL attach_observed;     /* FilterAttach ran, so a cycle really happened */
    BOOL driver_disarmed;     /* driver reports tasArmed = 0 */
    BOOL armed_unreadable;    /* tasArmed could not be read: unmeasured, not a failure */
    BOOL tsn_mode_clear;      /* TQAVCTRL.TRANSMIT_MODE reads legacy */
    unsigned regs_compared;   /* registers compared with the baseline */
    unsigned regs_unreadable; /* registers that could not be read back */
    unsigned regs_changed;    /* registers that differ from the baseline */
    BOOL phc_observed;        /* PHC read before and after */
    BOOL phc_continuous;      /* PHC advanced with wall time */
    BOOL link_up;             /* link is back */
    BOOL tx_probed;           /* post-cycle TX probe ran */
    BOOL tx_healthy;          /* probe advanced the ring and drained its completions */
} TAS_RESTORATION_EVIDENCE;

static __inline TAS_RESTORATION_OUTCOME tas_restoration_outcome(const TAS_RESTORATION_EVIDENCE *e)
{
    /* Demonstrated failures first: they outrank anything still unknown. */
    if (!e->cycle_completed || !e->adapter_returned || (!e->driver_disarmed && !e->armed_unreadable) ||
        !e->tsn_mode_clear || e->regs_changed != 0 || (e->phc_observed && !e->phc_continuous) ||
        (e->tx_probed && !e->tx_healthy)) {
        return TAS_RESTORATION_CLEANUP_FAILED;
    }
    if (!e->attach_observed || e->armed_unreadable || e->regs_compared == 0 || e->regs_unreadable != 0 ||
        !e->phc_observed || !e->link_up || !e->tx_probed) {
        return TAS_RESTORATION_INCONCLUSIVE;
    }
    return TAS_RESTORATION_VERIFIED;
}
