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
#include "intel-ethernet-regs/gen/i210_regs.h"  /* SSOT register definitions */

#define LINKNAME "\\\\.\\IntelAvbFilter"

// Legacy local aliases kept for backward compat (prefer SSOT names)
#define REG_CTRL        I210_CTRL
#define REG_STATUS      I210_STATUS
#define REG_SYSTIML     I210_SYSTIML
#define REG_SYSTIMH     I210_SYSTIMH
#define REG_TIMINCA     I210_TIMINCA
#define REG_TSYNCTXCTL  I210_TSYNCTXCTL
#define REG_TXSTMPL     I210_TXSTMPL
#define REG_TXSTMPH     I210_TXSTMPH
#define REG_TSYNCRXCTL  I210_TSYNCRXCTL
#define REG_RXSTMPL     I210_RXSTMPL
#define REG_RXSTMPH     I210_RXSTMPH

/* Legacy minimal enable constant removed; use SSOT bit/mask */

static HANDLE OpenDev(void) {
    HANDLE h = CreateFileA(LINKNAME, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE) fprintf(stderr, "Open %s failed: %lu\n", LINKNAME, GetLastError());
    return h;
}

/* Generic helpers */
static int read_reg(HANDLE h, unsigned long off, unsigned long* val){ AVB_REGISTER_REQUEST r; ZeroMemory(&r,sizeof(r)); r.offset=off; DWORD br=0; BOOL ok = DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL); if(!ok) return 0; *val=r.value; return 1; }
static void reg_read(HANDLE h, unsigned long off){ unsigned long v=0; if(read_reg(h,off,&v)) printf("MMIO[0x%08lX]=0x%08lX\n", off, v); else fprintf(stderr, "Read 0x%lX failed (GLE=%lu)\n", off, GetLastError()); }

/* Write with verification */
static int reg_write_checked(HANDLE h, unsigned long off, unsigned long val, const char* tag){
    AVB_REGISTER_REQUEST r; ZeroMemory(&r,sizeof(r)); r.offset=off; r.value=val; DWORD br=0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL);
    unsigned long rb=0; int rb_ok = read_reg(h, off, &rb);
    if(!ok){ fprintf(stderr,"WRITE FAIL off=0x%05lX (%s) GLE=%lu\n", off, tag?tag:"", GetLastError()); return 0; }
    if(!rb_ok){ fprintf(stderr,"WRITE VERIFY READ FAIL off=0x%05lX (%s)\n", off, tag?tag:"" ); return 0; }
    if(rb!=val){ fprintf(stderr,"WRITE MISMATCH off=0x%05lX (%s) want=0x%08lX got=0x%08lX\n", off, tag?tag:"", val, rb); return 0; }
    return 1;
}
static void reg_write(HANDLE h, unsigned long off, unsigned long val){ reg_write_checked(h, off, val, NULL); }
static void test_init(HANDLE h){ DWORD br=0; DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL,0, NULL,0, &br, NULL); }
static void test_device_info(HANDLE h){ AVB_DEVICE_INFO_REQUEST r; ZeroMemory(&r,sizeof(r)); r.buffer_size=sizeof(r.device_info); DWORD br=0; if(DeviceIoControl(h,IOCTL_AVB_GET_DEVICE_INFO,&r,sizeof(r),&r,sizeof(r),&br,NULL)) printf("Device: %s (0x%lx)\n", r.device_info, r.status); }

/* Capability enumeration via IOCTL_AVB_ENUM_ADAPTERS */
static int enum_caps(HANDLE h, AVB_ENUM_REQUEST* out){ if(!out) return 0; ZeroMemory(out,sizeof(*out)); DWORD br=0; BOOL ok = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS, out, sizeof(*out), out, sizeof(*out), &br, NULL); return ok?1:0; }
static void print_caps(uint32_t caps){
    struct { uint32_t bit; const char* name; } map[] = {
        { INTEL_CAP_BASIC_1588,   "BASIC_1588" },
        { INTEL_CAP_ENHANCED_TS,  "ENHANCED_TS" },
        { INTEL_CAP_TSN_TAS,      "TSN_TAS" },
        { INTEL_CAP_TSN_FP,       "TSN_FP" },
        { INTEL_CAP_PCIe_PTM,     "PCIe_PTM" },
        { INTEL_CAP_2_5G,         "2_5G" },
        { INTEL_CAP_MDIO,         "MDIO" },
        { INTEL_CAP_MMIO,         "MMIO" },
    };
    printf("Capabilities (0x%08X):", caps);
    int printed=0; for(size_t i=0;i<sizeof(map)/sizeof(map[0]);++i){ if(caps & map[i].bit){ printf(" %s", map[i].name); printed=1; } }
    if(!printed) printf(" <none>");
    printf("\n");
}

