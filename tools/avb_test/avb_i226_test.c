/*++

Module Name:

    avb_i226_test.c

Abstract:

    Intel I226-specific AVB/TSN test tool using proper I226 SSOT registers
    Tests advanced TSN features available on I226 controllers

--*/

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Use SSOT header for IOCTL definitions
#include "../../external/intel_avb/include/avb_ioctl.h"

// Use SSOT header for I226 register definitions  
#include "../../intel-ethernet-regs/gen/i226_regs.h"

#define LINKNAME "\\\\.\\IntelAvbFilter"

static HANDLE OpenDevice(void) {
    HANDLE h = CreateFileA(LINKNAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("? Failed to open %s (Error: %lu)\n", LINKNAME, GetLastError());
        printf("   Make sure Intel AVB Filter driver is installed and bound to Intel I226\n");
    }
    return h;
}

static BOOL ReadRegister(HANDLE h, uint32_t offset, uint32_t* value) {
    AVB_REGISTER_REQUEST r;
    ZeroMemory(&r, sizeof(r));
    r.offset = offset;
    DWORD br = 0;
    
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (ok) {
        *value = r.value;
        return TRUE;
    }
    return FALSE;
}

static BOOL WriteRegister(HANDLE h, uint32_t offset, uint32_t value) {
    AVB_REGISTER_REQUEST r;
    ZeroMemory(&r, sizeof(r));
    r.offset = offset;
    r.value = value;
    DWORD br = 0;
    
    return DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL);
}

static void InitializeDevice(HANDLE h) {
    DWORD br = 0;
    DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &br, NULL);
}

static void TestI226DeviceInfo(HANDLE h) {
    printf("\n?? === I226 DEVICE INFORMATION ===\n");
    
    // Get device capabilities
    AVB_ENUM_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    DWORD br = 0;
    
    if (DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS, &req, sizeof(req), &req, sizeof(req), &br, NULL)) {
        printf("?? Device: VID=0x%04X DID=0x%04X\n", req.vendor_id, req.device_id);
        printf("?? Capabilities: 0x%08X\n", req.capabilities);
        
        if (req.device_id == 0x125B || req.device_id == 0x125C) {
            printf("? Confirmed Intel I226 Controller\n");
        } else {
            printf("?? Not an I226 controller (DID=0x%04X)\n", req.device_id);
            return;
        }
        
        // Decode I226 capabilities
        printf("?? I226 Feature Support:\n");
        if (req.capabilities & INTEL_CAP_BASIC_1588)   printf("   ? IEEE 1588 Basic Support\n");
        if (req.capabilities & INTEL_CAP_ENHANCED_TS)  printf("   ? Enhanced Timestamping\n");
        if (req.capabilities & INTEL_CAP_TSN_TAS)      printf("   ? Time-Aware Shaper (TAS)\n");
        if (req.capabilities & INTEL_CAP_TSN_FP)       printf("   ? Frame Preemption (FP)\n");
        if (req.capabilities & INTEL_CAP_PCIe_PTM)     printf("   ? PCIe Precision Time Measurement\n");
        if (req.capabilities & INTEL_CAP_2_5G)         printf("   ? 2.5 Gigabit Support\n");
        if (req.capabilities & INTEL_CAP_MMIO)         printf("   ? Memory-Mapped I/O Access\n");
    } else {
        printf("? Failed to get device capabilities (Error: %lu)\n", GetLastError());
    }
}

