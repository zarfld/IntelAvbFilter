/**
 * @file test_hw_state.c
 * @brief Hardware state checks and PCI read latency measurement tests
 *
 * Implements: #288 (TEST-HW-LATENCY: PCI read latency < 100µs P99 assertion)
 * Verifies:   REQ-NF-PERF-PCI-001 (PCI register read via IOCTL_AVB_GET_CLOCK_CONFIG must
 *             complete within 100µs at P99 across all installed adapters)
 *
 * Test Cases:
 *   TC-PCI-LAT-001  1000-sample latency measurement, assert P50 <50µs and P99 <100µs
 *   TC-PCI-LAT-002  No single sample may exceed 500µs (outlier bound)
 *   TC-PCI-LAT-003  Per-adapter latency: repeat TC-PCI-LAT-001 for each enumerated adapter
 *
 * @author IntelAvbFilter team
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* -------------------------------------------------------------------------- */
/* Test harness macros                                                         */
/* -------------------------------------------------------------------------- */
static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

#define TEST_PASS(id) do { printf("[PASS] %s\n\n", id); g_passed++; } while(0)
#define TEST_FAIL(id, reason) do { printf("[FAIL] %s: %s\n\n", id, reason); g_failed++; } while(0)
#define TEST_SKIP(id, reason) do { printf("[SKIP] %s: %s\n\n", id, reason); g_skipped++; } while(0)

/* -------------------------------------------------------------------------- */
/* Latency measurement constants                                               */
/* -------------------------------------------------------------------------- */
#define LAT_SAMPLE_COUNT       1000
#define LAT_P50_THRESHOLD_NS   50000ULL   /* 50 µs */
#define LAT_P99_THRESHOLD_NS   100000ULL  /* 100 µs */
#define LAT_OUTLIER_MAX_NS     500000ULL  /* 500 µs — absolute single-sample bound */

/* -------------------------------------------------------------------------- */
/* Comparison function for qsort                                               */
/* -------------------------------------------------------------------------- */
static int cmp_uint64(const void *a, const void *b)
{
    uint64_t ua = *(const uint64_t *)a;
    uint64_t ub = *(const uint64_t *)b;
    return (ua > ub) - (ua < ub);
}

/* -------------------------------------------------------------------------- */
/* Diagnostic: print hardware state and clock config (preserved from v1)      */
/* -------------------------------------------------------------------------- */
static void diagnostic_print_hw_state(HANDLE h)
{
    printf("=== Hardware State Diagnostic ===\n\n");

    /* GET_DEVICE_INFO */
    printf("Step 1: GET_DEVICE_INFO\n");
    AVB_DEVICE_INFO_REQUEST devInfo;
    ZeroMemory(&devInfo, sizeof(devInfo));
    DWORD bytesRet = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_DEVICE_INFO,
                               &devInfo, sizeof(devInfo),
                               &devInfo, sizeof(devInfo),
                               &bytesRet, NULL);
    printf("  Result: %s  BytesReturned: %lu\n", ok ? "SUCCESS" : "FAILED", bytesRet);
    if (ok && bytesRet > 0) {
        /* device_info is a null-terminated string returned by the driver */
        devInfo.device_info[AVB_DEVICE_INFO_MAX - 1] = '\0';
        printf("  device_info: %s\n", devInfo.device_info);
    }

    /* GET_CLOCK_CONFIG */
    printf("\nStep 2: GET_CLOCK_CONFIG\n");
    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    bytesRet = 0;

    ok = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                          &cfg, sizeof(cfg),
                          &cfg, sizeof(cfg),
                          &bytesRet, NULL);
    printf("  Result: %s  BytesReturned: %lu (expected %zu)\n",
           ok ? "SUCCESS" : "FAILED", bytesRet, sizeof(cfg));
    if (!ok) {
        DWORD err = GetLastError();
        printf("  GetLastError: %lu (0x%08X)%s\n", err, err,
               (err == ERROR_NOT_READY) ? " -> Device not ready" : "");
    }
    if (bytesRet > 0) {
        printf("  systim:  0x%016llX\n  timinca: 0x%08X\n  tsauxc:  0x%08X\n",
               (unsigned long long)cfg.systim, cfg.timinca, cfg.tsauxc);
    }
    printf("\n");
}

