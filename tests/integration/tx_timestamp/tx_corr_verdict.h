/*
 * tx_corr_verdict.h - per-sample verdict for TC-CORR-TX-RX-001 (pure, no I/O)
 *
 * Shared by test_tx_timestamp_retrieval.c (production, HIL) and tests/unit/test_tx_corr_verdict.c
 * (offline characterization).  Contract: docs/testing/test-verdict-contract.md.
 *
 * A hardware TX-PHC correlation claim requires ts_provenance == AVB_TX_PROV_VERIFIED_HARDWARE_TX.
 * Anything weaker, a missing timestamp, or a zero timestamp on an adapter that advertises PTP is a
 * FAIL - never a pass and never a "skipped - no HW TX".  When the operation's capability is not
 * known from an authoritative contract the result is INCONCLUSIVE.
 *
 * Verifies: #35 (REQ-F-IOCTL-TS-001), #199
 */
#pragma once

#include "../../common/avb_verdict.h"
#include "../../../include/avb_ioctl.h"

#define TX_CORR_DELTA_MAX_NS 5000ULL   /* 5 us: hardware-to-PHC expected jitter (unchanged) */

typedef struct {
    LONG     send_ntstatus;        /* exact NTSTATUS of IOCTL_AVB_TEST_SEND_PTP */
    avb_u32  send_status_field;    /* AVB_TEST_SEND_PTP_REQUEST.status (only meaningful on success) */
    LONG     ts_ntstatus;          /* exact NTSTATUS of the last IOCTL_AVB_GET_TX_TIMESTAMP */
    avb_u32  ts_valid;             /* AVB_TX_TIMESTAMP_REQUEST.valid of that call */
    avb_u32  ts_provenance;        /* AVB_TX_TIMESTAMP_REQUEST.ts_provenance */
    avb_u64  hw_ts_ns;             /* AVB_TX_TIMESTAMP_REQUEST.timestamp_ns */
    avb_u64  phc_at_send_ns;       /* PHC snapshot taken by the driver at send */
    AVB_CAP_EVIDENCE cap;          /* evidence that this adapter supports hardware TX timestamps */
} TX_CORR_OBS;

/*
 * Capability evidence for hardware TX timestamping from the driver-advertised mask.
 * INTEL_CAP_BASIC_1588 is the driver's PTP capability for the adapter; if it is advertised,
 * hardware PTP timestamping is part of the advertised interface contract, so a missing or
 * non-hardware TX timestamp is a contract failure (this includes I219, which advertises it).
 * No bit means "no authoritative contract", i.e. UNKNOWN - absence is never inferred.
 * (There is no dedicated TX-timestamp capability bit; adding one is a separate ABI decision.)
 */
static __inline AVB_CAP_EVIDENCE tx_corr_cap_from_driver_caps(avb_u32 caps)
{
    return (caps & INTEL_CAP_BASIC_1588) ? AVB_CAP_PRESENT : AVB_CAP_UNKNOWN;
}

/* Classify one send+retrieve observation. *why (optional) receives a short static reason. */
static __inline AVB_VERDICT tx_corr_classify(const TX_CORR_OBS *o, const char **why)
{
    AVB_VERDICT v;
    const char *w = "";
    avb_u64 delta;

    v = avb_verdict_from_status(o->send_ntstatus, o->cap, o->send_status_field == 0, 1);
    if (v != AVB_VERDICT_PASS) {
        w = "IOCTL_AVB_TEST_SEND_PTP did not complete successfully";
        goto done;
    }

    v = avb_verdict_from_status(o->ts_ntstatus, o->cap, 1, 1);
    if (v != AVB_VERDICT_PASS) {
        w = "IOCTL_AVB_GET_TX_TIMESTAMP did not complete successfully";
        goto done;
    }

    /* Both calls succeeded: judge the content. A missing/weak timestamp is a FAIL when the
     * adapter advertises PTP, otherwise the evidence is insufficient (INCONCLUSIVE). */
    if (!o->ts_valid) {
        v = (o->cap == AVB_CAP_PRESENT) ? AVB_VERDICT_FAIL : AVB_VERDICT_INCONCLUSIVE;
        w = "no hardware TX timestamp captured within the poll window";
        goto done;
    }
    if (o->ts_provenance != AVB_TX_PROV_VERIFIED_HARDWARE_TX) {
        v = (o->cap == AVB_CAP_PRESENT) ? AVB_VERDICT_FAIL : AVB_VERDICT_INCONCLUSIVE;
        w = "ts_provenance is weaker than VERIFIED_HARDWARE_TX (not an egress hardware timestamp)";
        goto done;
    }
    if (o->hw_ts_ns == 0) {
        v = AVB_VERDICT_FAIL;
        w = "hardware TX timestamp is zero";
        goto done;
    }

    delta = (o->hw_ts_ns > o->phc_at_send_ns) ? (o->hw_ts_ns - o->phc_at_send_ns)
                                              : (o->phc_at_send_ns - o->hw_ts_ns);
    if (delta > TX_CORR_DELTA_MAX_NS) {
        v = AVB_VERDICT_FAIL;
        w = "hardware TX timestamp too far from PHC at send";
        goto done;
    }
    v = AVB_VERDICT_PASS;
    w = "verified hardware TX timestamp within threshold";

done:
    if (why) *why = w;
    return v;
}
