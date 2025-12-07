/**
 * Verify that driver and test use same IOCTL code
 */
#include <windows.h>
#include <stdio.h>

// Define macro exactly as in driver
#define _NDIS_CONTROL_CODE(request, method) \
    CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, (request), (method), FILE_ANY_ACCESS)

#define IOCTL_AVB_READ_REGISTER _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_GET_CLOCK_CONFIG _NDIS_CONTROL_CODE(39, METHOD_BUFFERED)

int main() {
    printf("IOCTL Code Verification:\n");
    printf("  IOCTL_AVB_READ_REGISTER = 0x%08X\n", IOCTL_AVB_READ_REGISTER);
    printf("  IOCTL_AVB_GET_CLOCK_CONFIG = 0x%08X\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    printf("\nCalculation breakdown for GET_CLOCK_CONFIG:\n");
    printf("  FILE_DEVICE_PHYSICAL_NETCARD = 0x%04X\n", FILE_DEVICE_PHYSICAL_NETCARD);
    printf("  Request = 39 (0x%02X)\n", 39);
    printf("  METHOD_BUFFERED = %d\n", METHOD_BUFFERED);
    printf("  FILE_ANY_ACCESS = %d\n", FILE_ANY_ACCESS);
    
    DWORD calculated = ((DWORD)FILE_DEVICE_PHYSICAL_NETCARD << 16) | 
                      ((DWORD)39 << 2) | 
                      (DWORD)METHOD_BUFFERED | 
                      ((DWORD)FILE_ANY_ACCESS << 14);
    
    printf("  Calculated: 0x%08X\n", calculated);
    printf("  CTL_CODE result: 0x%08X\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    printf("  Match: %s\n", calculated == IOCTL_AVB_GET_CLOCK_CONFIG ? "YES" : "NO");
    
    return 0;
}
