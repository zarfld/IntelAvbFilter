/*++

Module Name:

    avb_i226_advanced_test.c

Abstract:

    Intel I226 Advanced Feature Testing Tool
    Tests I226-specific features that weren't covered in basic testing:
    - EEE (Energy Efficient Ethernet)
    - PCIe PTM advanced configuration
    - Advanced MDIO PHY operations
    - 2.5G speed configuration
    - Interrupt management (EITR)
    - Advanced queue management

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

// I226 Advanced register definitions (from i226_regs.h SSOT)
#define I226_EEE_CTRL        0x01580  // EEE Control Register
#define I226_EEE_STATUS      0x01584  // EEE Status Register  
#define I226_EITR0           0x01680  // Interrupt Throttle Vector 0
#define I226_EITR1           0x01684  // Interrupt Throttle Vector 1
#define I226_IVAR0           0x01700  // Interrupt Vector Allocation 0
#define I226_GPIE            0x01514  // General Purpose Interrupt Enable
#define I226_MDIC            0x00020  // MDI Control Register
#define I226_CTRL            0x00000  // Control Register
#define I226_CTRL_EXT        0x00018  // Extended Control Register
#define I226_STATUS          0x00008  // Device Status Register

static HANDLE OpenDevice(void) {
    HANDLE h = CreateFileA(LINKNAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("? Failed to open %s (Error: %lu)\n", LINKNAME, GetLastError());
        printf("   Make sure Intel AVB Filter driver is installed and I226 hardware present\n");
    } else {
        printf("? Device opened successfully: %s\n", LINKNAME);
    }
    return h;
}

static BOOL ReadRegister(HANDLE h, ULONG offset, ULONG *value) {
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    
    DWORD br = 0;
    BOOL result = DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &req, sizeof(req), 
                                 &req, sizeof(req), &br, NULL);
    if (result) {
        *value = req.value;
    }
    return result;
}

static BOOL WriteRegister(HANDLE h, ULONG offset, ULONG value) {
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    req.value = value;
    
    DWORD br = 0;
    return DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &req, sizeof(req), 
                          &req, sizeof(req), &br, NULL);
}

static BOOL SelectI226Context(HANDLE h) {
    AVB_OPEN_REQUEST openReq;
    ZeroMemory(&openReq, sizeof(openReq));
    openReq.vendor_id = 0x8086;
    openReq.device_id = 0x125B; // I226
    
    DWORD br = 0;
    BOOL result = DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, &openReq, sizeof(openReq), 
                                 &openReq, sizeof(openReq), &br, NULL);
    
    if (!result || openReq.status != 0) {
        printf("? Failed to select I226 context (status=0x%08X, error=%lu)\n", 
               openReq.status, GetLastError());
        return FALSE;
    }
    
    printf("? I226 context selected successfully\n");
    return TRUE;
}

/**
 * @brief Test EEE (Energy Efficient Ethernet) features
 * Tests the I226 EEE capability that was reported but not validated
 */
