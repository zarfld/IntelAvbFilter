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

/* What the device code's activate_tas returns to the IOCTL handler. Driver-internal, never part of the request ABI.
 * RAN: request->result says what happened. NOT_IMPLEMENTED: a valid request whose pre-activation behaviour is not
 * implemented; nothing was read or written and request->result is not set */
#define AVB_TAS_ACTIVATE_RAN             0
#define AVB_TAS_ACTIVATE_NOT_IMPLEMENTED 1

/* What the device operation of the versioned HOLD activation reports to the generic handler (#357 producer; #365 R7 steps
 * 6-9, R8). Driver-internal, never part of the request ABI */
typedef struct AVB_TAS_OP_OUTCOME {
    avb_u32 result;             /* AVB_TAS_ACT_* */
    int write_attempted;        /* a register write was attempted, also an injected or failed one (#352 R1 L7) */
    int mode_written;           /* the transmit-mode write reached the adapter (#362 decision 4, R8) */
    int acceptance_acted;       /* the commit executed under explicit unverified acceptance (#365 R4: out_flags bit 0) */
    avb_u64 phc_before_ns;      /* step 7 PHC read, 0 if none */
    avb_u64 phc_at_commit_ns;   /* PHC read after the commit write, 0 if none */
    avb_u32 failed_write;       /* number of the schedule write that failed, 0 if none */
} AVB_TAS_OP_OUTCOME;

/*
 * A device initialization in an existing adapter context (bring-up; #352 R1 L10/L12, restart-path clarification 2026-11-25). Several
 * device initializations of one bring-up are one logical event. retained: the record is not CLEAN; invalidates: the device's
 * initialization resets the PHC (or writes TAS registers); invalidated: it has done so, also when it failed afterwards. All of it is
 * sampled, decided and published in one hold of the metadata order lock.
 */
typedef struct AVB_TAS_INIT_EVENT {
    int counted;                /* this event has advanced the generation ... */
    avb_u64 counted_generation; /* ... to this value */
} AVB_TAS_INIT_EVENT;

/* The event is already counted and nothing has moved the generation since: a further initialization belongs to the same transition */
static __inline int avb_tas_init_same_event(const AVB_TAS_INIT_EVENT *event, avb_u64 generation)
{
    return event->counted && generation == event->counted_generation;
}

/* L12: refuse before the first device write */
static __inline int avb_tas_init_refused(const AVB_TAS_INIT_EVENT *event, int retained, int invalidates, int can_advance, avb_u64 generation)
{
    return retained && invalidates && !can_advance && !avb_tas_init_same_event(event, generation);
}

/* L10: the generation advances once for this event now */
static __inline int avb_tas_init_advances(const AVB_TAS_INIT_EVENT *event, int retained, int invalidated, avb_u64 generation)
{
    return retained && invalidated && !avb_tas_init_same_event(event, generation);
}

/*
 * How IOCTL_AVB_TAS_ACTIVATE completes once the device code returned. When it ran: STATUS_SUCCESS with the whole request as
 * output, because METHOD_BUFFERED drops the output on an error status and the caller needs request->result. When it refused
 * a valid request as not implemented (PASS under the #363 interim Option C): STATUS_NOT_IMPLEMENTED and no output.
 */
static __inline LONG avb_tas_activate_completion(int outcome, ULONG_PTR *information)
{
    if (outcome == AVB_TAS_ACTIVATE_RAN) {
        *information = sizeof(AVB_TAS_ACTIVATE_REQUEST);
        return STATUS_SUCCESS;
    }
    *information = 0;
    return STATUS_NOT_IMPLEMENTED;
}

/* The gate list GET_TAS_STATE can report: the length of AVB_TAS_STATE.config.gate_states, never a second copy of it */
#define AVB_TAS_GCL_ENTRIES \
    ((avb_u32)(sizeof(((AVB_TAS_STATE *)0)->config.gate_states) / sizeof(((AVB_TAS_STATE *)0)->config.gate_states[0])))
