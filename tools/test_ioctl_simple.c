/**
 * Minimal IOCTL test - just calls GET_CLOCK_CONFIG and shows return values
 */
#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main() {
    printf("Opening driver...\n");
    HANDLE hDriver = CreateFileA("\\\\.\\IntelAvbFilter",
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("FAILED: CreateFile error %lu\n", GetLastError());
        return 1;
    }
    
    printf("Driver opened. Handle=%p\n", hDriver);
    printf("Calling IOCTL_AVB_GET_CLOCK_CONFIG (0x%08X)...\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    
    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    DWORD bytesReturned = 0;
    
    BOOL success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                                   &cfg, sizeof(cfg),
                                   &cfg, sizeof(cfg),
                                   &bytesReturned, NULL);
    
    printf("\nRESULT:\n");
    printf("  DeviceIoControl returned: %s\n", success ? "TRUE" : "FALSE");
    if (!success) {
        printf("  GetLastError()=%lu\n", GetLastError());
    }
    printf("  Bytes returned: %lu (expected %lu)\n", bytesReturned, (DWORD)sizeof(cfg));
    printf("  cfg.systim: 0x%016llX\n", cfg.systim);
    printf("  cfg.timinca: 0x%08X\n", cfg.timinca);
    printf("  cfg.tsauxc: 0x%08X\n", cfg.tsauxc);
    printf("  cfg.clock_rate_mhz: %u\n", cfg.clock_rate_mhz);
    printf("  cfg.status: 0x%08X\n", cfg.status);
    
    CloseHandle(hDriver);
    return (bytesReturned == sizeof(cfg)) ? 0 : 1;
}
