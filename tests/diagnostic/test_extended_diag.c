/**
 * Extended IOCTL test with detailed status reporting
 */
#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include "../include/avb_ioctl.h"

// NTSTATUS codes
#define STATUS_DEVICE_NOT_READY  ((NTSTATUS)0xC00000A1L)
#define STATUS_BUFFER_TOO_SMALL  ((NTSTATUS)0xC0000023L)

typedef struct _IO_STATUS_BLOCK_HACK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK_HACK;

int main() {
    printf("=== Extended IOCTL Diagnostic ===\n\n");
    
    printf("Opening driver...\n");
    HANDLE hDriver = CreateFileA("\\\\.\\IntelAvbFilter",
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("FAILED: CreateFile error %lu\n", GetLastError());
        return 1;
    }
    
    printf("Driver opened. Handle=%p\n\n", hDriver);
    
    // Test 1: Check buffer sizes
    printf("Sizeof checks:\n");
    printf("  sizeof(AVB_CLOCK_CONFIG) = %zu\n", sizeof(AVB_CLOCK_CONFIG));
    printf("  IOCTL code = 0x%08X\n\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    
    // Test 2: Try READ_REGISTER first (known working)
    printf("Test 1: READ_REGISTER (baseline)\n");
    AVB_REGISTER_REQUEST regReq;
    ZeroMemory(&regReq, sizeof(regReq));
    regReq.offset = 0x0B600;  // SYSTIML
    DWORD bytesRet1 = 0;
    
    BOOL success1 = DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                                    &regReq, sizeof(regReq),
                                    &regReq, sizeof(regReq),
                                    &bytesRet1, NULL);
    
    printf("  Result: %s, BytesReturned=%lu, Value=0x%08X\n\n", 
           success1 ? "SUCCESS" : "FAILED", bytesRet1, regReq.value);
    
    // Test 3: GET_CLOCK_CONFIG
    printf("Test 2: GET_CLOCK_CONFIG\n");
    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    DWORD bytesRet2 = 0;
    
    BOOL success2 = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                                    &cfg, sizeof(cfg),
                                    &cfg, sizeof(cfg),
                                    &bytesRet2, NULL);
    
    DWORD lastError = GetLastError();
    
    printf("  DeviceIoControl: %s\n", success2 ? "SUCCESS" : "FAILED");
    if (!success2) {
        printf("  GetLastError: %lu (0x%08X)\n", lastError, lastError);
        
        if (lastError == ERROR_NOT_READY) {
            printf("    -> ERROR_NOT_READY (device not ready)\n");
        } else if (lastError == ERROR_INSUFFICIENT_BUFFER) {
            printf("    -> ERROR_INSUFFICIENT_BUFFER (buffer too small)\n");
        }
    }
    printf("  BytesReturned: %lu (expected %zu)\n", bytesRet2, sizeof(cfg));
    printf("  cfg.systim: 0x%016llX\n", cfg.systim);
    printf("  cfg.timinca: 0x%08X\n", cfg.timinca);
    printf("  cfg.tsauxc: 0x%08X\n", cfg.tsauxc);
    printf("  cfg.clock_rate_mhz: %u\n", cfg.clock_rate_mhz);
    printf("  cfg.status: 0x%08X\n\n", cfg.status);
    
    // Try to query device info
    printf("Test 3: GET_DEVICE_INFO\n");
    AVB_DEVICE_INFO_REQUEST devInfo;
    ZeroMemory(&devInfo, sizeof(devInfo));
    devInfo.buffer_size = sizeof(devInfo.device_info);
    DWORD bytesRet3 = 0;
    
    BOOL success3 = DeviceIoControl(hDriver, IOCTL_AVB_GET_DEVICE_INFO,
                                    &devInfo, sizeof(devInfo),
                                    &devInfo, sizeof(devInfo),
                                    &bytesRet3, NULL);
    
    printf("  Result: %s, BytesReturned=%lu\n", success3 ? "SUCCESS" : "FAILED", bytesRet3);
    if (success3 && bytesRet3 > 0) {
        printf("  Device Info: %s\n", devInfo.device_info);
        
        // Get hardware state for vendor/device/capabilities
        AVB_HW_STATE_QUERY hwState;
        ZeroMemory(&hwState, sizeof(hwState));
        DWORD bytesRet4 = 0;
        if (DeviceIoControl(hDriver, IOCTL_AVB_GET_HW_STATE,
                           &hwState, sizeof(hwState),
                           &hwState, sizeof(hwState),
                           &bytesRet4, NULL)) {
            printf("  VID=0x%04X DID=0x%04X\n", hwState.vendor_id, hwState.device_id);
            printf("  hw_state=%u\n", hwState.hw_state);
            printf("  capabilities=0x%08X\n", hwState.capabilities);
        }
    }
    
    CloseHandle(hDriver);
    return (bytesRet2 == sizeof(cfg)) ? 0 : 1;
}
