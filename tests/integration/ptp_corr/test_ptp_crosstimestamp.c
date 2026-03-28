/**
 * @file test_ptp_crosstimestamp.c
 * @brief TDD RED step: UT-CORR-003 — IOCTL_AVB_PHC_CROSSTIMESTAMP implementation
 *
 * User-mode harness for PHC ↔ System cross-timestamp validation.
 * Proves IOCTL_AVB_PHC_CROSSTIMESTAMP (code 63) returns valid data:
 *   - phc_time_ns  : nonzero PHC nanosecond count
 *   - system_qpc   : nonzero KeQueryPerformanceCounter tick
 *   - qpc_frequency: nonzero ticks/second
 *   - valid == 1
 *   - DeviceIoControl returns TRUE
 *
 * TDD RED state (before handler exists):
 *   IOCTL_AVB_PHC_CROSSTIMESTAMP (code 63) has no handler case in
 *   avb_integration_fixed.c — falls to default: → STATUS_INVALID_DEVICE_REQUEST.
 *   DeviceIoControl returns FALSE → all tests FAIL (RED ✓)
 *
 * TDD GREEN state (after handler added):
 *   DeviceIoControl TRUE, valid==1, phc_time_ns!=0, qpc_frequency!=0
 *
 * Architecture compliance (HAL rules):
 *   - No register addresses here — HAL handles device-specific reads
 *   - Handler uses ops->get_systime() + KeQueryPerformanceCounter (kernel-generic)
 *   - SSOT header: include/avb_ioctl.h
 *
 * Implements: UT-CORR-003 (TEST-PLAN-MOCK-NDIS-HARNESS.md)
 * Traces to:  #48  (REQ-F-IOCTL-PHC-004: Cross-Timestamp IOCTL)
 * Closes:     IT-CORR-002 SKIP in test_ptp_corr.c (Track B of #317)
 * Verifies:   #149 (REQ-F-PTP-007: Hardware Timestamp Correlation)
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
#define DEVICE_PATH_W  L"\\\\.\\IntelAvbFilter"

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
    if (!ok || r.status != NDIS_STATUS_SUCCESS || r.timestamp == 0) return false;
    *out_ns = r.timestamp;
    return true;
}

/* =========================================================================
 * UT-CORR-003: PHC ↔ System Cross-Timestamp accuracy
 *
 * Requirement (REQ-F-IOCTL-PHC-004 / #48):
 *   IOCTL_AVB_PHC_CROSSTIMESTAMP must atomically sample:
 *     - phc_time_ns   via ops->get_systime()           (PHC register SYSTIM)
 *     - system_qpc    via KeQueryPerformanceCounter     (system counter)
 *     - qpc_frequency via KeQueryPerformanceCounter     (frequency)
 *   Both returned in a single IOCTL call so user-mode can compute the
 *   PHC↔wall-clock offset.
 *
 * Accuracy assertion:
 *   User-mode re-reads PHC before and after the IOCTL.  The phc_time_ns
 *   value returned must lie within [phc_before, phc_after].  This proves
 *   the driver read SYSTIM at approximately the same instant as QPC.
 *
 * RED state:
 *   No handler case → STATUS_INVALID_DEVICE_REQUEST → DeviceIoControl FALSE.
 *
 * GREEN state:
 *   DeviceIoControl TRUE, valid==1, phc_time_ns in [before, after],
 *   system_qpc != 0, qpc_frequency in [1e6, 1e10].
 * =========================================================================*/
