// User-mode AVB/TSN test tool implementation
// Build only via: nmake -f tools\\avb_test\\avb_test.mak
// This file must NOT be compiled as part of the kernel driver project.

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// IOCTLs (must match driver)
#define IOCTL_AVB_INIT_DEVICE           CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 20, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_GET_DEVICE_INFO       CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_READ_REGISTER         CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 22, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_WRITE_REGISTER        CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 23, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_GET_TIMESTAMP         CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 24, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_SET_TIMESTAMP         CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 25, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_SETUP_TAS             CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 26, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_SETUP_FP              CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 27, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_SETUP_PTM             CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 28, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_MDIO_READ             CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 29, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_MDIO_WRITE            CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 30, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define LINKNAME "\\\\.\\IntelAvbFilter"

#pragma pack(push,1)
typedef struct { char device_info[1024]; uint32_t buffer_size; uint32_t status; } AVB_DEVICE_INFO_REQUEST;
typedef struct { uint32_t offset; uint32_t value; uint32_t status; } AVB_REGISTER_REQUEST;
typedef struct { uint64_t timestamp; int32_t clock_id; uint32_t status; } AVB_TIMESTAMP_REQUEST;
typedef struct { uint64_t base_time_s; uint32_t base_time_ns; uint32_t cycle_time_s; uint32_t cycle_time_ns; uint8_t gate_states[8]; uint32_t gate_durations[8]; uint32_t status; } AVB_TAS_REQUEST;
typedef struct { uint8_t preemptable_queues; uint32_t min_fragment_size; uint8_t verify_disable; uint8_t pad[2]; uint32_t status; } AVB_FP_REQUEST;
typedef struct { uint8_t enabled; uint32_t clock_granularity; uint32_t status; } AVB_PTM_REQUEST;
typedef struct { uint32_t page; uint32_t reg; uint16_t value; uint16_t pad; uint32_t status; } AVB_MDIO_REQUEST;
#pragma pack(pop)

static BOOL Ioctl(HANDLE h, DWORD code, void* inbuf, DWORD inlen, void* outbuf, DWORD outlen) {
    DWORD ret=0; BOOL ok = DeviceIoControl(h, code, inbuf, inlen, outbuf, outlen, &ret, NULL);
    if (!ok) fprintf(stderr, "DeviceIoControl 0x%lx failed: %lu\n", code, GetLastError());
    return ok;
}

static HANDLE OpenDev(void) {
    HANDLE h = CreateFileA(LINKNAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE) fprintf(stderr, "Open %s failed: %lu\n", LINKNAME, GetLastError());
    return h;
}

