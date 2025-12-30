#include <windows.h>
#include <stdio.h>
#include "include/avb_ioctl.h"  // SSOT for IOCTL definitions and _NDIS_CONTROL_CODE macro

#define FILE_DEVICE_PHYSICAL_NETCARD 0x00000017

int main() {
    printf("IOCTL_AVB_ENUM_ADAPTERS (31): 0x%08X\n", _NDIS_CONTROL_CODE(31, METHOD_BUFFERED));
    printf("IOCTL_AVB_OPEN_ADAPTER (32): 0x%08X\n", _NDIS_CONTROL_CODE(32, METHOD_BUFFERED));
    printf("IOCTL_AVB_GET_CLOCK_CONFIG (39): 0x%08X\n", _NDIS_CONTROL_CODE(39, METHOD_BUFFERED));
    printf("IOCTL_AVB_READ_REGISTER (22): 0x%08X\n", _NDIS_CONTROL_CODE(22, METHOD_BUFFERED));
    return 0;
}
