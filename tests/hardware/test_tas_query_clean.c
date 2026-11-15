/**
 * @file test_tas_query_clean.c
 * @brief IOCTL_AVB_TAS_QUERY_HW on a clean adapter: state, read-only behaviour, PHC bracket, TX unaffected, owned cleanup (#366)
 *
 * TEST-TAS-API-001 (#366), case TC-TASAPI-001. Contract: #365 "Query v1 contract" (12.1 validation, 12.2 layout and values,
 * 12.4 status, 12.5 advisory restoration, 12.7 the #366 row). Layout: include/avb_ioctl.h (TC-ABI-026/027).
 *
 * --hw-verified: TEST-TAS-API-016 (#381), case TC-TASAPI-016. HARDWARE_VERIFIED is never returned (#365 12.4, 12.7): checked on
 * the clean adapter (QUERY), after IOCTL_AVB_TAS_ACTIVATE HOLD committed a schedule while BaseT is still ahead (PENDING), and
 * on a query whose own PHC bracket starts two cycles past BaseT (PAST_BASET). Each check needs a valid, executed query: an
 * invalid or unexecuted one takes its raw-NTSTATUS verdict and is never a pass. Nothing is activated unless the first query is
 * valid and reports a clean adapter. Schedule: the verified set of test_tas_activate.c (1 s cycle, window 200 to 400 ms on all
 * queues, BaseT the first cycle boundary at least 1.5 s ahead). The slot statuses are printed as evidence only; PENDING and
 * INFERRED_ACTIVE are #369's. Cleanup, CLEANUP and RESTORE as below (the restoration baseline is the clean query).
 * A forbidden status fails a phase line before any timing judgement; PENDING passes only on a valid, ordered PHC bracket that
 * ends before the retained target BaseT. The activation counts as committed only from its documented complete reply (all
 * bytes, status STATUS_SUCCESS, result COMMITTED, PHC at commit before BaseT). REPLIES covers every answered query of the
 * adapter run, polling and the cleanup's query after the cycle included: FAIL on any HARDWARE_VERIFIED, INCONCLUSIVE if no
 * query was answered.
 *
 * Preconditions, checked before the first query (BLOCKED, nothing sent to the adapter, nothing to clean): the adapter's device
 * node resolves from its NET_LUID, another adapter keeps the host reachable (the cleanup cycles this one), the adapter is up
 * with an IPv4 address, and the I210 observer exists (#366 "I210 observer alive").
 *
 * Checks, each with its own verdict line TC-TASAPI-001-<check>:
 *   QUERY     the query on a handle opened for reading only and bound by NET_LUID completes with STATUS_SUCCESS, 536 bytes,
 *             version and size echoed. The raw NTSTATUS is captured (avb_ntstatus.h) and classified by avb_verdict.h:
 *             STATUS_NOT_IMPLEMENTED is SKIP_NOT_IMPLEMENTED; any other error, STATUS_INVALID_DEVICE_REQUEST of a driver
 *             without a handler included, is FAIL. Without a successful query every dependent check below is BLOCKED.
 *   STATE     a clean adapter: hw_read READ with no invalid register, hw_mode OFF, current and next NONE, restoration COMPLETE
 *             (advisory only, #365 12.5), the bound adapter's identity, lookup NOT_REQUESTED, no operation in progress.
 *             DTXMXPKTSZ is recorded raw and decoded as a measurement. A readable not-clean report, or a filter not Running,
 *             is BLOCKED (precondition unmet); it is never relabelled PASS.
 *   CLOCK     GET_CLOCK_CONFIG read before the first and after the second query brackets both queries' PHC brackets, in order.
 *   READONLY  a second query reports every schedule register as the first did (BASET_L/H advance on their own and are only
 *             reported, #365 12.4), and the driver's PHC adjust/set, hardware-fault, pause/restart counters and its TAS record
 *             (GET_TAS_STATE) are unchanged.
 *   TX        only after STATE and READONLY passed: tagged broadcast frames seen by the I210 observer, a baseline window without
 *             queries, then a window with queries interleaved, each of which must report the same schedule registers.
 *   CLEANUP   #366 postcondition, after every adapter run that issued a query, whatever it returned: the test's own NIC cycle
 *             (CM_Disable_DevNode / CM_Enable_DevNode, bounded) ran and the adapter came back with the same NET_LUID, device ID
 *             and MAC.
 *   RESTORE   after the cycle (tests/ioctl/tas_activate_eval.h tas_act_restore): the TAS query reads every register with TSN
 *             mode off and the schedule registers as captured before the test, the driver record is CLEAN, and tagged frames
 *             reach the wire. TX after the cycle is sent only when that query reported a clean, readable adapter. Without a
 *             usable query the registers are unmeasured and restoration is INCONCLUSIVE_RESTORATION. The PHC across the cycle
 *             is a separate evidence line (the cycle reseeds it, E12), never a restoration verdict.
 *
 * Adapters are chosen by INTEL_CAP_TSN_TAS; the observer is the I210, chosen by identity for targeting only. Discovery validates
 * every identity and has no fixed adapter limit; an incomplete inventory is BLOCKED (DISCOVERY), never a silent pass. At most
 * TAS_QUERY_CLEAN_MAX_ADAPTERS TAS adapters run per process (its runner time budget); any further one is BLOCKED. The SHA256
 * of the on-disk file at the loaded driver module's path is printed as evidence, never used for selection; it is not a hash of
 * the image in memory, whose identity stays unverified (the file can be replaced while the old image remains loaded).
 *
 * IOCTLs sent: GET_VERSION, GET_ADAPTER_IDENTITY, OPEN_ADAPTER_BY_LUID (binds the handle only: no global context switch, no
 * PTP initialization, src/device.c), TAS_QUERY_HW, GET_CLOCK_CONFIG, GET_STATISTICS, GET_TAS_STATE. No READ_REGISTER, no
 * WRITE_REGISTER, no TAS write. Mutations: bounded broadcast UDP and ICMP on the adapter under test, and the owned NIC cycle of
 * that adapter, which resets its PHC. --hw-verified also sends IOCTL_AVB_TAS_ACTIVATE (a TAS schedule) on a clean adapter,
 * always followed by that NIC cycle. Safety class Unsafe.
 *
 * Exit: 0 all passed, 1 any nonpassing verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #350
 * Verifies: #353
 * (--hw-verified, TC-TASAPI-016: Verifies #350 per #381)
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
#include "../ioctl/tas_activate_eval.h"   /* TAS_ACT_SEEN_MIN_PERCENT, restoration and PHC classification */
#include "../common/avb_ntstatus.h"       /* exact NTSTATUS: STATUS_NOT_IMPLEMENTED and an unknown IOCTL share Win32 error 1 */
#include "tas_wire_observer.h"
#include "../ioctl/tas_query_tx_eval.h"   /* send-window accounting and TX verdict, checked offline by TC-MOCK-057 */

