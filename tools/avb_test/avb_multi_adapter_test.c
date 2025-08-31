/*++

Module Name:

    avb_multi_adapter_test.c

Abstract:

    Intel AVB Filter Driver - Multi-Adapter Diagnostic Test
    Tests all discovered Intel adapters and their capabilities individually

--*/

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Include the shared IOCTL ABI
#include "../../include/avb_ioctl.h"

#define LINKNAME "\\\\.\\IntelAvbFilter"

static HANDLE OpenDevice(void) {
    HANDLE h = CreateFileA(LINKNAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("? Failed to open %s (Error: %lu)\n", LINKNAME, GetLastError());
        printf("   Make sure Intel AVB Filter driver is installed and bound to Intel adapters\n");
    } else {
        printf("? Device opened successfully: %s\n", LINKNAME);
    }
    return h;
}

static void TestMultiAdapterEnumeration(HANDLE h) {
    printf("\n?? === MULTI-ADAPTER ENUMERATION TEST ===\n");
    
    ULONG totalAdapters = 0;
    ULONG adapterIndex = 0;
    
    // First, count total adapters by querying index 0
    AVB_ENUM_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.index = 0;
    DWORD br = 0;
    
    BOOL result = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS, &req, sizeof(req), &req, sizeof(req), &br, NULL);
    if (result) {
        totalAdapters = req.count;
        printf("?? Total Intel AVB adapters found: %lu\n", totalAdapters);
        
        if (totalAdapters == 0) {
            printf("??  No Intel AVB adapters found\n");
            printf("   Expected adapters based on your Get-NetAdapter output:\n");
            printf("   - Intel I210-T1 (should be DID=0x1533)\n");
            printf("   - Intel I226-V (should be DID=0x125B)\n");
            return;
        }
    } else {
        printf("? ENUM_ADAPTERS failed: %lu\n", GetLastError());
        return;
    }
    
    // Now enumerate each adapter individually
    for (adapterIndex = 0; adapterIndex < totalAdapters; adapterIndex++) {
        printf("\n?? --- ADAPTER #%lu ---\n", adapterIndex);
        
        ZeroMemory(&req, sizeof(req));
        req.index = adapterIndex;
        
        result = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS, &req, sizeof(req), &req, sizeof(req), &br, NULL);
        if (result) {
            printf("   Vendor ID: 0x%04X\n", req.vendor_id);
            printf("   Device ID: 0x%04X", req.device_id);
            
            // Identify device type
            switch (req.device_id) {
                case 0x1533: printf(" (Intel I210)\n"); break;
                case 0x125B: printf(" (Intel I226)\n"); break;
                case 0x15F2: printf(" (Intel I225)\n"); break;
                case 0x153A:
                case 0x153B: printf(" (Intel I217)\n"); break;
                case 0x15B7:
                case 0x15B8:
                case 0x15D6:
                case 0x15D7:
                case 0x15D8: printf(" (Intel I219)\n"); break;
                default: printf(" (Unknown Intel device)\n"); break;
            }
            
            printf("   Capabilities: 0x%08X\n", req.capabilities);
            printf("   Capability Details:\n");
            
            if (req.capabilities & 0x00000001) printf("     - BASIC_1588 (IEEE 1588 support)\n");
            if (req.capabilities & 0x00000002) printf("     - ENHANCED_TS (Enhanced timestamping)\n");
            if (req.capabilities & 0x00000004) printf("     - TSN_TAS (Time-Aware Shaper)\n");
            if (req.capabilities & 0x00000008) printf("     - TSN_FP (Frame Preemption)\n");
            if (req.capabilities & 0x00000010) printf("     - PCIe_PTM (Precision Time Measurement)\n");
            if (req.capabilities & 0x00000020) printf("     - 2_5G (2.5 Gigabit support)\n");
            if (req.capabilities & 0x00000040) printf("     - EEE (Energy Efficient Ethernet)\n");
            if (req.capabilities & 0x00000080) printf("     - MMIO (Memory-mapped I/O)\n");
            if (req.capabilities & 0x00000100) printf("     - MDIO (Management Data I/O)\n");
            
            if (req.capabilities == 0) {
                printf("     ??  No capabilities reported (initialization may have failed)\n");
            }
        } else {
            printf("   ? Failed to query adapter #%lu: %lu\n", adapterIndex, GetLastError());
        }
    }
}

