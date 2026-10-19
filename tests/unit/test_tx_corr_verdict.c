/*
 * test_tx_corr_verdict.c
 * Offline characterization of the TC-CORR-TX-RX-001 per-sample verdict.
 *
 * Defect being fixed: the integration test used to call test_result(..., true) when
 * IOCTL_AVB_TEST_SEND_PTP failed or when no hardware TX timestamp arrived, so a missing or
 * broken hardware TX path was reported as PASS ("skipped - no HW TX"), and it never looked at
 * ts_provenance.  Contract: docs/testing/test-verdict-contract.md.
 *
 * Verifies: #35 (REQ-F-IOCTL-TS-001), #199 (TX/RX PHC correlation), #340
 * No driver, no hardware.
 *
 * Build: cl /nologo /W4 /Zi -I include -I external/intel_avb/lib tests\unit\test_tx_corr_verdict.c
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../integration/tx_timestamp/tx_corr_verdict.h"

static int g_run = 0, g_fail = 0;
#define CHECK_V(label, got, want) do { g_run++; \
    if ((got) == (want)) printf("  [PASS] %s\n", (label)); \
    else { g_fail++; printf("  [FAIL] %s: got=%s want=%s\n", (label), avb_verdict_tag(got), avb_verdict_tag(want)); } } while (0)

/* A fully healthy observation: send ok, timestamp valid, verified hardware provenance, 1.2 us delta. */
static TX_CORR_OBS good(AVB_CAP_EVIDENCE cap)
{
    TX_CORR_OBS o;
    memset(&o, 0, sizeof(o));
    o.send_ntstatus = STATUS_SUCCESS;  o.send_status_field = 0;
    o.ts_ntstatus   = STATUS_SUCCESS;  o.ts_valid = 1;
    o.ts_provenance = AVB_TX_PROV_VERIFIED_HARDWARE_TX;
    o.hw_ts_ns = 1000000200ULL; o.phc_at_send_ns = 1000000000ULL;   /* delta 200 ns */
    o.cap = cap;
    return o;
}

