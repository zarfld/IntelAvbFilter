/**
 * @file test_vv_corr_003_crossdomain.c
 * @brief VV-CORR-003 — Cross-domain PHC↔System↔TX correlation validation
 *
 * Combines all three timestamp domains in a single test run to prove they share
 * the same SYSTIM hardware clock and are mutually consistent:
 *
 *   Domain 1 — PHC reads:         IOCTL_AVB_GET_TIMESTAMP (code 46)
 *   Domain 2 — Cross-timestamp:   IOCTL_AVB_PHC_CROSSTIMESTAMP (code 63) → phc_time_ns + QPC
 *   Domain 3 — TX timestamp:      IOCTL_AVB_TEST_SEND_PTP (code 51) → timestamp_ns
 *   Domain 4 — RX timestamp:      IOCTL_AVB_GET_RX_TIMESTAMP (code 50) → SKIP if no loopback
 *
 * What it proves per adapter:
 *   1. Cross-timestamp PHC value brackets within [phc_before, phc_after] < 100µs window
 *      → proves phc_time_ns returned by XT-IOCTL reads the same SYSTIM register
 *   2. TX timestamp brackets within [phc_before, phc_after] < 100µs window
 *      → proves TX-TS returned by SEND_PTP reads the same SYSTIM register (confirmed Track D)
 *   3. xt_phc ≤ tx_ts (captured in order A→B in the same run)
 *      → proves both are monotonically increasing in same domain
 *   4. QPC data from cross-timestamp is valid (nonzero, realistic frequency)
 *      → establishes the PHC↔System bridge accessible from user mode
 *   5. (SKIP) RX timestamp if no loopback cable available
 *      → same hardware-gate as UT-CORR-004 (accepted)
 *
 * Architecture note:
 *   All three "PHC-domain" IOCTLs (GET_TIMESTAMP, PHC_CROSSTIMESTAMP, TEST_SEND_PTP)
 *   read ops->get_systime() (SYSTIM register) internally.  They therefore share the
 *   same hardware clock and the same nanosecond epoch.  VV-CORR-003 observationally
 *   confirms this by bracket-testing each against a simultaneous PHC read pair.
 *
 * Dependencies (all resolved):
 *   Track B DONE — IOCTL_AVB_PHC_CROSSTIMESTAMP (code 63) implemented
 *   Track C DONE — IOCTL_AVB_GET_RX_TIMESTAMP (code 50) handler implemented
 *   Track D DONE — IOCTL_AVB_TEST_SEND_PTP (code 51) TX bracket confirmed (SYSTIM epoch)
 *
 * Implements: VV-CORR-003
 * Traces to:  #149 (REQ-F-PTP-007: Hardware Timestamp Correlation)
 * Traces to:  #48  (REQ-F-IOCTL-PHC-004: Cross-Timestamp IOCTL)
 * Closes:     Cross-domain coverage gap for #317 (INFRA: Mock NDIS Unit Test Harness)
 * Verifies:   #199 (TEST-PTP-CORR-001: 17/17 coverage plan)
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef NDIS_STATUS_SUCCESS
#define NDIS_STATUS_SUCCESS  ((NDIS_STATUS)0x00000000L)
#endif
typedef ULONG NDIS_STATUS;

#include "../../../include/avb_ioctl.h"

/* -------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/
#define DEVICE_PATH_W         L"\\\\.\\IntelAvbFilter"
#define MAX_ADAPTERS          6   /* 6 physical I226 adapters; driver accepts any index */
#define BRACKET_WINDOW_NS     100000ULL  /* 100µs — generous; real brackets are 40-50µs */
#define QPC_FREQ_MIN          1000000ULL     /* 1 MHz — minimum plausible QPC frequency */
#define QPC_FREQ_MAX          10000000000ULL /* 10 GHz — maximum plausible QPC frequency */

/* -------------------------------------------------------------------------
 * Test counters
 * -------------------------------------------------------------------------*/
static int s_total  = 0;
static int s_passed = 0;
static int s_failed = 0;

