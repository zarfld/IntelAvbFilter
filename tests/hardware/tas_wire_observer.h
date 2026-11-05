/* tas_wire_observer.h - did the probe frames leave the adapter? (#328)
 *
 * Primary: passive capture on the I210 through Npcap, loaded at run time, matching the source MAC, run nonce and sequence
 * number (tas_wire_match.h). The I219 stays untouched for management. On the HIL host all adapters share one switch, so
 * only flooded broadcast is visible here; switched unicast is not expected. No switch setting is changed. Same host, so
 * this is supplementary evidence. A broadcast seen here crossed the wire, but visibility depends on the switch, so a
 * baseline must prove it first. All captured frames are counted too, so 0 matches can be told from a dead capture.
 *
 * Supplementary: ICMP echo to the adapter's gateway from its own address, once a second. Each echo also uses a transmit
 * descriptor. The route is checked, because every adapter here has an equal default route. Replies alone prove nothing
 * about gates.
 */
#ifndef TAS_WIRE_OBSERVER_H
#define TAS_WIRE_OBSERVER_H

#include <icmpapi.h>
#include "../ioctl/tas_wire_match.h"

#define WIRE_NPCAP_DLL        "C:\\Windows\\System32\\Npcap\\wpcap.dll"
#define WIRE_NPCAP_PREFIX     "\\Device\\NPF_"
#define WIRE_MAX_OBSERVERS    1
#define WIRE_MAX_FRAMES       8192u
#define WIRE_CAPTURE_BYTES    128
#define WIRE_READ_TIMEOUT_MS  100
#define WIRE_STOP_WAIT_MS     2000
#define WIRE_ERRBUF_BYTES     256
#define ICMP_PERIOD_MS        1000
#define ICMP_TIMEOUT_MS       800
#define ICMP_REPLY_BYTES      256

typedef struct { long tv_sec, tv_usec; unsigned caplen, len; } WIRE_PCAP_HEADER;
typedef void *(__cdecl *wire_open_fn)(const char *device, int snaplen, int promisc, int to_ms, char *errbuf);
typedef int (__cdecl *wire_next_fn)(void *pcap, WIRE_PCAP_HEADER **header, const unsigned char **data);
typedef void (__cdecl *wire_close_fn)(void *pcap);

typedef struct {
    void *pcap;
    HANDLE thread;
    volatile LONG matched, captured, from_target;
} WIRE_OBSERVER;

static WIRE_OBSERVER g_observers[WIRE_MAX_OBSERVERS];
static int g_observer_count;
static volatile LONG g_wire_seen[WIRE_MAX_FRAMES];
static volatile LONG g_wire_stop;
static unsigned char g_wire_mac[6];
static unsigned g_wire_nonce;
static wire_next_fn g_wire_next;
static wire_close_fn g_wire_close;

static HANDLE g_icmp_thread;
static IPAddr g_icmp_source, g_icmp_target;
static volatile LONG g_icmp_sent, g_icmp_replied;

static DWORD WINAPI wire_thread(LPVOID context)
{
    WIRE_OBSERVER *o = (WIRE_OBSERVER *)context;

    while (InterlockedCompareExchange(&g_wire_stop, 0, 0) == 0) {
        WIRE_PCAP_HEADER *header = NULL;
        const unsigned char *data = NULL;
        unsigned seq = 0;
        int rc = g_wire_next(o->pcap, &header, &data);
        if (rc < 0) {
            break;
        }
        if (rc == 1) {
            InterlockedIncrement(&o->captured);
            if (header->caplen >= 12 && memcmp(data + 6, g_wire_mac, sizeof(g_wire_mac)) == 0) {
                InterlockedIncrement(&o->from_target);
            }
        }
        if (rc == 1 && tas_wire_match(data, header->caplen, g_wire_mac, g_wire_nonce, &seq) && seq < WIRE_MAX_FRAMES) {
            InterlockedExchange(&g_wire_seen[seq], 1);
            InterlockedIncrement(&o->matched);
        }
    }
    return 0;
}

static DWORD WINAPI icmp_thread(LPVOID context)
{
    static const char data[8] = "TASICMP";
    unsigned char reply[ICMP_REPLY_BYTES];
    HANDLE icmp = IcmpCreateFile();

    UNREFERENCED_PARAMETER(context);
    if (icmp == INVALID_HANDLE_VALUE) {
        printf("    [OBSERVATION] ICMP unavailable: IcmpCreateFile error %lu\n", GetLastError());
        return 0;
    }
    while (InterlockedCompareExchange(&g_wire_stop, 0, 0) == 0) {
        DWORD answers = IcmpSendEcho2Ex(icmp, NULL, NULL, NULL, g_icmp_source, g_icmp_target, (LPVOID)data, sizeof(data), NULL,
                                        reply, sizeof(reply), ICMP_TIMEOUT_MS);
        InterlockedIncrement(&g_icmp_sent);
        if (answers != 0 && ((ICMP_ECHO_REPLY *)reply)->Status == IP_SUCCESS) {
            InterlockedIncrement(&g_icmp_replied);
        }
        Sleep(ICMP_PERIOD_MS);
    }
    IcmpCloseHandle(icmp);
    return 0;
}