/* Ensure PTP (SYSTIM) is running; if not, initialize minimal increment and enable RX/TX timestamp capture */
static void ptp_ensure_started(HANDLE h){
    unsigned long l1=0,h1=0; if(!read_reg(h,REG_SYSTIML,&l1) || !read_reg(h,REG_SYSTIMH,&h1)){ fprintf(stderr,"PTP: base read failed\n"); return; }
    Sleep(10);
    unsigned long l2=0,h2=0; int running = (read_reg(h,REG_SYSTIML,&l2) && read_reg(h,REG_SYSTIMH,&h2) && (l1!=l2 || h1!=h2));
    if(running){ printf("PTP: running (SYSTIM=0x%08lX%08lX)\n", h2, l2); return; }
    printf("PTP: not running, attempting start (legacy seq, then SSOT)...\n");
    int ok = 1;
    ok &= reg_write_checked(h, REG_SYSTIML, 0x00000000, "SYSTIML");
    ok &= reg_write_checked(h, REG_SYSTIMH, 0x00000000, "SYSTIMH");
    ok &= reg_write_checked(h, REG_TIMINCA, 0x00000001, "TIMINCA");
    /* First try with proper SSOT enable bits */
    unsigned long tx_en_val = (1U << I210_TSYNCTXCTL_EN_SHIFT) | (I210_TSYNCTXCTL_TYPE_ALL << I210_TSYNCTXCTL_TYPE_SHIFT);
    unsigned long rx_en_val = (1U << I210_TSYNCRXCTL_EN_SHIFT) | (I210_TSYNCRXCTL_TYPE_ALL << I210_TSYNCRXCTL_TYPE_SHIFT);
    ok &= reg_write_checked(h, REG_TSYNCRXCTL, rx_en_val, "TSYNCRXCTL(SSOT)");
    ok &= reg_write_checked(h, REG_TSYNCTXCTL, tx_en_val, "TSYNCTXCTL(SSOT)");
    if(!ok){ fprintf(stderr,"PTP: write sequence incomplete (writes blocked?)\n"); return; }
    Sleep(10);
    unsigned long l3=0,h3=0; read_reg(h,REG_SYSTIML,&l3); read_reg(h,REG_SYSTIMH,&h3);
    if(l3||h3){ printf("PTP: started (SYSTIM=0x%08lX%08lX)\n", h3, l3); }
    else fprintf(stderr,"PTP: start failed (SYSTIM still zero)\n");
}

static void ts_get(HANDLE h){ AVB_TIMESTAMP_REQUEST t; ZeroMemory(&t,sizeof(t)); DWORD br=0; BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &t, sizeof(t), &t, sizeof(t), &br, NULL); if (ok) { printf("TS(IOCTL)=0x%016llX\n", (unsigned long long)t.timestamp); return; } unsigned long hi=0, lo=0; if (read_reg(h, REG_SYSTIMH, &hi) && read_reg(h, REG_SYSTIML, &lo)) { unsigned long long ts = ((unsigned long long)hi<<32)|lo; printf("TS=0x%016llX\n", ts); } else { printf("TS=read-failed\n"); } }
static void ts_set_now(HANDLE h){ FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long ts=(unsigned long long)now.QuadPart*100ULL; AVB_TIMESTAMP_REQUEST t; ZeroMemory(&t,sizeof(t)); t.timestamp=ts; DWORD br=0; if (DeviceIoControl(h, IOCTL_AVB_SET_TIMESTAMP, &t, sizeof(t), &t, sizeof(t), &br, NULL)) printf("TS set (0x%lx)\n", t.status); else fprintf(stderr, "TS set failed (GLE=%lu)\n", GetLastError()); }

