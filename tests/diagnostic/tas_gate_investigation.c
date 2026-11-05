/**
 * @file tas_gate_investigation.c
 * @brief Exploratory: what opens I226 egress after SETUP_TAS? (#328)
 *
 * Not a test. No runner entry, no testcase verdicts, classified Diagnostic in $TestArtifactClassification. It uses the
 * generic Debug-only WRITE_REGISTER to learn register behaviour before any implementation, which regression tests must
 * not do. What it establishes is implemented later through a dedicated IOCTL and verified by hardware TDD. Output is
 * LOCAL_DIAGNOSTIC evidence, never HIL regression evidence.
 *
 * Sequence: tagged broadcast baseline seen on the I210, SETUP_TAS, then one candidate at a time, stopping when frames
 * reach the wire:
 *   A  per-queue windows: STQT=0, ENDQT=cycle
 *   B  shadow cycle and base time, write order and past-base handling as upstream igc_tsn.c. FUTSCDDIS and a future base
 *      are deliberately not used: upstream warns of a TX hang on I226 around them.
 *
 * Each step stays below the fill level seen on 2026-11-05, so a stalled ring cannot hide an opening window.
 *
 * Mutations: SETUP_TAS, the writes above, bounded broadcast and ICMP. Restoration is the NIC cycle of the I226 only. The
 * PHC restarts, a known defect, so this tool reports registers and not full restoration. Requires a Debug driver.
 *
 * Exit: 0 registers restored, 1 registers differ or the cycle failed, 3 not run (nothing mutated).
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

/* Everything this tool or SETUP_TAS writes */
static const REG g_regs[] = {
    {"TQAVCTRL", I226_TQAVCTRL}, {"QBVCYCLET", I226_QBVCYCLET}, {"QBVCYCLET_S", I226_QBVCYCLET_S},
    {"BASET_L", I226_BASET_L}, {"BASET_H", I226_BASET_H},
    {"STQT0", I226_STQT0}, {"STQT1", I226_STQT1}, {"STQT2", I226_STQT2}, {"STQT3", I226_STQT3},
    {"ENDQT0", I226_ENDQT0}, {"ENDQT1", I226_ENDQT1}, {"ENDQT2", I226_ENDQT2}, {"ENDQT3", I226_ENDQT3},
};
#define REG_COUNT (sizeof(g_regs) / sizeof(g_regs[0]))

static const uint32_t g_stqt[QUEUES] = {I226_STQT0, I226_STQT1, I226_STQT2, I226_STQT3};
static const uint32_t g_endqt[QUEUES] = {I226_ENDQT0, I226_ENDQT1, I226_ENDQT2, I226_ENDQT3};

static unsigned g_next_seq;
static volatile LONG g_armed;

typedef struct { DEVINST inst; BOOL enable; CONFIGRET result; } CM_CALL;

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

static BOOL write_reg(HANDLE h, uint32_t offset, uint32_t value, const char *name)
{
    AVB_REGISTER_REQUEST r = {0};
    DWORD br = 0;
    r.offset = offset;
    r.value = value;
    if (!DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL) || r.status != 0) {
        printf(TAG " write %s failed (error %lu)\n", name, GetLastError());
        return FALSE;
    }
    printf("    write %-12s 0x%05X = 0x%08X\n", name, offset, value);
    return TRUE;
}