int main(void)
{
    const char *why = NULL;
    TX_CORR_OBS o;
    printf("=== TC-CORR-TX-RX-001 verdict characterization (offline) ===\n");
    printf("Verifies: #35, #199, #340\n");

    printf("\n[T-001] healthy verified hardware TX timestamp => PASS\n");
    o = good(AVB_CAP_PRESENT);
    CHECK_V("verified provenance + small delta", tx_corr_classify(&o, &why), AVB_VERDICT_PASS);

    printf("\n[T-002] ts_provenance weaker than VERIFIED_HARDWARE_TX on a PTP-capable adapter => FAIL (was PASS/SKIP)\n");
    o = good(AVB_CAP_PRESENT); o.ts_provenance = AVB_TX_PROV_PRE_SEND_PHC;
    CHECK_V("PRE_SEND_PHC", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);
    o = good(AVB_CAP_PRESENT); o.ts_provenance = AVB_TX_PROV_SOFTWARE_FALLBACK;
    CHECK_V("SOFTWARE_FALLBACK", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);
    o = good(AVB_CAP_PRESENT); o.ts_provenance = AVB_TX_PROV_UNAVAILABLE;
    CHECK_V("UNAVAILABLE", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);

    printf("\n[T-003] no hardware TX timestamp within the poll window => FAIL (was PASS 'skipped')\n");
    o = good(AVB_CAP_PRESENT); o.ts_valid = 0;
    CHECK_V("valid=0, success status", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);

    printf("\n[T-004] zero hardware timestamp => FAIL even with verified provenance\n");
    o = good(AVB_CAP_PRESENT); o.hw_ts_ns = 0;
    CHECK_V("hw_ts_ns == 0", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);

    printf("\n[T-005] delta beyond threshold => FAIL\n");
    o = good(AVB_CAP_PRESENT); o.hw_ts_ns = o.phc_at_send_ns + TX_CORR_DELTA_MAX_NS + 1;
    CHECK_V("delta = max + 1 ns", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);
    o = good(AVB_CAP_PRESENT); o.hw_ts_ns = o.phc_at_send_ns + TX_CORR_DELTA_MAX_NS;
    CHECK_V("delta = max ns is within threshold", tx_corr_classify(&o, &why), AVB_VERDICT_PASS);

    printf("\n[T-006] SEND_PTP failure modes\n");
    o = good(AVB_CAP_PRESENT); o.send_ntstatus = STATUS_NOT_IMPLEMENTED;
    CHECK_V("SEND_PTP NOT_IMPLEMENTED => SKIP_NOT_IMPLEMENTED", tx_corr_classify(&o, &why), AVB_VERDICT_SKIP_NOT_IMPLEMENTED);
    o = good(AVB_CAP_PRESENT); o.send_ntstatus = STATUS_NOT_SUPPORTED;
    CHECK_V("SEND_PTP NOT_SUPPORTED on a PTP-advertising adapter => FAIL", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);
    o = good(AVB_CAP_ABSENT); o.send_ntstatus = STATUS_NOT_SUPPORTED;
    CHECK_V("SEND_PTP NOT_SUPPORTED, capability proven absent => SKIP_UNSUPPORTED", tx_corr_classify(&o, &why), AVB_VERDICT_SKIP_UNSUPPORTED);
    o = good(AVB_CAP_PRESENT); o.send_ntstatus = STATUS_SUCCESS; o.send_status_field = 0xC0000001u;
    CHECK_V("SEND_PTP success status but failing status field => FAIL", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);
    o = good(AVB_CAP_PRESENT); o.send_ntstatus = STATUS_DEVICE_NOT_READY;
    CHECK_V("SEND_PTP DEVICE_NOT_READY => BLOCKED, never an unsupported skip", tx_corr_classify(&o, &why), AVB_VERDICT_BLOCKED);

    printf("\n[T-007] GET_TX_TIMESTAMP error statuses\n");
    o = good(AVB_CAP_PRESENT); o.ts_ntstatus = STATUS_NOT_IMPLEMENTED;
    CHECK_V("GET_TX_TIMESTAMP NOT_IMPLEMENTED => SKIP_NOT_IMPLEMENTED", tx_corr_classify(&o, &why), AVB_VERDICT_SKIP_NOT_IMPLEMENTED);
    o = good(AVB_CAP_PRESENT); o.ts_ntstatus = STATUS_INVALID_PARAMETER;
    CHECK_V("GET_TX_TIMESTAMP actual error => FAIL", tx_corr_classify(&o, &why), AVB_VERDICT_FAIL);

    printf("\n[T-008] capability unknown for this operation => INCONCLUSIVE, never PASS / unsupported skip\n");
    o = good(AVB_CAP_UNKNOWN); o.ts_valid = 0;
    CHECK_V("no timestamp, capability unknown", tx_corr_classify(&o, &why), AVB_VERDICT_INCONCLUSIVE);
    o = good(AVB_CAP_UNKNOWN); o.ts_provenance = AVB_TX_PROV_PRE_SEND_PHC;
    CHECK_V("weak provenance, capability unknown", tx_corr_classify(&o, &why), AVB_VERDICT_INCONCLUSIVE);
    o = good(AVB_CAP_UNKNOWN); o.send_ntstatus = STATUS_NOT_SUPPORTED;
    CHECK_V("NOT_SUPPORTED, capability unknown", tx_corr_classify(&o, &why), AVB_VERDICT_INCONCLUSIVE);

    printf("\n[T-009] capability evidence from the driver-advertised mask\n");
    CHECK_V("BASIC_1588 advertised => PRESENT (as FAIL on weak provenance)",
            (o = good(tx_corr_cap_from_driver_caps(INTEL_CAP_BASIC_1588)), o.ts_valid = 0, tx_corr_classify(&o, &why)),
            AVB_VERDICT_FAIL);
    CHECK_V("no PTP bits advertised => UNKNOWN (INCONCLUSIVE), not ABSENT",
            (o = good(tx_corr_cap_from_driver_caps(0)), o.ts_valid = 0, tx_corr_classify(&o, &why)),
            AVB_VERDICT_INCONCLUSIVE);

    printf("\n PASS: %d / %d\n FAIL: %d / %d\n", g_run - g_fail, g_run, g_fail, g_run);
    if (g_fail) { printf("[RESULT] FAILURE\n"); return 1; }
    printf("[RESULT] ALL TX CORRELATION VERDICT TESTS PASSED\n");
    return 0;
}
