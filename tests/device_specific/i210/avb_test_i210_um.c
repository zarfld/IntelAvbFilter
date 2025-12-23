/* Re-create avb_test_i210_um.c after accidental truncation */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../../../include/avb_ioctl.h"

static int read_reg(HANDLE h, uint32_t off, uint32_t *val) {
    AVB_REGISTER_REQUEST req; ZeroMemory(&req, sizeof(req));
    DWORD br=0; BOOL ok; req.offset=off;
    ok = DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &req, sizeof(req), &req, sizeof(req), &br, NULL);
    if(!ok) return (int)GetLastError();
    *val = req.value; return 0;
}

int main(void){
    HANDLE h=CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){ printf("Open fail err=%lu\n", GetLastError()); return 1; }
    printf("Opened device.\n"); DWORD br=0; (void)DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE,NULL,0,NULL,0,&br,NULL);
    AVB_DEVICE_INFO_REQUEST di; ZeroMemory(&di,sizeof(di)); di.buffer_size=sizeof(di.device_info);
    if(DeviceIoControl(h, IOCTL_AVB_GET_DEVICE_INFO,&di,sizeof(di),&di,sizeof(di),&br,NULL)){
        di.device_info[(di.buffer_size<sizeof(di.device_info))?di.buffer_size:sizeof(di.device_info)-1]='\0';
        printf("Info: %s (status=0x%08X used=%u)\n", di.device_info, di.status, di.buffer_size);
    } else printf("Device info ioctl failed (err=%lu)\n", GetLastError());
    uint32_t v; if(read_reg(h,0x00000,&v)==0) printf("CTRL=0x%08X\n", v); if(read_reg(h,0x00008,&v)==0) printf("STATUS=0x%08X\n", v);
    AVB_TIMESTAMP_REQUEST ts; ZeroMemory(&ts,sizeof(ts)); if(DeviceIoControl(h,IOCTL_AVB_GET_TIMESTAMP,&ts,sizeof(ts),&ts,sizeof(ts),&br,NULL)) printf("TS=0x%016llX\n", (unsigned long long)ts.timestamp); else printf("TS ioctl fail (%lu)\n", GetLastError());
    CloseHandle(h); return 0; }