static void TestI226EEE(HANDLE h) {
    printf("\n?? === I226 EEE (ENERGY EFFICIENT ETHERNET) TEST ===\n");
    printf("Testing I226 EEE features using SSOT register definitions\n");
    
    if (!SelectI226Context(h)) return;
    
    ULONG eee_ctrl = 0, eee_status = 0;
    
    // Step 1: Read current EEE state
    printf("?? Step 1: Reading current EEE configuration...\n");
    if (ReadRegister(h, I226_EEE_CTRL, &eee_ctrl)) {
        printf("   I226_EEE_CTRL (0x%05X): 0x%08X", I226_EEE_CTRL, eee_ctrl);
        
        // Analyze EEE control bits (Intel I226 datasheet)
        if (eee_ctrl & 0x00000001) printf(" (? EEE enabled)");
        else printf(" (? EEE disabled)");
        
        if (eee_ctrl & 0x00000002) printf(" (? TX LPI enabled)");
        if (eee_ctrl & 0x00000004) printf(" (? RX LPI enabled)");
        printf("\n");
    } else {
        printf("? Failed to read I226_EEE_CTRL register\n");
        return;
    }
    
    if (ReadRegister(h, I226_EEE_STATUS, &eee_status)) {
        printf("   I226_EEE_STATUS (0x%05X): 0x%08X", I226_EEE_STATUS, eee_status);
        
        // Analyze EEE status bits
        if (eee_status & 0x00000001) printf(" (? EEE negotiated)");
        else printf(" (? EEE not negotiated)");
        
        if (eee_status & 0x00000002) printf(" (? TX LPI active)");
        if (eee_status & 0x00000004) printf(" (? RX LPI active)");
        printf("\n");
    }
    
    // Step 2: Test EEE activation if not already enabled
    if (!(eee_ctrl & 0x00000001)) {
        printf("?? Step 2: Testing EEE activation...\n");
        
        ULONG new_eee_ctrl = eee_ctrl | 0x00000007; // Enable EEE + TX LPI + RX LPI
        if (WriteRegister(h, I226_EEE_CTRL, new_eee_ctrl)) {
            printf("   ? EEE enable write successful\n");
            
            Sleep(100); // Allow hardware to process
            
            // Read back to verify
            ULONG readback = 0;
            if (ReadRegister(h, I226_EEE_CTRL, &readback)) {
                printf("   Readback I226_EEE_CTRL: 0x%08X", readback);
                if (readback & 0x00000001) {
                    printf(" (? EEE ACTIVATION SUCCESS)\n");
                } else {
                    printf(" (? EEE activation failed)\n");
                }
            }
        } else {
            printf("   ? EEE enable write failed\n");
        }
    } else {
        printf("? EEE already enabled\n");
    }
    
    // Step 3: Monitor EEE power savings (if available)
    printf("?? Step 3: EEE power state monitoring...\n");
    for (int i = 0; i < 3; i++) {
        if (ReadRegister(h, I226_EEE_STATUS, &eee_status)) {
            printf("   EEE Status sample %d: 0x%08X", i+1, eee_status);
            if (eee_status & 0x00000002) printf(" (TX LPI active)");
            if (eee_status & 0x00000004) printf(" (RX LPI active)");
            printf("\n");
        }
        Sleep(50);
    }
}

/**
 * @brief Test advanced PCIe PTM configuration
 * Goes beyond basic PTM detection to test actual timing synchronization
 */
static void TestI226AdvancedPTM(HANDLE h) {
    printf("\n?? === I226 ADVANCED PCIe PTM TEST ===\n");
    printf("Testing PCIe Precision Time Measurement advanced features\n");
    
    if (!SelectI226Context(h)) return;
    
    // Step 1: Read extended control register for PTM hints
    printf("?? Step 1: Advanced PTM configuration analysis...\n");
    ULONG ctrl_ext = 0;
    if (ReadRegister(h, I226_CTRL_EXT, &ctrl_ext)) {
        printf("   I226_CTRL_EXT: 0x%08X\n", ctrl_ext);
        
        // Analyze PTM-related bits (Intel I226 datasheet required for exact masks)
        if (ctrl_ext & 0x00100000) printf("   ? PTM capability present\n");
        else printf("   ? PTM capability not detected\n");
        
        if (ctrl_ext & 0x00200000) printf("   ? PTM enabled\n");
        else printf("   ?? PTM not enabled\n");
    }
    
    // Step 2: Test PTM timing precision
    printf("?? Step 2: PTM timing precision test...\n");
    
    // Read multiple timestamps to check precision
    AVB_TIMESTAMP_REQUEST tsReq;
    ULONGLONG timestamps[5];
    BOOL timestamp_success = FALSE;
    
    for (int i = 0; i < 5; i++) {
        ZeroMemory(&tsReq, sizeof(tsReq));
        tsReq.clock_id = 0; // System clock
        
        DWORD br = 0;
        if (DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &tsReq, sizeof(tsReq), 
                           &tsReq, sizeof(tsReq), &br, NULL)) {
            timestamps[i] = tsReq.timestamp;
            timestamp_success = TRUE;
            printf("   Timestamp %d: 0x%016llX", i+1, timestamps[i]);
            if (i > 0) {
                LONGLONG delta = timestamps[i] - timestamps[i-1];
                printf(" (delta: %lld ns)", delta);
            }
            printf("\n");
        } else {
            printf("   ? Failed to get timestamp %d\n", i+1);
        }
        Sleep(10);
    }
    
    if (timestamp_success) {
        // Calculate precision metrics
        LONGLONG total_delta = timestamps[4] - timestamps[0];
        LONGLONG avg_delta = total_delta / 4;
        printf("   ?? PTM Precision Analysis:\n");
        printf("     Total time span: %lld ns\n", total_delta);
        printf("     Average sample delta: %lld ns\n", avg_delta);
        printf("     Expected delta: ~10,000,000 ns (10ms)\n");
        
        if (avg_delta > 8000000 && avg_delta < 12000000) {
            printf("   ? PTM timing precision: GOOD\n");
        } else {
            printf("   ?? PTM timing precision: UNUSUAL (may indicate issues)\n");
        }
    }
}

