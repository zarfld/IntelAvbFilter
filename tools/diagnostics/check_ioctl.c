#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

int main() {
    printf("IOCTL_AVB_GET_CLOCK_CONFIG = 0x%08X\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    printf("IOCTL_AVB_READ_REGISTER = 0x%08X\n", IOCTL_AVB_READ_REGISTER);
    printf("IOCTL_AVB_ADJUST_FREQUENCY = 0x%08X\n", IOCTL_AVB_ADJUST_FREQUENCY);
    return 0;
}