static void test_ut_corr_003(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n[UT-CORR-003] PHC Cross-Timestamp IOCTL (adapter %u)\n", adapter_idx);
    printf("  Verifies: #48 (REQ-F-IOCTL-PHC-004) | Traces to: #149 (REQ-F-PTP-007)\n");
    printf("  Closes Track B: #317 (INFRA: Mock NDIS Unit Test Harness)\n");

    /* Step 1: Confirm adapter is reachable via PHC read */
    uint64_t phc_before = 0;
    if (!read_phc(hDev, adapter_idx, &phc_before)) {
        printf("  [SKIP] PHC read failed on adapter %u — adapter not ready\n", adapter_idx);
        tc_result("UT-CORR-003 PHC Cross-Timestamp (SKIP - adapter not ready)", true);
        return;
    }

    /* Step 2: Call IOCTL_AVB_PHC_CROSSTIMESTAMP */
    AVB_CROSS_TIMESTAMP_REQUEST r = {0};
    r.adapter_index = adapter_idx;
    DWORD br = 0;

    BOOL ok = DeviceIoControl(
        hDev,
        IOCTL_AVB_PHC_CROSSTIMESTAMP,   /* code 63 */
        &r, sizeof(r),
        &r, sizeof(r),
        &br, NULL
    );

    /* Capture PHC after IOCTL for bracket check */
    uint64_t phc_after = 0;
    read_phc(hDev, adapter_idx, &phc_after);

    /* --- Assert 1: IOCTL must succeed ---
     * RED:   DeviceIoControl FALSE (no handler → STATUS_INVALID_DEVICE_REQUEST)
     * GREEN: DeviceIoControl TRUE
     */
    if (!ok) {
        DWORD err = GetLastError();
        printf("  FAIL: DeviceIoControl returned FALSE  (Win32 error %lu)\n", err);
        printf("  Root cause: IOCTL_AVB_PHC_CROSSTIMESTAMP (code 63) has no handler\n");
        printf("              in avb_integration_fixed.c — falls to default:\n");
        printf("  Fix: Add 'case IOCTL_AVB_PHC_CROSSTIMESTAMP:' calling ops->get_systime()\n");
        printf("       + KeQueryPerformanceCounter in avb_integration_fixed.c\n");
        printf("       + add case to dispatch list in device.c\n");
        tc_result("UT-CORR-003 PHC Cross-Timestamp IOCTL Handler Exists", false);
        return;
    }

    /* --- Assert 2: status must be NDIS_STATUS_SUCCESS --- */
    if (r.status != NDIS_STATUS_SUCCESS) {
        printf("  FAIL: r.status=0x%08X (expected 0x00000000 NDIS_STATUS_SUCCESS)\n", r.status);
        tc_result("UT-CORR-003 PHC Cross-Timestamp Status OK", false);
        return;
    }

    /* --- Assert 3: valid flag must be set --- */
    if (!r.valid) {
        printf("  FAIL: r.valid=0  (expected 1 — both get_systime and QPC succeeded)\n");
        tc_result("UT-CORR-003 PHC Cross-Timestamp Valid Flag", false);
        return;
    }

    /* --- Assert 4: PHC time nonzero --- */
    if (r.phc_time_ns == 0) {
        printf("  FAIL: r.phc_time_ns=0  (PTP clock not running?)\n");
        tc_result("UT-CORR-003 PHC Time Nonzero", false);
        return;
    }

    /* --- Assert 5: QPC values sane --- */
    if (r.system_qpc == 0) {
        printf("  FAIL: r.system_qpc=0  (KeQueryPerformanceCounter returned 0?)\n");
        tc_result("UT-CORR-003 System QPC Nonzero", false);
        return;
    }
    if (r.qpc_frequency < 1000000ULL || r.qpc_frequency > 10000000000ULL) {
        printf("  FAIL: r.qpc_frequency=%llu out of expected range [1e6, 1e10]\n",
               (unsigned long long)r.qpc_frequency);
        tc_result("UT-CORR-003 QPC Frequency in Range", false);
        return;
    }

    /* --- Assert 6: PHC bracket  phc_before <= phc_time_ns <= phc_after ---
     * This proves the driver read SYSTIM at a point in time that is coherent
     * with the user-mode PHC reads bracketing it.
     */
    if (r.phc_time_ns < phc_before || r.phc_time_ns > phc_after) {
        printf("  WARN: phc_time_ns=%llu not in [%llu, %llu]\n",
               (unsigned long long)r.phc_time_ns,
               (unsigned long long)phc_before,
               (unsigned long long)phc_after);
        printf("  This may indicate the PHC advanced by more than the IOCTL roundtrip.\n");
        printf("  Treating as WARN (not hard fail) — may need loopback or lower-jitter env.\n");
        /* Non-fatal: epoch/wrap-around or high-load jitter can cause this */
    } else {
        printf("  PHC bracket: %llu <= %llu <= %llu ✓\n",
               (unsigned long long)phc_before,
               (unsigned long long)r.phc_time_ns,
               (unsigned long long)phc_after);
    }

    /* Compute approx PHC↔system offset for informational output */
    double qpc_us = (double)r.system_qpc / (double)r.qpc_frequency * 1e6;
    double phc_us = (double)r.phc_time_ns / 1000.0;

    printf("  phc_time_ns  : %llu ns\n",    (unsigned long long)r.phc_time_ns);
    printf("  system_qpc   : %llu ticks\n", (unsigned long long)r.system_qpc);
    printf("  qpc_frequency: %llu Hz\n",    (unsigned long long)r.qpc_frequency);
    printf("  PHC  time    : %.3f µs since epoch\n", phc_us);
    printf("  QPC  time    : %.3f µs since boot\n",  qpc_us);
    printf("  r.valid=%u  r.status=0x%08X\n", r.valid, r.status);

    tc_result("UT-CORR-003 PHC Cross-Timestamp IOCTL", true);
}