/**
 * @brief Test advanced MDIO PHY management features
 * Tests PHY register access and advanced configuration
 */
static void TestI226AdvancedMDIO(HANDLE h) {
    printf("\n?? === I226 ADVANCED MDIO PHY MANAGEMENT TEST ===\n");
    printf("Testing I226 MDIO advanced features for PHY management\n");
    
    if (!SelectI226Context(h)) return;
    
    // Step 1: Analyze MDIO control register
    printf("?? Step 1: MDIO control register analysis...\n");
    ULONG mdic = 0;
    if (ReadRegister(h, I226_MDIC, &mdic)) {
        printf("   I226_MDIC: 0x%08X\n", mdic);
        
        // Analyze MDIO bits (Intel I226 datasheet)
        ULONG phy_addr = (mdic >> 21) & 0x1F;
        ULONG reg_addr = (mdic >> 16) & 0x1F;
        ULONG data = mdic & 0xFFFF;
        BOOL ready = (mdic & 0x10000000) != 0;
        BOOL error = (mdic & 0x40000000) != 0;
        
        printf("   ?? MDIO State Analysis:\n");
        printf("     PHY Address: 0x%02X\n", phy_addr);
        printf("     Register Address: 0x%02X\n", reg_addr);
        printf("     Data: 0x%04X\n", data);
        printf("     Ready: %s\n", ready ? "? YES" : "? NO");
        printf("     Error: %s\n", error ? "? YES" : "? NO");
        
        if (!ready) {
            printf("   ?? MDIO not ready - testing MDIO initialization...\n");
            
            // Test MDIO initialization sequence
            printf("?? Testing MDIO readiness...\n");
            
            // Wait for MDIO to become ready
            for (int attempt = 0; attempt < 10; attempt++) {
                Sleep(10);
                if (ReadRegister(h, I226_MDIC, &mdic) && (mdic & 0x10000000)) {
                    printf("   ? MDIO became ready after %d attempts\n", attempt + 1);
                    ready = TRUE;
                    break;
                }
            }
            
            if (!ready) {
                printf("   ? MDIO still not ready after waiting\n");
                return;
            }
        }
    }
    
    // Step 2: Test PHY register access (standard IEEE registers)
    printf("?? Step 2: PHY register access test...\n");
    
    // Test reading standard PHY registers (IEEE 802.3)
    USHORT phy_regs[] = {0x00, 0x01, 0x02, 0x03}; // Control, Status, ID1, ID2
    char* phy_names[] = {"BMCR", "BMSR", "PHYID1", "PHYID2"};
    
    for (int i = 0; i < 4; i++) {
        // Construct MDIO read command
        ULONG mdio_cmd = 0x08000000 |        // Read operation
                        (0x01 << 21) |        // PHY address 1 (typical)
                        (phy_regs[i] << 16);  // Register address
        
        if (WriteRegister(h, I226_MDIC, mdio_cmd)) {
            // Wait for completion
            Sleep(1);
            
            ULONG mdic_result = 0;
            if (ReadRegister(h, I226_MDIC, &mdic_result)) {
                if (mdic_result & 0x10000000) { // Ready bit
                    if (!(mdic_result & 0x40000000)) { // No error
                        USHORT phy_data = (USHORT)(mdic_result & 0xFFFF);
                        printf("   %s (0x%02X): 0x%04X", phy_names[i], phy_regs[i], phy_data);
                        
                        // Analyze specific registers
                        if (i == 0) { // BMCR
                            if (phy_data & 0x8000) printf(" (Reset)");
                            if (phy_data & 0x4000) printf(" (Loopback)");
                            if (phy_data & 0x1000) printf(" (Auto-neg)");
                            if (phy_data & 0x0200) printf(" (Restart auto-neg)");
                        } else if (i == 1) { // BMSR
                            if (phy_data & 0x0004) printf(" (? Link up)");
                            else printf(" (? Link down)");
                            if (phy_data & 0x0020) printf(" (? Auto-neg complete)");
                        }
                        printf("\n");
                    } else {
                        printf("   ? PHY %s read error\n", phy_names[i]);
                    }
                } else {
                    printf("   ? PHY %s read timeout\n", phy_names[i]);
                }
            }
        } else {
            printf("   ? Failed to initiate PHY %s read\n", phy_names[i]);
        }
    }
}