#define CASE_ID            g_case                   /* TC-TASAPI-001 (#366) or, with --hw-verified, TC-TASAPI-016 (#381) */
#define DEVICE_NAME        "\\\\.\\IntelAvbFilter"
/* --hw-verified schedule: the verified set of tests/hardware/test_tas_activate.c (E6 characterisation, LOCAL_DIAGNOSTIC,
 * I226 0x125B REV_04, Debug), within the window rule of #357 */
#define NS_PER_MS          1000000LL
#define CYCLE_NS           (1000LL * NS_PER_MS)
#define WINDOW_START_NS    (200LL * NS_PER_MS)
#define WINDOW_END_NS      (400LL * NS_PER_MS)
#define BASE_LEAD_NS       (1500LL * NS_PER_MS)   /* BaseT is the first cycle boundary at least this far ahead */
#define FRAME_OVERHEAD_BYTES 38u                   /* MTU plus header, FCS, preamble and gap: the frame on the wire */
#define PAST_BASE_CYCLES   2                       /* the query bracket must start this many cycles past BaseT */
#define PAST_BASE_BOUND_MS 15000                   /* bound on waiting for that, from the activation */
#define PAST_BASE_POLL_MS  200
#define DRIVER_IMAGE       "intelavbfilter.sys"
#define NS_PER_SEC         1000000000LL
#define SEND_PERIOD_MS     50
#define WINDOW_MS          2000
#define DRAIN_MS           1500
#define QUERY_EVERY_FRAMES 4                    /* one interleaved query per 200 ms of traffic */
#define PAYLOAD_BYTES      256
#define BIND_BOUND_MS      60000
#define PHC_TOLERANCE_NS   (2 * NS_PER_SEC)
#define DISABLE_BOUND_MS   120000
#define ENABLE_BOUND_MS    60000
#define RETURN_BOUND_MS    60000
#define SETTLE_MS          5000
#define DRIVER_LIST_TRIES  3
/* Supported inventory: TAS adapters this process runs. The runner budget is 560 s x min(enumerated adapters, this value)
 * ($HilTestTimeoutPerAdapter, checked by TEST-HILGATE-098), so every TAS adapter run fits it; discovery stays complete and a
 * TAS adapter beyond this value is reported BLOCKED, never left out silently. */
#define TAS_QUERY_CLEAN_MAX_ADAPTERS 8
#define NET_CLASS_KEY      "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\"

/* One driver read with its validity: a value that was not read is never printed or compared as a measured zero */
typedef struct { BOOL ok; avb_u64 phc; LARGE_INTEGER qpc; } PHC_READ;
typedef struct { BOOL ok; AVB_DRIVER_STATISTICS s; } STATS_READ;
typedef struct { BOOL ok; AVB_TAS_STATE s; } STATE_READ;
typedef struct { LONG status; DWORD got; AVB_TAS_QUERY_HW_REQUEST q; } QUERY_READ;

static const char *g_case = "TC-TASAPI-001";
static BOOL g_hw_verified_mode;   /* --hw-verified: TC-TASAPI-016 (#381) */
static int g_failed, g_blocked, g_ran;
static avb_u64 g_capture_luid;
static unsigned g_next_seq;

/* Non-negative from an activation (--hw-verified) or the start of the owned NIC cycle until the cycle has reported */
static volatile LONG g_pending_adapter = -1;
static volatile LONG g_pending_did;

static void report_abort(void)
{
    LONG adapter = InterlockedExchange(&g_pending_adapter, -1);
    if (adapter >= 0) {
        printf("[CLEANUP_FAILED] %s-CLEANUP/adapter:%ld did=0x%04lX: test aborted after an activation or during its NIC cycle; the "
               "adapter may hold a schedule or be disabled. Operator: NIC cycle (enable it), then check it with the TAS query\n",
               CASE_ID, adapter, g_pending_did);
        fflush(stdout);
    }
}

static BOOL WINAPI on_console_ctrl(DWORD type) { (void)type; report_abort(); return FALSE; }
static LONG WINAPI on_crash(EXCEPTION_POINTERS *info) { (void)info; report_abort(); return EXCEPTION_CONTINUE_SEARCH; }