/** Starts the passive capture on capture_luid, and ICMP towards the gateway of the adapter under test if it has one */
static int wire_start(avb_u64 luid, avb_u64 capture_luid, const unsigned char *mac, unsigned nonce, IN_ADDR source)
{
    char errbuf[WIRE_ERRBUF_BYTES];
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES *list = (IP_ADAPTER_ADDRESSES *)malloc(size), *a;
    HMODULE npcap = LoadLibraryExA(WIRE_NPCAP_DLL, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    wire_open_fn open_live = NULL;
    SOCKADDR_INET src, dst;
    MIB_IPFORWARD_ROW2 route;
    SOCKADDR_INET best;
    BOOL have_gateway = FALSE;

    ZeroMemory((void *)g_wire_seen, sizeof(g_wire_seen));
    InterlockedExchange(&g_wire_stop, 0);
    g_observer_count = 0;
    g_icmp_thread = NULL;
    g_wire_nonce = nonce;
    memcpy(g_wire_mac, mac, sizeof(g_wire_mac));
    if (npcap != NULL) {
        open_live = (wire_open_fn)GetProcAddress(npcap, "pcap_open_live");
        g_wire_next = (wire_next_fn)GetProcAddress(npcap, "pcap_next_ex");
        g_wire_close = (wire_close_fn)GetProcAddress(npcap, "pcap_close");
    }
    if (open_live == NULL || g_wire_next == NULL || g_wire_close == NULL) {
        printf("    [OBSERVATION] Npcap unavailable at %s (error %lu)\n", WIRE_NPCAP_DLL, GetLastError());
    }
    if (list == NULL || GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, list, &size) != NO_ERROR) {
        printf("    [OBSERVATION] adapter list unavailable, no wire observer\n");
        free(list);
        return 0;
    }
    for (a = list; a != NULL; a = a->Next) {
        if (a->Luid.Value == luid) {
            if (a->FirstGatewayAddress != NULL) {
                g_icmp_target = ((SOCKADDR_IN *)a->FirstGatewayAddress->Address.lpSockaddr)->sin_addr.s_addr;
                have_gateway = TRUE;
            }
        } else if (open_live != NULL && g_observer_count < WIRE_MAX_OBSERVERS && a->Luid.Value == capture_luid &&
                   a->OperStatus == IfOperStatusUp) {
            char device[128];
            WIRE_OBSERVER *o = &g_observers[g_observer_count];
            sprintf_s(device, sizeof(device), WIRE_NPCAP_PREFIX "%s", a->AdapterName);
            o->matched = 0;
            o->captured = 0;
            o->from_target = 0;
            o->pcap = open_live(device, WIRE_CAPTURE_BYTES, 0, WIRE_READ_TIMEOUT_MS, errbuf);
            if (o->pcap == NULL) {
                printf("    [OBSERVATION] capture refused on %ls: %s\n", a->FriendlyName, errbuf);
                continue;
            }
            o->thread = CreateThread(NULL, 0, wire_thread, o, 0, NULL);
            if (o->thread == NULL) {
                g_wire_close(o->pcap);
                continue;
            }
            printf("    wire observer %d: %ls\n", g_observer_count, a->FriendlyName);
            g_observer_count++;
        }
    }
    free(list);

    g_icmp_source = source.s_addr;
    if (!have_gateway) {
        printf("    [OBSERVATION] ICMP not used: the adapter has no gateway\n");
        return g_observer_count;
    }
    ZeroMemory(&src, sizeof(src));
    ZeroMemory(&dst, sizeof(dst));
    src.Ipv4.sin_family = AF_INET;
    src.Ipv4.sin_addr.s_addr = g_icmp_source;
    dst.Ipv4.sin_family = AF_INET;
    dst.Ipv4.sin_addr.s_addr = g_icmp_target;
    if (GetBestRoute2(NULL, 0, &src, &dst, 0, &route, &best) != NO_ERROR || route.InterfaceLuid.Value != luid) {
        printf("    [OBSERVATION] ICMP not used: the route from this source does not leave this adapter\n");
        return g_observer_count;
    }
    g_icmp_sent = 0;
    g_icmp_replied = 0;
    g_icmp_thread = CreateThread(NULL, 0, icmp_thread, NULL, 0, NULL);
    return g_observer_count;
}

static void wire_stop(void)
{
    int i;

    InterlockedExchange(&g_wire_stop, 1);
    for (i = 0; i < g_observer_count; i++) {
        if (WaitForSingleObject(g_observers[i].thread, WIRE_STOP_WAIT_MS) == WAIT_OBJECT_0) {
            g_wire_close(g_observers[i].pcap);
        } else {
            printf("    [OBSERVATION] wire observer %d did not stop\n", i);
        }
        printf("    wire observer %d matched %ld tagged frames, %ld from the adapter under test, %ld captured in total\n", i,
               g_observers[i].matched, g_observers[i].from_target, g_observers[i].captured);
        CloseHandle(g_observers[i].thread);
    }
    if (g_icmp_thread != NULL) {
        (void)WaitForSingleObject(g_icmp_thread, ICMP_PERIOD_MS + ICMP_TIMEOUT_MS + WIRE_STOP_WAIT_MS);
        CloseHandle(g_icmp_thread);
        g_icmp_thread = NULL;
    }
    g_observer_count = 0;
}

/** Totals of the observers, to tell silence on the wire from a capture that stopped working */
static void wire_counts(LONG *captured, LONG *from_target)
{
    int i;

    *captured = 0;
    *from_target = 0;
    for (i = 0; i < g_observer_count; i++) {
        *captured += g_observers[i].captured;
        *from_target += g_observers[i].from_target;
    }
}

/** Sequence numbers first..end-1 seen by any observer; the first missing and last seen are reported through the pointers */
static unsigned wire_seen(unsigned first, unsigned end, long *first_missing, long *last_seen)
{
    unsigned seq, seen = 0;

    *first_missing = -1;
    *last_seen = -1;
    for (seq = first; seq < end && seq < WIRE_MAX_FRAMES; seq++) {
        if (g_wire_seen[seq] != 0) {
            seen++;
            *last_seen = (long)seq;
        } else if (*first_missing < 0) {
            *first_missing = (long)seq;
        }
    }
    return seen;
}

#endif /* TAS_WIRE_OBSERVER_H */
