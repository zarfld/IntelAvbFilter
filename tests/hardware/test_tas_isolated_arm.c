/**
 * @file test_tas_isolated_arm.c
 * @brief Isolated SETUP_TAS on a real adapter: schedule state, TX health and restoration (#328)
 *
 * Runs once for every adapter advertising INTEL_CAP_TSN_TAS. No FP, no PTM, no unload, no service stop.
 *
 *   TC-TASARM-001  after SETUP_TAS the adapter holds the requested schedule
 *   TC-TASARM-002  TX stays healthy while armed
 *   TC-TASARM-003  a NIC cycle restores everything observed, judged by tas_restoration_eval.h
 *
 * Hardware mutations: SETUP_TAS sets TQAVCTRL.TRANSMIT_MODE and ENHANCED_QAV and writes QBVCYCLET. Restoration
 * disables and enables the adapter device node, because DISARM_TAS has no hardware path (it returns -ENOTSUP).
 * Bounded broadcast UDP is sent on the adapter under test. The NIC cycle itself is a mutation: it detaches the filter
 * and may reset PTP state, so the PHC is compared too.
 *
 * Limits: the driver exposes no per-queue completion count. Per-queue progress comes from TDH/TDT, which show fetch
 * progress only (manual section 8.11.13); completions are the adapter-wide OutstandingSendNBLs gauge, a lower bound on
 * forwarded NBLs not completed back to the filter. READ_REGISTER exists only in Debug drivers, so a Release driver is BLOCKED.
 *
 * Armed TX stalled about 5 s after SETUP_TAS on 2026-11-05 (0x125B, driver F3CC845E). The armed window samples densely
 * across that time and all registers are dumped before and after it. --armed-period-ms N changes the send period of
 * the armed window only, which separates a stall that follows time from one that follows the frame count.
 *
 * --stall-probe N tags every UDP frame with a sequence number in an N byte payload and reports frames, bytes, descriptors,
 * ring occupancy and outstanding sends separately, with wire observers from tas_wire_observer.h. Without it nothing of this
 * runs. A probe whose observers cannot see the baseline frames is BLOCKED before SETUP_TAS.
 *
 * Exit: 0 all passed, 1 any failure or restoration verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #328
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../../intel-ethernet-regs/gen/i226_regs.h"
#include "../ioctl/tas_restoration_eval.h"
#include "tas_wire_observer.h"

#define DEVICE_NAME          "\\\\.\\IntelAvbFilter"
#define TX_QUEUES            4
#define TX_SOCKETS           8
#define TX_PERIOD_MS         50
#define MAX_ARMED_PERIOD_MS  1000
#define MAX_PROBE_PAYLOAD    1400
#define FRAME_HEADER_BYTES   42
#define TX_DESCRIPTOR_BYTES  16
#define WIRE_BASELINE_MIN_PERCENT 90
#define TX_DISCARD_PORT      9
#define TX_DRAIN_MS          5000
#define GAUGE_SLACK          2u
#define NS_PER_SEC           1000000000LL
#define PHC_TOLERANCE_NS     (2 * NS_PER_SEC)
#define DISABLE_BOUND_MS     120000
#define ENABLE_BOUND_MS      60000
#define RETURN_BOUND_MS      60000
#define SETTLE_MS            5000
#define ALL_ONES             0xFFFFFFFFu
#define NET_CLASS_KEY        "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\"

typedef struct { const char *name; uint32_t offset; } REG;

/* Everything compared before and after the cycle. TDH/TDT move with traffic and are sampled instead. */
static const REG g_regs[] = {
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
    {"RAL0", I226_RAL0}, {"RAH0", I226_RAH0},
};
#define REG_COUNT (sizeof(g_regs) / sizeof(g_regs[0]))

static const uint32_t g_tdh[TX_QUEUES] = {I226_TDH0, I226_TDH1, I226_TDH2, I226_TDH3};
static const uint32_t g_tdt[TX_QUEUES] = {I226_TDT0, I226_TDT1, I226_TDT2, I226_TDT3};

typedef struct {
    uint32_t tdh[TX_QUEUES], tdt[TX_QUEUES];
    uint32_t tqavctrl, baset_l, baset_h, cycle, cycle_s, txdctl0, tctl;
    unsigned long long systim, gauge, os_out, os_discards, os_errors;
    BOOL ok;
} SAMPLE;