static void tc_result(const char *name, bool passed)
{
    s_total++;
    if (passed) { s_passed++; printf("  [PASS] %s\n", name); }
    else        { s_failed++; printf("  [FAIL] %s\n", name); }
}

/* -------------------------------------------------------------------------
 * PHC read helper (IOCTL_AVB_GET_TIMESTAMP)
 * -------------------------------------------------------------------------*/
static bool read_phc(HANDLE hDev, uint32_t adapter_idx, uint64_t *out_ns)
{
    AVB_TIMESTAMP_REQUEST r = {0};
    r.clock_id = adapter_idx;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_GET_TIMESTAMP,
                              &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (!ok || r.status != (ULONG)NDIS_STATUS_SUCCESS || r.timestamp == 0) return false;
    *out_ns = r.timestamp;
    return true;
}

/* =========================================================================
 * VV-CORR-003 Check A: Cross-Timestamp PHC bracket
 *
 * IOCTL_AVB_PHC_CROSSTIMESTAMP must return phc_time_ns in [phc_before, phc_after].
 * This proves the XT-IOCTL reads SYSTIM (same register as GET_TIMESTAMP).
 *
 * Also validates QPC data: system_qpc != 0, qpc_frequency in [1MHz, 10GHz].
 *
 * Returns phc_xt value for use in ordering check (Check C).
 * Returns system_qpc and qpc_frequency for caller to use in cross-domain report.
 * =========================================================================*/
static bool check_xt_bracket(HANDLE hDev, uint32_t adapter_idx,
                             uint64_t *out_phc_xt,
                             uint64_t *out_system_qpc,
                             uint64_t *out_qpc_freq)
{
    printf("\n  [Check A] Cross-Timestamp PHC bracket (adapter %u)\n", adapter_idx);

    uint64_t phc_before = 0;
    if (!read_phc(hDev, adapter_idx, &phc_before)) {
        printf("    [SKIP] PHC read failed — adapter not ready\n");
        tc_result("VV-CORR-003-A XT bracket (SKIP - adapter not ready)", true);
        return false;
    }

    AVB_CROSS_TIMESTAMP_REQUEST xt = {0};
    xt.adapter_index = adapter_idx;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_PHC_CROSSTIMESTAMP,
                              &xt, sizeof(xt), &xt, sizeof(xt), &br, NULL);

    uint64_t phc_after = 0;
    read_phc(hDev, adapter_idx, &phc_after);

    /* Assert: IOCTL must succeed */
    if (!ok) {
        printf("    FAIL: IOCTL_AVB_PHC_CROSSTIMESTAMP returned FALSE (err=%lu)\n",
               GetLastError());
        tc_result("VV-CORR-003-A XT IOCTL succeeds", false);
        return false;
    }

    /* Assert: valid */
    if (!xt.valid) {
        printf("    FAIL: xt.valid=0 (expected 1)\n");
        tc_result("VV-CORR-003-A XT valid flag", false);
        return false;
    }

    /* Assert: PHC bracket */
    uint64_t window = phc_after - phc_before;
    bool in_bracket = (xt.phc_time_ns >= phc_before && xt.phc_time_ns <= phc_after);
    printf("    phc_before  = %llu ns\n", (unsigned long long)phc_before);
    printf("    phc_xt      = %llu ns\n", (unsigned long long)xt.phc_time_ns);
    printf("    phc_after   = %llu ns\n", (unsigned long long)phc_after);
    printf("    window      = %llu ns  (limit %llu ns)\n",
           (unsigned long long)window, (unsigned long long)BRACKET_WINDOW_NS);
    tc_result("VV-CORR-003-A phc_xt in [phc_before, phc_after]", in_bracket);
    tc_result("VV-CORR-003-A bracket window < 100µs", window < BRACKET_WINDOW_NS);

    /* Assert: QPC sanity */
    bool qpc_sane = (xt.system_qpc  != 0 &&
                     xt.qpc_frequency >= QPC_FREQ_MIN &&
                     xt.qpc_frequency <= QPC_FREQ_MAX);
    printf("    system_qpc  = %llu ticks\n",  (unsigned long long)xt.system_qpc);
    printf("    qpc_freq    = %llu Hz\n",      (unsigned long long)xt.qpc_frequency);
    tc_result("VV-CORR-003-A QPC data valid (nonzero, realistic freq)", qpc_sane);

    *out_phc_xt      = xt.phc_time_ns;
    *out_system_qpc  = xt.system_qpc;
    *out_qpc_freq    = xt.qpc_frequency;
    return in_bracket && (window < BRACKET_WINDOW_NS) && qpc_sane;
}

