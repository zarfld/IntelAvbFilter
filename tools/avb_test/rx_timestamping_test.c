/**
 * @file rx_timestamping_test.c
 * @brief Test suite for RX packet timestamping configuration IOCTLs
 * 
 * Tests the complete RX packet timestamping configuration sequence:
 * 1. Enable 16-byte timestamp buffer (RXPBSIZE.CFG_TS_EN)
 * 2. Enable per-queue timestamping (SRRCTL[n].TIMESTAMP)
 * 
 * Based on Intel I210/I226 datasheet requirements.
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

    printf("=== Intel AVB Filter - RX Packet Timestamping Test ===\n\n");

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

    // Test 1: Query current RXPBSIZE state (read operation)
    printf("--- Test 1: Query Current RXPBSIZE State ---\n");
    {
        AVB_RX_TIMESTAMP_REQUEST rxReq = {0};
        rxReq.enable = 0;  // Just query, don't change

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_RX_TIMESTAMP,
                                 &rxReq,
                                 sizeof(rxReq),
                                 &rxReq,
                                 sizeof(rxReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Previous RXPBSIZE: 0x%08X\n", rxReq.previous_rxpbsize);
            printf("  Current RXPBSIZE:  0x%08X\n", rxReq.current_rxpbsize);
            printf("  CFG_TS_EN bit (29): %s\n", 
                   (rxReq.current_rxpbsize & (1 << 29)) ? "ENABLED" : "DISABLED");
            printf("  Requires reset: %s\n", rxReq.requires_reset ? "YES" : "NO");
            printf("  Status: %s\n", NdisStatusName(rxReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 2: Enable RX packet timestamping (CFG_TS_EN=1)
    printf("--- Test 2: Enable RX Packet Timestamping ---\n");
    {
        AVB_RX_TIMESTAMP_REQUEST rxReq = {0};
        rxReq.enable = 1;  // Enable 16-byte timestamp buffer

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_RX_TIMESTAMP,
                                 &rxReq,
                                 sizeof(rxReq),
                                 &rxReq,
                                 sizeof(rxReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Previous RXPBSIZE: 0x%08X\n", rxReq.previous_rxpbsize);
            printf("  Current RXPBSIZE:  0x%08X\n", rxReq.current_rxpbsize);
            printf("  CFG_TS_EN changed: %s\n", rxReq.requires_reset ? "YES" : "NO (already enabled)");
            if (rxReq.requires_reset) {
                printf("  WARNING: Port software reset (CTRL.RST) required!\n");
            }
            printf("  Status: %s\n", NdisStatusName(rxReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 3: Enable per-queue timestamping for queue 0
    printf("--- Test 3: Enable Queue 0 Timestamping ---\n");
    {
        AVB_QUEUE_TIMESTAMP_REQUEST queueReq = {0};
        queueReq.queue_index = 0;
        queueReq.enable = 1;  // Enable TIMESTAMP bit in SRRCTL[0]

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_QUEUE_TIMESTAMP,
                                 &queueReq,
                                 sizeof(queueReq),
                                 &queueReq,
                                 sizeof(queueReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Previous SRRCTL[0]: 0x%08X\n", queueReq.previous_srrctl);
            printf("  Current SRRCTL[0]:  0x%08X\n", queueReq.current_srrctl);
            printf("  TIMESTAMP bit (30): %s\n",
                   (queueReq.current_srrctl & (1 << 30)) ? "ENABLED" : "DISABLED");
            printf("  Status: %s\n", NdisStatusName(queueReq.status));
        } else {
            printf("  FAILED: DeviceIoControl error %lu\n", GetLastError());
            exitCode = 1;
        }
        printf("\n");
    }

    // Test 4: Verify all queues (0-3)
    printf("--- Test 4: Query All Queue Timestamp States ---\n");
    for (int q = 0; q < 4; q++) {
        AVB_QUEUE_TIMESTAMP_REQUEST queueReq = {0};
        queueReq.queue_index = q;
        queueReq.enable = 0;  // Just query

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_QUEUE_TIMESTAMP,
                                 &queueReq,
                                 sizeof(queueReq),
                                 &queueReq,
                                 sizeof(queueReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Queue %d SRRCTL: 0x%08X (TIMESTAMP=%s)\n",
                   q, queueReq.current_srrctl,
                   (queueReq.current_srrctl & (1 << 30)) ? "ON" : "OFF");
        } else {
            printf("  Queue %d: FAILED (error %lu)\n", q, GetLastError());
        }
    }
    printf("\n");

    // Test 5: Disable RX timestamping
    printf("--- Test 5: Disable RX Packet Timestamping ---\n");
    {
        AVB_RX_TIMESTAMP_REQUEST rxReq = {0};
        rxReq.enable = 0;  // Disable CFG_TS_EN

        success = DeviceIoControl(hDevice,
                                 IOCTL_AVB_SET_RX_TIMESTAMP,
                                 &rxReq,
                                 sizeof(rxReq),
                                 &rxReq,
                                 sizeof(rxReq),
                                 &bytesReturned,
                                 NULL);

        if (success) {
            printf("  Previous RXPBSIZE: 0x%08X\n", rxReq.previous_rxpbsize);
            printf("  Current RXPBSIZE:  0x%08X\n", rxReq.current_rxpbsize);
            printf("  CFG_TS_EN bit (29): %s\n",
                   (rxReq.current_rxpbsize & (1 << 29)) ? "ENABLED" : "DISABLED");
            printf("  Status: %s\n", NdisStatusName(rxReq.status));
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