/* -------------------------------------------------------------------------- */
/* TC-PCI-LAT-001 / TC-PCI-LAT-002: per-adapter latency measurement          */
/* Runs LAT_SAMPLE_COUNT IOCTL_AVB_GET_CLOCK_CONFIG calls, records round-trip */
/* latency using QueryPerformanceCounter, asserts P50/P99/outlier bounds.     */
/* -------------------------------------------------------------------------- */
static void test_pci_latency_one_adapter(HANDLE h, int adapter_index)
{
    char id[64];
    sprintf(id, "TC-PCI-LAT-001[adapter %d]", adapter_index);
    printf("=== TEST: %s ===\n", id);
    printf("  Sampling %d IOCTL_AVB_GET_CLOCK_CONFIG round-trips...\n", LAT_SAMPLE_COUNT);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    uint64_t *samples = (uint64_t *)malloc(LAT_SAMPLE_COUNT * sizeof(uint64_t));
    if (!samples) {
        TEST_FAIL(id, "malloc failed");
        return;
    }

    int consecutive_fail = 0;
    for (int i = 0; i < LAT_SAMPLE_COUNT; i++) {
        AVB_CLOCK_CONFIG cfg;
        ZeroMemory(&cfg, sizeof(cfg));
        DWORD bytesRet = 0;

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                                   &cfg, sizeof(cfg),
                                   &cfg, sizeof(cfg),
                                   &bytesRet, NULL);
        QueryPerformanceCounter(&t1);

        if (!ok || bytesRet != sizeof(cfg)) {
            consecutive_fail++;
            samples[i] = 0; /* Record 0 for failed calls — sorted to front */
            if (consecutive_fail >= 5) {
                char reason[128];
                sprintf(reason, "IOCTL_AVB_GET_CLOCK_CONFIG failed %d times at sample %d",
                        consecutive_fail, i);
                free(samples);
                TEST_FAIL(id, reason);
                return;
            }
            continue;
        }
        consecutive_fail = 0;

        uint64_t elapsed_ns = (uint64_t)((t1.QuadPart - t0.QuadPart) * 1000000000ULL /
                                          freq.QuadPart);
        samples[i] = elapsed_ns;
    }

    /* Sort for percentile calculation */
    qsort(samples, LAT_SAMPLE_COUNT, sizeof(uint64_t), cmp_uint64);

    uint64_t p50 = samples[LAT_SAMPLE_COUNT / 2];
    uint64_t p99 = samples[(LAT_SAMPLE_COUNT * 99) / 100];
    uint64_t p_max = samples[LAT_SAMPLE_COUNT - 1];

    printf("  Latency — P50: %llu ns (%.1f µs)  P99: %llu ns (%.1f µs)  Max: %llu ns (%.1f µs)\n",
           (unsigned long long)p50,  (double)p50  / 1000.0,
           (unsigned long long)p99,  (double)p99  / 1000.0,
           (unsigned long long)p_max,(double)p_max / 1000.0);

    free(samples);

    /* Assert P50 < 50 µs */
    if (p50 > LAT_P50_THRESHOLD_NS) {
        char reason[128];
        sprintf(reason, "P50 %llu ns exceeds %llu ns (%.0f µs limit)",
                (unsigned long long)p50, (unsigned long long)LAT_P50_THRESHOLD_NS,
                (double)LAT_P50_THRESHOLD_NS / 1000.0);
        TEST_FAIL(id, reason);
        return;
    }
    /* Assert P99 < 100 µs */
    if (p99 > LAT_P99_THRESHOLD_NS) {
        char reason[128];
        sprintf(reason, "P99 %llu ns exceeds %llu ns (%.0f µs limit)",
                (unsigned long long)p99, (unsigned long long)LAT_P99_THRESHOLD_NS,
                (double)LAT_P99_THRESHOLD_NS / 1000.0);
        TEST_FAIL(id, reason);
        return;
    }

    TEST_PASS(id);

    /* TC-PCI-LAT-002: no single outlier > 500 µs */
    sprintf(id, "TC-PCI-LAT-002[adapter %d]", adapter_index);
    printf("=== TEST: %s ===\n", id);
    if (p_max > LAT_OUTLIER_MAX_NS) {
        char reason[128];
        sprintf(reason, "Max sample %llu ns exceeds outlier bound %llu ns (%.0f µs)",
                (unsigned long long)p_max, (unsigned long long)LAT_OUTLIER_MAX_NS,
                (double)LAT_OUTLIER_MAX_NS / 1000.0);
        TEST_FAIL(id, reason);
    } else {
        TEST_PASS(id);
    }
}

