/* ptp_phc_common.h - shared helpers of test_ptp_phc_stability.c (UT-CORR-005..008) and test_ptp_phc_lifecycle.c
 * (UT-CORR-009).  Moved verbatim out of test_ptp_phc_stability.c when UT-CORR-009 was split into its own executable
 * (Q 2026-10-22, TEST-HILSUITE-REAL-003).  No process creation, no service control in this header. */
#ifndef PTP_PHC_COMMON_H
#define PTP_PHC_COMMON_H
#pragma warning(disable: 4505)   /* each executable uses a subset of the static helpers */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#define _USE_MATH_DEFINES
#include <math.h>    /* sqrt */

#ifndef NDIS_STATUS_SUCCESS
#define NDIS_STATUS_SUCCESS  ((NDIS_STATUS)0x00000000L)
#endif
typedef ULONG NDIS_STATUS;

#include "../../../include/avb_ioctl.h"
#include "../../common/avb_test_common.h"   /* I210 TIMINCA oracle (datasheet 8.15.11), verified restore (#343) */

/* -------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/
#define DEVICE_PATH_W       L"\\\\.\\IntelAvbFilter"
#define DELTA_1US_NS        1000ULL     /* 1 microsecond in nanoseconds */
#define DELTA_JITTER_NS     100ULL      /* stddev threshold per issue #199 (100 ns) */
#define JITTER_SAMPLES      1000        /* sample count for UT-CORR-007 */
#define BURST_COUNT         100         /* burst packet count for UT-CORR-008 */
#define TX_RETRY_COUNT      10          /* retries to poll TX timestamp FIFO */
#define TX_RETRY_SLEEP_MS   2           /* ms between TX FIFO read retries */
#define SERVICE_NAME        "IntelAvbFilter"

/* -------------------------------------------------------------------------
 * Test result counters
 * -------------------------------------------------------------------------*/
static int s_total    = 0;
static int s_passed   = 0;
static int s_failed   = 0;
static int s_skipped  = 0;  /* SKIP: hardware not ready, capability absent */
static int s_cleanup_failed = 0;  /* cleanup FAILED: restore step failed (not a testcase verdict, not in the exit code) */

static void tc_result(const char *name, bool passed)
{
    s_total++;
    if (passed) { s_passed++; printf("  [PASS] %s\n", name); }
    else        { s_failed++; printf("  [FAIL] %s\n", name); }
}

/* Use tc_skip instead of tc_result(name, true) for non-applicable conditions.
 * SKIP is distinct from PASS in the summary and does not contribute to s_passed. */
static void tc_skip(const char *reason)
{
    s_total++;
    s_skipped++;
    printf("  [SKIP] %s\n", reason);
}

/* BLOCKED = unmet safety prerequisite; a failure-class verdict (docs/testing/test-verdict-contract.md), never PASS/SKIP. */
static void tc_blocked(const char *reason)
{
    s_total++;
    s_failed++;
    printf("  [BLOCKED] %s\n", reason);
}

/* FAILED_SETUP = the test's precondition was not established; a failure-class verdict, never PASS/SKIP.  Used only
 * BEFORE the test's first state-modifying IOCTL: the line claims "no state modified", which Run-Tests-CI.ps1 verifies
 * independently (adapter/TIMINCA/TSAUXC equality, PHC timeline continuity) before it treats the run as no mutation. */
static void tc_failed_setup(const char *id, uint32_t adapter_idx, const char *why)
{
    s_total++;
    s_failed++;
    printf("  [FAILED_SETUP] %s/adapter:%u: %s --- no state modified\n", id, adapter_idx, why);
}

/* -------------------------------------------------------------------------
 * QPC helper — user-mode QueryPerformanceCounter wrapper
 * -------------------------------------------------------------------------*/
static LARGE_INTEGER QPC(LARGE_INTEGER *freq)
{
    LARGE_INTEGER qpc = {0};
    if (freq) QueryPerformanceFrequency(freq);
    QueryPerformanceCounter(&qpc);
    return qpc;
}

/* Convert QPC tick delta to nanoseconds */
static uint64_t qpc_delta_to_ns(LARGE_INTEGER t0, LARGE_INTEGER t1, LARGE_INTEGER freq)
{
    int64_t ticks = t1.QuadPart - t0.QuadPart;
    if (ticks <= 0 || freq.QuadPart == 0) return 0;
    return (uint64_t)((double)ticks / (double)freq.QuadPart * 1e9);
}

/* -------------------------------------------------------------------------
 * PHC read helper
 * -------------------------------------------------------------------------*/
static bool read_phc(HANDLE hDev, uint32_t adapter_idx, uint64_t *out_ns)
{
    AVB_TIMESTAMP_REQUEST r = {0};
    r.clock_id = adapter_idx;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_GET_TIMESTAMP,
                              &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (!ok || r.status != NDIS_STATUS_SUCCESS || r.timestamp == 0) return false;
    *out_ns = r.timestamp;
    return true;
}

