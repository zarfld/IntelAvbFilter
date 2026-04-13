/**
 * Minimal IOCTL test - just calls GET_CLOCK_CONFIG and shows return values
 */
#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main(void) {
    printf("========================================\n");
    printf("GET_CLOCK_CONFIG IOCTL TEST\n");
    printf("  Verifies:   #25  (REQ-F-PTP-IOCTL-001: GET_CLOCK_CONFIG)\n");
    printf("  MULTI-ADAPTER: tests all enumerated adapters\n");
    printf("========================================\n\n");

    HANDLE discovery = CreateFileA("\\\\.\\IntelAvbFilter",
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (discovery == INVALID_HANDLE_VALUE) {
        printf("FAILED: cannot open AVB device (error %lu)\n", GetLastError());
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

        HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  [FAIL] Cannot open per-adapter handle (error %lu)\n", GetLastError());
            total_fail++; adapters_tested++; continue;
        }

        AVB_OPEN_REQUEST open_req;
        ZeroMemory(&open_req, sizeof(open_req));
        open_req.vendor_id = enum_req.vendor_id;
        open_req.device_id = enum_req.device_id;
        open_req.index     = idx;
        if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                             &open_req, sizeof(open_req),
                             &open_req, sizeof(open_req), &br, NULL)
            || open_req.status != 0) {
            printf("  [FAIL] OPEN_ADAPTER (error %lu, status=0x%08X)\n",
                   GetLastError(), open_req.status);
            CloseHandle(h); total_fail++; adapters_tested++; continue;
        }
        printf("  OPEN_ADAPTER: OK\n");

        // PTP clock config requires both HW support (BASIC_1588) and MMIO register access.
        // If MMIO is unavailable (e.g. I219 BAR0 not mapped) the driver returns NOT_SUPPORTED.
        BOOL hasPtpCap = ((enum_req.capabilities & INTEL_CAP_BASIC_1588) != 0) &&
                         ((enum_req.capabilities & INTEL_CAP_MMIO) != 0);
        printf("  Capabilities: 0x%08X  INTEL_CAP_BASIC_1588: %s  INTEL_CAP_MMIO: %s\n",
               enum_req.capabilities,
               (enum_req.capabilities & INTEL_CAP_BASIC_1588) ? "YES" : "NO",
               (enum_req.capabilities & INTEL_CAP_MMIO) ? "YES" : "NO");
        printf("  Calling IOCTL_AVB_GET_CLOCK_CONFIG (0x%08X)...\n",
               IOCTL_AVB_GET_CLOCK_CONFIG);

        AVB_CLOCK_CONFIG cfg;
        ZeroMemory(&cfg, sizeof(cfg));
        DWORD bytesReturned = 0;
        BOOL success = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                                       &cfg, sizeof(cfg),
                                       &cfg, sizeof(cfg),
                                       &bytesReturned, NULL);
        DWORD ioErr = GetLastError();
        CloseHandle(h);

        if (hasPtpCap) {
            /* Adapter advertises INTEL_CAP_BASIC_1588 + INTEL_CAP_MMIO — IOCTL must succeed */
            if (!success || bytesReturned != sizeof(cfg)) {
                printf("  [FAIL] GET_CLOCK_CONFIG: adapter has BASIC_1588+MMIO but ok=%d bytesReturned=%lu error=%lu\n",
                       success, bytesReturned, ioErr);
                total_fail++;
            } else {
                printf("  [PASS] GET_CLOCK_CONFIG: bytesReturned=%lu\n", bytesReturned);
                printf("    SYSTIM=0x%016llX TIMINCA=0x%08X TSAUXC=0x%08X rate=%uMHz\n",
                       cfg.systim, cfg.timinca, cfg.tsauxc, cfg.clock_rate_mhz);
            }
        } else {
            /* Adapter has no PTP MMIO access — expect STATUS_NOT_SUPPORTED (error 50) */
            if (!success && ioErr == ERROR_NOT_SUPPORTED) {
                printf("  [PASS] GET_CLOCK_CONFIG: correctly returned ERROR_NOT_SUPPORTED (no MMIO or no BASIC_1588)\n");
            } else if (!success) {
                printf("  [FAIL] GET_CLOCK_CONFIG: returned error %lu (expected ERROR_NOT_SUPPORTED=50 for no-PTP adapter)\n",
                       ioErr);
                total_fail++;
            } else {
                printf("  [INFO] GET_CLOCK_CONFIG: succeeded on adapter without BASIC_1588 cap\n");
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