/* One verdict line. Nonpassing tags count as failures, BLOCKED separately */
static void verdict(const char *tag, const char *check, UINT32 adapter, avb_u16 did, const char *reason)
{
    printf("[%s] %s-%s/adapter:%u did=0x%04X%s%s\n", tag, CASE_ID, check, adapter, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
    if (strcmp(tag, "BLOCKED") == 0) {
        g_blocked++;
    } else if (strcmp(tag, "PASS") != 0) {
        g_failed++;
    }
}

static HANDLE open_device(DWORD access)
{
    return CreateFileA(DEVICE_NAME, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

/** One validated identity: the driver answers an index with no adapter with STATUS_SUCCESS and status ADAPTER_NOT_FOUND
 *  (src/device.c), so success of the call alone is not an identity */
static BOOL identity_at(HANDLE h, UINT32 index, AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    DWORD br = 0;
    ZeroMemory(id, sizeof(*id));
    id->version = AVB_ADAPTER_IDENTITY_VERSION;
    id->index = index;
    return DeviceIoControl(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, id, sizeof(*id), id, sizeof(*id), &br, NULL) &&
           br == sizeof(*id) && id->version == AVB_ADAPTER_IDENTITY_VERSION && id->index == index && id->status == 0 &&
           id->count > index;
}

static BOOL bind_luid(HANDLE h, avb_u64 luid)
{
    AVB_OPEN_BY_LUID_REQUEST bind = {0};
    DWORD br = 0;
    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = luid;
    return DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &br, NULL);
}

/** Read access only: the query is FILE_READ_ACCESS (#362 decision 3) */
static HANDLE open_bound_read(avb_u64 luid)
{
    HANDLE h = open_device(GENERIC_READ);
    if (h != INVALID_HANDLE_VALUE && !bind_luid(h, luid)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static void read_phc(HANDLE h, PHC_READ *r)
{
    AVB_CLOCK_CONFIG c;
    DWORD br = 0;

    ZeroMemory(r, sizeof(*r));
    ZeroMemory(&c, sizeof(c));
    r->ok = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &br, NULL) && c.status == 0;
    QueryPerformanceCounter(&r->qpc);
    if (r->ok) {
        r->phc = c.systim;
    }
}

static void read_stats(HANDLE h, STATS_READ *r)
{
    DWORD br = 0;
    ZeroMemory(r, sizeof(*r));
    r->ok = DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, &r->s, sizeof(r->s), &r->s, sizeof(r->s), &br, NULL);
}

static void read_state(HANDLE h, STATE_READ *r)
{
    DWORD br = 0;
    ZeroMemory(r, sizeof(*r));
    r->ok = DeviceIoControl(h, IOCTL_AVB_GET_TAS_STATE, &r->s, sizeof(r->s), &r->s, sizeof(r->s), &br, NULL) && r->s.status == 0;
}

/* --hw-verified (#381): every answered query of the current adapter run, the polling and cleanup replies included */
static unsigned g_hwv_answered, g_hwv_forbidden;
static BOOL query_valid(const QUERY_READ *r);

/** One query: in = version, size, lookup id 0. Keeps the raw NTSTATUS and IoStatus.Information. With --hw-verified, every
 *  answered reply is checked for HARDWARE_VERIFIED in either slot, whatever phase the query belongs to */
static void query(HANDLE h, QUERY_READ *r)
{
    ZeroMemory(r, sizeof(*r));
    r->q.version = AVB_TAS_QUERY_VERSION;
    r->q.size = (avb_u32)sizeof(r->q);
    r->status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_QUERY_HW, &r->q, sizeof(r->q), &r->q, sizeof(r->q), &r->got);
    if (g_hw_verified_mode && query_valid(r)) {
        g_hwv_answered++;
        if (r->q.current_status == AVB_TAS_ST_HARDWARE_VERIFIED || r->q.next_status == AVB_TAS_ST_HARDWARE_VERIFIED) {
            g_hwv_forbidden++;
            printf("    [EVIDENCE] HARDWARE_VERIFIED in an answered reply: current=%u next=%u (reply %u of this adapter run)\n",
                   r->q.current_status, r->q.next_status, g_hwv_answered);
        }
    }
}

/** A reply whose fields mean something: STATUS_SUCCESS with the full v1 reply */
static BOOL query_valid(const QUERY_READ *r)
{
    return r->status == 0 && r->got == sizeof(r->q) && r->q.version == AVB_TAS_QUERY_VERSION && r->q.size == sizeof(r->q);
}

/** hw_read READ and no register invalid */
static BOOL query_readable(const QUERY_READ *r)
{
    return query_valid(r) && r->q.hw_read == AVB_TAS_HWREAD_READ && r->q.regs.read_failed == 0;
}

static BOOL query_clean(const QUERY_READ *r)
{
    return query_readable(r) && r->q.hw_mode == AVB_TAS_HWMODE_OFF && r->q.current_status == AVB_TAS_ST_NONE &&
           r->q.next_status == AVB_TAS_ST_NONE && r->q.restoration == AVB_TAS_RESTORE_COMPLETE;
}

static void print_query(const char *label, const QUERY_READ *r)
{
    const AVB_TAS_QUERY_HW_REQUEST *q = &r->q;
    const AVB_TAS_REGS *g = &q->regs;
    int i;

    printf("    [EVIDENCE] query %s: ntstatus=0x%08lX information=%lu\n", label, (unsigned long)r->status, r->got);
    if (!query_valid(r)) {
        printf("    query %s: no v1 reply, its fields are not reported\n", label);
        return;
    }
    printf("    query %s: hw_read=%u hw_mode=%u origin=%u gate=%u current=%u/0x%X next=%u/0x%X snapshot=0x%X restoration=%u flags=0x%X "
           "lookup=%u link=%u legacy=0x%X rev=%u/%u epoch=%llu gen=%llu\n",
           label, q->hw_read, q->hw_mode, q->origin, q->gate_policy, q->current_status, q->current_reasons, q->next_status,
           q->next_reasons, q->snapshot_reasons, q->restoration, q->flags, q->lookup_result, q->link_speed_mbps, q->legacy_conflicts,
           q->revision_id, q->revision_state, q->context_epoch, q->generation);
    printf("    query %s: phc=[%llu, %llu] luid=0x%016llX vid=0x%04X did=0x%04X dtx=%u bytes state=%u commit=%u/%u\n",
           label, q->phc_before_ns, q->phc_after_ns, q->net_luid, q->vendor_id, q->device_id, q->dtx_max_frame_bytes,
           q->dtx_max_frame_state, q->current_commit.state, q->next_commit.state);
    printf("    regs %s: TQAVCTRL=0x%08X BASET_L=0x%08X BASET_H=0x%08X QBVCYCLET=0x%08X QBVCYCLET_S=0x%08X DTXMXPKTSZ=0x%08X "
           "read_failed=0x%X consistency=0x%X\n",
           label, g->tqavctrl, g->baset_l, g->baset_h, g->qbvcyclet, g->qbvcyclet_s, g->dtxmxpktsz, g->read_failed, g->consistency);
    for (i = 0; i < 4; i++) {
        printf("    regs %s: q%d STQT=0x%08X ENDQT=0x%08X TXQCTL=0x%08X\n", label, i, g->stqt[i], g->endqt[i], g->txqctl[i]);
    }
}

/** Schedule registers that differ, BASET_L/H excluded: they advance on their own and are reported only (#365 12.4) */
static unsigned schedule_regs_differing(const AVB_TAS_REGS *a, const AVB_TAS_REGS *b)
{
    unsigned n = 0;
    int i;

    n += a->tqavctrl != b->tqavctrl;
    n += a->qbvcyclet != b->qbvcyclet;
    n += a->qbvcyclet_s != b->qbvcyclet_s;
    n += a->dtxmxpktsz != b->dtxmxpktsz;
    for (i = 0; i < 4; i++) {
        n += a->stqt[i] != b->stqt[i];
        n += a->endqt[i] != b->endqt[i];
        n += a->txqctl[i] != b->txqctl[i];
    }
    return n;
}

static BOOL phc_valid(const AVB_TAS_QUERY_HW_REQUEST *q)
{
    return (q->snapshot_reasons & (AVB_TAS_SNAP_RSN_PHC_BEFORE_INVALID | AVB_TAS_SNAP_RSN_PHC_AFTER_INVALID)) == 0;
}

static void fmt_u64(char *buf, size_t size, BOOL ok, unsigned long long v)
{
    if (ok) {
        sprintf_s(buf, size, "%llu", v);
    } else {
        strcpy_s(buf, size, "unavailable");
    }
}

static BOOL read_if(avb_u64 luid, MIB_IF_ROW2 *row)
{
    ZeroMemory(row, sizeof(*row));
    row->InterfaceLuid.Value = luid;
    return GetIfEntry2(row) == NO_ERROR;
}

/** The adapter's IPv4 address, and whether another adapter keeps the machine reachable */
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
            if (a->OperStatus == IfOperStatusUp && a->FirstUnicastAddress != NULL) {
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
        if (GetTickCount() - start >= BIND_BOUND_MS) {
            closesocket(sock);
            return INVALID_SOCKET;
        }
        Sleep(1000);
    }
    return sock;
}

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
    if (sendto(sock, (const char *)payload, (int)sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) == SOCKET_ERROR) {
        return FALSE;
    }
    g_next_seq++;
    return TRUE;
}

/** Sends for WINDOW_MS, optionally querying every QUERY_EVERY_FRAMES send attempts against ref. Accounting and judgement are
 *  tests/ioctl/tas_query_tx_eval.h (checked offline by TC-MOCK-057): every try counts, a refused send is an error */