static void snapshot_i210_basic(HANDLE h){ unsigned long v; printf("\n--- Basic I210 register snapshot (legacy offsets) ---\n"); if (read_reg(h, REG_CTRL, &v))     printf("  CTRL(0x%05X)   = 0x%08lX\n", REG_CTRL, v); if (read_reg(h, REG_STATUS, &v))   printf("  STATUS(0x%05X) = 0x%08lX\n", REG_STATUS, v); if (read_reg(h, REG_SYSTIML, &v))  printf("  SYSTIML        = 0x%08lX\n", v); if (read_reg(h, REG_SYSTIMH, &v))  printf("  SYSTIMH        = 0x%08lX\n", v); if (read_reg(h, REG_TIMINCA, &v))  printf("  TIMINCA        = 0x%08lX\n", v); if (read_reg(h, REG_TSYNCRXCTL, &v)) printf("  TSYNCRXCTL      = 0x%08lX\n", v); if (read_reg(h, REG_TSYNCTXCTL, &v)) printf("  TSYNCTXCTL      = 0x%08lX\n", v); if (read_reg(h, REG_RXSTMPL, &v))  printf("  RXSTMPL         = 0x%08lX\n", v); if (read_reg(h, REG_RXSTMPH, &v))  printf("  RXSTMPH         = 0x%08lX\n", v); if (read_reg(h, REG_TXSTMPL, &v))  printf("  TXSTMPL         = 0x%08lX\n", v); if (read_reg(h, REG_TXSTMPH, &v))  printf("  TXSTMPH         = 0x%08lX\n", v); }

static void snapshot_i210_ssot(HANDLE h){
    unsigned long vtx=0, vrx=0; read_reg(h, I210_TSYNCTXCTL, &vtx); read_reg(h, I210_TSYNCRXCTL, &vrx);
    unsigned long tx_en = (unsigned long)I210_TSYNCTXCTL_GET(vtx, (unsigned long)I210_TSYNCTXCTL_EN_MASK, I210_TSYNCTXCTL_EN_SHIFT);
    unsigned long tx_type = (unsigned long)I210_TSYNCTXCTL_GET(vtx, (unsigned long)I210_TSYNCTXCTL_TYPE_MASK, I210_TSYNCTXCTL_TYPE_SHIFT);
    unsigned long rx_en = (unsigned long)I210_TSYNCRXCTL_GET(vrx, (unsigned long)I210_TSYNCRXCTL_EN_MASK, I210_TSYNCRXCTL_EN_SHIFT);
    unsigned long rx_type = (unsigned long)I210_TSYNCRXCTL_GET(vrx, (unsigned long)I210_TSYNCRXCTL_TYPE_MASK, I210_TSYNCRXCTL_TYPE_SHIFT);
    unsigned long rxl=0, rxh=0, txl=0, txh=0; read_reg(h,I210_RXSTMPL,&rxl); read_reg(h,I210_RXSTMPH,&rxh); read_reg(h,I210_TXSTMPL,&txl); read_reg(h,I210_TXSTMPH,&txh);
    printf("\n--- SSOT I210 PTP decode ---\n");
    printf("  TSYNCTXCTL raw=0x%08lX EN=%lu TYPE=%lu\n", vtx, tx_en, tx_type);
    printf("  TSYNCRXCTL raw=0x%08lX EN=%lu TYPE=%lu\n", vrx, rx_en, rx_type);
    printf("  RXSTMP = 0x%08lX%08lX  TXSTMP = 0x%08lX%08lX\n", rxh, rxl, txh, txl);
}

/* Explicit SSOT enable attempt command */
static void ptp_enable_ssot_cmd(HANDLE h){
    unsigned long val_before=0; read_reg(h, I210_TSYNCRXCTL, &val_before);
    unsigned long rx_en_val = (1U << I210_TSYNCRXCTL_EN_SHIFT) | (I210_TSYNCRXCTL_TYPE_ALL << I210_TSYNCRXCTL_TYPE_SHIFT);
    unsigned long tx_en_val = (1U << I210_TSYNCTXCTL_EN_SHIFT) | (I210_TSYNCTXCTL_TYPE_ALL << I210_TSYNCTXCTL_TYPE_SHIFT);
    int ok_rx = reg_write_checked(h, I210_TSYNCRXCTL, rx_en_val, "TSYNCRXCTL(SSOT)");
    int ok_tx = reg_write_checked(h, I210_TSYNCTXCTL, tx_en_val, "TSYNCTXCTL(SSOT)");
    unsigned long vtx=0, vrx=0; read_reg(h, I210_TSYNCTXCTL, &vtx); read_reg(h, I210_TSYNCRXCTL, &vrx);
    printf("PTP SSOT enable attempt: rx_ok=%d tx_ok=%d new_rx=0x%08lX new_tx=0x%08lX\n", ok_rx, ok_tx, vrx, vtx);
    snapshot_i210_ssot(h);
}

