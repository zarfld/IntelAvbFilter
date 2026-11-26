/* tas_lookup_eval.h - one #352 operation-record lookup through IOCTL_AVB_TAS_QUERY_HW, and the record it returns (TC-TASAPI-014, #379).
 * Pure: no I/O.
 *
 * Used by tests/hardware/test_tas_activate.c (--failure-retrieval) and checked offline by tests/unit/test_tas_restore_contract.c
 * (TC-MOCK-081). The offline check is test logic only, never a lookup on hardware.
 *
 * Published rules:
 *   - #365 12.1: the query's inputs are version, size and lookup_client_op_id. A bound adapter without INTEL_CAP_TSN_TAS is refused
 *     STATUS_NOT_SUPPORTED, a family without a query register map STATUS_NOT_IMPLEMENTED; both before any lookup;
 *   - #365 12.2: lookup_result NOT_REQUESTED 1 (id 0 only), FOUND 2, NOT_FOUND 3 (a store exists and holds no such record),
 *     NOT_IMPLEMENTED 4 (no store, nothing searched); lookup is valid only with FOUND and otherwise all 0 (U1);
 *   - #352 AC1/AC2 and its 2026-11-17 terminology: a failed operation's record is found on its originating open instance; a handle
 *     duplicated from that instance is the same instance; an independent CreateFile instance or another adapter gets NOT_FOUND;
 *   - #352 R1 L7: an operation that attempted a register write advances the generation exactly once, so its generation_after is the
 *     generation it was admitted with + 1;
 *   - #365 13.4: FAILED_RESTORED 3 completes STATUS_IO_DEVICE_ERROR.
 * Not published, therefore never asserted here: the encodings of operation, stage, hw_effect and op_flags, and the commit bracket of
 * an operation that failed before any commit. The caller prints them raw.
 */
#ifndef TAS_LOOKUP_EVAL_H
#define TAS_LOOKUP_EVAL_H

#include <windows.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../common/avb_verdict.h"   /* the SDK NTSTATUS values, AVB_NTSTATUS_UNAVAILABLE */

typedef enum {
    TAS_LK_PASS = 0,          /* the answer this instance must get */
    TAS_LK_FAIL,              /* an observed wrong answer */
    TAS_LK_NOT_IMPLEMENTED,   /* the query (STATUS_NOT_IMPLEMENTED) or the store (lookup_result NOT_IMPLEMENTED) is missing:
                                 SKIP_NOT_IMPLEMENTED, nonpassing */
    TAS_LK_UNSUPPORTED,       /* another adapter without INTEL_CAP_TSN_TAS refused the query: no lookup happened there, so isolation
                                 is not exercised on it (SKIP_UNSUPPORTED, named as not exercised) */
    TAS_LK_INCONCLUSIVE       /* the measurement failed, or the reply does not show the instance's binding */
} TAS_LK_OUT;