static void TestAdapterSelection(HANDLE h) {
    printf("\n?? === ADAPTER SELECTION TEST ===\n");
    
    // Test opening specific adapters by device ID
    USHORT testDeviceIds[] = {0x1533, 0x125B, 0x15F2}; // I210, I226, I225
    char* deviceNames[] = {"I210", "I226", "I225"};
    
    for (int i = 0; i < sizeof(testDeviceIds)/sizeof(testDeviceIds[0]); i++) {
        printf("\n?? Testing adapter selection for %s (DID=0x%04X):\n", deviceNames[i], testDeviceIds[i]);
        
        AVB_OPEN_REQUEST openReq;
        ZeroMemory(&openReq, sizeof(openReq));
        openReq.vendor_id = 0x8086;
        openReq.device_id = testDeviceIds[i];
        
        DWORD br = 0;
        BOOL result = DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, &openReq, sizeof(openReq), 
                                     &openReq, sizeof(openReq), &br, NULL);
        
        if (result) {
            if (openReq.status == 0) { // NDIS_STATUS_SUCCESS
                printf("   ? Successfully opened %s adapter\n", deviceNames[i]);
                
                // Test device info after opening specific adapter
                AVB_DEVICE_INFO_REQUEST infoReq;
                ZeroMemory(&infoReq, sizeof(infoReq));
                
                if (DeviceIoControl(h, IOCTL_AVB_GET_DEVICE_INFO, &infoReq, sizeof(infoReq), 
                                   &infoReq, sizeof(infoReq), &br, NULL)) {
                    printf("   ?? Device Info: \"%s\"\n", infoReq.device_info);
                } else {
                    printf("   ??  Failed to get device info after opening %s\n", deviceNames[i]);
                }
                
                // Test hardware state
                AVB_HW_STATE_QUERY stateReq;
                ZeroMemory(&stateReq, sizeof(stateReq));
                
                if (DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE, &stateReq, sizeof(stateReq), 
                                   &stateReq, sizeof(stateReq), &br, NULL)) {
                    printf("   ?? Hardware State: %lu", stateReq.hw_state);
                    switch (stateReq.hw_state) {
                        case 0: printf(" (BOUND)\n"); break;
                        case 1: printf(" (BAR_MAPPED)\n"); break;
                        case 2: printf(" (PTP_READY)\n"); break;
                        default: printf(" (UNKNOWN)\n"); break;
                    }
                    printf("   ?? HW VID/DID: 0x%04X/0x%04X\n", stateReq.vendor_id, stateReq.device_id);
                    printf("   ?? HW Capabilities: 0x%08X\n", stateReq.capabilities);
                } else {
                    printf("   ??  Failed to get hardware state for %s\n", deviceNames[i]);
                }
                
            } else {
                printf("   ? Failed to open %s adapter (status=0x%08X)\n", deviceNames[i], openReq.status);
            }
        } else {
            printf("   ? IOCTL_AVB_OPEN_ADAPTER failed for %s: %lu\n", deviceNames[i], GetLastError());
        }
    }
}

