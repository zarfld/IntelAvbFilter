/**
 * @file tas_gate_investigation.c
 * @brief Exploratory: what opens I226 egress after SETUP_TAS? (#328)
 *
 * Not a test. No runner entry, no testcase verdicts, classified Diagnostic in $TestArtifactClassification. It uses the
 * generic Debug-only WRITE_REGISTER to learn register behaviour before any implementation, which regression tests must
 * not do. What it establishes is implemented later through a dedicated IOCTL and verified by hardware TDD. Output is
 * LOCAL_DIAGNOSTIC evidence, never HIL regression evidence.
 *
 * Sequence: tagged broadcast baseline seen on the I210, SETUP_TAS, then register groups, in this order:
 *   w  per-queue windows: STQT=0, ENDQT=cycle
 *   s  shadow cycle, then cycle
 *   b  base time at the next cycle boundary after the measured PHC, as upstream igc_tsn.c does for a base in the past
 * FUTSCDDIS and a far-future base are deliberately not used: upstream warns of a TX hang on I226 around them.
 *
 * Without arguments: w, then s and b together, stopping when frames reach the wire. Those groups are cumulative, so that
 * run cannot say which register mattered. --apply <letters> writes only the named groups in one step, from a fresh NIC
 * cycle, which separates them.
 *
 * Each step stays below the fill level seen on 2026-11-05, so a stalled ring cannot hide an opening window.
 *
 * Mutations: SETUP_TAS, the writes above, bounded broadcast and ICMP. Restoration is the NIC cycle of the I226 only.
 * Requires a Debug driver. The tool takes no machine lock; the operator command holds it.
 *
 * Exit: 0 registers, TX and PHC all restored; 1 registers differ, TX unhealthy or the cycle failed; 2 registers and TX
 * restored but the PHC is not continuous; 3 not run (nothing mutated).
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
#include "../../include/intel_pci_ids.h"
#include "../../intel-ethernet-regs/gen/i226_regs.h"
#include "../hardware/tas_wire_observer.h"

#define DEVICE_NAME          "\\\\.\\IntelAvbFilter"
#define TAG                  "[LOCAL_DIAGNOSTIC]"
#define QUEUES               4
#define CYCLE_NS             1000000u
#define NS_PER_SEC           1000000000ULL
#define PAYLOAD_BYTES        64
#define TX_DESCRIPTOR_BYTES  16
#define PHC_TOLERANCE_NS     (2 * 1000000000LL)   /* the same unsupported constant as test_tas_isolated_arm; no hardware-derived tolerance exists */
#define SEND_PERIOD_MS       50
#define BASELINE_MS          3000
#define ARMED_MS             1500
#define STEP_MS              1000
#define DRAIN_MS             1000
#define WIRE_MIN_PERCENT     90
#define DISABLE_BOUND_MS     120000
#define ENABLE_BOUND_MS      60000
#define RETURN_BOUND_MS      60000
#define SETTLE_MS            5000
#define NET_CLASS_KEY        "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\"

typedef struct { const char *name; uint32_t offset; } REG;

/* The same set as test_tas_isolated_arm, so a difference outside the written registers is seen too */
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

static const uint32_t g_stqt[QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
static const uint32_t g_endqt[QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};

static unsigned g_next_seq;
static volatile LONG g_armed;

typedef struct { DEVINST inst; BOOL enable; CONFIGRET result; } CM_CALL;

/* Host-side quantities of one moment. None of them shows that a frame left the adapter. */
typedef struct {
    uint32_t tdh, tdt, ring;
    unsigned long long os_out, gauge;
    LONG captured, from_target;
    BOOL ok;
} SNAP;

static BOOL WINAPI on_console_ctrl(DWORD type)
{
    (void)type;
    if (InterlockedExchange(&g_armed, 0) != 0) {
        printf(TAG " aborted while armed; the I226 needs a NIC cycle\n");
        fflush(stdout);
    }
    return FALSE;
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
    return TRUE;
}

/** Records the original value, the intended value, the operation status and the readback */
static BOOL write_reg(HANDLE h, uint32_t offset, uint32_t value, const char *name)
{
    AVB_REGISTER_REQUEST r = {0};
    uint32_t before = 0, after = 0;
    BOOL have_before = read_reg(h, offset, &before), have_after, wrote;
    DWORD br = 0, error;

    r.offset = offset;
    r.value = value;
    wrote = DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL) && r.status == 0;
    error = wrote ? ERROR_SUCCESS : GetLastError();
    have_after = read_reg(h, offset, &after);
    printf("    write %-12s 0x%05X before=%s0x%08X intended=0x%08X status=%s error=%lu readback=%s0x%08X%s\n", name, offset,
           have_before ? "" : "unreadable ", before, value, wrote ? "ok" : "FAILED", error,
           have_after ? "" : "unreadable ", after, (have_after && after == value) ? "" : " MISMATCH");
    return wrote;
}

