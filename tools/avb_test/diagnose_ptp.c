/**
 * @file diagnose_ptp.c
 * @brief Diagnose why PTP clock initialization is failing
 * 
 * This tool directly reads PTP registers to determine hardware state
 */

#include <windows.h>
#include <stdio.h>
#include "../../include/avb_ioctl.h"

#define REG_SYSTIML     0x0B600
#define REG_SYSTIMH     0x0B604
#define REG_TIMINCA     0x0B608
#define REG_TSAUXC      0x0B640
#define REG_CTRL        0x00000

static BOOL ReadReg(HANDLE h, ULONG offset, ULONG* value) {
    AVB_REGISTER_REQUEST req = {0};
    req.offset = offset;
    DWORD ret = 0;
    
    if (!DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &req, sizeof(req), &req, sizeof(req), &ret, NULL)) {
        printf("  ERROR: Read failed at 0x%05X (GLE=%lu)\n", offset, GetLastError());
        return FALSE;
    }
    *value = req.value;
    return TRUE;
}

static BOOL WriteReg(HANDLE h, ULONG offset, ULONG value) {
    AVB_REGISTER_REQUEST req = {0};
    req.offset = offset;
    req.value = value;
    DWORD ret = 0;
    
    if (!DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &req, sizeof(req), &req, sizeof(req), &ret, NULL)) {
        printf("  ERROR: Write failed at 0x%05X (GLE=%lu)\n", offset, GetLastError());
        return FALSE;
    }
    return TRUE;
}

