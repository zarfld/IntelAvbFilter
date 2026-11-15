/**
 * @file test_tas_query_clean.c
 * @brief IOCTL_AVB_TAS_QUERY_HW on a clean adapter: state, read-only behaviour, PHC bracket, TX unaffected (#366)
 *
 * TEST-TAS-API-001 (#366), case TC-TASAPI-001. Contract: #365 "Query v1 contract" (12.1 validation, 12.2 layout and values,
 * 12.4 status, 12.5 advisory restoration, 12.7 the #366 row). Layout: include/avb_ioctl.h (TC-ABI-026/027).
 *
 * Checks, each with its own verdict line TC-TASAPI-001-<check>:
 *   QUERY     the query on a handle opened for reading only and bound by NET_LUID completes with STATUS_SUCCESS, 536 bytes,
 *             version and size echoed. The raw NTSTATUS is captured (avb_ntstatus.h) and classified by avb_verdict.h: a driver
 *             without a handler completes the code with STATUS_INVALID_DEVICE_REQUEST, which is FAIL, never a skip.
 *             STATUS_NOT_IMPLEMENTED (capability present, no family query map) is SKIP_NOT_IMPLEMENTED. Nothing below runs then.
 *   STATE     a clean adapter: hw_read READ with no invalid register, hw_mode OFF, current and next NONE, restoration COMPLETE
 *             (advisory only, #365 12.5), the bound adapter's identity, lookup NOT_REQUESTED, no operation in progress.
 *             DTXMXPKTSZ is recorded raw and decoded as a measurement. A query that reports a not-clean adapter is BLOCKED
 *             (precondition unmet), with every field printed; it is never relabelled PASS.
 *   CLOCK     GET_CLOCK_CONFIG read before the first and after the second query brackets both queries' PHC brackets, in order.
 *   READONLY  a second query reports every schedule register as the first did (BASET_L/H advance on their own and are only
 *             reported, #365 12.4), and the driver's PHC adjust/set, hardware-fault, pause/restart counters and its TAS record
 *             (GET_TAS_STATE) are unchanged.
 *   TX        tagged broadcast frames seen by the I210 observer: a baseline window without queries must reach the share, then a
 *             window with queries interleaved must reach it too, and each interleaved query must still report the same
 *             schedule registers. No observer, link or IPv4 address: BLOCKED for this line only.
 *
 * Adapters are chosen by INTEL_CAP_TSN_TAS; the observer is the I210, chosen by identity for targeting only. The loaded driver
 * image and its SHA256 are printed as evidence, never used for selection.
 *
 * IOCTLs sent: GET_VERSION, GET_ADAPTER_IDENTITY, OPEN_ADAPTER_BY_LUID (binds the handle only: no global context switch, no
 * PTP initialization, src/device.c), TAS_QUERY_HW, GET_CLOCK_CONFIG, GET_STATISTICS, GET_TAS_STATE. Plus bounded broadcast UDP
 * and ICMP on the adapter under test, and passive capture on the I210. No READ_REGISTER, no WRITE_REGISTER, no TAS write, no
 * NIC cycle: the test changes no adapter state, so it has nothing to restore (testing contract E, case 1).
 *
 * Exit: 0 all passed, 1 any nonpassing verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #350
 * Verifies: #353
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/avb_ioctl.h"
#include "../../include/intel_pci_ids.h"
#include "../ioctl/tas_activate_eval.h"   /* TAS_ACT_SEEN_MIN_PERCENT: the share a baseline must reach */
#include "../common/avb_ntstatus.h"       /* exact NTSTATUS: STATUS_NOT_IMPLEMENTED and an unknown IOCTL share Win32 error 1 */
#include "tas_wire_observer.h"

#define CASE_ID            "TC-TASAPI-001"
#define DEVICE_NAME        "\\\\.\\IntelAvbFilter"
#define DRIVER_IMAGE       "intelavbfilter.sys"
#define SEND_PERIOD_MS     50
#define WINDOW_MS          2000
#define DRAIN_MS           1500
#define QUERY_EVERY_FRAMES 4                    /* one interleaved query per 200 ms of traffic */
#define PAYLOAD_BYTES      256
#define BIND_BOUND_MS      60000