static void send_window(SOCKET sock, unsigned nonce, HANDLE h, const AVB_TAS_REGS *ref, TAS_TX_WINDOW *w)
{
    unsigned first = g_next_seq, seq;
    DWORD start = GetTickCount();

    ZeroMemory(w, sizeof(*w));
    while (GetTickCount() - start < WINDOW_MS) {
        tas_tx_record_send(w, send_tagged(sock, nonce));
        if (ref != NULL && (w->attempted % QUERY_EVERY_FRAMES) == 0) {
            QUERY_READ r;
            query(h, &r);
            tas_tx_record_query(w, query_readable(&r) && schedule_regs_differing(ref, &r.q.regs) == 0);
        }
        Sleep(SEND_PERIOD_MS);
    }
    Sleep(DRAIN_MS);
    for (seq = first; seq < g_next_seq; seq++) {   /* only accepted sends get a sequence number */
        if (g_wire_seen[seq] != 0) {
            w->seen++;
        }
    }
}

static void print_window(const char *label, const TAS_TX_WINDOW *w)
{
    printf("    tx %s: attempted=%u sent=%u sendErrors=%u seen=%u queries=%u changedOrFailed=%u\n", label, w->attempted, w->sent,
           w->errors, w->seen, w->queries, w->changed);
}

/** SHA256 of the on-disk file at the path of the loaded driver module. Not a hash of the image in memory.
 *  EnumDeviceDrivers: the list is retried with the size lpcbNeeded reports, so no entry beyond the buffer is read. */
static void driver_provenance(void)
{
    LPVOID *bases = NULL;
    DWORD cb = 256 * sizeof(LPVOID), needed = 0, i, n = 0;
    char name[MAX_PATH], path[MAX_PATH * 2];
    int tries;

    for (tries = 0; tries < DRIVER_LIST_TRIES; tries++) {
        LPVOID *grown = (LPVOID *)realloc(bases, cb);
        if (grown == NULL) {
            break;
        }
        bases = grown;
        if (!EnumDeviceDrivers(bases, cb, &needed)) {
            printf("    [EVIDENCE] loaded driver module: unknown (EnumDeviceDrivers error %lu)\n", GetLastError());
            free(bases);
            return;
        }
        if (needed <= cb) {
            n = needed / sizeof(LPVOID);
            break;
        }
        cb = needed + 16 * sizeof(LPVOID);   /* drivers can load between the calls */
    }
    if (n == 0) {
        printf("    [EVIDENCE] loaded driver module: unknown (driver list did not fit after %d tries)\n", DRIVER_LIST_TRIES);
        free(bases);
        return;
    }
    for (i = 0; i < n; i++) {   /* n <= cb / sizeof(LPVOID): never past the buffer (8520afe's bound, kept by the retry) */
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
            printf("    [EVIDENCE] loaded driver module path: %s; SHA256 of the on-disk file at that path: ", path);
            for (k = 0; ok && k < 32; k++) { printf("%02X", digest[k]); }
            printf("%s; in-memory image identity: UNVERIFIED\n", ok ? "" : "unreadable");
            if (hash != NULL) { BCryptDestroyHash(hash); }
            if (alg != NULL) { BCryptCloseAlgorithmProvider(alg, 0); }
            if (f != INVALID_HANDLE_VALUE) { CloseHandle(f); }
        }
        free(bases);
        return;
    }
    printf("    [EVIDENCE] loaded driver module: %s is not loaded\n", DRIVER_IMAGE);
    free(bases);
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

