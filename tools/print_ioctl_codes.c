/**
 * Print IOCTL codes to verify they match
 */
#include <windows.h>
#include <stdio.h>

#define _NDIS_CONTROL_CODE(Request,Method) \
        CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, (Request), (Method), FILE_ANY_ACCESS)

#define IOCTL_AVB_GET_CLOCK_CONFIG _NDIS_CONTROL_CODE(39, METHOD_BUFFERED)
#define IOCTL_AVB_READ_REGISTER _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)

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
