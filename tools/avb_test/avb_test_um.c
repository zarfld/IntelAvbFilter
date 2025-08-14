// User-mode AVB/TSN test tool implementation
// Build only via: nmake -f tools\\avb_test\\avb_test.mak
// This file must NOT be compiled as part of the kernel driver project.

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "filteruser.h" // for _NDIS_CONTROL_CODE macro matching the driver

// IOCTLs (must exactly match avb_integration.h)
#define IOCTL_AVB_INIT_DEVICE           _NDIS_CONTROL_CODE(20, METHOD_BUFFERED)
#define IOCTL_AVB_GET_DEVICE_INFO       _NDIS_CONTROL_CODE(21, METHOD_BUFFERED)
#define IOCTL_AVB_READ_REGISTER         _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_WRITE_REGISTER        _NDIS_CONTROL_CODE(23, METHOD_BUFFERED)
#define IOCTL_AVB_GET_TIMESTAMP         _NDIS_CONTROL_CODE(24, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TIMESTAMP         _NDIS_CONTROL_CODE(25, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_TAS             _NDIS_CONTROL_CODE(26, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_FP              _NDIS_CONTROL_CODE(27, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_PTM             _NDIS_CONTROL_CODE(28, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_READ             _NDIS_CONTROL_CODE(29, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_WRITE            _NDIS_CONTROL_CODE(30, METHOD_BUFFERED)

#define LINKNAME "\\\\.\\IntelAvbFilter"

#pragma pack(push,1)
// Structs mirrored from avb_integration.h (user-mode equivalents)
typedef struct _AVB_DEVICE_INFO_REQUEST {
    char device_info[1024];
    unsigned long buffer_size;
    unsigned long status; // NDIS_STATUS compatible
} AVB_DEVICE_INFO_REQUEST;

typedef struct _AVB_REGISTER_REQUEST {
    unsigned long offset;
    unsigned long value;
    unsigned long status; // NDIS_STATUS compatible
} AVB_REGISTER_REQUEST;

typedef struct _AVB_TIMESTAMP_REQUEST {
    unsigned long long timestamp;
    int clock_id;
    unsigned long status;
} AVB_TIMESTAMP_REQUEST;

// Nested config structs as in avb_integration.h
struct tsn_tas_config_um {
    unsigned long long base_time_s;
    unsigned long base_time_ns;
    unsigned long cycle_time_s;
    unsigned long cycle_time_ns;
    unsigned char gate_states[8];
    unsigned long gate_durations[8];
};

struct tsn_fp_config_um {
    unsigned char preemptable_queues;
    unsigned long min_fragment_size;
    unsigned char verify_disable;
};

struct ptm_config_um {
    unsigned char enabled;
    unsigned long clock_granularity;
};

typedef struct _AVB_TAS_REQUEST {
    struct tsn_tas_config_um config;
    unsigned long status;
} AVB_TAS_REQUEST;

typedef struct _AVB_FP_REQUEST {
    struct tsn_fp_config_um config;
    unsigned long status;
} AVB_FP_REQUEST;

typedef struct _AVB_PTM_REQUEST {
    struct ptm_config_um config;
    unsigned long status;
} AVB_PTM_REQUEST;

