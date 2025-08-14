// User-mode AVB/TSN test tool implementation
// Build only via: nmake -f tools\\avb_test\\avb_test.mak

#ifdef _KERNEL_MODE
#error "tools/avb_test/avb_test_um.c must not be compiled in kernel-mode driver project"
#endif

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "include/avb_ioctl.h"

#define LINKNAME "\\\\.\\IntelAvbFilter"

// i210 register offsets
#define REG_CTRL            0x00000
#define REG_STATUS          0x00008
#define REG_SYSTIML         0x0B600
#define REG_SYSTIMH         0x0B604
#define REG_TSYNCTXCTL      0x0B614
#define REG_TXSTMPL         0x0B618
#define REG_TXSTMPH         0x0B61C
#define REG_TSYNCRXCTL      0x0B620
#define REG_RXSTMPL         0x0B624
#define REG_RXSTMPH         0x0B628

static HANDLE OpenDev(void) {
    HANDLE h = CreateFileA(LINKNAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE) fprintf(stderr, "Open %s failed: %lu\n", LINKNAME, GetLastError());
    return h;
}

static int read_reg(HANDLE h, unsigned long off, unsigned long* val){ AVB_REGISTER_REQUEST r; ZeroMemory(&r,sizeof(r)); r.offset=off; DWORD br=0; BOOL ok = DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL); if(!ok) return 0; *val=r.value; return 1; }
static void reg_read(HANDLE h, unsigned long off){ unsigned long v=0; if(read_reg(h,off,&v)) printf("MMIO[0x%08lX]=0x%08lX\n", off, v); else fprintf(stderr, "Read 0x%lX failed (GLE=%lu)\n", off, GetLastError()); }
static void reg_write(HANDLE h, unsigned long off, unsigned long val){ AVB_REGISTER_REQUEST r; ZeroMemory(&r,sizeof(r)); r.offset=off; r.value=val; DWORD br=0; DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL); }

static void test_init(HANDLE h){ DWORD br=0; DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL,0, NULL,0, &br, NULL); }
static void test_device_info(HANDLE h){ AVB_DEVICE_INFO_REQUEST r; ZeroMemory(&r,sizeof(r)); r.buffer_size=sizeof(r.device_info); DWORD br=0; if(DeviceIoControl(h,IOCTL_AVB_GET_DEVICE_INFO,&r,sizeof(r),&r,sizeof(r),&br,NULL)) printf("Device: %s (0x%lx)\n", r.device_info, r.status); }

static void ts_get(HANDLE h){ AVB_TIMESTAMP_REQUEST t; ZeroMemory(&t,sizeof(t)); DWORD br=0; BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &t, sizeof(t), &t, sizeof(t), &br, NULL); if (ok) { printf("TS(IOCTL)=0x%016llX\n", (unsigned long long)t.timestamp); return; } unsigned long hi=0, lo=0; if (read_reg(h, REG_SYSTIMH, &hi) && read_reg(h, REG_SYSTIML, &lo)) { unsigned long long ts = ((unsigned long long)hi<<32)|lo; printf("TS=0x%016llX\n", ts); } else { printf("TS=read-failed\n"); } }
static void ts_set_now(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long ts=(unsigned long long)now.QuadPart*100ULL; AVB_TIMESTAMP_REQUEST t; ZeroMemory(&t,sizeof(t)); t.timestamp=ts; DWORD br=0; if (DeviceIoControl(h, IOCTL_AVB_SET_TIMESTAMP, &t, sizeof(t), &t, sizeof(t), &br, NULL)) printf("TS set (0x%lx)\n", t.status); else fprintf(stderr, "TS set failed (GLE=%lu)\n", GetLastError()); }

static void snapshot_i210(HANDLE h){ unsigned long v; printf("\n--- Basic I210 register snapshot ---\n"); if (read_reg(h, REG_CTRL, &v))     printf("  CTRL(0x%05X)   = 0x%08lX\n", REG_CTRL, v); if (read_reg(h, REG_STATUS, &v))   printf("  STATUS(0x%05X) = 0x%08lX\n", REG_STATUS, v); if (read_reg(h, REG_SYSTIML, &v))  printf("  SYSTIML        = 0x%08lX\n", v); if (read_reg(h, REG_SYSTIMH, &v))  printf("  SYSTIMH        = 0x%08lX\n", v); if (read_reg(h, REG_TSYNCRXCTL, &v)) printf("  TSYNCRXCTL      = 0x%08lX\n", v); if (read_reg(h, REG_TSYNCTXCTL, &v)) printf("  TSYNCTXCTL      = 0x%08lX\n", v); if (read_reg(h, REG_RXSTMPL, &v))  printf("  RXSTMPL         = 0x%08lX\n", v); if (read_reg(h, REG_RXSTMPH, &v))  printf("  RXSTMPH         = 0x%08lX\n", v); if (read_reg(h, REG_TXSTMPL, &v))  printf("  TXSTMPL         = 0x%08lX\n", v); if (read_reg(h, REG_TXSTMPH, &v))  printf("  TXSTMPH         = 0x%08lX\n", v); }

