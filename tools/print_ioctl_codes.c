/**
 * Print IOCTL codes to verify they match
 *
 * Implements REQ-NF-SSOT-001: Uses Single Source of Truth (include/avb_ioctl.h)
 */
#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"  // SSOT for IOCTL definitions

int main() {
    printf("IOCTL Code Verification:\n");
    printf("  FILE_DEVICE_PHYSICAL_NETCARD = 0x%X\n", FILE_DEVICE_PHYSICAL_NETCARD);
    printf("  METHOD_BUFFERED = %d\n", METHOD_BUFFERED);
    printf("  FILE_ANY_ACCESS = %d\n", FILE_ANY_ACCESS);
    printf("\n");
    printf("  IOCTL_AVB_READ_REGISTER (22) = 0x%08X\n", IOCTL_AVB_READ_REGISTER);
    printf("  IOCTL_AVB_GET_CLOCK_CONFIG (39) = 0x%08X\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    printf("\n");
    printf("Expected GET_CLOCK_CONFIG: 0x%08X\n", 
           ((FILE_DEVICE_PHYSICAL_NETCARD) << 16) | (39 << 2) | METHOD_BUFFERED);
    
    return 0;
}
