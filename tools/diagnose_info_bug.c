/**
 * Check actual hw_state value after INIT_DEVICE
 */
#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main() {
    printf("=== Hardware State Check ===\n\n");
    
    HANDLE hDriver = CreateFileA("\\\\.\\IntelAvbFilter",
                                  GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("FAILED: CreateFile error %lu\n", GetLastError());
        return 1;
    }
    
    printf("Driver opened.\n\n");
    
    // Call INIT_DEVICE
    printf("Step 1: INIT_DEVICE\n");
    DWORD dummy = 0, bytesRet = 0;
    DeviceIoControl(hDriver, IOCTL_AVB_INIT_DEVICE,
                    &dummy, sizeof(dummy),
                    &dummy, sizeof(dummy),
                    &bytesRet, NULL);
    printf("  Result: bytes=%lu\n\n", bytesRet);
    
    // Try to read a register to infer hw_state
    printf("Step 2: Read SYSTIML register\n");
    AVB_REGISTER_REQUEST reg = {0};
    reg.offset = 0x0B600;
    bytesRet = 0;
    
    BOOL success = DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                                    &reg, sizeof(reg),
                                    &reg, sizeof(reg),
                                    &bytesRet, NULL);
    
    printf("  Result: %s\n", success ? "SUCCESS" : "FAILED");
    printf("  Value: 0x%08X\n", reg.value);
    printf("  Status: 0x%08X\n\n", reg.status);
    
    if (success && reg.value != 0) {
        printf("  -> Hardware access is working (hw_state >= BAR_MAPPED)\n\n");
    }
    
    // Now try GET_CLOCK_CONFIG
    printf("Step 3: GET_CLOCK_CONFIG\n");
    AVB_CLOCK_CONFIG cfg = {0};
    bytesRet = 0;
    
    success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                              &cfg, sizeof(cfg),
                              &cfg, sizeof(cfg),
                              &bytesRet, NULL);
    
    DWORD err = GetLastError();
    printf("  DeviceIoControl: %s\n", success ? "SUCCESS" : "FAILED");
    printf("  GetLastError: %lu (0x%08X)\n", err, err);
    printf("  BytesReturned: %lu\n\n", bytesRet);
    
    if (bytesRet == 0) {
        printf("*** DIAGNOSIS ***\n");
        printf("READ_REGISTER works, but GET_CLOCK_CONFIG returns 0 bytes.\n");
        printf("This means the IOCTL handler is hitting an error path that\n");
        printf("doesn't set Irp->IoStatus.Information (info variable).\n\n");
        printf("Most likely causes:\n");
        printf("  1. hw_state check failing (line 718: hw_state < AVB_HW_BAR_MAPPED)\n");
        printf("  2. Buffer size check failing (line 709)\n");
        printf("  3. The 'info' variable is only set on SUCCESS path (line 788)\n\n");
        printf("RECOMMENDATION:\n");
        printf("The driver needs to be fixed to set 'info' on ALL code paths,\n");
        printf("not just the success path. Windows expects Information to be\n");
        printf("set even for error returns.\n");
    }
    
    CloseHandle(hDriver);
    return (bytesRet == sizeof(cfg)) ? 0 : 1;
}