typedef struct {
    SAMPLE first, last;
    unsigned sent, send_errors;
    BOOL head_moved, settled, healthy;
    unsigned first_seq, ring;
    LONG icmp_sent, icmp_replied;
} TX_RESULT;

static const DWORD g_baseline_marks[] = {0, 1000, 2000, 5000};
static const DWORD g_armed_marks[] = {0, 1000, 2000, 3000, 4000, 4250, 4500, 4750, 5000, 5250, 5500, 5750, 6000, 6250, 6500,
                                      6750, 7000, 7250, 7500, 7750, 8000, 8250, 8500, 8750, 9000, 9250, 9500, 9750, 10000,
                                      12000, 14000, 16000, 18000, 20000, 22000, 24000, 26000, 28000, 30000};
#define MARK_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int g_failed, g_blocked, g_ran;
static DWORD g_armed_period_ms = TX_PERIOD_MS;
static unsigned g_probe_payload, g_next_seq;

/* Non-negative while the adapter is armed and restoration has not reported */
static volatile LONG g_pending_adapter = -1;
static volatile LONG g_pending_did;

static void report_abort(void)
{
    LONG adapter = InterlockedExchange(&g_pending_adapter, -1);
    if (adapter >= 0) {
        printf("[INCONCLUSIVE_RESTORATION] TC-TASARM-003/adapter:%ld did=0x%04lX: test aborted after SETUP_TAS, restoration not run; "
               "run a NIC cycle, then TC-TSNREG-001\n", adapter, g_pending_did);
        fflush(stdout);
    }
}

static BOOL WINAPI on_console_ctrl(DWORD type)
{
    (void)type;
    report_abort();
    return FALSE;
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *info)
{
    (void)info;
    report_abort();
    return EXCEPTION_CONTINUE_SEARCH;
}

static void verdict(const char *tag, const char *tc, UINT32 adapter, avb_u16 did, const char *reason)
{
    printf("[%s] %s/adapter:%u did=0x%04X%s%s\n", tag, tc, adapter, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
}

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

static BOOL read_reg(HANDLE h, uint32_t offset, uint32_t *value)
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

static BOOL read_stats(HANDLE h, AVB_DRIVER_STATISTICS *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, s, sizeof(*s), s, sizeof(*s), &br, NULL);
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

static BOOL read_clock(HANDLE h, AVB_CLOCK_CONFIG *c)
{
    DWORD br = 0;
    ZeroMemory(c, sizeof(*c));
    return DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, c, sizeof(*c), c, sizeof(*c), &br, NULL) && c->status == 0;
}

static BOOL read_if(avb_u64 luid, MIB_IF_ROW2 *row)
{
    ZeroMemory(row, sizeof(*row));
    row->InterfaceLuid.Value = luid;
    return GetIfEntry2(row) == NO_ERROR;
}

static unsigned read_regs(HANDLE h, uint32_t *values)
{
    unsigned i, unreadable = 0;
    for (i = 0; i < REG_COUNT; i++) {
        if (!read_reg(h, g_regs[i].offset, &values[i])) {
            printf("    %-12s 0x%05X unreadable\n", g_regs[i].name, g_regs[i].offset);
            unreadable++;
        } else {
            printf("    %-12s 0x%05X = 0x%08X\n", g_regs[i].name, g_regs[i].offset, values[i]);
        }
    }
    return unreadable;
}

