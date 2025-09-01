/*
 * TSN IOCTL Handler Verification Test (User Mode)
 * Tests that the missing TAS/FP/PTM IOCTL handlers have been properly added
 * and are no longer returning ERROR_INVALID_FUNCTION (Error 1)
 */

#include <windows.h>
#include <stdio.h>
#include "../../include/avb_ioctl.h"

static HANDLE OpenDevice() {
    HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                          GENERIC_READ | GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        printf("? Failed to open device: %lu\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }
    
    printf("? Device opened successfully\n");
    return h;
}

static void TestTSNIOCTLHandlerExists(HANDLE h, DWORD ioctl, const char* name) {
    DWORD bytesReturned = 0;
    char buffer[1024] = {0};
    
    printf("Testing %s...\n", name);
    
    BOOL result = DeviceIoControl(h, ioctl, buffer, sizeof(buffer), 
                                 buffer, sizeof(buffer), &bytesReturned, NULL);
    
    DWORD error = GetLastError();
    
    if (!result && error == ERROR_INVALID_FUNCTION) {
        printf("  ? %s: IOCTL handler MISSING (Error: 1) - FIX FAILED\n", name);
    } else if (!result) {
        printf("  ? %s: Handler exists, returned error %lu (FIX WORKED)\n", name, error);
        printf("      (Error is expected - means our handler is being called)\n");
    } else {
        printf("  ? %s: Handler exists and succeeded (FIX WORKED)\n", name);
    }
}

static void TestTASHandlerImplementation(HANDLE h) {
    printf("\n?? Testing TAS IOCTL Handler Implementation:\n");
    
    AVB_TAS_REQUEST tas;
    ZeroMemory(&tas, sizeof(tas));
    
    // Basic TAS configuration for handler test
    tas.config.base_time_s = 0;
    tas.config.base_time_ns = 1000000;  // 1ms
    tas.config.cycle_time_s = 0;
    tas.config.cycle_time_ns = 1000000; // 1ms cycle
    tas.config.gate_states[0] = 0xFF;   // All queues open
    tas.config.gate_durations[0] = 500000; // 500µs
    tas.config.gate_states[1] = 0x01;   // Only queue 0
    tas.config.gate_durations[1] = 500000; // 500µs
    
    DWORD bytesReturned = 0;
    if (DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, &tas, sizeof(tas),
                       &tas, sizeof(tas), &bytesReturned, NULL)) {
        printf("? TAS Handler: SUCCESS (Status: 0x%08X) - IOCTL HANDLER WORKING\n", tas.status);
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_FUNCTION) {
            printf("? TAS Handler: MISSING - Our fix did not work\n");
        } else {
            printf("? TAS Handler: Called successfully (Error: %lu, Status: 0x%08X)\n", 
                   error, tas.status);
            printf("    Handler implementation is working - actual functionality depends on hardware\n");
        }
    }
}

static void TestFramePreemptionHandlerImplementation(HANDLE h) {
    printf("\n?? Testing Frame Preemption IOCTL Handler Implementation:\n");
    
    AVB_FP_REQUEST fp;
    ZeroMemory(&fp, sizeof(fp));
    
    // Basic FP configuration for handler test
    fp.config.preemptable_queues = 0xFE; // Queues 1-7 preemptable
    fp.config.min_fragment_size = 64;
    fp.config.verify_disable = 0;
    
    DWORD bytesReturned = 0;
    if (DeviceIoControl(h, IOCTL_AVB_SETUP_FP, &fp, sizeof(fp),
                       &fp, sizeof(fp), &bytesReturned, NULL)) {
        printf("? FP Handler: SUCCESS (Status: 0x%08X) - IOCTL HANDLER WORKING\n", fp.status);
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_FUNCTION) {
            printf("? FP Handler: MISSING - Our fix did not work\n");
        } else {
            printf("? FP Handler: Called successfully (Error: %lu, Status: 0x%08X)\n", 
                   error, fp.status);
            printf("    Handler implementation is working - actual functionality depends on hardware\n");
        }
    }
}

static void TestPTMHandlerImplementation(HANDLE h) {
    printf("\n?? Testing PTM IOCTL Handler Implementation:\n");
    
    AVB_PTM_REQUEST ptm;
    ZeroMemory(&ptm, sizeof(ptm));
    
    // Basic PTM configuration for handler test
    ptm.config.enabled = 1;
    ptm.config.clock_granularity = 16; // 16ns
    
    DWORD bytesReturned = 0;
    if (DeviceIoControl(h, IOCTL_AVB_SETUP_PTM, &ptm, sizeof(ptm),
                       &ptm, sizeof(ptm), &bytesReturned, NULL)) {
        printf("? PTM Handler: SUCCESS (Status: 0x%08X) - IOCTL HANDLER WORKING\n", ptm.status);
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_FUNCTION) {
            printf("? PTM Handler: MISSING - Our fix did not work\n");
        } else {
            printf("? PTM Handler: Called successfully (Error: %lu, Status: 0x%08X)\n", 
                   error, ptm.status);
            printf("    Handler implementation is working - actual functionality depends on hardware\n");
        }
    }
}

int main() {
    printf("Intel AVB Filter Driver - TSN IOCTL Handler Verification\n");
    printf("=======================================================\n");
    printf("Purpose: Verify TAS/FP/PTM IOCTL handlers are no longer missing\n");
    printf("Success: IOCTLs don't return ERROR_INVALID_FUNCTION (Error 1)\n\n");
    
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    // Initialize device
    DWORD bytesReturned = 0;
    DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytesReturned, NULL);
    
    printf("\n?? Phase 1: TSN IOCTL Handler Existence Verification\n");
    printf("==================================================\n");
    TestTSNIOCTLHandlerExists(h, IOCTL_AVB_SETUP_TAS, "IOCTL_AVB_SETUP_TAS");
    TestTSNIOCTLHandlerExists(h, IOCTL_AVB_SETUP_FP, "IOCTL_AVB_SETUP_FP");
    TestTSNIOCTLHandlerExists(h, IOCTL_AVB_SETUP_PTM, "IOCTL_AVB_SETUP_PTM");
    
    printf("\n?? Phase 2: TSN IOCTL Handler Implementation Test\n");
    printf("===============================================\n");
    TestTASHandlerImplementation(h);
    TestFramePreemptionHandlerImplementation(h);
    TestPTMHandlerImplementation(h);
    
    printf("\n?? TEST RESULTS INTERPRETATION:\n");
    printf("===============================\n");
    printf("? ERROR_INVALID_FUNCTION (Error 1) = Handler missing, fix failed\n");
    printf("? Other errors/success = Handler exists, fix worked!\n");
    printf("   ?? Actual TSN functionality depends on hardware support\n");
    printf("   ?? This test only verifies IOCTL routing works\n");
    
    CloseHandle(h);
    return 0;
}