int main() {
    printf("========================================\n");
    printf("PTP CLOCK DIAGNOSTIC TOOL\n");
    printf("========================================\n\n");
    
    HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open driver (GLE=%lu)\n", GetLastError());
        printf("Make sure driver is loaded and you're running as Administrator.\n");
        return 1;
    }
    printf("✓ Driver opened\n\n");
    
    // Read basic controller state
    ULONG ctrl = 0;
    printf("STEP 1: Check Controller Status\n");
    printf("--------------------------------\n");
    if (ReadReg(h, REG_CTRL, &ctrl)) {
        printf("CTRL (0x00000) = 0x%08X\n", ctrl);
        if (ctrl == 0) {
            printf("  ⚠ WARNING: CTRL is zero - hardware may not be accessible!\n");
        } else {
            printf("  ✓ Hardware is responding\n");
        }
    }
    printf("\n");
    
    // Read PTP clock registers BEFORE any initialization
    printf("STEP 2: Read PTP Registers (BEFORE init)\n");
    printf("-----------------------------------------\n");
    ULONG systiml_before = 0, systimh_before = 0, timinca_before = 0, tsauxc_before = 0;
    
    ReadReg(h, REG_SYSTIML, &systiml_before);
    ReadReg(h, REG_SYSTIMH, &systimh_before);
    ReadReg(h, REG_TIMINCA, &timinca_before);
    ReadReg(h, REG_TSAUXC, &tsauxc_before);
    
    printf("SYSTIML  (0x%05X) = 0x%08X\n", REG_SYSTIML, systiml_before);
    printf("SYSTIMH  (0x%05X) = 0x%08X\n", REG_SYSTIMH, systimh_before);
    printf("TIMINCA  (0x%05X) = 0x%08X\n", REG_TIMINCA, timinca_before);
    printf("TSAUXC   (0x%05X) = 0x%08X\n", REG_TSAUXC, tsauxc_before);
    
    if (timinca_before == 0) {
        printf("  ⚠ TIMINCA is zero - clock not configured!\n");
    }
    if (tsauxc_before & 0x80000000) {
        printf("  ⚠ TSAUXC bit 31 set - clock may be disabled!\n");
    }
    printf("\n");
    
    // Try to initialize the clock ourselves
    printf("STEP 3: Manual PTP Clock Initialization\n");
    printf("----------------------------------------\n");
    
    // Set TIMINCA
    if (timinca_before == 0) {
        printf("Setting TIMINCA to 0x18000000 (24ns/cycle for I226)...\n");
        if (WriteReg(h, REG_TIMINCA, 0x18000000)) {
            ULONG verify = 0;
            if (ReadReg(h, REG_TIMINCA, &verify) && verify == 0x18000000) {
                printf("  ✓ TIMINCA set successfully\n");
            } else {
                printf("  ⚠ TIMINCA verify failed (read back 0x%08X)\n", verify);
            }
        }
    } else {
        printf("TIMINCA already set (0x%08X), skipping\n", timinca_before);
    }
    
    // Write SYSTIM to start clock
    printf("\nWriting SYSTIM to 0x1000000000000000 to start clock...\n");
    if (WriteReg(h, REG_SYSTIMH, 0x10000000) && WriteReg(h, REG_SYSTIML, 0x00000000)) {
        printf("  ✓ SYSTIM write successful\n");
    }
    
    // Configure TSAUXC
    printf("\nConfiguring TSAUXC (enable clock)...\n");
    ULONG tsauxc_new = tsauxc_before;
    tsauxc_new &= ~(1U << 31);  // Clear disable bit
    tsauxc_new |= (1 << 2);      // Set enable bit
    if (WriteReg(h, REG_TSAUXC, tsauxc_new)) {
        printf("  ✓ TSAUXC written (0x%08X)\n", tsauxc_new);
    }
    
    printf("\n");
    
    // Read back and verify
    printf("STEP 4: Verify Clock is Running\n");
    printf("--------------------------------\n");
    
    Sleep(100);  // Wait 100ms
    
    ULONG systiml1 = 0, systimh1 = 0;
    ReadReg(h, REG_SYSTIML, &systiml1);
    ReadReg(h, REG_SYSTIMH, &systimh1);
    printf("SYSTIM after 100ms: 0x%08X%08X\n", systimh1, systiml1);
    
    Sleep(100);  // Wait another 100ms
    
    ULONG systiml2 = 0, systimh2 = 0;
    ReadReg(h, REG_SYSTIML, &systiml2);
    ReadReg(h, REG_SYSTIMH, &systimh2);
    printf("SYSTIM after 200ms: 0x%08X%08X\n", systimh2, systiml2);
    
    ULONGLONG time1 = ((ULONGLONG)systimh1 << 32) | systiml1;
    ULONGLONG time2 = ((ULONGLONG)systimh2 << 32) | systiml2;
    LONGLONG delta = (LONGLONG)(time2 - time1);
    
    printf("\nDelta: %lld ns (%.3f ms)\n", delta, delta / 1000000.0);
    
    if (delta > 0) {
        printf("  ✓ CLOCK IS RUNNING!\n");
        printf("  Rate: %.2f MHz\n", delta / 100000.0);  // delta over 100ms
        printf("\n");
        printf("DIAGNOSIS: Clock hardware is working!\n");
        printf("PROBLEM: Driver initialization is not being called or is failing.\n");
        printf("ACTION: Check DebugView for driver debug output.\n");
    } else if (delta == 0) {
        printf("  ⚠ CLOCK IS NOT INCREMENTING\n");
        printf("\n");
        printf("DIAGNOSIS: Hardware not responding to clock configuration.\n");
        printf("POSSIBLE CAUSES:\n");
        printf("  1. BAR0 mapping is incorrect\n");
        printf("  2. Register offsets are wrong for this device\n");
        printf("  3. Hardware requires different initialization sequence\n");
        printf("  4. PCI device not properly enabled\n");
    } else {
        printf("  ⚠ CLOCK GOING BACKWARDS?!\n");
        printf("\n");
        printf("DIAGNOSIS: Register read/write not working correctly.\n");
    }
    
    // Read final state
    printf("\n");
    printf("STEP 5: Final Register State\n");
    printf("-----------------------------\n");
    ULONG timinca_final = 0, tsauxc_final = 0;
    ReadReg(h, REG_TIMINCA, &timinca_final);
    ReadReg(h, REG_TSAUXC, &tsauxc_final);
    
    printf("SYSTIM:  0x%08X%08X\n", systimh2, systiml2);
    printf("TIMINCA: 0x%08X\n", timinca_final);
    printf("TSAUXC:  0x%08X\n", tsauxc_final);
    
    CloseHandle(h);
    
    printf("\n========================================\n");
    printf("Press Enter to exit...");
    getchar();
    
    return 0;
}
