/**
 * Multi-Adapter GET_CLOCK_CONFIG Test
 * Tests all 6 Intel I226-V adapters with diagnostic markers
 *
 * Implements REQ-NF-SSOT-001: Uses Single Source of Truth (include/avb_ioctl.h)
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../../../include/avb_ioctl.h"  // SSOT for IOCTL definitions

typedef uint8_t  avb_u8;
typedef uint16_t avb_u16;
typedef uint32_t avb_u32;
typedef uint64_t avb_u64;

#pragma pack(push, 1)

typedef struct AVB_ADAPTER_INFO {
    avb_u16 vendor_id;
    avb_u16 device_id;
    avb_u32 capabilities;
    char    friendly_name[256];
    avb_u32 adapter_index;
} AVB_ADAPTER_INFO;

typedef struct AVB_ADAPTER_LIST {
    avb_u32           count;
    AVB_ADAPTER_INFO  adapters[16];
    avb_u32           status;
} AVB_ADAPTER_LIST;

typedef struct AVB_OPEN_ADAPTER_REQUEST {
    avb_u32 adapter_index;
    avb_u32 status;
} AVB_OPEN_ADAPTER_REQUEST;

typedef struct AVB_CLOCK_CONFIG {
    avb_u64 systim;
    avb_u32 timinca;
    avb_u32 tsauxc;
    avb_u32 clock_rate_mhz;
    avb_u32 status;
} AVB_CLOCK_CONFIG;

#pragma pack(pop)

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
    printf("Tests all 6 Intel I226-V adapters\n");
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

    // Step 1: Enumerate adapters
    printf("STEP 1: Enumerating adapters...\n");
    printf("================================\n");
    
    AVB_ADAPTER_LIST adapterList;
    memset(&adapterList, 0, sizeof(adapterList));
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDriver,
        IOCTL_AVB_ENUM_ADAPTERS,
        &adapterList, sizeof(adapterList),
        &adapterList, sizeof(adapterList),
        &bytesReturned,
        NULL
    );

    if (!result || adapterList.count == 0) {
        printf("ERROR: ENUM_ADAPTERS failed (error %lu)\n", GetLastError());
        CloseHandle(hDriver);
        return 1;
    }

    printf("Found %lu adapters:\n", adapterList.count);
    for (DWORD i = 0; i < adapterList.count; i++) {
        AVB_ADAPTER_INFO* adapter = &adapterList.adapters[i];
        printf("  [%lu] VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               adapter->adapter_index,
               adapter->vendor_id,
               adapter->device_id,
               adapter->capabilities);
        printf("      %s\n", GetCapabilityString(adapter->capabilities));
        printf("      Name: %s\n", adapter->friendly_name);
    }
    printf("\n");

    // Step 2: Test each adapter
    printf("STEP 2: Testing GET_CLOCK_CONFIG on each adapter...\n");
    printf("====================================================\n\n");

    for (DWORD i = 0; i < adapterList.count; i++) {
        AVB_ADAPTER_INFO* adapter = &adapterList.adapters[i];
        
        printf("--- Adapter %lu ---\n", adapter->adapter_index);
        printf("VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               adapter->vendor_id, adapter->device_id, adapter->capabilities);

        // Open adapter
        AVB_OPEN_ADAPTER_REQUEST openReq;
        openReq.adapter_index = adapter->adapter_index;
        openReq.status = 0xCCCCCCCC; // Marker

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
        memset(&cfg, 0xCC, sizeof(cfg)); // Fill with 0xCC to detect changes

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
            // Not a success marker, show error
            printf("  cfg.systim: 0x%016llX\n", cfg.systim);
            printf("  cfg.timinca: 0x%08X\n", cfg.timinca);
            printf("  cfg.tsauxc: 0x%08X\n", cfg.tsauxc);
        } else if ((cfg.status & 0xFFFF0000) == 0xDEAD0000) {
            // Success! Show clock values
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