/** Finds the adapter's IPv4 address, and whether another adapter keeps the machine reachable */
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
            if (a->FirstUnicastAddress != NULL) {
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

static void take_sample(HANDLE h, avb_u64 luid, DWORD at_ms, SAMPLE *s)
{
    AVB_DRIVER_STATISTICS st;
    AVB_CLOCK_CONFIG clock_cfg;
    MIB_IF_ROW2 row;
    int q;

    ZeroMemory(s, sizeof(*s));
    s->ok = TRUE;
    for (q = 0; q < TX_QUEUES; q++) {
        s->ok &= read_reg(h, g_tdh[q], &s->tdh[q]);
        s->ok &= read_reg(h, g_tdt[q], &s->tdt[q]);
    }
    s->ok &= read_reg(h, I226_TQAVCTRL, &s->tqavctrl);
    s->ok &= read_reg(h, I226_BASET_L, &s->baset_l);
    s->ok &= read_reg(h, I226_BASET_H, &s->baset_h);
    s->ok &= read_reg(h, I226_QBVCYCLET, &s->cycle);
    s->ok &= read_reg(h, I226_QBVCYCLET_S, &s->cycle_s);
    s->ok &= read_reg(h, I226_TXDCTL0, &s->txdctl0);
    s->ok &= read_reg(h, I226_TCTL, &s->tctl);
    s->ok &= read_clock(h, &clock_cfg);
    s->systim = clock_cfg.systim;
    s->ok &= read_stats(h, &st);
    s->gauge = st.OutstandingSendNBLs;
    s->ok &= read_if(luid, &row);
    s->os_out = row.OutUcastPkts + row.OutNUcastPkts;
    s->os_discards = row.OutDiscards;
    s->os_errors = row.OutErrors;
    printf("    t=%-6lu TDH/TDT q0=%u/%u q1=%u/%u q2=%u/%u q3=%u/%u TQAVCTRL=0x%08X BaseT=0x%08X:%08X cycle=%u shadow=%u "
           "TXDCTL0=0x%08X TCTL=0x%08X phc=%llu gauge=%llu osOut=%llu osDisc=%llu osErr=%llu ok=%d\n", at_ms,
           s->tdh[0], s->tdt[0], s->tdh[1], s->tdt[1], s->tdh[2], s->tdt[2], s->tdh[3], s->tdt[3],
           s->tqavctrl, s->baset_h, s->baset_l, s->cycle, s->cycle_s, s->txdctl0, s->tctl, s->systim,
           s->gauge, s->os_out, s->os_discards, s->os_errors, s->ok);
    fflush(stdout);
}

/** Stall probe only: frames, bytes, descriptors, occupancy and outstanding sends are different quantities */
static void probe_line(const TX_RESULT *r, const SAMPLE *s, DWORD at_ms)
{
    long missing, last;

    if (g_probe_payload == 0) {
        return;
    }
    if (r->ring == 0) {
        printf("    probe t=%-6lu ring size unreadable\n", at_ms);
        return;
    }
    printf("    probe t=%-6lu frames=%u bytes=%llu desc=%u occupancy=%u/%u gauge=%llu wire=%u icmp=%ld/%ld\n", at_ms, r->sent,
           (unsigned long long)r->sent * (g_probe_payload + FRAME_HEADER_BYTES), (s->tdh[0] + r->ring - r->first.tdh[0]) % r->ring,
           (s->tdt[0] + r->ring - s->tdh[0]) % r->ring, r->ring, s->gauge, wire_seen(r->first_seq, g_next_seq, &missing, &last),
           g_icmp_replied - r->icmp_replied, g_icmp_sent - r->icmp_sent);
}

static void probe_summary(const TX_RESULT *r, const char *label)
{
    long missing, last;
    unsigned seen = wire_seen(r->first_seq, r->first_seq + r->sent, &missing, &last);

    printf("  [OBSERVATION] %s: payload=%u frames=%u bytes=%llu descFetched=%u occupancy=%u gauge=%llu wireSeen=%u firstMissingSeq=%ld "
           "lastSeenSeq=%ld firstSeq=%u icmp=%ld/%ld\n", label, g_probe_payload, r->sent,
           (unsigned long long)r->sent * (g_probe_payload + FRAME_HEADER_BYTES),
           r->ring != 0 ? (r->last.tdh[0] + r->ring - r->first.tdh[0]) % r->ring : 0,
           r->ring != 0 ? (r->last.tdt[0] + r->ring - r->last.tdh[0]) % r->ring : 0, r->last.gauge, seen, missing, last, r->first_seq,
           g_icmp_replied - r->icmp_replied, g_icmp_sent - r->icmp_sent);
}

/** Bounded broadcast from several source ports, with samples at the given marks, then a drain */
static BOOL tx_window(HANDLE h, avb_u64 luid, IN_ADDR ipv4, const DWORD *marks, size_t mark_count, DWORD period_ms,
                      const char *label, TX_RESULT *r)
{
    SOCKET sock[TX_SOCKETS];
    SOCKADDR_IN local = {0}, peer = {0};
    static const char payload[64] = "IntelAvbFilter #328 TC-TASARM TX probe";
    static unsigned char probe[MAX_PROBE_PAYLOAD];
    BOOL on = TRUE;
    u_long nonblocking = 1;
    size_t next_mark = 0;
    DWORD start, now, duration = marks[mark_count - 1];
    unsigned turn = 0;
    int i, q;

    ZeroMemory(r, sizeof(*r));
    local.sin_family = AF_INET;
    local.sin_addr = ipv4;
    peer.sin_family = AF_INET;
    peer.sin_port = htons(TX_DISCARD_PORT);
    peer.sin_addr.s_addr = INADDR_BROADCAST;
    for (i = 0; i < TX_SOCKETS; i++) {
        sock[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock[i] == INVALID_SOCKET || setsockopt(sock[i], SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on)) != 0 ||
            ioctlsocket(sock[i], FIONBIO, &nonblocking) != 0 || bind(sock[i], (SOCKADDR *)&local, sizeof(local)) != 0) {
            printf("    socket setup failed: %d\n", WSAGetLastError());
            while (i >= 0) { closesocket(sock[i--]); }
            return FALSE;
        }
    }

    printf("  --- TX window: %s ---\n", label);
    if (g_probe_payload != 0) {
        uint32_t tdlen = 0;
        r->first_seq = g_next_seq;
        r->icmp_sent = g_icmp_sent;
        r->icmp_replied = g_icmp_replied;
        if (read_reg(h, I226_TDLEN0, &tdlen)) {
            r->ring = tdlen / TX_DESCRIPTOR_BYTES;
        }
    }
    start = GetTickCount();
    do {
        now = GetTickCount() - start;
        if (next_mark < mark_count && now >= marks[next_mark]) {
            take_sample(h, luid, now, next_mark == 0 ? &r->first : &r->last);
            probe_line(r, next_mark == 0 ? &r->first : &r->last, now);
            next_mark++;
        }
        if (g_probe_payload != 0) {
            tas_wire_fill(probe, g_probe_payload, g_wire_nonce, g_next_seq);
        }
        if (sendto(sock[turn++ % TX_SOCKETS], g_probe_payload != 0 ? (const char *)probe : payload,
                   g_probe_payload != 0 ? (int)g_probe_payload : (int)sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) == SOCKET_ERROR) {
            r->send_errors++;
        } else {
            r->sent++;
            if (g_probe_payload != 0) { g_next_seq++; }
        }
        Sleep(period_ms);
    } while (next_mark < mark_count);

    Sleep(TX_DRAIN_MS);
    take_sample(h, luid, GetTickCount() - start, &r->last);
    probe_line(r, &r->last, GetTickCount() - start);
    for (i = 0; i < TX_SOCKETS; i++) {
        closesocket(sock[i]);
    }

    r->settled = r->last.ok && r->first.ok;
    for (q = 0; q < TX_QUEUES; q++) {
        r->head_moved |= (r->last.tdh[q] != r->first.tdh[q]);
        r->settled &= (r->last.tdh[q] == r->last.tdt[q]);
        printf("    queue %d: TDH %u -> %u, TDT %u -> %u\n", q, r->first.tdh[q], r->last.tdh[q], r->first.tdt[q], r->last.tdt[q]);
    }
    r->healthy = r->sent > 0 && r->head_moved && r->settled &&
                 r->last.gauge <= r->first.gauge + GAUGE_SLACK &&
                 (r->last.os_out - r->first.os_out) * 2 >= r->sent;
    printf("    sent=%u sendErrors=%u osOutDelta=%llu gauge %llu -> %llu headMoved=%d settled=%d healthy=%d\n",
           r->sent, r->send_errors, r->last.os_out - r->first.os_out, r->first.gauge, r->last.gauge,
           r->head_moved, r->settled, r->healthy);
    (void)duration;
    return TRUE;
}

