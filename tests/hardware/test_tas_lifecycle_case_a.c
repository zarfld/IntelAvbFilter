/**
 * @file test_tas_lifecycle_case_a.c
 * @brief #364 Cases A and B: one real FilterPause and FilterRestart on every eligible adapter, with no schedule or under SETUP_TAS
 *
 *   TC-CASEA-001  the lifecycle ran: Pause and Restart entered once on the target only, then traffic through the stack
 *   TC-CASEA-002  NDIS state events on this filter instance agree with it (an event alone proves nothing)
 *   TC-CASEA-003  restoration, from register, TX, PHC and consistency facts, never from the lifecycle result
 *
 * --case-b adds SETUP_TAS and held frames before the same trigger. No frame reaches the wire there, so TC-CASEB-002 to 004 repeat
 * the three above with the NDIS sequence opening the register reads and the ring tail standing in for frames on the wire:
 *
 *   TC-CASEB-001  setup: armed, frames accepted, none on the wire, observer alive. Otherwise FAILED_SETUP and no trigger
 *
 * --probe-restart-status, TC-CASEA-004: a restart request for no instance is refused as not found, and nothing restarts. Release too
 *
 * --case-b-drained holds about 24 frames, so the gauge is 0 at the trigger, and needs that as setup. It separates outstanding sends
 * from TSN mode as the reason run 1 of --case-b saw no Pause entry in 60 s, and otherwise the same test cases apply.
 *
 * SETUP_TAS is the dedicated IOCTL, registers are read through READ_REGISTER only, and nothing here can release the hold: the
 * operator recovers the target by NIC cycle after every run.
 *
 * Mutation: IOCTL_FILTER_RESTART_ONE_INSTANCE, so NdisFRestartFilter on the exact instance. Unsafe in the runner: it runs
 * only on an operator-attended run that names this entry. Dedicated IOCTLs only, no NIC cycle. A filter restart cannot be
 * undone, and by source reading the I226 PHC is reseeded (#348): that is a result, and restoration stays failed.
 *
 * Adapters are selected from capabilities in tas_plan_build(); NET_LUID binds and correlates, DIDs are printed only. The
 * runner owns the machine lock, the baseline and post-test state check, build provenance and the DbgView capture. This
 * test owns its ETW session and its restoration verdict. Where the management session runs is reported and stops nothing:
 * nothing here depends on it, and no verdict reads it.
 *
 * Evidence: LOCAL_DIAGNOSTIC, never #384 RED or GREEN. Pure rules are in tests/ioctl/tas_lifecycle_eval.h. Procedure and
 * rule: investigations/issue-364-filter-lifecycle-case-a.md.
 *
 * Exit: 0 all passed, 1 any failure or restoration verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #364
 */
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tlhelp32.h>
#include <evntprov.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../../include/intel_pci_ids.h"
#include "../../intel-ethernet-regs/gen/i226_regs.h"
typedef int NDIS_PARAMETER_TYPE;   /* a kernel type in structures of filteruser.h that this test does not use */
#include "../../src/filteruser.h"
#include "tas_wire_observer.h"
#include "../ioctl/tas_lifecycle_eval.h"

#define DEVICE_NAME          "\\\\.\\IntelAvbFilter"
#define TAG                  "[LOCAL_DIAGNOSTIC]"
#define PAYLOAD_BYTES        64
#define LARGE_PAYLOAD_BYTES  1400u
#define SEND_PERIOD_MS       50
#define BASELINE_MS          3000
#define PHC_PAIRS            5
#define BASELINE_HEAD_FRAMES 5
#define FILETIME_TO_UNIX_US  11644473600000000LL
#define DRAIN_MS             1000
#define WIRE_MIN_PERCENT     90
#define RETURN_BOUND_MS      60000
#define SETTLE_MS            5000
#define TRIGGER_BOUND_MS     5000     /* the trigger only schedules, so it should return at once */
#define LIFECYCLE_BOUND_MS   60000    /* a stop condition, not an acceptance criterion */
#define BASELINE_POLL_MS     200      /* undisturbed polling before the trigger, to state the resolution */
#define PASSIVE_POLL_MS      1000
#define MARKER_CHARS         96
#define INSTANCE_LIST_BYTES  16384
#define GUID_TEXT_CHARS      39
#define TRACE_NAME           L"AvbI364CaseA"
#define NDIS_KEYWORDS        0x4316ULL  /* filter, miniport and power keywords, as in the Stage 0 and Stage 1 sessions */
#define NDIS_KEYWORD_PROTOCOL 0x800ULL  /* Case B: its first run saw no filter state at all, and this is where a protocol pause would show */
#define MANAGEMENT_PROCESS   L"vncserver.exe"
#define MAX_NDIS_EVENTS      4096
#define MAX_MARKS            32
#define NAME_CHARS           160
#define ALL_ONES             0xFFFFFFFFu
#define CASE_B_HOLD_MS       8000     /* past the 84 to 87 frames at 64 bytes after which the ring stalled, 3 of 3 on 2026-11-05 */
#define CASE_B_DRAINED_MS    1500     /* about 24 frames: held, with descriptors still completing, so nothing stays outstanding */
#define CASE_B_EVIDENCE_DID  INTEL_DEV_I226_LM   /* the register map is hardware-observed here only, REV_04. It limits evidence, never eligibility */
#define TX_QUEUES            4

static unsigned g_next_seq;
static unsigned g_payload_bytes = PAYLOAD_BYTES;
static DWORD g_send_period_ms = SEND_PERIOD_MS;
static avb_u64 g_capture_luid;
static LONGLONG g_send_us[WIRE_MAX_FRAMES];

/* Private ETW provider for markers beside the NDIS events */
static const GUID g_marker_provider = {0xB7E1F3C2, 0x6A54, 0x4D0E, {0x9C, 0x37, 0x36, 0x40, 0xE8, 0xA1, 0xD3, 0x64}};
/* Microsoft-Windows-NDIS, read from this machine with logman query providers */
static const GUID g_ndis_provider = {0xCDEAD503, 0x17F5, 0x4A3E, {0xB7, 0xAE, 0xDF, 0x8C, 0xC2, 0x90, 0x2E, 0xB9}};
static REGHANDLE g_marker;
static avb_u64 g_marker_luid;

typedef struct {
    LONG captured, from_target;
    unsigned long long os_out, gauge;
    BOOL ok;
} SNAP;

/* What one eligible adapter leaves for the verdicts, which need the closed trace */
typedef struct {
    BOOL ran, triggered, failed_setup;
    avb_u64 luid;
    int index;
    avb_u16 did;
    WCHAR instance[NAME_CHARS], if_guid[GUID_TEXT_CHARS];
    LONGLONG marker_qpc, marker_bracket;
    TAS_WINDOW_TRACK track;
    TAS_LIFECYCLE_OUTCOME outcome;
    TAS_RESTORATION_OUTCOME cleanup;
    unsigned others_changed;
    int rc;
} CASE_RESULT;

static CASE_RESULT g_case[TAS_MAX_ADAPTERS];
static CASE_RESULT *g_now;
static BOOL g_case_b, g_case_b_drained;
static const char *g_tc_run = "TC-CASEA-001", *g_tc_state = "TC-CASEA-002", *g_tc_clean = "TC-CASEA-003";

/* The table of test_tas_isolated_arm without the MAC address. TDH and TDT move with traffic and are read apart */
typedef struct { const char *name; avb_u32 offset; } TSN_REG;

static const TSN_REG g_tsn[] = {
    {"TQAVCTRL", I226_TQAVCTRL}, {"TXPBSIZE", I226_TXPBSIZE}, {"RXPBSIZE", I226_RXPBSIZE}, {"DTXMXPKTSZ", I226_DTXMXPKTSZ},
    {"TXARB", I226_TXARB}, {"RETX_CTL", I226_RETX_CTL}, {"GTXOFFSET", I226_GTXOFFSET}, {"BASET_L", I226_BASET_L},
    {"BASET_H", I226_BASET_H}, {"QBVCYCLET", I226_QBVCYCLET}, {"QBVCYCLET_S", I226_QBVCYCLET_S},
    {"STQT0", I226_STQT0}, {"STQT1", I226_STQT1}, {"STQT2", I226_STQT2}, {"STQT3", I226_STQT3},
    {"ENDQT0", I226_ENDQT0}, {"ENDQT1", I226_ENDQT1}, {"ENDQT2", I226_ENDQT2}, {"ENDQT3", I226_ENDQT3},
    {"TXQCTL0", I226_TXQCTL0}, {"TXQCTL1", I226_TXQCTL1}, {"TXQCTL2", I226_TXQCTL2}, {"TXQCTL3", I226_TXQCTL3},
    {"TQAVCC0", I226_TQAVCC0}, {"TQAVCC1", I226_TQAVCC1}, {"TQAVHC0", I226_TQAVHC0}, {"TQAVHC1", I226_TQAVHC1},
    {"TCTL", I226_TCTL}, {"TDLEN0", I226_TDLEN0}, {"TDLEN1", I226_TDLEN1}, {"TDLEN2", I226_TDLEN2}, {"TDLEN3", I226_TDLEN3},
    {"TXDCTL0", I226_TXDCTL0}, {"TXDCTL1", I226_TXDCTL1}, {"TXDCTL2", I226_TXDCTL2}, {"TXDCTL3", I226_TXDCTL3},
    {"TIMINCA", I226_TIMINCA}, {"TSAUXC", I226_TSAUXC}, {"TSYNCTXCTL", I226_TSYNCTXCTL}, {"TSYNCRXCTL", I226_TSYNCRXCTL},
};
#define TSN_COUNT (sizeof(g_tsn) / sizeof(g_tsn[0]))
#define TSN_TQAVCTRL 0u   /* its index in g_tsn */

static const avb_u32 g_tdh[TX_QUEUES] = {I226_TDH0, I226_TDH1, I226_TDH2, I226_TDH3};
static const avb_u32 g_tdt[TX_QUEUES] = {I226_TDT0, I226_TDT1, I226_TDT2, I226_TDT3};

static void verdict(const char *tag, const char *tc, int adapter, avb_u16 did, const char *reason);

static HANDLE open_device(void)
{
    return CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL identity_at(HANDLE h, UINT32 index, AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    DWORD br = 0;
    ZeroMemory(id, sizeof(*id));
    id->version = AVB_ADAPTER_IDENTITY_VERSION;
    id->index = index;
    return DeviceIoControl(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, id, sizeof(*id), id, sizeof(*id), &br, NULL);
}

static BOOL bind_luid(HANDLE h, avb_u64 luid)
{
    AVB_OPEN_BY_LUID_REQUEST bind = {0};
    DWORD br = 0;
    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = luid;
    return DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &br, NULL);
}

static BOOL read_systim(HANDLE h, unsigned long long *systim)
{
    AVB_CLOCK_CONFIG c;
    DWORD br = 0;
    ZeroMemory(&c, sizeof(c));
    if (!DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &br, NULL)) {
        return FALSE;
    }
    *systim = c.systim;
    return TRUE;
}

static BOOL read_armed(HANDLE h, unsigned *armed)
{
    AVB_TAS_STATE tas = {0};
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_GET_TAS_STATE, &tas, sizeof(tas), &tas, sizeof(tas), &br, NULL)) {
        return FALSE;
    }
    *armed = tas.armed;
    return TRUE;
}

