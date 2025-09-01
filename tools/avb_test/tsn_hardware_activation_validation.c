/*
 * TSN Hardware Activation Validation Test
 * 
 * Purpose: Validates that TSN features (TAS, FP, PTM) actually activate at the 
 * hardware level instead of just succeeding at the IOCTL level.
 * 
 * This test addresses the hardware activation failures identified during
 * comprehensive testing where:
 * - IOCTLs returned success (Status: 0x00000000)  
 * - But hardware registers showed no activation (enable bits remained clear)
 * 
 * Success criteria:
 * - TAS: I226_TAS_CTRL enable bit SET after configuration
 * - Frame Preemption: I226_FP_CONFIG enable bit SET after configuration  
 * - I210 PTP: SYSTIM advances instead of being stuck at zero
 */

#include <windows.h>
#include <stdio.h>
#include "../../include/avb_ioctl.h"

#define I226_TAS_CTRL           0x08600
#define I226_TAS_CTRL_EN        0x00000001
#define I226_FP_CONFIG          0x08700  
#define I226_FP_CONFIG_ENABLE   0x00000001
#define I210_SYSTIML            0x0B600
#define I210_SYSTIMH            0x0B604

static HANDLE OpenDevice(void) {
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

static BOOL ReadRegister(HANDLE h, DWORD offset, DWORD* value) {
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, 
                                 &req, sizeof(req),
                                 &req, sizeof(req), 
                                 &bytesReturned, NULL);
    
    if (result && req.status == 0) {
        *value = req.value;
        return TRUE;
    }
    
    return FALSE;
}

static void TestPhase2TASActivation(HANDLE h) {
    printf("\n?? Phase 2: TAS Hardware Activation Test\n");
    printf("=======================================\n");
    printf("Purpose: Verify TAS actually activates in hardware (not just IOCTL success)\n\n");
    
    // Step 1: Check TAS activation before configuration
    DWORD tas_ctrl_before = 0;
    if (ReadRegister(h, I226_TAS_CTRL, &tas_ctrl_before)) {
        printf("?? TAS_CTRL before configuration: 0x%08X\n", tas_ctrl_before);
        printf("   Enable bit: %s\n", (tas_ctrl_before & I226_TAS_CTRL_EN) ? "SET" : "CLEAR");
    }
    
    // Step 2: Configure TAS using Phase 2 enhanced implementation  
    printf("\n?? Phase 2: Configuring TAS with enhanced implementation...\n");
    
    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));
    
    // Configure a basic TAS schedule for testing
    tasReq.config.base_time_s = 0;
    tasReq.config.base_time_ns = 1000000;   // 1ms in future
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 1000000;  // 1ms cycle
    tasReq.config.gate_states[0] = 0xFF;    // All queues open
    tasReq.config.gate_durations[0] = 500000; // 500µs
    tasReq.config.gate_states[1] = 0x01;    // Only queue 0  
    tasReq.config.gate_durations[1] = 500000; // 500µs
    
    DWORD bytesReturned = 0;
    BOOL ioctl_success = DeviceIoControl(h, IOCTL_AVB_SETUP_TAS,
                                        &tasReq, sizeof(tasReq),
                                        &tasReq, sizeof(tasReq), 
                                        &bytesReturned, NULL);
    
    if (ioctl_success) {
        printf("? Phase 2: TAS IOCTL succeeded (Status: 0x%08X)\n", tasReq.status);
        
        // Step 3: CRITICAL - Verify hardware activation
        printf("\n?? Phase 2: Hardware Activation Verification\n");
        
        Sleep(100); // Allow time for hardware to process
        
        DWORD tas_ctrl_after = 0;
        if (ReadRegister(h, I226_TAS_CTRL, &tas_ctrl_after)) {
            printf("?? TAS_CTRL after configuration: 0x%08X\n", tas_ctrl_after);
            printf("   Enable bit: %s\n", (tas_ctrl_after & I226_TAS_CTRL_EN) ? "SET" : "CLEAR");
            
            if (tas_ctrl_after & I226_TAS_CTRL_EN) {
                printf("?? SUCCESS: Phase 2 TAS HARDWARE ACTIVATION CONFIRMED!\n");
                printf("   ? Enable bit is SET - TAS is controlling traffic\n");
                printf("   ? This proves the Phase 2 hardware activation fix works\n");
            } else {
                printf("? FAILURE: Phase 2 TAS hardware activation failed\n");
                printf("   ?? Enable bit is still CLEAR after configuration\n"); 
                printf("   ?? This indicates prerequisite or activation sequence issues\n");
                printf("   ?? Check: PTP clock running, base time in future, valid gate list\n");
            }
        } else {
            printf("? Cannot read TAS_CTRL register for verification\n");
        }
        
    } else {
        DWORD error = GetLastError();
        printf("? Phase 2: TAS IOCTL failed (Error: %lu, Status: 0x%08X)\n", 
               error, tasReq.status);
        
        if (error == ERROR_INVALID_FUNCTION) {
            printf("   ?? CRITICAL: This indicates Phase 1 IOCTL handler fix failed\n");
        } else {
            printf("   ?? IOCTL handler working, but configuration failed\n");
        }
    }
}