/** Same request as test_tsn_ioctl_handlers: 1 ms cycle with two gate entries */
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

typedef struct { DEVINST inst; BOOL enable; CONFIGRET result; } CM_CALL;

static DWORD WINAPI cm_thread(LPVOID arg)
{
    CM_CALL *call = (CM_CALL *)arg;
    call->result = call->enable ? CM_Enable_DevNode(call->inst, 0) : CM_Disable_DevNode(call->inst, 0);
    return 0;
}

/** Runs one device-node call with a bound: a stalled disable must end in a verdict, not a hung test */
static BOOL cm_bounded(DEVINST inst, BOOL enable, DWORD bound_ms)
{
    static CM_CALL call;   /* static: survives an abandoned, still blocked thread */
    HANDLE thread;
    DWORD wait;

    call.inst = inst;
    call.enable = enable;
    call.result = CR_FAILURE;
    thread = CreateThread(NULL, 0, cm_thread, &call, 0, NULL);
    if (thread == NULL) {
        return FALSE;
    }
    wait = WaitForSingleObject(thread, bound_ms);
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0) {
        printf("    %s did not return within %lu ms\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", bound_ms);
        return FALSE;
    }
    printf("    %s result=0x%08X\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", (unsigned)call.result);
    return call.result == CR_SUCCESS;
}