C_ASSERT(sizeof(((AVB_TAS_STATE *)0)->config.gate_durations) / sizeof(((AVB_TAS_STATE *)0)->config.gate_durations[0]) ==
         sizeof(((AVB_TAS_STATE *)0)->config.gate_states) / sizeof(((AVB_TAS_STATE *)0)->config.gate_states[0]));

/*
 * The state after an activation result. A written schedule arms, timed or not and whether or not BaseT had passed. A restore
 * that did not complete leaves the hardware state unknown, so the adapter is dirty and refuses further activation until a NIC
 * cycle gives it a new context. Refusals change nothing.
 *
 * FAILED_RESTORED says every register read back at its old value. A readback does not prove the transmit mode the adapter is
 * in (see disable_tas in devices/intel_i226_impl.c), so once the transmit-mode write had reached the adapter
 * (tsn_mode_written) its state is unknown as well: dirty (PR #397 R8). This is conservative reporting, not evidence that the
 * mode latches on this hardware. Only when no write reached the adapter does FAILED_RESTORED change nothing. An unknown result
 * is never taken as clean.
 */
static __inline avb_u32 avb_tas_state_after(avb_u32 result, avb_u32 previous, int tsn_mode_written)
{
    switch (result) {
    case AVB_TAS_ACT_COMMITTED:
    case AVB_TAS_ACT_COMMITTED_UNTIMED:
    case AVB_TAS_ACT_BASE_PASSED:
        return AVB_TAS_STATE_ARMED;
    case AVB_TAS_ACT_FAILED_NOT_RESTORED:
        return AVB_TAS_STATE_DIRTY;
    case AVB_TAS_ACT_FAILED_RESTORED:
        return tsn_mode_written ? AVB_TAS_STATE_DIRTY : previous;
    case AVB_TAS_ACT_INVALID:
    case AVB_TAS_ACT_NOT_CLEAN:
    case AVB_TAS_ACT_BUSY:
    /* The further refusals of the versioned request (#365 13.4): each completes before any register write */
    case AVB_TAS_ACT_READ_FAILED:
    case AVB_TAS_ACT_NOT_HARDWARE_VERIFIED:
    case AVB_TAS_ACT_NOT_SUPPORTED:
    case AVB_TAS_ACT_NOT_IMPLEMENTED:
    case AVB_TAS_ACT_STATE_CONFLICT:
    case AVB_TAS_ACT_NOT_READY:
    case AVB_TAS_ACT_NOT_BOUND:
    case AVB_TAS_ACT_ADAPTER_MISMATCH:
        return previous;
    default:
        return AVB_TAS_STATE_DIRTY;
    }
}

/*
 * How the versioned IOCTL_AVB_TAS_ACTIVATE completes (#365 13.4 with R5): only COMMITTED returns the structure (STATUS_SUCCESS,
 * Information 144); every other result completes with its NTSTATUS and no output. BASE_PASSED and COMMITTED_UNTIMED leave a
 * schedule (R5); a shared status does not establish which outcome occurred. An unknown result is never a success.
 */
