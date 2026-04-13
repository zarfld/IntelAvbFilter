/**
 * Simple test to diagnose GET_CLOCK_CONFIG failure
 * Tests why IOCTL_AVB_GET_CLOCK_CONFIG returns all zeros
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// Include proper IOCTL definitions from shared header
#include "../include/avb_ioctl.h"

// Structures are now defined in avb_ioctl.h

int main(void)
{
    printf("========================================\n");
    printf("GET_CLOCK_CONFIG DIAGNOSTIC TEST\n");
    printf("  Verifies:   #25  (REQ-F-PTP-IOCTL-001: GET_CLOCK_CONFIG)\n");
    printf("  MULTI-ADAPTER: tests all enumerated adapters\n");
    printf("========================================\n\n");

    HANDLE discovery = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (discovery == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open driver (error %lu)\n", GetLastError());
        return 1;
    }

    int total_fail = 0, adapters_tested = 0;

    for (UINT32 idx = 0; idx < 16; idx++) {
        AVB_ENUM_REQUEST enum_req;
        DWORD br = 0;
        ZeroMemory(&enum_req, sizeof(enum_req));
        enum_req.index = idx;
        if (!DeviceIoControl(discovery, IOCTL_AVB_ENUM_ADAPTERS,
                             &enum_req, sizeof(enum_req),
                             &enum_req, sizeof(enum_req), &br, NULL))
            break;

        printf("--- Adapter %u  VID=0x%04X DID=0x%04X ---\n",
               idx, enum_req.vendor_id, enum_req.device_id);

        HANDLE hDriver = CreateFileA(
            "\\\\.\\IntelAvbFilter",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDriver == INVALID_HANDLE_VALUE) {
            printf("  [FAIL] Cannot open per-adapter handle (error %lu)\n", GetLastError());
            total_fail++; adapters_tested++; continue;
        }

        AVB_OPEN_REQUEST open_req;
        ZeroMemory(&open_req, sizeof(open_req));
        open_req.vendor_id = enum_req.vendor_id;
        open_req.device_id = enum_req.device_id;
        open_req.index     = idx;
        if (!DeviceIoControl(hDriver, IOCTL_AVB_OPEN_ADAPTER,
                             &open_req, sizeof(open_req),
                             &open_req, sizeof(open_req), &br, NULL)
            || open_req.status != 0) {
            printf("  [FAIL] OPEN_ADAPTER (error %lu, status=0x%08X)\n",
                   GetLastError(), open_req.status);
            CloseHandle(hDriver); total_fail++; adapters_tested++; continue;
        }
        printf("  OPEN_ADAPTER: OK\n");

        // STEP 0: Initialize device first
        DWORD dummy = 0;
        BOOL initSuccess = DeviceIoControl(hDriver, IOCTL_AVB_INIT_DEVICE,
                                           &dummy, sizeof(dummy),
                                           &dummy, sizeof(dummy),
                                           &br, NULL);
        printf("  INIT_DEVICE: %s (bytes=%lu)\n\n", initSuccess ? "SUCCESS" : "FAILED", br);

        // Test 1: Get clock config via public API
        printf("  TEST 1: Get clock config via IOCTL_AVB_GET_CLOCK_CONFIG\n");

        BOOL has1588 = ((enum_req.capabilities & INTEL_CAP_BASIC_1588) != 0);
        BOOL hasMmio = ((enum_req.capabilities & INTEL_CAP_MMIO) != 0);
        printf("  Capabilities: 0x%08X  INTEL_CAP_BASIC_1588: %s  INTEL_CAP_MMIO: %s\n",
               enum_req.capabilities,
               has1588 ? "YES" : "NO",
               hasMmio ? "YES" : "NO");

        /* FAIL immediately if hardware supports 1588 but driver failed to map BAR0.
         * Every 1588-capable Intel device (I210, I219, I226, ...) has a BAR0 MMIO
         * space.  MMIO=NO with BASIC_1588=YES means BAR0 discovery/mapping failed
         * in the driver — that is a driver bug, not an expected hardware state. */
        if (has1588 && !hasMmio) {
            printf("  [FAIL] BASIC_1588=YES but MMIO=NO — driver failed to map BAR0 for this adapter\n");
            CloseHandle(hDriver);
            total_fail++;
            adapters_tested++;
            printf("\n");
            continue;
        }

        AVB_CLOCK_CONFIG cfg = {0};
        DWORD bytesReturned = 0;
        BOOL success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                                       &cfg, sizeof(cfg),
                                       &cfg, sizeof(cfg),
                                       &bytesReturned, NULL);
        DWORD ioErr = GetLastError();
        CloseHandle(hDriver);

        if (has1588 && hasMmio) {
            /* Adapter advertises PTP support and MMIO access — must succeed */
            if (!success || cfg.status != 0) {
                printf("  [FAIL] Adapter has BASIC_1588+MMIO but GET_CLOCK_CONFIG failed (error=%lu status=0x%08X)\n",
                       ioErr, cfg.status);
                total_fail++;
            } else {
                printf("  [PASS] GET_CLOCK_CONFIG succeeded\n");
                printf("  Clock Config: SYSTIM=0x%016llX TIMINCA=0x%08X TSAUXC=0x%08X rate=%uMHz status=0x%08X\n",
                       cfg.systim, cfg.timinca, cfg.tsauxc, cfg.clock_rate_mhz, cfg.status);
                if (cfg.systim == 0 && cfg.timinca == 0 && cfg.tsauxc == 0) {
                    printf("  [WARN] All fields are zero — hardware may not be initialised\n");
                }
            }
        } else {
            /* No BASIC_1588 — device does not support PTP; expect STATUS_NOT_SUPPORTED */
            if (!success && ioErr == ERROR_NOT_SUPPORTED) {
                printf("  [PASS] GET_CLOCK_CONFIG correctly returned ERROR_NOT_SUPPORTED (no 1588 hardware)\n");
            } else if (!success) {
                printf("  [FAIL] GET_CLOCK_CONFIG returned error %lu (expected ERROR_NOT_SUPPORTED=50 for non-1588 adapter)\n",
                       ioErr);
                total_fail++;
            } else {
                printf("  [INFO] GET_CLOCK_CONFIG succeeded on adapter without BASIC_1588 cap\n");
            }
        }
        adapters_tested++;
        printf("\n");
    }

    CloseHandle(discovery);

    printf("========================================\n");
    printf(" Adapters tested: %d  Failures: %d\n", adapters_tested, total_fail);
    printf("========================================\n");
    return (total_fail > 0) ? 1 : 0;
}