static void TestPhase2FramePreemptionActivation(HANDLE h) {
    printf("\n?? Phase 2: Frame Preemption Hardware Activation Test\n");
    printf("===================================================\n");
    
    // Check FP activation before configuration
    DWORD fp_config_before = 0;
    if (ReadRegister(h, I226_FP_CONFIG, &fp_config_before)) {
        printf("?? FP_CONFIG before: 0x%08X\n", fp_config_before);
        printf("   Enable bit: %s\n", (fp_config_before & I226_FP_CONFIG_ENABLE) ? "SET" : "CLEAR");
    }
    
    // Configure Frame Preemption
    printf("\n?? Phase 2: Configuring Frame Preemption...\n");
    
    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0xFE; // Queues 1-7 preemptable
    fpReq.config.min_fragment_size = 64;    // 64 byte minimum
    fpReq.config.verify_disable = 0;        // Enable verification
    
    DWORD bytesReturned = 0;
    BOOL ioctl_success = DeviceIoControl(h, IOCTL_AVB_SETUP_FP,
                                        &fpReq, sizeof(fpReq),
                                        &fpReq, sizeof(fpReq),
                                        &bytesReturned, NULL);
    
    if (ioctl_success) {
        printf("? Phase 2: FP IOCTL succeeded (Status: 0x%08X)\n", fpReq.status);
        
        // Verify hardware activation
        printf("\n?? Phase 2: Frame Preemption Hardware Verification\n");
        
        Sleep(100);
        
        DWORD fp_config_after = 0;
        if (ReadRegister(h, I226_FP_CONFIG, &fp_config_after)) {
            printf("?? FP_CONFIG after configuration: 0x%08X\n", fp_config_after);
            printf("   Enable bit: %s\n", (fp_config_after & I226_FP_CONFIG_ENABLE) ? "SET" : "CLEAR");
            
            if (fp_config_after & I226_FP_CONFIG_ENABLE) {
                printf("?? SUCCESS: Phase 2 Frame Preemption HARDWARE ACTIVATION CONFIRMED!\n");
                printf("   ? Enable bit is SET - Frame Preemption is active\n");
            } else {
                printf("? FAILURE: Frame Preemption hardware activation failed\n");
                printf("   ?? This may require compatible link partner\n");
            }
        }
        
    } else {
        DWORD error = GetLastError();
        printf("? Phase 2: FP IOCTL failed (Error: %lu)\n", error);
    }
}