/** Waits until the adapter is back with the same identity and a read-only handle is bound to it */
static HANDLE wait_for_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *want)
{
    DWORD start = GetTickCount();

    while (GetTickCount() - start < RETURN_BOUND_MS) {
        HANDLE h = open_device(GENERIC_READ);
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

static void check_tx(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, HANDLE h, IN_ADDR ipv4, const AVB_TAS_REGS *ref)
{
    SOCKET sock;
    TAS_TX_WINDOW base, probe;
    unsigned nonce;
    char reason[256];

    sock = open_tx_socket(ipv4);
    nonce = GetTickCount() ^ GetCurrentProcessId();
    if (sock == INVALID_SOCKET || wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) == 0) {
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        wire_stop();
        verdict("BLOCKED", "TX", index, id->device_id, "no TX socket, or the I210 capture could not be started");
        return;
    }
    send_window(sock, nonce, h, NULL, &base);
    send_window(sock, nonce, h, ref, &probe);
    wire_stop();
    closesocket(sock);
    print_window("baseline", &base);
    print_window("with queries", &probe);
    switch (tas_tx_judge(&base, &probe, TAS_ACT_SEEN_MIN_PERCENT)) {
    case TAS_TX_BLOCKED_BASELINE:
        sprintf_s(reason, sizeof(reason), "baseline not established: seen %u of %u attempted, %u send errors; the wire proves nothing here",
                  base.seen, base.attempted, base.errors);
        verdict("BLOCKED", "TX", index, id->device_id, reason);
        return;
    case TAS_TX_PASS:
        verdict("PASS", "TX", index, id->device_id, NULL);
        return;
    default:
        break;
    }
    sprintf_s(reason, sizeof(reason), "with queries interleaved: seen %u of %u attempted, %u send errors, %u of %u queries failed or "
              "reported other registers", probe.seen, probe.attempted, probe.errors, probe.changed, probe.queries);
    verdict("FAIL", "TX", index, id->device_id, reason);
}

/** #366 postcondition: the test's own NIC cycle, then TAS query, measured PHC and TX on the wire */
static void cleanup_and_verify(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, IN_ADDR ipv4,
                               const QUERY_READ *before, const PHC_READ *phc_before)
{
    TAS_ACT_RESTORE_EVIDENCE e;
    QUERY_READ after;
    STATE_READ s;
    PHC_READ phc_after;
    MIB_IF_ROW2 row;
    HANDLE fresh;
    DWORD start;
    BOOL link_up = FALSE;
    long long wall_ns = 0;
    char b1[32], b2[32];

    ZeroMemory(&e, sizeof(e));
    ZeroMemory(&after, sizeof(after));
    ZeroMemory(&s, sizeof(s));
    ZeroMemory(&phc_after, sizeof(phc_after));
    e.phc = TAS_ACT_PHC_UNMEASURED;
    printf("\n  --- Owned cleanup (#366): NIC cycle of this adapter ---\n");
    CloseHandle(h);   /* an open bound handle must not hold the adapter context across the detach */
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);
    e.cycle_completed = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
    if (e.cycle_completed) {
        Sleep(3000);
        e.cycle_completed = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
    } else {
        printf("    enable not attempted: the disable did not complete and may still be pending in Windows\n");
    }
    fresh = e.cycle_completed ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
    e.adapter_returned = (fresh != INVALID_HANDLE_VALUE);
    InterlockedExchange(&g_pending_adapter, -1);
    verdict(e.cycle_completed && e.adapter_returned ? "PASS" : "CLEANUP_FAILED", "CLEANUP", index, id->device_id,
            e.cycle_completed && e.adapter_returned ? NULL
                                                    : "the NIC cycle did not complete or the adapter did not return; a timed-out disable may "
                                                      "still be pending in Windows. Operator: NIC cycle once it ends, then check it with the TAS query");

    if (e.adapter_returned) {
        start = GetTickCount();
        while (GetTickCount() - start < RETURN_BOUND_MS) {
            if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
                link_up = TRUE;
                break;
            }
            Sleep(1000);
        }
        Sleep(SETTLE_MS);
        read_state(fresh, &s);
        e.driver_clean = s.ok && s.s.armed == AVB_TAS_STATE_CLEAN;
        if (s.ok) {
            printf("    GET_TAS_STATE after the cycle: armed=%u\n", s.s.armed);
        } else {
            printf("    GET_TAS_STATE after the cycle: unavailable\n");
        }
        query(fresh, &after);
        print_query("after the cycle", &after);
        /* Measured only with a readable reply after the cycle and the test's own readable capture before it */
        e.regs_measured = query_readable(&after) && query_readable(before);
        if (e.regs_measured) {
            e.regs_differing = schedule_regs_differing(&before->q.regs, &after.q.regs);
            e.tsn_mode_off = after.q.hw_mode == AVB_TAS_HWMODE_OFF;
        } else {
            printf("    [EVIDENCE] TAS registers after the cycle: UNVERIFIED, no readable query before and after the cycle\n");
        }
        read_phc(fresh, &phc_after);
        if (phc_after.ok && phc_before->ok) {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            wall_ns = (long long)((double)(phc_after.qpc.QuadPart - phc_before->qpc.QuadPart) * 1e9 / (double)freq.QuadPart);
            e.phc = tas_act_phc(TRUE, (long long)(phc_after.phc - phc_before->phc), wall_ns, PHC_TOLERANCE_NS);
        }
        if (!link_up) {
            e.tx_probed = TRUE;   /* the link was up before the test: no link now is an observed failure, not a gap */
            e.tx_on_wire = FALSE;
            printf("    TX after the cycle: link did not come back within %d ms\n", RETURN_BOUND_MS);
        } else if (!query_clean(&after)) {
            printf("    TX after the cycle: not sent, the TAS query after the cycle does not report a clean, readable adapter\n");
        } else {
            SOCKET sock = open_tx_socket(ipv4);
            unsigned nonce = GetTickCount() ^ (GetCurrentProcessId() << 8);
            LONG captured = 0, from_target = 0;
            TAS_TX_WINDOW w;

            if (sock != INVALID_SOCKET && wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) != 0) {
                send_window(sock, nonce, fresh, NULL, &w);
                wire_counts(&captured, &from_target);
                wire_stop();
                e.tx_probed = captured != 0;
                e.tx_on_wire = tas_tx_on_wire(&w, TAS_ACT_SEEN_MIN_PERCENT);   /* a send error fails it, never shrinks the denominator */
                print_window("after the cycle", &w);
                printf("    TX after the cycle: captured=%ld\n", captured);
            } else {
                printf("    TX after the cycle: not probed, no socket or no observer\n");
            }
            if (sock != INVALID_SOCKET) { closesocket(sock); }
        }
        CloseHandle(fresh);
    }
    printf("    evidence: cycle=%d returned=%d driverClean=%d regsMeasured=%d regsDiffering=%u tsnOff=%d link=%d txProbed=%d txOnWire=%d\n",
           e.cycle_completed, e.adapter_returned, e.driver_clean, e.regs_measured, e.regs_differing, e.tsn_mode_off, link_up,
           e.tx_probed, e.tx_on_wire);
    fmt_u64(b1, sizeof(b1), phc_before->ok, phc_before->phc);
    fmt_u64(b2, sizeof(b2), phc_after.ok, phc_after.phc);
    printf("    [EVIDENCE] PHC across the cleanup: %s (before=%s after=%s wallNs=%lld)\n", tas_act_phc_name(e.phc), b1, b2,
           e.phc == TAS_ACT_PHC_UNMEASURED ? 0LL : wall_ns);
    switch (tas_act_restore(&e)) {
    case TAS_ACT_RESTORE_COMPLETE:
        verdict("PASS", "RESTORE", index, id->device_id, "registers as captured before the test, TSN mode off, record clean, TX on the wire");
        break;
    case TAS_ACT_RESTORE_UNVERIFIED:
        verdict("INCONCLUSIVE_RESTORATION", "RESTORE", index, id->device_id,
                "nothing observed failed, but the registers or the wire were not measured; see the evidence line");
        break;
    default:
        verdict("CLEANUP_FAILED", "RESTORE", index, id->device_id,
                "restoration incomplete: see the evidence line (cycle, return, record, registers or TX on the wire failed)");
        break;
    }
}

/** TC-TASAPI-016 assertion for one reply: a valid, executed query whose slots are not HARDWARE_VERIFIED (#381, #365 12.7).
 *  An invalid or unexecuted query is never a pass: its verdict comes from its raw NTSTATUS (avb_verdict.h). */
static void verdict_not_hw_verified(const char *check, UINT32 index, avb_u16 did, const QUERY_READ *r, const char *when)
{
    AVB_VERDICT v = avb_verdict_from_status((NTSTATUS)r->status, AVB_CAP_PRESENT, query_valid(r), TRUE);
    char reason[256];

    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "%s: no valid query, ntstatus=0x%08lX information=%lu%s; an unexecuted query is never a pass",
                  when, (unsigned long)r->status, r->got,
                  r->status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" : "");
        verdict(avb_verdict_tag(v), check, index, did, reason);
    } else if (r->q.current_status == AVB_TAS_ST_HARDWARE_VERIFIED || r->q.next_status == AVB_TAS_ST_HARDWARE_VERIFIED) {
        sprintf_s(reason, sizeof(reason), "%s: HARDWARE_VERIFIED returned (current=%u next=%u); v1 must never return it", when,
                  r->q.current_status, r->q.next_status);
        verdict("FAIL", check, index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "%s: current=%u next=%u", when, r->q.current_status, r->q.next_status);
        verdict("PASS", check, index, did, reason);
    }
}

/** --hw-verified, TC-TASAPI-016 (#381): HARDWARE_VERIFIED is never returned, on a clean adapter, with a committed schedule
 *  whose BaseT is still ahead, and with the PHC past BaseT. Only the verdicts on HARDWARE_VERIFIED belong to this case; the
 *  slot statuses themselves (PENDING, INFERRED_ACTIVE) are #369's and are printed as evidence only. */
