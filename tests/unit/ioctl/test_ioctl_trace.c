/**
 * @file test_ioctl_trace.c
 * @brief Minimal test to trace IOCTL codes sent to driver
 */

#include <windows.h>
#include <stdio.h>

// Calculate IOCTL codes manually
#define FILE_DEVICE_PHYSICAL_NETCARD 0x17
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0

#define CTL_CODE_CALC(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))

#define IOCTL_AVB_READ_REGISTER    CTL_CODE_CALC(FILE_DEVICE_PHYSICAL_NETCARD, 22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_GET_CLOCK_CONFIG CTL_CODE_CALC(FILE_DEVICE_PHYSICAL_NETCARD, 39, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    unsigned int offset;
    unsigned int value;
    unsigned int status;
} AVB_REGISTER_ACCESS;

typedef struct {
    unsigned long long systim;
    unsigned int timinca;
    unsigned int tsauxc;
    unsigned int clock_rate_mhz;
    unsigned int status;
} AVB_CLOCK_CONFIG;

int main(void) {
    HANDLE hDriver;
    DWORD bytesReturned;
    BOOL success;
    
    printf("===========================================\n");
    printf("IOCTL CODE VERIFICATION TEST\n");
    printf("===========================================\n\n");
    
    // Print calculated IOCTL codes
    printf("Calculated IOCTL codes:\n");
    printf("  IOCTL_AVB_READ_REGISTER    = 0x%08X\n", IOCTL_AVB_READ_REGISTER);
    printf("  IOCTL_AVB_GET_CLOCK_CONFIG = 0x%08X\n\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    
    // Open driver
    printf("Opening driver: \\\\.\\IntelAvbFilter\n");
    hDriver = CreateFileA("\\\\.\\IntelAvbFilter",
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    
    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("ERROR: Could not open driver (error %lu)\n", GetLastError());
        return 1;
    }
    printf("SUCCESS: Driver opened (handle=%p)\n\n", hDriver);
    
    // Test 1: READ_REGISTER (we know this works)
    printf("TEST 1: IOCTL_AVB_READ_REGISTER (0x%08X)\n", IOCTL_AVB_READ_REGISTER);
    printf("  Reading CTRL register (offset 0x00000)...\n");
    
    AVB_REGISTER_ACCESS reg;
    ZeroMemory(&reg, sizeof(reg));
    reg.offset = 0x00000;  // CTRL register
    
    bytesReturned = 0;
    success = DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                             &reg, sizeof(reg),
                             &reg, sizeof(reg),
                             &bytesReturned, NULL);
    
    printf("  DeviceIoControl returned: %s\n", success ? "SUCCESS" : "FAILED");
    printf("  Bytes returned: %lu (expected %lu)\n", bytesReturned, (DWORD)sizeof(reg));
    printf("  Value: 0x%08X\n", reg.value);
    printf("  Status: 0x%08X\n\n", reg.status);
    
    // Test 2: GET_CLOCK_CONFIG (problematic)
    printf("TEST 2: IOCTL_AVB_GET_CLOCK_CONFIG (0x%08X)\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    
    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    
    bytesReturned = 0;
    success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                             &cfg, sizeof(cfg),
                             &cfg, sizeof(cfg),
                             &bytesReturned, NULL);
    
    printf("  DeviceIoControl returned: %s\n", success ? "SUCCESS" : "FAILED");
    printf("  GetLastError: %lu\n", GetLastError());
    printf("  Bytes returned: %lu (expected %lu)\n", bytesReturned, (DWORD)sizeof(cfg));
    printf("  SYSTIM: 0x%016llX\n", cfg.systim);
    printf("  TIMINCA: 0x%08X\n", cfg.timinca);
    printf("  TSAUXC: 0x%08X\n\n", cfg.tsauxc);
    
    // Analysis
    printf("===========================================\n");
    printf("ANALYSIS:\n");
    printf("===========================================\n");
    
    if (success && bytesReturned == 0) {
        printf("PROBLEM CONFIRMED:\n");
        printf("  DeviceIoControl returns SUCCESS\n");
        printf("  But bytesReturned = 0\n");
        printf("  This means the IOCTL is being handled\n");
        printf("  but the driver is returning STATUS_DEVICE_NOT_READY\n");
        printf("  or failing the hardware_context check.\n\n");
        printf("CONCLUSION:\n");
        printf("  The IOCTL IS reaching the driver!\n");
        printf("  But hardware_context check is failing.\n");
    } else if (!success) {
        printf("DeviceIoControl failed entirely.\n");
        printf("  Error code: %lu\n", GetLastError());
    } else if (bytesReturned == sizeof(cfg)) {
        printf("SUCCESS: IOCTL returned expected data size!\n");
    }
    
    CloseHandle(hDriver);
    
    printf("\nPress Enter to exit...\n");
    getchar();
    
    return 0;
}