/**
 * @brief Test 2.5G speed configuration and capabilities
 * Tests I226's advanced 2.5 Gigabit features
 */
static void TestI2265GSpeed(HANDLE h) {
    printf("\n?? === I226 2.5G SPEED CONFIGURATION TEST ===\n");
    printf("Testing I226 2.5 Gigabit speed capabilities\n");
    
    if (!SelectI226Context(h)) return;
    
    // Step 1: Read current link status
    printf("?? Step 1: Current link status analysis...\n");
    ULONG status = 0, ctrl = 0;
    
    if (ReadRegister(h, I226_STATUS, &status)) {
        printf("   I226_STATUS: 0x%08X\n", status);
        
        // Analyze status bits (Intel I226 specific)
        ULONG speed_indication = (status >> 6) & 0x03; // Speed bits 7:6
        switch (speed_indication) {
            case 0: printf("   ?? Current Speed: 10 Mbps\n"); break;
            case 1: printf("   ?? Current Speed: 100 Mbps\n"); break;
            case 2: printf("   ????? Current Speed: 1000 Mbps\n"); break;
            case 3: printf("   ?? Current Speed: 2500 Mbps (2.5G)\n"); break;
        }
        
        if (status & 0x00000002) printf("   ? Link is UP\n");
        else printf("   ? Link is DOWN\n");
        
        if (status & 0x00000080) printf("   ? Full duplex\n");
        else printf("   ? Half duplex\n");
    }
    
    if (ReadRegister(h, I226_CTRL, &ctrl)) {
        printf("   I226_CTRL: 0x%08X\n", ctrl);
        
        // Analyze control settings
        if (ctrl & 0x00000020) printf("   ? Auto-negotiation enabled\n");
        else printf("   ? Auto-negotiation disabled\n");
        
        if (ctrl & 0x00001000) printf("   ? Reset in progress\n");
    }
    
    // Step 2: Test 2.5G capability negotiation
    printf("? Step 2: 2.5G capability testing...\n");
    
    // Read PHY capabilities for 2.5G support (vendor-specific registers)
    // This requires MDIO access to vendor-specific PHY registers
    printf("   ?? Checking PHY 2.5G capabilities...\n");
    
    // Intel I226 typically uses Intel PHY - check vendor-specific registers
    // Register 0x09: 1000BASE-T Control (includes 2.5G advertisement)
    ULONG mdio_cmd = 0x08000000 | (0x01 << 21) | (0x09 << 16);
    if (WriteRegister(h, I226_MDIC, mdio_cmd)) {
        Sleep(2);
        ULONG mdic_result = 0;
        if (ReadRegister(h, I226_MDIC, &mdic_result) && 
            (mdic_result & 0x10000000) && !(mdic_result & 0x40000000)) {
            
            USHORT phy_1000t_ctrl = (USHORT)(mdic_result & 0xFFFF);
            printf("   PHY 1000BASE-T_CTRL: 0x%04X", phy_1000t_ctrl);
            
            // Check for 2.5G advertisement bits (vendor-specific)
            if (phy_1000t_ctrl & 0x0400) printf(" (? 2.5G capable)");
            else printf(" (? 2.5G not advertised)");
            printf("\n");
        }
    }
    
    // Step 3: Check current negotiated speed via MDIO
    printf("?? Step 3: Current negotiated speed verification...\n");
    
    // Read PHY status register for actual negotiated speed
    mdio_cmd = 0x08000000 | (0x01 << 21) | (0x01 << 16); // BMSR
    if (WriteRegister(h, I226_MDIC, mdio_cmd)) {
        Sleep(2);
        ULONG mdic_result = 0;
        if (ReadRegister(h, I226_MDIC, &mdic_result) && 
            (mdic_result & 0x10000000) && !(mdic_result & 0x40000000)) {
            
            USHORT bmsr = (USHORT)(mdic_result & 0xFFFF);
            printf("   PHY BMSR: 0x%04X", bmsr);
            
            if (bmsr & 0x0004) {
                printf(" (? Link up)\n");
                
                // For detailed speed, need to read vendor-specific status registers
                printf("   ?? Reading vendor-specific speed status...\n");
                
                // Try reading Intel PHY vendor status (register varies by PHY model)
                for (USHORT test_reg = 0x10; test_reg <= 0x1F; test_reg++) {
                    mdio_cmd = 0x08000000 | (0x01 << 21) | (test_reg << 16);
                    if (WriteRegister(h, I226_MDIC, mdio_cmd)) {
                        Sleep(1);
                        if (ReadRegister(h, I226_MDIC, &mdic_result) && 
                            (mdic_result & 0x10000000) && !(mdic_result & 0x40000000)) {
                            
                            USHORT vendor_status = (USHORT)(mdic_result & 0xFFFF);
                            if (vendor_status != 0 && vendor_status != 0xFFFF) {
                                printf("     PHY[0x%02X]: 0x%04X", test_reg, vendor_status);
                                
                                // Look for speed indication patterns
                                if ((vendor_status & 0xC000) == 0xC000) {
                                    printf(" (?? Possible 2.5G indication)");
                                } else if ((vendor_status & 0x8000) == 0x8000) {
                                    printf(" (????? Possible 1G indication)");
                                }
                                printf("\n");
                            }
                        }
                    }
                }
            } else {
                printf(" (? Link down)\n");
            }
        }
    }
}