static void run_hw_verified(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, IN_ADDR ipv4,
                            const MIB_IF_ROW2 *row)
{
    QUERY_READ q0, q1, q2;
    PHC_READ p0;
    AVB_TAS_ACTIVATE_REQUEST req;
    HANDLE hw;
    DWORD br = 0, err = ERROR_SUCCESS, start;
    BOOL ok, past = FALSE, committed;
    long long base = 0;
    unsigned q;
    char reason[256];

    ZeroMemory(&q1, sizeof(q1));
    ZeroMemory(&q2, sizeof(q2));
    g_hwv_answered = 0;
    g_hwv_forbidden = 0;
    read_phc(h, &p0);
    query(h, &q0);
    print_query("clean", &q0);
    verdict_not_hw_verified("QUERY", index, id->device_id, &q0, "clean adapter");
    if (!query_valid(&q0)) {
        verdict("BLOCKED", "PENDING", index, id->device_id, "requires a valid query of the clean adapter before any activation");
        verdict("BLOCKED", "PAST_BASET", index, id->device_id, "requires a valid query of the clean adapter before any activation");
        goto done;
    }
    if (!query_clean(&q0) || !p0.ok) {
        verdict("BLOCKED", "PENDING", index, id->device_id, !p0.ok ? "PHC unavailable: no BaseT can be placed" :
                "the query does not report a clean adapter; no activation on it (#381 precondition)");
        verdict("BLOCKED", "PAST_BASET", index, id->device_id, "requires a clean adapter, the PHC and a committed schedule");
        goto done;
    }

    /* The schedule: IOCTL_AVB_TAS_ACTIVATE, HOLD, the verified set; needs a handle with write access (#362) */
    base = ((long long)p0.phc + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    ZeroMemory(&req, sizeof(req));
    req.version = AVB_TAS_ACTIVATE_VERSION;
    req.pre_activation = AVB_TAS_PRE_HOLD;
    req.base_time_ns = (avb_u64)base;
    req.cycle_ns = (avb_u32)CYCLE_NS;
    req.link_mbps = (avb_u32)(row->TransmitLinkSpeed / 1000000ULL);
    req.max_frame_bytes = (avb_u32)row->Mtu + FRAME_OVERHEAD_BYTES;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        req.window_start_ns[q] = (avb_u32)WINDOW_START_NS;
        req.window_end_ns[q] = (avb_u32)WINDOW_END_NS;
    }
    hw = open_device(GENERIC_READ | GENERIC_WRITE);
    if (hw == INVALID_HANDLE_VALUE || !bind_luid(hw, id->net_luid)) {
        if (hw != INVALID_HANDLE_VALUE) { CloseHandle(hw); }
        verdict("BLOCKED", "PENDING", index, id->device_id, "cannot bind a handle with write access for the activation");
        verdict("BLOCKED", "PAST_BASET", index, id->device_id, "no committed schedule");
        goto done;
    }
    /* COMMITTED is 0: the out fields start at values the driver never writes, so a reply it did not write cannot read as one */
    req.result = 0xFFFFFFFFu;
    req.status = 0xFFFFFFFFu;
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);   /* from here the cleanup owes a NIC cycle */
    ok = DeviceIoControl(hw, IOCTL_AVB_TAS_ACTIVATE, &req, sizeof(req), &req, sizeof(req), &br, NULL);
    if (!ok) { err = GetLastError(); }
    CloseHandle(hw);
    printf("    [EVIDENCE] activation: base=%lld cycle=%lld window=[%lld, %lld) ok=%d win32=%lu bytes=%lu result=0x%X status=0x%08X "
           "phcAtCommit=%llu\n", base, CYCLE_NS, WINDOW_START_NS, WINDOW_END_NS, ok, err, br, req.result, req.status,
           req.phc_at_commit_ns);
    /* The documented reply of an activation that ran: the whole structure (IoStatus.Information = sizeof), status
     * STATUS_SUCCESS, result COMMITTED, and COMMITTED means the PHC read after the commit was still before BaseT */
    committed = ok && br == sizeof(req) && req.status == 0u && req.result == AVB_TAS_ACT_COMMITTED &&
                req.phc_at_commit_ns != 0 && req.phc_at_commit_ns < (avb_u64)base;
    if (!committed) {
        sprintf_s(reason, sizeof(reason), "activation not established as COMMITTED (ok=%d win32=%lu bytes=%lu result=0x%X status=0x%08X "
                  "phcAtCommit=%llu); no schedule to query", ok, err, br, req.result, req.status, req.phc_at_commit_ns);
        verdict("BLOCKED", "PENDING", index, id->device_id, reason);
        verdict("BLOCKED", "PAST_BASET", index, id->device_id, reason);
        goto done;
    }

    /* PENDING: a forbidden status fails whatever the timing; a pass needs valid, ordered PHC readings before BaseT */
    query(h, &q1);
    print_query("committed, BaseT ahead", &q1);
    if (!query_valid(&q1) || q1.q.current_status == AVB_TAS_ST_HARDWARE_VERIFIED || q1.q.next_status == AVB_TAS_ST_HARDWARE_VERIFIED) {
        verdict_not_hw_verified("PENDING", index, id->device_id, &q1, "committed, BaseT ahead");
    } else if (!phc_valid(&q1.q) || q1.q.phc_before_ns > q1.q.phc_after_ns) {
        verdict("BLOCKED", "PENDING", index, id->device_id, "the query's PHC bracket is invalid or unordered; it cannot show the query precedes BaseT");
    } else if (q1.q.phc_after_ns >= (avb_u64)base) {
        verdict("BLOCKED", "PENDING", index, id->device_id, "the PHC had already reached BaseT at this query; the BaseT-ahead case was not observed");
    } else {
        verdict_not_hw_verified("PENDING", index, id->device_id, &q1, "committed, query bracket before BaseT");
    }

    /* Wait, bounded, until a valid query's own ordered PHC bracket starts PAST_BASE_CYCLES cycles past BaseT. Every polled reply
     * is checked by query() for HARDWARE_VERIFIED; the REPLIES line below reports it */
    start = GetTickCount();
    do {
        Sleep(PAST_BASE_POLL_MS);
        query(h, &q2);
        past = query_valid(&q2) && phc_valid(&q2.q) && q2.q.phc_before_ns <= q2.q.phc_after_ns &&
               q2.q.phc_before_ns >= (avb_u64)(base + PAST_BASE_CYCLES * CYCLE_NS);
        /* Stop at the first forbidden reply: it becomes PAST_BASET's FAIL, never overwritten by a later reply (PR #438) */
    } while (!past && query_valid(&q2) && q2.q.current_status != AVB_TAS_ST_HARDWARE_VERIFIED &&
             q2.q.next_status != AVB_TAS_ST_HARDWARE_VERIFIED && GetTickCount() - start < PAST_BASE_BOUND_MS);
    print_query("PHC past BaseT", &q2);
    if (!query_valid(&q2) || q2.q.current_status == AVB_TAS_ST_HARDWARE_VERIFIED || q2.q.next_status == AVB_TAS_ST_HARDWARE_VERIFIED) {
        verdict_not_hw_verified("PAST_BASET", index, id->device_id, &q2, "PHC past BaseT");
    } else if (!past) {
        verdict("BLOCKED", "PAST_BASET", index, id->device_id, "no valid, ordered query bracket past BaseT within the bound; the case was not observed");
    } else {
        verdict_not_hw_verified("PAST_BASET", index, id->device_id, &q2, "query bracket two cycles past BaseT");
    }

