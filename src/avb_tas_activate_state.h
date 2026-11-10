/*
 * avb_tas_activate_state.h - what IOCTL_AVB_TAS_ACTIVATE leaves the adapter's TAS state as (#357).
 *
 * Pure rules, shared by the IOCTL handler in avb_integration_fixed.c and its offline cases (TC-MOCK-047 and 048 in
 * tests/unit/test_tas_restore_contract.c). Not device-specific: the device code reports a result, and this decides what the
 * driver records and what GET_TAS_STATE then says.
 */
#pragma once

#include "../include/avb_ioctl.h"
#include "../external/intel_avb/lib/intel_private.h"   /* NSEC_PER_SEC, the one shared ns-per-second definition */

/* The gate list GET_TAS_STATE can report: the length of AVB_TAS_STATE.config.gate_states, never a second copy of it */
#define AVB_TAS_GCL_ENTRIES \
    ((avb_u32)(sizeof(((AVB_TAS_STATE *)0)->config.gate_states) / sizeof(((AVB_TAS_STATE *)0)->config.gate_states[0])))
C_ASSERT(sizeof(((AVB_TAS_STATE *)0)->config.gate_durations) / sizeof(((AVB_TAS_STATE *)0)->config.gate_durations[0]) ==
         sizeof(((AVB_TAS_STATE *)0)->config.gate_states) / sizeof(((AVB_TAS_STATE *)0)->config.gate_states[0]));

/*
 * The state after an activation result. A written schedule arms, timed or not and whether or not BaseT had passed. A restore
 * that did not complete leaves the hardware state unknown, so the adapter is dirty and refuses further activation until a NIC
 * cycle gives it a new context. Refusals and a verified restore change nothing. An unknown result is never taken as clean.
 */
static __inline avb_u32 avb_tas_state_after(avb_u32 result, avb_u32 previous)
{
    switch (result) {
    case AVB_TAS_ACT_COMMITTED:
    case AVB_TAS_ACT_COMMITTED_UNTIMED:
    case AVB_TAS_ACT_BASE_PASSED:
        return AVB_TAS_STATE_ARMED;
    case AVB_TAS_ACT_FAILED_NOT_RESTORED:
        return AVB_TAS_STATE_DIRTY;
    case AVB_TAS_ACT_FAILED_RESTORED:
    case AVB_TAS_ACT_INVALID:
    case AVB_TAS_ACT_NOT_CLEAN:
    case AVB_TAS_ACT_BUSY:
        return previous;
    default:
        return AVB_TAS_STATE_DIRTY;
    }
}

/*
 * The per-queue windows of a request as the gate list GET_TAS_STATE reports: one entry per interval between window edges,
 * bit q set while queue q is open, durations adding up to the cycle. FALSE when the intervals need more than eight entries,
 * so a request whose state cannot be reported truthfully is refused before any write. Expects a request whose windows lie
 * inside the cycle.
 */
static __inline BOOLEAN avb_tas_gcl_from_windows(const AVB_TAS_ACTIVATE_REQUEST *r, struct tsn_tas_config *c)
{
    avb_u32 edge[2u * AVB_TAS_QUEUES + 2u], n = 0, i, j, q, entries = 0;

    RtlZeroMemory(c, sizeof(*c));
    c->base_time_s = r->base_time_ns / NSEC_PER_SEC;
    c->base_time_ns = (avb_u32)(r->base_time_ns % NSEC_PER_SEC);
    c->cycle_time_s = (avb_u32)(r->cycle_ns / NSEC_PER_SEC);
    c->cycle_time_ns = (avb_u32)(r->cycle_ns % NSEC_PER_SEC);

    /* Every edge in [0, cycle], sorted, without duplicates */
    edge[n++] = 0;
    edge[n++] = r->cycle_ns;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        edge[n++] = r->window_start_ns[q];
        edge[n++] = r->window_end_ns[q];
    }
    for (i = 1; i < n; i++) {
        avb_u32 v = edge[i];

        for (j = i; j > 0 && edge[j - 1] > v; j--) {
            edge[j] = edge[j - 1];
        }
        edge[j] = v;
    }
    for (i = 0; i + 1 < n; i++) {
        avb_u32 from = edge[i], to = edge[i + 1], gates = 0;

        if (to == from) {
            continue;
        }
        if (entries == AVB_TAS_GCL_ENTRIES) {
            return FALSE;
        }
        for (q = 0; q < AVB_TAS_QUEUES; q++) {
            if (r->window_start_ns[q] <= from && to <= r->window_end_ns[q]) {
                gates |= 1u << q;
            }
        }
        c->gate_states[entries] = (uint8_t)gates;
        c->gate_durations[entries] = to - from;
        entries++;
    }
    return TRUE;
}