static LONGLONG host_us(void)
{
    FILETIME ft;
    ULARGE_INTEGER t;

    GetSystemTimePreciseAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (LONGLONG)(t.QuadPart / 10) - FILETIME_TO_UNIX_US;
}

static void snap(HANDLE h, avb_u64 luid, SNAP *s)
{
    AVB_DRIVER_STATISTICS st;
    MIB_IF_ROW2 row;
    DWORD br = 0;

    ZeroMemory(s, sizeof(*s));
    ZeroMemory(&st, sizeof(st));
    ZeroMemory(&row, sizeof(row));
    row.InterfaceLuid.Value = luid;
    s->ok = DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, &st, sizeof(st), &st, sizeof(st), &br, NULL) && GetIfEntry2(&row) == NO_ERROR;
    s->gauge = st.OutstandingSendNBLs;
    s->os_out = row.OutUcastPkts + row.OutNUcastPkts;
    wire_counts(&s->captured, &s->from_target);
}

static unsigned tx_step(HANDLE h, avb_u64 luid, SOCKET sock, DWORD ms, const char *label, unsigned armed_first_seq, BOOL *capture_alive)
{
    static unsigned char payload[LARGE_PAYLOAD_BYTES];
    SOCKADDR_IN peer = {0};
    unsigned first = g_next_seq, sent = 0, errors = 0, seen, all = 0;
    long missing, last, m2, l2;
    SNAP before, after;
    DWORD start;

    snap(h, luid, &before);
    start = GetTickCount();

    peer.sin_family = AF_INET;
    peer.sin_port = htons(TAS_WIRE_UDP_PORT);
    peer.sin_addr.s_addr = INADDR_BROADCAST;
    while (GetTickCount() - start < ms) {
        tas_wire_fill(payload, g_payload_bytes, g_wire_nonce, g_next_seq);
        if (g_next_seq < WIRE_MAX_FRAMES) {
            g_send_us[g_next_seq] = host_us();
        }
        if (sendto(sock, (const char *)payload, (int)g_payload_bytes, 0, (SOCKADDR *)&peer, sizeof(peer)) != SOCKET_ERROR) {
            sent++;
            g_next_seq++;
        } else {
            errors++;
        }
        Sleep(g_send_period_ms);
    }
    Sleep(DRAIN_MS);
    seen = wire_seen(first, first + sent, &missing, &last);
    all = wire_seen(armed_first_seq, g_next_seq, &m2, &l2);
    snap(h, luid, &after);
    printf(TAG " %s: accepted=%u sendErrors=%u wireSeen=%u firstMissingSeq=%ld sinceArmSeen=%u/%u osOutDelta=%llu "
           "gauge=%llu capturedDelta=%ld fromTargetDelta=%ld icmp=%ld/%ld samplesOk=%d\n", label, sent, errors, seen, missing, all,
           g_next_seq - armed_first_seq,
           after.os_out - before.os_out, after.gauge, after.captured - before.captured,
           after.from_target - before.from_target, g_icmp_replied, g_icmp_sent, before.ok && after.ok);
    if (after.captured == before.captured) {
        *capture_alive = FALSE;
        printf(TAG " %s: the capture saw nothing at all, so wireSeen=0 proves nothing here\n", label);
    }
    fflush(stdout);
    return seen;
}

static void phc_pairs(HANDLE h, const char *label)
{
    int i;

    for (i = 0; i < PHC_PAIRS; i++) {
        unsigned long long phc = 0;
        LONGLONG t0 = host_us(), t1;
        BOOL ok = read_systim(h, &phc);
        t1 = host_us();
        printf("    phcpair %s hostMidUs=%lld halfUs=%lld phcNs=%llu ok=%d\n", label, (t0 + t1) / 2, (t1 - t0) / 2, phc, ok);
    }
}

static void dump_baseline_head(unsigned first)
{
    unsigned seq;

    for (seq = first; seq < first + BASELINE_HEAD_FRAMES && seq < g_next_seq; seq++) {
        printf("    baseline frame seq=%u sendUs=%lld rxUs=%lld delayUs=%lld\n", seq, g_send_us[seq],
               g_wire_seen[seq] != 0 ? g_wire_rx_us[seq] : -1LL, g_wire_seen[seq] != 0 ? g_wire_rx_us[seq] - g_send_us[seq] : -1LL);
    }
}

static void report_phc(HANDLE h, unsigned long long phc_before, LARGE_INTEGER qpc_before, const char *label)
{
    LARGE_INTEGER qpc, frequency;
    unsigned long long phc = 0;
    long long wall_ns, drift_ns;

    if (!read_systim(h, &phc)) {
        printf(TAG " %s: PHC unreadable\n", label);
        return;
    }
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&frequency);
    wall_ns = (long long)((double)(qpc.QuadPart - qpc_before.QuadPart) * 1e9 / (double)frequency.QuadPart);
    drift_ns = ((long long)phc - (long long)phc_before) - wall_ns;
    printf(TAG " %s: PHC before=%llu now=%llu wallNs=%lld driftNs=%lld\n", label, phc_before, phc, wall_ns, drift_ns);
}

static void observer_line(const char *label)
{
    MIB_IF_ROW2 row;

    ZeroMemory(&row, sizeof(row));
    row.InterfaceLuid.Value = g_capture_luid;
    if (GetIfEntry2(&row) == NO_ERROR) {
        printf("    observer %s inNUcast=%llu inDiscards=%llu inErrors=%llu\n", label, row.InNUcastPkts, row.InDiscards, row.InErrors);
    } else {
        printf("    observer %s counters unreadable\n", label);
    }
}

static BOOL find_addressing(avb_u64 luid, IN_ADDR *ipv4, BOOL *other_link)
{
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES *list = (IP_ADAPTER_ADDRESSES *)malloc(size), *a;
    BOOL found = FALSE;

    *other_link = FALSE;
    if (list == NULL || GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, list, &size) != NO_ERROR) {
        free(list);
        return FALSE;
    }
    for (a = list; a != NULL; a = a->Next) {
        if (a->Luid.Value == luid) {
            if (a->FirstUnicastAddress != NULL && a->OperStatus == IfOperStatusUp) {
                *ipv4 = ((SOCKADDR_IN *)a->FirstUnicastAddress->Address.lpSockaddr)->sin_addr;
                found = TRUE;
            }
        } else if (a->OperStatus == IfOperStatusUp && a->FirstGatewayAddress != NULL) {
            *other_link = TRUE;
        }
    }
    free(list);
    return found;
}

static SOCKET open_tx_socket(IN_ADDR ipv4)
{
    SOCKADDR_IN local = {0};
    BOOL on = TRUE;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD start = GetTickCount();

    local.sin_family = AF_INET;
    local.sin_addr = ipv4;
    if (sock == INVALID_SOCKET || setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on)) != 0) {
        return INVALID_SOCKET;
    }
    while (bind(sock, (SOCKADDR *)&local, sizeof(local)) != 0) {
        if (GetTickCount() - start >= RETURN_BOUND_MS) {
            closesocket(sock);
            return INVALID_SOCKET;
        }
        Sleep(1000);
    }
    return sock;
}

static BOOL read_stats(HANDLE h, AVB_DRIVER_STATISTICS *st)
{
    DWORD br = 0;

    ZeroMemory(st, sizeof(*st));
    return DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, st, sizeof(*st), st, sizeof(*st), &br, NULL);
}

static BOOL read_snapshot(HANDLE h, AVB_HW_SNAPSHOT_REQUEST *s)
{
    DWORD br = 0;

    ZeroMemory(s, sizeof(*s));
    s->version = AVB_HW_SNAPSHOT_VERSION;
    return DeviceIoControl(h, IOCTL_AVB_QUERY_HW_SNAPSHOT, s, sizeof(*s), s, sizeof(*s), &br, NULL) && s->status == 0;
}

static BOOL read_hw_state(HANDLE h, AVB_HW_STATE_QUERY *q)
{
    DWORD br = 0;

    ZeroMemory(q, sizeof(*q));
    return DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE, q, sizeof(*q), q, sizeof(*q), &br, NULL);
}

static BOOL read_reg(HANDLE h, avb_u32 offset, avb_u32 *value)
{
    AVB_REGISTER_REQUEST r = {0};
    DWORD br = 0;

    r.offset = offset;
    if (!DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL) || r.status != 0) {
        return FALSE;
    }
    *value = r.value;
    return *value != ALL_ONES;
}

/** Every TSN register, printed. Returns how many were unreadable */
static unsigned read_tsn(HANDLE h, avb_u32 *values, const char *label)
{
    unsigned i, unreadable = 0;

    printf("    tsn %s", label);
    for (i = 0; i < TSN_COUNT; i++) {
        if (read_reg(h, g_tsn[i].offset, &values[i])) {
            printf(" %s=0x%08X", g_tsn[i].name, values[i]);
        } else {
            values[i] = ALL_ONES;
            unreadable++;
            printf(" %s=unreadable", g_tsn[i].name);
        }
    }
    printf("\n");
    return unreadable;
}

/** The transmit rings. Only queue 0 was configured in the #328 baseline, so its tail is returned and all four are printed */
static BOOL read_ring(HANDLE h, const char *label, avb_u32 *tail0)
{
    avb_u32 tdh[TX_QUEUES], tdt[TX_QUEUES];
    BOOL ok = TRUE;
    int q;

    for (q = 0; q < TX_QUEUES; q++) {
        ok &= read_reg(h, g_tdh[q], &tdh[q]);
        ok &= read_reg(h, g_tdt[q], &tdt[q]);
    }
    printf("    ring %s ok=%d TDH/TDT q0=%u/%u q1=%u/%u q2=%u/%u q3=%u/%u\n", label, ok, tdh[0], tdt[0], tdh[1], tdt[1], tdh[2], tdt[2], tdh[3], tdt[3]);
    *tail0 = tdt[0];
    return ok;
}

/** Same request as test_tas_isolated_arm: the evidence that SETUP_TAS holds frames was taken with it */
static BOOL arm_tas(HANDLE h, DWORD *error)
{
    AVB_TAS_REQUEST tas;
    DWORD br = 0;

    ZeroMemory(&tas, sizeof(tas));
    tas.config.base_time_ns = 1000000;
    tas.config.cycle_time_ns = 1000000;
    tas.config.gate_states[0] = 0xFF;
    tas.config.gate_durations[0] = 500000;
    tas.config.gate_states[1] = 0x01;
    tas.config.gate_durations[1] = 500000;
    *error = ERROR_SUCCESS;
    if (!DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, &tas, sizeof(tas), &tas, sizeof(tas), &br, NULL)) {
        *error = GetLastError();
        return FALSE;
    }
    return tas.status == 0;
}

static int oper_status(avb_u64 luid)
{
    MIB_IF_ROW2 row;

    ZeroMemory(&row, sizeof(row));
    row.InterfaceLuid.Value = luid;
    return GetIfEntry2(&row) == NO_ERROR ? (int)row.OperStatus : -1;
}

