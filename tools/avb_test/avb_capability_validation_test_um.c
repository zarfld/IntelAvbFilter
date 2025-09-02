/*++

Module Name:

    avb_capability_validation_test_um.c

Abstract:

    User-mode test to validate realistic hardware capability reporting
    Tests the "Hardware Capability Reality" architectural requirement
    
    Validates that devices only report capabilities they actually support:
    - TSN only on I225/I226 (2019+) 
    - No false advertising of advanced features on legacy hardware
    - Proper capability bits based on actual Intel hardware specifications

--*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// AVB IOCTL definitions
#include "..\\..\\external\\intel_avb\\include\\avb_ioctl.h"

// Intel device type definitions (from driver)
typedef enum {
    INTEL_DEVICE_UNKNOWN = 0,
    INTEL_DEVICE_82575,   // 2008 - No PTP
    INTEL_DEVICE_82576,   // 2009 - No PTP  
    INTEL_DEVICE_82580,   // 2010 - Basic PTP
    INTEL_DEVICE_I350,    // 2012 - Standard IEEE 1588
    INTEL_DEVICE_I354,    // 2012 - Same as I350
    INTEL_DEVICE_I210,    // 2013 - Enhanced PTP, NO TSN
    INTEL_DEVICE_I217,    // 2013 - Basic PTP
    INTEL_DEVICE_I219,    // 2014 - Enhanced PTP, NO TSN
    INTEL_DEVICE_I225,    // 2019 - First Intel TSN
    INTEL_DEVICE_I226     // 2020 - Full TSN + EEE
} intel_device_type_t;

// Intel capability flags (from driver)
#define INTEL_CAP_MMIO          0x00000001
#define INTEL_CAP_MDIO          0x00000002
#define INTEL_CAP_BASIC_1588    0x00000004
#define INTEL_CAP_ENHANCED_TS   0x00000008
#define INTEL_CAP_TSN_TAS       0x00000010
#define INTEL_CAP_TSN_FP        0x00000020
#define INTEL_CAP_PCIe_PTM      0x00000040
#define INTEL_CAP_2_5G          0x00000080
#define INTEL_CAP_EEE           0x00000100

// Device ID to type mapping (from driver)
intel_device_type_t GetDeviceTypeFromDID(USHORT device_id) {
    switch(device_id) {
        // I-series modern devices
        case 0x1533: return INTEL_DEVICE_I210;  // I210 Copper
        case 0x1534: return INTEL_DEVICE_I210;  // I210 Copper OEM1  
        case 0x1535: return INTEL_DEVICE_I210;  // I210 Copper IT
        case 0x1536: return INTEL_DEVICE_I210;  // I210 Fiber
        case 0x1537: return INTEL_DEVICE_I210;  // I210 Serdes
        case 0x1538: return INTEL_DEVICE_I210;  // I210 SGMII
        
        case 0x153A: 
        case 0x153B: return INTEL_DEVICE_I217;  // I217 family
        
        case 0x15B7: 
        case 0x15B8: 
        case 0x15D6: 
        case 0x15D7: 
        case 0x15D8: 
        case 0x0DC7: 
        case 0x1570: 
        case 0x15E3: return INTEL_DEVICE_I219;  // I219 family
        
        case 0x15F2: return INTEL_DEVICE_I225;  // I225
        case 0x125B: return INTEL_DEVICE_I226;  // I226
        
        // IGB device family (82xxx series)
        case 0x10A7: return INTEL_DEVICE_82575;  // 82575EB Copper
        case 0x10A9: return INTEL_DEVICE_82575;  // 82575EB Fiber/Serdes  
        case 0x10D6: return INTEL_DEVICE_82575;  // 82575GB Quad Copper
        
        case 0x10C9: return INTEL_DEVICE_82576;  // 82576 Gigabit Network Connection
        case 0x10E6: return INTEL_DEVICE_82576;  // 82576 Fiber
        case 0x10E7: return INTEL_DEVICE_82576;  // 82576 Serdes
        case 0x10E8: return INTEL_DEVICE_82576;  // 82576 Quad Copper
        case 0x1526: return INTEL_DEVICE_82576;  // 82576 Quad Copper ET2
        case 0x150A: return INTEL_DEVICE_82576;  // 82576 NS
        case 0x1518: return INTEL_DEVICE_82576;  // 82576 NS Serdes
        case 0x150D: return INTEL_DEVICE_82576;  // 82576 Serdes Quad
        
        case 0x150E: return INTEL_DEVICE_82580;  // 82580 Copper
        case 0x150F: return INTEL_DEVICE_82580;  // 82580 Fiber
        case 0x1510: return INTEL_DEVICE_82580;  // 82580 Serdes
        case 0x1511: return INTEL_DEVICE_82580;  // 82580 SGMII
        case 0x1516: return INTEL_DEVICE_82580;  // 82580 Copper Dual
        case 0x1527: return INTEL_DEVICE_82580;  // 82580 Quad Fiber
        
        case 0x1521: return INTEL_DEVICE_I350;   // I350 Copper
        case 0x1522: return INTEL_DEVICE_I350;   // I350 Fiber
        case 0x1523: return INTEL_DEVICE_I350;   // I350 Serdes
        case 0x1524: return INTEL_DEVICE_I350;   // I350 SGMII
        case 0x1546: return INTEL_DEVICE_I350;   // I350 DA4
        
        // I354 uses same operations as I350
        case 0x1F40: return INTEL_DEVICE_I354;   // I354 Backplane 2.5GbE
        case 0x1F41: return INTEL_DEVICE_I354;   // I354 Backplane 1GbE
        case 0x1F45: return INTEL_DEVICE_I354;   // I354 SGMII
        
        default: return INTEL_DEVICE_UNKNOWN; 
    }
}

// Expected capabilities based on Intel hardware reality
ULONG GetExpectedCapabilities(intel_device_type_t device_type) {
    switch (device_type) {
        // Legacy IGB devices - REALISTIC capabilities only
        case INTEL_DEVICE_82575:
            return INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic hardware only - NO PTP (2008 era)
        case INTEL_DEVICE_82576:
            return INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic hardware only - NO PTP (2009 era)
        case INTEL_DEVICE_82580:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic PTP added (2010)
        case INTEL_DEVICE_I350:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Standard IEEE 1588 (2012)
        case INTEL_DEVICE_I354:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Same as I350 (2012)
            
        // Modern I-series devices - REALISTIC capabilities based on actual hardware
        case INTEL_DEVICE_I210:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO; // Enhanced PTP, NO TSN (2013)
        case INTEL_DEVICE_I217:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic PTP (2013)
        case INTEL_DEVICE_I219:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Enhanced PTP, NO TSN (2014)
            
        // ONLY I225/I226 support TSN - TSN standard finalized 2015-2016, first Intel implementation 2019
        case INTEL_DEVICE_I225:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                   INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO; // First Intel TSN (2019)
        case INTEL_DEVICE_I226:
            return INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                   INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE; // Full TSN (2020)
        default:
            return INTEL_CAP_MMIO; // Minimal safe assumption
    }
}

const char* GetDeviceName(intel_device_type_t device_type) {
    switch (device_type) {
        case INTEL_DEVICE_82575: return "82575EB (2008 - No PTP)";
        case INTEL_DEVICE_82576: return "82576 (2009 - No PTP)";
        case INTEL_DEVICE_82580: return "82580 (2010 - Basic PTP)";
        case INTEL_DEVICE_I350: return "I350 (2012 - IEEE 1588)";
        case INTEL_DEVICE_I354: return "I354 (2012 - IEEE 1588)";
        case INTEL_DEVICE_I210: return "I210 (2013 - Enhanced PTP, NO TSN)";
        case INTEL_DEVICE_I217: return "I217 (2013 - Basic PTP)";
        case INTEL_DEVICE_I219: return "I219 (2014 - Enhanced PTP, NO TSN)";
        case INTEL_DEVICE_I225: return "I225 (2019 - FIRST Intel TSN)";
        case INTEL_DEVICE_I226: return "I226 (2020 - Full TSN + EEE)";
        default: return "Unknown";
    }
}

void PrintCapabilities(ULONG caps, const char* prefix) {
    printf("%s(0x%08X): ", prefix, caps);
    if (caps & INTEL_CAP_MMIO) printf("MMIO ");
    if (caps & INTEL_CAP_MDIO) printf("MDIO ");
    if (caps & INTEL_CAP_BASIC_1588) printf("BASIC_1588 ");
    if (caps & INTEL_CAP_ENHANCED_TS) printf("ENHANCED_TS ");
    if (caps & INTEL_CAP_TSN_TAS) printf("TSN_TAS ");
    if (caps & INTEL_CAP_TSN_FP) printf("TSN_FP ");
    if (caps & INTEL_CAP_PCIe_PTM) printf("PCIe_PTM ");
    if (caps & INTEL_CAP_2_5G) printf("2_5G ");
    if (caps & INTEL_CAP_EEE) printf("EEE ");
    if (caps == 0) printf("<none>");
    printf("\\n");
}

int main() {
    printf("Intel AVB Filter Driver - Capability Validation Test\\n");
    printf("====================================================\\n");
    printf("Purpose: Verify realistic hardware capability reporting\\n");
    printf("Requirement: No false advertising of advanced features\\n\\n");
    
    HANDLE hDevice = CreateFile(L"\\\\.\\\\IntelAvbFilter",
                               GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("? Failed to open driver (Error: %lu)\\n", GetLastError());
        printf("   This is expected if no Intel AVB hardware is present\\n");
        return 0; // Not a failure - just no hardware
    }
    
    printf("? Driver connection successful\\n\\n");
    
    // Test 1: Enumerate adapters and validate capabilities
    printf("?? Test 1: Capability Validation for All Detected Adapters\\n");
    printf("=========================================================\\n");
    
    AVB_ENUM_REQUEST enum_req = {0};
    DWORD bytesReturned;
    
    // Get adapter count
    BOOL result = DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                                 &enum_req, sizeof(enum_req),
                                 &enum_req, sizeof(enum_req),
                                 &bytesReturned, NULL);
    
    if (!result) {
        printf("? ENUM_ADAPTERS failed: %lu\\n", GetLastError());
        CloseHandle(hDevice);
        return 1;
    }
    
    printf("Found %lu Intel adapter(s)\\n\\n", enum_req.count);
    
    if (enum_req.count == 0) {
        printf("?? No Intel adapters detected - test complete\\n");
        CloseHandle(hDevice);
        return 0;
    }
    
    int validation_failures = 0;
    
    // Validate each adapter
    for (ULONG i = 0; i < enum_req.count; i++) {
        printf("--- Adapter %lu ---\\n", i);
        
        enum_req.index = i;
        result = DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                                &enum_req, sizeof(enum_req),
                                &enum_req, sizeof(enum_req),
                                &bytesReturned, NULL);
        
        if (!result) {
            printf("? Failed to get adapter %lu info: %lu\\n", i, GetLastError());
            validation_failures++;
            continue;
        }
        
        printf("Device: VID=0x%04X DID=0x%04X\\n", enum_req.vendor_id, enum_req.device_id);
        
        intel_device_type_t device_type = GetDeviceTypeFromDID(enum_req.device_id);
        printf("Type: %s\\n", GetDeviceName(device_type));
        
        ULONG expected_caps = GetExpectedCapabilities(device_type);
        ULONG actual_caps = enum_req.capabilities;
        
        PrintCapabilities(expected_caps, "Expected ");
        PrintCapabilities(actual_caps, "Actual   ");
        
        // Validation logic
        BOOL validation_passed = TRUE;
        
        // Check for false advertising (reporting capabilities not supported)
        if (actual_caps & ~expected_caps) {
            printf("? FALSE ADVERTISING: Device reports unsupported capabilities\\n");
            ULONG false_caps = actual_caps & ~expected_caps;
            PrintCapabilities(false_caps, "False    ");
            validation_passed = FALSE;
        }
        
        // Critical checks for TSN false advertising
        if (device_type < INTEL_DEVICE_I225) { // Pre-2019 hardware
            if (actual_caps & (INTEL_CAP_TSN_TAS | INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM)) {
                printf("? CRITICAL: Pre-2019 hardware falsely advertises TSN support!\\n");
                printf("   TSN Standard: 2015-2016, First Intel TSN: I225 (2019)\\n");
                validation_passed = FALSE;
            }
        }
        
        // Check for missing basic capabilities
        if (!(actual_caps & INTEL_CAP_MMIO)) {
            printf("? Missing basic MMIO capability\\n");
            validation_passed = FALSE;
        }
        
        if (validation_passed) {
            printf("? Capability validation PASSED\\n");
        } else {
            printf("? Capability validation FAILED\\n");
            validation_failures++;
        }
        
        printf("\\n");
    }
    
    // Summary
    printf("===================\\n");
    printf("Validation Summary:\\n");
    printf("===================\\n");
    printf("Adapters tested: %lu\\n", enum_req.count);
    printf("Validation failures: %d\\n", validation_failures);
    
    if (validation_failures == 0) {
        printf("? ALL CAPABILITY VALIDATIONS PASSED\\n");
        printf("? No false advertising detected\\n");
        printf("? Hardware capabilities are realistic and honest\\n");
    } else {
        printf("? CAPABILITY VALIDATION FAILED\\n");
        printf("? Driver is reporting incorrect capabilities\\n");
        printf("? This violates the 'Hardware Capability Reality' requirement\\n");
    }
    
    CloseHandle(hDevice);
    return validation_failures;
}