/**
 * @brief Test interrupt management features
 * Tests I226's advanced interrupt throttling and vector allocation
 */
static void TestI226InterruptManagement(HANDLE h) {
    printf("\n? === I226 INTERRUPT MANAGEMENT TEST ===\n");
    printf("Testing I226 advanced interrupt features (EITR, IVAR, GPIE)\n");
    
    if (!SelectI226Context(h)) return;
    
    // Step 1: Test interrupt throttle registers (EITR)
    printf("? Step 1: Interrupt throttle configuration...\n");
    
    ULONG eitr_regs[] = {I226_EITR0, I226_EITR1};
    char* eitr_names[] = {"EITR0", "EITR1"};
    
    for (int i = 0; i < 2; i++) {
        ULONG eitr_val = 0;
        if (ReadRegister(h, eitr_regs[i], &eitr_val)) {
            printf("   %s (0x%05X): 0x%08X", eitr_names[i], eitr_regs[i], eitr_val);
            
            // Analyze EITR fields using I226 SSOT definitions
            ULONG interval = eitr_val & 0xFFFF;        // Bits 15:0
            ULONG counter = (eitr_val >> 16) & 0xFFFF; // Bits 31:16
            
            printf(" (Interval: %lu, Counter: %lu)", interval, counter);
            
            if (interval > 0) {
                ULONG throttle_us = interval * 256 / 1000; // Convert to microseconds
                printf(" (?? %lu ?s throttle)", throttle_us);
            } else {
                printf(" (? No throttling)");
            }
            printf("\n");
        }
    }
    
    // Step 2: Test interrupt vector allocation (IVAR)
    printf("?? Step 2: Interrupt vector allocation...\n");
    ULONG ivar0 = 0;
    if (ReadRegister(h, I226_IVAR0, &ivar0)) {
        printf("   I226_IVAR0: 0x%08X\n", ivar0);
        
        // Analyze vector allocations (4 vectors per IVAR register)
        for (int vec = 0; vec < 4; vec++) {
            ULONG vec_field = (ivar0 >> (vec * 8)) & 0xFF;
            ULONG vec_num = vec_field & 0x07;
            BOOL valid = (vec_field & 0x80) != 0;
            
            printf("     Vector %d: MSI-X %lu %s\n", vec, vec_num, 
                   valid ? "? Valid" : "? Invalid");
        }
    }
    
    // Step 3: Test general purpose interrupt enable
    printf("?? Step 3: General purpose interrupt configuration...\n");
    ULONG gpie = 0;
    if (ReadRegister(h, I226_GPIE, &gpie)) {
        printf("   I226_GPIE: 0x%08X", gpie);
        
        // Analyze GPIE bits
        if (gpie & 0x00000001) printf(" (? Multiple MSI-X)");
        if (gpie & 0x00000002) printf(" (? Auto-mask)");
        if (gpie & 0x00000010) printf(" (? Extended descriptors)");
        printf("\n");
    }
    
    // Step 4: Test interrupt throttle programming
    printf("?? Step 4: Testing interrupt throttle programming...\n");
    
    // Program EITR0 with a test value
    ULONG test_interval = 100;  // 100 * 256ns = 25.6?s
    ULONG test_eitr = test_interval | (0 << 16); // Interval with zero counter
    
    if (WriteRegister(h, I226_EITR0, test_eitr)) {
        printf("   ? EITR0 programmed with test value: 0x%08X\n", test_eitr);
        
        // Read back to verify
        ULONG readback = 0;
        if (ReadRegister(h, I226_EITR0, &readback)) {
            printf("   ?? EITR0 readback: 0x%08X", readback);
            if ((readback & 0xFFFF) == test_interval) {
                printf(" (? Programming successful)\n");
            } else {
                printf(" (? Programming failed)\n");
            }
        }
    } else {
        printf("   ? Failed to program EITR0\n");
    }
}