static void TestPhase2I210PTPClockFix(HANDLE h) {
    printf("\n?? Phase 2: I210 PTP Clock Fix Test\n");
    printf("===================================\n");
    printf("Purpose: Verify I210 PTP clock advances instead of being stuck at zero\n\n");
    
    // First select I210 adapter
    printf("?? Step 1: Selecting I210 adapter context...\n");
    AVB_OPEN_REQUEST openReq;
    ZeroMemory(&openReq, sizeof(openReq));
    openReq.vendor_id = 0x8086;
    openReq.device_id = 0x1533; // I210
    
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, &openReq, sizeof(openReq),
                        &openReq, sizeof(openReq), &bytesReturned, NULL) || 
        openReq.status != 0) {
        printf("? I210 not available for testing\n");
        return;
    }
    
    printf("? I210 adapter selected\n");
    
    // Check initial SYSTIM state
    printf("\n?? Step 2: Checking initial I210 SYSTIM state...\n");
    
    DWORD systiml1 = 0, systimh1 = 0;
    if (ReadRegister(h, I210_SYSTIML, &systiml1) && 
        ReadRegister(h, I210_SYSTIMH, &systimh1)) {
        printf("?? Initial SYSTIM: 0x%08X%08X\n", systimh1, systiml1);
        
        if (systiml1 == 0 && systimh1 == 0) {
            printf("?? I210 PTP clock appears stuck at zero - applying Phase 2 fix\n");
        }
    }
    
    // Trigger device initialization (which includes Phase 2 PTP fix for I210)
    printf("\n?? Step 3: Triggering Phase 2 I210 PTP initialization...\n");
    
    if (DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        printf("? I210 initialization completed\n");
    } else {
        printf("? I210 initialization failed\n");
        return;
    }
    
    // Verify clock is now advancing
    printf("\n?? Step 4: Verifying I210 PTP clock advancement...\n");
    
    Sleep(100); // Allow time for clock to advance
    
    DWORD systiml2 = 0, systimh2 = 0;
    if (ReadRegister(h, I210_SYSTIML, &systiml2) && 
        ReadRegister(h, I210_SYSTIMH, &systimh2)) {
        printf("?? SYSTIM after fix: 0x%08X%08X\n", systimh2, systiml2);
        
        if (systiml2 > systiml1 || systimh2 > systimh1) {
            printf("?? SUCCESS: Phase 2 I210 PTP Clock Fix CONFIRMED!\n");
            printf("   ? Clock is now advancing properly\n");
            printf("   ? SYSTIM: 0x%08X%08X -> 0x%08X%08X\n", 
                   systimh1, systiml1, systimh2, systiml2);
        } else {
            printf("? FAILURE: I210 PTP clock still stuck\n");
            printf("   ?? Clock not advancing: 0x%08X%08X -> 0x%08X%08X\n", 
                   systimh1, systiml1, systimh2, systiml2);
        }
    }
}

int main() {
    printf("Intel AVB Filter Driver - Phase 2: Hardware Activation Validation\n");
    printf("=================================================================\n");
    printf("Purpose: Verify Phase 2 enhanced implementations actually activate hardware\n");
    printf("Success: TSN features work at hardware level, not just IOCTL level\n\n");
    
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    // Initialize device
    DWORD bytesReturned = 0;
    DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytesReturned, NULL);
    
    printf("\n?? Phase 2: Hardware Activation Test Suite\n");
    printf("==========================================\n");
    
    // Test Phase 2 TAS activation
    TestPhase2TASActivation(h);
    
    // Test Phase 2 Frame Preemption activation  
    TestPhase2FramePreemptionActivation(h);
    
    // Test Phase 2 I210 PTP clock fix
    TestPhase2I210PTPClockFix(h);
    
    printf("\n?? PHASE 2 VALIDATION SUMMARY\n");
    printf("=============================\n");
    printf("? SUCCESS indicators:\n");
    printf("   - TAS_CTRL enable bit SET after configuration\n");
    printf("   - FP_CONFIG enable bit SET after configuration\n"); 
    printf("   - I210 SYSTIM advancing instead of stuck at zero\n");
    printf("\n? FAILURE indicators:\n");
    printf("   - Enable bits remain CLEAR (hardware not activated)\n");
    printf("   - I210 clock still stuck at zero\n");
    printf("\n?? This test validates Phase 2 hardware activation fixes\n");
    printf("    Phase 1 validated IOCTL handlers (no more Error 1)\n");
    printf("    Phase 2 validates actual hardware functionality\n");
    
    CloseHandle(h);
    return 0;
}