static int g_failed, g_blocked, g_ran;
static avb_u64 g_capture_luid;
static unsigned g_next_seq;

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

static BOOL identity_at(HANDLE h, UINT32 index, AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    DWORD br = 0;
    ZeroMemory(id, sizeof(*id));
    id->version = AVB_ADAPTER_IDENTITY_VERSION;
    id->index = index;
    return DeviceIoControl(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, id, sizeof(*id), id, sizeof(*id), &br, NULL);
}

static HANDLE open_bound_read(avb_u64 luid)
{
    AVB_OPEN_BY_LUID_REQUEST bind = {0};
    DWORD br = 0;
    HANDLE h = open_device(GENERIC_READ);   /* read access only: the query is FILE_READ_ACCESS (#362 decision 3) */

    if (h == INVALID_HANDLE_VALUE) {
        return h;
    }
    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = luid;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &br, NULL)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static BOOL read_phc(HANDLE h, avb_u64 *phc)
{
    AVB_CLOCK_CONFIG c;
    DWORD br = 0;
    ZeroMemory(&c, sizeof(c));
    if (!DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &c, sizeof(c), &c, sizeof(c), &br, NULL) || c.status != 0) {
        return FALSE;
    }
    *phc = c.systim;
    return TRUE;
}

static BOOL read_stats(HANDLE h, AVB_DRIVER_STATISTICS *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS, s, sizeof(*s), s, sizeof(*s), &br, NULL);
}

static BOOL read_state(HANDLE h, AVB_TAS_STATE *s)
{
    DWORD br = 0;
    ZeroMemory(s, sizeof(*s));
    return DeviceIoControl(h, IOCTL_AVB_GET_TAS_STATE, s, sizeof(*s), s, sizeof(*s), &br, NULL) && s->status == 0;
}

/** One query: in = version, size, lookup id 0. Returns the raw NTSTATUS; *got = IoStatus.Information */
static LONG query(HANDLE h, AVB_TAS_QUERY_HW_REQUEST *q, DWORD *got)
{
    ZeroMemory(q, sizeof(*q));
    q->version = AVB_TAS_QUERY_VERSION;
    q->size = (avb_u32)sizeof(*q);
    return avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_QUERY_HW, q, sizeof(*q), q, sizeof(*q), got);
}

static void print_query(const char *label, const AVB_TAS_QUERY_HW_REQUEST *q)
{
    const AVB_TAS_REGS *r = &q->regs;
    int i;

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
           label, r->tqavctrl, r->baset_l, r->baset_h, r->qbvcyclet, r->qbvcyclet_s, r->dtxmxpktsz, r->read_failed, r->consistency);
    for (i = 0; i < 4; i++) {
        printf("    regs %s: q%d STQT=0x%08X ENDQT=0x%08X TXQCTL=0x%08X\n", label, i, r->stqt[i], r->endqt[i], r->txqctl[i]);
    }
}

/** Every schedule register except BASET_L/H, which advance on their own and are reported only (#365 12.4) */
static BOOL same_schedule_regs(const AVB_TAS_REGS *a, const AVB_TAS_REGS *b)
{
    return a->tqavctrl == b->tqavctrl && a->qbvcyclet == b->qbvcyclet && a->qbvcyclet_s == b->qbvcyclet_s &&
           memcmp(a->stqt, b->stqt, sizeof(a->stqt)) == 0 && memcmp(a->endqt, b->endqt, sizeof(a->endqt)) == 0 &&
           memcmp(a->txqctl, b->txqctl, sizeof(a->txqctl)) == 0 && a->dtxmxpktsz == b->dtxmxpktsz &&
           a->read_failed == 0 && b->read_failed == 0;
}

static BOOL phc_valid(const AVB_TAS_QUERY_HW_REQUEST *q)
{
    return (q->snapshot_reasons & (AVB_TAS_SNAP_RSN_PHC_BEFORE_INVALID | AVB_TAS_SNAP_RSN_PHC_AFTER_INVALID)) == 0;
}