/* Optional feature IOCTL wrappers (unchanged core logic, but return tri-state) */
#define AVB_OPT_OK        1
#define AVB_OPT_UNSUP     0
#define AVB_OPT_FAIL     -1

static int tas_audio(HANDLE h){ AVB_TAS_REQUEST q; ZeroMemory(&q,sizeof(q)); FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart=ft.dwLowDateTime; now.HighPart=ft.dwHighDateTime; unsigned long long start=((unsigned long long)now.QuadPart*100ULL)+1000000000ULL; q.config.base_time_s=(uint32_t)(start/1000000000ULL); q.config.base_time_ns=(uint32_t)(start%1000000000ULL); q.config.cycle_time_s=0; q.config.cycle_time_ns=125000; q.config.gate_states[0]=0x01; q.config.gate_durations[0]=62500; q.config.gate_states[1]=0x00; q.config.gate_durations[1]=62500; DWORD br=0; BOOL ok=DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, &q, sizeof(q), &q, sizeof(q), &br, NULL); if(ok){ printf("TAS OK (0x%lx)\n", q.status); return AVB_OPT_OK; } DWORD gle=GetLastError(); if (gle==ERROR_INVALID_FUNCTION) return AVB_OPT_UNSUP; fprintf(stderr,"TAS failed (GLE=%lu)\n", gle); return AVB_OPT_FAIL; }
static int fp_on(HANDLE h){ AVB_FP_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.preemptable_queues=0x01; r.config.min_fragment_size=128; r.config.verify_disable=0; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r),&br,NULL); if(ok){ printf("FP ON OK (0x%lx)\n", r.status); return AVB_OPT_OK;} DWORD gle=GetLastError(); if (gle==ERROR_INVALID_FUNCTION) return AVB_OPT_UNSUP; fprintf(stderr,"FP ON failed (GLE=%lu)\n", gle); return AVB_OPT_FAIL; }
static int fp_off(HANDLE h){ AVB_FP_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.preemptable_queues=0x00; r.config.verify_disable=1; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_FP,&r,sizeof(r),&r,sizeof(r),&br,NULL); if(ok){ printf("FP OFF OK (0x%lx)\n", r.status); return AVB_OPT_OK;} DWORD gle=GetLastError(); if (gle==ERROR_INVALID_FUNCTION) return AVB_OPT_UNSUP; fprintf(stderr,"FP OFF failed (GLE=%lu)\n", gle); return AVB_OPT_FAIL; }
static int ptm_on(HANDLE h){ AVB_PTM_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.enabled=1; r.config.clock_granularity=16; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r),&br,NULL); if(ok){ printf("PTM ON OK (0x%lx)\n", r.status); return AVB_OPT_OK;} DWORD gle=GetLastError(); if (gle==ERROR_INVALID_FUNCTION) return AVB_OPT_UNSUP; fprintf(stderr,"PTM ON failed (GLE=%lu)\n", gle); return AVB_OPT_FAIL; }
static int ptm_off(HANDLE h){ AVB_PTM_REQUEST r; ZeroMemory(&r,sizeof(r)); r.config.enabled=0; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_SETUP_PTM,&r,sizeof(r),&r,sizeof(r),&br,NULL); if(ok){ printf("PTM OFF OK (0x%lx)\n", r.status); return AVB_OPT_OK;} DWORD gle=GetLastError(); if (gle==ERROR_INVALID_FUNCTION) return AVB_OPT_UNSUP; fprintf(stderr,"PTM OFF failed (GLE=%lu)\n", gle); return AVB_OPT_FAIL; }
static int mdio_read_cmd(HANDLE h){ AVB_MDIO_REQUEST m; ZeroMemory(&m,sizeof(m)); m.page=0; m.reg=1; DWORD br=0; BOOL ok=DeviceIoControl(h,IOCTL_AVB_MDIO_READ,&m,sizeof(m),&m,sizeof(m),&br,NULL); if(ok){ printf("MDIO[0,1]=0x%04X (0x%lx)\n", m.value, m.status); return AVB_OPT_OK;} DWORD gle=GetLastError(); if (gle==ERROR_INVALID_FUNCTION) return AVB_OPT_UNSUP; fprintf(stderr,"MDIO failed (GLE=%lu)\n", gle); return AVB_OPT_FAIL; }