static void TestI226PTPVerification(HANDLE h) {
    printf("\n? === I226 PTP REAL HARDWARE VERIFICATION ===\n");
    
    uint32_t systiml1, systiml2, systiml3;
    
    // Use I226 SSOT register offsets
    printf("?? Reading I226_SYSTIML (0x%05X) multiple times:\n", I226_SYSTIML);
    
    ReadRegister(h, I226_SYSTIML, &systiml1);
    Sleep(10); // 10ms delay
    ReadRegister(h, I226_SYSTIML, &systiml2);
    Sleep(10); // 10ms delay  
    ReadRegister(h, I226_SYSTIML, &systiml3);
    
    printf("   Read 1: 0x%08X\n", systiml1);
    printf("   Read 2: 0x%08X (+%ld)\n", systiml2, (long)(systiml2 - systiml1));
    printf("   Read 3: 0x%08X (+%ld)\n", systiml3, (long)(systiml3 - systiml2));
    
    // Real I226 hardware should show incremental changes
    if (systiml2 > systiml1 && systiml3 > systiml2) {
        printf("? REAL I226 HARDWARE CONFIRMED: PTP clock advancing normally\n");
        uint64_t rate_ns_per_10ms = (uint64_t)(systiml2 - systiml1);
        printf("   Clock Rate: %llu ns per 10ms\n", rate_ns_per_10ms);
        if (rate_ns_per_10ms > 5000000 && rate_ns_per_10ms < 15000000) { // 5-15M ns per 10ms = reasonable
            printf("? I226 PTP Clock Rate: NORMAL\n");
        } else {
            printf("?? I226 PTP Clock Rate: UNUSUAL (%llu ns/10ms)\n", rate_ns_per_10ms);
        }
    } else if (systiml1 == 0 && systiml2 == 0 && systiml3 == 0) {
        printf("?? I226 PTP CLOCK NOT RUNNING: All SYSTIML reads return 0\n");
        printf("   This indicates real hardware but PTP initialization needed\n");
    } else if (systiml1 == systiml2 && systiml2 == systiml3) {
        printf("?? STATIC VALUES: Same value (0x%08X) across all reads\n", systiml1);
        printf("   This could indicate simulation or clock stopped\n");
    } else {
        printf("? I226 HARDWARE ACTIVITY DETECTED: Values changing\n");
    }
}

static void TestI226TSNRegisters(HANDLE h) {
    printf("\n?? === I226 TSN REGISTER ACCESS ===\n");
    
    uint32_t value;
    
    // Test I226-specific TSN registers using SSOT offsets
    printf("?? Time-Aware Shaper (TAS) Registers:\n");
    if (ReadRegister(h, I226_TAS_CTRL, &value)) {
        printf("   I226_TAS_CTRL (0x%05X): 0x%08X\n", I226_TAS_CTRL, value);
        
        // Decode using I226 SSOT macros
        uint32_t tas_en = (uint32_t)I226_TAS_CTRL_GET(value, I226_TAS_CTRL_EN_MASK, I226_TAS_CTRL_EN_SHIFT);
        printf("     TAS Enabled: %s\n", tas_en ? "YES" : "NO");
    }
    
    if (ReadRegister(h, I226_TAS_CONFIG0, &value)) {
        printf("   I226_TAS_CONFIG0 (0x%05X): 0x%08X\n", I226_TAS_CONFIG0, value);
    }
    
    printf("?? Frame Preemption (FP) Registers:\n");
    if (ReadRegister(h, I226_FP_CONFIG, &value)) {
        printf("   I226_FP_CONFIG (0x%05X): 0x%08X\n", I226_FP_CONFIG, value);
        
        // Decode using I226 SSOT macros
        uint32_t fp_en = (uint32_t)I226_FP_CONFIG_GET(value, I226_FP_CONFIG_EN_MASK, I226_FP_CONFIG_EN_SHIFT);
        uint32_t preemptable = (uint32_t)I226_FP_CONFIG_GET(value, I226_FP_CONFIG_PREEMPTABLE_QUEUES_MASK, I226_FP_CONFIG_PREEMPTABLE_QUEUES_SHIFT);
        printf("     FP Enabled: %s, Preemptable Queues: 0x%02X\n", fp_en ? "YES" : "NO", preemptable);
    }
    
    if (ReadRegister(h, I226_FP_STATUS, &value)) {
        printf("   I226_FP_STATUS (0x%05X): 0x%08X\n", I226_FP_STATUS, value);
    }
}