/** Prints every register, or with base only those that differ from it. Returns the unreadable count */
static unsigned read_regs(HANDLE h, uint32_t *values, const char *label, const uint32_t *base)
{
    unsigned i, unreadable = 0, shown = 0;

    printf("  --- registers: %s%s ---\n", label, base != NULL ? ", differences from baseline" : "");
    for (i = 0; i < REG_COUNT; i++) {
        if (!read_reg(h, g_regs[i].offset, &values[i])) {
            printf("    %-12s 0x%05X unreadable\n", g_regs[i].name, g_regs[i].offset);
            unreadable++;
        } else if (base == NULL || values[i] != base[i]) {
            printf("    %-12s 0x%05X = 0x%08X\n", g_regs[i].name, g_regs[i].offset, values[i]);
            shown++;
        }
    }
    if (base != NULL && shown == 0) {
        printf("    none\n");
    }
    return unreadable;
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

/** Same request as test_tas_isolated_arm */
static BOOL arm_tas(HANDLE h)
{
    AVB_TAS_REQUEST tas;
    DWORD br = 0;

    ZeroMemory(&tas, sizeof(tas));
    tas.config.base_time_ns = CYCLE_NS;
    tas.config.cycle_time_ns = CYCLE_NS;
    tas.config.gate_states[0] = 0xFF;
    tas.config.gate_durations[0] = CYCLE_NS / 2;
    tas.config.gate_states[1] = 0x01;
    tas.config.gate_durations[1] = CYCLE_NS / 2;
    return DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, &tas, sizeof(tas), &tas, sizeof(tas), &br, NULL) && tas.status == 0;
}

static void snap(HANDLE h, avb_u64 luid, SNAP *s)
{
    AVB_DRIVER_STATISTICS st;
    MIB_IF_ROW2 row;
    uint32_t tdlen = 0;
    DWORD br = 0;

    ZeroMemory(s, sizeof(*s));
    ZeroMemory(&st, sizeof(st));
    ZeroMemory(&row, sizeof(row));
    row.InterfaceLuid.Value = luid;
    s->ok = read_reg(h, I226_TDH0, &s->tdh) && read_reg(h, I226_TDT0, &s->tdt) && read_reg(h, I226_TDLEN0, &tdlen) &&
            tdlen >= TX_DESCRIPTOR_BYTES &&
            DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, &st, sizeof(st), &st, sizeof(st), &br, NULL) && GetIfEntry2(&row) == NO_ERROR;
    s->ring = s->ok ? tdlen / TX_DESCRIPTOR_BYTES : 1;
    s->gauge = st.OutstandingSendNBLs;
    s->os_out = row.OutUcastPkts + row.OutNUcastPkts;
    wire_counts(&s->captured, &s->from_target);
}

/** Sends tagged broadcast for ms, waits, and returns the frames of this window seen on the wire. Reports each quantity
 *  separately: accepted by Windows, descriptors fetched, OS counter, outstanding sends, seen on the wire. */
static unsigned tx_step(HANDLE h, avb_u64 luid, SOCKET sock, DWORD ms, const char *label, unsigned armed_first_seq, BOOL *capture_alive)
{
    static unsigned char payload[PAYLOAD_BYTES];
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
        tas_wire_fill(payload, sizeof(payload), g_wire_nonce, g_next_seq);
        if (sendto(sock, (const char *)payload, sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) != SOCKET_ERROR) {
            sent++;
            g_next_seq++;
        } else {
            errors++;
        }
        Sleep(SEND_PERIOD_MS);
    }
    Sleep(DRAIN_MS);
    seen = wire_seen(first, first + sent, &missing, &last);
    all = wire_seen(armed_first_seq, g_next_seq, &m2, &l2);
    snap(h, luid, &after);
    printf(TAG " %s: accepted=%u sendErrors=%u wireSeen=%u firstMissingSeq=%ld sinceArmSeen=%u/%u descFetched=%u occupancy=%u/%u osOutDelta=%llu "
           "gauge=%llu capturedDelta=%ld fromTargetDelta=%ld icmp=%ld/%ld samplesOk=%d\n", label, sent, errors, seen, missing, all,
           g_next_seq - armed_first_seq, (after.tdh + after.ring - before.tdh) % after.ring, (after.tdt + after.ring - after.tdh) % after.ring,
           after.ring, after.os_out - before.os_out, after.gauge, after.captured - before.captured,
           after.from_target - before.from_target, g_icmp_replied, g_icmp_sent, before.ok && after.ok);
    if (after.captured == before.captured) {
        *capture_alive = FALSE;
        printf(TAG " %s: the capture saw nothing at all, so wireSeen=0 proves nothing here\n", label);
    }
    fflush(stdout);
    return seen;
}

