/**
 * Direct GET_CLOCK_CONFIG test (bypass OPEN_ADAPTER)
 * Tests if GET_CLOCK_CONFIG works without OPEN_ADAPTER
 */

#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main(void) {
    printf("Direct GET_CLOCK_CONFIG Test (No OPEN_ADAPTER)\n");
    printf("==============================================\n\n");
    
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to open device: error=%lu\n", GetLastError());
        return 1;
    }
    
    printf("Device opened: handle=%p\n\n", h);
    
    // Test 1: INIT_DEVICE
    printf("Test 1: INIT_DEVICE\n");
    printf("-------------------\n");
    DWORD bytes = 0;
    BOOL result = DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytes, NULL);
    printf("  Result: %d, Error: %lu, Bytes: %lu\n\n", result, GetLastError(), bytes);
    
    // Test 2: GET_CLOCK_CONFIG directly (no OPEN_ADAPTER)
    printf("Test 2: GET_CLOCK_CONFIG (Direct - No OPEN)\n");
    printf("--------------------------------------------\n");
    AVB_CLOCK_CONFIG cfg;
    memset(&cfg, 0xCC, sizeof(cfg));
    
    result = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, 
                            &cfg, sizeof(cfg), 
                            &cfg, sizeof(cfg), 
                            &bytes, NULL);
    
    printf("  DeviceIoControl: result=%d, error=%lu, bytes=%lu\n", result, GetLastError(), bytes);
    printf("  cfg.status: 0x%08X\n", cfg.status);
    printf("  cfg.systim: 0x%016llX\n", cfg.systim);
    printf("  cfg.timinca: 0x%08X\n", cfg.timinca);
    printf("  cfg.clock_rate_mhz: %lu MHz\n", cfg.clock_rate_mhz);
    
    if (bytes == sizeof(cfg) && cfg.status != 0xCCCCCCCC) {
        printf("\n  ✓ GET_CLOCK_CONFIG WORKS without OPEN_ADAPTER!\n");
        printf("  ✓ Retrieved %lu bytes of clock data\n", bytes);
    } else {
        printf("\n  ✗ GET_CLOCK_CONFIG failed or returned unchanged buffer\n");
    }
    
    CloseHandle(h);
    
    printf("\n==============================================\n");
    printf("Analysis:\n");
    printf("- If this works, OPEN_ADAPTER is not required\n");
    printf("- Driver can access first Intel adapter directly\n");
    printf("- Multi-adapter support needs OPEN_ADAPTER fix\n");
    
    return 0;
}
