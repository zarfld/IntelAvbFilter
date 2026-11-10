/**
 * @file test_tas_activate.c
 * @brief IOCTL_AVB_TAS_ACTIVATE on a real adapter: first gate schedule, wire behaviour, state, refusal, cleanup (#357)
 *
 * One mode per run, each its own registered runner entry:
 *   --hold   TEST-TAS-API-004 (#369, Verifies #357 and #350), case TC-TASAPI-004
 *   --pass   TEST-TAS-API-005 (#370, Verifies #357), case TC-TASAPI-005
 *   --fault  the failed-rollback path of #357 (PR #397 review), case TC-TASACT-FAULT-001. Debug drivers fail schedule write
 *            13 and the first restore write on request (devices/i226_tas_activate.h); Release drivers must refuse that request.
 *
 * Checks, each with its own verdict line <case>-<check>:
 *   ACCESS   a handle opened for reading only is refused TAS_ACTIVATE (FILE_WRITE_ACCESS, #362) and still serves GET_TAS_STATE.
 *            The request is invalid, so nothing is written even if the access check were missing.
 *   COMMIT   two bound handles race the same request: exactly one COMMITTED, the other BUSY or NOT_CLEAN (ADR-TSN-002).
 *   STATE    GET_TAS_STATE reports ARMED and the windows as the gate list it must report (#350, PR #397 review).
 *   REFUSE   a further activation on the armed adapter is NOT_CLEAN and changes no reported state.
 *   WIRE     tagged broadcast seen by the I210 observer, judged by tests/ioctl/tas_activate_eval.h against BaseT and the window.
 *   STATUS   (#369 only) PENDING then INFERRED_ACTIVE: #350's status model is not implemented, so SKIP_NOT_IMPLEMENTED.
 *   CLEANUP  the test's own NIC cycle ran and the adapter came back: primary cleanup, owned by this test.
 *   RESTORE  restoration judged by tests/ioctl/tas_restoration_eval.h. No dedicated read-only snapshot of the TAS registers
 *            exists yet (#361), so the registers are UNVERIFIED and restoration is never claimed from GET_TAS_STATE.
 *
 * Schedule: 1 s cycle, one window 200 to 400 ms on all four queues, BaseT the first cycle boundary at least 1.5 s after the
 * PHC. That is the verified set of the E6 characterisation (LOCAL_DIAGNOSTIC, I226 0x125B REV_04, Debug), within the
 * window rule of #357. Adapters are chosen by INTEL_CAP_TSN_TAS; the observer is the I210, chosen by identity for targeting
 * only. The PCI revision and the loaded driver image with its SHA256 are printed as evidence, never used for selection.
 *
 * Mutations: TAS_ACTIVATE, bounded broadcast UDP and ICMP on the adapter under test, and the NIC cycle of that adapter
 * (CM_Disable_DevNode / CM_Enable_DevNode), which resets its PHC. The cycle runs after every TAS_ACTIVATE issued, whatever
 * it returned. Dedicated IOCTLs only: no READ_REGISTER, no WRITE_REGISTER. Runs on Debug and Release drivers.
 *
 * Exit: 0 all passed, 1 any nonpassing verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #357
 * Verifies: #350
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <cfgmgr32.h>
#include <psapi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../../include/intel_pci_ids.h"
#include "../ioctl/tas_restoration_eval.h"
#include "../ioctl/tas_activate_eval.h"
#include "tas_wire_observer.h"

#define DEVICE_NAME          "\\\\.\\IntelAvbFilter"
#define DRIVER_IMAGE         "intelavbfilter.sys"
#define NS_PER_MS            1000000LL
#define NS_PER_SEC           1000000000LL
#define CYCLE_NS             (1000LL * NS_PER_MS)
#define WINDOW_START_NS      (200LL * NS_PER_MS)
#define WINDOW_END_NS        (400LL * NS_PER_MS)
#define BASE_LEAD_NS         (1500LL * NS_PER_MS)    /* BaseT is the first cycle boundary at least this far ahead */
#define SEND_CYCLES_AFTER    3                       /* frames keep going until BaseT plus this many cycles and a half */
#define SEND_PERIOD_MS       50
#define SEND_BOUND_MS        20000                   /* the send phase ends here even if the PHC cannot be read */
#define DRAIN_MS             1500                    /* longer than a closed interval, so held frames leave before the end */
#define PAYLOAD_BYTES        256
#define FRAME_OVERHEAD_BYTES 38u                     /* MTU plus header, FCS, preamble and gap: the frame on the wire */
#define BASELINE_MS          2000
#define ANCHOR_READS         5
#define FAULT_WRITE          13u                     /* HOLD's commit write, so everything before it reached the adapter */
#define UNGATED_DELAY_US     500000LL                /* after the cycle: a gate of this schedule holds most frames longer */
#define PHC_TOLERANCE_NS     (2 * NS_PER_SEC)
#define DISABLE_BOUND_MS     120000
#define ENABLE_BOUND_MS      60000
#define RETURN_BOUND_MS      60000
#define SETTLE_MS            5000
#define FILETIME_TO_UNIX_US  11644473600000000LL
#define NET_CLASS_KEY        "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\"

typedef enum { MODE_NONE = 0, MODE_HOLD, MODE_PASS, MODE_FAULT } RUN_MODE;