static int tas_audio(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long start=((unsigned long long)now.QuadPart*100ULL)+1000000000ULL; AVB_TAS_REQUEST q; ZeroMemory(&q,sizeof(q)); q.config.base_time_s=start/1000000000ULL; q.config.base_time_ns=(unsigned long)(start%1000000000ULL); q.config.cycle_time_s=0; q.config.cycle_time_ns=125000; q.config.gate_states[0]=0x01; q.config.gate_durations[0]=62500; q.config.gate_states[1]=0x00; q.config.gate_durations[1]=62500; DWORD br=0; BOOL ok=DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, &q, sizeof(q), &q, sizeof(q), &br, NULL); if (ok) { printf("TAS OK (0x%lx)\n", q.status); return 1; } if (GetLastError()==ERROR_INVALID_FUNCTION){ printf("TAS: not supported\n"); return 0;} fprintf(stderr,"TAS failed (GLE=%lu)\n", GetLastError()); return 0; }
static int fp_on(HANDLE h){ AVB_FP_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.preemptable_queues=0x01; r.config.min_fragment_size=128; r.config.verify_disable=0; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r),&br,NULL); if (ok) { printf("FP ON OK (0x%lx)\n", r.status); return 1; } if (GetLastError()==ERROR_INVALID_FUNCTION){ printf("FP: not supported\n"); return 0;} fprintf(stderr,"FP ON failed (GLE=%lu)\n", GetLastError()); return 0; }
static int fp_off(HANDLE h){ AVB_FP_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.preemptable_queues=0x00; r.config.verify_disable=1; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r),&br,NULL); if (ok) { printf("FP OFF OK (0x%lx)\n", r.status); return 1; } if (GetLastError()==ERROR_INVALID_FUNCTION){ printf("FP: not supported\n"); return 0;} fprintf(stderr,"FP OFF failed (GLE=%lu)\n", GetLastError()); return 0; }
static int ptm_on(HANDLE h){ AVB_PTM_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.enabled=1; r.config.clock_granularity=16; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r),&br,NULL); if (ok) { printf("PTM ON OK (0x%lx)\n", r.status); return 1; } if (GetLastError()==ERROR_INVALID_FUNCTION){ printf("PTM: not supported\n"); return 0;} fprintf(stderr,"PTM ON failed (GLE=%lu)\n", GetLastError()); return 0; }
static int ptm_off(HANDLE h){ AVB_PTM_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.enabled=0; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r),&br,NULL); if (ok) { printf("PTM OFF OK (0x%lx)\n", r.status); return 1; } if (GetLastError()==ERROR_INVALID_FUNCTION){ printf("PTM: not supported\n"); return 0;} fprintf(stderr,"PTM OFF failed (GLE=%lu)\n", GetLastError()); return 0; }
static int mdio_read_cmd(HANDLE h){ AVB_MDIO_REQUEST m; ZeroMemory(&m,sizeof(m)); m.page=0; m.reg=1; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_MDIO_READ,&m,sizeof(m),&m,sizeof(m),&br,NULL); if (ok) { printf("MDIO[0,1]=0x%04X (0x%lx)\n", m.value, m.status); return 1; } if (GetLastError()==ERROR_INVALID_FUNCTION){ printf("MDIO: not supported\n"); return 0;} fprintf(stderr,"MDIO failed (GLE=%lu)\n", GetLastError()); return 0; }

static void usage(const char* e){ printf("Usage: %s [all|selftest|snapshot|info|ts-get|ts-set-now|reg-read <hexOff>|reg-write <hexOff> <hexVal>]\n", e); }

static int selftest(HANDLE h){ int base_ok=1; int optional_ok=1; test_device_info(h); snapshot_i210(h); ts_get(h); if (!tas_audio(h)) optional_ok=0; if (!fp_on(h)) optional_ok=0; if (!fp_off(h)) optional_ok=0; if (!ptm_on(h)) optional_ok=0; if (!ptm_off(h)) optional_ok=0; if (!mdio_read_cmd(h)) optional_ok=0; printf("\nSummary: base=%s, optional=%s\n", base_ok?"OK":"FAIL", optional_ok?"OK/Skipped":"FAIL"); return base_ok?0:1; }

int main(int argc, char** argv){ HANDLE h=OpenDev(); if(h==INVALID_HANDLE_VALUE) return 1; test_init(h);
    if(argc<2 || _stricmp(argv[1],"all")==0 || _stricmp(argv[1],"selftest")==0){ int rc=selftest(h); CloseHandle(h); return rc; }
    if(_stricmp(argv[1],"snapshot")==0){ snapshot_i210(h); }
    else if(_stricmp(argv[1],"info")==0){ test_device_info(h); }
    else if(_stricmp(argv[1],"ts-get")==0){ ts_get(h); }
    else if(_stricmp(argv[1],"ts-set-now")==0){ ts_set_now(h); }
    else if(_stricmp(argv[1],"reg-read")==0 && argc>=3){ reg_read(h,(unsigned long)strtoul(argv[2],NULL,16)); }
    else if(_stricmp(argv[1],"reg-write")==0 && argc>=4){ reg_write(h,(unsigned long)strtoul(argv[2],NULL,16),(unsigned long)strtoul(argv[3],NULL,16)); }
    else { usage(argv[0]); CloseHandle(h); return 2; }
    CloseHandle(h); return 0; }
