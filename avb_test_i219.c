/*++

Module Name:

    avb_test_i219.c

Abstract:

    Simple test application for Intel AVB Filter Driver I219 validation
    Tests device detection and basic hardware access

--*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// IOCTL definitions (from avb_integration.h)
#define IOCTL_AVB_INIT_DEVICE         CTL_CODE(FILE_DEVICE_NETWORK, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_READ_REGISTER       CTL_CODE(FILE_DEVICE_NETWORK, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_GET_DEVICE_INFO     CTL_CODE(FILE_DEVICE_NETWORK, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Request structures
typedef struct _AVB_REGISTER_REQUEST {
    ULONG offset;
    ULONG value;
    ULONG status;
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

typedef struct _AVB_DEVICE_INFO {
    ULONG device_type;
    ULONG vendor_id;
    ULONG device_id;
    BOOLEAN hw_access_enabled;
    char device_name[64];
} AVB_DEVICE_INFO, *PAVB_DEVICE_INFO;

/**
 * @brief Test I219 device detection and basic access
 */
int main()
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    DWORD bytesReturned;
    BOOL result;
    
    printf("=== Intel AVB Filter Driver I219 Test ===\n\n");
    
    // Open device
    hDevice = CreateFile(
        L"\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open IntelAvbFilter device (Error: %lu)\n", GetLastError());
        printf("Make sure the driver is installed and loaded.\n");
        return -1;
    }
    
    printf("? Successfully opened IntelAvbFilter device\n");
    
    // Test 1: Initialize device
    printf("\n--- Test 1: Device Initialization ---\n");
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_INIT_DEVICE,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Device initialization: SUCCESS\n");
    } else {
        printf("? Device initialization: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test 2: Get device info
    printf("\n--- Test 2: Device Information ---\n");
    AVB_DEVICE_INFO deviceInfo = {0};
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_DEVICE_INFO,
        NULL,
        0,
        &deviceInfo,
        sizeof(deviceInfo),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Device info: SUCCESS\n");
        printf("   Device Type: %lu\n", deviceInfo.device_type);
        printf("   Vendor ID: 0x%04X\n", deviceInfo.vendor_id);
        printf("   Device ID: 0x%04X\n", deviceInfo.device_id);
        printf("   Hardware Access: %s\n", deviceInfo.hw_access_enabled ? "ENABLED" : "SIMULATED");
        printf("   Device Name: %s\n", deviceInfo.device_name);
        
        // Check if this is I219
        if (deviceInfo.vendor_id == 0x8086 && 
            (deviceInfo.device_id == 0x15B7 || deviceInfo.device_id == 0x15B8 || 
             deviceInfo.device_id == 0x15D6 || deviceInfo.device_id == 0x15D7 || 
             deviceInfo.device_id == 0x15D8 || deviceInfo.device_id == 0x0DC7 || 
             deviceInfo.device_id == 0x1570 || deviceInfo.device_id == 0x15E3)) {
            printf("?? CONFIRMED: This is an Intel I219 controller!\n");
        }
    } else {
        printf("? Device info: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test 3: Read basic registers (from SSOT where available)
    printf("\n--- Test 3: Register Access Tests ---\n");
    
    // Test reading device control register
    AVB_REGISTER_REQUEST regRequest = {0};
    regRequest.offset = 0x00000;  // Device Control Register (CTRL)
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_READ_REGISTER,
        &regRequest,
        sizeof(regRequest),
        &regRequest,
        sizeof(regRequest),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Control Register (0x00000): 0x%08X\n", regRequest.value);
        if (regRequest.value != 0 && regRequest.value != 0x12340000) {
            printf("   ?? Looks like REAL hardware value!\n");
        } else {
            printf("   ??  Might be simulated value\n");
        }
    } else {
        printf("? Control Register read: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test reading device status register  
    regRequest.offset = 0x00008;  // Device Status Register (STATUS)
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_READ_REGISTER,
        &regRequest,
        sizeof(regRequest),
        &regRequest,
        sizeof(regRequest),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Status Register (0x00008): 0x%08X\n", regRequest.value);
        if (regRequest.value & 0x00000002) {  // Link up bit
            printf("   ?? Link Status: UP\n");
        } else {
            printf("   ?? Link Status: DOWN\n");
        }
    } else {
        printf("? Status Register read: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test I219 IEEE 1588 timestamp register
    printf("\n--- Test 4: I219 IEEE 1588 Timestamp ---\n");
    printf("I219 timestamp register offsets are not verified in SSOT yet; skipping raw reads.\n");
    printf("Use IOCTL_AVB_GET_TIMESTAMP once the kernel path is wired for I219.\n");
    
    // Summary
    printf("\n=== TEST SUMMARY ===\n");
    printf("If you see 'REAL hardware' values and enabled hardware access,\n");
    printf("your I219 controller is working with the driver!\n");
    printf("\nTo enable debug output:\n");
    printf("1. Use DebugView.exe (from Microsoft Sysinternals)\n");
    printf("2. Enable 'Capture Kernel' option\n"); 
    printf("3. Look for messages containing 'AvbMmioReadReal' and '(REAL HARDWARE)'\n");
    
    CloseHandle(hDevice);
    return 0;
}