/**
 * @brief Test advanced queue management for TSN
 * Tests I226's queue priority and traffic class configuration
 */
static void TestI226AdvancedQueues(HANDLE h) {
    printf("\n?? === I226 ADVANCED QUEUE MANAGEMENT TEST ===\n");
    printf("Testing I226 advanced queue features for TSN traffic classes\n");
    
    if (!SelectI226Context(h)) return;
    
    // Step 1: Read current queue configuration
    printf("?? Step 1: Current queue configuration analysis...\n");
    
    // Test queue control registers (I226-specific)
    ULONG queue_regs[] = {
        0x02800, // Queue 0 TX control
        0x02804, // Queue 1 TX control  
        0x02808, // Queue 2 TX control
        0x0280C  // Queue 3 TX control
    };
    
    for (int i = 0; i < 4; i++) {
        ULONG queue_ctrl = 0;
        if (ReadRegister(h, queue_regs[i], &queue_ctrl)) {
            printf("   Queue %d TX_CTRL (0x%05X): 0x%08X", i, queue_regs[i], queue_ctrl);
            
            // Analyze queue settings
            if (queue_ctrl & 0x00000001) printf(" (? Enabled)");
            else printf(" (? Disabled)");
            
            ULONG priority = (queue_ctrl >> 4) & 0x07;
            printf(" (Priority: %lu)", priority);
            printf("\n");
        }
    }
    
    // Step 2: Test traffic class mapping
    printf("??? Step 2: Traffic class mapping test...\n");
    
    // Read traffic class configuration (I226-specific register)
    ULONG tc_config = 0;
    if (ReadRegister(h, 0x05800, &tc_config)) { // Approximate TC config register
        printf("   Traffic Class Config: 0x%08X\n", tc_config);
        
        // Analyze traffic class mappings
        for (int tc = 0; tc < 8; tc++) {
            ULONG queue_mapping = (tc_config >> (tc * 4)) & 0x0F;
            printf("     TC%d -> Queue %lu\n", tc, queue_mapping);
        }
    }
    
    // Step 3: Test queue priority programming
    printf("?? Step 3: Queue priority programming test...\n");
    
    // Program test priority values for TSN
    ULONG test_priorities[] = {7, 6, 5, 4}; // High to low priority
    
    for (int i = 0; i < 4; i++) {
        ULONG current_ctrl = 0;
        if (ReadRegister(h, queue_regs[i], &current_ctrl)) {
            // Modify priority field (bits 6:4)
            ULONG new_ctrl = (current_ctrl & ~0x00000070) | ((test_priorities[i] & 0x07) << 4);
            
            if (WriteRegister(h, queue_regs[i], new_ctrl)) {
                printf("   ? Queue %d priority set to %lu\n", i, test_priorities[i]);
                
                // Verify the change
                ULONG verify_ctrl = 0;
                if (ReadRegister(h, queue_regs[i], &verify_ctrl)) {
                    ULONG readback_priority = (verify_ctrl >> 4) & 0x07;
                    if (readback_priority == test_priorities[i]) {
                        printf("     ? Priority verified: %lu\n", readback_priority);
                    } else {
                        printf("     ? Priority mismatch: wrote %lu, read %lu\n", 
                               test_priorities[i], readback_priority);
                    }
                }
            } else {
                printf("   ? Failed to set Queue %d priority\n", i);
            }
        }
    }
}

/**
 * @brief Test TAS initialization sequence with proper prerequisites
 * Tests the complete I226 TAS setup that failed in basic testing
 */
