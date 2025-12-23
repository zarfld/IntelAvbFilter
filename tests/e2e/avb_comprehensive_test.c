
#include <windows.h>
#include <stdio.h>

#include "avb_ioctl.h"

static HANDLE open_driver(void)
{
	HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
						   GENERIC_READ | GENERIC_WRITE,
						   0,
						   NULL,
						   OPEN_EXISTING,
						   FILE_ATTRIBUTE_NORMAL,
						   NULL);
	return h;
}

int main(void)
{
	printf("=== AVB Comprehensive E2E Test (minimal) ===\n");

	HANDLE h = open_driver();
	if (h == INVALID_HANDLE_VALUE) {
		printf("ERROR: Failed to open \\\\.\\IntelAvbFilter (GLE=%lu)\n", GetLastError());
		return 2;
	}

	DWORD bytesReturned = 0;

	printf("[1] IOCTL_AVB_INIT_DEVICE...\n");
	if (!DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
		printf("ERROR: IOCTL_AVB_INIT_DEVICE failed (GLE=%lu)\n", GetLastError());
		CloseHandle(h);
		return 3;
	}
	printf("OK: INIT_DEVICE\n");

	printf("[2] IOCTL_AVB_GET_DEVICE_INFO...\n");
	AVB_DEVICE_INFO_REQUEST info = {0};
	info.buffer_size = sizeof(info.device_info);
	bytesReturned = 0;
	if (!DeviceIoControl(h,
						 IOCTL_AVB_GET_DEVICE_INFO,
						 &info,
						 (DWORD)sizeof(info),
						 &info,
						 (DWORD)sizeof(info),
						 &bytesReturned,
						 NULL)) {
		printf("ERROR: IOCTL_AVB_GET_DEVICE_INFO failed (GLE=%lu)\n", GetLastError());
		CloseHandle(h);
		return 4;
	}
	if (info.buffer_size == 0) {
		printf("WARN: device_info buffer_size reported as 0\n");
	} else {
		printf("OK: device_info: %.*s\n", (int)(info.buffer_size - 1), info.device_info);
	}

	printf("[3] IOCTL_AVB_GET_HW_STATE...\n");
	AVB_HW_STATE_QUERY hw = {0};
	bytesReturned = 0;
	if (!DeviceIoControl(h,
						 IOCTL_AVB_GET_HW_STATE,
						 &hw,
						 (DWORD)sizeof(hw),
						 &hw,
						 (DWORD)sizeof(hw),
						 &bytesReturned,
						 NULL)) {
		printf("ERROR: IOCTL_AVB_GET_HW_STATE failed (GLE=%lu)\n", GetLastError());
		CloseHandle(h);
		return 5;
	}

	    printf("OK: HW_STATE=%u VID=0x%04X DID=0x%04X CAPS=0x%08X\n",
		   hw.hw_state,
		   hw.vendor_id,
		   hw.device_id,
		    hw.capabilities);

	CloseHandle(h);
	printf("PASS\n");
	return 0;
}