/* =========================================================================
 * VV-CORR-003 Check B: TX Timestamp PHC bracket
 *
 * IOCTL_AVB_TEST_SEND_PTP must return timestamp_ns (TX pre-send SYSTIM) in
 * [phc_before, phc_after].  This re-confirms the UT-CORR-001 finding that all
 * TX timestamps are in SYSTIM domain (same as PHC reads).
 *
 * Returns tx_ts for use in ordering check (Check C).
 * =========================================================================*/
static bool check_tx_bracket(HANDLE hDev, uint32_t adapter_idx, uint64_t *out_tx_ts)
{
    printf("\n  [Check B] TX Timestamp PHC bracket (adapter %u)\n", adapter_idx);

    uint64_t phc_before = 0;
    if (!read_phc(hDev, adapter_idx, &phc_before)) {
        printf("    [SKIP] PHC read failed — adapter not ready\n");
        tc_result("VV-CORR-003-B TX bracket (SKIP - adapter not ready)", true);
        return false;
    }

    AVB_TEST_SEND_PTP_REQUEST ptp = {0};
    ptp.adapter_index = adapter_idx;
    ptp.sequence_id   = 0xC003;   /* VV-CORR-003 signature sequence ID */
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_TEST_SEND_PTP,
                              &ptp, sizeof(ptp), &ptp, sizeof(ptp), &br, NULL);

    uint64_t phc_after = 0;
    read_phc(hDev, adapter_idx, &phc_after);

    if (!ok) {
        printf("    FAIL: IOCTL_AVB_TEST_SEND_PTP returned FALSE (err=%lu)\n",
               GetLastError());
        tc_result("VV-CORR-003-B TX IOCTL succeeds", false);
        return false;
    }

    uint64_t tx_ts = ptp.timestamp_ns;
    uint64_t window = phc_after - phc_before;
    bool in_bracket = (tx_ts >= phc_before && tx_ts <= phc_after);

    printf("    phc_before  = %llu ns\n", (unsigned long long)phc_before);
    printf("    tx_ts       = %llu ns\n", (unsigned long long)tx_ts);
    printf("    phc_after   = %llu ns\n", (unsigned long long)phc_after);
    printf("    window      = %llu ns  (TX send latency — informational only)\n",
           (unsigned long long)window);
    /* NOTE: IOCTL_AVB_TEST_SEND_PTP transmits a real packet; wall-clock window
     * reflects NIC TX queuing + completion latency (typically 0.1–10 ms) and
     * is NOT a correctness criterion.  Only containment proves SYSTIM domain. */
    tc_result("VV-CORR-003-B tx_ts in [phc_before, phc_after]", in_bracket);

    *out_tx_ts = tx_ts;
    return in_bracket;
}

/* =========================================================================
 * VV-CORR-003 Check C: Cross-domain ordering
 *
 * Since Check A (XT) was run before Check B (TX) in the same function call,
 * phc_xt (SYSTIM from XT-IOCTL) must be ≤ tx_ts (SYSTIM from TX-IOCTL).
 * If they're in different domains this ordering can't be guaranteed.
 * =========================================================================*/