static void TestI226ProperTASInitialization(HANDLE h) {
    printf("\n??? === I226 PROPER TAS INITIALIZATION TEST ===\n");
    printf("Testing complete TAS setup sequence with all prerequisites\n");
    
    if (!SelectI226Context(h)) return;
    
    // Step 1: Ensure PTP clock is running (prerequisite for TAS)
    printf("? Step 1: Verifying PTP clock prerequisite...\n");
    ULONG systiml1 = 0, systiml2 = 0;
    if (ReadRegister(h, 0x0B600, &systiml1)) { // I226_SYSTIML
        Sleep(10);
        if (ReadRegister(h, 0x0B600, &systiml2)) {
            if (systiml2 > systiml1) {
                printf("   ? PTP clock running (SYSTIM advancing)\n");
            } else {
                printf("   ? PTP clock not running - TAS will fail\n");
                return;
            }
        }
    }
    
    // Step 2: Configure base time (must be in future)
    printf("?? Step 2: Configuring TAS base time...\n");
    
    // Get current time and add 1 second for base time
    ULONG current_low = 0, current_high = 0;
    ReadRegister(h, 0x0B600, &current_low);  // SYSTIML
    ReadRegister(h, 0x0B604, &current_high); // SYSTIMH
    
    ULONGLONG current_time = ((ULONGLONG)current_high << 32) | current_low;
    ULONGLONG base_time = current_time + 1000000000ULL; // +1 second
    
    ULONG base_low = (ULONG)(base_time & 0xFFFFFFFF);
    ULONG base_high = (ULONG)((base_time >> 32) & 0xFFFFFFFF);
    
    if (WriteRegister(h, 0x08604, base_low) &&   // I226_TAS_CONFIG0
        WriteRegister(h, 0x08608, base_high)) {  // I226_TAS_CONFIG1
        printf("   ? TAS base time configured: 0x%08X%08X\n", base_high, base_low);
    } else {
        printf("   ? Failed to configure TAS base time\n");
        return;
    }
    
    // Step 3: Configure cycle time (required for TAS operation)
    printf("?? Step 3: Configuring TAS cycle time...\n");
    
    // Set a 1ms cycle time (1,000,000 ns)
    ULONG cycle_time = 1000000;
    if (WriteRegister(h, 0x0860C, cycle_time)) { // I226_TAS_CYCLE_TIME (if available)
        printf("   ? TAS cycle time set to %lu ns (1ms)\n", cycle_time);
    } else {
        printf("   ?? TAS cycle time register may not be available\n");
    }
    
    // Step 4: Program complete gate list
    printf("?? Step 4: Programming complete TAS gate list...\n");
    
    // Program a realistic gate list for TSN
    ULONG gate_entries[] = {
        0xFF000064, // All queues open for 100 cycles  
        0x01000064, // Only queue 0 (highest priority) for 100 cycles
        0xFF000064, // All queues open for 100 cycles
        0x0F000064, // Queues 0-3 for 100 cycles
        0x00000000, // End of list
        0x00000000,
        0x00000000,
        0x00000000
    };
    
    BOOL gate_success = TRUE;
    for (int i = 0; i < 8; i++) {
        ULONG gate_offset = 0x08610 + (i * 4); // I226_TAS_GATE_LIST base
        if (WriteRegister(h, gate_offset, gate_entries[i])) {
            if (gate_entries[i] != 0) {
                printf("   ? Gate[%d]: 0x%08X (state=0x%02X, duration=%lu)\n", 
                       i, gate_entries[i], 
                       (gate_entries[i] >> 24) & 0xFF,
                       gate_entries[i] & 0x00FFFFFF);
            }
        } else {
            printf("   ? Failed to program gate[%d]\n", i);
            gate_success = FALSE;
        }
    }
    
    if (!gate_success) {
        printf("   ? Gate list programming failed\n");
        return;
    }
    
    // Step 5: Enable TAS with all prerequisites met
    printf("?? Step 5: Enabling TAS with full configuration...\n");
    
    ULONG tas_ctrl = 0x00000001 |  // TAS Enable
                     0x00000002 |  // Gate list valid
                     0x00000004;   // Base time valid
    
    if (WriteRegister(h, 0x08600, tas_ctrl)) { // I226_TAS_CTRL
        printf("   ? TAS enable command sent\n");
        
        // Wait for hardware to process
        Sleep(100);
        
        // Verify TAS activation
        ULONG readback = 0;
        if (ReadRegister(h, 0x08600, &readback)) {
            printf("   ?? TAS_CTRL readback: 0x%08X", readback);
            
            if (readback & 0x00000001) {
                printf(" (? TAS SUCCESSFULLY ACTIVATED!)\n");
                printf("   ?? I226 Time-Aware Shaper is now operational\n");
                
                // Read TAS status if available
                ULONG tas_status = 0;
                if (ReadRegister(h, 0x08620, &tas_status)) { // Approximate TAS status register
                    printf("   ?? TAS Status: 0x%08X\n", tas_status);
                }
                
            } else {
                printf(" (? TAS activation still failed)\n");
                printf("   ?? Possible reasons:\n");
                printf("     - Base time not in future enough\n");
                printf("     - Cycle time too small\n");
                printf("     - Gate list format incorrect\n");
                printf("     - Hardware prerequisites not met\n");
            }
        }
    } else {
        printf("   ? TAS enable write failed\n");
    }
}

