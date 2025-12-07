/**
 * Diagnostic: Check hw_state before calling GET_CLOCK_CONFIG
 */
#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main() {
    printf("=== Hardware State Diagnostic ===\n\n");
    
    HANDLE hDriver = CreateFileA("\\\\.\\IntelAvbFilter",
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("FAILED: CreateFile error %lu\n", GetLastError());
        return 1;
    }
    
    printf("Driver opened.\n\n");
    
    // Query device info to check hw_state
    printf("Step 1: GET_DEVICE_INFO\n");
    AVB_DEVICE_INFO_REQUEST devInfo;
    ZeroMemory(&devInfo, sizeof(devInfo));
    DWORD bytesRet = 0;
    
    BOOL success = DeviceIoControl(hDriver, IOCTL_AVB_GET_DEVICE_INFO,
                                    &devInfo, sizeof(devInfo),
                                    &devInfo, sizeof(devInfo),
                                    &bytesRet, NULL);
    
    printf("  Result: %s\n", success ? "SUCCESS" : "FAILED");
    printf("  BytesReturned: %lu\n", bytesRet);
    
    if (success && bytesRet > 0) {
        printf("  VID=0x%04X DID=0x%04X\n", devInfo.pci_vendor_id, devInfo.pci_device_id);
        printf("  hw_state=%u ", devInfo.hw_state);
        
        // Decode hw_state
        const char* stateNames[] = {
            "UNINITIALIZED", "DEVICE_FOUND", "CONTEXT_CREATED", 
            "BAR_MAPPED", "REGISTERS_CONFIGURED", "FULLY_OPERATIONAL"
        };
        if (devInfo.hw_state < 6) {
            printf("(%s)\n", stateNames[devInfo.hw_state]);
        } else {
            printf("(UNKNOWN)\n");
        }
        
        printf("  capabilities=0x%016llX\n", devInfo.capabilities);
        
        // Check if hw_state >= BAR_MAPPED (3)
        if (devInfo.hw_state < 3) {
            printf("\n  *** WARNING: hw_state < BAR_MAPPED ***\n");
            printf("  *** GET_CLOCK_CONFIG will return STATUS_DEVICE_NOT_READY ***\n");
        }
    }
    
    printf("\nStep 2: GET_CLOCK_CONFIG\n");
    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    bytesRet = 0;
    
    success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                              &cfg, sizeof(cfg),
                              &cfg, sizeof(cfg),
                              &bytesRet, NULL);
    
    printf("  Result: %s\n", success ? "SUCCESS" : "FAILED");
    if (!success) {
        DWORD err = GetLastError();
        printf("  GetLastError: %lu (0x%08X)\n", err, err);
        
        // ERROR_NOT_READY = 21 = 0x15
        if (err == 21 || err == ERROR_NOT_READY) {
            printf("    -> Device not ready (hw_state too low)\n");
        }
    }
    printf("  BytesReturned: %lu (expected %zu)\n", bytesRet, sizeof(cfg));
    
    if (bytesRet > 0) {
        printf("  cfg.systim: 0x%016llX\n", cfg.systim);
        printf("  cfg.timinca: 0x%08X\n", cfg.timinca);
        printf("  cfg.tsauxc: 0x%08X\n", cfg.tsauxc);
    }
    
    CloseHandle(hDriver);
    return (bytesRet == sizeof(cfg)) ? 0 : 1;
}