done:
    cleanup_and_verify(h, index, id, inst, ipv4, &q0, &p0);
    /* REPLIES: every answered query of this adapter run, polling and the cleanup's query after the cycle included */
    if (g_hwv_forbidden != 0) {
        sprintf_s(reason, sizeof(reason), "%u of %u answered replies reported HARDWARE_VERIFIED; v1 must never return it",
                  g_hwv_forbidden, g_hwv_answered);
        verdict("FAIL", "REPLIES", index, id->device_id, reason);
    } else if (g_hwv_answered == 0) {
        verdict("INCONCLUSIVE", "REPLIES", index, id->device_id, "no query was answered in this run; an unexecuted query is never a pass");
    } else {
        sprintf_s(reason, sizeof(reason), "none of %u answered replies reported HARDWARE_VERIFIED", g_hwv_answered);
        verdict("PASS", "REPLIES", index, id->device_id, reason);
    }
}

static void run_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    QUERY_READ q1, q2;
    STATS_READ s0, s1;
    STATE_READ t0, t1;
    PHC_READ p0, p3;
    MIB_IF_ROW2 row;
    IN_ADDR ipv4;
    DEVINST inst = 0;
    AVB_VERDICT v;
    HANDLE h;
    BOOL other_link = FALSE, state_pass = FALSE, readonly_pass = FALSE;
    char reason[400], a[32], b[32];

    ZeroMemory(&q1, sizeof(q1));
    ZeroMemory(&q2, sizeof(q2));
    ZeroMemory(&s0, sizeof(s0));
    ZeroMemory(&s1, sizeof(s1));
    ZeroMemory(&t0, sizeof(t0));
    ZeroMemory(&t1, sizeof(t1));
    ZeroMemory(&p0, sizeof(p0));
    ZeroMemory(&p3, sizeof(p3));
    ZeroMemory(&ipv4, sizeof(ipv4));
    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u ===\n", index, id->device_id, id->net_luid, id->if_index);
    g_ran++;

    /* Preconditions of the owned cleanup and the wire checks: nothing is sent to the adapter before they hold */
    if (!find_devinst(id->net_luid, &inst)) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "cannot resolve the adapter device node required for the owned NIC-cycle cleanup");
        return;
    }
    if (!read_if(id->net_luid, &row) || row.OperStatus != IfOperStatusUp || !find_addressing(id->net_luid, &ipv4, &other_link)) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "adapter link is down or it has no IPv4 address");
        return;
    }
    if (!other_link) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "no independent management link; the owned NIC cycle would isolate the host");
        return;
    }
    if (g_capture_luid == 0) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "no I210 observer adapter; the wire cannot be observed (#366 precondition)");
        return;
    }
    h = open_bound_read(id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "cannot open a read-only handle and bind it by NET_LUID");
        return;
    }
    if (g_hw_verified_mode) {
        run_hw_verified(h, index, id, inst, ipv4, &row);
        return;
    }

    read_phc(h, &p0);
    read_stats(h, &s0);
    read_state(h, &t0);
    query(h, &q1);
    print_query("1", &q1);
    v = avb_verdict_from_status((NTSTATUS)q1.status, AVB_CAP_PRESENT, query_valid(&q1), TRUE);
    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu%s", (unsigned long)q1.status, q1.got,
                  q1.status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" :
                  q1.status == 0 ? " (success without the v1 reply)" : "");
        verdict(avb_verdict_tag(v), "QUERY", index, id->device_id, reason);
        verdict("BLOCKED", "STATE", index, id->device_id, "requires a successful query");
        verdict("BLOCKED", "CLOCK", index, id->device_id, "requires a successful query");
        verdict("BLOCKED", "READONLY", index, id->device_id, "requires a successful query");
        verdict("BLOCKED", "TX", index, id->device_id, "requires a successful query and a clean, readable, unchanged state");
        cleanup_and_verify(h, index, id, inst, ipv4, &q1, &p0);
        return;
    }
    verdict("PASS", "QUERY", index, id->device_id, NULL);
    query(h, &q2);
    read_phc(h, &p3);
    read_stats(h, &s1);
    read_state(h, &t1);
    print_query("2", &q2);
    fmt_u64(a, sizeof(a), p0.ok, p0.phc);
    fmt_u64(b, sizeof(b), p3.ok, p3.phc);
    printf("    [EVIDENCE] GET_CLOCK_CONFIG before=%s after=%s; statistics %s/%s; TAS record armed %s -> %s\n", a, b,
           s0.ok ? "read" : "unavailable", s1.ok ? "read" : "unavailable",
           t0.ok ? (t0.s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not clean") : "unavailable",
           t1.ok ? (t1.s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not clean") : "unavailable");
    printf("    [EVIDENCE] DTXMXPKTSZ raw=0x%08X decoded=%u bytes state=%u (measurement, #365 12.7)\n",
           q1.q.regs.dtxmxpktsz, q1.q.dtx_max_frame_bytes, q1.q.dtx_max_frame_state);

    /* STATE: a clean adapter as #366 expects; a readable not-clean report means the precondition is unmet */
    if (q1.q.net_luid != id->net_luid || q1.q.vendor_id != id->vendor_id || q1.q.device_id != id->device_id ||
        q1.q.lookup_result != AVB_TAS_LOOKUP_NOT_REQUESTED || (q1.q.flags & AVB_TAS_FLAG_OP_IN_PROGRESS) != 0 ||
        q1.q.current_status == AVB_TAS_ST_HARDWARE_VERIFIED || q1.q.next_status == AVB_TAS_ST_HARDWARE_VERIFIED ||
        q1.q.dtx_max_frame_state == 0 || (query_readable(&q1) && q1.q.dtx_max_frame_state == AVB_TAS_DTX_NOT_READ)) {
        verdict("FAIL", "STATE", index, id->device_id, "identity, lookup, flags, status or DTX state contradict the contract; see query 1");
    } else if (q1.q.hw_read == AVB_TAS_HWREAD_NOT_RUNNING) {
        verdict("BLOCKED", "STATE", index, id->device_id, "the filter is not Running, so the query performed no MMIO (hw_read NOT_RUNNING)");
    } else if (!query_readable(&q1)) {
        verdict("FAIL", "STATE", index, id->device_id, "the query did not read every v1 register; see hw_read and read_failed");
    } else if (!query_clean(&q1)) {
        verdict("BLOCKED", "STATE", index, id->device_id, "the query reports a not-clean adapter; the precondition of #366 is unmet");
    } else {
        state_pass = TRUE;
        verdict("PASS", "STATE", index, id->device_id, NULL);
    }

    /* CLOCK: p0 <= q1 bracket <= q2 bracket <= p3, every PHC value valid */
    if (!p0.ok || !p3.ok || !query_valid(&q2)) {
        sprintf_s(reason, sizeof(reason), "measurement unavailable: GET_CLOCK_CONFIG before=%s after=%s, second query %s", a, b,
                  query_valid(&q2) ? "valid" : "without a v1 reply");
        verdict("BLOCKED", "CLOCK", index, id->device_id, reason);
    } else if (phc_valid(&q1.q) && phc_valid(&q2.q) && p0.phc <= q1.q.phc_before_ns && q1.q.phc_before_ns <= q1.q.phc_after_ns &&
               q1.q.phc_after_ns <= q2.q.phc_before_ns && q2.q.phc_before_ns <= q2.q.phc_after_ns && q2.q.phc_after_ns <= p3.phc) {
        verdict("PASS", "CLOCK", index, id->device_id, NULL);
    } else {
        verdict("FAIL", "CLOCK", index, id->device_id, "the query PHC brackets are invalid or not ordered inside GET_CLOCK_CONFIG; see the query lines");
    }

    /* READONLY: nothing the query could change has changed; every compared value was read */
    if (!s0.ok || !s1.ok || !t0.ok || !t1.ok || !query_readable(&q1) || !query_readable(&q2)) {
        sprintf_s(reason, sizeof(reason), "measurement unavailable: statistics %s/%s, TAS record %s/%s, readable queries %s/%s",
                  s0.ok ? "read" : "unavailable", s1.ok ? "read" : "unavailable", t0.ok ? "read" : "unavailable",
                  t1.ok ? "read" : "unavailable", query_readable(&q1) ? "yes" : "no", query_readable(&q2) ? "yes" : "no");
        verdict("BLOCKED", "READONLY", index, id->device_id, reason);
    } else if (schedule_regs_differing(&q1.q.regs, &q2.q.regs) == 0 && s0.s.PhcAdjustCount == s1.s.PhcAdjustCount &&
               s0.s.PhcSetCount == s1.s.PhcSetCount && s0.s.HardwareFaults == s1.s.HardwareFaults &&
               s0.s.FilterPauseCount == s1.s.FilterPauseCount && s0.s.FilterRestartCount == s1.s.FilterRestartCount &&
               s0.s.PauseRestartGeneration == s1.s.PauseRestartGeneration && t0.s.armed == t1.s.armed) {
        readonly_pass = TRUE;
        verdict("PASS", "READONLY", index, id->device_id, NULL);
    } else {
        sprintf_s(reason, sizeof(reason), "changed: registers differing %u, phcAdjust %llu->%llu phcSet %llu->%llu faults %llu->%llu "
                  "pause %llu->%llu restart %llu->%llu armed %u->%u",
                  schedule_regs_differing(&q1.q.regs, &q2.q.regs), s0.s.PhcAdjustCount, s1.s.PhcAdjustCount, s0.s.PhcSetCount,
                  s1.s.PhcSetCount, s0.s.HardwareFaults, s1.s.HardwareFaults, s0.s.FilterPauseCount, s1.s.FilterPauseCount,
                  s0.s.FilterRestartCount, s1.s.FilterRestartCount, t0.s.armed, t1.s.armed);
        verdict("FAIL", "READONLY", index, id->device_id, reason);
    }

    /* TX: never transmit while the clean, readable, unchanged state is not established */
    if (state_pass && readonly_pass) {
        check_tx(index, id, h, ipv4, &q1.q.regs);
    } else {
        verdict("BLOCKED", "TX", index, id->device_id, "requires STATE and READONLY to pass: a clean, readable, unchanged state");
    }
    cleanup_and_verify(h, index, id, inst, ipv4, &q1, p3.ok ? &p3 : &p0);
}

int main(int argc, char **argv)
{
    AVB_ADAPTER_IDENTITY_REQUEST *ids = NULL, first;
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i = 0, count = 0, stored = 0;
    BOOL complete = FALSE;
    char reason[200];

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    if (argc == 2 && strcmp(argv[1], "--hw-verified") == 0) {
        g_hw_verified_mode = TRUE;
        g_case = "TC-TASAPI-016";
    } else if (argc != 1) {
        printf("[BLOCKED] %s: usage: test_tas_query_clean [--hw-verified]\n", CASE_ID);
        return 3;
    }
    printf("=== IntelAvbFilter - IOCTL_AVB_TAS_QUERY_HW, %s (%s) ===\n", CASE_ID,
           g_hw_verified_mode ? "#381: HARDWARE_VERIFIED never returned, clean, BaseT ahead and PHC past BaseT" : "#366: clean adapter");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[BLOCKED] %s: Winsock unavailable\n", CASE_ID);
        return 3;
    }
    h = open_device(GENERIC_READ);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] %s: cannot open %s for reading (error %lu)\n", CASE_ID, DEVICE_NAME, GetLastError());
        WSACleanup();
        return 3;
    }
    if (DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL)) {
        printf("    [EVIDENCE] driver version %u.%u flags=0x%04X (%s build)\n", version.Major, version.Minor, version.Flags,
               (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) ? "Debug" : "Release");
    }
    driver_provenance();

    /* Identities first, every one of them (the owned NIC cycle may reorder the enumeration). No fixed adapter limit: the
     * array is sized by the driver's count, and an incomplete inventory is a nonpassing setup result, never silence. */
    if (identity_at(h, 0, &first)) {
        count = first.count;
        ids = (AVB_ADAPTER_IDENTITY_REQUEST *)calloc(count, sizeof(*ids));
        complete = ids != NULL;
        for (i = 0; complete && i < count; i++) {
            AVB_ADAPTER_IDENTITY_REQUEST id;
            if (!identity_at(h, i, &id) || id.count != count) {
                complete = FALSE;
                break;
            }
            printf("    [EVIDENCE] adapter:%u did=0x%04X luid=0x%016llX caps=0x%08X\n", i, id.device_id, id.net_luid, id.capabilities);
            if (g_capture_luid == 0 && id.device_id == INTEL_DEV_I210_AT) {
                g_capture_luid = id.net_luid;
            }
            if (id.capabilities & INTEL_CAP_TSN_TAS) {
                ids[stored++] = id;
            } else {
                printf("[SKIP_UNSUPPORTED] %s-QUERY/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_TSN_TAS\n", CASE_ID, i, id.device_id);
            }
        }
    }
    CloseHandle(h);
    if (!complete) {
        sprintf_s(reason, sizeof(reason), "adapter discovery incomplete: %u of %u identities validated (call, size, index, status, "
                  "stable count); untested adapters are not passes", i, count);
        printf("[BLOCKED] %s-DISCOVERY: %s\n", CASE_ID, reason);
        g_blocked++;
    }
    for (i = 0; i < stored; i++) {
        if (i < TAS_QUERY_CLEAN_MAX_ADAPTERS) {
            run_adapter(ids[i].index, &ids[i]);
        } else {
            printf("[BLOCKED] %s-QUERY/adapter:%u did=0x%04X: beyond the supported inventory of %d TAS adapters per run (runner time "
                   "budget); not tested, not a pass\n", CASE_ID, ids[i].index, ids[i].device_id, TAS_QUERY_CLEAN_MAX_ADAPTERS);
            g_blocked++;
        }
    }
    free(ids);
    WSACleanup();

    printf("\nadapters run=%d nonpassing verdicts=%d blocked=%d\n", g_ran, g_failed, g_blocked);
    if (g_ran == 0) { return 2; }
    if (g_failed != 0) { return 1; }
    return g_blocked != 0 ? 3 : 0;
}