/** Maps NET_LUID to exactly this adapter's device node, never by model or name */
static BOOL find_devinst(avb_u64 luid, DEVINST *inst)
{
    NET_LUID l;
    GUID guid;
    char key[256], pnp[512];
    DWORD size = sizeof(pnp);

    l.Value = luid;
    if (ConvertInterfaceLuidToGuid(&l, &guid) != NO_ERROR) {
        return FALSE;
    }
    sprintf_s(key, sizeof(key), NET_CLASS_KEY "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\\Connection",
              guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
              guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "PnPInstanceId", RRF_RT_REG_SZ, NULL, pnp, &size) != ERROR_SUCCESS) {
        return FALSE;
    }
    printf("    device node: %s\n", pnp);
    return CM_Locate_DevNodeA(inst, pnp, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

/** Waits until the adapter is back with the same identity and a handle is bound to it */
static HANDLE wait_for_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *want)
{
    DWORD start = GetTickCount();

    while (GetTickCount() - start < RETURN_BOUND_MS) {
        HANDLE h = open_device();
        if (h != INVALID_HANDLE_VALUE) {
            AVB_ADAPTER_IDENTITY_REQUEST id;
            UINT32 i, count = 1;
            for (i = 0; i < count && identity_at(h, i, &id); i++) {
                count = id.count;
                if (id.net_luid == want->net_luid && id.device_id == want->device_id &&
                    memcmp(id.mac, want->mac, sizeof(id.mac)) == 0 && bind_luid(h, want->net_luid)) {
                    return h;
                }
            }
            CloseHandle(h);
        }
        Sleep(1000);
    }
    return INVALID_HANDLE_VALUE;
}