static void usage(const char* e){ printf("Usage: %s [selftest|snapshot|snapshot-ssot|ptp-enable-ssot|info|caps|ts-get|ts-set-now|reg-read <hexOff>|reg-write <hexOff> <hexVal>]\n", e); }

static int selftest(HANDLE h){ int base_ok=1; int optional_fail=0; int optional_used=0; AVB_ENUM_REQUEST er; if(enum_caps(h,&er)){ print_caps(er.capabilities); } else { printf("Capabilities: <enum failed GLE=%lu>\n", GetLastError()); er.capabilities=0; }
    ptp_ensure_started(h); /* ensure timer running */
    test_device_info(h); snapshot_i210_basic(h); snapshot_i210_ssot(h); ts_get(h);
    if(er.capabilities & INTEL_CAP_TSN_TAS){ optional_used++; int r=tas_audio(h); if(r==AVB_OPT_FAIL) optional_fail=1; } else printf("TAS: SKIPPED (unsupported)\n");
    if(er.capabilities & INTEL_CAP_TSN_FP){ optional_used++; int r1=fp_on(h); if(r1==AVB_OPT_FAIL) optional_fail=1; int r2=fp_off(h); if(r2==AVB_OPT_FAIL) optional_fail=1; } else { printf("FP: SKIPPED (unsupported)\n"); }
    if(er.capabilities & INTEL_CAP_PCIe_PTM){ optional_used++; int r=ptm_on(h); if(r==AVB_OPT_FAIL) optional_fail=1; r=ptm_off(h); if(r==AVB_OPT_FAIL) optional_fail=1; } else { printf("PTM: SKIPPED (unsupported)\n"); }
    if(er.capabilities & INTEL_CAP_MDIO){ optional_used++; int r=mdio_read_cmd(h); if(r==AVB_OPT_FAIL) optional_fail=1; } else { printf("MDIO: SKIPPED (unsupported)\n"); }
    printf("\nSummary: base=%s, optional=%s\n", base_ok?"OK":"FAIL", optional_fail?"FAIL":(optional_used?"OK":"NONE")); return base_ok?0:1; }

int main(int argc, char** argv){ HANDLE h=OpenDev(); if(h==INVALID_HANDLE_VALUE) return 1; test_init(h);
    if(argc<2 || _stricmp(argv[1],"selftest")==0){ int rc=selftest(h); CloseHandle(h); return rc; }
    if(_stricmp(argv[1],"snapshot")==0){ snapshot_i210_basic(h); }
    else if(_stricmp(argv[1],"snapshot-ssot")==0){ snapshot_i210_ssot(h); }
    else if(_stricmp(argv[1],"ptp-enable-ssot")==0){ ptp_enable_ssot_cmd(h); }
    else if(_stricmp(argv[1],"info")==0){ test_device_info(h); }
    else if(_stricmp(argv[1],"caps")==0){ AVB_ENUM_REQUEST er; if(enum_caps(h,&er)){ print_caps(er.capabilities); } else fprintf(stderr,"caps enum failed (GLE=%lu)\n", GetLastError()); }
    else if(_stricmp(argv[1],"ts-get")==0){ ts_get(h); }
    else if(_stricmp(argv[1],"ts-set-now")==0){ ts_set_now(h); }
    else if(_stricmp(argv[1],"reg-read")==0 && argc>=3){ reg_read(h,(unsigned long)strtoul(argv[2],NULL,16)); }
    else if(_stricmp(argv[1],"reg-write")==0 && argc>=4){ reg_write(h,(unsigned long)strtoul(argv[2],NULL,16),(unsigned long)strtoul(argv[3],NULL,16)); }
    else { usage(argv[0]); CloseHandle(h); return 2; }
    CloseHandle(h); return 0; }