static unsigned read_regs(HANDLE h, uint32_t *values, const char *label)
{
    unsigned i, unreadable = 0;

    printf("  --- registers: %s ---\n", label);
    for (i = 0; i < REG_COUNT; i++) {
        if (read_reg(h, g_regs[i].offset, &values[i])) {
            printf("    %-12s 0x%05X = 0x%08X\n", g_regs[i].name, g_regs[i].offset, values[i]);
        } else {
            printf("    %-12s 0x%05X unreadable\n", g_regs[i].name, g_regs[i].offset);
            unreadable++;
        }
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

/** Sends tagged broadcast for ms, waits, and returns the frames of this window seen on the wire */
static unsigned tx_step(HANDLE h, SOCKET sock, DWORD ms, const char *label, unsigned armed_first_seq)
{
    static unsigned char payload[PAYLOAD_BYTES];
    SOCKADDR_IN peer = {0};
    unsigned first = g_next_seq, sent = 0, seen, all = 0;
    long missing, last;
    LONG captured, from_target;
    uint32_t tdh = 0, tdt = 0;
    DWORD start = GetTickCount();

    peer.sin_family = AF_INET;
    peer.sin_port = htons(TAS_WIRE_UDP_PORT);
    peer.sin_addr.s_addr = INADDR_BROADCAST;
    while (GetTickCount() - start < ms) {
        tas_wire_fill(payload, sizeof(payload), g_wire_nonce, g_next_seq);
        if (sendto(sock, (const char *)payload, sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) != SOCKET_ERROR) {
            sent++;
            g_next_seq++;
        }
        Sleep(SEND_PERIOD_MS);
    }
    Sleep(DRAIN_MS);
    seen = wire_seen(first, first + sent, &missing, &last);
    if (armed_first_seq != g_next_seq) {
        long m2, l2;
        all = wire_seen(armed_first_seq, g_next_seq, &m2, &l2);
    }
    wire_counts(&captured, &from_target);
    (void)read_reg(h, I226_TDH0, &tdh);
    (void)read_reg(h, I226_TDT0, &tdt);
    printf(TAG " %s: sent=%u wireSeen=%u firstMissingSeq=%ld allArmedSeen=%u TDH0=%u TDT0=%u capturedTotal=%ld fromTargetTotal=%ld icmp=%ld/%ld\n",
           label, sent, seen, missing, all, tdh, tdt, captured, from_target, g_icmp_replied, g_icmp_sent);
    fflush(stdout);
    return seen;
}

/** Candidate A: every queue window covers the whole cycle */
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

/** Candidate B: shadow cycle, cycle, then base time at the next boundary, as upstream does for a base in the past */
static BOOL step_schedule(HANDLE h)
{
    unsigned long long systim = 0, base;

    if (!read_systim(h, &systim)) {
        printf(TAG " PHC unreadable, candidate B not applied\n");
        return FALSE;
    }
    base = (systim / CYCLE_NS + 1) * CYCLE_NS;
    return write_reg(h, I226_QBVCYCLET_S, CYCLE_NS, "QBVCYCLET_S") && write_reg(h, I226_QBVCYCLET, CYCLE_NS, "QBVCYCLET") &&
           write_reg(h, I226_BASET_H, (uint32_t)(base / NS_PER_SEC), "BASET_H") &&
           write_reg(h, I226_BASET_L, (uint32_t)(base % NS_PER_SEC), "BASET_L");
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

/** Waits for the adapter to return and reports the registers against the baseline */
static int restore(const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, const uint32_t *baseline)
{
    uint32_t after[REG_COUNT];
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
            if (read_regs(h, after, "after the cycle") == 0 && read_armed(h, &armed)) {
                for (i = 0; i < REG_COUNT; i++) {
                    if (after[i] != baseline[i]) {
                        printf(TAG " %s differs: 0x%08X, baseline 0x%08X\n", g_regs[i].name, after[i], baseline[i]);
                        differ++;
                    }
                }
                printf(TAG " registers differing=%u tasArmed=%u; the PHC restarted, not restored (known defect)\n", differ, armed);
                CloseHandle(h);
                return (differ == 0 && armed == 0) ? 0 : 1;
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

int main(void)
{
    AVB_ADAPTER_IDENTITY_REQUEST target, id;
    IOCTL_VERSION version = {0};
    uint32_t baseline[REG_COUNT], now[REG_COUNT];
    SOCKADDR_IN local = {0};
    WSADATA wsa;
    IN_ADDR ipv4;
    DEVINST inst;
    SOCKET sock;
    avb_u64 capture = 0;
    UINT32 i, count = 1;
    DWORD br = 0;
    HANDLE h;
    BOOL on = TRUE, have_target = FALSE, other_link = FALSE;
    unsigned armed = 1, armed_first, seen, baseline_seq;
    int rc;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    printf("=== IntelAvbFilter - exploratory TAS gate investigation (#328), %s ===\n", TAG);
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
        if (!have_target && (id.capabilities & INTEL_CAP_TSN_TAS)) {
            target = id;
            have_target = TRUE;
        }
    }
    if (!have_target || capture == 0 || !bind_luid(h, target.net_luid) || !find_devinst(target.net_luid, &inst) ||
        !find_addressing(target.net_luid, &ipv4, &other_link) || !other_link || !read_armed(h, &armed) || armed != 0) {
        printf(TAG " not run: needs a bound TAS adapter with link, an I210, a separate management link and tasArmed=0\n");
        CloseHandle(h);
        return 3;
    }
    if (read_regs(h, baseline, "baseline") != 0) {
        printf(TAG " not run: baseline registers unreadable\n");
        CloseHandle(h);
        return 3;
    }
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    local.sin_family = AF_INET;
    local.sin_addr = ipv4;
    if (sock == INVALID_SOCKET || setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on)) != 0 ||
        bind(sock, (SOCKADDR *)&local, sizeof(local)) != 0) {
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
    seen = tx_step(h, sock, BASELINE_MS, "baseline", g_next_seq);
    if ((unsigned long long)seen * 100 < (unsigned long long)(g_next_seq - baseline_seq) * WIRE_MIN_PERCENT) {
        printf(TAG " not run: the I210 did not see the baseline frames; nothing mutated\n");
        wire_stop();
        CloseHandle(h);
        return 3;
    }

    /* First mutation. From here the NIC cycle always runs. */
    InterlockedExchange(&g_armed, 1);
    printf("  --- SETUP_TAS ---\n");
    if (!arm_tas(h)) {
        printf(TAG " SETUP_TAS failed (error %lu)\n", GetLastError());
    }
    (void)read_regs(h, now, "after SETUP_TAS");
    armed_first = g_next_seq;
    seen = tx_step(h, sock, ARMED_MS, "SETUP_TAS only", armed_first);

    if (seen == 0) {
        printf("  --- candidate A: per-queue windows ---\n");
        if (step_windows(h)) {
            (void)read_regs(h, now, "after candidate A");
            seen = tx_step(h, sock, STEP_MS, "candidate A", armed_first);
        }
    }
    if (seen == 0) {
        printf("  --- candidate B: shadow cycle and base time ---\n");
        if (step_schedule(h)) {
            (void)read_regs(h, now, "after candidate B");
            seen = tx_step(h, sock, STEP_MS, "candidate B", armed_first);
        }
    }
    printf(TAG " egress %s\n", seen != 0 ? "opened: tagged frames reached the I210" : "stayed closed through both candidates");

    wire_stop();
    closesocket(sock);
    CloseHandle(h);
    rc = restore(&target, inst, baseline);
    WSACleanup();
    return rc;
}