/** Restores by NIC cycle, gathers the evidence and reports TC-TASARM-003. The handle h is closed here. */
static void restore_and_verify(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, IN_ADDR ipv4,
                               const uint32_t *baseline, const AVB_DRIVER_STATISTICS *stats_before,
                               const AVB_CLOCK_CONFIG *clock_before, LARGE_INTEGER qpc_before)
{
    TAS_RESTORATION_EVIDENCE e;
    uint32_t after[REG_COUNT];
    DEVINST inst = 0;
    HANDLE fresh;
    unsigned i, armed = 1;
    DWORD start;
    MIB_IF_ROW2 row;

    ZeroMemory(&e, sizeof(e));
    printf("\n  --- Restoration: NIC cycle ---\n");
    fflush(stdout);
    if (find_devinst(id->net_luid, &inst)) {
        CloseHandle(h);   /* an open bound handle must not hold the adapter context across the detach */
        h = INVALID_HANDLE_VALUE;
        e.cycle_completed = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
        if (e.cycle_completed) {
            Sleep(3000);
            e.cycle_completed = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
        } else {
            printf("    enable not attempted: the disable did not complete and may still be pending in Windows\n");
        }
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }

    fresh = e.cycle_completed ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
    e.adapter_returned = (fresh != INVALID_HANDLE_VALUE);
    if (e.adapter_returned) {
        AVB_DRIVER_STATISTICS stats_after;
        AVB_CLOCK_CONFIG clock_after;

        start = GetTickCount();
        while (GetTickCount() - start < RETURN_BOUND_MS) {
            if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
                e.link_up = TRUE;
                break;
            }
            Sleep(1000);
        }
        Sleep(SETTLE_MS);

        /* The statistics live in the adapter context, destroyed at detach: a lower IoctlCount proves a new context. */
        if (read_stats(fresh, &stats_after)) {
            e.attach_observed = stats_after.IoctlCount < stats_before->IoctlCount;
            printf("    IoctlCount %llu -> %llu, FilterAttachCount %llu -> %llu\n", stats_before->IoctlCount, stats_after.IoctlCount,
                   stats_before->FilterAttachCount, stats_after.FilterAttachCount);
        }
        if (read_armed(fresh, &armed)) {
            e.driver_disarmed = (armed == 0);
            printf("    tasArmed=%u\n", armed);
        } else {
            e.armed_unreadable = TRUE;
            printf("    tasArmed unreadable\n");
        }

        printf("  --- Registers after the cycle ---\n");
        memset(after, 0xFF, sizeof(after));   /* ALL_ONES = not read, reported through regs_unreadable */
        e.regs_unreadable = read_regs(fresh, after);
        e.tsn_mode_clear = TRUE;              /* TQAVCTRL unreadable is unmeasured, not a failure */
        e.regs_compared = (unsigned)REG_COUNT;
        for (i = 0; i < REG_COUNT; i++) {
            if (after[i] == ALL_ONES) {
                continue;
            }
            if (after[i] != baseline[i]) {
                printf("    CHANGED %-12s 0x%08X -> 0x%08X\n", g_regs[i].name, baseline[i], after[i]);
                e.regs_changed++;
            }
            if (g_regs[i].offset == I226_TQAVCTRL) {
                e.tsn_mode_clear = (after[i] & I226_TQAVCTRL_TRANSMIT_MODE_MASK) == 0;
            }
        }

        if (read_clock(fresh, &clock_after)) {
            LARGE_INTEGER now, freq;
            long long wall_ns, delta_ns, drift_ns;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            wall_ns = (long long)((double)(now.QuadPart - qpc_before.QuadPart) * 1e9 / (double)freq.QuadPart);
            delta_ns = (long long)clock_after.systim - (long long)clock_before->systim;
            drift_ns = delta_ns - wall_ns;
            e.phc_observed = TRUE;
            e.phc_continuous = drift_ns > -PHC_TOLERANCE_NS && drift_ns < PHC_TOLERANCE_NS;
            printf("    PHC before=%llu after=%llu wallNs=%lld drift=%lld\n", clock_before->systim, clock_after.systim, wall_ns, drift_ns);
        }

        if (e.link_up) {
            TX_RESULT tx;
            e.tx_probed = tx_window(fresh, id->net_luid, ipv4, g_baseline_marks, MARK_COUNT(g_baseline_marks), TX_PERIOD_MS, "after restoration", &tx);
            e.tx_healthy = tx.healthy;
        }
        CloseHandle(fresh);
    }

    printf("    evidence: cycle=%d returned=%d attach=%d disarmed=%d armedUnreadable=%d tsnClear=%d compared=%u unreadable=%u changed=%u "
           "phcObserved=%d phcContinuous=%d link=%d txProbed=%d txHealthy=%d\n",
           e.cycle_completed, e.adapter_returned, e.attach_observed, e.driver_disarmed, e.armed_unreadable, e.tsn_mode_clear, e.regs_compared,
           e.regs_unreadable, e.regs_changed, e.phc_observed, e.phc_continuous, e.link_up, e.tx_probed, e.tx_healthy);
    InterlockedExchange(&g_pending_adapter, -1);
    switch (tas_restoration_outcome(&e)) {
    case TAS_RESTORATION_VERIFIED:
        verdict("PASS", "TC-TASARM-003", index, id->device_id, "observed state restored; internal scheduler and FIFO state are not observable");
        break;
    case TAS_RESTORATION_INCONCLUSIVE:
        verdict("INCONCLUSIVE_RESTORATION", "TC-TASARM-003", index, id->device_id, "state the restoration depends on could not be measured; see evidence line");
        g_failed++;
        break;
    default:
        verdict("CLEANUP_FAILED", "TC-TASARM-003", index, id->device_id, "restoration demonstrably incomplete; see evidence line. A timed-out disable may still be pending in Windows. Recovery: operator NIC cycle once it ends, then TC-TSNREG-001; reboot is an operator decision");
        g_failed++;
        break;
    }
}