static void TestI226AdvancedFeatures(HANDLE h) {
    printf("\n?? === I226 ADVANCED FEATURE TESTING ===\n");
    
    // Test actual TSN configuration via IOCTL
    printf("?? Testing Time-Aware Shaper Configuration:\n");
    AVB_TAS_REQUEST tas;
    ZeroMemory(&tas, sizeof(tas));
    
    // Audio streaming schedule: 125µs cycle
    tas.config.cycle_time_ns = 125000; // 125µs cycle for audio
    tas.config.gate_states[0] = 0x01;  // Queue 0 open for audio
    tas.config.gate_durations[0] = 62500; // 50% duty cycle
    tas.config.gate_states[1] = 0x00;  // All queues closed
    tas.config.gate_durations[1] = 62500; // 50% duty cycle
    
    DWORD br = 0;
    if (DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, &tas, sizeof(tas), &tas, sizeof(tas), &br, NULL)) {
        printf("? TAS Configuration: SUCCESS (Status: 0x%08X)\n", tas.status);
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_FUNCTION) {
            printf("?? TAS Configuration: NOT IMPLEMENTED in driver\n");
        } else {
            printf("? TAS Configuration: FAILED (Error: %lu)\n", error);
        }
    }
    
    // Test Frame Preemption
    printf("?? Testing Frame Preemption Configuration:\n");
    AVB_FP_REQUEST fp;
    ZeroMemory(&fp, sizeof(fp));
    
    fp.config.preemptable_queues = 0x01; // Queue 0 preemptable
    fp.config.min_fragment_size = 64;
    
    if (DeviceIoControl(h, IOCTL_AVB_SETUP_FP, &fp, sizeof(fp), &fp, sizeof(fp), &br, NULL)) {
        printf("? FP Configuration: SUCCESS (Status: 0x%08X)\n", fp.status);
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_FUNCTION) {
            printf("?? FP Configuration: NOT IMPLEMENTED in driver\n");
        } else {
            printf("? FP Configuration: FAILED (Error: %lu)\n", error);
        }
    }
}

static void usage(const char* exe) {
    printf("Intel I226 AVB/TSN Test Tool\n");
    printf("============================\n\n");
    printf("Usage: %s [command]\n\n", exe);
    printf("Commands:\n");
    printf("  info          - Show I226 device information\n");
    printf("  ptp           - Test I226 PTP timing verification\n");
    printf("  tsn           - Test I226 TSN register access\n");
    printf("  advanced      - Test I226 advanced TSN features\n");
    printf("  all           - Run all tests (default)\n");
    printf("\nDirect Register Access:\n");
    printf("  reg-read <offset>       - Read specific I226 register\n");
    printf("  reg-write <offset> <val> - Write specific I226 register\n");
    printf("\nExample I226 SSOT Registers:\n");
    printf("  reg-read 0x%05X         - I226_SYSTIML (PTP time low)\n", I226_SYSTIML);
    printf("  reg-read 0x%05X         - I226_TAS_CTRL (Time-Aware Shaper)\n", I226_TAS_CTRL);
    printf("  reg-read 0x%05X         - I226_FP_CONFIG (Frame Preemption)\n", I226_FP_CONFIG);
}

int main(int argc, char** argv) {
    printf("Intel I226 Advanced TSN Test Tool (Using I226 SSOT)\n");
    printf("===================================================\n");
    
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    InitializeDevice(h);
    
    if (argc < 2 || _stricmp(argv[1], "all") == 0) {
        // Run comprehensive I226 test suite
        TestI226DeviceInfo(h);
        TestI226PTPVerification(h);
        TestI226TSNRegisters(h);
        TestI226AdvancedFeatures(h);
    }
    else if (_stricmp(argv[1], "info") == 0) {
        TestI226DeviceInfo(h);
    }
    else if (_stricmp(argv[1], "ptp") == 0) {
        TestI226PTPVerification(h);
    }
    else if (_stricmp(argv[1], "tsn") == 0) {
        TestI226TSNRegisters(h);
    }
    else if (_stricmp(argv[1], "advanced") == 0) {
        TestI226AdvancedFeatures(h);
    }
    else if (_stricmp(argv[1], "reg-read") == 0 && argc >= 3) {
        uint32_t offset = (uint32_t)strtoul(argv[2], NULL, 16);
        uint32_t value;
        if (ReadRegister(h, offset, &value)) {
            printf("I226[0x%05X] = 0x%08X\n", offset, value);
        } else {
            printf("? Failed to read I226 register 0x%05X (Error: %lu)\n", offset, GetLastError());
        }
    }
    else if (_stricmp(argv[1], "reg-write") == 0 && argc >= 4) {
        uint32_t offset = (uint32_t)strtoul(argv[2], NULL, 16);
        uint32_t value = (uint32_t)strtoul(argv[3], NULL, 16);
        if (WriteRegister(h, offset, value)) {
            printf("? I226[0x%05X] ? 0x%08X\n", offset, value);
        } else {
            printf("? Failed to write I226 register 0x%05X (Error: %lu)\n", offset, GetLastError());
        }
    }
    else {
        usage(argv[0]);
        CloseHandle(h);
        return 2;
    }
    
    CloseHandle(h);
    return 0;
}