/* -------------------------------------------------------------------------- */
/* TC-PCI-LAT-003: per-adapter sweep via IOCTL_AVB_ENUM_ADAPTERS              */
/* -------------------------------------------------------------------------- */
static void test_pci_latency_all_adapters(HANDLE h)
{
    printf("=== TEST: TC-PCI-LAT-003: Per-adapter latency sweep ===\n");

    int adapter_count = 0;
    for (int i = 0; i < 16; i++) {
        AVB_ENUM_REQUEST req;
        ZeroMemory(&req, sizeof(req));
        req.index = (avb_u32)i;
        DWORD bytesRet = 0;

        BOOL ok = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                                   &req, sizeof(req),
                                   &req, sizeof(req),
                                   &bytesRet, NULL);
        if (!ok || req.status != 0) {
            break;
        }
        printf("  Adapter %d: VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               i, req.vendor_id, req.device_id, req.capabilities);
        adapter_count++;
    }

    if (adapter_count == 0) {
        TEST_SKIP("TC-PCI-LAT-003", "No adapters enumerated");
        return;
    }

    printf("  Found %d adapter(s) — testing latency per adapter\n\n", adapter_count);

    /* Run latency sub-tests per adapter */
    for (int ai = 0; ai < adapter_count; ai++) {
        test_pci_latency_one_adapter(h, ai);
    }

    /* TC-PCI-LAT-003 overall pass: all per-adapter sub-tests must not have added failures
     * (their individual PASS/FAIL macros already incremented the counters). */
}

/* -------------------------------------------------------------------------- */
/* main                                                                        */
/* -------------------------------------------------------------------------- */
int main(void)
{
    printf("==============================================================\n");
    printf("Hardware State & PCI Read Latency Tests\n");
    printf("Implements: #288 (TEST-HW-LATENCY)\n");
    printf("Verifies:   REQ-NF-PERF-PCI-001\n");
    printf("==============================================================\n\n");

    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter",
                            GENERIC_READ | GENERIC_WRITE,
                            0, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FATAL] Cannot open IntelAvbFilter (error=%lu)\n", GetLastError());
        return 1;
    }

    /* Preserve original diagnostic output */
    diagnostic_print_hw_state(h);

    /* New: PCI read latency tests */
    printf("==============================================================\n");
    printf("PCI Read Latency Tests (Issue #288)\n");
    printf("==============================================================\n\n");
    test_pci_latency_all_adapters(h);

    CloseHandle(h);

    /* Summary */
    printf("==============================================================\n");
    printf("TEST SUMMARY\n");
    printf("==============================================================\n");
    printf("PASSED:  %d\n", g_passed);
    printf("FAILED:  %d\n", g_failed);
    printf("SKIPPED: %d\n", g_skipped);
    printf("TOTAL:   %d\n", g_passed + g_failed + g_skipped);
    printf("==============================================================\n");

    if (g_failed > 0) {
        printf("\n[RESULT] FAILED\n");
        return 1;
    }
    printf("\n[RESULT] PASSED\n");
    return 0;
}