/* -------------------------------------------------------------------------
 * Open device helper — FILE_SHARE_READ|WRITE allows concurrent handles from
 * the same process (needed for per-adapter handle isolation tests).
 * -------------------------------------------------------------------------*/
static HANDLE open_device(void)
{
    return CreateFileW(
        DEVICE_PATH_W,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
}

/* -------------------------------------------------------------------------
 * TX timestamp helper — poll hardware FIFO with retry
 *
 * Returns true if a valid TX timestamp is available.  The FIFO may not be
 * populated immediately after IOCTL_AVB_TEST_SEND_PTP if the miniport has
 * not yet physically transmitted the frame, so we retry up to TX_RETRY_COUNT
 * times with TX_RETRY_SLEEP_MS between attempts.
 * -------------------------------------------------------------------------*/
static bool get_tx_timestamp_retry(HANDLE hDev, uint32_t adapter_idx, uint64_t *out_ns)
{
    int attempt;
    for (attempt = 0; attempt < TX_RETRY_COUNT; attempt++) {
        if (attempt > 0) Sleep(TX_RETRY_SLEEP_MS);
        AVB_TX_TIMESTAMP_REQUEST r = {0};
        r.adapter_index = adapter_idx;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_GET_TX_TIMESTAMP,
                                  &r, sizeof(r), &r, sizeof(r), &br, NULL);
        if (!ok) return false;                  /* IOCTL dispatch error */
        if (r.status != NDIS_STATUS_SUCCESS) return false;
        if (r.valid && r.timestamp_ns != 0) {
            *out_ns = r.timestamp_ns;
            return true;
        }
    }
    return false;  /* FIFO empty after all retries (link down or HW ts disabled) */
}

/* -------------------------------------------------------------------------
 * Send PTP test packet and return provenance-tagged result.
 *
 * Injects a PTP Sync frame via IOCTL_AVB_TEST_SEND_PTP (kernel-mode).
 * Both timestamp_ns (pre-send) and phc_at_send_ns (IOCTL-entry) are now
 * DISTINCT snapshots; their delta represents kernel IOCTL setup overhead
 * (~100-500 ns) NOT hardware egress latency.
 *
 * ts_provenance is always PRE_SEND_PHC or SOFTWARE_FALLBACK — never
 * VERIFIED_HARDWARE_TX.  Callers that need hardware TX-PHC correlation
 * must use IOCTL_AVB_GET_TX_TIMESTAMP after the send and verify that
 * ts_provenance == AVB_TX_PROV_VERIFIED_HARDWARE_TX before claiming PASS.
 *
 * Returns false if the IOCTL fails or packets_sent == 0.
 * -------------------------------------------------------------------------*/
static bool send_ptp_get_tx(HANDLE hDev, uint32_t adapter_idx,
                             uint32_t seq_id, uint64_t *out_tx_ns,
                             uint64_t *out_phc_ns)  /* may be NULL */
{
    AVB_TEST_SEND_PTP_REQUEST send_req = {0};
    send_req.adapter_index = adapter_idx;
    send_req.sequence_id   = seq_id;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_TEST_SEND_PTP,
                              &send_req, sizeof(send_req),
                              &send_req, sizeof(send_req), &br, NULL);
    if (!ok || send_req.status != NDIS_STATUS_SUCCESS || send_req.packets_sent == 0)
        return false;
    if (out_phc_ns) *out_phc_ns = send_req.phc_at_send_ns;  /* IOCTL-entry PHC snapshot */
    *out_tx_ns = send_req.timestamp_ns;                       /* pre-send SYSTIM snapshot */
    return (*out_tx_ns > 0);
}

/* Returns true only when the SEND_PTP result carries provenance that confirms
 * it came from hardware SYSTIM (not QPC fallback).  Currently always returns
 * false because SEND_PTP never yields VERIFIED_HARDWARE_TX. */
static bool send_ptp_is_hw_verified(HANDLE hDev, uint32_t adapter_idx, uint32_t seq_id,
                                     uint64_t *out_tx_ns, uint64_t *out_phc_ns)
{
    AVB_TEST_SEND_PTP_REQUEST send_req = {0};
    send_req.adapter_index = adapter_idx;
    send_req.sequence_id   = seq_id;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_TEST_SEND_PTP,
                              &send_req, sizeof(send_req),
                              &send_req, sizeof(send_req), &br, NULL);
    if (!ok || send_req.status != NDIS_STATUS_SUCCESS || send_req.packets_sent == 0)
        return false;
    if (out_phc_ns) *out_phc_ns = send_req.phc_at_send_ns;
    if (out_tx_ns)  *out_tx_ns  = send_req.timestamp_ns;
    /* Only VERIFIED_HARDWARE_TX qualifies for TX-PHC correlation claims.
     * PRE_SEND_PHC and SOFTWARE_FALLBACK always return false here. */
    return (send_req.ts_provenance == AVB_TX_PROV_VERIFIED_HARDWARE_TX);
}


#endif /* PTP_PHC_COMMON_H */