static RUN_MODE g_mode;
static const char *g_case;
static int g_failed, g_blocked, g_ran;
static avb_u64 g_capture_luid;
static unsigned g_next_seq;
static LONGLONG g_send_us[WIRE_MAX_FRAMES];

/* Non-negative while a TAS_ACTIVATE has been issued and the cleanup has not reported */
static volatile LONG g_pending_adapter = -1;
static volatile LONG g_pending_did;

static void report_abort(void)
{
    LONG adapter = InterlockedExchange(&g_pending_adapter, -1);
    if (adapter >= 0) {
        printf("[INCONCLUSIVE_RESTORATION] %s-RESTORE/adapter:%ld did=0x%04lX: test aborted after TAS_ACTIVATE, its cleanup did not run; "
               "operator: NIC cycle of this adapter, then TC-TSNREG-001\n", g_case, adapter, g_pending_did);
        fflush(stdout);
    }
}

static BOOL WINAPI on_console_ctrl(DWORD type) { (void)type; report_abort(); return FALSE; }
static LONG WINAPI on_crash(EXCEPTION_POINTERS *info) { (void)info; report_abort(); return EXCEPTION_CONTINUE_SEARCH; }

/* One verdict line. Nonpassing tags count as failures, BLOCKED separately */
static void verdict(const char *tag, const char *check, UINT32 adapter, avb_u16 did, const char *reason)
{
    printf("[%s] %s-%s/adapter:%u did=0x%04X%s%s\n", tag, g_case, check, adapter, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
    if (strcmp(tag, "BLOCKED") == 0) {
        g_blocked++;
    } else if (strcmp(tag, "PASS") != 0) {
        g_failed++;
    }
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

static HANDLE open_device(DWORD access)
{
    return CreateFileA(DEVICE_NAME, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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

static HANDLE open_bound(DWORD access, avb_u64 luid)
{
    HANDLE h = open_device(access);
    if (h != INVALID_HANDLE_VALUE && !bind_luid(h, luid)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static BOOL read_state(HANDLE h, AVB_TAS_STATE *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_TAS_STATE, s, sizeof(*s), s, sizeof(*s), &br, NULL) && s->status == 0;
}

static BOOL read_phc(HANDLE h, long long *phc)
{
    AVB_CLOCK_CONFIG c;
    DWORD br = 0;
    ZeroMemory(&c, sizeof(c));
    if (!DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &br, NULL) || c.status != 0) {
        return FALSE;
    }
    *phc = (long long)c.systim;
    return TRUE;
}

static BOOL read_stats(HANDLE h, AVB_DRIVER_STATISTICS *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, s, sizeof(*s), s, sizeof(*s), &br, NULL);
}

static BOOL read_if(avb_u64 luid, MIB_IF_ROW2 *row)
{
    ZeroMemory(row, sizeof(*row));
    row->InterfaceLuid.Value = luid;
    return GetIfEntry2(row) == NO_ERROR;
}

/** The tightest of a few bracketed PHC reads: host midpoint and PHC, the line anchor for capture times */
static BOOL take_anchor(HANDLE h, TAS_ACT_ANCHOR *a, const char *label)
{
    LONGLONG best = -1;
    int i;

    for (i = 0; i < ANCHOR_READS; i++) {
        LONGLONG before = host_us(), after;
        long long phc;
        if (!read_phc(h, &phc)) {
            continue;
        }
        after = host_us();
        if (best < 0 || after - before < best) {
            best = after - before;
            a->host_us = before + (after - before) / 2;
            a->phc_ns = phc;
        }
    }
    if (best < 0) {
        ZeroMemory(a, sizeof(*a));
        printf("    anchor %s: unavailable (all PHC reads failed)\n", label);
        return FALSE;
    }
    printf("    anchor %s: hostUs=%lld phcNs=%lld bracketUs=%lld\n", label, a->host_us, a->phc_ns, best);
    return TRUE;
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

static SOCKET open_tx_socket(IN_ADDR ipv4)
{
    SOCKADDR_IN local = {0};
    BOOL on = TRUE;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD start = GetTickCount();

    local.sin_family = AF_INET;
    local.sin_addr = ipv4;
    if (sock == INVALID_SOCKET || setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on)) != 0) {
        if (sock != INVALID_SOCKET) { closesocket(sock); }
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

/** One tagged broadcast frame; its host send time is kept by sequence number */
static BOOL send_tagged(SOCKET sock, unsigned nonce)
{
    static unsigned char payload[PAYLOAD_BYTES];
    SOCKADDR_IN peer = {0};

    if (g_next_seq >= WIRE_MAX_FRAMES) {
        return FALSE;
    }
    peer.sin_family = AF_INET;
    peer.sin_port = htons(TAS_WIRE_UDP_PORT);
    peer.sin_addr.s_addr = INADDR_BROADCAST;
    tas_wire_fill(payload, sizeof(payload), nonce, g_next_seq);
    g_send_us[g_next_seq] = host_us();
    if (sendto(sock, (const char *)payload, (int)sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) == SOCKET_ERROR) {
        return FALSE;
    }
    g_next_seq++;
    return TRUE;
}

/** Sends for ms, drains, and returns how many of these frames the observer saw. delayed: seen later than UNGATED_DELAY_US */
static unsigned send_window(SOCKET sock, unsigned nonce, DWORD ms, unsigned *sent, unsigned *delayed)
{
    unsigned first = g_next_seq, seq, seen = 0;
    DWORD start = GetTickCount();

    *delayed = 0;
    while (GetTickCount() - start < ms) {
        (void)send_tagged(sock, nonce);
        Sleep(SEND_PERIOD_MS);
    }
    Sleep(DRAIN_MS);
    *sent = g_next_seq - first;
    for (seq = first; seq < g_next_seq; seq++) {
        if (g_wire_seen[seq] != 0) {
            seen++;
            if (g_wire_rx_us[seq] - g_send_us[seq] > UNGATED_DELAY_US) {
                (*delayed)++;
            }
        }
    }
    return seen;
}

/** Image and SHA256 of the driver Windows has loaded: the runner's System32 hash may name another file (#357 HIL record) */
static void driver_provenance(void)
{
    LPVOID bases[1024];
    DWORD needed = 0, i;
    char name[MAX_PATH], path[MAX_PATH * 2];

    if (!EnumDeviceDrivers(bases, sizeof(bases), &needed)) {
        printf("    [EVIDENCE] loaded driver image: unknown (EnumDeviceDrivers error %lu)\n", GetLastError());
        return;
    }
    for (i = 0; i < needed / sizeof(bases[0]); i++) {
        if (GetDeviceDriverBaseNameA(bases[i], name, sizeof(name)) == 0 || _stricmp(name, DRIVER_IMAGE) != 0 ||
            GetDeviceDriverFileNameA(bases[i], name, sizeof(name)) == 0) {
            continue;
        }
        if (_strnicmp(name, "\\SystemRoot\\", 12) == 0) {
            char root[MAX_PATH];
            GetWindowsDirectoryA(root, sizeof(root));
            sprintf_s(path, sizeof(path), "%s\\%s", root, name + 12);
        } else {
            strcpy_s(path, sizeof(path), name);
        }
        {
            BCRYPT_ALG_HANDLE alg = NULL;
            BCRYPT_HASH_HANDLE hash = NULL;
            unsigned char digest[32], buf[65536];
            HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            DWORD got = 0;
            BOOL ok = f != INVALID_HANDLE_VALUE && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
                      BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0;
            int k;

            while (ok) {
                if (!ReadFile(f, buf, sizeof(buf), &got, NULL)) {
                    ok = FALSE;
                    break;
                }
                if (got == 0) { break; }
                ok = BCryptHashData(hash, buf, got, 0) == 0;
            }
            ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
            printf("    [EVIDENCE] loaded driver image: %s sha256=", path);
            for (k = 0; ok && k < 32; k++) { printf("%02X", digest[k]); }
            printf("%s\n", ok ? "" : "unreadable");
            if (hash != NULL) { BCryptDestroyHash(hash); }
            if (alg != NULL) { BCryptCloseAlgorithmProvider(alg, 0); }
            if (f != INVALID_HANDLE_VALUE) { CloseHandle(f); }
        }
        return;
    }
    printf("    [EVIDENCE] loaded driver image: %s is not loaded\n", DRIVER_IMAGE);
}

typedef struct { DEVINST inst; BOOL enable; CONFIGRET result; } CM_CALL;

static DWORD WINAPI cm_thread(LPVOID arg)
{
    CM_CALL *call = (CM_CALL *)arg;
    call->result = call->enable ? CM_Enable_DevNode(call->inst, 0) : CM_Disable_DevNode(call->inst, 0);
    return 0;
}

/** One device-node call with a bound: a stalled disable must end in a verdict, not a hung test */
static BOOL cm_bounded(DEVINST inst, BOOL enable, DWORD bound_ms)
{
    CM_CALL *call = (CM_CALL *)calloc(1, sizeof(*call));
    HANDLE thread;
    BOOL ok;

    if (call == NULL) {
        return FALSE;
    }
    call->inst = inst;
    call->enable = enable;
    call->result = CR_FAILURE;
    thread = CreateThread(NULL, 0, cm_thread, call, 0, NULL);
    if (thread == NULL) {
        free(call);
        return FALSE;
    }
    if (WaitForSingleObject(thread, bound_ms) != WAIT_OBJECT_0) {
        CloseHandle(thread);   /* call must outlive the worker thread; leaked on purpose */
        printf("    %s did not return within %lu ms\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", bound_ms);
        return FALSE;
    }
    CloseHandle(thread);
    printf("    %s result=0x%08X\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", (unsigned)call->result);
    ok = (call->result == CR_SUCCESS);
    free(call);
    return ok;
}

/** NET_LUID to exactly this adapter's device node, never by model or name. Prints the instance, which carries the revision */
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
    printf("    [EVIDENCE] device instance: %s\n", pnp);
    return CM_Locate_DevNodeA(inst, pnp, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

/** Waits until the adapter is back with the same identity and a handle is bound to it */
static HANDLE wait_for_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *want)
{
    DWORD start = GetTickCount();

    while (GetTickCount() - start < RETURN_BOUND_MS) {
        HANDLE h = open_device(GENERIC_READ | GENERIC_WRITE);
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

static const char *result_name(avb_u32 r)
{
    switch (r) {
    case AVB_TAS_ACT_COMMITTED:           return "COMMITTED";
    case AVB_TAS_ACT_INVALID:             return "INVALID";
    case AVB_TAS_ACT_NOT_CLEAN:           return "NOT_CLEAN";
    case AVB_TAS_ACT_FAILED_RESTORED:     return "FAILED_RESTORED";
    case AVB_TAS_ACT_FAILED_NOT_RESTORED: return "FAILED_NOT_RESTORED";
    case AVB_TAS_ACT_BASE_PASSED:         return "BASE_PASSED";
    case AVB_TAS_ACT_COMMITTED_UNTIMED:   return "COMMITTED_UNTIMED";
    case AVB_TAS_ACT_BUSY:                return "BUSY";
    default:                              return "UNKNOWN";
    }
}

typedef struct {
    HANDLE h, go;
    AVB_TAS_ACTIVATE_REQUEST r;
    BOOL ok;
    DWORD error;
} ACT_CALL;

static BOOL activate(HANDLE h, AVB_TAS_ACTIVATE_REQUEST *r, DWORD *error)
{
    DWORD br = 0;
    *error = ERROR_SUCCESS;
    if (!DeviceIoControl(h, IOCTL_AVB_TAS_ACTIVATE, r, sizeof(*r), r, sizeof(*r), &br, NULL)) {
        *error = GetLastError();
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI act_thread(LPVOID arg)
{
    ACT_CALL *c = (ACT_CALL *)arg;
    (void)WaitForSingleObject(c->go, INFINITE);
    c->ok = activate(c->h, &c->r, &c->error);
    return 0;
}

static void print_call(const char *label, BOOL ok, DWORD error, const AVB_TAS_ACTIVATE_REQUEST *r)
{
    printf("    %s: ok=%d win32=%lu result=%s(%u) status=0x%08X phcAtCommit=%llu\n", label, ok, error,
           ok ? result_name(r->result) : "-", ok ? r->result : 0, ok ? r->status : 0, ok ? r->phc_at_commit_ns : 0ULL);
}

static void build_request(AVB_TAS_ACTIVATE_REQUEST *r, avb_u32 pre, long long base, avb_u32 mbps, avb_u32 frame)
{
    unsigned q;

    ZeroMemory(r, sizeof(*r));
    r->version = AVB_TAS_ACTIVATE_VERSION;
    r->pre_activation = pre;
    r->base_time_ns = (avb_u64)base;
    r->cycle_ns = (avb_u32)CYCLE_NS;
    r->link_mbps = mbps;
    r->max_frame_bytes = frame;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        r->window_start_ns[q] = (avb_u32)WINDOW_START_NS;
        r->window_end_ns[q] = (avb_u32)WINDOW_END_NS;
    }
}

/** What GET_TAS_STATE must report for the request: closed, open on queues 0 to 3, closed. Written out, not derived */
static BOOL state_matches(const AVB_TAS_STATE *s, long long base)
{
    unsigned i;
    BOOL ok = s->armed == AVB_TAS_STATE_ARMED && s->config.base_time_s == (uint64_t)(base / NS_PER_SEC) &&
              s->config.base_time_ns == (uint32_t)(base % NS_PER_SEC) && s->config.cycle_time_s == 1 && s->config.cycle_time_ns == 0 &&
              s->config.gate_states[0] == 0x00 && s->config.gate_durations[0] == (uint32_t)WINDOW_START_NS &&
              s->config.gate_states[1] == 0x0F && s->config.gate_durations[1] == (uint32_t)(WINDOW_END_NS - WINDOW_START_NS) &&
              s->config.gate_states[2] == 0x00 && s->config.gate_durations[2] == (uint32_t)(CYCLE_NS - WINDOW_END_NS);
    for (i = 3; i < 8; i++) {
        ok = ok && s->config.gate_states[i] == 0 && s->config.gate_durations[i] == 0;
    }
    return ok;
}

static void print_state(const char *label, const AVB_TAS_STATE *s)
{
    unsigned i;
    printf("    GET_TAS_STATE %s: armed=%u base=%llu.%09u cycle=%u.%09u gcl=", label, s->armed, (unsigned long long)s->config.base_time_s,
           s->config.base_time_ns, s->config.cycle_time_s, s->config.cycle_time_ns);
    for (i = 0; i < 8; i++) {
        printf("%s0x%02X/%u", i ? "," : "", s->config.gate_states[i], s->config.gate_durations[i]);
    }
    printf("\n");
}

/** The check of a handle without write access. Invalid request: even a missing access check writes nothing */
static void check_access(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    AVB_TAS_ACTIVATE_REQUEST r;
    AVB_TAS_STATE s;
    DWORD error;
    BOOL ok, state_ok;
    HANDLE h = open_bound(GENERIC_READ, id->net_luid);

    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "ACCESS", index, id->device_id, "cannot open and bind a read-only handle");
        return;
    }
    ZeroMemory(&r, sizeof(r));   /* version 0: INVALID if it reached the driver */
    ok = activate(h, &r, &error);
    state_ok = read_state(h, &s);
    printf("    read-only handle: TAS_ACTIVATE ok=%d win32=%lu result=%u; GET_TAS_STATE ok=%d\n", ok, error, r.result, state_ok);
    CloseHandle(h);
    if (!ok && error == ERROR_ACCESS_DENIED && state_ok) {
        verdict("PASS", "ACCESS", index, id->device_id, NULL);
    } else {
        verdict("FAIL", "ACCESS", index, id->device_id, ok ? "TAS_ACTIVATE reached the driver through a handle without write access"
                                                           : (error != ERROR_ACCESS_DENIED ? "refused, but not with ERROR_ACCESS_DENIED"
                                                                                            : "the read-only handle no longer serves GET_TAS_STATE"));
    }
}

/** Primary cleanup and restoration: NIC cycle, then everything this test can observe without raw register access. Closes h */
static void cleanup_and_verify(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, IN_ADDR ipv4,
                               const AVB_DRIVER_STATISTICS *stats_before, long long phc_before, LARGE_INTEGER qpc_before)
{
    TAS_RESTORATION_EVIDENCE e;
    DEVINST inst = 0;
    HANDLE fresh;
    MIB_IF_ROW2 row;
    DWORD start;

    ZeroMemory(&e, sizeof(e));
    printf("\n  --- Primary cleanup: NIC cycle of this adapter ---\n");
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
    InterlockedExchange(&g_pending_adapter, -1);
    verdict(e.cycle_completed && e.adapter_returned ? "PASS" : "CLEANUP_FAILED", "CLEANUP", index, id->device_id,
            e.cycle_completed && e.adapter_returned ? NULL
                                                    : "the NIC cycle did not complete or the adapter did not return; a timed-out disable may still be "
                                                      "pending in Windows. Operator: NIC cycle once it ends, then TC-TSNREG-001");

    if (e.adapter_returned) {
        AVB_DRIVER_STATISTICS stats_after;
        AVB_TAS_STATE s;
        long long phc_after;

        start = GetTickCount();
        while (GetTickCount() - start < RETURN_BOUND_MS) {
            if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
                e.link_up = TRUE;
                break;
            }
            Sleep(1000);
        }
        Sleep(SETTLE_MS);
        if (read_stats(fresh, &stats_after)) {   /* the statistics live in the adapter context: lower means a new one */
            e.attach_observed = stats_after.IoctlCount < stats_before->IoctlCount;
            printf("    IoctlCount %llu -> %llu\n", stats_before->IoctlCount, stats_after.IoctlCount);
        }
        if (read_state(fresh, &s)) {
            e.driver_disarmed = (s.armed == AVB_TAS_STATE_CLEAN);
            print_state("after the cycle", &s);
        } else {
            e.armed_unreadable = TRUE;
        }
        /* No dedicated read-only snapshot covers the TAS registers yet (#361): unmeasured, never assumed */
        e.tsn_mode_clear = TRUE;
        e.regs_compared = 0;
        printf("    [EVIDENCE] TAS registers after the cycle: UNVERIFIED, no dedicated read-only snapshot of them exists yet (#361)\n");
        if (read_phc(fresh, &phc_after)) {
            LARGE_INTEGER now, freq;
            long long wall_ns, drift_ns;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            wall_ns = (long long)((double)(now.QuadPart - qpc_before.QuadPart) * 1e9 / (double)freq.QuadPart);
            drift_ns = (phc_after - phc_before) - wall_ns;
            e.phc_observed = TRUE;
            e.phc_continuous = drift_ns > -PHC_TOLERANCE_NS && drift_ns < PHC_TOLERANCE_NS;
            printf("    PHC before=%lld after=%lld wallNs=%lld drift=%lld\n", phc_before, phc_after, wall_ns, drift_ns);
        }
        if (e.link_up) {
            SOCKET sock = open_tx_socket(ipv4);
            unsigned nonce = GetTickCount() ^ (GetCurrentProcessId() << 8), sent = 0, delayed = 0, seen;
            LONG captured = 0, from_target = 0;

            if (sock != INVALID_SOCKET && wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) != 0) {
                seen = send_window(sock, nonce, BASELINE_MS, &sent, &delayed);
                wire_counts(&captured, &from_target);
                wire_stop();
                e.tx_probed = captured != 0;
                e.tx_healthy = (unsigned long long)seen * 100 >= (unsigned long long)sent * TAS_ACT_SEEN_MIN_PERCENT && delayed == 0;
                printf("    TX after the cycle: sent=%u seen=%u delayedOver%lldms=%u captured=%ld\n", sent, seen, UNGATED_DELAY_US / 1000,
                       delayed, captured);
            } else {
                printf("    TX after the cycle: not probed, no socket or no observer\n");
            }
            if (sock != INVALID_SOCKET) { closesocket(sock); }
        }
        CloseHandle(fresh);
    }
    printf("    evidence: cycle=%d returned=%d attach=%d disarmed=%d armedUnreadable=%d regsCompared=%u(UNVERIFIED, #361) "
           "phcObserved=%d phcContinuous=%d link=%d txProbed=%d txHealthy=%d\n", e.cycle_completed, e.adapter_returned, e.attach_observed,
           e.driver_disarmed, e.armed_unreadable, e.regs_compared, e.phc_observed, e.phc_continuous, e.link_up, e.tx_probed, e.tx_healthy);
    switch (tas_restoration_outcome(&e)) {
    case TAS_RESTORATION_VERIFIED:   /* unreachable while regs_compared is 0; kept so a future snapshot cannot skip this line */
        verdict("PASS", "RESTORE", index, id->device_id, "observed state restored");
        break;
    case TAS_RESTORATION_INCONCLUSIVE:
        verdict("INCONCLUSIVE_RESTORATION", "RESTORE", index, id->device_id,
                "the TAS registers are UNVERIFIED (no dedicated read-only snapshot, #361); see the evidence line");
        break;
    default:
        verdict("CLEANUP_FAILED", "RESTORE", index, id->device_id,
                "restoration demonstrably incomplete, see the evidence line (a PHC reset alone prevents it, D9); the TAS registers are "
                "UNVERIFIED (#361)");
        break;
    }
}

/** Frames sent after the activation returned, with PHC times on the line through the two anchors */
static unsigned collect_frames(unsigned first, unsigned end, const TAS_ACT_ANCHOR *a, const TAS_ACT_ANCHOR *b, TAS_ACT_FRAME *out)
{
    unsigned seq, n = 0;

    for (seq = first; seq < end && seq < WIRE_MAX_FRAMES; seq++) {
        TAS_ACT_FRAME *f = &out[n];
        if (!tas_act_phc_at(a, b, g_send_us[seq], &f->sent_ns)) {
            return 0;
        }
        f->seen = g_wire_seen[seq] != 0 && tas_act_phc_at(a, b, g_wire_rx_us[seq], &f->arrival_ns);
        n++;
    }
    return n;
}

static void run_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    static TAS_ACT_FRAME frames[WIRE_MAX_FRAMES];
    AVB_DRIVER_STATISTICS stats;
    AVB_TAS_STATE s;
    AVB_TAS_ACTIVATE_REQUEST req;
    TAS_ACT_ANCHOR before_anchor, after_anchor;
    MIB_IF_ROW2 row;
    LARGE_INTEGER qpc;
    IN_ADDR ipv4;
    SOCKET sock;
    HANDLE h, h2;
    BOOL other_link = FALSE;
    long long phc = 0, base;
    unsigned nonce, sent = 0, seen, delayed = 0, first_after = 0;
    avb_u32 mbps, frame;
    DEVINST inst;
    LONG captured = 0, from_target = 0;

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u ===\n", index, id->device_id, id->net_luid, id->if_index);
    (void)find_devinst(id->net_luid, &inst);   /* the revision is evidence; selection is by capability */
    g_ran++;

    /* Everything up to the activation is read-only or bounded TX: a BLOCKED here leaves the adapter untouched */
    h = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "cannot bind a handle to the adapter");
        return;
    }
    if (!read_if(id->net_luid, &row) || row.OperStatus != IfOperStatusUp || !find_addressing(id->net_luid, &ipv4, &other_link)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "adapter link is down or it has no IPv4 address");
        CloseHandle(h);
        return;
    }
    if (!other_link) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "no independent management link; the NIC cycle would isolate the host");
        CloseHandle(h);
        return;
    }
    mbps = (avb_u32)(row.TransmitLinkSpeed / 1000000ULL);
    frame = (avb_u32)row.Mtu + FRAME_OVERHEAD_BYTES;
    printf("    link=%u Mbit/s mtu=%lu frameOnWire=%u\n", mbps, row.Mtu, frame);
    if (!read_state(h, &s) || s.armed != AVB_TAS_STATE_CLEAN) {   /* pre-state proved by the TAS query, as #369 requires */
        verdict("BLOCKED", "COMMIT", index, id->device_id, "the TAS query does not report a clean adapter, or cannot be read");
        CloseHandle(h);
        return;
    }
    print_state("before", &s);
    if (g_mode != MODE_FAULT) {
        check_access(index, id);
    }
    sock = open_tx_socket(ipv4);
    nonce = GetTickCount() ^ GetCurrentProcessId();
    if (sock == INVALID_SOCKET || wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) == 0) {
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        wire_stop();
        verdict("BLOCKED", "COMMIT", index, id->device_id, "no TX socket, or the I210 capture could not be started");
        CloseHandle(h);
        return;
    }
    seen = send_window(sock, nonce, BASELINE_MS, &sent, &delayed);
    printf("    baseline: sent=%u seen=%u\n", sent, seen);
    if (sent == 0 || (unsigned long long)seen * 100 < (unsigned long long)sent * TAS_ACT_SEEN_MIN_PERCENT) {
        wire_stop();
        closesocket(sock);
        verdict("BLOCKED", "COMMIT", index, id->device_id, "the I210 did not see the baseline broadcast frames; the wire proves nothing here");
        CloseHandle(h);
        return;
    }
    h2 = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (h2 == INVALID_HANDLE_VALUE || !read_stats(h, &stats) || !take_anchor(h, &before_anchor, "before") || !read_phc(h, &phc)) {
        if (h2 != INVALID_HANDLE_VALUE) { CloseHandle(h2); }
        wire_stop();
        closesocket(sock);
        verdict("BLOCKED", "COMMIT", index, id->device_id, "second handle, statistics or PHC unavailable before the activation");
        CloseHandle(h);
        return;
    }
    QueryPerformanceCounter(&qpc);
    base = (phc + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    build_request(&req, g_mode == MODE_PASS ? AVB_TAS_PRE_PASS : AVB_TAS_PRE_HOLD, base, mbps, frame);
    printf("    request: pre=%u base=%lld (PHC %lld, lead %lld ms) cycle=%u window=%lld..%lld ms link=%u frame=%u\n", req.pre_activation, base,
           phc, (base - phc) / NS_PER_MS, req.cycle_ns, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, mbps, frame);

    /* First mutation. From here the cleanup always runs, whatever TAS_ACTIVATE returns */
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);

    if (g_mode == MODE_FAULT) {
        AVB_TAS_ACTIVATE_REQUEST again;
        DWORD error, error2;
        BOOL ok, ok2, debug_driver = FALSE;
        IOCTL_VERSION version = {0};
        DWORD br = 0;

        if (DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL)) {
            debug_driver = (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;
        }
        wire_stop();
        closesocket(sock);
        CloseHandle(h2);
        req.reserved = FAULT_WRITE;
        printf("\n  --- TAS_ACTIVATE with the fault selector %u (%s driver) ---\n", FAULT_WRITE, debug_driver ? "Debug" : "Release");
        ok = activate(h, &req, &error);
        print_call("fault request", ok, error, &req);
        if (!debug_driver) {
            BOOL refused = ok && req.result == AVB_TAS_ACT_INVALID && read_state(h, &s) && s.armed == AVB_TAS_STATE_CLEAN;
            verdict(refused ? "PASS" : "FAIL", "RELEASE", index, id->device_id,
                    refused ? NULL : "a Release driver must refuse a nonzero reserved word as INVALID and stay CLEAN");
        } else {
            BOOL state_ok;
            verdict(ok && req.result == AVB_TAS_ACT_FAILED_NOT_RESTORED ? "PASS" : "FAIL", "RESULT", index, id->device_id,
                    ok && req.result == AVB_TAS_ACT_FAILED_NOT_RESTORED ? NULL : "the injected failure is not reported as FAILED_NOT_RESTORED");
            state_ok = read_state(h, &s);
            if (state_ok) { print_state("after the failure", &s); }
            verdict(state_ok && s.armed == AVB_TAS_STATE_DIRTY ? "PASS" : "FAIL", "STATE", index, id->device_id,
                    state_ok && s.armed == AVB_TAS_STATE_DIRTY ? NULL : "GET_TAS_STATE does not report DIRTY (2)");
            build_request(&again, AVB_TAS_PRE_HOLD, base + CYCLE_NS, mbps, frame);
            ok2 = activate(h, &again, &error2);
            print_call("activation on the dirty adapter", ok2, error2, &again);
            state_ok = read_state(h, &s);
            verdict(ok2 && again.result == AVB_TAS_ACT_NOT_CLEAN && state_ok && s.armed == AVB_TAS_STATE_DIRTY ? "PASS" : "FAIL", "REFUSE", index,
                    id->device_id, ok2 && again.result == AVB_TAS_ACT_NOT_CLEAN && state_ok && s.armed == AVB_TAS_STATE_DIRTY ? NULL
                                   : "a dirty adapter must refuse activation as NOT_CLEAN and stay DIRTY until recovered");
        }
        cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc);
        return;
    }

    {
        ACT_CALL calls[2];
        HANDLE threads[2], go = CreateEventA(NULL, TRUE, FALSE, NULL);
        unsigned committed = 0, refused = 0, k;
        AVB_TAS_ACTIVATE_REQUEST again;
        DWORD error;
        BOOL ok, armed_ok;

        printf("\n  --- TAS_ACTIVATE from two handles at once ---\n");
        for (k = 0; k < 2; k++) {
            calls[k].h = k == 0 ? h : h2;
            calls[k].go = go;
            calls[k].r = req;
            calls[k].ok = FALSE;
            calls[k].error = ERROR_SUCCESS;
            threads[k] = go != NULL ? CreateThread(NULL, 0, act_thread, &calls[k], 0, NULL) : NULL;
        }
        if (go != NULL) { SetEvent(go); }
        for (k = 0; k < 2; k++) {
            if (threads[k] != NULL) {
                (void)WaitForSingleObject(threads[k], INFINITE);
                CloseHandle(threads[k]);
            }
            print_call(k == 0 ? "handle 1" : "handle 2", calls[k].ok, calls[k].error, &calls[k].r);
            if (calls[k].ok && calls[k].r.result == AVB_TAS_ACT_COMMITTED) { committed++; req = calls[k].r; }
            if (calls[k].ok && (calls[k].r.result == AVB_TAS_ACT_BUSY || calls[k].r.result == AVB_TAS_ACT_NOT_CLEAN)) { refused++; }
        }
        if (go != NULL) { CloseHandle(go); }
        CloseHandle(h2);
        first_after = g_next_seq;
        verdict(committed == 1 && refused == 1 ? "PASS" : "FAIL", "COMMIT", index, id->device_id,
                committed == 1 && refused == 1 ? NULL : "expected exactly one COMMITTED and one BUSY or NOT_CLEAN; see the two calls above");

        armed_ok = read_state(h, &s);
        if (armed_ok) { print_state("after the activation", &s); }
        verdict(armed_ok && state_matches(&s, base) ? "PASS" : "FAIL", "STATE", index, id->device_id,
                armed_ok && state_matches(&s, base) ? NULL : "GET_TAS_STATE does not report ARMED with the requested gate list");

        build_request(&again, req.pre_activation, base + CYCLE_NS, mbps, frame);
        ok = activate(h, &again, &error);
        print_call("activation on the armed adapter", ok, error, &again);
        armed_ok = read_state(h, &s);
        verdict(ok && again.result == AVB_TAS_ACT_NOT_CLEAN && armed_ok && state_matches(&s, base) ? "PASS" : "FAIL", "REFUSE", index,
                id->device_id, ok && again.result == AVB_TAS_ACT_NOT_CLEAN && armed_ok && state_matches(&s, base) ? NULL
                               : "an armed adapter must refuse activation as NOT_CLEAN and keep the reported schedule");
    }

    {
        DWORD start = GetTickCount();
        long long now = 0;
        TAS_ACT_WIRE_COUNTS c;
        TAS_ACT_WIRE_CLASS w;
        unsigned n;
        char reason[256];

        printf("\n  --- Tagged frames across BaseT ---\n");
        while (GetTickCount() - start < SEND_BOUND_MS && (!read_phc(h, &now) || now < base + SEND_CYCLES_AFTER * CYCLE_NS + CYCLE_NS / 2)) {
            (void)send_tagged(sock, nonce);
            Sleep(SEND_PERIOD_MS);
        }
        Sleep(DRAIN_MS);
        wire_counts(&captured, &from_target);
        (void)take_anchor(h, &after_anchor, "after");
        wire_stop();
        closesocket(sock);
        n = collect_frames(first_after, g_next_seq, &before_anchor, &after_anchor, frames);
        w = tas_act_wire(req.pre_activation, frames, n, captured != 0, base, CYCLE_NS, WINDOW_START_NS, WINDOW_END_NS, TAS_ACT_EDGE_MARGIN_NS, &c);
        printf("    wire: %s frames=%u seen=%u early=%u heldBeforeBase=%u flowed=%u released=%u inside=%u nearEdge=%u outside=%u captured=%ld "
               "fromTarget=%ld\n", tas_act_wire_name(w), c.frames, c.seen, c.early, c.held_before_base, c.flowed, c.released, c.inside,
               c.near_edge, c.outside, captured, from_target);
        sprintf_s(reason, sizeof(reason), "%s; see the wire line", tas_act_wire_name(w));
        verdict(w == TAS_ACT_WIRE_GATED ? "PASS" : (w == TAS_ACT_WIRE_UNOBSERVED ? "INCONCLUSIVE" : "FAIL"), "WIRE", index, id->device_id,
                w == TAS_ACT_WIRE_GATED ? NULL : reason);
        if (g_mode == MODE_HOLD) {
            verdict("SKIP_NOT_IMPLEMENTED", "STATUS", index, id->device_id,
                    "#369 expects PENDING then INFERRED_ACTIVE; the schedule status model of #350 is not implemented, GET_TAS_STATE has no status");
        }
    }
    cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc);
}

