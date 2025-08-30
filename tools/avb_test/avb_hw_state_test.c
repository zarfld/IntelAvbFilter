/*++

Module Name:

    avb_hw_state_test.c

Abstract:

    Simple hardware state test tool that triggers forced BAR0 discovery

--*/

#include <windows.h>
#include <stdio.h>
#include "../../include/avb_ioctl.h"

int main()
{
    printf("Intel AVB Filter - Hardware State Test\n");
    printf("=====================================\n\n");
    
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("? ERROR: Cannot open device (Error: %lu)\n", GetLastError());
        return 1;
    }
    
    printf("? Device opened successfully\n\n");
    
    // Call init device first
    printf("?? Calling IOCTL_AVB_INIT_DEVICE...\n");
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &br, NULL);
    printf("   Result: %s (GLE: %lu)\n\n", ok ? "SUCCESS" : "FAILED", GetLastError());
    
    // Query hardware state (this will trigger forced BAR0 discovery)
    printf("?? Calling IOCTL_AVB_GET_HW_STATE (will trigger forced BAR0 discovery if needed)...\n");
    AVB_HW_STATE_QUERY q;
    ZeroMemory(&q, sizeof(q));
    
    ok = DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE, &q, sizeof(q), &q, sizeof(q), &br, NULL);
    
    if (ok) {
        printf("? Hardware State Query SUCCESS:\n");
        printf("   Hardware State: %u\n", q.hw_state);
        printf("   Vendor ID: 0x%04X\n", q.vendor_id);
        printf("   Device ID: 0x%04X\n", q.device_id);
        printf("   Capabilities: 0x%08X\n", q.capabilities);
        
        const char* state_names[] = {"UNBOUND", "BOUND", "BAR_MAPPED", "PTP_READY"};
        if (q.hw_state < 4) {
            printf("   State Name: %s\n", state_names[q.hw_state]);
        }
        
        if (q.hw_state >= 2) { // AVB_HW_BAR_MAPPED
            printf("?? Hardware is ready for register access!\n");
        } else {
            printf("?? Hardware still needs BAR0 mapping (check debug logs)\n");
        }
    } else {
        printf("? Hardware State Query FAILED (Error: %lu)\n", GetLastError());
    }
    
    printf("\n?? Check debug output (DebugView/Event Viewer) for detailed BAR0 discovery logs\n");
    
    CloseHandle(h);
    return 0;
}