/** w: every queue window covers the whole cycle */
static BOOL step_windows(HANDLE h)
{
    int q;
    for (q = 0; q < QUEUES; q++) {
        if (!write_reg(h, g_stqt[q], 0, "STQT") || !write_reg(h, g_endqt[q], CYCLE_NS, "ENDQT")) {
            return FALSE;
        }
    }
    return TRUE;
}

/** s: shadow cycle first, then cycle, as upstream */
static BOOL step_shadow(HANDLE h)
{
    return write_reg(h, I226_QBVCYCLET_S, CYCLE_NS, "QBVCYCLET_S") && write_reg(h, I226_QBVCYCLET, CYCLE_NS, "QBVCYCLET");
}

/** b: next cycle boundary after the measured PHC. The second PHC read shows whether it was future or past on arrival. */
static BOOL step_base(HANDLE h)
{
    unsigned long long systim = 0, base, after = 0;
    BOOL ok;

    if (!read_systim(h, &systim)) {
        printf(TAG " PHC unreadable, base time not written\n");
        return FALSE;
    }
    base = (systim / CYCLE_NS + 1) * CYCLE_NS;
    ok = write_reg(h, I226_BASET_H, (uint32_t)(base / NS_PER_SEC), "BASET_H") &&
         write_reg(h, I226_BASET_L, (uint32_t)(base % NS_PER_SEC), "BASET_L");
    if (read_systim(h, &after)) {
        printf("    PHC before=%llu baseT=%llu after=%llu, baseT - PHC after write = %lld ns\n", systim, base, after, (long long)base - (long long)after);
    }
    return ok;
}

/** Writes the groups named in letters, in the order w, s, b */
static BOOL apply_groups(HANDLE h, const char *letters)
{
    return (strchr(letters, 'w') == NULL || step_windows(h)) && (strchr(letters, 's') == NULL || step_shadow(h)) &&
           (strchr(letters, 'b') == NULL || step_base(h));
}

static DWORD WINAPI cm_thread(LPVOID arg)
{
    CM_CALL *call = (CM_CALL *)arg;
    call->result = call->enable ? CM_Enable_DevNode(call->inst, 0) : CM_Disable_DevNode(call->inst, 0);
    return 0;
}

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
    return CM_Locate_DevNodeA(inst, pnp, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

/** NIC cycle, then registers, armed state and PHC against the baseline, each reported separately. Exit code as in the header. */
static int restore(const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, const uint32_t *baseline, unsigned long long phc_before,
                   LARGE_INTEGER qpc_before, HANDLE *fresh)
{
    uint32_t after[REG_COUNT];
    LARGE_INTEGER qpc, frequency;
    unsigned long long phc_after = 0;
    long long wall_ns, drift_ns;
    DWORD start;
    unsigned i, differ = 0, armed = 1;

    printf("  --- NIC cycle of the I226 ---\n");
    if (!cm_bounded(inst, FALSE, DISABLE_BOUND_MS)) {
        printf(TAG " disable did not complete; do not enable; a reboot is an operator decision\n");
        return 1;
    }
    if (!cm_bounded(inst, TRUE, ENABLE_BOUND_MS)) {
        printf(TAG " enable did not complete\n");
        return 1;
    }
    InterlockedExchange(&g_armed, 0);
    start = GetTickCount();
    while (GetTickCount() - start < RETURN_BOUND_MS) {
        HANDLE h = open_device();
        if (h != INVALID_HANDLE_VALUE && bind_luid(h, id->net_luid)) {
            Sleep(SETTLE_MS);
            if (read_regs(h, after, "after the cycle", baseline) == 0 && read_armed(h, &armed) && read_systim(h, &phc_after)) {
                QueryPerformanceCounter(&qpc);
                QueryPerformanceFrequency(&frequency);
                for (i = 0; i < REG_COUNT; i++) {
                    if (after[i] != baseline[i]) {
                        differ++;
                    }
                }
                wall_ns = (long long)((double)(qpc.QuadPart - qpc_before.QuadPart) * 1e9 / (double)frequency.QuadPart);
                drift_ns = ((long long)phc_after - (long long)phc_before) - wall_ns;
                printf(TAG " registers compared=%u differing=%u tasArmed=%u\n", (unsigned)REG_COUNT, differ, armed);
                printf(TAG " PHC before=%llu after=%llu wallNs=%lld driftNs=%lld continuous=%d\n", phc_before, phc_after, wall_ns, drift_ns,
                       drift_ns > -PHC_TOLERANCE_NS && drift_ns < PHC_TOLERANCE_NS);
                *fresh = h;
                if (differ != 0 || armed != 0) {
                    return 1;
                }
                return (drift_ns > -PHC_TOLERANCE_NS && drift_ns < PHC_TOLERANCE_NS) ? 0 : 2;
            }
        }
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); }
        Sleep(1000);
    }
    printf(TAG " the adapter did not return within %d ms\n", RETURN_BOUND_MS);
    return 1;
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