static void run_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, BOOL debug_build)
{
    uint32_t baseline[REG_COUNT];
    AVB_DRIVER_STATISTICS stats;
    AVB_CLOCK_CONFIG clock_cfg;
    LARGE_INTEGER qpc;
    MIB_IF_ROW2 row;
    TX_RESULT before, armed_tx;
    uint32_t armed_regs[REG_COUNT];
    IN_ADDR ipv4;
    SAMPLE now;
    BOOL other_link = FALSE, arm_ok, schedule_ok;
    DWORD arm_error;
    unsigned armed = 0;
    HANDLE h;

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u ===\n", index, id->device_id, id->net_luid, id->if_index);
    g_ran++;

    /* Everything up to arm_tas is read-only or bounded TX: a BLOCKED here leaves the adapter untouched */
    if (!debug_build) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "READ_REGISTER exists only in Debug drivers; no evidence possible");
        g_blocked++;
        return;
    }
    h = open_device();
    if (h == INVALID_HANDLE_VALUE || !bind_luid(h, id->net_luid)) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "cannot bind a handle to the adapter");
        g_blocked++;
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); }
        return;
    }
    if (!read_if(id->net_luid, &row) || row.OperStatus != IfOperStatusUp || !find_addressing(id->net_luid, &ipv4, &other_link)) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "adapter link is down or it has no IPv4 address");
        g_blocked++;
        CloseHandle(h);
        return;
    }
    if (!other_link) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "no independent management link; the NIC cycle would isolate the host");
        g_blocked++;
        CloseHandle(h);
        return;
    }
    if (!read_armed(h, &armed) || armed != 0 || !read_stats(h, &stats) || !read_clock(h, &clock_cfg)) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "TAS already armed, or driver state unreadable");
        g_blocked++;
        CloseHandle(h);
        return;
    }
    printf("  --- Baseline registers ---\n");
    if (read_regs(h, baseline) != 0) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "baseline registers unreadable");
        g_blocked++;
        CloseHandle(h);
        return;
    }
    if (g_probe_payload != 0 && wire_start(id->net_luid, id->mac, GetTickCount() ^ GetCurrentProcessId(), ipv4) == 0) {
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "no wire observer could be started, so the probe cannot answer its question");
        g_blocked++;
        wire_stop();
        CloseHandle(h);
        return;
    }
    if (!tx_window(h, id->net_luid, ipv4, g_baseline_marks, MARK_COUNT(g_baseline_marks), TX_PERIOD_MS, "before SETUP_TAS", &before) || !before.healthy) {
        if (g_probe_payload != 0) { wire_stop(); }
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "the TX probe is not healthy before arming, so it could not detect a change");
        g_blocked++;
        CloseHandle(h);
        return;
    }
    if (g_probe_payload != 0) {
        long missing, last;
        unsigned seen = wire_seen(before.first_seq, before.first_seq + before.sent, &missing, &last);
        probe_summary(&before, "before SETUP_TAS");
        if ((unsigned long long)seen * 100 < (unsigned long long)before.sent * WIRE_BASELINE_MIN_PERCENT) {
            wire_stop();
            verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "the wire observers did not see the baseline frames; the capture proves nothing here");
            g_blocked++;
            CloseHandle(h);
            return;
        }
    }
    if (!read_clock(h, &clock_cfg) || !read_stats(h, &stats)) {
        if (g_probe_payload != 0) { wire_stop(); }
        verdict("BLOCKED", "TC-TASARM-001", index, id->device_id, "PHC or statistics unreadable before arming");
        g_blocked++;
        CloseHandle(h);
        return;
    }
    QueryPerformanceCounter(&qpc);

    /* First mutation.  From here restoration always runs, also when SETUP_TAS reports failure. */
    printf("\n  --- SETUP_TAS ---\n");
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);
    arm_ok = arm_tas(h, &arm_error);
    printf("    SETUP_TAS ok=%d win32=%lu\n", arm_ok, arm_error);
    take_sample(h, id->net_luid, 0, &now);
    printf("  --- Registers right after SETUP_TAS ---\n");
    (void)read_regs(h, armed_regs);

    schedule_ok = arm_ok && now.ok && (now.tqavctrl & I226_TQAVCTRL_TRANSMIT_MODE_MASK) != 0 &&
                  now.cycle == 1000000u && now.cycle_s != 0 && (now.baset_l != 0 || now.baset_h != 0);
    printf("  armed send period %lu ms\n", g_armed_period_ms);
    (void)tx_window(h, id->net_luid, ipv4, g_armed_marks, MARK_COUNT(g_armed_marks), g_armed_period_ms, "while armed", &armed_tx);
    printf("  --- Registers at the end of the armed window ---\n");
    (void)read_regs(h, armed_regs);
    if (g_probe_payload != 0) {
        probe_summary(&armed_tx, "while armed");
        wire_stop();
    }
    verdict(schedule_ok ? "PASS" : "FAIL", "TC-TASARM-001", index, id->device_id,
            schedule_ok ? NULL : "the requested schedule is not in the adapter: needs TSN mode, QBVCYCLET 1 ms, a shadow cycle and a base time");
    if (!schedule_ok) { g_failed++; }
    verdict(armed_tx.healthy ? "PASS" : "FAIL", "TC-TASARM-002", index, id->device_id,
            armed_tx.healthy ? NULL : "TX is not healthy while armed; see the per-queue lines and gauge");
    if (!armed_tx.healthy) { g_failed++; }

    restore_and_verify(h, index, id, ipv4, baseline, &stats, &clock_cfg, qpc);
}