static __inline LONG avb_tas_op_completion(avb_u32 result)
{
    switch (result) {
    case AVB_TAS_ACT_COMMITTED:             return STATUS_SUCCESS;
    case AVB_TAS_ACT_INVALID:               return STATUS_INVALID_PARAMETER;
    case AVB_TAS_ACT_NOT_CLEAN:             return STATUS_INVALID_DEVICE_STATE;
    case AVB_TAS_ACT_FAILED_RESTORED:       return STATUS_IO_DEVICE_ERROR;
    case AVB_TAS_ACT_FAILED_NOT_RESTORED:   return STATUS_UNSUCCESSFUL;
    case AVB_TAS_ACT_BASE_PASSED:           return STATUS_IO_TIMEOUT;
    case AVB_TAS_ACT_COMMITTED_UNTIMED:     return STATUS_IO_DEVICE_ERROR;
    case AVB_TAS_ACT_BUSY:                  return STATUS_DEVICE_BUSY;
    case AVB_TAS_ACT_READ_FAILED:           return STATUS_IO_DEVICE_ERROR;
    case AVB_TAS_ACT_NOT_HARDWARE_VERIFIED: return STATUS_REQUEST_NOT_ACCEPTED;
    case AVB_TAS_ACT_NOT_SUPPORTED:         return STATUS_NOT_SUPPORTED;
    case AVB_TAS_ACT_NOT_IMPLEMENTED:       return STATUS_NOT_IMPLEMENTED;
    case AVB_TAS_ACT_STATE_CONFLICT:        return STATUS_INVALID_DEVICE_STATE;
    case AVB_TAS_ACT_NOT_READY:             return STATUS_DEVICE_NOT_READY;
    case AVB_TAS_ACT_NOT_BOUND:             return STATUS_INVALID_DEVICE_STATE;
    case AVB_TAS_ACT_ADAPTER_MISMATCH:      return STATUS_INVALID_DEVICE_STATE;
    default:                                return STATUS_UNSUCCESSFUL;
    }
}

/*
 * The per-queue windows of a request as the gate list GET_TAS_STATE reports: one entry per interval between window edges,
 * bit q set while queue q is open, durations adding up to the cycle. FALSE when the intervals need more than eight entries,
 * so a request whose state cannot be reported truthfully is refused before any write. Expects a request whose windows lie
 * inside the cycle.
 */
static __inline BOOLEAN avb_tas_gcl_from_edges(avb_u64 base_time_ns, avb_u32 cycle_ns, const avb_u32 *start, const avb_u32 *end,
                                               struct tsn_tas_config *c)
{
    avb_u32 edge[2u * AVB_TAS_QUEUES + 2u], n = 0, i, j, q, entries = 0;

    RtlZeroMemory(c, sizeof(*c));
    c->base_time_s = base_time_ns / NSEC_PER_SEC;
    c->base_time_ns = (avb_u32)(base_time_ns % NSEC_PER_SEC);
    c->cycle_time_s = (avb_u32)(cycle_ns / NSEC_PER_SEC);
    c->cycle_time_ns = (avb_u32)(cycle_ns % NSEC_PER_SEC);

    /* Every edge in [0, cycle], sorted, without duplicates */
    edge[n++] = 0;
    edge[n++] = cycle_ns;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        edge[n++] = start[q];
        edge[n++] = end[q];
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
            if (start[q] <= from && to <= end[q]) {
                gates |= 1u << q;
            }
        }
        c->gate_states[entries] = (uint8_t)gates;
        c->gate_durations[entries] = to - from;
        entries++;
    }
    return TRUE;
}

/* The gate list of the withdrawn 80-byte request (kept for its offline cases, TC-MOCK-048) */
static __inline BOOLEAN avb_tas_gcl_from_windows(const AVB_TAS_ACTIVATE_REQUEST *r, struct tsn_tas_config *c)
{
    return avb_tas_gcl_from_edges(r->base_time_ns, r->cycle_ns, r->window_start_ns, r->window_end_ns, c);
}

/* The gate list of a versioned schedule (#365 13.2): start_ns and end_ns are relative to BaseT within the cycle, as the
 * former windows were relative to the cycle start, so the same intervals result */
static __inline BOOLEAN avb_tas_gcl_from_schedule(const AVB_TAS_SCHEDULE *s, struct tsn_tas_config *c)
{
    avb_u32 start[AVB_TAS_QUEUES], end[AVB_TAS_QUEUES], q;

    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        start[q] = s->queue[q].start_ns;
        end[q] = s->queue[q].end_ns;
    }
    return avb_tas_gcl_from_edges(s->base_time_ns, s->cycle_ns, start, end, c);
}
