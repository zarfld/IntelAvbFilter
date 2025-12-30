/**
 * Multi-adapter GET_CLOCK_CONFIG test
 * Tests all 6 Intel I226-V adapters
 *
 * Implements: #302 (TEST-SSOT-003: Verify All Files Use SSOT Header Include)
 * Verifies: #24 (REQ-NF-SSOT-001: Single Source of Truth for IOCTL Interface)
 * Uses Single Source of Truth (include/avb_ioctl.h)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../../include/avb_ioctl.h"  // SSOT for IOCTL definitions

// Type definitions and structures are in include/avb_ioctl.h (SSOT)

#pragma pack(push, 1)

// AVB_ENUM_REQUEST, AVB_OPEN_REQUEST, AVB_CLOCK_CONFIG all defined in SSOT header

#pragma pack(pop)

const char* InterpretMarker(avb_u32 status) {
    switch (status) {
        case 0xCCCCCCCC: return "UNCHANGED - IOCTL never reached driver";
        case 0xAAAA0001: return "Reached AvbHandleDeviceIoControl entry";
        case 0xBBBB0002: return "Took early return (blocked by !initialized)";
        case 0xCCCC0003: return "Passed early return check";
        case 0xDEAD0001: return "SUCCESS - Entered GET_CLOCK_CONFIG case";
        default:
            if ((status & 0xFFFF0000) == 0xDEAD0000)
                return "SUCCESS - Case executed";
            return "Unknown";
    }
}

int main() {
    printf("========================================\n");
    printf("MULTI-ADAPTER GET_CLOCK_CONFIG TEST\n");
    printf("========================================\n\n");
    
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter",
                          GENERIC_READ | GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Could not open driver (error %lu)\n", GetLastError());
        printf("Make sure to run as Administrator!\n");
        return 1;
    }
    
    printf("Driver opened: handle=%p\n\n", h);
    
    // Step 0: Initialize device
    printf("STEP 0: Initializing device...\n");
    printf("===============================\n");
    
    DWORD bytesRet;
    BOOL result = DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE,
                                  NULL, 0, NULL, 0, &bytesRet, NULL);
    
    printf("INIT_DEVICE: result=%s bytes=%lu\n\n", result ? "TRUE" : "FALSE", bytesRet);
    
    // Step 1: Enumerate adapters (query index 0 first to get total count)
    printf("STEP 1: Enumerating adapters...\n");
    printf("================================\n");
    
    AVB_ENUM_REQUEST enumReq;
    memset(&enumReq, 0, sizeof(enumReq));
    enumReq.index = 0; // Query first adapter to get count
    
    result = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                                  &enumReq, sizeof(enumReq),
                                  &enumReq, sizeof(enumReq),
                                  &bytesRet, NULL);
    
    if (!result || enumReq.count == 0) {
        printf("ERROR: ENUM_ADAPTERS failed (error %lu, count=%lu)\n", GetLastError(), enumReq.count);
        CloseHandle(h);
        return 1;
    }
    
    printf("Found %lu adapters\n", enumReq.count);
    
    // Query each adapter
    AVB_ENUM_REQUEST adapters[16];
    for (avb_u32 i = 0; i < enumReq.count && i < 16; i++) {
        memset(&adapters[i], 0, sizeof(AVB_ENUM_REQUEST));
        adapters[i].index = i;
        
        result = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                                &adapters[i], sizeof(AVB_ENUM_REQUEST),
                                &adapters[i], sizeof(AVB_ENUM_REQUEST),
                                &bytesRet, NULL);
        
        if (result) {
            printf("  [%lu] VID=0x%04X DID=0x%04X Caps=0x%08X\n",
                   i, adapters[i].vendor_id, adapters[i].device_id, adapters[i].capabilities);
        }
    }
    printf("\n");
    
    // Step 2: Test GET_CLOCK_CONFIG on ALL adapters
    printf("STEP 2: Testing GET_CLOCK_CONFIG on ALL %lu adapters...\n", enumReq.count);
    printf("=======================================================\n\n");
    
    for (avb_u32 i = 0; i < enumReq.count && i < 16; i++) {
        printf("--- ADAPTER %lu (VID=0x%04X DID=0x%04X Caps=0x%08X) ---\n", 
               i, adapters[i].vendor_id, adapters[i].device_id, adapters[i].capabilities);
        
        // Open this adapter
        AVB_OPEN_REQUEST openReq;
        openReq.vendor_id = adapters[i].vendor_id;
        openReq.device_id = adapters[i].device_id;
        openReq.reserved = 0;
        openReq.status = 0xCCCCCCCC;
        
        result = DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                                &openReq, sizeof(openReq),
                                &openReq, sizeof(openReq),
                                &bytesRet, NULL);
        
        printf("OPEN_ADAPTER: result=%s bytes=%lu status=0x%08X\n",
               result ? "TRUE" : "FALSE", bytesRet, openReq.status);
        
        if (!result || bytesRet == 0) {
            printf("  *** OPEN failed, skipping this adapter\n\n");
            continue;
        }
        
        // Try GET_CLOCK_CONFIG
        AVB_CLOCK_CONFIG cfg;
        memset(&cfg, 0xCC, sizeof(cfg));
        
        result = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                                &cfg, sizeof(cfg),
                                &cfg, sizeof(cfg),
                                &bytesRet, NULL);
        
        printf("GET_CLOCK_CONFIG:\n");
        printf("  DeviceIoControl: %s\n", result ? "TRUE" : "FALSE");
        printf("  GetLastError: %lu (0x%08lX)\n", GetLastError(), GetLastError());
        printf("  bytesReturned: %lu (expected %zu)\n", bytesRet, sizeof(cfg));
        printf("  cfg.status: 0x%08X - %s\n", cfg.status, InterpretMarker(cfg.status));
        
        if ((cfg.status & 0xFFFF0000) == 0xDEAD0000) {
            printf("  *** SUCCESS! Clock values:\n");
            printf("      SYSTIM: 0x%016llX\n", cfg.systim);
            printf("      TIMINCA: 0x%08X\n", cfg.timinca);
            printf("      TSAUXC: 0x%08X\n", cfg.tsauxc);
            printf("      Clock Rate: %u MHz\n", cfg.clock_rate_mhz);
        } else if (cfg.status == 0xCCCCCCCC) {
            printf("  *** FAILURE: IOCTL never reached driver!\n");
        } else if (cfg.status != 0) {
            printf("  *** Driver returned error status\n");
        }
        
        printf("\n");
    }
    
    CloseHandle(h);
    printf("========================================\n");
    printf("Test complete!\n");
    return 0;
}