int main(int argc, char **argv)
{
    AVB_ADAPTER_IDENTITY_REQUEST ids[8];
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i, count = 1, stored = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    if (argc == 2 && strcmp(argv[1], "--hold") == 0) { g_mode = MODE_HOLD; g_case = "TC-TASAPI-004"; }
    if (argc == 2 && strcmp(argv[1], "--pass") == 0) { g_mode = MODE_PASS; g_case = "TC-TASAPI-005"; }
    if (argc == 2 && strcmp(argv[1], "--fault") == 0) { g_mode = MODE_FAULT; g_case = "TC-TASACT-FAULT-001"; }
    printf("=== IntelAvbFilter - IOCTL_AVB_TAS_ACTIVATE on real hardware (#357) ===\n");
    if (g_mode == MODE_NONE) {
        printf("[BLOCKED] TC-TASAPI-004: usage: test_tas_activate --hold | --pass | --fault\n");
        return 3;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[BLOCKED] %s: Winsock unavailable\n", g_case);
        return 3;
    }
    h = open_device(GENERIC_READ | GENERIC_WRITE);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] %s: cannot open %s (error %lu)\n", g_case, DEVICE_NAME, GetLastError());
        return 3;
    }
    if (DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL)) {
        printf("    [EVIDENCE] driver version %u.%u flags=0x%04X (%s build)\n", version.Major, version.Minor, version.Flags,
               (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) ? "Debug" : "Release");
    }
    driver_provenance();

    /* Identities first: a NIC cycle may reorder the enumeration */
    for (i = 0; i < count && stored < ARRAYSIZE(ids); i++) {
        AVB_ADAPTER_IDENTITY_REQUEST id;
        if (!identity_at(h, i, &id)) {
            break;
        }
        count = id.count;
        if (g_capture_luid == 0 && id.device_id == INTEL_DEV_I210_AT) {
            g_capture_luid = id.net_luid;
        }
        if (id.capabilities & INTEL_CAP_TSN_TAS) {
            ids[stored++] = id;
        } else {
            printf("[SKIP_UNSUPPORTED] %s-COMMIT/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_TSN_TAS\n", g_case, i, id.device_id);
        }
    }
    CloseHandle(h);
    if (g_capture_luid == 0) {
        printf("[BLOCKED] %s-COMMIT: no I210 observer adapter; the wire cannot be observed\n", g_case);
        WSACleanup();
        return 3;
    }
    for (i = 0; i < stored; i++) {
        run_adapter(ids[i].index, &ids[i]);
    }
    WSACleanup();

    printf("\nadapters run=%d nonpassing verdicts=%d blocked=%d\n", g_ran, g_failed, g_blocked);
    if (g_ran == 0) { return 2; }
    if (g_failed != 0) { return 1; }
    return g_blocked != 0 ? 3 : 0;
}
