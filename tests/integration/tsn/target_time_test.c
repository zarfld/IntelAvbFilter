/**
 * @file target_time_test.c
 * @brief Test suite for target time and auxiliary timestamp IOCTLs
 * 
 * Tests target time configuration (TRGTTIML/H) and auxiliary timestamp
 * reading (AUXSTMP0/1) for time-triggered interrupts and SDP pin events.
 * 
 * Based on Intel I210/I226 datasheet specifications.
 */

#include <windows.h>
#include <stdio.h>
#include "../../include/avb_ioctl.h"

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"

static const char* NdisStatusName(ULONG status) {
    switch (status) {
        case 0x00000000: return "NDIS_STATUS_SUCCESS";
        case 0xC0010001: return "NDIS_STATUS_FAILURE";
        case 0xC001000D: return "NDIS_STATUS_INVALID_PARAMETER";
        case 0xC0010004: return "NDIS_STATUS_ADAPTER_NOT_READY";
        default: return "Unknown";
    }
}

int main(void) {
    HANDLE hDevice;
    DWORD bytesReturned;
    BOOL success;
    int exitCode = 0;

    printf("=== Intel AVB Filter - Target Time & Aux Timestamp Test ===\n\n");

    // Open device
    hDevice = CreateFileA(DEVICE_NAME,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open device: %s (Error: %lu)\n", DEVICE_NAME, GetLastError());
        return 1;
    }

    printf("Device opened successfully\n\n");

    // Test 1: Get current system time (for reference)
    printf("--- Test 1: Query Current SYSTIM ---\n");
    {
        AVB_CLOCK_CONFIG clockConfig = {0};

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_GET_CLOCK_CONFIG,
                                 &clockConfig,
                                 sizeof(clockConfig),
                                 &clockConfig,
                                 sizeof(clockConfig),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            ULONGLONG systim_ns = ((ULONGLONG)clockConfig.systim_high << 32) | clockConfig.systim_low;
            printf("  Current SYSTIM: 0x%08X%08X (%llu ns)\n",
                   clockConfig.systim_high, clockConfig.systim_low, systim_ns);
            printf("  TSAUXC: 0x%08X\n", clockConfig.tsauxc);
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 2: Set target time 0 (5 seconds in future)
    printf("--- Test 2: Set Target Time 0 (5s in future) ---\n");
    {
        // First get current time
        AVB_CLOCK_CONFIG clockConfig = {0};
        DeviceIoControl(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG,
                       &clockConfig, sizeof(clockConfig),
                       &clockConfig, sizeof(clockConfig),
                       &bytesReturned, NULL);
        
        ULONGLONG current_ns = ((ULONGLONG)clockConfig.systim_high << 32) | clockConfig.systim_low;
        ULONGLONG target_ns = current_ns + 5000000000ULL;  // +5 seconds

        AVB_TARGET_TIME_REQUEST targetReq = {0};
        targetReq.timer_index = 0;
        targetReq.target_time = target_ns;
        targetReq.enable_interrupt = 1;  // Enable EN_TT0 interrupt
        targetReq.enable_sdp_output = 0;
        targetReq.sdp_mode = 0;

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_TARGET_TIME,
                                 &targetReq,
                                 sizeof(targetReq),
                                 &targetReq,
                                 sizeof(targetReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Current time:  %llu ns\n", current_ns);
            printf("  Target time:   %llu ns\n", target_ns);
            printf("  Delta:         %llu ns (%.2f sec)\n", 
                   target_ns - current_ns, (target_ns - current_ns) / 1e9);
            printf("  Status: %s\n", NdisStatusName(targetReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 3: Set target time 1 (10 seconds in future)
    printf("--- Test 3: Set Target Time 1 (10s in future) ---\n");
    {
        AVB_CLOCK_CONFIG clockConfig = {0};
        DeviceIoControl(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG,
                       &clockConfig, sizeof(clockConfig),
                       &clockConfig, sizeof(clockConfig),
                       &bytesReturned, NULL);
        
        ULONGLONG current_ns = ((ULONGLONG)clockConfig.systim_high << 32) | clockConfig.systim_low;
        ULONGLONG target_ns = current_ns + 10000000000ULL;  // +10 seconds

        AVB_TARGET_TIME_REQUEST targetReq = {0};
        targetReq.timer_index = 1;
        targetReq.target_time = target_ns;
        targetReq.enable_interrupt = 1;  // Enable EN_TT1 interrupt
        targetReq.enable_sdp_output = 0;

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_TARGET_TIME,
                                 &targetReq,
                                 sizeof(targetReq),
                                 &targetReq,
                                 sizeof(targetReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Target time 1: %llu ns\n", target_ns);
            printf("  Delta:         %.2f seconds\n", (target_ns - current_ns) / 1e9);
            printf("  Status: %s\n", NdisStatusName(targetReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 4: Query auxiliary timestamp 0
    printf("--- Test 4: Query Auxiliary Timestamp 0 ---\n");
    {
        AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
        auxReq.timer_index = 0;
        auxReq.clear_flag = 0;  // Don't clear AUTT0 yet

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_GET_AUX_TIMESTAMP,
                                 &auxReq,
                                 sizeof(auxReq),
                                 &auxReq,
                                 sizeof(auxReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Aux timestamp 0: 0x%08X%08X\n",
                   (ULONG)(auxReq.timestamp >> 32), (ULONG)(auxReq.timestamp & 0xFFFFFFFF));
            printf("  Valid (AUTT0):   %s\n", auxReq.valid ? "YES" : "NO");
            if (auxReq.valid) {
                printf("  Value:           %llu ns\n", auxReq.timestamp);
            } else {
                printf("  (No SDP event captured yet)\n");
            }
            printf("  Status: %s\n", NdisStatusName(auxReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 5: Query auxiliary timestamp 1
    printf("--- Test 5: Query Auxiliary Timestamp 1 ---\n");
    {
        AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
        auxReq.timer_index = 1;
        auxReq.clear_flag = 0;

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_GET_AUX_TIMESTAMP,
                                 &auxReq,
                                 sizeof(auxReq),
                                 &auxReq,
                                 sizeof(auxReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Aux timestamp 1: 0x%08X%08X\n",
                   (ULONG)(auxReq.timestamp >> 32), (ULONG)(auxReq.timestamp & 0xFFFFFFFF));
            printf("  Valid (AUTT1):   %s\n", auxReq.valid ? "YES" : "NO");
            if (auxReq.valid) {
                printf("  Value:           %llu ns\n", auxReq.timestamp);
            }
            printf("  Status: %s\n", NdisStatusName(auxReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 6: Clear aux timestamp flag (if valid)
    printf("--- Test 6: Clear Aux Timestamp 0 Flag ---\n");
    {
        AVB_AUX_TIMESTAMP_REQUEST auxReq = {0};
        auxReq.timer_index = 0;
        auxReq.clear_flag = 1;  // Clear AUTT0 after reading

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_GET_AUX_TIMESTAMP,
                                 &auxReq,
                                 sizeof(auxReq),
                                 &auxReq,
                                 sizeof(auxReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Timestamp: %llu ns\n", auxReq.timestamp);
            printf("  Valid before clear: %s\n", auxReq.valid ? "YES" : "NO");
            printf("  Status: %s\n", NdisStatusName(auxReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 7: Verify TSAUXC state
    printf("--- Test 7: Verify TSAUXC Configuration ---\n");
    {
        AVB_CLOCK_CONFIG clockConfig = {0};

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_GET_CLOCK_CONFIG,
                                 &clockConfig,
                                 sizeof(clockConfig),
                                 &clockConfig,
                                 sizeof(clockConfig),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  TSAUXC: 0x%08X\n", clockConfig.tsauxc);
            printf("  EN_TT0 (bit 0):  %s\n", (clockConfig.tsauxc & (1 << 0)) ? "ENABLED" : "disabled");
            printf("  EN_TT1 (bit 4):  %s\n", (clockConfig.tsauxc & (1 << 4)) ? "ENABLED" : "disabled");
            printf("  EN_TS0 (bit 8):  %s\n", (clockConfig.tsauxc & (1 << 8)) ? "ENABLED" : "disabled");
            printf("  EN_TS1 (bit 10): %s\n", (clockConfig.tsauxc & (1 << 10)) ? "ENABLED" : "disabled");
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    CloseHandle(hDevice);
    printf("=== Test Complete (Exit Code: %d) ===\n", exitCode);
    return exitCode;
}