int main(int argc, char **argv)
{
    AVB_ADAPTER_IDENTITY_REQUEST ids[8];
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i, count = 1, stored = 0;
    BOOL debug_build;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    printf("=== IntelAvbFilter - isolated SETUP_TAS on real hardware (#328) ===\n");
    if (argc == 3 && strcmp(argv[1], "--armed-period-ms") == 0) {
        g_armed_period_ms = strtoul(argv[2], NULL, 10);
    } else if (argc == 3 && strcmp(argv[1], "--stall-probe") == 0) {
        g_probe_payload = strtoul(argv[2], NULL, 10);
        if (g_probe_payload < TAS_WIRE_TAG_BYTES || g_probe_payload > MAX_PROBE_PAYLOAD) {
            g_armed_period_ms = 0;
        }
    } else if (argc != 1) {
        g_armed_period_ms = 0;
    }
    if (g_armed_period_ms == 0 || g_armed_period_ms > MAX_ARMED_PERIOD_MS) {
        printf("[BLOCKED] TC-TASARM-001: usage: test_tas_isolated_arm [--armed-period-ms 1..%d | --stall-probe %d..%d]\n",
               MAX_ARMED_PERIOD_MS, TAS_WIRE_TAG_BYTES, MAX_PROBE_PAYLOAD);
        return 3;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[BLOCKED] TC-TASARM-001: Winsock unavailable\n");
        return 3;
    }
    h = open_device();
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] TC-TASARM-001: cannot open %s (error %lu)\n", DEVICE_NAME, GetLastError());
        return 3;
    }
    debug_build = DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) &&
                  (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;

    /* Identities first: a NIC cycle may reorder the enumeration */
    for (i = 0; i < count && stored < ARRAYSIZE(ids); i++) {
        AVB_ADAPTER_IDENTITY_REQUEST id;
        if (!identity_at(h, i, &id)) {
            break;
        }
        count = id.count;
        if (id.capabilities & INTEL_CAP_TSN_TAS) {
            ids[stored++] = id;
        } else {
            verdict("SKIP_UNSUPPORTED", "TC-TASARM-001", i, id.device_id, "adapter does not advertise INTEL_CAP_TSN_TAS");
        }
    }
    CloseHandle(h);

    for (i = 0; i < stored; i++) {
        run_adapter(ids[i].index, &ids[i], debug_build);
    }
    WSACleanup();

    printf("\nadapters run=%d failed verdicts=%d blocked=%d\n", g_ran, g_failed, g_blocked);
    if (g_ran == 0) { return 2; }
    if (g_failed != 0) { return 1; }
    return g_blocked != 0 ? 3 : 0;
}
