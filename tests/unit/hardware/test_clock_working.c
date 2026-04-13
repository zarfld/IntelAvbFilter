/**
 * Working GET_CLOCK_CONFIG Test
 * Demonstrates that GET_CLOCK_CONFIG works when called directly
 * without OPEN_ADAPTER (uses first/default Intel adapter)
 */

#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main(void) {
    printf("========================================\n");
    printf("WORKING GET_CLOCK_CONFIG TEST\n");
    printf("  Verifies:   #25  (REQ-F-PTP-IOCTL-001: GET_CLOCK_CONFIG)\n");
    printf("  MULTI-ADAPTER: tests all enumerated adapters\n");
    printf("========================================\n\n");

    HANDLE discovery = CreateFileA("\\\\.\\IntelAvbFilter",
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (discovery == INVALID_HANDLE_VALUE) {
        printf("Failed to open device: error=%lu\n", GetLastError());
        printf("(Run as Administrator if error=5)\n");
        return 1;
    }

    int total_fail = 0, adapters_tested = 0;

    for (UINT32 idx = 0; idx < 16; idx++) {
        AVB_ENUM_REQUEST enum_req;
        DWORD bytes = 0;
        ZeroMemory(&enum_req, sizeof(enum_req));
        enum_req.index = idx;
        if (!DeviceIoControl(discovery, IOCTL_AVB_ENUM_ADAPTERS,
                             &enum_req, sizeof(enum_req),
                             &enum_req, sizeof(enum_req), &bytes, NULL))
            break;

        printf("--- Adapter %u  VID=0x%04X DID=0x%04X ---\n",
               idx, enum_req.vendor_id, enum_req.device_id);

        HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  [FAIL] Cannot open per-adapter handle (error=%lu)\n", GetLastError());
            total_fail++; adapters_tested++; continue;
        }

        AVB_OPEN_REQUEST open_req;
        ZeroMemory(&open_req, sizeof(open_req));
        open_req.vendor_id = enum_req.vendor_id;
        open_req.device_id = enum_req.device_id;
        open_req.index     = idx;
        if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                             &open_req, sizeof(open_req),
                             &open_req, sizeof(open_req), &bytes, NULL)
            || open_req.status != 0) {
            printf("  [FAIL] OPEN_ADAPTER (error=%lu, status=0x%08X)\n",
                   GetLastError(), open_req.status);
            CloseHandle(h); total_fail++; adapters_tested++; continue;
        }
        printf("  OPEN_ADAPTER: OK\n");

        // Optional: Initialize device
        DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytes, NULL);

        // GET_CLOCK_CONFIG
        AVB_CLOCK_CONFIG cfg;
        memset(&cfg, 0xCC, sizeof(cfg));
        BOOL result = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                                     &cfg, sizeof(cfg),
                                     &cfg, sizeof(cfg),
                                     &bytes, NULL);
        CloseHandle(h);

        printf("  bytes=%lu result=%d\n", bytes, result);

        if (bytes > 0 && cfg.status != 0xCCCCCCCC) {
            printf("  Status=0x%08X SYSTIM=0x%016llX TIMINCA=0x%08X TSAUXC=0x%08X rate=%luMHz\n",
                   cfg.status, cfg.systim, cfg.timinca, cfg.tsauxc, cfg.clock_rate_mhz);
            if (cfg.status == 0) {
                printf("  [PASS] GET_CLOCK_CONFIG succeeded\n");
            } else {
                printf("  [FAIL] GET_CLOCK_CONFIG driver status=0x%08X\n", cfg.status);
                total_fail++;
            }
        } else {
            printf("  [FAIL] GET_CLOCK_CONFIG returned unchanged buffer (bytes=%lu)\n", bytes);
            total_fail++;
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