typedef struct _AVB_MDIO_REQUEST {
    unsigned long page;
    unsigned long reg;
    unsigned short value;
    unsigned long status;
} AVB_MDIO_REQUEST;
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
static void test_device_info(HANDLE h){ AVB_DEVICE_INFO_REQUEST r; ZeroMemory(&r,sizeof(r)); r.buffer_size=sizeof(r.device_info); if(Ioctl(h,IOCTL_AVB_GET_DEVICE_INFO,&r,sizeof(r),&r,sizeof(r))) printf("Device: %s (0x%lx)\n", r.device_info, r.status); }
static void reg_read(HANDLE h, unsigned long off){ AVB_REGISTER_REQUEST r; ZeroMemory(&r,sizeof(r)); r.offset=off; if(Ioctl(h,IOCTL_AVB_READ_REGISTER,&r,sizeof(r),&r,sizeof(r))) printf("MMIO[0x%08lX]=0x%08lX (0x%lx)\n", r.offset, r.value, r.status); }
static void reg_write(HANDLE h, unsigned long off, unsigned long val){ AVB_REGISTER_REQUEST r; ZeroMemory(&r,sizeof(r)); r.offset=off; r.value=val; Ioctl(h,IOCTL_AVB_WRITE_REGISTER,&r,sizeof(r),&r,sizeof(r)); }
static void ts_get(HANDLE h){ AVB_TIMESTAMP_REQUEST t; ZeroMemory(&t,sizeof(t)); if(Ioctl(h,IOCTL_AVB_GET_TIMESTAMP,&t,sizeof(t),&t,sizeof(t))) printf("TS=0x%016llX (0x%lx)\n", (unsigned long long)t.timestamp, t.status); }
static void ts_set_now(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long ts=(unsigned long long)now.QuadPart*100ULL; AVB_TIMESTAMP_REQUEST t; ZeroMemory(&t,sizeof(t)); t.timestamp=ts; Ioctl(h,IOCTL_AVB_SET_TIMESTAMP,&t,sizeof(t),&t,sizeof(t)); }
static void tas_audio(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long start=(unsigned long long)now.QuadPart*100ULL+1000000000ULL; AVB_TAS_REQUEST q; ZeroMemory(&q,sizeof(q)); q.config.base_time_s=start/1000000000ULL; q.config.base_time_ns=(unsigned long)(start%1000000000ULL); q.config.cycle_time_s=0; q.config.cycle_time_ns=125000; q.config.gate_states[0]=0x01; q.config.gate_durations[0]=62500; q.config.gate_states[1]=0x00; q.config.gate_durations[1]=62500; Ioctl(h,IOCTL_AVB_SETUP_TAS,&q,sizeof(q),&q,sizeof(q)); }
static void fp_on(HANDLE h){ AVB_FP_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.preemptable_queues=0x01; r.config.min_fragment_size=128; r.config.verify_disable=0; Ioctl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r)); }
static void fp_off(HANDLE h){ AVB_FP_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.preemptable_queues=0; r.config.verify_disable=1; Ioctl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r)); }
static void ptm_on(HANDLE h){ AVB_PTM_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.enabled=1; r.config.clock_granularity=16; Ioctl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r)); }
static void ptm_off(HANDLE h){ AVB_PTM_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.enabled=0; Ioctl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r)); }
static void mdio_read(HANDLE h){ AVB_MDIO_REQUEST m; ZeroMemory(&m,sizeof(m)); m.page=0; m.reg=1; if(Ioctl(h,IOCTL_AVB_MDIO_READ,&m,sizeof(m),&m,sizeof(m))) printf("MDIO[0,1]=0x%04X (0x%lx)\n", m.value, m.status); }

static void usage(const char* e){
    printf("Usage: %s [all|info|reg-read <hexOff>|reg-write <hexOff> <hexVal>|ts-get|ts-set-now|tas-audio|fp-on|fp-off|ptm-on|ptm-off|mdio]\n", e);
}

int main(int argc, char** argv){
    HANDLE h=OpenDev(); if(h==INVALID_HANDLE_VALUE) return 1; test_init(h);
    if(argc<2 || _stricmp(argv[1],"all")==0){ test_device_info(h); reg_read(h,0x0B600); ts_get(h); tas_audio(h); fp_on(h); ptm_on(h); mdio_read(h); CloseHandle(h); return 0; }
    if(_stricmp(argv[1],"info")==0){ test_device_info(h); }
    else if(_stricmp(argv[1],"reg-read")==0 && argc>=3){ reg_read(h,(unsigned long)strtoul(argv[2],NULL,16)); }
    else if(_stricmp(argv[1],"reg-write")==0 && argc>=4){ reg_write(h,(unsigned long)strtoul(argv[2],NULL,16),(unsigned long)strtoul(argv[3],NULL,16)); }
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
