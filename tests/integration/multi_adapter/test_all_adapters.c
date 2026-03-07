/**
 * Multi-Adapter GET_CLOCK_CONFIG Test
 * Tests all Intel adapters exposed by the driver
 *
 * Implements: #303 (TEST-SSOT-004: Verify SSOT Header Completeness)
 * Verifies: #24 (REQ-NF-SSOT-001: Single Source of Truth for IOCTL Interface)
 * Uses Single Source of Truth (include/avb_ioctl.h)
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../../../include/avb_ioctl.h"  // SSOT for IOCTL definitions
// All struct types (AVB_ENUM_REQUEST, AVB_CLOCK_CONFIG, ...) come from avb_ioctl.h

const char* GetCapabilityString(avb_u32 caps) {
    static char buf[512];
    buf[0] = '\0';
    if (caps & 0x001) strcat_s(buf, sizeof(buf), "BASIC_1588 ");
    if (caps & 0x002) strcat_s(buf, sizeof(buf), "ENHANCED_TS ");
    if (caps & 0x004) strcat_s(buf, sizeof(buf), "TSN_TAS ");
    if (caps & 0x008) strcat_s(buf, sizeof(buf), "TSN_FP ");
    if (caps & 0x010) strcat_s(buf, sizeof(buf), "PCIe_PTM ");
    if (caps & 0x020) strcat_s(buf, sizeof(buf), "2_5G ");
    if (caps & 0x040) strcat_s(buf, sizeof(buf), "5G ");
    if (caps & 0x080) strcat_s(buf, sizeof(buf), "MMIO ");
    if (caps & 0x100) strcat_s(buf, sizeof(buf), "EEE ");
    return buf;
}

const char* InterpretMarker(avb_u32 status) {
    switch (status) {
        case 0xCCCCCCCC: return "UNCHANGED - IOCTL never reached driver";
        case 0xAAAA0001: return "Reached AvbHandleDeviceIoControl entry";
        case 0xBBBB0002: return "Took early return (blocked by !initialized)";
        case 0xCCCC0003: return "Passed early return check";
        case 0xDEAD0001: return "SUCCESS - Entered GET_CLOCK_CONFIG case";
        default:
            if ((status & 0xFFFF0000) == 0xDEAD0000)
                return "SUCCESS - Case executed multiple times";
            return "Unknown marker or error code";
    }
}

int main() {
    printf("========================================\n");
    printf("MULTI-ADAPTER GET_CLOCK_CONFIG TEST\n");
    printf("Tests all Intel adapters via ENUM_ADAPTERS\n");
    printf("========================================\n\n");

    // Open driver
    HANDLE hDriver = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("ERROR: Could not open driver (error %lu)\n", GetLastError());
        return 1;
    }

    printf("Driver opened: handle=0x%p\n\n", hDriver);

    // Step 1: Enumerate adapters using AVB_ENUM_REQUEST (SSOT)
    // Protocol: call with index=0 first to discover total count, then loop.
    printf("STEP 1: Enumerating adapters...\n");
    printf("================================\n");

    AVB_ENUM_REQUEST enumReq;
    DWORD bytesReturned;

    memset(&enumReq, 0, sizeof(enumReq));
    enumReq.index = 0;  /* Query adapter 0; driver always returns total count */

    BOOL result = DeviceIoControl(
        hDriver,
        IOCTL_AVB_ENUM_ADAPTERS,
        &enumReq, sizeof(enumReq),
        &enumReq, sizeof(enumReq),
        &bytesReturned,
        NULL
    );

    if (!result || enumReq.count == 0) {
        printf("ERROR: ENUM_ADAPTERS failed (error %lu)\n", GetLastError());
        CloseHandle(hDriver);
        return 1;
    }

    DWORD adapterCount = enumReq.count;
    printf("Found %lu adapter(s):\n\n", adapterCount);

    // Step 2: Query each adapter individually
    printf("STEP 2: Testing GET_CLOCK_CONFIG on each adapter...\n");
    printf("====================================================\n\n");

    for (DWORD i = 0; i < adapterCount; i++) {
        memset(&enumReq, 0, sizeof(enumReq));
        enumReq.index = i;

        result = DeviceIoControl(
            hDriver,
            IOCTL_AVB_ENUM_ADAPTERS,
            &enumReq, sizeof(enumReq),
            &enumReq, sizeof(enumReq),
            &bytesReturned,
            NULL
        );

        printf("--- Adapter %lu ---\n", i);
        if (!result) {
            printf("  ENUM_ADAPTERS[%lu] failed (error %lu)\n\n", i, GetLastError());
            continue;
        }
        printf("VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               enumReq.vendor_id, enumReq.device_id, enumReq.capabilities);
        printf("  %s\n", GetCapabilityString(enumReq.capabilities));

        // Open adapter by VID/DID/index
        AVB_OPEN_REQUEST openReq;
        memset(&openReq, 0, sizeof(openReq));
        openReq.vendor_id = enumReq.vendor_id;
        openReq.device_id = enumReq.device_id;
        openReq.index     = i;
        openReq.status    = 0xCCCCCCCC; /* Marker */

        result = DeviceIoControl(
            hDriver,
            IOCTL_AVB_OPEN_ADAPTER,
            &openReq, sizeof(openReq),
            &openReq, sizeof(openReq),
            &bytesReturned,
            NULL
        );

        printf("OPEN_ADAPTER: result=%d bytes=%lu status=0x%08X\n",
               result, bytesReturned, openReq.status);

        if (!result || bytesReturned == 0) {
            printf("  *** OPEN failed, skipping GET_CLOCK_CONFIG\n\n");
            continue;
        }

        // Get clock config
        AVB_CLOCK_CONFIG cfg;
        memset(&cfg, 0xCC, sizeof(cfg)); /* Fill with 0xCC to detect changes */

        result = DeviceIoControl(
            hDriver,
            IOCTL_AVB_GET_CLOCK_CONFIG,
            &cfg, sizeof(cfg),
            &cfg, sizeof(cfg),
            &bytesReturned,
            NULL
        );

        printf("GET_CLOCK_CONFIG:\n");
        printf("  DeviceIoControl: %s\n", result ? "TRUE" : "FALSE");
        printf("  GetLastError: %lu (0x%08lX)\n", GetLastError(), GetLastError());
        printf("  bytesReturned: %lu (expected %zu)\n", bytesReturned, sizeof(cfg));
        printf("  cfg.status: 0x%08X\n", cfg.status);
        printf("  >> %s\n", InterpretMarker(cfg.status));

        if (cfg.status != 0xCCCCCCCC && (cfg.status & 0xFFFF0000) != 0xDEAD0000) {
            /* Not a success marker, show error */
            printf("  cfg.systim: 0x%016llX\n", cfg.systim);
            printf("  cfg.timinca: 0x%08X\n", cfg.timinca);
            printf("  cfg.tsauxc: 0x%08X\n", cfg.tsauxc);
        } else if ((cfg.status & 0xFFFF0000) == 0xDEAD0000) {
            /* Success! Show clock values */
            printf("  *** SUCCESS! Clock values:\n");
            printf("      SYSTIM: 0x%016llX\n", cfg.systim);
            printf("      TIMINCA: 0x%08X\n", cfg.timinca);
            printf("      TSAUXC: 0x%08X\n", cfg.tsauxc);
            printf("      Clock Rate: %lu MHz\n", cfg.clock_rate_mhz);
        }

        printf("\n");
    }

    CloseHandle(hDriver);
    printf("========================================\n");
    printf("Test complete!\n");
    return 0;
}
