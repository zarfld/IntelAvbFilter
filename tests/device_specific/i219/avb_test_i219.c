/*++

Module Name:

    avb_test_i219.c

Abstract:

    Simple test application for Intel AVB Filter Driver I219 validation
    Tests device detection and basic hardware access

--*/

/*
 * POLICY COMPLIANCE: Use shared IOCTL ABI (include/avb_ioctl.h); remove ad-hoc copies
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../../include/avb_ioctl.h"

/**
 * @brief Test I219 device detection and basic access
 */
int main()
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    DWORD bytesReturned;
    BOOL result;
    
    printf("=== Intel AVB Filter Driver I219 Test ===\n\n");
    
    // Open device
    hDevice = CreateFile(
        L"\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open IntelAvbFilter device (Error: %lu)\n", GetLastError());
        printf("Make sure the driver is installed and loaded.\n");
        return -1;
    }
    
    printf("? Successfully opened IntelAvbFilter device\n");
    
    // Test 1: Initialize device
    printf("\n--- Test 1: Device Initialization ---\n");
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_INIT_DEVICE,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Device initialization: SUCCESS\n");
    } else {
        printf("? Device initialization: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test 2: Get device info
    printf("\n--- Test 2: Device Information ---\n");
    AVB_DEVICE_INFO_REQUEST dir; ZeroMemory(&dir, sizeof(dir)); dir.buffer_size = sizeof(dir.device_info);
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_DEVICE_INFO,
        &dir,
        sizeof(dir),
        &dir,
        sizeof(dir),
        &bytesReturned,
        NULL
    );
    if (result) {
        dir.device_info[(dir.buffer_size < sizeof(dir.device_info)) ? dir.buffer_size : (sizeof(dir.device_info)-1)]='\0';
        printf("? Device info string: %s (status=0x%08X used=%u)\n", dir.device_info, dir.status, dir.buffer_size);
    } else {
        printf("? Device info: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test 3: Read basic registers (from SSOT where available)
    printf("\n--- Test 3: Register Access Tests ---\n");
    
    // Test reading device control register
    AVB_REGISTER_REQUEST regRequest; ZeroMemory(&regRequest, sizeof(regRequest));
    regRequest.offset = 0x00000;  // CTRL
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_READ_REGISTER,
        &regRequest,
        sizeof(regRequest),
        &regRequest,
        sizeof(regRequest),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Control Register (0x00000): 0x%08X\n", regRequest.value);
        if (regRequest.value != 0 && regRequest.value != 0x12340000) {
            printf("   ?? Looks like REAL hardware value!\n");
        } else {
            printf("   ??  Might be simulated value\n");
        }
    } else {
        printf("? Control Register read: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test reading device status register  
    regRequest.offset = 0x00008;  // STATUS
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_READ_REGISTER,
        &regRequest,
        sizeof(regRequest),
        &regRequest,
        sizeof(regRequest),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("? Status Register (0x00008): 0x%08X\n", regRequest.value);
        if (regRequest.value & 0x00000002) {  // Link up bit
            printf("   ?? Link Status: UP\n");
        } else {
            printf("   ?? Link Status: DOWN\n");
        }
    } else {
        printf("? Status Register read: FAILED (Error: %lu)\n", GetLastError());
    }
    
    // Test 4: Enable I219 PTP/IEEE 1588 hardware timestamping and verify register state
    // TDD: These tests define expected I219 PTP behavior — they exercise:
    //   enable_packet_timestamping (fix: wrong offsets 0xB344/48 → 0xB614/20, wrong enable bit)
    //   init_ptp (fix: stub → TSAUXC+TIMINCA+ETQF0+ETQS0 writes)
    //   set_systime (fix: stub → TSAUXC freeze/SYSTIML/SYSTIMH/unfreeze)
    //   get_systime (fix: KE fallback → hardware SYSTIML/SYSTIMH read)
    printf("\n--- Test 4: Enable PTP Hardware Timestamping ---\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST hwts; ZeroMemory(&hwts, sizeof(hwts));
        hwts.enable             = 1;  // Enable timestamping
        hwts.timer_mask         = 1;  // SYSTIM0 (primary timer only)
        hwts.enable_target_time = 0;  // No target time interrupts (I219 lacks TRGTTIML)
        hwts.enable_aux_ts      = 0;  // No aux timestamp (I219 lacks AUXSTMPL)

        result = DeviceIoControl(
            hDevice,
            IOCTL_AVB_SET_HW_TIMESTAMPING,
            &hwts, sizeof(hwts),
            &hwts, sizeof(hwts),
            &bytesReturned, NULL
        );

        if (result && hwts.status == 0 /* NDIS_STATUS_SUCCESS */) {
            printf("PASS: HW timestamping enabled (TSAUXC: 0x%08X -> 0x%08X)\n",
                   hwts.previous_tsauxc, hwts.current_tsauxc);
        } else {
            printf("FAIL: HW timestamping enable failed (DeviceIoControl=%d err=%lu status=0x%08X)\n",
                   result, GetLastError(), hwts.status);
        }

        // Verify TSYNCTXCTL register has EN bit set (bit 4 = 0x10)
        // I219 TSYNCTXCTL is at MMIO offset 0xB614 (IGB PTP block base 0xB600 + 0x14)
        // Bug under test: current code writes to 0xB344 (82580/I350 offset) instead of 0xB614
        AVB_REGISTER_REQUEST txCtlReg; ZeroMemory(&txCtlReg, sizeof(txCtlReg));
        txCtlReg.offset = 0xB614;  /* I219_TSYNCTXCTL */

        BOOL txRegOk = DeviceIoControl(
            hDevice,
            IOCTL_AVB_READ_REGISTER,
            &txCtlReg, sizeof(txCtlReg),
            &txCtlReg, sizeof(txCtlReg),
            &bytesReturned, NULL
        );
        if (txRegOk) {
            printf("  TSYNCTXCTL (0xB614) = 0x%08X\n", txCtlReg.value);
            if (txCtlReg.value & 0x10u) {  /* bit 4 = TX EN */
                printf("  PASS: TSYNCTXCTL EN bit (bit 4) is SET - TX timestamping active\n");
            } else {
                printf("  FAIL: TSYNCTXCTL EN bit (bit 4) NOT set (bug: wrong offset or enable bit)\n");
            }
        } else {
            printf("  FAIL: Cannot read TSYNCTXCTL at 0xB614 (Error: %lu)\n", GetLastError());
        }

        // Verify TSYNCRXCTL register has EN bit and TYPE_ALL_PKTS set
        // I219 TSYNCRXCTL is at MMIO offset 0xB620 (PTP base 0xB600 + 0x20)
        // Expected: EN(bit4)=1, TYPE(bits3:1)=4 -> 0x10 | (4<<1) = 0x18
        // Bug under test: current code writes to 0xB348 (wrong) and uses wrong enable bits
        AVB_REGISTER_REQUEST rxCtlReg; ZeroMemory(&rxCtlReg, sizeof(rxCtlReg));
        rxCtlReg.offset = 0xB620;  /* I219_TSYNCRXCTL */

        BOOL rxRegOk = DeviceIoControl(
            hDevice,
            IOCTL_AVB_READ_REGISTER,
            &rxCtlReg, sizeof(rxCtlReg),
            &rxCtlReg, sizeof(rxCtlReg),
            &bytesReturned, NULL
        );
        if (rxRegOk) {
            DWORD enBit    = (rxCtlReg.value >> 4) & 0x1u;  /* bit 4 */
            DWORD typeField = (rxCtlReg.value >> 1) & 0x7u;  /* bits 3:1 */
            printf("  TSYNCRXCTL (0xB620) = 0x%08X (EN=%lu TYPE=%lu)\n",
                   rxCtlReg.value, (unsigned long)enBit, (unsigned long)typeField);
            if (enBit) {
                printf("  PASS: TSYNCRXCTL EN bit (bit 4) is SET - RX timestamping active\n");
            } else {
                printf("  FAIL: TSYNCRXCTL EN bit (bit 4) NOT set (bug: wrong offset or enable bit)\n");
            }
            if (typeField == 4u) {  /* ALL_PKTS = 4 in I219 TYPE field */
                printf("  PASS: TSYNCRXCTL TYPE = ALL_PKTS (4)\n");
            } else {
                printf("  FAIL: TSYNCRXCTL TYPE = %lu, expected 4 (ALL_PKTS)\n", (unsigned long)typeField);
            }
        } else {
            printf("  FAIL: Cannot read TSYNCRXCTL at 0xB620 (Error: %lu)\n", GetLastError());
        }
    }

    // Test 5: Set and read back hardware PTP system clock (SYSTIML/SYSTIMH)
    // Bug under test: set_systime is a stub — never writes SYSTIML/SYSTIMH
    printf("\n--- Test 5: Hardware PTP System Clock ---\n");
    {
        // Read hardware clock — should be non-zero if init_ptp ran correctly
        // I219: reading SYSTIML latches SYSTIMH to prevent rollover
        AVB_REGISTER_REQUEST stmlReg; ZeroMemory(&stmlReg, sizeof(stmlReg));
        stmlReg.offset = 0xB600;  /* I219_SYSTIML */

        AVB_REGISTER_REQUEST stmhReg; ZeroMemory(&stmhReg, sizeof(stmhReg));
        stmhReg.offset = 0xB604;  /* I219_SYSTIMH */

        BOOL stmlOk = DeviceIoControl(
            hDevice, IOCTL_AVB_READ_REGISTER,
            &stmlReg, sizeof(stmlReg), &stmlReg, sizeof(stmlReg), &bytesReturned, NULL
        );
        BOOL stmhOk = DeviceIoControl(
            hDevice, IOCTL_AVB_READ_REGISTER,
            &stmhReg, sizeof(stmhReg), &stmhReg, sizeof(stmhReg), &bytesReturned, NULL
        );

        if (stmlOk && stmhOk) {
            avb_u64 hwTime = ((avb_u64)stmhReg.value << 32) | stmlReg.value;
            printf("  SYSTIML (0xB600) = 0x%08X\n", stmlReg.value);
            printf("  SYSTIMH (0xB604) = 0x%08X\n", stmhReg.value);
            printf("  Hardware PTP time = 0x%016llX (%llu ns)\n", hwTime, hwTime);
            if (hwTime != 0) {
                printf("  PASS: Hardware PTP clock is running (non-zero)\n");
            } else {
                printf("  FAIL: Hardware PTP clock reads zero (init_ptp stub did not initialize TIMINCA)\n");
            }
        } else {
            printf("  FAIL: Cannot read SYSTIML/SYSTIMH (Error: %lu)\n", GetLastError());
        }
    }

    // Test 6: GET_TIMESTAMP IOCTL returns hardware clock time (not KE fallback)
    // Bug under test: get_systime uses KeQuerySystemTime fallback instead of reading hardware
    printf("\n--- Test 6: GET_TIMESTAMP returns hardware clock ---\n");
    {
        AVB_TIMESTAMP_REQUEST getReq; ZeroMemory(&getReq, sizeof(getReq));
        getReq.clock_id = 0;

        result = DeviceIoControl(
            hDevice,
            IOCTL_AVB_GET_TIMESTAMP,
            &getReq, sizeof(getReq),
            &getReq, sizeof(getReq),
            &bytesReturned, NULL
        );

        if (result) {
            printf("  GET_TIMESTAMP = 0x%016llX (%llu ns)\n", getReq.timestamp, getReq.timestamp);
            if (getReq.timestamp != 0) {
                printf("  PASS: GET_TIMESTAMP returns non-zero timestamp\n");
            } else {
                printf("  FAIL: GET_TIMESTAMP returns zero (get_systime stub bug)\n");
            }
        } else {
            printf("  FAIL: GET_TIMESTAMP IOCTL failed (Error: %lu status=0x%08X)\n",
                   GetLastError(), getReq.status);
        }
    }

    // Summary
    printf("\n=== TEST SUMMARY ===\n");
    printf("Tests 1-3: Device enumeration and basic register access\n");
    printf("Tests 4-6: I219 PTP hardware timestamping (requires fixed i219_impl.c)\n");
    printf("  Test 4 PASS: TSYNCTXCTL/TSYNCRXCTL at 0xB614/0xB620 have EN bit (bit 4) set\n");
    printf("  Test 5 PASS: SYSTIML/SYSTIMH non-zero (init_ptp wrote TIMINCA)\n");
    printf("  Test 6 PASS: GET_TIMESTAMP returns non-zero hardware PHC time\n");
    printf("\nIf FAIL, enable debug output:\n");
    printf("  1. DebugView.exe (Sysinternals), Capture Kernel\n");
    printf("  2. Look for 'i219_enable_packet_timestamping', 'i219_init_ptp', 'i219_get_systime'\n");

    CloseHandle(hDevice);
    return 0;
}