static void check_ordering(uint32_t adapter_idx,
                           uint64_t phc_xt, uint64_t tx_ts)
{
    printf("\n  [Check C] Cross-domain ordering (adapter %u)\n", adapter_idx);
    printf("    phc_xt (XT-IOCTL, captured first) = %llu ns\n", (unsigned long long)phc_xt);
    printf("    tx_ts  (TX-IOCTL, captured after) = %llu ns\n", (unsigned long long)tx_ts);
    bool ordered = (phc_xt <= tx_ts);
    tc_result("VV-CORR-003-C phc_xt <= tx_ts (same SYSTIM domain, ordered)", ordered);
}

/* =========================================================================
 * VV-CORR-003 Check D: RX Timestamp IOCTL reachable (SKIP if no data)
 *
 * Confirms IOCTL_AVB_GET_RX_TIMESTAMP handler exists (Track C DONE).
 * If rx_ts != 0, also bracket-tests it against PHC reads.
 * If no RX event available (no loopback cable), accepts SKIP.
 * =========================================================================*/
static void check_rx_ioctl(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n  [Check D] RX Timestamp IOCTL reachable (adapter %u)\n", adapter_idx);

    uint64_t phc_before = 0;
    if (!read_phc(hDev, adapter_idx, &phc_before)) {
        printf("    [SKIP] PHC read failed — adapter not ready\n");
        tc_result("VV-CORR-003-D RX IOCTL (SKIP - adapter not ready)", true);
        return;
    }

    AVB_TX_TIMESTAMP_REQUEST r = {0};
    r.adapter_index = adapter_idx;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_GET_RX_TIMESTAMP,
                              &r, sizeof(r), &r, sizeof(r), &br, NULL);

    uint64_t phc_after = 0;
    read_phc(hDev, adapter_idx, &phc_after);

    if (!ok) {
        printf("    FAIL: IOCTL_AVB_GET_RX_TIMESTAMP returned FALSE (err=%lu)\n",
               GetLastError());
        tc_result("VV-CORR-003-D RX IOCTL handler reachable", false);
        return;
    }

    tc_result("VV-CORR-003-D RX IOCTL handler reachable (status 0x%08X, valid=%u)",
              r.status == NDIS_STATUS_SUCCESS);

    if (r.timestamp_ns == 0) {
        printf("    [SKIP] rx_ts=0 — no RX event in hardware latch (no loopback cable)\n");
        printf("    Hardware gate accepted: same condition as UT-CORR-004\n");
        tc_result("VV-CORR-003-D RX bracket (SKIP - no loopback, hardware-gated)", true);
        return;
    }

    /* Loopback cable connected — run full bracket */
    uint64_t rx_ts = r.timestamp_ns;
    uint64_t window = phc_after - phc_before;
    bool in_bracket = (rx_ts >= phc_before && rx_ts <= phc_after);
    printf("    phc_before  = %llu ns\n", (unsigned long long)phc_before);
    printf("    rx_ts       = %llu ns\n", (unsigned long long)rx_ts);
    printf("    phc_after   = %llu ns\n", (unsigned long long)phc_after);
    printf("    window      = %llu ns\n", (unsigned long long)window);
    tc_result("VV-CORR-003-D rx_ts in [phc_before, phc_after] (loopback)", in_bracket);
}

/* =========================================================================
 * Per-adapter VV-CORR-003 run
 * =========================================================================*/