static void test_init(HANDLE h){ DWORD br=0; DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL,0, NULL,0, &br, NULL); }
static void test_device_info(HANDLE h){ AVB_DEVICE_INFO_REQUEST r={0}; r.buffer_size=sizeof(r.device_info); if(Ioctl(h,IOCTL_AVB_GET_DEVICE_INFO,&r,sizeof(r),&r,sizeof(r))) printf("Device: %s (0x%x)\n", r.device_info, r.status); }
static void reg_read(HANDLE h, uint32_t off){ AVB_REGISTER_REQUEST r={0}; r.offset=off; if(Ioctl(h,IOCTL_AVB_READ_REGISTER,&r,sizeof(r),&r,sizeof(r))) printf("MMIO[0x%08X]=0x%08X (0x%x)\n", r.offset, r.value, r.status); }
static void reg_write(HANDLE h, uint32_t off, uint32_t val){ AVB_REGISTER_REQUEST r={0}; r.offset=off; r.value=val; if(Ioctl(h,IOCTL_AVB_WRITE_REGISTER,&r,sizeof(r),&r,sizeof(r))) printf("MMIO[0x%08X]<=0x%08X (0x%x)\n", r.offset, r.value, r.status); }
static void ts_get(HANDLE h){ AVB_TIMESTAMP_REQUEST t={0}; if(Ioctl(h,IOCTL_AVB_GET_TIMESTAMP,&t,sizeof(t),&t,sizeof(t))) printf("TS=0x%016llX (0x%x)\n", (unsigned long long)t.timestamp, t.status); }
static void ts_set_now(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long ts=(unsigned long long)now.QuadPart*100ULL; AVB_TIMESTAMP_REQUEST t={0}; t.timestamp=ts; if(Ioctl(h,IOCTL_AVB_SET_TIMESTAMP,&t,sizeof(t),&t,sizeof(t))) printf("TS set (0x%x)\n", t.status); }
static void tas_audio(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long start=(unsigned long long)now.QuadPart*100ULL+1000000000ULL; AVB_TAS_REQUEST q={0}; q.base_time_s=start/1000000000ULL; q.base_time_ns=(uint32_t)(start%1000000000ULL); q.cycle_time_ns=125000; q.gate_states[0]=0x01; q.gate_durations[0]=62500; q.gate_states[1]=0x00; q.gate_durations[1]=62500; if(Ioctl(h,IOCTL_AVB_SETUP_TAS,&q,sizeof(q),&q,sizeof(q))) printf("TAS (0x%x)\n", q.status); }
static void fp_on(HANDLE h){ AVB_FP_REQUEST r={0}; r.preemptable_queues=0x01; r.min_fragment_size=128; if(Ioctl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r))) printf("FP ON (0x%x)\n", r.status); }
static void fp_off(HANDLE h){ AVB_FP_REQUEST r={0}; r.verify_disable=1; if(Ioctl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r))) printf("FP OFF (0x%x)\n", r.status); }
static void ptm_on(HANDLE h){ AVB_PTM_REQUEST r={0}; r.enabled=1; r.clock_granularity=16; if(Ioctl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r))) printf("PTM ON (0x%x)\n", r.status); }
static void ptm_off(HANDLE h){ AVB_PTM_REQUEST r={0}; r.enabled=0; if(Ioctl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r))) printf("PTM OFF (0x%x)\n", r.status); }
static void mdio_read(HANDLE h){ AVB_MDIO_REQUEST m={0}; m.page=0; m.reg=1; if(Ioctl(h,IOCTL_AVB_MDIO_READ,&m,sizeof(m),&m,sizeof(m))) printf("MDIO[0,1]=0x%04X (0x%x)\n", m.value, m.status); }

static void usage(const char* e){
    printf("Usage: %s [all|info|reg-read <hexOff>|reg-write <hexOff> <hexVal>|ts-get|ts-set-now|tas-audio|fp-on|fp-off|ptm-on|ptm-off|mdio]\n", e);
}

int main(int argc, char** argv){
    HANDLE h=OpenDev(); if(h==INVALID_HANDLE_VALUE) return 1; test_init(h);
    if(argc<2 || _stricmp(argv[1],"all")==0){ test_device_info(h); reg_read(h,0x0B600); ts_get(h); tas_audio(h); fp_on(h); ptm_on(h); mdio_read(h); CloseHandle(h); return 0; }
    if(_stricmp(argv[1],"info")==0){ test_device_info(h); }
    else if(_stricmp(argv[1],"reg-read")==0 && argc>=3){ reg_read(h,(uint32_t)strtoul(argv[2],NULL,16)); }
    else if(_stricmp(argv[1],"reg-write")==0 && argc>=4){ reg_write(h,(uint32_t)strtoul(argv[2],NULL,16),(uint32_t)strtoul(argv[3],NULL,16)); }
    else if(_stricmp(argv[1],"ts-get")==0){ ts_get(h); }
    else if(_stricmp(argv[1],"ts-set-now")==0){ ts_set_now(h); }
    else if(_stricmp(argv[1],"tas-audio")==0){ tas_audio(h); }
    else if(_stricmp(argv[1],"fp-on")==0){ fp_on(h); }
    else if(_stricmp(argv[1],"fp-off")==0){ fp_off(h); }
    else if(_stricmp(argv[1],"ptm-on")==0){ ptm_on(h); }
    else if(_stricmp(argv[1],"ptm-off")==0){ ptm_off(h); }
    else if(_stricmp(argv[1],"mdio")==0){ mdio_read(h); }
    else { usage(argv[0]); CloseHandle(h); return 2; }
    CloseHandle(h); return 0; 
}
