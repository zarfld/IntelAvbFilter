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
    printf("========================================\n\n");

    // Open driver
    HANDLE hDriver = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open driver (error %lu)\n", GetLastError());
        printf("Make sure driver is installed and running\n");
        return 1;
    }
    printf("✓ Driver opened\n");
    printf("  Using default context (adapter 0 - the one with full initialization)\n\n");

    // CRITICAL: DO NOT call OPEN_ADAPTER - it switches context away from adapter 0
    // Adapter 0 is the only one with full hardware initialization
    // Other adapters remain in BOUND state until explicitly initialized
    
    DWORD bytesReturned = 0;

    // STEP 0: Initialize device first
    printf("INIT: Calling INIT_DEVICE to ensure hardware is ready\n");
    printf("-------------------------------------------------------\n");
    DWORD dummy = 0;
    BOOL initSuccess = DeviceIoControl(hDriver, IOCTL_AVB_INIT_DEVICE,
                                       &dummy, sizeof(dummy),
                                       &dummy, sizeof(dummy),
                                       &bytesReturned, NULL);
    printf("  Result: %s (bytes=%lu)\n\n", 
           initSuccess ? "SUCCESS" : "FAILED", bytesReturned);

    // Test 1: Get clock config via public API
    printf("TEST 1: Get clock config via IOCTL_AVB_GET_CLOCK_CONFIG\n");
    printf("-----------------------------------------------\n");
    
    AVB_CLOCK_CONFIG cfg = {0};
    BOOL success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                                   &cfg, sizeof(cfg),
                                   &cfg, sizeof(cfg),
                                   &bytesReturned, NULL);
    
    if (!success) {
        printf("ERROR: DeviceIoControl failed (error %lu)\n", GetLastError());
    } else {
        printf("SUCCESS: DeviceIoControl returned\n");
        printf("  Bytes returned: %lu (expected %lu)\n", bytesReturned, (DWORD)sizeof(cfg));
    }
    
    printf("\nClock Config Results:\n");
    printf("  SYSTIM:     0x%016llX ns\n", cfg.systim);
    printf("  TIMINCA:    0x%08X\n", cfg.timinca);
    printf("  TSAUXC:     0x%08X\n", cfg.tsauxc);
    printf("  Clock Rate: %u MHz\n", cfg.clock_rate_mhz);
    printf("  Status:     0x%08X\n\n", cfg.status);

    int testResult = 0;
    if (!success || cfg.status != 0) {
        printf("[FAIL] GET_CLOCK_CONFIG returned status 0x%08X\n", cfg.status);
        testResult = 1;
    } else {
        printf("[PASS] GET_CLOCK_CONFIG succeeded\n");
        if (cfg.systim == 0 && cfg.timinca == 0 && cfg.tsauxc == 0) {
            printf("[WARN] All fields are zero — hardware may not be initialized\n");
        }
    }

    CloseHandle(hDriver);

    printf("\n========================================\n");
    return testResult;
}