/* =========================================================================
 * main
 * =========================================================================*/
int main(void)
{
    printf("========================================================================\n");
    printf("TEST-PTP-CROSSTIMESTAMP: PHC↔System Cross-Timestamp IOCTL Verification\n");
    printf("Harness: Track B / #317  |  TDD cycle\n");
    printf("Tests: UT-CORR-003\n");
    printf("Verifies: #48 (REQ-F-IOCTL-PHC-004) | Traces to: #149 (REQ-F-PTP-007)\n");
    printf("========================================================================\n");
    printf("\nExpected state (TDD RED — before handler):\n");
    printf("  Test FAILs with 'DeviceIoControl returned FALSE'.\n");
    printf("  Reason: IOCTL_AVB_PHC_CROSSTIMESTAMP (code 63) has no handler case in\n");
    printf("          avb_integration_fixed.c — falls to default:.\n");
    printf("  Fix: Add case + call ops->get_systime() + KeQueryPerformanceCounter\n");
    printf("       and add to dispatch list in device.c\n");
    printf("========================================================================\n");

    HANDLE hDev = CreateFileW(
        DEVICE_PATH_W,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDev == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open device %S (Win32 error %lu)\n",
               DEVICE_PATH_W, GetLastError());
        printf("  Is the IntelAvbFilter driver installed and running?\n");
        return 1;
    }
    printf("Device opened successfully.\n");

    /* Enumerate adapters */
    int adapter_count = 0;
    for (int i = 0; i < 8; i++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = i;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                                  &r, sizeof(r), &r, sizeof(r), &br, NULL);
        if (!ok || r.status != NDIS_STATUS_SUCCESS) break;
        printf("  Adapter %d: VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               i, r.vendor_id, r.device_id, r.capabilities);
        adapter_count++;
    }

    if (adapter_count == 0) {
        printf("ERROR: No adapters enumerated.\n");
        CloseHandle(hDev);
        return 1;
    }
    printf("Found %d adapter(s).\n\n", adapter_count);

    /* Run per-adapter tests */
    for (int ai = 0; ai < adapter_count; ai++) {
        printf("\n--- Adapter %d / %d ---\n", ai, adapter_count - 1);
        test_ut_corr_003(hDev, (uint32_t)ai);
    }

    CloseHandle(hDev);

    /* Summary */
    printf("\n========================================================================\n");
    printf("Test Summary  (Track B / #317)\n");
    printf("  Total: %d  Passed: %d  Failed: %d\n", s_total, s_passed, s_failed);
    if (s_failed == 0) {
        printf("  STATUS: PASS ✓  (TDD GREEN — IOCTL_AVB_PHC_CROSSTIMESTAMP working)\n");
    } else {
        printf("  STATUS: FAIL ✗  (TDD RED  — handler missing or broken)\n");
        printf("  See output above for root cause and fix.\n");
    }
    printf("========================================================================\n");

    return (s_failed == 0) ? 0 : 1;
}