/**
 * @brief Comprehensive I226 advanced feature test
 */
static void TestI226ComprehensiveAdvanced(HANDLE h) {
    printf("\n?? === I226 COMPREHENSIVE ADVANCED FEATURE TEST ===\n");
    printf("Testing ALL I226 advanced features that weren't covered in basic testing\n");
    
    // Test all missing features
    TestI226EEE(h);
    TestI226AdvancedPTM(h);
    TestI226AdvancedMDIO(h);
    TestI2265GSpeed(h);
    TestI226InterruptManagement(h);
    TestI226AdvancedQueues(h);
    TestI226ProperTASInitialization(h);
    
    printf("\n?? === I226 ADVANCED TEST SUMMARY ===\n");
    printf("? Tested Features:\n");
    printf("   - EEE (Energy Efficient Ethernet) configuration\n");
    printf("   - PCIe PTM advanced timing measurement\n");
    printf("   - Advanced MDIO PHY management\n");
    printf("   - 2.5G speed detection and configuration\n");
    printf("   - Interrupt management (EITR, IVAR, GPIE)\n");
    printf("   - Advanced queue management for TSN\n");
    printf("   - Complete TAS initialization with prerequisites\n");
    
    printf("\n?? Expected Results:\n");
    printf("   - EEE should activate if link partner supports it\n");
    printf("   - PTM should provide high-precision timing\n");
    printf("   - MDIO should show PHY capabilities\n");
    printf("   - Speed should negotiate to 2.5G if supported\n");
    printf("   - Interrupts should be configurable\n");
    printf("   - TAS should activate with proper setup\n");
}

int main(int argc, char** argv) {
    printf("Intel I226 Advanced Feature Test Tool\n");
    printf("=====================================\n");
    printf("Tests I226 features not covered in basic testing\n\n");
    
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    // Initialize device
    DWORD br = 0;
    if (DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &br, NULL)) {
        printf("? Device initialization successful\n");
    } else {
        printf("? Device initialization failed: %lu\n", GetLastError());
    }
    
    if (argc < 2 || _stricmp(argv[1], "all") == 0) {
        TestI226ComprehensiveAdvanced(h);
    }
    else if (_stricmp(argv[1], "eee") == 0) {
        TestI226EEE(h);
    }
    else if (_stricmp(argv[1], "ptm") == 0) {
        TestI226AdvancedPTM(h);
    }
    else if (_stricmp(argv[1], "mdio") == 0) {
        TestI226AdvancedMDIO(h);
    }
    else if (_stricmp(argv[1], "speed") == 0) {
        TestI2265GSpeed(h);
    }
    else if (_stricmp(argv[1], "interrupts") == 0) {
        TestI226InterruptManagement(h);
    }
    else if (_stricmp(argv[1], "queues") == 0) {
        TestI226AdvancedQueues(h);
    }
    else if (_stricmp(argv[1], "tas-proper") == 0) {
        TestI226ProperTASInitialization(h);
    }
    else {
        printf("Available test modes:\n");
        printf("  all          - Test all advanced I226 features\n");
        printf("  eee          - Energy Efficient Ethernet\n");
        printf("  ptm          - Advanced PCIe PTM timing\n");
        printf("  mdio         - Advanced MDIO PHY management\n");
        printf("  speed        - 2.5G speed configuration\n");
        printf("  interrupts   - Interrupt management (EITR/IVAR)\n");
        printf("  queues       - Advanced queue management\n");
        printf("  tas-proper   - Complete TAS initialization\n");
        CloseHandle(h);
        return 2;
    }
    
    CloseHandle(h);
    return 0;
}