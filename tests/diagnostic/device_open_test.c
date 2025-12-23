/**
 * @file device_open_test.c
 * @brief Simple test to verify driver device can be opened
 * 
 * Tests basic device access before running complex hardware tests
 */

#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("INTEL AVB FILTER DEVICE OPEN TEST\n");
    printf("========================================\n\n");
    
    printf("Attempting to open device: \\\\.\\IntelAvbFilter\n");
    
    HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                          GENERIC_READ | GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("\n? FAILED: Could not open device (Error: %lu)\n", error);
        
        switch (error) {
            case ERROR_FILE_NOT_FOUND:
                printf("   ERROR_FILE_NOT_FOUND (2) - Device object not created\n");
                printf("   Check: sc query IntelAvbFilter\n");
                break;
            case ERROR_PATH_NOT_FOUND:
                printf("   ERROR_PATH_NOT_FOUND (3) - Symbolic link not created\n");
                printf("   The driver is loaded but device interface not initialized\n");
                printf("   This can happen if the filter hasn't attached to an adapter yet\n");
                break;
            case ERROR_ACCESS_DENIED:
                printf("   ERROR_ACCESS_DENIED (5) - Run as Administrator\n");
                break;
            case ERROR_SHARING_VIOLATION:
                printf("   ERROR_SHARING_VIOLATION (32) - Device already opened exclusively\n");
                break;
            default:
                printf("   Unknown error code\n");
                break;
        }
        
        printf("\nDebugging steps:\n");
        printf("1. Check driver status: sc query IntelAvbFilter\n");
        printf("2. Check for errors in Event Viewer:\n");
        printf("   eventvwr.msc -> Windows Logs -> System\n");
        printf("3. Enable DebugView to see driver debug output:\n");
        printf("   - Download Sysinternals DebugView\n");
        printf("   - Run as Administrator\n");
        printf("   - Enable 'Capture Kernel' option\n");
        printf("4. Check if filter attached to any adapter:\n");
        printf("   netcfg -s n\n");
        
        return -1;
    }
    
    printf("\n? SUCCESS: Device opened!\n");
    printf("   Handle: 0x%p\n", h);
    
    // Try a simple IOCTL
    printf("\nTesting basic IOCTL communication...\n");
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(h, 
                                  0x00000000,  // Dummy IOCTL
                                  NULL, 0,
                                  NULL, 0,
                                  &bytesReturned, 
                                  NULL);
    
    if (!result) {
        DWORD ioctlError = GetLastError();
        if (ioctlError == ERROR_INVALID_FUNCTION || ioctlError == ERROR_NOT_SUPPORTED) {
            printf("? Device responds to IOCTLs (invalid function expected)\n");
        } else {
            printf("? IOCTL communication error: %lu\n", ioctlError);
        }
    }
    
    CloseHandle(h);
    
    printf("\n========================================\n");
    printf("DEVICE ACCESS WORKING - Driver Ready!\n");
    printf("========================================\n");
    printf("\nYou can now run hardware tests:\n");
    printf("  - ptp_clock_control_test.exe\n");
    printf("  - avb_test_i210_um.exe\n");
    
    return 0;
}