/** Finds the adapter's IPv4 address */
static BOOL find_ipv4(avb_u64 luid, IN_ADDR *ipv4)
{
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES *list = (IP_ADAPTER_ADDRESSES *)malloc(size), *a;
    BOOL found = FALSE;

    if (list == NULL || GetAdaptersAddresses(AF_INET, 0, NULL, list, &size) != NO_ERROR) {
        free(list);
        return FALSE;
    }
    for (a = list; a != NULL; a = a->Next) {
        if (a->Luid.Value == luid && a->OperStatus == IfOperStatusUp && a->FirstUnicastAddress != NULL) {
            *ipv4 = ((SOCKADDR_IN *)a->FirstUnicastAddress->Address.lpSockaddr)->sin_addr;
            found = TRUE;
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

/** Sends for WINDOW_MS, optionally querying every QUERY_EVERY_FRAMES frames; returns the frames the observer saw.
 *  queries/changed: interleaved queries issued, and how many failed or reported other schedule registers than ref. */
static unsigned send_window(SOCKET sock, unsigned nonce, HANDLE h, const AVB_TAS_REGS *ref, unsigned *sent, unsigned *queries,
                            unsigned *changed)
{
    unsigned first = g_next_seq, seq, seen = 0, n = 0;
    DWORD start = GetTickCount();

    *queries = 0;
    *changed = 0;
    while (GetTickCount() - start < WINDOW_MS) {
        (void)send_tagged(sock, nonce);
        if (ref != NULL && (++n % QUERY_EVERY_FRAMES) == 0) {
            AVB_TAS_QUERY_HW_REQUEST q;
            DWORD got = 0;
            LONG st = query(h, &q, &got);
            (*queries)++;
            if (st != 0 || got != sizeof(q) || !same_schedule_regs(ref, &q.regs)) {
                (*changed)++;
            }
        }
        Sleep(SEND_PERIOD_MS);
    }
    Sleep(DRAIN_MS);
    *sent = g_next_seq - first;
    for (seq = first; seq < g_next_seq; seq++) {
        if (g_wire_seen[seq] != 0) {
            seen++;
        }
    }
    return seen;
}

static BOOL share_reached(unsigned seen, unsigned sent)
{
    return sent != 0 && (unsigned long long)seen * 100u >= (unsigned long long)sent * TAS_ACT_SEEN_MIN_PERCENT;
}

/** Image and SHA256 of the driver Windows has loaded (same method as test_tas_activate.c) */
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

static void check_tx(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, HANDLE h, const AVB_TAS_REGS *ref)
{
    IN_ADDR ipv4;
    SOCKET sock;
    unsigned nonce, sent_a = 0, seen_a, sent_b = 0, seen_b, queries = 0, changed = 0, unused = 0;
    char reason[256];

    if (g_capture_luid == 0) {
        verdict("BLOCKED", "TX", index, id->device_id, "no I210 observer adapter; the wire cannot be observed");
        return;
    }
    if (!find_ipv4(id->net_luid, &ipv4)) {
        verdict("BLOCKED", "TX", index, id->device_id, "adapter link is down or it has no IPv4 address");
        return;
    }
    sock = open_tx_socket(ipv4);
    nonce = GetTickCount() ^ GetCurrentProcessId();
    if (sock == INVALID_SOCKET || wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) == 0) {
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        wire_stop();
        verdict("BLOCKED", "TX", index, id->device_id, "no TX socket, or the I210 capture could not be started");
        return;
    }
    seen_a = send_window(sock, nonce, h, NULL, &sent_a, &unused, &unused);
    seen_b = send_window(sock, nonce, h, ref, &sent_b, &queries, &changed);
    wire_stop();
    closesocket(sock);
    printf("    tx: baseline sent=%u seen=%u; with queries sent=%u seen=%u queries=%u changedOrFailed=%u\n",
           sent_a, seen_a, sent_b, seen_b, queries, changed);
    if (!share_reached(seen_a, sent_a)) {
        verdict("BLOCKED", "TX", index, id->device_id, "the I210 did not see the baseline broadcast frames; the wire proves nothing here");
        return;
    }
    if (share_reached(seen_b, sent_b) && queries != 0 && changed == 0) {
        verdict("PASS", "TX", index, id->device_id, NULL);
        return;
    }
    sprintf_s(reason, sizeof(reason), "with queries interleaved: seen %u of %u, %u of %u queries failed or reported other registers",
              seen_b, sent_b, changed, queries);
    verdict("FAIL", "TX", index, id->device_id, reason);
}

static void run_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    AVB_TAS_QUERY_HW_REQUEST q1, q2;
    AVB_DRIVER_STATISTICS s0, s1;
    AVB_TAS_STATE t0, t1;
    avb_u64 p0 = 0, p3 = 0;
    DWORD got1 = 0, got2 = 0;
    LONG st1, st2;
    AVB_VERDICT v;
    HANDLE h;
    char reason[320];
    BOOL pre_ok, post_ok, clean, readable;

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u ===\n", index, id->device_id, id->net_luid, id->if_index);
    g_ran++;
    h = open_bound_read(id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "cannot open a read-only handle and bind it by NET_LUID");
        return;
    }
    pre_ok = read_phc(h, &p0) && read_stats(h, &s0) && read_state(h, &t0);
    st1 = query(h, &q1, &got1);
    printf("    [EVIDENCE] query 1: ntstatus=0x%08lX information=%lu\n", (unsigned long)st1, got1);
    v = avb_verdict_from_status((NTSTATUS)st1, AVB_CAP_PRESENT,
                                got1 == sizeof(q1) && q1.version == AVB_TAS_QUERY_VERSION && q1.size == sizeof(q1), TRUE);
    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu%s", (unsigned long)st1, got1,
                  st1 == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" :
                  st1 == 0 ? " (success without the v1 reply)" : "");
        verdict(avb_verdict_tag(v), "QUERY", index, id->device_id, reason);
        CloseHandle(h);
        return;
    }
    verdict("PASS", "QUERY", index, id->device_id, NULL);
    print_query("1", &q1);
    st2 = query(h, &q2, &got2);
    post_ok = read_phc(h, &p3) && read_stats(h, &s1) && read_state(h, &t1);
    printf("    [EVIDENCE] query 2: ntstatus=0x%08lX information=%lu; GET_CLOCK_CONFIG before=%llu after=%llu; TAS record armed %u -> %u\n",
           (unsigned long)st2, got2, p0, p3, t0.armed, t1.armed);
    print_query("2", &q2);
    printf("    [EVIDENCE] DTXMXPKTSZ raw=0x%08X decoded=%u bytes state=%u (measurement, #365 12.7)\n",
           q1.regs.dtxmxpktsz, q1.dtx_max_frame_bytes, q1.dtx_max_frame_state);

    /* STATE: a clean adapter as #366 expects; a readable not-clean report means the precondition is unmet */
    readable = q1.hw_read == AVB_TAS_HWREAD_READ && q1.regs.read_failed == 0;
    clean = q1.hw_mode == AVB_TAS_HWMODE_OFF && q1.current_status == AVB_TAS_ST_NONE && q1.next_status == AVB_TAS_ST_NONE &&
            q1.restoration == AVB_TAS_RESTORE_COMPLETE;
    if (q1.net_luid != id->net_luid || q1.vendor_id != id->vendor_id || q1.device_id != id->device_id ||
        q1.lookup_result != AVB_TAS_LOOKUP_NOT_REQUESTED || (q1.flags & AVB_TAS_FLAG_OP_IN_PROGRESS) != 0 ||
        q1.current_status == AVB_TAS_ST_HARDWARE_VERIFIED || q1.next_status == AVB_TAS_ST_HARDWARE_VERIFIED ||
        q1.dtx_max_frame_state == 0 || (readable && q1.dtx_max_frame_state == AVB_TAS_DTX_NOT_READ)) {
        verdict("FAIL", "STATE", index, id->device_id, "identity, lookup, flags, status or DTX state contradict the contract; see query 1");
    } else if (q1.hw_read == AVB_TAS_HWREAD_NOT_RUNNING) {
        verdict("BLOCKED", "STATE", index, id->device_id, "the filter is not Running, so the query performed no MMIO (hw_read NOT_RUNNING)");
    } else if (!readable) {
        verdict("FAIL", "STATE", index, id->device_id, "the query did not read every v1 register; see hw_read and read_failed");
    } else if (!clean) {
        verdict("BLOCKED", "STATE", index, id->device_id, "the query reports a not-clean adapter; the precondition of #366 is unmet");
    } else {
        verdict("PASS", "STATE", index, id->device_id, NULL);
    }

    /* CLOCK: p0 <= q1 bracket <= q2 bracket <= p3, every PHC value valid */
    if (!pre_ok || !post_ok || st2 != 0 || got2 != sizeof(q2)) {
        verdict("FAIL", "CLOCK", index, id->device_id, "GET_CLOCK_CONFIG, statistics, GET_TAS_STATE or the second query failed");
    } else if (phc_valid(&q1) && phc_valid(&q2) && p0 <= q1.phc_before_ns && q1.phc_before_ns <= q1.phc_after_ns &&
               q1.phc_after_ns <= q2.phc_before_ns && q2.phc_before_ns <= q2.phc_after_ns && q2.phc_after_ns <= p3) {
        verdict("PASS", "CLOCK", index, id->device_id, NULL);
    } else {
        verdict("FAIL", "CLOCK", index, id->device_id, "the query PHC brackets are invalid or not ordered inside GET_CLOCK_CONFIG; see the query lines");
    }

    /* READONLY: nothing the query could change has changed */
    if (pre_ok && post_ok && st2 == 0 && got2 == sizeof(q2) && same_schedule_regs(&q1.regs, &q2.regs) &&
        s0.PhcAdjustCount == s1.PhcAdjustCount && s0.PhcSetCount == s1.PhcSetCount && s0.HardwareFaults == s1.HardwareFaults &&
        s0.FilterPauseCount == s1.FilterPauseCount && s0.FilterRestartCount == s1.FilterRestartCount &&
        s0.PauseRestartGeneration == s1.PauseRestartGeneration && t0.armed == t1.armed) {
        verdict("PASS", "READONLY", index, id->device_id, NULL);
    } else {
        sprintf_s(reason, sizeof(reason), "registers or counters changed: phcAdjust %llu->%llu phcSet %llu->%llu faults %llu->%llu "
                  "pause %llu->%llu restart %llu->%llu armed %u->%u",
                  s0.PhcAdjustCount, s1.PhcAdjustCount, s0.PhcSetCount, s1.PhcSetCount, s0.HardwareFaults, s1.HardwareFaults,
                  s0.FilterPauseCount, s1.FilterPauseCount, s0.FilterRestartCount, s1.FilterRestartCount, t0.armed, t1.armed);
        verdict("FAIL", "READONLY", index, id->device_id, reason);
    }

    check_tx(index, id, h, &q1.regs);
    CloseHandle(h);
}

int main(void)
{
    AVB_ADAPTER_IDENTITY_REQUEST ids[8];
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i, count = 1, stored = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== IntelAvbFilter - IOCTL_AVB_TAS_QUERY_HW on a clean adapter, %s (#366) ===\n", CASE_ID);
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
            printf("[SKIP_UNSUPPORTED] %s-QUERY/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_TSN_TAS\n", CASE_ID, i, id.device_id);
        }
    }
    CloseHandle(h);
    for (i = 0; i < stored; i++) {
        run_adapter(ids[i].index, &ids[i]);
    }
    WSACleanup();

    printf("\nadapters run=%d nonpassing verdicts=%d blocked=%d\n", g_ran, g_failed, g_blocked);
    if (g_ran == 0) { return 2; }
    if (g_failed != 0) { return 1; }
    return g_blocked != 0 ? 3 : 0;
}