static void print_clock(HANDLE h, const char *label)
{
    AVB_CLOCK_CONFIG c;
    DWORD br = 0;

    ZeroMemory(&c, sizeof(c));
    if (DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &br, NULL)) {
        printf("    clock %s systim=%llu timinca=0x%08X tsauxc=0x%08X rateMhz=%u\n", label, (unsigned long long)c.systim, c.timinca, c.tsauxc, c.clock_rate_mhz);
    } else {
        printf("    clock %s unreadable (error %lu)\n", label, GetLastError());
    }
}

static TAS_INSTANCE_RESULT find_instance(HANDLE h, avb_u64 luid, const WCHAR **name, USHORT *name_bytes)
{
    static BYTE list[INSTANCE_LIST_BYTES];   /* static: the trigger thread may outlive the call that started it */
    WCHAR guid_text[GUID_TEXT_CHARS];
    NET_LUID l;
    GUID g;
    DWORD br = 0, at;

    l.Value = luid;
    if (ConvertInterfaceLuidToGuid(&l, &g) != NO_ERROR) {
        return TAS_INSTANCE_NONE;
    }
    swprintf_s(guid_text, GUID_TEXT_CHARS, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
               g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    if (!DeviceIoControl(h, IOCTL_FILTER_ENUERATE_ALL_INSTANCES, NULL, 0, list, sizeof(list), &br, NULL)) {
        printf(TAG " instance list unreadable (error %lu)\n", GetLastError());
        return TAS_INSTANCE_MALFORMED;
    }
    printf("    target adapter GUID %ls, instance list %lu bytes\n", guid_text, br);
    for (at = 0; at + sizeof(USHORT) <= br;) {
        USHORT bytes;

        memcpy(&bytes, list + at, sizeof(bytes));
        at += sizeof(USHORT);
        if (bytes > br - at) {
            break;
        }
        printf("    instance %.*ls\n", (int)(bytes / sizeof(WCHAR)), (const WCHAR *)(list + at));
        at += bytes;
    }
    return tas_instance_find(list, br, guid_text, name, name_bytes);
}

typedef struct { HANDLE h; const WCHAR *name; USHORT bytes; BOOL ok; DWORD error; ULONG ndis_status; DWORD out_bytes; } TRIGGER_CALL;

static LONGLONG qpc_now(void)
{
    LARGE_INTEGER q;

    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static double ticks_to_us(LONGLONG ticks)
{
    LARGE_INTEGER frequency;

    QueryPerformanceFrequency(&frequency);
    return (double)ticks * 1e6 / (double)frequency.QuadPart;
}

static void marker(const char *label)
{
    WCHAR text[MARKER_CHARS];
    LONGLONG before = qpc_now(), after;
    ULONG status;

    swprintf_s(text, MARKER_CHARS, L"i364 %hs luid=0x%016llX qpc=%lld", label, g_marker_luid, before);
    status = EventWriteString(g_marker, 0, 0, text);
    after = qpc_now();
    if (g_now != NULL && !g_now->triggered) {
        g_now->marker_bracket = after - before;   /* the bracket of trigger-issued, which places the events */
    }
    printf("    marker %s luid=0x%016llX qpcBefore=%lld qpcAfter=%lld hostUs=%lld status=%lu\n", label, g_marker_luid, before, after, host_us(), status);
}

static DWORD WINAPI trigger_thread(LPVOID arg)
{
    TRIGGER_CALL *call = (TRIGGER_CALL *)arg;
    DWORD br = 0;

    /* The output word is filled only by a driver that reports the NDIS status of NdisFRestartFilter; older drivers leave 0 bytes */
    call->ok = DeviceIoControl(call->h, IOCTL_FILTER_RESTART_ONE_INSTANCE, (LPVOID)call->name, call->bytes, &call->ndis_status, sizeof(call->ndis_status), &br, NULL);
    call->error = call->ok ? ERROR_SUCCESS : GetLastError();
    call->out_bytes = br;
    return 0;
}

static void poll_print_edge(const char *name, const TAS_EDGE *edge)
{
    if (edge->seen) {
        printf("    edge %s entry between qpc=%lld and qpc=%lld widthUs=%.1f\n", name, edge->last_old, edge->first_new, ticks_to_us(edge->first_new - edge->last_old));
    } else {
        printf("    edge %s not seen by the polls\n", name);
    }
}

static BOOL poll_baseline(HANDLE h, TAS_WINDOW_TRACK *track, BOOL passive)
{
    AVB_DRIVER_STATISTICS st;
    LARGE_INTEGER frequency;
    DWORD start = GetTickCount();
    LONGLONG first = qpc_now();

    QueryPerformanceFrequency(&frequency);
    do {
        TAS_POLL poll;

        poll.qpc_start = qpc_now();
        if (!read_stats(h, &st)) {
            printf(TAG " statistics unreadable before the trigger (error %lu)\n", GetLastError());
            return FALSE;
        }
        poll.qpc_end = qpc_now();
        poll.pause = st.FilterPauseCount;
        poll.restart = st.FilterRestartCount;
        tas_window_feed(track, &poll);
    } while (!passive && GetTickCount() - start < BASELINE_POLL_MS);
    printf("    timing qpcHz=%lld passive=%d baselinePolls=%u meanGapUs=%.1f worstGapUs=%.1f\n", frequency.QuadPart, passive, track->polls,
           ticks_to_us((qpc_now() - first) / (LONGLONG)track->polls), ticks_to_us(track->max_gap));
    return TRUE;
}

static void lifecycle_measure(HANDLE h, const AVB_DRIVER_STATISTICS *before, const WCHAR *name, USHORT bytes, BOOL passive)
{
    static TRIGGER_CALL call;   /* static: survives an abandoned, still blocked thread */
    static TAS_WINDOW_TRACK track;
    AVB_DRIVER_STATISTICS st, last = *before;
    TAS_WINDOW_CLASS window;
    HANDLE thread;
    LONGLONG lower = 0, upper = 0;
    DWORD start;
    BOOL returned = FALSE, ended = FALSE;

    tas_window_init(&track, before->FilterPauseCount, before->FilterRestartCount);
    if (!poll_baseline(h, &track, passive)) {
        return;
    }

    call.h = open_device();
    call.name = name;
    call.bytes = bytes;
    call.ok = FALSE;
    call.ndis_status = 0;
    call.out_bytes = 0;
    if (call.h == INVALID_HANDLE_VALUE) {
        printf(TAG " the trigger could not open a handle (error %lu)\n", GetLastError());
        return;
    }
    marker("trigger-issued");
    if (g_now != NULL) {
        g_now->triggered = TRUE;
    }
    thread = CreateThread(NULL, 0, trigger_thread, &call, 0, NULL);
    if (thread == NULL) {
        printf(TAG " the trigger thread could not start (error %lu)\n", GetLastError());
        return;
    }

    start = GetTickCount();
    while (!ended && GetTickCount() - start < LIFECYCLE_BOUND_MS) {
        TAS_POLL poll;

        poll.qpc_start = qpc_now();
        if (!read_stats(h, &st)) {
            printf(TAG " statistics unreadable during the lifecycle (error %lu)\n", GetLastError());
            break;
        }
        poll.qpc_end = qpc_now();
        poll.pause = st.FilterPauseCount;
        poll.restart = st.FilterRestartCount;
        tas_window_feed(&track, &poll);
        if (st.FilterPauseCount != last.FilterPauseCount || st.FilterRestartCount != last.FilterRestartCount ||
            st.PauseRestartGeneration != last.PauseRestartGeneration || st.FilterDetachCount != last.FilterDetachCount ||
            st.FilterAttachCount != last.FilterAttachCount) {
            printf("    lifecycle hostUs=%lld qpcEnd=%lld pause=%llu restart=%llu generation=%llu attach=%llu detach=%llu gauge=%llu\n", host_us(), poll.qpc_end,
                   st.FilterPauseCount, st.FilterRestartCount, st.PauseRestartGeneration, st.FilterAttachCount, st.FilterDetachCount, st.OutstandingSendNBLs);
            last = st;
        }
        if (!returned && WaitForSingleObject(thread, 0) == WAIT_OBJECT_0) {
            returned = TRUE;
            printf(TAG " trigger returned ok=%d error=%lu ndisStatus=0x%08lX statusBytes=%lu at qpc=%lld; this proves nothing about the lifecycle\n", call.ok, call.error,
                   call.ndis_status, call.out_bytes, qpc_now());
        }
        ended = st.FilterRestartCount != before->FilterRestartCount || st.FilterAttachCount != before->FilterAttachCount ||
                st.FilterDetachCount != before->FilterDetachCount || st.FilterPauseCount < before->FilterPauseCount;
        if (passive) {
            Sleep(PASSIVE_POLL_MS);
        } else {
            (void)SwitchToThread();
        }
    }
    marker("lifecycle-seen");
    if (!ended) {
        printf(TAG " no Restart entry within %d ms; stopping here is a stop condition, not a result\n", LIFECYCLE_BOUND_MS);
    }
    if (!returned && WaitForSingleObject(thread, TRIGGER_BOUND_MS) == WAIT_OBJECT_0) {
        returned = TRUE;
        printf(TAG " trigger returned ok=%d error=%lu ndisStatus=0x%08lX statusBytes=%lu at qpc=%lld, after the lifecycle ended; this proves nothing about it\n", call.ok,
               call.error, call.ndis_status, call.out_bytes, qpc_now());
    }
    if (!returned) {
        printf(TAG " the trigger did not return within the bounds\n");
    }
    CloseHandle(thread);

    if (g_now != NULL) {
        g_now->track = track;
    }
    window = tas_window_result(&track, &lower, &upper);
    poll_print_edge("pause", &track.pause);
    poll_print_edge("restart", &track.restart);
    printf(TAG " window %s entryToEntry lowerUs=%.1f upperUs=%.1f polls=%u worstGapUs=%.1f insideReads=%u provenInsideCalls=%u; entry to entry only, which includes Pausing, and a poll-based bound never shows absence\n",
           tas_window_name(window), ticks_to_us(lower), ticks_to_us(upper), track.polls, ticks_to_us(track.max_gap), track.inside_samples, tas_window_proven_calls(&track));
}

static TAS_LIFECYCLE_OUTCOME lifecycle_read(HANDLE h, const AVB_DRIVER_STATISTICS *before, AVB_DRIVER_STATISTICS *after, BOOL traffic)
{
    TAS_LIFECYCLE_EVIDENCE e;
    TAS_LIFECYCLE_OUTCOME outcome;

    ZeroMemory(after, sizeof(*after));

    if (!read_stats(h, after)) {
        printf(TAG " statistics unreadable after the trigger (error %lu); counters read as zero, which is a new context\n", GetLastError());
    }
    tas_lifecycle_evidence(before, after, traffic, &e);
    outcome = tas_lifecycle_outcome(&e);
    printf(TAG " lifecycle %s pauseDelta=%lld restartDelta=%lld generationDelta=%lld attachDelta=%lld detachDelta=%lld statusDelta=%lld netPnpDelta=%lld gauge=%llu\n",
           tas_lifecycle_name(outcome), e.pause_delta, e.restart_delta, e.generation_delta, e.attach_delta, e.detach_delta,
           (LONGLONG)(after->FilterStatusCount - before->FilterStatusCount), (LONGLONG)(after->FilterNetPnPCount - before->FilterNetPnPCount), after->OutstandingSendNBLs);
    printf(TAG " claims pause=%s restart=%s; observed completion needs an NDIS state event on this instance, which this tool cannot see\n",
           tas_claim_name(tas_pause_claim(&e)), tas_claim_name(tas_restart_claim(&e)));
    return outcome;
}

static unsigned report_others(const AVB_ADAPTER_IDENTITY_REQUEST *ids, const HANDLE *others, const AVB_DRIVER_STATISTICS *before, int n, int t)
{
    AVB_DRIVER_STATISTICS st;
    unsigned differs_count = 0;
    int i;

    for (i = 0; i < n; i++) {
        BOOL differs;

        if (i == t) {
            continue;
        }
        ZeroMemory(&st, sizeof(st));
        differs = !read_stats(others[i], &st) || st.FilterPauseCount != before[i].FilterPauseCount || st.FilterRestartCount != before[i].FilterRestartCount ||
                  st.FilterAttachCount != before[i].FilterAttachCount || st.FilterDetachCount != before[i].FilterDetachCount;
        printf("    other adapter luid=0x%016llX did=0x%04X pause=%llu restart=%llu attach=%llu detach=%llu oper=%d %s\n", ids[i].net_luid, ids[i].device_id, st.FilterPauseCount,
               st.FilterRestartCount, st.FilterAttachCount, st.FilterDetachCount, oper_status(ids[i].net_luid), differs ? "CHANGED" : "unchanged");
        differs_count += differs ? 1u : 0u;
    }
    printf(TAG " other adapters changed=%u of %d\n", differs_count, n - 1);
    return differs_count;
}

static BOOL observer_prove(const AVB_ADAPTER_IDENTITY_REQUEST *ids, const TAS_ADAPTER_FACTS *facts, int n, int t, HANDLE h, SOCKET sock, IN_ADDR ipv4, BOOL *capture_alive)
{
    unsigned order[TAS_MAX_ADAPTERS];
    unsigned m = tas_observer_candidates(facts, (unsigned)n, (unsigned)t, order), i;

    for (i = 0; i < m; i++) {
        unsigned first = g_next_seq, seen;

        g_capture_luid = facts[order[i]].net_luid;
        *capture_alive = TRUE;
        printf("    observer candidate luid=0x%016llX did=0x%04X\n", ids[order[i]].net_luid, ids[order[i]].device_id);
        if (wire_start(ids[t].net_luid, g_capture_luid, ids[t].mac, GetTickCount() ^ GetCurrentProcessId(), ipv4) != 0) {
            seen = tx_step(h, ids[t].net_luid, sock, BASELINE_MS, "baseline", g_next_seq, capture_alive);
            if (*capture_alive && g_next_seq != first && (unsigned long long)seen * 100 >= (unsigned long long)(g_next_seq - first) * WIRE_MIN_PERCENT) {
                printf(TAG " observer proved luid=0x%016llX did=0x%04X frames=%u of %u\n", ids[order[i]].net_luid, ids[order[i]].device_id, seen, g_next_seq - first);
                return TRUE;
            }
            printf("    candidate did not see the target's frames: not an observer here\n");
        }
        wire_stop();
    }
    return FALSE;
}

/** Where the management session runs, from its own sockets: established TCP and UDP bound to one address. Stops nothing */
static void management_report(IN_ADDR target)
{
    static BYTE table[256 * 1024];
    DWORD pids[16], size, i;
    unsigned npids = 0, locals = 0, on_target = 0, p;
    PROCESSENTRY32W pe;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    TAS_MGMT_CLASS c;

    if (snapshot != INVALID_HANDLE_VALUE) {
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, MANAGEMENT_PROCESS) == 0 && npids < 16) {
                    pids[npids++] = pe.th32ProcessID;
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    size = sizeof(table);
    if (npids != 0 && GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        const MIB_TCPTABLE_OWNER_PID *t = (const MIB_TCPTABLE_OWNER_PID *)table;

        for (i = 0; i < t->dwNumEntries; i++) {
            for (p = 0; p < npids; p++) {
                if (t->table[i].dwOwningPid == pids[p] && t->table[i].dwState == MIB_TCP_STATE_ESTAB && t->table[i].dwLocalAddr != htonl(INADDR_LOOPBACK)) {
                    locals++;
                    on_target += t->table[i].dwLocalAddr == target.s_addr;
                }
            }
        }
    }
    size = sizeof(table);
    if (npids != 0 && GetExtendedUdpTable(table, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
        const MIB_UDPTABLE_OWNER_PID *t = (const MIB_UDPTABLE_OWNER_PID *)table;

        for (i = 0; i < t->dwNumEntries; i++) {
            for (p = 0; p < npids; p++) {
                if (t->table[i].dwOwningPid == pids[p] && t->table[i].dwLocalAddr != INADDR_ANY && t->table[i].dwLocalAddr != htonl(INADDR_LOOPBACK)) {
                    locals++;
                    on_target += t->table[i].dwLocalAddr == target.s_addr;
                }
            }
        }
    }
    c = tas_mgmt_class(locals, on_target);
    printf(TAG " management class=%s process=%ls processes=%u boundSockets=%u onTarget=%u\n", tas_mgmt_name(c), MANAGEMENT_PROCESS, npids, locals, on_target);
    if (c != TAS_MGMT_NOT_ON_TARGET) {
        printf(TAG " WARNING management session is on a target adapter or not attributed: it may be interrupted, and this run does not depend on it\n");
    }
    fflush(stdout);
}

static BOOL trace_gate(const CASE_RESULT *c);

/** Case B from the proved observer on. Returns 3 when nothing was armed, else 1: restoration is always owed here */
static int case_b_target(const AVB_ADAPTER_IDENTITY_REQUEST *ids, int n, int t, HANDLE h, SOCKET sock, const WCHAR *name, USHORT name_bytes, const HANDLE *others,
                         const AVB_DRIVER_STATISTICS *other_before, BOOL *capture_alive)
{
    static avb_u32 base[TSN_COUNT], held[TSN_COUNT], now[TSN_COUNT];
    AVB_DRIVER_STATISTICS before, after;
    AVB_HW_STATE_QUERY hw_before, hw_after;
    TAS_LIFECYCLE_CLEANUP cleanup;
    TAS_LIFECYCLE_OUTCOME outcome;
    TAS_HOLD_CLASS hold;
    TAS_PHC_FACT phc_fact;
    LARGE_INTEGER qpc_before;
    unsigned long long phc_before = 0, phc_after = 0;
    unsigned armed = 0, armed_after = 1, hold_first = g_next_seq, hold_sent, hold_seen, released, first, sent, seen, kept = 0, i;
    avb_u32 tail_before = 0, tail_after = 0, unused;
    long missing, last;
    DWORD error;
    BOOL armed_ok, gate, wire, traffic, tail_read = FALSE, phc_before_ok, phc_after_ok, mode_after, consistent;
    char why[160];

    ZeroMemory(&cleanup, sizeof(cleanup));
    if (ids[t].device_id != CASE_B_EVIDENCE_DID) {
        printf(TAG " layout applicable=no evidence=none did=0x%04X: eligible, but this register map has no evidence here, so no register access and nothing armed\n", ids[t].device_id);
        return 3;
    }
    printf(TAG " layout applicable=yes evidence=I226-LM scope=REV_04,1Gbit,Debug revision=from the runner's PCI instance\n");
    if (read_tsn(h, base, "pre-state") != 0) {
        printf(TAG " not run: a TSN register is unreadable, so the pre-state is not proved; nothing armed\n");
        return 3;
    }
    if ((base[TSN_TQAVCTRL] & I226_TQAVCTRL_TRANSMIT_MODE_MASK) != 0) {
        printf(TAG " not run: TSN transmit mode is already set, so the adapter is not clean; nothing armed\n");
        return 3;
    }
    (void)read_ring(h, "pre-state", &unused);

    /* Setup. Whatever happens from here on, the adapter is the operator's to recover */
    armed_ok = arm_tas(h, &error);
    printf(TAG " SETUP_TAS ok=%d error=%lu\n", armed_ok, error);
    armed_ok = armed_ok && read_armed(h, &armed) && armed != 0;
    hold_seen = armed_ok ? tx_step(h, ids[t].net_luid, sock, g_case_b_drained ? CASE_B_DRAINED_MS : CASE_B_HOLD_MS, "hold", g_next_seq, capture_alive) : 0;
    hold_sent = g_next_seq - hold_first;
    hold = tas_hold_class(armed_ok, *capture_alive, hold_sent, hold_seen);
    sprintf_s(why, sizeof(why), "%s accepted=%u seen=%u tasArmed=%u observerAlive=%d", tas_hold_name(hold), hold_sent, hold_seen, armed, *capture_alive);
    verdict(hold == TAS_HOLD_PROVEN ? "PASS" : "FAILED_SETUP", "TC-CASEB-001", t, ids[t].device_id, why);
    (void)read_tsn(h, held, "held");
    (void)read_ring(h, "held", &unused);
    if (hold != TAS_HOLD_PROVEN) {
        cleanup.regs_compared = TSN_COUNT;
        for (i = 0; i < TSN_COUNT; i++) {
            cleanup.regs_changed += held[i] != base[i];
        }
        g_now->failed_setup = TRUE;
        g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
        printf(TAG " no trigger: the setup of Case B was not reached\n");
        return 1;
    }

    /* The state that matters is the one just before the trigger */
    print_clock(h, "before");
    phc_pairs(h, "before");
    observer_line("before");
    phc_before_ok = read_systim(h, &phc_before);
    QueryPerformanceCounter(&qpc_before);
    if (!read_stats(h, &before) || !read_hw_state(h, &hw_before) || oper_status(ids[t].net_luid) != IfOperStatusUp) {
        printf(TAG " no trigger: statistics or link changed\n");
        g_now->failed_setup = TRUE;
        g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
        return 1;
    }
    if (g_case_b_drained && before.OutstandingSendNBLs != 0) {
        sprintf_s(why, sizeof(why), "the drained variant needs gauge 0 at the trigger, and it is %llu", before.OutstandingSendNBLs);
        verdict("FAILED_SETUP", "TC-CASEB-001", t, ids[t].device_id, why);
        g_now->failed_setup = TRUE;
        g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
        return 1;
    }
    printf("    lifecycle hostUs=%lld before trigger pause=%llu restart=%llu generation=%llu attach=%llu detach=%llu gauge=%llu\n", host_us(), before.FilterPauseCount,
           before.FilterRestartCount, before.PauseRestartGeneration, before.FilterAttachCount, before.FilterDetachCount, before.OutstandingSendNBLs);
    lifecycle_measure(h, &before, name, name_bytes, FALSE);
    Sleep(SETTLE_MS);   /* FilterRestartCount moves on entry, before the bring-up, and no signal marks its end */

    outcome = lifecycle_read(h, &before, &after, FALSE);
    g_now->outcome = outcome;
    g_now->others_changed = report_others(ids, others, other_before, n, t);
    gate = trace_gate(g_now);

    /* #350: no MMIO unless the filter is Running. Frames need no MMIO, so they go out either way */
    if (gate) {
        tail_read = read_ring(h, "after the restart", &tail_before);
    }
    first = g_next_seq;
    seen = tx_step(h, ids[t].net_luid, sock, BASELINE_MS, "after the restart", g_next_seq, capture_alive);
    sent = g_next_seq - first;
    wire = *capture_alive && sent != 0 && (unsigned long long)seen * 100 >= (unsigned long long)sent * WIRE_MIN_PERCENT;
    if (gate) {
        tail_read = read_ring(h, "after the probe", &tail_after) && tail_read;
    }
    traffic = tas_hold_stack_carried(wire, tail_read, sent, tail_before, tail_after);
    released = wire_seen(hold_first, hold_first + hold_sent, &missing, &last);
    outcome = lifecycle_read(h, &before, &after, traffic);
    g_now->outcome = outcome;
    g_now->others_changed = report_others(ids, others, other_before, n, t);
    cleanup.others_changed = g_now->others_changed;
    cleanup.tx_probed = *capture_alive && sent != 0;
    cleanup.tx_shown = wire;
    g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
    printf(TAG " fact q3 held=%u releasedOnWire=%u firstUnseenSeq=%ld probeSeen=%u of %u tailMoved=%d gaugeBefore=%llu gaugeAfter=%llu\n", hold_sent, released, missing, seen, sent,
           tail_read && tail_before != tail_after, before.OutstandingSendNBLs, after.OutstandingSendNBLs);
    if (!gate) {
        printf(TAG " Running not shown: no MMIO, so register state, PHC and consistency stay unmeasured\n");
        marker("run-end");
        return 1;
    }

    print_clock(h, "after");
    phc_pairs(h, "after");
    observer_line("after");
    phc_after_ok = read_systim(h, &phc_after);
    if (phc_after_ok) {
        printf("    implied PHC zero hostUs=%lld at the nominal rate, to place a reseed against the NDIS events\n", host_us() - (LONGLONG)(phc_after / 1000));
    }
    if (phc_before_ok && phc_after_ok) {
        report_phc(h, phc_before, qpc_before, "across the lifecycle");
    }
    cleanup.regs_unreadable = read_tsn(h, now, "after");
    (void)read_armed(h, &armed_after);
    ZeroMemory(&hw_after, sizeof(hw_after));
    (void)read_hw_state(h, &hw_after);
    for (i = 0; i < TSN_COUNT; i++) {
        kept += now[i] == held[i];
        cleanup.regs_changed += now[i] != base[i];
        if (now[i] != held[i]) {
            printf("    tsn change %s held=0x%08X after=0x%08X base=0x%08X\n", g_tsn[i].name, held[i], now[i], base[i]);
        }
    }
    mode_after = (now[TSN_TQAVCTRL] & I226_TQAVCTRL_TRANSMIT_MODE_MASK) != 0;
    consistent = (armed_after != 0) == mode_after && hw_after.hw_state == hw_before.hw_state && hw_after.capabilities == hw_before.capabilities;
    phc_fact = tas_phc_fact(phc_before_ok, phc_after_ok, phc_before, phc_after);
    printf(TAG " fact q1 tsnModeAfter=%d registersKept=%u of %u\n", mode_after, kept, (unsigned)TSN_COUNT);
    printf(TAG " fact q2 phc %s before=%llu after=%llu\n", tas_phc_fact_name(phc_fact), phc_before, phc_after);
    printf(TAG " fact q4 consistency %s tasArmed=%u tsnMode=%d hwState=%u to %u caps=0x%08X to 0x%08X oper=%d\n", consistent ? "agrees" : "DIFFERS", armed_after, mode_after, hw_before.hw_state,
           hw_after.hw_state, hw_before.capabilities, hw_after.capabilities, oper_status(ids[t].net_luid));
    printf(TAG " fact restoration registers changed from the pre-state=%u of %u, tx onWire=%d\n", cleanup.regs_changed, (unsigned)TSN_COUNT, wire);
    cleanup.lifecycle_shown = TRUE;
    cleanup.regs_compared = TSN_COUNT;
    cleanup.phc = phc_fact;
    cleanup.consistent = consistent;
    g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
    marker("run-end");
    printf(TAG " result: lifecycle=%s cleanup=%s; a NIC cycle on the target is owed, and it is the operator's\n", tas_lifecycle_name(outcome), tas_cleanup_name(g_now->cleanup));
    return 1;
}

static int lifecycle_target(const AVB_ADAPTER_IDENTITY_REQUEST *ids, const TAS_ADAPTER_FACTS *facts, int n, int t, BOOL dry_run, BOOL passive)
{
    HANDLE others[TAS_MAX_ADAPTERS];
    AVB_DRIVER_STATISTICS other_before[TAS_MAX_ADAPTERS], before, after;
    AVB_HW_SNAPSHOT_REQUEST snap_before, snap_after;
    AVB_HW_STATE_QUERY hw_before, hw_after;
    TAS_LIFECYCLE_OUTCOME outcome;
    TAS_LIFECYCLE_CLEANUP cleanup;
    TAS_PHC_FACT phc_fact;
    LARGE_INTEGER qpc_before;
    IN_ADDR ipv4;
    const WCHAR *name = NULL;
    USHORT name_bytes = 0;
    SOCKET sock = INVALID_SOCKET;
    HANDLE h;
    unsigned long long phc_before = 0, phc_after = 0;
    unsigned compared = 0, changed = 0, unreadable = 0, armed_before = 1, armed_after = 1, seen = 0, first, other_differs;
    int target_oper, rc = 3, i;
    BOOL other_link = FALSE, capture_alive = TRUE, phc_before_ok, phc_after_ok, traffic = FALSE, consistent, wire_up = FALSE;
    TAS_INSTANCE_RESULT instance;

    ZeroMemory(others, sizeof(others));
    ZeroMemory(&cleanup, sizeof(cleanup));
    g_now = &g_case[t];
    g_now->luid = ids[t].net_luid;
    g_now->did = ids[t].device_id;
    g_now->index = t;
    g_marker_luid = ids[t].net_luid;
    printf(TAG " --- adapter luid=0x%016llX did=0x%04X ---\n", ids[t].net_luid, ids[t].device_id);
    h = open_device();
    if (h == INVALID_HANDLE_VALUE || !bind_luid(h, ids[t].net_luid) || !find_addressing(ids[t].net_luid, &ipv4, &other_link) || !other_link) {
        printf(TAG " not run: needs a bound target with link and IPv4, and a separate management link\n");
        goto done;
    }
    /* A dry run mutates nothing, and after a NIC cycle a nonzero value is the fact it is there to record */
    if (!read_armed(h, &armed_before) || (armed_before != 0 && !dry_run)) {
        printf(TAG " not run: needs tasArmed=0\n");
        goto done;
    }
    if (!read_snapshot(h, &snap_before) || snap_before.net_luid != ids[t].net_luid || !read_hw_state(h, &hw_before)) {
        printf(TAG " not run: the register snapshot or hardware state is unreadable, or is not the target's\n");
        goto done;
    }
    instance = find_instance(h, ids[t].net_luid, &name, &name_bytes);
    if (instance != TAS_INSTANCE_FOUND) {
        printf(TAG " not run: no unique instance for the target (result %d); the trigger would not be exact\n", (int)instance);
        goto done;
    }
    printf(TAG " instance luid=0x%016llX name=%.*ls\n", ids[t].net_luid, (int)(name_bytes / sizeof(WCHAR)), name);
    swprintf_s(g_now->instance, NAME_CHARS, L"%.*ls", (int)(name_bytes / sizeof(WCHAR)), name);
    swprintf_s(g_now->if_guid, GUID_TEXT_CHARS, L"%.*ls", GUID_TEXT_CHARS - 1, name);   /* the layout tas_instance_find proved: {interface GUID}-... */
    management_report(ipv4);
    for (i = 0; i < n; i++) {
        if (i == t) {
            continue;
        }
        others[i] = open_device();
        if (others[i] == INVALID_HANDLE_VALUE || !bind_luid(others[i], ids[i].net_luid) || !read_stats(others[i], &other_before[i])) {
            printf(TAG " not run: adapter did=0x%04X cannot be watched, so effects on it could not be excluded\n", ids[i].device_id);
            goto done;
        }
    }
    sock = open_tx_socket(ipv4);
    if (sock == INVALID_SOCKET) {
        printf(TAG " not run: socket setup failed (%d)\n", WSAGetLastError());
        goto done;
    }
    if (dry_run) {
        static avb_u32 tsn[TSN_COUNT];
        avb_u32 tail;

        /* Registers, PHC and TAS state, read before the frames: a held adapter fails the observer proof, and is when these matter most. Nothing judged */
        printf(TAG " fact tasArmed=%u\n", armed_before);
        phc_pairs(h, "dry run");
        if (ids[t].device_id == CASE_B_EVIDENCE_DID) {
            (void)read_tsn(h, tsn, "dry run");
            (void)read_ring(h, "dry run", &tail);
        }
    }
    if (!observer_prove(ids, facts, n, t, h, sock, ipv4, &capture_alive)) {
        printf(TAG " not run: no other adapter saw this target's baseline frames, so nothing here could be proved; nothing mutated\n");
        goto done;
    }
    wire_up = TRUE;
    if (g_case_b) {
        rc = case_b_target(ids, n, t, h, sock, name, name_bytes, others, other_before, &capture_alive);
        goto done;
    }
    if (dry_run) {
        static TAS_WINDOW_TRACK resolution;

        /* What observing itself changed: the same reads as the real run, with no lifecycle event */
        ZeroMemory(&snap_after, sizeof(snap_after));
        ZeroMemory(&before, sizeof(before));
        (void)read_snapshot(h, &snap_after);
        tas_snapshot_compare(&snap_before, &snap_after, &compared, &changed, &unreadable);
        other_differs = report_others(ids, others, other_before, n, t);
        print_clock(h, "dry run");
        printf(TAG " fact register compared=%u changed=%u unreadable=%u\n", compared, changed, unreadable);
        if (!read_stats(h, &before)) {
            printf(TAG " statistics unreadable (error %lu)\n", GetLastError());
            rc = 1;
            goto done;
        }
        tas_window_init(&resolution, before.FilterPauseCount, before.FilterRestartCount);
        if (!poll_baseline(h, &resolution, FALSE) || changed != 0 || unreadable != 0 || compared == 0 || other_differs != 0) {
            rc = 1;
            goto done;
        }
        rc = 0;
        printf(TAG " dry run complete: instance and observer proved, polling resolution measured, nothing triggered\n");
        goto done;
    }

    /* The state that matters is the one just before the trigger */
    print_clock(h, "before");
    phc_pairs(h, "before");
    observer_line("before");
    phc_before_ok = read_systim(h, &phc_before);
    QueryPerformanceCounter(&qpc_before);
    if (!read_stats(h, &before) || oper_status(ids[t].net_luid) != IfOperStatusUp) {
        printf(TAG " not run: statistics or link changed before the trigger; nothing mutated\n");
        goto done;
    }
    printf("    lifecycle hostUs=%lld before trigger pause=%llu restart=%llu generation=%llu attach=%llu detach=%llu gauge=%llu\n", host_us(), before.FilterPauseCount,
           before.FilterRestartCount, before.PauseRestartGeneration, before.FilterAttachCount, before.FilterDetachCount, before.OutstandingSendNBLs);
    lifecycle_measure(h, &before, name, name_bytes, passive);
    Sleep(SETTLE_MS);   /* FilterRestartCount moves on entry, before the bring-up, and no signal marks its end */

    /* #350: no MMIO unless the filter is Running. Frames come first: they need no MMIO, and they are the evidence for it */
    outcome = lifecycle_read(h, &before, &after, FALSE);
    first = g_next_seq;
    if (outcome == TAS_LIFECYCLE_RESTART_ENTERED) {
        seen = tx_step(h, ids[t].net_luid, sock, BASELINE_MS, "after the restart", g_next_seq, &capture_alive);
        traffic = capture_alive && g_next_seq != first && (unsigned long long)seen * 100 >= (unsigned long long)(g_next_seq - first) * WIRE_MIN_PERCENT;
        dump_baseline_head(first);
        cleanup.tx_probed = capture_alive && g_next_seq != first;
        cleanup.tx_shown = traffic;
        outcome = lifecycle_read(h, &before, &after, traffic);
    }
    other_differs = report_others(ids, others, other_before, n, t);
    cleanup.others_changed = other_differs;
    g_now->outcome = outcome;
    g_now->others_changed = other_differs;
    g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
    if (outcome != TAS_LIFECYCLE_RESUMED) {
        printf(TAG " Running not shown: no MMIO, so register state, PHC and consistency stay unmeasured\n");
        rc = 1;
        printf(TAG " result: exit=%d lifecycle=%s cleanup=%s; recovery is owed, and it is the operator's: this mode never cycles the adapter\n", rc,
               tas_lifecycle_name(outcome), tas_cleanup_name(tas_lifecycle_cleanup(&cleanup)));
        goto done;
    }

    print_clock(h, "after");
    phc_pairs(h, "after");
    observer_line("after");
    phc_after_ok = read_systim(h, &phc_after);
    if (phc_after_ok) {
        printf("    implied PHC zero hostUs=%lld at the nominal rate, to place a reseed against the NDIS events\n", host_us() - (LONGLONG)(phc_after / 1000));
    }
    if (phc_before_ok && phc_after_ok) {
        report_phc(h, phc_before, qpc_before, "across the lifecycle");
    }

    /* The four facts of #361, each on its own. Case A has no restoration event, so none of this is COMPLETE_RESTORATION */
    ZeroMemory(&snap_after, sizeof(snap_after));
    ZeroMemory(&hw_after, sizeof(hw_after));
    (void)read_snapshot(h, &snap_after);
    (void)read_hw_state(h, &hw_after);
    (void)read_armed(h, &armed_after);
    target_oper = oper_status(ids[t].net_luid);
    tas_snapshot_compare(&snap_before, &snap_after, &compared, &changed, &unreadable);
    for (i = 0; i < (int)AVB_HWREG_COUNT; i++) {
        if (snap_before.entries[i].value != snap_after.entries[i].value || snap_before.entries[i].state != snap_after.entries[i].state) {
            printf("    snapshot id=%u before=0x%08X state=%u after=0x%08X state=%u\n", snap_before.entries[i].id, snap_before.entries[i].value, snap_before.entries[i].state,
                   snap_after.entries[i].value, snap_after.entries[i].state);
        }
    }
    phc_fact = tas_phc_fact(phc_before_ok, phc_after_ok, phc_before, phc_after);
    consistent = armed_after == 0 && hw_after.hw_state == hw_before.hw_state && hw_after.capabilities == hw_before.capabilities && snap_after.net_luid == ids[t].net_luid;
    printf(TAG " fact register compared=%u changed=%u unreadable=%u\n", compared, changed, unreadable);
    printf(TAG " fact tx onWire=%d framesSeen=%u of %u gauge=%llu\n", traffic, seen, g_next_seq - first, after.OutstandingSendNBLs);
    printf(TAG " fact phc %s before=%llu after=%llu\n", tas_phc_fact_name(phc_fact), phc_before, phc_after);
    printf(TAG " fact consistency %s tasArmed=%u hwState=%u to %u caps=0x%08X to 0x%08X oper=%d\n", consistent ? "agrees" : "DIFFERS", armed_after, hw_before.hw_state, hw_after.hw_state,
           hw_before.capabilities, hw_after.capabilities, target_oper);

    if (changed != 0 || unreadable != 0 || compared == 0 || !traffic || !consistent || other_differs != 0) {
        rc = 1;
    } else {
        rc = phc_fact == TAS_PHC_DISCONTINUOUS ? 2 : 0;
    }
    cleanup.lifecycle_shown = TRUE;
    cleanup.regs_compared = compared;
    cleanup.regs_changed = changed;
    cleanup.regs_unreadable = unreadable;
    cleanup.phc = phc_fact;
    cleanup.consistent = consistent;
    g_now->cleanup = tas_lifecycle_cleanup(&cleanup);
    marker("run-end");
    printf(TAG " result: exit=%d lifecycle=%s cleanup=%s; recovery, if needed, is the operator's: this mode never cycles the adapter\n", rc,
           tas_lifecycle_name(outcome), tas_cleanup_name(tas_lifecycle_cleanup(&cleanup)));

done:
    if (wire_up) {
        wire_stop();
    }
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
    for (i = 0; i < (int)TAS_MAX_ADAPTERS; i++) {
        if (others[i] != NULL && others[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(others[i]);
        }
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    return rc;
}

static void adapter_facts(const AVB_ADAPTER_IDENTITY_REQUEST *id, TAS_ADAPTER_FACTS *f)
{
    AVB_HW_STATE_QUERY q;
    IN_ADDR ipv4;
    HANDLE bound = open_device();

    ZeroMemory(f, sizeof(*f));
    f->net_luid = id->net_luid;
    f->identity_ok = TRUE;
    f->hw_caps = id->capabilities;
    if (bound != INVALID_HANDLE_VALUE) {
        if (bind_luid(bound, id->net_luid) && read_hw_state(bound, &q)) {
            f->hw_state_ok = TRUE;
            f->driver_caps = AVB_HW_STATE_DRIVER_CAPABILITIES(q);
            f->hw_state = q.hw_state;
        }
        CloseHandle(bound);
    }
    f->link_up = find_addressing(id->net_luid, &ipv4, &f->separate_management_link);
}

/* ETW: one file session for the whole run, QPC clocked, decoded after it stops */
typedef struct { LONGLONG ts; unsigned state; WCHAR instance[NAME_CHARS], miniport[GUID_TEXT_CHARS]; } NDIS_EVENT;
typedef struct { LONGLONG ts, qpc; avb_u64 luid; BOOL run_end; } MARK;
typedef struct { EVENT_TRACE_PROPERTIES p; WCHAR logger[64]; WCHAR file[MAX_PATH]; } TRACE_PROPS;

static NDIS_EVENT g_ndis[MAX_NDIS_EVENTS];
static MARK g_marks[MAX_MARKS];
static unsigned g_ndis_count, g_ndis_dropped, g_mark_count, g_provider_events, g_stop_events;
static unsigned g_ndis_printed;
static BOOL g_decoded;
static ULONG g_events_lost;
static TRACE_PROPS g_props;
static TRACEHANDLE g_session;
static volatile LONG g_session_open;
static WCHAR g_etl[MAX_PATH];

static void props_init(void)
{
    ZeroMemory(&g_props, sizeof(g_props));
    g_props.p.Wnode.BufferSize = sizeof(g_props);
    g_props.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    g_props.p.Wnode.ClientContext = 1;   /* QPC, the clock of every poll here */
    g_props.p.BufferSize = 64;
    g_props.p.MinimumBuffers = 16;
    g_props.p.FlushTimer = 1;
    g_props.p.LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    g_props.p.LoggerNameOffset = (ULONG)offsetof(TRACE_PROPS, logger);
    g_props.p.LogFileNameOffset = (ULONG)offsetof(TRACE_PROPS, file);
    wcscpy_s(g_props.file, MAX_PATH, g_etl);
}

/** Beside the other evidence: logs under the repository the binary was built in, else the temporary directory */
static void etl_path(void)
{
    WCHAR dir[MAX_PATH], *build;
    SYSTEMTIME t;

    GetLocalTime(&t);
    dir[0] = L'\0';
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    build = wcsstr(dir, L"\\build\\");
    if (build != NULL) {
        wcscpy_s(build, MAX_PATH - (size_t)(build - dir), L"\\logs\\");
    } else {
        GetTempPathW(MAX_PATH, dir);
    }
    swprintf_s(g_etl, MAX_PATH, L"%lsdiag-i364-casea-%04u%02u%02u_%02u%02u%02u.etl", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

static void trace_stop(void)
{
    if (InterlockedExchange(&g_session_open, 0) != 0) {
        props_init();
        if (ControlTraceW(g_session, NULL, &g_props.p, EVENT_TRACE_CONTROL_STOP) == ERROR_SUCCESS) {
            g_events_lost = g_props.p.EventsLost + g_props.p.RealTimeBuffersLost;
        }
    }
}

static BOOL WINAPI on_console_ctrl(DWORD type)
{
    (void)type;
    trace_stop();
    return FALSE;
}

static BOOL trace_start(void)
{
    ULONG status;

    etl_path();
    props_init();
    status = StartTraceW(&g_session, TRACE_NAME, &g_props.p);
    if (status == ERROR_ALREADY_EXISTS) {
        printf(TAG " a session of an earlier killed run was still open and is closed here\n");
        props_init();
        (void)ControlTraceW(0, TRACE_NAME, &g_props.p, EVENT_TRACE_CONTROL_STOP);
        props_init();
        status = StartTraceW(&g_session, TRACE_NAME, &g_props.p);
    }
    if (status != ERROR_SUCCESS) {
        printf(TAG " NDIS trace not running (error %lu)\n", status);
        return FALSE;
    }
    InterlockedExchange(&g_session_open, 1);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    status = EnableTraceEx2(g_session, &g_ndis_provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE, g_case_b ? NDIS_KEYWORDS | NDIS_KEYWORD_PROTOCOL : NDIS_KEYWORDS, 0, 0, NULL);
    if (status != ERROR_SUCCESS) {
        printf(TAG " NDIS provider not enabled (error %lu)\n", status);
        trace_stop();
        return FALSE;
    }
    status = EnableTraceEx2(g_session, &g_marker_provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE, ~0ULL, 0, 0, NULL);
    if (status != ERROR_SUCCESS) {
        printf(TAG " marker provider not enabled (error %lu)\n", status);
        trace_stop();
        return FALSE;
    }
    printf(TAG " trace %ls\n", g_etl);
    return TRUE;
}

/** One property as text. A GUID property becomes its braced form, so it compares with the instance name */
static void event_text(PEVENT_RECORD r, const WCHAR *name, WCHAR *out, size_t chars)
{
    PROPERTY_DATA_DESCRIPTOR d;
    BYTE data[2 * NAME_CHARS];
    ULONG size = 0;

    out[0] = L'\0';
    ZeroMemory(&d, sizeof(d));
    d.PropertyName = (ULONGLONG)(ULONG_PTR)name;
    d.ArrayIndex = ULONG_MAX;
    if (TdhGetPropertySize(r, 0, NULL, 1, &d, &size) != ERROR_SUCCESS || size == 0 || size > sizeof(data) ||
        TdhGetProperty(r, 0, NULL, 1, &d, size, data) != ERROR_SUCCESS) {
        return;
    }
    if (size == sizeof(GUID) && wcscmp(name, L"MiniportIfGuid") == 0) {
        const GUID *g = (const GUID *)data;

        swprintf_s(out, chars, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
                   g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    } else {
        swprintf_s(out, chars, L"%.*ls", (int)(size / sizeof(WCHAR)), (const WCHAR *)data);
    }
}

static unsigned event_number(PEVENT_RECORD r, const WCHAR *name)
{
    PROPERTY_DATA_DESCRIPTOR d;
    ULONG value = 0, size = 0;

    ZeroMemory(&d, sizeof(d));
    d.PropertyName = (ULONGLONG)(ULONG_PTR)name;
    d.ArrayIndex = ULONG_MAX;
    if (TdhGetPropertySize(r, 0, NULL, 1, &d, &size) == ERROR_SUCCESS && size != 0 && size <= sizeof(value)) {
        (void)TdhGetProperty(r, 0, NULL, 1, &d, size, (PBYTE)&value);
    }
    return (unsigned)value;
}

static void WINAPI on_event(PEVENT_RECORD r)
{
    if (memcmp(&r->EventHeader.ProviderId, &g_marker_provider, sizeof(GUID)) == 0) {
        WCHAR label[32], text[MARKER_CHARS];
        unsigned long long luid = 0;
        long long qpc = 0;

        swprintf_s(text, MARKER_CHARS, L"%.*ls", (int)(r->UserDataLength / sizeof(WCHAR)), (const WCHAR *)r->UserData);
        if (g_mark_count < MAX_MARKS && swscanf_s(text, L"i364 %31ls luid=0x%llX qpc=%lld", label, (unsigned)_countof(label), &luid, &qpc) == 3) {
            g_marks[g_mark_count].ts = r->EventHeader.TimeStamp.QuadPart;
            g_marks[g_mark_count].qpc = qpc;
            g_marks[g_mark_count].luid = luid;
            g_marks[g_mark_count].run_end = wcscmp(label, L"run-end") == 0;
            g_mark_count += wcscmp(label, L"run-end") == 0 || wcscmp(label, L"trigger-issued") == 0;
        }
    } else if (memcmp(&r->EventHeader.ProviderId, &g_ndis_provider, sizeof(GUID)) == 0) {
        USHORT id = r->EventHeader.EventDescriptor.Id;

        g_provider_events++;
        g_stop_events += id == 10007 || id == 10058 || id == 10050;
        if (id == 10312) {
            if (g_ndis_count < MAX_NDIS_EVENTS) {
                NDIS_EVENT *e = &g_ndis[g_ndis_count++];

                e->ts = r->EventHeader.TimeStamp.QuadPart;
                e->state = event_number(r, L"State");
                event_text(r, L"FilterInstanceName", e->instance, NAME_CHARS);
                event_text(r, L"MiniportIfGuid", e->miniport, GUID_TEXT_CHARS);
            } else {
                g_ndis_dropped++;
            }
        }
    }
}

static BOOL trace_decode(void)
{
    EVENT_TRACE_LOGFILEW file;
    TRACEHANDLE h;
    ULONG status;
    unsigned i;

    ZeroMemory(&file, sizeof(file));
    file.LogFileName = g_etl;
    file.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    file.EventRecordCallback = on_event;
    h = OpenTraceW(&file);
    if (h == INVALID_PROCESSTRACE_HANDLE) {
        printf(TAG " trace not readable (error %lu)\n", GetLastError());
        return FALSE;
    }
    status = ProcessTrace(&h, 1, NULL, NULL);
    CloseTrace(h);
    printf(TAG " trace status=%lu providerEvents=%u state10312=%u dropped=%u stopEvents=%u lost=%lu markers=%u\n", status, g_provider_events, g_ndis_count, g_ndis_dropped,
           g_stop_events, g_events_lost, g_mark_count);
    for (i = g_ndis_printed; i < g_ndis_count && i < 64; i++) {
        printf("    ndis 10312 ts=%lld state=%u instance=%ls miniport=%ls\n", g_ndis[i].ts, g_ndis[i].state, g_ndis[i].instance, g_ndis[i].miniport);
    }
    g_ndis_printed = g_ndis_count;
    g_decoded = status == ERROR_SUCCESS;
    return g_decoded;
}

static void verdict(const char *tag, const char *tc, int adapter, avb_u16 did, const char *reason)
{
    printf("[%s] %s/adapter:%d did=0x%04X%s%s\n", tag, tc, adapter, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
}

/** The NDIS events of one instance against its markers and counters. outside counts those beyond the window */
static TAS_SEQ_CLASS case_sequence(const CASE_RESULT *c, BOOL decoded, TAS_SEQ_EVIDENCE *ev, LONGLONG *marker_offset, unsigned *events_outside)
{
    LONGLONG start = 0, end = MAXLONGLONG, offset = 0;
    unsigned i, outside = 0;

    ZeroMemory(ev, sizeof(*ev));
    ev->provider_events = decoded ? g_provider_events : 0;
    ev->events_lost = g_events_lost + g_ndis_dropped;
    for (i = 0; i < g_mark_count; i++) {
        if (g_marks[i].luid == c->luid && g_marks[i].run_end) {
            end = g_marks[i].ts;
        } else if (g_marks[i].luid == c->luid) {
            start = g_marks[i].ts;
            offset = g_marks[i].qpc - g_marks[i].ts;
            ev->have_window = TRUE;
        }
    }
    for (i = 0; i < g_ndis_count; i++) {
        if (_wcsicmp(g_ndis[i].instance, c->instance) == 0 && _wcsicmp(g_ndis[i].miniport, c->if_guid) == 0) {
            if (ev->have_window && g_ndis[i].ts >= start && g_ndis[i].ts <= end) {
                if (ev->count < TAS_SEQ_MAX_EVENTS) {
                    ev->value[ev->count] = g_ndis[i].state;
                    ev->qpc[ev->count] = g_ndis[i].ts + offset;
                }
                ev->count++;
            } else {
                outside++;
            }
        }
    }
    ev->pause_edge = c->track.pause.seen;
    ev->restart_edge = c->track.restart.seen;
    ev->pause = c->track.pause;
    ev->restart = c->track.restart;
    ev->slack = c->marker_bracket;
    ev->outcome = c->outcome;
    *marker_offset = offset;
    *events_outside = outside;
    return tas_seq_classify(ev);
}

/**
 * Case B reads registers only once NDIS has shown Running, so its trace ends here and a second one covers the rest of the run. The
 * events of both go to the same verdicts. Returns whether the reads may go ahead: this permits a read and claims no transition.
 */
static BOOL trace_gate(const CASE_RESULT *c)
{
    static TAS_SEQ_EVIDENCE e;
    LONGLONG offset = 0;
    unsigned outside = 0;
    TAS_SEQ_CLASS sequence;
    BOOL decoded, gate;

    trace_stop();
    decoded = trace_decode();
    sequence = case_sequence(c, decoded, &e, &offset, &outside);
    gate = tas_hold_may_read(sequence, c->outcome, c->others_changed);
    printf(TAG " read gate %s sequence=%s lifecycle=%s othersChanged=%u\n", gate ? "open" : "closed", tas_seq_name(sequence), tas_lifecycle_name(c->outcome), c->others_changed);
    if (!trace_start()) {
        printf(TAG " no trace for the rest of the run, so a later NDIS reset would not be seen\n");
    }
    return gate;
}

/** The three results of one adapter, kept apart. Returns nonzero when any is not a pass */
static int report_case(const CASE_RESULT *c, BOOL decoded)
{
    static TAS_SEQ_EVIDENCE e;
    TAS_SEQ_CLASS sequence;
    LONGLONG offset = 0;
    BOOL claim;
    char why[256];
    unsigned i, outside = 0;
    int bad = 0;

    sequence = case_sequence(c, decoded, &e, &offset, &outside);
    claim = tas_state_claim(sequence, c->outcome, c->others_changed);
    printf(TAG " state sequence luid=0x%016llX class=%s events=%u outsideWindow=%u markerOffsetTicks=%lld values=", c->luid, tas_seq_name(sequence), e.count, outside, offset);
    for (i = 0; i < e.count && i < TAS_SEQ_MAX_EVENTS; i++) {
        printf("%s%u", i ? "," : "", e.value[i]);
    }
    if (sequence == TAS_SEQ_CONSISTENT) {
        printf(" pausedNdisUs=%.1f bracketUs=%.1f", ticks_to_us(tas_seq_paused_ticks(&e)), ticks_to_us(e.slack));
    }
    printf("\n");

    sprintf_s(why, sizeof(why), "lifecycle=%s othersChanged=%u", tas_lifecycle_name(c->outcome), c->others_changed);
    if (c->outcome == TAS_LIFECYCLE_NOT_TRIGGERED) {
        verdict("BLOCKED", g_tc_run, c->index, c->did, g_case_b ? "no Pause entry within the bound under SETUP_TAS: this path ran in Case A, and the status of NdisFRestartFilter is not visible"
                                                                 : "the trigger reached nothing: #364 is BLOCKED until another trigger exists");
        bad = 1;
    } else if (c->outcome == TAS_LIFECYCLE_RESUMED && c->others_changed == 0) {
        verdict("PASS", g_tc_run, c->index, c->did, why);
    } else if (g_case_b && c->outcome == TAS_LIFECYCLE_RESTART_ENTERED && c->others_changed == 0) {
        verdict("INCONCLUSIVE", g_tc_run, c->index, c->did, why);   /* a hold keeps frames off the wire, so a missing frame shows nothing */
        bad = 1;
    } else {
        verdict("FAIL", g_tc_run, c->index, c->did, why);
        bad = 1;
    }
    sprintf_s(why, sizeof(why), "sequence=%s lifecycle=%s", tas_seq_name(sequence), tas_lifecycle_name(c->outcome));
    verdict(claim ? "PASS" : "INCONCLUSIVE", g_tc_state, c->index, c->did, why);
    bad |= !claim;
    if (g_stop_events != 0) {
        verdict("CLEANUP_FAILED", g_tc_clean, c->index, c->did, "NDIS reported a reset, a halt or a failed restart");
        bad = 1;
    } else if (c->cleanup == TAS_RESTORATION_VERIFIED) {
        verdict("PASS", g_tc_clean, c->index, c->did, "every restoration fact holds");
    } else {
        verdict(tas_cleanup_name(c->cleanup), g_tc_clean, c->index, c->did,
                g_case_b ? "restoration facts above; this test cannot release a hold, so a NIC cycle on the target is owed" : "restoration facts above; the PHC has no hardware-derived tolerance");
        bad = 1;
    }
    return bad;
}

/** Case A of #364 on every eligible adapter */
static int lifecycle_case_a(BOOL dry_run, BOOL passive)
{
    static AVB_ADAPTER_IDENTITY_REQUEST ids[TAS_MAX_ADAPTERS];
    static TAS_ADAPTER_FACTS facts[TAS_MAX_ADAPTERS];
    TAS_PLAN_CLASS classes[TAS_MAX_ADAPTERS];
    int results[TAS_MAX_ADAPTERS];
    AVB_ADAPTER_IDENTITY_REQUEST id;
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    HANDLE h;
    UINT32 count = 1, index;
    unsigned n = 0, i, eligible, ran = 0;
    DWORD br = 0;
    BOOL traced = FALSE, decoded = FALSE, pending, setup_failed = FALSE;
    int rc, bad = 0;

    if (EventRegister(&g_marker_provider, NULL, NULL, &g_marker) != ERROR_SUCCESS) {
        g_marker = 0;   /* EventWriteString then fails harmlessly, and the trace shows no window */
    }
    if (g_case_b) {
        printf(TAG " #364 Case B: SETUP_TAS only, frames held%s, one FilterPause and FilterRestart per eligible adapter\n", g_case_b_drained ? ", gauge 0 at the trigger" : "");
    } else {
        printf(TAG " #364 Case A: no schedule, one FilterPause and FilterRestart per eligible adapter%s\n", dry_run ? " (dry run, no trigger)" : passive ? " (passive polling)" : "");
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[BLOCKED] %s: Winsock unavailable\n", g_tc_run);
        rc = 3;
        goto unregister;
    }
    h = open_device();
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] %s: cannot open %s (error %lu)\n", g_tc_run, DEVICE_NAME, GetLastError());
        rc = 3;
        goto winsock;
    }
    if (!DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) ||
        (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) == 0) {
        printf("[BLOCKED] %s: #364 is a Debug-only investigation\n", g_tc_run);
        CloseHandle(h);
        rc = 3;
        goto winsock;
    }
    for (index = 0; index < count && n < TAS_MAX_ADAPTERS && identity_at(h, index, &id); index++) {
        count = id.count;
        ids[n++] = id;
    }
    CloseHandle(h);
    if (n == 0 || n != count) {
        printf("[BLOCKED] %s: %u of %u adapters enumerated, so the plan would not cover them all\n", g_tc_run, n, count);
        rc = 3;
        goto winsock;
    }
    for (i = 0; i < n; i++) {
        adapter_facts(&ids[i], &facts[i]);
    }
    eligible = tas_plan_build(facts, n, classes);
    for (i = 0; i < n; i++) {
        results[i] = 3;
        printf(TAG " plan luid=0x%016llX did=0x%04X hwCaps=0x%08X driverCaps=0x%08X hwState=%u link=%d managementLink=%d class=%s\n", facts[i].net_luid, ids[i].device_id, facts[i].hw_caps,
               facts[i].driver_caps, facts[i].hw_state, facts[i].link_up, facts[i].separate_management_link, tas_plan_name(classes[i]));
    }
    traced = trace_start();
    if (!traced && !dry_run) {
        printf("[BLOCKED] %s: no NDIS trace, so the trigger is not issued\n", g_tc_run);
        rc = 3;
        goto winsock;
    }
    for (i = 0; i < n; i++) {
        if (classes[i] == TAS_PLAN_ELIGIBLE && g_case_b && g_session_open == 0) {
            printf(TAG " not run: the trace of an earlier target did not restart, so nothing here could be proved\n");
        } else if (classes[i] == TAS_PLAN_ELIGIBLE) {
            results[i] = lifecycle_target(ids, facts, (int)n, (int)i, dry_run, passive);
        } else {
            verdict(tas_plan_name(classes[i]), g_tc_run, (int)i, ids[i].device_id, "not a target");
        }
    }
    pending = g_session_open != 0;
    trace_stop();
    decoded = traced && (pending ? trace_decode() : g_decoded);   /* Case B may have decoded its only trace at the read gate */
    for (i = 0; i < n; i++) {
        if (classes[i] == TAS_PLAN_ELIGIBLE) {
            ran += g_case[i].triggered;
            if (g_case[i].triggered) {
                bad |= report_case(&g_case[i], decoded);
            } else if (g_case[i].failed_setup) {
                setup_failed = TRUE;
                bad = 1;
                verdict(tas_cleanup_name(g_case[i].cleanup), g_tc_clean, (int)i, ids[i].device_id, "SETUP_TAS may have changed the adapter and no trigger followed; a NIC cycle on the target is owed");
            } else {
                /* A dry run passes on what it proved. That nothing was triggered is true of every way out, so it can never be the reason */
                BOOL proved = dry_run && results[i] == 0;

                verdict(proved ? "PASS" : dry_run ? "INCONCLUSIVE" : "BLOCKED", g_tc_run, (int)i, ids[i].device_id,
                        proved ? "dry run: instance and observer proved, nothing triggered" : dry_run ? "dry run stopped early, see the lines above; nothing was triggered" : "stopped before the trigger, see the lines above");
                bad |= !proved;
            }
        }
    }
    rc = tas_plan_exit(classes, results, n);
    printf(TAG " summary adapters=%u eligible=%u triggered=%u toolExit=%d\n", n, eligible, ran, rc);
    if (eligible == 0) {
        rc = 2;
    } else if (ran == 0 && !dry_run && !setup_failed) {
        rc = 3;
    } else if (bad || rc != 0) {
        rc = 1;
    }

winsock:
    trace_stop();
    WSACleanup();
unregister:
    if (g_marker != 0) {
        EventUnregister(g_marker);
    }
    return rc;
}

/**
 * TC-CASEA-004: a restart request that matches no instance must be refused. Before the fix the driver reported success for it, and
 * dropped the status of NdisFRestartFilter, so Case B's trigger could not say whether NDIS accepted it. The name is checked against
 * every instance first, with the driver's own matching rule, and nothing is sent if one would match; the Pause and Restart counters of
 * every adapter must stay unchanged. Needs no Debug-only IOCTL, so it runs against Debug and Release drivers.
 */
static int probe_restart_status(void)
{
    static const WCHAR probe[] = L"{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}-{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}-0000";
    static BYTE list[INSTANCE_LIST_BYTES];
    static AVB_ADAPTER_IDENTITY_REQUEST ids[TAS_MAX_ADAPTERS];
    AVB_DRIVER_STATISTICS before[TAS_MAX_ADAPTERS], after;
    HANDLE bound[TAS_MAX_ADAPTERS], h = open_device();
    AVB_ADAPTER_IDENTITY_REQUEST id;
    DWORD br = 0, bytes = (DWORD)(sizeof(probe) - sizeof(WCHAR));
    ULONG ndis_status = 0;
    UINT32 count = 1, index;
    unsigned n = 0, i, moved = 0;
    BOOL ok;
    DWORD error;
    TAS_PROBE_CLASS c;
    int rc = 3;

    ZeroMemory(bound, sizeof(bound));
    printf(TAG " #364 restart status probe: one IOCTL_FILTER_RESTART_ONE_INSTANCE for a name no instance has\n");
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] TC-CASEA-004: cannot open %s (error %lu)\n", DEVICE_NAME, GetLastError());
        return 3;
    }
    if (!DeviceIoControl(h, IOCTL_FILTER_ENUERATE_ALL_INSTANCES, NULL, 0, list, sizeof(list), &br, NULL) || br == 0) {
        printf("[BLOCKED] TC-CASEA-004: instance list unreadable or empty (error %lu); nothing sent\n", GetLastError());
        goto done;
    }
    if (tas_name_matches_any(list, br, (const BYTE *)probe, bytes)) {
        printf("[BLOCKED] TC-CASEA-004: an instance begins the probe name or the list does not parse; nothing sent\n");
        goto done;
    }
    for (index = 0; index < count && n < TAS_MAX_ADAPTERS && identity_at(h, index, &id); index++) {
        count = id.count;
        ids[n] = id;
        bound[n] = open_device();
        if (bound[n] == INVALID_HANDLE_VALUE || !bind_luid(bound[n], id.net_luid) || !read_stats(bound[n], &before[n])) {
            printf("[BLOCKED] TC-CASEA-004: adapter did=0x%04X cannot be watched, so a restart could not be excluded; nothing sent\n", id.device_id);
            n++;
            goto done;
        }
        n++;
    }
    if (n == 0 || n != count) {
        printf("[BLOCKED] TC-CASEA-004: %u of %u adapters watched; nothing sent\n", n, count);
        goto done;
    }
    br = 0;
    ok = DeviceIoControl(h, IOCTL_FILTER_RESTART_ONE_INSTANCE, (LPVOID)probe, bytes, &ndis_status, sizeof(ndis_status), &br, NULL);
    error = ok ? ERROR_SUCCESS : GetLastError();
    Sleep(SETTLE_MS);   /* a restart that did happen moves the counters within milliseconds in Case A */
    for (i = 0; i < n; i++) {
        BOOL changed;

        ZeroMemory(&after, sizeof(after));
        changed = !read_stats(bound[i], &after) || after.FilterPauseCount != before[i].FilterPauseCount || after.FilterRestartCount != before[i].FilterRestartCount;
        moved += changed ? 1u : 0u;
        printf("    adapter did=0x%04X pause=%llu restart=%llu %s\n", ids[i].device_id, after.FilterPauseCount, after.FilterRestartCount, changed ? "CHANGED" : "unchanged");
    }
    c = tas_probe_verdict(ok, error);
    printf(TAG " probe returned ok=%d error=%lu ndisStatus=0x%08lX statusBytes=%lu adaptersChanged=%u\n", ok, error, ndis_status, br, moved);
    if (moved != 0) {
        printf("[FAIL] TC-CASEA-004: a request for no instance changed an adapter's lifecycle counters\n");
        rc = 1;
    } else if (c == TAS_PROBE_REFUSED) {
        printf("[PASS] TC-CASEA-004: refused as not found, and nothing restarted\n");
        rc = 0;
    } else if (c == TAS_PROBE_SUCCEEDED_FOR_NOTHING) {
        printf("[FAIL] TC-CASEA-004: the driver reported success for a request that matched no instance\n");
        rc = 1;
    } else {
        printf("[FAIL] TC-CASEA-004: refused with error %lu, where not found (%lu) is required\n", error, (DWORD)ERROR_NOT_FOUND);
        rc = 1;
    }

done:
    for (i = 0; i < n; i++) {
        if (bound[i] != NULL && bound[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(bound[i]);
        }
    }
    CloseHandle(h);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--probe-restart-status") == 0) {
        return probe_restart_status();
    }
    BOOL dry_run = argc == 2 && strcmp(argv[1], "--dry-run") == 0, passive = argc == 2 && strcmp(argv[1], "--passive") == 0;

    g_case_b_drained = argc == 2 && strcmp(argv[1], "--case-b-drained") == 0;
    g_case_b = g_case_b_drained || (argc == 2 && strcmp(argv[1], "--case-b") == 0);
    if (g_case_b) {
        g_tc_run = "TC-CASEB-002";
        g_tc_state = "TC-CASEB-003";
        g_tc_clean = "TC-CASEB-004";
    }
    if (argc > 2 || (argc == 2 && !dry_run && !passive && !g_case_b)) {
        printf("[BLOCKED] %s: usage: test_tas_lifecycle_case_a [--dry-run | --passive | --case-b | --case-b-drained | --probe-restart-status]\n", g_tc_run);
        return 3;
    }
    return lifecycle_case_a(dry_run, passive);
}