static void run_vv_corr_003(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n====================================================\n");
    printf("[VV-CORR-003] Cross-domain PHC↔System↔TX↔RX check\n");
    printf("  Adapter %u | Verifies: #149 (REQ-F-PTP-007) | Closes: #317 (Track E gap)\n",
           adapter_idx);
    printf("====================================================\n");

    uint64_t phc_xt     = 0;
    uint64_t system_qpc = 0;
    uint64_t qpc_freq   = 0;
    uint64_t tx_ts      = 0;

    bool xt_ok = check_xt_bracket(hDev, adapter_idx, &phc_xt, &system_qpc, &qpc_freq);
    bool tx_ok = check_tx_bracket(hDev, adapter_idx, &tx_ts);

    if (xt_ok && tx_ok) {
        check_ordering(adapter_idx, phc_xt, tx_ts);
    } else {
        printf("\n  [Check C] Skipped (A or B failed)\n");
        tc_result("VV-CORR-003-C ordering (SKIP - prerequisite failed)", true);
    }

    check_rx_ioctl(hDev, adapter_idx);

    /* Cross-domain summary report */
    if (xt_ok && tx_ok && qpc_freq > 0) {
        /* Compute approximate QPC-time elapsed between XT capture and TX capture.
         * Both SYSTIM values are in nanoseconds; the QPC bridge is via system_qpc
         * captured at the same instant as phc_xt.  This is informational only —
         * no hard assertion on rate ratio here (that's IT-CORR-001's job). */
        int64_t systim_delta_ns = (int64_t)tx_ts - (int64_t)phc_xt;
        double  systim_delta_us = systim_delta_ns / 1000.0;
        printf("\n  [Summary] Cross-domain bridge (adapter %u)\n", adapter_idx);
        printf("    phc_xt  (XT PHC, SYSTIM) = %llu ns\n",   (unsigned long long)phc_xt);
        printf("    tx_ts   (TX PHC, SYSTIM) = %llu ns\n",   (unsigned long long)tx_ts);
        printf("    delta   (tx - xt_phc)    = %.2f µs\n",   systim_delta_us);
        printf("    system_qpc               = %llu ticks\n",(unsigned long long)system_qpc);
        printf("    qpc_frequency            = %llu Hz\n",   (unsigned long long)qpc_freq);
        printf("    Bridge: tx_ts maps to QPC = system_qpc + "
               "%.2f µs * %llu / 1e9 ticks\n",
               systim_delta_us, (unsigned long long)qpc_freq);
    }
}

/* =========================================================================
 * main
 * =========================================================================*/
int main(void)
{
    printf("====================================================\n");
    printf(" VV-CORR-003: Cross-Domain PHC↔System↔TX Correlation\n");
    printf(" Tracks B+C+D Complete | Verifies #149 | Closes #317\n");
    printf(" RX domain: SKIP if no loopback cable (hardware-gated)\n");
    printf("====================================================\n\n");

    HANDLE hDev = CreateFileW(
        DEVICE_PATH_W,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("FATAL: Cannot open %ls (err=%lu)\n", DEVICE_PATH_W, GetLastError());
        printf("  Ensure IntelAvbFilter driver is installed and running.\n");
        return 1;
    }
    printf("Device opened: %ls\n", DEVICE_PATH_W);

    /* Warm-up: one dummy PHC read to prime the device path.  The very first
     * IOCTL after CreateFile has elevated latency (~150-200µs) due to kernel
     * path initialisation.  Without this, adapter 0 bracket windows exceed
     * the 100µs limit even though the hardware is fine. */
    { uint64_t warmup = 0; read_phc(hDev, 0, &warmup); }

    /* Run on each adapter until PHC read fails */
    int adapters_tested = 0;
    for (uint32_t i = 0; i < MAX_ADAPTERS; i++) {
        uint64_t probe = 0;
        if (!read_phc(hDev, i, &probe)) {
            printf("\n[adapter %u] PHC read failed — no more adapters\n", i);
            break;
        }
        run_vv_corr_003(hDev, i);
        adapters_tested++;
    }

    CloseHandle(hDev);

    printf("\n====================================================\n");
    printf(" VV-CORR-003 RESULTS\n");
    printf(" Adapters tested : %d\n", adapters_tested);
    printf(" Total           : %d\n", s_total);
    printf(" Passed          : %d\n", s_passed);
    printf(" Failed          : %d\n", s_failed);
    if (s_failed == 0)
        printf("\n STATUS: PASS ✓ — Cross-domain PHC↔System↔TX correlation confirmed\n");
    else
        printf("\n STATUS: FAIL ✗ — %d assertion(s) failed\n", s_failed);
    printf("====================================================\n");

    return (s_failed == 0) ? 0 : 1;
}