/** The lookup bytes are all 0 */
static __inline BOOL tas_lk_record_zero(const AVB_TAS_OP_RECORD *r)
{
    const unsigned char *p = (const unsigned char *)r;
    size_t i;

    for (i = 0; i < sizeof(*r); i++) {
        if (p[i] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/** One lookup reply on one open instance.
 *  captured     the raw NTSTATUS was captured
 *  status, got  the raw NTSTATUS and IoStatus.Information
 *  q            the reply; read only with STATUS_SUCCESS
 *  luid         the adapter the instance was bound to
 *  tas          that adapter advertises INTEL_CAP_TSN_TAS
 *  originating  the instance is the operation's originating open instance, or a handle duplicated from it */
static __inline TAS_LK_OUT tas_lk_reply(BOOL captured, LONG status, DWORD got, const AVB_TAS_QUERY_HW_REQUEST *q, avb_u64 luid, BOOL tas,
                                        BOOL originating)
{
    if (!captured || status == (LONG)AVB_NTSTATUS_UNAVAILABLE) {
        return TAS_LK_INCONCLUSIVE;
    }
    if (status == (LONG)STATUS_NOT_SUPPORTED) {
        /* capability absent: no lookup; capability present (or the operation ran there): advertised, yet refused */
        return (tas || originating) ? TAS_LK_FAIL : TAS_LK_UNSUPPORTED;
    }
    if (status == (LONG)STATUS_NOT_IMPLEMENTED) {
        return TAS_LK_NOT_IMPLEMENTED;
    }
    if (status != (LONG)STATUS_SUCCESS) {
        return TAS_LK_INCONCLUSIVE;   /* the measurement failed; no answer to judge */
    }
    if (got != (DWORD)sizeof(*q) || q->version != AVB_TAS_QUERY_VERSION || q->size != (avb_u32)sizeof(*q)) {
        return TAS_LK_FAIL;           /* a success without the complete reply */
    }
    if (q->net_luid != luid) {
        return TAS_LK_INCONCLUSIVE;   /* the reply is not shown to be this instance's adapter */
    }
    switch (q->lookup_result) {
    case AVB_TAS_LOOKUP_FOUND:
        return originating ? TAS_LK_PASS : TAS_LK_FAIL;
    case AVB_TAS_LOOKUP_NOT_FOUND:
        if (!tas_lk_record_zero(&q->lookup)) {
            return TAS_LK_FAIL;       /* U1: record bytes without FOUND */
        }
        return originating ? TAS_LK_FAIL : TAS_LK_PASS;
    case AVB_TAS_LOOKUP_NOT_IMPLEMENTED:
        return tas_lk_record_zero(&q->lookup) ? TAS_LK_NOT_IMPLEMENTED : TAS_LK_FAIL;
    default:
        return TAS_LK_FAIL;           /* NOT_REQUESTED for a non-zero id, 0 or an undefined value */
    }
}

/* Record fields compared with what the test sent and observed */
#define TAS_LK_F_ID       0x01u   /* client_op_id */
#define TAS_LK_F_LUID     0x02u   /* net_luid */
#define TAS_LK_F_EPOCH    0x04u   /* context_epoch */
#define TAS_LK_F_GEN      0x08u   /* generation_after */
#define TAS_LK_F_RESULT   0x10u   /* result */
#define TAS_LK_F_NTSTATUS 0x20u   /* ntstatus */

typedef struct {
    unsigned mismatch;     /* TAS_LK_F_*: compared and different */
    unsigned unmeasured;   /* TAS_LK_F_*: the expected value is not established, so the field is not compared */
} TAS_LK_CONTENT;

/** The record found for the operation against the request and the independent observations.
 *  id, luid    the request's client_op_id and net_luid
 *  epoch, gen  the context_epoch and expected_generation the request carried; 0 = not reported, never compared
 *  captured    the completion's raw NTSTATUS was captured; status is that value
 *  wrote       the write attempt is established: the query after it is in the same context with the generation + 1
 *  restored    FAILED_RESTORED is established independently: status STATUS_IO_DEVICE_ERROR with no output, every restoration-set
 *              register measured as before and the record CLEAN */
static __inline TAS_LK_CONTENT tas_lk_content(const AVB_TAS_OP_RECORD *r, avb_u64 id, avb_u64 luid, avb_u64 epoch, avb_u64 gen,
                                              BOOL captured, LONG status, BOOL wrote, BOOL restored)
{
    TAS_LK_CONTENT c = { 0u, 0u };

    if (r->client_op_id != id) {
        c.mismatch |= TAS_LK_F_ID;
    }
    if (r->net_luid != luid) {
        c.mismatch |= TAS_LK_F_LUID;
    }
    if (epoch == 0) {
        c.unmeasured |= TAS_LK_F_EPOCH;
    } else if (r->context_epoch != epoch) {
        c.mismatch |= TAS_LK_F_EPOCH;
    }
    if (gen == 0 || !wrote) {
        c.unmeasured |= TAS_LK_F_GEN;
    } else if (r->generation_after != gen + 1u) {
        c.mismatch |= TAS_LK_F_GEN;
    }
    if (!restored) {
        c.unmeasured |= TAS_LK_F_RESULT;
    } else if (r->result != AVB_TAS_ACT_FAILED_RESTORED) {
        c.mismatch |= TAS_LK_F_RESULT;
    }
    if (!captured || status == (LONG)AVB_NTSTATUS_UNAVAILABLE) {
        c.unmeasured |= TAS_LK_F_NTSTATUS;
    } else if ((LONG)r->ntstatus != status) {
        c.mismatch |= TAS_LK_F_NTSTATUS;
    }
    return c;
}

/** CONTENT verdict: a compared difference is FAIL, whatever else is missing; otherwise a missing expected value is INCONCLUSIVE */
static __inline TAS_LK_OUT tas_lk_content_out(TAS_LK_CONTENT c)
{
    if (c.mismatch != 0) {
        return TAS_LK_FAIL;
    }
    return c.unmeasured != 0 ? TAS_LK_INCONCLUSIVE : TAS_LK_PASS;
}

#endif /* TAS_LOOKUP_EVAL_H */
