/**
 * @file test_send_ptp_debug.c
 * @brief Diagnostic test for IOCTL_AVB_TEST_SEND_PTP (Step 8e debug)
 * 
 * Purpose: Diagnose why send_ptp_packet() fails by printing detailed error info
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define NDIS_STATUS_SUCCESS     ((ULONG)0x00000000L)
#define NDIS_STATUS_FAILURE     ((ULONG)0xC0000001L)
#define NDIS_STATUS_RESOURCES   ((ULONG)0xC000009AL)
#define NDIS_STATUS_ADAPTER_NOT_READY    ((ULONG)0xC00000ADL)
#define NDIS_STATUS_ADAPTER_NOT_FOUND    ((ULONG)0xC0010002L)

typedef ULONG NDIS_STATUS;

#include "../../include/avb_ioctl.h"

int main(void) {
    printf("====================================================================\n");
    printf("  IOCTL_AVB_TEST_SEND_PTP Diagnostic Tool (Step 8e)\n");
    printf("====================================================================\n\n");
    
    // Open device
    printf("Opening device: \\\\.\\IntelAvbFilter\n");
    HANDLE hDevice = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open device (error=%lu)\n", GetLastError());
        return 1;
    }
    printf("[OK] Device opened\n\n");
    
    // Enumerate adapters first
    printf("Enumerating adapters...\n");
    for (UINT32 i = 0; i < 10; i++) {
        AVB_ENUM_REQUEST enum_req = {0};
        enum_req.index = i;
        
        DWORD bytesRet = 0;
        BOOL success = DeviceIoControl(
            hDevice,
            IOCTL_AVB_ENUM_ADAPTERS,
            &enum_req, sizeof(enum_req),
            &enum_req, sizeof(enum_req),
            &bytesRet,
            NULL
        );
        
        if (!success || enum_req.status != NDIS_STATUS_SUCCESS) {
            break;
        }
        
        printf("  Adapter %u: VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
               i, enum_req.vendor_id, enum_req.device_id, enum_req.capabilities);
    }
    printf("\n");
    
    // Test IOCTL_AVB_TEST_SEND_PTP for adapter index 0
    printf("Testing IOCTL_AVB_TEST_SEND_PTP (Code 51)...\n");
    printf("  Adapter Index: 0\n");
    printf("  Sequence ID: 1\n\n");
    
    AVB_TEST_SEND_PTP_REQUEST req = {0};
    req.adapter_index = 0;
    req.sequence_id = 1;
    
    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_AVB_TEST_SEND_PTP,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
    printf("IOCTL Result:\n");
    printf("  DeviceIoControl success: %s\n", success ? "YES" : "NO");
    if (!success) {
        printf("  GetLastError(): %lu (0x%08lX)\n", GetLastError(), GetLastError());
    }
    printf("  BytesReturned: %lu\n", bytesReturned);
    printf("  req.status: 0x%08X\n", req.status);
    printf("  req.packets_sent: %u\n\n", req.packets_sent);
    
    // Decode status
    printf("Status Interpretation:\n");
    if (req.status == NDIS_STATUS_SUCCESS) {
        printf("  ✓ SUCCESS - Packet sent successfully\n");
    } else if (req.status == NDIS_STATUS_ADAPTER_NOT_FOUND) {
        printf("  ✗ NDIS_STATUS_ADAPTER_NOT_FOUND (0xC0010002)\n");
        printf("    → Adapter index %u not found in FilterModuleList\n", req.adapter_index);
        printf("    → Check: Is I226 adapter bound to filter?\n");
    } else if (req.status == NDIS_STATUS_RESOURCES) {
        printf("  ✗ NDIS_STATUS_RESOURCES (0xC000009A)\n");
        printf("    → NBL/NB pools not allocated\n");
        printf("    → Check: Did AvbCreateMinimalContext succeed?\n");
    } else if (req.status == NDIS_STATUS_ADAPTER_NOT_READY) {
        printf("  ✗ NDIS_STATUS_ADAPTER_NOT_READY (0xC00000AD)\n");
        printf("    → filter_instance is NULL\n");
        printf("    → Check: Is filter attached to adapter?\n");
    } else if (req.status == NDIS_STATUS_FAILURE) {
        printf("  ✗ NDIS_STATUS_FAILURE (0xC0000001)\n");
        printf("    → Generic failure in IOCTL handler\n");
    } else {
        printf("  ✗ UNKNOWN STATUS: 0x%08X\n", req.status);
    }
    
    printf("\n====================================================================\n");
    if (req.status == NDIS_STATUS_SUCCESS && req.packets_sent > 0) {
        printf("RESULT: SUCCESS - Packet injection working!\n");
    } else {
        printf("RESULT: FAILED - See status interpretation above\n");
    }
    printf("====================================================================\n");
    
    CloseHandle(hDevice);
    return (req.status == NDIS_STATUS_SUCCESS) ? 0 : 1;
}