/** The adapter's address comes back after the cycle at its own pace */
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

int main(int argc, char **argv)
{
    AVB_ADAPTER_IDENTITY_REQUEST target, id;
    IOCTL_VERSION version = {0};
    uint32_t baseline[REG_COUNT], now[REG_COUNT];
    const char *letters = NULL;
    LARGE_INTEGER qpc_before;
    unsigned long long phc_before = 0;
    MIB_IF_ROW2 row;
    WSADATA wsa;
    IN_ADDR ipv4;
    DEVINST inst;
    SOCKET sock;
    avb_u64 capture = 0;
    UINT32 i, count = 1;
    DWORD br = 0;
    HANDLE h, fresh = INVALID_HANDLE_VALUE;
    BOOL have_target = FALSE, other_link = FALSE, capture_alive = TRUE, tx_restored = FALSE;
    unsigned armed = 1, armed_first, seen, baseline_seq, after_seq;
    size_t n;
    int rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    printf("=== IntelAvbFilter - exploratory TAS gate investigation (#328), %s ===\n", TAG);
    if (argc == 3 && strcmp(argv[1], "--apply") == 0 && argv[2][0] != '\0' && strspn(argv[2], "wsb") == strlen(argv[2])) {
        letters = argv[2];
    } else if (argc != 1) {
        printf(TAG " not run: usage: tas_gate_investigation [--apply <any of w, s, b>]\n");
        return 3;
    }
    for (n = 0; letters != NULL && n < strlen(letters); n++) {
        if (strchr(letters + n + 1, letters[n]) != NULL) {
            printf(TAG " not run: --apply names a group twice\n");
            return 3;
        }
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf(TAG " not run: Winsock unavailable\n");
        return 3;
    }
    h = open_device();
    if (h == INVALID_HANDLE_VALUE) {
        printf(TAG " not run: cannot open %s (error %lu)\n", DEVICE_NAME, GetLastError());
        return 3;
    }
    if (!DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) ||
        (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) == 0) {
        printf(TAG " not run: register access exists only in Debug drivers\n");
        CloseHandle(h);
        return 3;
    }
    for (i = 0; i < count && identity_at(h, i, &id); i++) {
        count = id.count;
        if (capture == 0 && id.device_id == INTEL_DEV_I210_AT) {
            capture = id.net_luid;
        }
        /* The register map below is the I226 one, so the capability alone is not enough */
        if (!have_target && (id.capabilities & INTEL_CAP_TSN_TAS) &&
            (id.device_id == INTEL_DEV_I226_LM || id.device_id == INTEL_DEV_I226_V || id.device_id == INTEL_DEV_I226_IT)) {
            target = id;
            have_target = TRUE;
        }
    }
    if (!have_target || capture == 0 || !bind_luid(h, target.net_luid) || !find_devinst(target.net_luid, &inst) ||
        !find_addressing(target.net_luid, &ipv4, &other_link) || !other_link || !read_armed(h, &armed) || armed != 0) {
        printf(TAG " not run: needs a bound TAS-capable I226 with link, an I210, a separate management link and tasArmed=0\n");
        CloseHandle(h);
        return 3;
    }
    if (read_regs(h, baseline, "baseline", NULL) != 0) {
        printf(TAG " not run: baseline registers unreadable\n");
        CloseHandle(h);
        return 3;
    }
    sock = open_tx_socket(ipv4);
    if (sock == INVALID_SOCKET) {
        printf(TAG " not run: socket setup failed (%d)\n", WSAGetLastError());
        CloseHandle(h);
        return 3;
    }
    if (wire_start(target.net_luid, capture, target.mac, GetTickCount() ^ GetCurrentProcessId(), ipv4) == 0) {
        printf(TAG " not run: the I210 capture could not be started\n");
        wire_stop();
        CloseHandle(h);
        return 3;
    }

    baseline_seq = g_next_seq;
    seen = tx_step(h, target.net_luid, sock, BASELINE_MS, "baseline", g_next_seq, &capture_alive);
    if ((unsigned long long)seen * 100 < (unsigned long long)(g_next_seq - baseline_seq) * WIRE_MIN_PERCENT) {
        printf(TAG " not run: the I210 did not see the baseline frames; nothing mutated\n");
        wire_stop();
        CloseHandle(h);
        return 3;
    }

    /* Checked again here: the state that matters is the one just before the first mutation */
    ZeroMemory(&row, sizeof(row));
    row.InterfaceLuid.Value = target.net_luid;
    if (!read_armed(h, &armed) || armed != 0 || GetIfEntry2(&row) != NO_ERROR || row.OperStatus != IfOperStatusUp ||
        !read_systim(h, &phc_before)) {
        printf(TAG " not run: tasArmed, link or PHC changed or became unreadable before SETUP_TAS; nothing mutated\n");
        wire_stop();
        CloseHandle(h);
        return 3;
    }
    QueryPerformanceCounter(&qpc_before);

    /* First mutation. From here the NIC cycle always runs. */
    InterlockedExchange(&g_armed, 1);
    printf("  --- SETUP_TAS ---\n");
    armed_first = g_next_seq;
    if (!arm_tas(h)) {
        printf(TAG " SETUP_TAS failed (error %lu); no register group applied\n", GetLastError());
        seen = 0;
    } else {
        (void)read_regs(h, now, "after SETUP_TAS", baseline);
        seen = tx_step(h, target.net_luid, sock, ARMED_MS, "SETUP_TAS only", armed_first, &capture_alive);
        if (letters != NULL) {
            printf("  --- applying %s ---\n", letters);
            if (seen == 0 && apply_groups(h, letters)) {
                (void)read_regs(h, now, "after --apply", baseline);
                seen = tx_step(h, target.net_luid, sock, STEP_MS, letters, armed_first, &capture_alive);
            }
        } else {
            if (seen == 0) {
                printf("  --- candidate A: per-queue windows ---\n");
                if (apply_groups(h, "w")) {
                    (void)read_regs(h, now, "after candidate A", baseline);
                    seen = tx_step(h, target.net_luid, sock, STEP_MS, "candidate A", armed_first, &capture_alive);
                }
            }
            if (seen == 0) {
                printf("  --- candidate B: shadow cycle and base time ---\n");
                if (apply_groups(h, "sb")) {
                    (void)read_regs(h, now, "after candidate B", baseline);
                    seen = tx_step(h, target.net_luid, sock, STEP_MS, "candidate B", armed_first, &capture_alive);
                }
            }
        }
    }
    printf(TAG " egress %s\n", seen != 0 ? "opened: tagged frames reached the I210" :
           capture_alive ? "stayed closed: the capture was alive and saw no armed frame" : "unknown: the capture went silent");

    closesocket(sock);
    CloseHandle(h);
    rc = restore(&target, inst, baseline, phc_before, qpc_before, &fresh);

    /* Registers can match while TX is dead, so wire visibility is proven again. The capture on the I210 stayed up. */
    if (fresh != INVALID_HANDLE_VALUE) {
        sock = open_tx_socket(ipv4);
        if (sock != INVALID_SOCKET) {
            after_seq = g_next_seq;
            seen = tx_step(fresh, target.net_luid, sock, BASELINE_MS, "after the cycle", after_seq, &capture_alive);
            tx_restored = (unsigned long long)seen * 100 >= (unsigned long long)(g_next_seq - after_seq) * WIRE_MIN_PERCENT;
            closesocket(sock);
        }
        CloseHandle(fresh);
    }
    wire_stop();
    if (!tx_restored) {
        printf(TAG " TX to the wire not shown after the cycle\n");
        rc = 1;
    }
    printf(TAG " result: exit=%d (0 all restored, 1 registers/TX/cycle failed, 2 registers and TX restored, PHC not continuous)\n", rc);
    WSACleanup();
    return rc;
}