static void TestRegisterAccessForEachAdapter(HANDLE h) {
    printf("\n?? === REGISTER ACCESS TEST (ALL ADAPTERS) ===\n");
    
    // Test device IDs we expect to find
    USHORT testDeviceIds[] = {0x1533, 0x125B}; // I210, I226
    char* deviceNames[] = {"I210", "I226"};
    
    for (int i = 0; i < sizeof(testDeviceIds)/sizeof(testDeviceIds[0]); i++) {
        printf("\n?? Testing register access for %s (DID=0x%04X):\n", deviceNames[i], testDeviceIds[i]);
        
        // First, open the specific adapter
        AVB_OPEN_REQUEST openReq;
        ZeroMemory(&openReq, sizeof(openReq));
        openReq.vendor_id = 0x8086;
        openReq.device_id = testDeviceIds[i];
        
        DWORD br = 0;
        if (DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, &openReq, sizeof(openReq), 
                           &openReq, sizeof(openReq), &br, NULL) && 
            openReq.status == 0) {
            
            printf("   ? Opened %s adapter for register testing\n", deviceNames[i]);
            
            // Test reading CTRL register (common to all Intel devices)
            AVB_REGISTER_REQUEST regReq;
            ZeroMemory(&regReq, sizeof(regReq));
            regReq.offset = 0x00000; // CTRL register
            
            if (DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &regReq, sizeof(regReq), 
                               &regReq, sizeof(regReq), &br, NULL)) {
                printf("   ?? CTRL (0x00000): 0x%08X\n", regReq.value);
                
                if (regReq.value == 0x00000000 || regReq.value == 0xFFFFFFFF) {
                    printf("   ??  Suspicious CTRL value (may indicate hardware access issue)\n");
                }
            } else {
                printf("   ? Failed to read CTRL register from %s\n", deviceNames[i]);
            }
            
            // Test device-specific registers
            if (testDeviceIds[i] == 0x1533) { // I210
                printf("   ?? Testing I210-specific PTP registers:\n");
                
                ULONG ptpRegisters[] = {0x0B600, 0x0B604, 0x0B608, 0x0B640}; // SYSTIML, SYSTIMH, TIMINCA, TSAUXC
                char* ptpNames[] = {"SYSTIML", "SYSTIMH", "TIMINCA", "TSAUXC"};
                
                for (int j = 0; j < 4; j++) {
                    regReq.offset = ptpRegisters[j];
                    if (DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &regReq, sizeof(regReq), 
                                       &regReq, sizeof(regReq), &br, NULL)) {
                        printf("     %s (0x%05X): 0x%08X", ptpNames[j], ptpRegisters[j], regReq.value);
                        
                        if (j <= 1 && regReq.value == 0) { // SYSTIM registers
                            printf(" (??  Clock not running)");
                        } else if (j == 3 && (regReq.value & 0x40000000)) { // TSAUXC PHC enable
                            printf(" (? PHC enabled)");
                        }
                        printf("\n");
                    }
                }
            } else if (testDeviceIds[i] == 0x125B) { // I226
                printf("   ?? Testing I226-specific TSN registers:\n");
                
                ULONG tsnRegisters[] = {0x08600, 0x08700}; // TAS_CTRL, FP_CONFIG
                char* tsnNames[] = {"TAS_CTRL", "FP_CONFIG"};
                
                for (int j = 0; j < 2; j++) {
                    regReq.offset = tsnRegisters[j];
                    if (DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &regReq, sizeof(regReq), 
                                       &regReq, sizeof(regReq), &br, NULL)) {
                        printf("     %s (0x%05X): 0x%08X\n", tsnNames[j], tsnRegisters[j], regReq.value);
                    }
                }
            }
            
        } else {
            printf("   ? Could not open %s adapter for register testing\n", deviceNames[i]);
        }
    }
}

static void PrintSummary(void) {
    printf("\n?? === TEST SUMMARY ===\n");
    printf("Multi-Adapter Diagnostic Test completed.\n\n");
    printf("?? Expected Results on Your System:\n");
    printf("   - Total Adapters: 2\n");
    printf("   - Adapter #0: Intel I210-T1 (DID=0x1533, capabilities with BASIC_1588)\n");
    printf("   - Adapter #1: Intel I226-V (DID=0x125B, capabilities with TSN features)\n\n");
    printf("??  If you see different results:\n");
    printf("   1. Check that IntelAvbFilter is bound to both adapters\n");
    printf("   2. Verify both adapters are Intel AVB-capable devices\n");
    printf("   3. Check driver logs in DebugView for initialization details\n\n");
    printf("?? Next Steps:\n");
    printf("   - Test individual adapter targeting\n");
    printf("   - Validate PTP functionality on I210\n");
    printf("   - Test TSN features on I226\n");
}

int main(void) {
    printf("Intel AVB Filter Driver - Multi-Adapter Test Tool\n");
    printf("==================================================\n");
    
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    // Initialize device
    DWORD br = 0;
    BOOL result = DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &br, NULL);
    if (result) {
        printf("? Device initialization successful\n");
    } else {
        printf("??  Device initialization failed: %lu\n", GetLastError());
    }
    
    // Run comprehensive multi-adapter tests
    TestMultiAdapterEnumeration(h);
    TestAdapterSelection(h);
    TestRegisterAccessForEachAdapter(h);
    PrintSummary();
    
    CloseHandle(h);
    return 0;
}