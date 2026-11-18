/**
 * @file test_tas_query_clean.c
 * @brief IOCTL_AVB_TAS_QUERY_HW on a clean adapter: state, read-only behaviour, PHC bracket, TX unaffected, owned cleanup (#366)
 *
 * TEST-TAS-API-001 (#366), case TC-TASAPI-001. Contract: #365 "Query v1 contract" (12.1 validation, 12.2 layout and values,
 * 12.4 status, 12.5 advisory restoration, 12.7 the #366 row). Layout: include/avb_ioctl.h (TC-ABI-026/027).
 *
 * --tiers-unbound: TEST-TAS-API-002 (#367, Verifies #351), case TC-TASAPI-002 (#365 12.1, 12.2, 12.7).
 *   UNBOUND     a handle that is never bound sends the query: exactly STATUS_INVALID_DEVICE_STATE (raw NTSTATUS), no output.
 *   UNBOUND-COUNTERS  an observation kept apart from the status: each adapter's IoctlCount across the query against two
 *               back-to-back reads, printed. PASS only with equal deltas on a complete, non-empty inventory; BLOCKED on an
 *               incomplete or empty one; INCONCLUSIVE when a reading is missing or a delta differs (not attributable).
 *   CAPABILITY  per adapter without INTEL_CAP_TSN_TAS, a bound query: STATUS_NOT_SUPPORTED with no output gives
 *               SKIP_UNSUPPORTED (green); success, or an error with output, is FAIL. Only the query is exercised here; mutating
 *               operations on adapters without the capability are a coverage gap of #367 in this mode (network topology is not
 *               a reason to omit them, #463).
 *   With no adapter advertising INTEL_CAP_TSN_TAS, QUERY and TIERS are reported BLOCKED: the TAS part was not exercised, so the
 *               case cannot pass on the unbound and capability lines alone.
 *   QUERY, TIERS per TAS adapter: the #351 tier table does not exist, so tiers read 0 (not reported) and TIERS is
 *               INCONCLUSIVE (incomplete). Asserted only: a reported tier is 1..4, tier_pass is NOT_IMPLEMENTED (#363 option
 *               C). Then the owned cleanup below.
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
 * --invalid: TEST-TAS-API-003 (#368, Verifies #351), case TC-TASAPI-003 (DES-C-TAS-010 5.2, 3.4, 9.5, 11.2, 12.7). Each invalid
 * IOCTL_AVB_TAS_ACTIVATE request is a one-field change of the verified HOLD set below (BaseT recomputed per case from a fresh PHC
 * read, so only the named field is invalid). Pre-state: the test's own query (clean, readable), GET_TAS_STATE CLEAN, statistics,
 * PHC (PRESTATE; otherwise nothing is submitted). Per case, three separate lines, never merged:
 *   <case>-REFUSED     the request was refused: the raw NTSTATUS, IoStatus.Information and, only for a complete success reply,
 *                      result are printed. A refusal is either the interim #357 reply (STATUS_SUCCESS, all bytes, result INVALID)
 *                      or an error completion with no output whose status a refusal rule names (3.4, 9.5). An accepted or
 *                      otherwise answered request is FAIL; another error status takes its avb_verdict.h verdict.
 *   <case>-UNCHANGED   the query after the case against the test's own pre-test capture: every schedule register equal (BASET_L/H
 *                      reported only, #365 12.4), still clean (ENDQT 0, or _ALT only under 3.6, through the advisory COMPLETE),
 *                      the TAS record CLEAN and the PHC set/adjust, fault and pause/restart counters unchanged. Not measured: BLOCKED.
 *   <case>-COMPLETION  conformance to the completion contract of the operation: the NTSTATUS the design requires (3.4, 9.5) with
 *                      no output. The interim STATUS_SUCCESS + result INVALID reply is reported as not conforming (K2, #352),
 *                      separately from REFUSED and UNCHANGED: a refusal that changed nothing is not completion conformance.
 * A case whose refusal was not established (REFUSED not PASS), or after which the state changed or could not be measured, stops
 * the remaining cases (BLOCKED): a later request needs an established refusal and the clean pre-state. COMPLETION never stops.
 * OBSERVER, before any request: the I226 under test, which runs TAS, sends tagged broadcast frames in a baseline window and the
 * I210, the receiving wire observer (passive Npcap capture on this host, flooded broadcast only, tas_wire_observer.h), must
 * receive them. Without a started capture or a received baseline, nothing is submitted (BLOCKED) and the cleanup still runs.
 * TX: a window after the field cases, with queries interleaved against the pre-test registers, judged against that baseline;
 * supplementary evidence that frames crossed the wire, no gate timing. END_EQ_CYCLE (TAS-DR-06,
 * end equal to the cycle) runs last, after that TX window: today's validation does not refuse it, so it can commit a schedule.
 * It gets its own TX-END_EQ_CYCLE window only when it was refused and nothing changed. Then CLEANUP and RESTORE as below.
 * Passes on the first hardware run are characterization of validation that existed before this test, never RED (12.7).
 * REPLACE (#358) has no IOCTL: not exercised. EG-06 (whether the end-equals-cycle boundary stalls hardware) is not tested here.
 *
 * --restore: TEST-TAS-API-012 (#377, Verifies #361), case TC-TASAPI-012 (DES-C-TAS-010 8, 12.5; #365 query contract). Two
 * restoration facts are kept apart: the driver's advisory `restoration`, computed from the SSOT observed baselines, and the
 * test's own restoration verdict (RESTORE), which needs its pre-test capture, TSN off, a clean record and received TX.
 *   PRESTATE   the test's own query (clean, readable), GET_TAS_STATE CLEAN, statistics and PHC; otherwise nothing is activated.
 *   OBSERVER   the I226 under test, which runs TAS, sends tagged broadcast frames in a baseline window and the I210, the
 *              receiving wire observer (passive Npcap capture on this host, flooded broadcast only, tas_wire_observer.h),
 *              must receive them before anything is activated.
 *   RESIDUE    after IOCTL_AVB_TAS_ACTIVATE HOLD committed the verified set: a readable query observing TSN on must not report
 *              restoration COMPLETE (#361 AC2). Only this TSN-on non-restored state is exercised.
 *   CLEANUP, RESTORE   the owned NIC cycle and the test's own restoration verdict, as below.
 *   REPORTED   a query after the cycle, judged only by generic relations of the contract, never by model or family (testing
 *              contract A). DES 12.5 makes the advisory a function of the restoration set (v1 set without DTXMXPKTSZ, BASET_L/H
 *              included; read_failed bits 0-16; hw_read READ or PARTIAL, since a DTXMXPKTSZ or PHC read failure is no input):
 *              (1) COMPLETE needs a valid restoration set with TSN off, otherwise FAIL; (2) TSN on and not COMPLETE is PASS;
 *              (3) a restoration set identical to the test's own clean pre-test observation must report the pre-test value,
 *              otherwise FAIL; (4) a valid set that differs from it (an ENDQT at the qualified _ALT, for example) cannot be
 *              decided generically, because the query exposes neither the baseline values nor per-register agreement: that is
 *              a contract gap, reported INCONCLUSIVE, never FAIL. Set not observed: BLOCKED. The test applies no baseline
 *              constants; the exact pre/post comparison stays with RESTORE.
 *   PHC        the test's own GET_CLOCK_CONFIG reads just before and after the cycle, reported separately and never a
 *              restoration verdict. No PHC tolerance is defined, so only a backward step establishes a discontinuity;
 *              anything else, or a missing read, is INCONCLUSIVE. The cleanup's own PHC line is evidence only.
 * Not exercised, printed as COVERAGE: TSN-off residue (PARTIAL_RESIDUE), PARTIAL_OPEN_ALL, unreadable registers (UNKNOWN),
 * NO_SSOT_BASELINE, a cycle that leaves residue, PHC continuity, every other failure path; and, not decidable through the generic
 * query, a restoration set that differs from the pre-test observation (REPORTED rule 4). A pass on the first hardware run is
 * characterization of restoration reporting implemented under #350, never RED.
 *
 * --link-speed: TEST-TAS-API-021 (#472, Verifies #351), case TC-TASAPI-021 (#365 12.2 link_speed_mbps "K8 (#351)", 13.6 D.2).
 * The query's link_speed_mbps must be the driver's own current link speed from NDIS, equal to an independent operating-system
 * observation of the same interface: GetIfEntry2 for the adapter's NET_LUID (MediaConnectState, TransmitLinkSpeed in bit/s), 0
 * when the OS reports the interface disconnected. Every comparison is bracketed: OS reading, query, OS reading; a bracket whose
 * OS state or rate differs is retried up to LINK_BRACKET_TRIES times, and an unresolved one is INCONCLUSIVE, never agreement.
 * 64-bit arithmetic: query Mbit/s x 1,000,000 against TransmitLinkSpeed. A connected rate that is unknown (0 or all ones) or
 * not representable in integer Mbit/s (not a whole Mbit/s, or above 2^32-1) has no specified conversion yet (#472): that
 * comparison is INCONCLUSIVE, never a measured control. Every raw value is printed with its QPC time.
 *   PRESTATE  the OS observation and a query before anything else, evidence only; the OS state is FINAL's link reference.
 *   CYCLE     the test's own NIC cycle of the adapter (the attach ATTACH needs), bounded; the adapter must return with the same
 *             NET_LUID, device ID and MAC. If not: CLEANUP_FAILED, and ATTACH, STEADY and FINAL are BLOCKED.
 *   ATTACH    after the cycle: when the link was connected before the test, the OS reports it connected again within
 *             LINK_BOUND_MS first; then one bracketed comparison.
 *   STEADY    two bracketed comparisons LINK_STEADY_INTERVAL_MS apart; each must agree with its own bracket. An OS state or rate
 *             that changed between them is printed as a measured link event, not failed for violating a constant speed.
 *   EVENT     a link transition without a detach is not exercised: the test has no owned procedure that changes the link
 *             without detaching the filter and restores it (a cable or link-partner change depends on topology the test cannot
 *             establish, #463). INCONCLUSIVE with that reason; repeating ATTACH is not EVENT coverage, never PASS by omission.
 *   FINAL     the adapter is bound, the OS link state and rate equal the pre-test observation, the TAS query reports a clean,
 *             readable adapter and GET_TAS_STATE is CLEAN, and HardwareFaults did not change from ATTACH to FINAL. The test arms
 *             no fault. A difference is CLEANUP_FAILED, reported apart from the functional checks.
 *   CLOCK     cleanup-only evidence (runner convention [CLEANUP:FAILED] / [CLEANUP:NOT_VERIFIED] / [CLEANUP:SUCCESS], never a
 *             functional verdict): TIMINCA and TSAUXC read through GET_CLOCK_CONFIG before the first NIC operation and after
 *             cleanup, each with its raw NTSTATUS, size and reply status. A measured difference is [CLEANUP:FAILED]; a missing or
 *             invalid read is [CLEANUP:NOT_VERIFIED]. Read-only: nothing is written back, because neither the incoming nor the
 *             post-cycle values are an established restoration target. A clean TAS query does not prove clock restoration.
 * The incoming TAS state is printed as found (origin unknown to the test); it is never assumed clean.
 * Mutation: the owned NIC cycle only (it reseeds the PHC, E12; reported, not restored). No TAS write, no traffic. Agreement with
 * the OS checks the acquisition path through the shared Windows/miniport reporting chain; it is not a wire measurement and does
 * not verify TAS-DR-05's arithmetic (#368). Expected RED against a driver without K8: link_speed_mbps 0 while the OS reports a
 * connected speed (a hypothesis until a hardware run shows it). Unlike the other modes, it needs no IPv4 address, no
 * observer and no link: a disconnected adapter is a valid ATTACH case (0).
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
 * always followed by that NIC cycle. --invalid sends invalid IOCTL_AVB_TAS_ACTIVATE requests (END_EQ_CYCLE can be accepted
 * and commit a schedule), always followed by that NIC cycle. --restore commits the verified HOLD schedule, always followed by
 * that NIC cycle. --link-speed runs that NIC cycle as its setup and then sends only queries and reads. Safety class Unsafe.
 *
 * Exit: 0 all passed, 1 any nonpassing verdict, 2 no adapter ran, 3 only BLOCKED.
 *
 * Verifies: #350
 * Verifies: #353
 * (--hw-verified, TC-TASAPI-016: Verifies #350 per #381)
 * Verifies: #351 (--tiers-unbound, TC-TASAPI-002, per #367; --invalid, TC-TASAPI-003, per #368)
 * Verifies: #361 (--restore, TC-TASAPI-012, per #377)
 * Verifies: #351 (--link-speed, TC-TASAPI-021, per #472)
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
#include "../../intel-ethernet-regs/gen/i226_regs.h"   /* --invalid: the QBVCYCLET.QBV_CYCLE_TIME field width (SSOT) */

#define CASE_ID            g_case                   /* TC-TASAPI-001 (#366), -016 (--hw-verified, #381), -002 (--tiers-unbound, #367), -003 (--invalid, #368), -012 (--restore, #377), -021 (--link-speed, #472) */
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
/* --link-speed (#472): the test's own choices, documented bounds, never tolerances on the compared values */
#define LINK_BRACKET_TRIES        5        /* a bracket whose OS readings differ is retried at most this often */
#define LINK_BRACKET_RETRY_MS     500
#define LINK_BOUND_MS             60000    /* after the cycle: wait for the OS to report a pre-test connected link again */
#define LINK_STEADY_INTERVAL_MS   10000    /* between the two STEADY observations */
#define BITS_PER_MBIT             1000000ULL
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
static BOOL g_tiers_mode;         /* --tiers-unbound: TC-TASAPI-002 (#367) */
static BOOL g_invalid_mode;       /* --invalid: TC-TASAPI-003 (#368) */
static BOOL g_restore_mode;       /* --restore: TC-TASAPI-012 (#377) */
static BOOL g_link_mode;          /* --link-speed: TC-TASAPI-021 (#472) */
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

/* One verdict line. Nonpassing tags count as failures, BLOCKED separately. SKIP_UNSUPPORTED (capability evidenced absent,
 * STATUS_NOT_SUPPORTED) is green per docs/testing/test-verdict-contract.md and counts as neither */
static void verdict(const char *tag, const char *check, UINT32 adapter, avb_u16 did, const char *reason)
{
    printf("[%s] %s-%s/adapter:%u did=0x%04X%s%s\n", tag, CASE_ID, check, adapter, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
    if (strcmp(tag, "BLOCKED") == 0) {
        g_blocked++;
    } else if (strcmp(tag, "PASS") != 0 && strcmp(tag, "SKIP_UNSUPPORTED") != 0) {
        g_failed++;
    }
}

/* A verdict line that belongs to no adapter (TC-TASAPI-002 UNBOUND) */
static void verdict_global(const char *tag, const char *check, const char *reason)
{
    printf("[%s] %s-%s%s%s\n", tag, CASE_ID, check, reason ? ": " : "", reason ? reason : "");
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

/** --tiers-unbound, TC-TASAPI-002 (#367, #365 12.1 row 1). Two separate lines:
 *  UNBOUND           the raw NTSTATUS of the v1 query on a handle never bound: exactly STATUS_INVALID_DEVICE_STATE, no output.
 *                    A wrong status is FAIL whatever the counters show.
 *  UNBOUND-COUNTERS  an observation, not a proof of routing: each adapter's IoctlCount (statistics of its own context, read on a
 *                    bound handle) across the unbound query against two back-to-back reads. Equal deltas on every adapter of a
 *                    complete, non-empty inventory: PASS. An incomplete or empty inventory: BLOCKED. A reading unavailable, or a
 *                    delta that differs (other requests or a statistics reset can move the counter, so the difference cannot
 *                    be attributed to this query): INCONCLUSIVE. Every measured delta is printed. */
static void check_unbound(const AVB_ADAPTER_IDENTITY_REQUEST *all, UINT32 n, BOOL inventory_complete)
{
    HANDLE *hs = (HANDLE *)calloc(n ? n : 1, sizeof(HANDLE)), hu;
    QUERY_READ r;
    UINT32 i, drifted = 0, unavailable = 0;
    char reason[320];

    ZeroMemory(&r, sizeof(r));
    if (hs == NULL) {
        verdict_global("BLOCKED", "UNBOUND", "no memory for the per-adapter handles");
        return;
    }
    for (i = 0; i < n; i++) {
        hs[i] = open_bound_read(all[i].net_luid);
    }
    /* Per adapter, in the same pattern: cal0, cal1 back to back (calibration delta), then before, the one unbound query, after */
    {
        STATS_READ *before = (STATS_READ *)calloc(n ? n : 1, sizeof(STATS_READ));
        STATS_READ *after = (STATS_READ *)calloc(n ? n : 1, sizeof(STATS_READ));
        STATS_READ *cal0 = (STATS_READ *)calloc(n ? n : 1, sizeof(STATS_READ));
        STATS_READ *cal1 = (STATS_READ *)calloc(n ? n : 1, sizeof(STATS_READ));
        if (before == NULL || after == NULL || cal0 == NULL || cal1 == NULL) {
            free(before); free(after); free(cal0); free(cal1);
            for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { CloseHandle(hs[i]); } }
            free(hs);
            verdict_global("BLOCKED", "UNBOUND", "no memory for the statistics readings");
            return;
        }
        for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { read_stats(hs[i], &cal0[i]); } }
        for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { read_stats(hs[i], &cal1[i]); } }
        for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { read_stats(hs[i], &before[i]); } }
        hu = open_device(GENERIC_READ);   /* never bound: no OPEN_ADAPTER, no OPEN_ADAPTER_BY_LUID */
        if (hu == INVALID_HANDLE_VALUE) {
            free(before); free(after); free(cal0); free(cal1);
            for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { CloseHandle(hs[i]); } }
            free(hs);
            verdict_global("BLOCKED", "UNBOUND", "cannot open an unbound handle with read access");
            return;
        }
        query(hu, &r);
        CloseHandle(hu);
        for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { read_stats(hs[i], &after[i]); } }
        for (i = 0; i < n; i++) {
            if (hs[i] == INVALID_HANDLE_VALUE || !cal0[i].ok || !cal1[i].ok || !before[i].ok || !after[i].ok) {
                unavailable++;
                printf("    [EVIDENCE] unbound check adapter:%u did=0x%04X: IoctlCount unavailable\n", all[i].index, all[i].device_id);
                continue;
            }
            {
                /* Signed deltas: a statistics reset makes one negative, and is then reported, never hidden */
                long long dcal = (long long)(cal1[i].s.IoctlCount - cal0[i].s.IoctlCount);
                long long dq = (long long)(after[i].s.IoctlCount - before[i].s.IoctlCount);
                printf("    [EVIDENCE] unbound check adapter:%u did=0x%04X: IoctlCount delta without the query=%lld, across the "
                       "unbound query=%lld\n", all[i].index, all[i].device_id, dcal, dq);
                if (dq != dcal) {
                    drifted++;
                }
            }
        }
        free(before); free(after); free(cal0); free(cal1);
    }
    for (i = 0; i < n; i++) { if (hs[i] != INVALID_HANDLE_VALUE) { CloseHandle(hs[i]); } }
    free(hs);

    /* UNBOUND: the raw status alone */
    printf("    [EVIDENCE] unbound query: ntstatus=0x%08lX information=%lu (expected 0x%08lX, information 0)\n",
           (unsigned long)r.status, r.got, (unsigned long)STATUS_INVALID_DEVICE_STATE);
    if (r.status == AVB_NTSTATUS_UNAVAILABLE) {
        verdict_global("INCONCLUSIVE", "UNBOUND", "raw NTSTATUS not captured");
    } else if (r.status != (LONG)STATUS_INVALID_DEVICE_STATE || r.got != 0) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu, expected exactly STATUS_INVALID_DEVICE_STATE (0x%08lX) and no output%s",
                  (unsigned long)r.status, r.got, (unsigned long)STATUS_INVALID_DEVICE_STATE,
                  r.status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" : "");
        verdict_global("FAIL", "UNBOUND", reason);
    } else {
        verdict_global("PASS", "UNBOUND", "STATUS_INVALID_DEVICE_STATE, no output");
    }

    /* UNBOUND-COUNTERS: the observation, judged only on a complete, non-empty inventory and never attributed beyond the data */
    if (!inventory_complete || n == 0) {
        sprintf_s(reason, sizeof(reason), "adapter inventory %s (%u adapter(s) observed); no counter observation can cover every context",
                  n == 0 ? "empty" : "incomplete", n);
        verdict_global("BLOCKED", "UNBOUND-COUNTERS", reason);
    } else if (unavailable != 0) {
        sprintf_s(reason, sizeof(reason), "IoctlCount unavailable for %u of %u adapter(s)", unavailable, n);
        verdict_global("INCONCLUSIVE", "UNBOUND-COUNTERS", reason);
    } else if (drifted != 0) {
        sprintf_s(reason, sizeof(reason), "%u of %u adapter(s) moved differently across the query than in calibration; other requests or a "
                  "statistics reset can do that, so the difference is not attributed to the query (see the deltas)", drifted, n);
        verdict_global("INCONCLUSIVE", "UNBOUND-COUNTERS", reason);
    } else {
        sprintf_s(reason, sizeof(reason), "IoctlCount moved exactly as in calibration on all %u adapter(s) of a complete inventory "
                  "(an observation, see the deltas)", n);
        verdict_global("PASS", "UNBOUND-COUNTERS", reason);
    }
}

/** --tiers-unbound, TC-TASAPI-002 CAPABILITY (#367, #365 12.1 row 5): on an adapter without INTEL_CAP_TSN_TAS the bound query
 *  is refused with STATUS_NOT_SUPPORTED, which is SKIP_UNSUPPORTED. Read-only: nothing to restore on that adapter. */
static void check_capability_absent(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    QUERY_READ r;
    HANDLE h;
    AVB_VERDICT v;
    char reason[256];

    g_ran++;
    h = open_bound_read(id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "CAPABILITY", index, id->device_id, "cannot open a read-only handle and bind it by NET_LUID");
        return;
    }
    query(h, &r);
    CloseHandle(h);
    printf("    [EVIDENCE] capability absent adapter:%u did=0x%04X caps=0x%08X: ntstatus=0x%08lX information=%lu\n", index,
           id->device_id, id->capabilities, (unsigned long)r.status, r.got);
    if (r.status >= 0 && r.status != AVB_NTSTATUS_UNAVAILABLE) {
        verdict("FAIL", "CAPABILITY", index, id->device_id, "the query was served on an adapter without INTEL_CAP_TSN_TAS");
        return;
    }
    if (r.status != AVB_NTSTATUS_UNAVAILABLE && r.got != 0) {   /* #365 12.1: on any error, IoStatus.Information is 0 */
        sprintf_s(reason, sizeof(reason), "error ntstatus=0x%08lX with information=%lu; every error must complete with no output%s",
                  (unsigned long)r.status, r.got, r.status == (LONG)STATUS_NOT_SUPPORTED ? " (STATUS_NOT_SUPPORTED with output)" : "");
        verdict("FAIL", "CAPABILITY", index, id->device_id, reason);
        return;
    }
    v = avb_verdict_from_status((NTSTATUS)r.status, AVB_CAP_ABSENT, FALSE, TRUE);
    sprintf_s(reason, sizeof(reason), "INTEL_CAP_TSN_TAS absent, ntstatus=0x%08lX%s", (unsigned long)r.status,
              r.status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW; expected STATUS_NOT_SUPPORTED)" : "");
    verdict(avb_verdict_tag(v), "CAPABILITY", index, id->device_id, reason);
}

/** --tiers-unbound, TC-TASAPI-002 on a TAS adapter: QUERY, then TIERS (#351). No #351 tier table exists: tiers read 0 (not
 *  reported) and stay incomplete. Only what the published contract fixes is asserted: a reported tier is 1..4, and tier_pass
 *  is NOT_IMPLEMENTED (#363 option C). No other tier value is assumed. */
static void run_tiers(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, IN_ADDR ipv4)
{
    QUERY_READ q0;
    PHC_READ p0;
    AVB_VERDICT v;
    const AVB_TAS_LIMITS *l;
    avb_u32 t[5];
    int k, reported = 0, out_of_range = 0;
    char reason[320];

    read_phc(h, &p0);
    query(h, &q0);
    print_query("tiers", &q0);
    v = avb_verdict_from_status((NTSTATUS)q0.status, AVB_CAP_PRESENT, query_valid(&q0), TRUE);
    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu%s", (unsigned long)q0.status, q0.got,
                  q0.status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" : "");
        verdict(avb_verdict_tag(v), "QUERY", index, id->device_id, reason);
        verdict("BLOCKED", "TIERS", index, id->device_id, "requires a valid query");
        cleanup_and_verify(h, index, id, inst, ipv4, &q0, &p0);
        return;
    }
    verdict("PASS", "QUERY", index, id->device_id, NULL);
    l = &q0.q.limits;
    t[0] = l->tier_hold; t[1] = l->tier_pass; t[2] = l->tier_strict_end; t[3] = l->tier_strict_cycle; t[4] = l->tier_per_queue;
    for (k = 0; k < 5; k++) {
        reported += t[k] != 0;
        out_of_range += t[k] > AVB_TAS_TIER_IMPLEMENTED_VERIFIED;
    }
    printf("    [EVIDENCE] tiers: hold=%u pass=%u strict_end=%u strict_cycle=%u per_queue=%u (0 = not reported; #351 table absent)\n",
           t[0], t[1], t[2], t[3], t[4]);
    if (out_of_range != 0 || (t[1] != 0 && t[1] != AVB_TAS_TIER_NOT_IMPLEMENTED)) {
        verdict("FAIL", "TIERS", index, id->device_id, "a tier outside 1..4, or tier_pass other than NOT_IMPLEMENTED (#363 option C)");
    } else {
        sprintf_s(reason, sizeof(reason), "%d of 5 tiers reported; without the #351 tier table they cannot be verified: incomplete", reported);
        verdict("INCONCLUSIVE", "TIERS", index, id->device_id, reason);
    }
    cleanup_and_verify(h, index, id, inst, ipv4, &q0, &p0);
}

/* --invalid, TC-TASAPI-003 (#368). The window rule of DES 5.2 TAS-DR-05, computed here from the design text, not from the
 * driver: a window must exceed 1.1 x the transmit time of max(512 bytes, max_frame_bytes) at the link speed */
#define WINDOW_RULE_FLOOR_BYTES   512ULL
#define WINDOW_RULE_NS_MBPS       8800ULL   /* 1.1 x 8 bit x 1000 ns per us: ns x Mbit/s per byte */
#define IEEE8023_MIN_FRAME_BYTES  64u       /* smallest Ethernet frame */
#define BASET_H_SECONDS_LIMIT     0x100000000ULL   /* BaseT seconds must fit the 32-bit BASET_H register (TAS-DR-07) */

typedef enum {
    INV_VERSION, INV_PRE_ACTIVATION, INV_RESERVED, INV_CYCLE_ZERO, INV_CYCLE_FIELD, INV_BASET_SECONDS, INV_BASET_NEAR,
    INV_FRAME_MIN, INV_WINDOW_EMPTY, INV_WINDOW_PAST_CYCLE, INV_WINDOW_MIN, INV_SHORT_BUFFER,
    INV_END_EQ_CYCLE,   /* last: today's validation does not refuse it, so it can commit a schedule */
    INV_COUNT
} INV_ID;

/* One case: its rule with the authoritative source, and the completion the design requires for the refusal (no output) */
typedef struct {
    const char *name;
    const char *rule;
    LONG required;
    const char *required_src;
} INV_CASE;

static const INV_CASE g_inv[INV_COUNT] = {
    { "VERSION", "TAS-DR-01, DES 3.4: request version one above AVB_TAS_ACTIVATE_VERSION",
      (LONG)STATUS_REVISION_MISMATCH, "DES 3.4 (wrong version)" },
    { "PRE_ACTIVATION", "TAS-DR-14, DES 5.2 and #357: pre_activation set explicitly, there is no default; 0 sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "RESERVED", "TAS-DR-01, DES 3.4: reserved field 0; 0xFFFFFFFF sent, above every Debug fault-injection index",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG (DES 3.4 names no status for reserved fields)" },
    { "CYCLE_ZERO", "TAS-DR-04, DES 5.2: cycle within the hardware range; 0 sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "CYCLE_FIELD", "TAS-DR-04, DES 5.2: cycle within the hardware range; one above the QBVCYCLET.QBV_CYCLE_TIME field (SSOT) sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "BASET_SECONDS", "TAS-DR-07, DES 5.2: BaseT seconds fit BASET_H; 2^32 s sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "BASET_NEAR", "TAS-DR-08, DES 5.2: BaseT at least the minimum margin after a PHC read; the cycle boundary at or before the PHC sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 DEADLINE_TOO_NEAR" },
    { "FRAME_MIN", "max_frame_bytes below the IEEE 802.3 minimum frame of 64 bytes (the #357 field TAS-DR-05 depends on); 63 sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "WINDOW_EMPTY", "TAS-DR-05, DES 5.2: start_ns < end_ns; every queue start equal to its end sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "WINDOW_PAST_CYCLE", "TAS-DR-05, DES 5.2: end_ns <= cycle_ns; every queue end one ns past the cycle sent",
      (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "WINDOW_MIN", "TAS-DR-05, DES 5.2, #351 AC3: window greater than 1.1 x transmit time of max(512 bytes, max_frame_bytes) at the "
      "link speed; every queue window equal to that bound (rounded down) sent", (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
    { "SHORT_BUFFER", "DES 3.4: short buffer; the request one byte short sent",
      (LONG)STATUS_BUFFER_TOO_SMALL, "DES 3.4 (short buffer)" },
    { "END_EQ_CYCLE", "TAS-DR-06, DES 5.2: end_ns < cycle_ns with a gap, except OPEN_ALL (basis DOC 7.5.2.9.2 and EG-06, which stays "
      "UNVERIFIED); every queue end equal to the cycle sent. OPEN_ALL is a REPLACE schedule (DES 8, #359) that this request cannot "
      "express, so the exception does not apply", (LONG)STATUS_INVALID_PARAMETER, "DES 9.5 INVALID_CONFIG" },
};

typedef struct {
    HANDLE h, hw;
    UINT32 index;
    const AVB_ADAPTER_IDENTITY_REQUEST *id;
    const MIB_IF_ROW2 *row;
    const QUERY_READ *q0;
    const STATS_READ *s0;
} INV_CTX;

static const char *act_result_name(avb_u32 r)
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
    default:                              return "unknown";
    }
}

/** Results after which the activation may have written registers */
static BOOL act_result_wrote(avb_u32 r)
{
    return r == AVB_TAS_ACT_COMMITTED || r == AVB_TAS_ACT_BASE_PASSED || r == AVB_TAS_ACT_COMMITTED_UNTIMED ||
           r == AVB_TAS_ACT_FAILED_RESTORED || r == AVB_TAS_ACT_FAILED_NOT_RESTORED;
}

static BOOL stats_unchanged(const AVB_DRIVER_STATISTICS *a, const AVB_DRIVER_STATISTICS *b)
{
    return a->PhcAdjustCount == b->PhcAdjustCount && a->PhcSetCount == b->PhcSetCount && a->HardwareFaults == b->HardwareFaults &&
           a->FilterPauseCount == b->FilterPauseCount && a->FilterRestartCount == b->FilterRestartCount &&
           a->PauseRestartGeneration == b->PauseRestartGeneration;
}

/** The verified HOLD set of #381 and test_tas_activate.c, BaseT the first cycle boundary at least BASE_LEAD_NS past phc */
static void inv_request(AVB_TAS_ACTIVATE_REQUEST *r, const MIB_IF_ROW2 *row, avb_u64 phc)
{
    unsigned q;

    ZeroMemory(r, sizeof(*r));
    r->version = AVB_TAS_ACTIVATE_VERSION;
    r->pre_activation = AVB_TAS_PRE_HOLD;
    r->base_time_ns = (avb_u64)((((long long)phc + BASE_LEAD_NS + CYCLE_NS - 1) / CYCLE_NS) * CYCLE_NS);
    r->cycle_ns = (avb_u32)CYCLE_NS;
    r->link_mbps = (avb_u32)(row->TransmitLinkSpeed / 1000000ULL);
    r->max_frame_bytes = (avb_u32)row->Mtu + FRAME_OVERHEAD_BYTES;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        r->window_start_ns[q] = (avb_u32)WINDOW_START_NS;
        r->window_end_ns[q] = (avb_u32)WINDOW_END_NS;
    }
}

static void inv_blocked(const char *name, UINT32 index, avb_u16 did, const char *reason)
{
    verdict("BLOCKED", name, index, did, reason);
}

/** One case: REFUSED, UNCHANGED and COMPLETION lines. TRUE when the adapter is still measured in its clean pre-state */
static BOOL run_inv_case(const INV_CTX *c, INV_ID k)
{
    const INV_CASE *t = &g_inv[k];
    const avb_u16 did = c->id->device_id;
    AVB_TAS_ACTIVATE_REQUEST req;
    PHC_READ p;
    QUERY_READ qa;
    STATE_READ ta;
    STATS_READ sa;
    DWORD got = 0, len = (DWORD)sizeof(req);
    LONG st;
    BOOL reply, wrote = FALSE, refused = FALSE, measured, unchanged;
    unsigned differing = 0, q;
    char check[48], reason[480], res[48];

    printf("\n  --- %s: %s ---\n", t->name, t->rule);
    read_phc(c->h, &p);
    if (!p.ok || c->row->TransmitLinkSpeed == 0) {
        inv_blocked(t->name, c->index, did, !p.ok ? "PHC unavailable: no valid BaseT can be placed, so the case cannot isolate its field" :
                    "link speed unknown: the window rule cannot be applied");
        return FALSE;
    }
    inv_request(&req, c->row, p.phc);
    switch (k) {
    case INV_VERSION:        req.version = AVB_TAS_ACTIVATE_VERSION + 1u; break;
    case INV_PRE_ACTIVATION: req.pre_activation = 0; break;
    case INV_RESERVED:       req.reserved = 0xFFFFFFFFu; break;
    case INV_CYCLE_ZERO:     req.cycle_ns = 0; break;
    case INV_CYCLE_FIELD:
        req.cycle_ns = (avb_u32)((I226_QBVCYCLET_QBV_CYCLE_TIME_MASK >> I226_QBVCYCLET_QBV_CYCLE_TIME_SHIFT) + 1ULL);
        break;
    case INV_BASET_SECONDS:  req.base_time_ns = BASET_H_SECONDS_LIMIT * (avb_u64)NS_PER_SEC; break;
    case INV_BASET_NEAR:     req.base_time_ns = (p.phc / (avb_u64)CYCLE_NS) * (avb_u64)CYCLE_NS; break;
    case INV_FRAME_MIN:      req.max_frame_bytes = IEEE8023_MIN_FRAME_BYTES - 1u; break;
    /* Window cases change every queue alike, as the verified set has identical windows: differing windows could make the
     * gate-list conversion refuse the request for another reason than the rule under test */
    case INV_WINDOW_EMPTY:   for (q = 0; q < AVB_TAS_QUEUES; q++) { req.window_end_ns[q] = req.window_start_ns[q]; } break;
    case INV_WINDOW_PAST_CYCLE: for (q = 0; q < AVB_TAS_QUEUES; q++) { req.window_end_ns[q] = req.cycle_ns + 1u; } break;
    case INV_WINDOW_MIN: {
        avb_u64 floor_bytes = req.max_frame_bytes > WINDOW_RULE_FLOOR_BYTES ? req.max_frame_bytes : WINDOW_RULE_FLOOR_BYTES;
        for (q = 0; q < AVB_TAS_QUEUES; q++) {
            req.window_end_ns[q] = req.window_start_ns[q] + (avb_u32)(floor_bytes * WINDOW_RULE_NS_MBPS / req.link_mbps);
        }
        break;
    }
    case INV_SHORT_BUFFER:   len = (DWORD)sizeof(req) - 1u; break;
    case INV_END_EQ_CYCLE:   for (q = 0; q < AVB_TAS_QUEUES; q++) { req.window_end_ns[q] = req.cycle_ns; } break;
    default: break;
    }
    req.phc_at_commit_ns = 0;
    req.result = 0xFFFFFFFFu;   /* values the driver never writes: an unwritten reply cannot read as a result */
    req.status = 0xFFFFFFFFu;
    printf("    [EVIDENCE] %s request: version=%u pre=%u reserved=0x%X base=%llu (PHC %llu) cycle=%u link=%u frame=%u "
           "q0=[%u, %u) q1-3=[%u, %u) length=%lu\n", t->name, req.version, req.pre_activation, req.reserved, req.base_time_ns, p.phc,
           req.cycle_ns, req.link_mbps, req.max_frame_bytes, req.window_start_ns[0], req.window_end_ns[0], req.window_start_ns[1],
           req.window_end_ns[1], len);
    st = avb_ioctl_ntstatus(c->hw, IOCTL_AVB_TAS_ACTIVATE, &req, len, &req, len, &got);
    reply = st == 0 && got == sizeof(req);
    if (reply) {
        sprintf_s(res, sizeof(res), "%s (%u)", act_result_name(req.result), req.result);
    } else {
        strcpy_s(res, sizeof(res), "not delivered");
    }
    printf("    [EVIDENCE] %s reply: ntstatus=0x%08lX information=%lu result=%s status=0x%08X phcAtCommit=%llu\n", t->name,
           (unsigned long)st, got, res, reply ? req.status : 0u, reply ? req.phc_at_commit_ns : 0ULL);

    /* REFUSED: the observation alone, whatever the completion contract says */
    sprintf_s(check, sizeof(check), "%s-REFUSED", t->name);
    if (st == AVB_NTSTATUS_UNAVAILABLE) {
        wrote = TRUE;   /* unknown: never assume nothing was written */
        verdict("INCONCLUSIVE", check, c->index, did, "raw NTSTATUS not captured; whether the request was refused is unknown");
    } else if (reply && req.result == AVB_TAS_ACT_INVALID) {
        refused = TRUE;
        verdict("PASS", check, c->index, did, "refused: interim #357 reply STATUS_SUCCESS, result INVALID; characterization of "
                "validation that predates this test, not RED");
    } else if (reply) {
        wrote = act_result_wrote(req.result);
        sprintf_s(reason, sizeof(reason), "not refused: result %s; the rule requires a refusal%s", res,
                  wrote ? "; the activation may have written registers" : "");
        verdict("FAIL", check, c->index, did, reason);
    } else if (st == 0) {
        wrote = TRUE;
        sprintf_s(reason, sizeof(reason), "STATUS_SUCCESS without the complete reply (information=%lu); the outcome is unknown", got);
        verdict("FAIL", check, c->index, did, reason);
    } else if (got == 0 && (st == t->required || st == (LONG)STATUS_INVALID_PARAMETER || st == (LONG)STATUS_REVISION_MISMATCH ||
                            st == (LONG)STATUS_BUFFER_TOO_SMALL)) {
        refused = TRUE;
        sprintf_s(reason, sizeof(reason), "refused: error completion ntstatus=0x%08lX, no output; characterization of validation "
                  "that predates this test, not RED", (unsigned long)st);
        verdict("PASS", check, c->index, did, reason);
    } else {
        AVB_VERDICT v = avb_verdict_from_status((NTSTATUS)st, AVB_CAP_PRESENT, FALSE, TRUE);
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu: not a refusal any validation rule names%s",
                  (unsigned long)st, got, st == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_ACTIVATE)" : "");
        verdict(v == AVB_VERDICT_PASS ? "FAIL" : avb_verdict_tag(v), check, c->index, did, reason);
    }

    /* UNCHANGED: the test's own capture before the cases against a query, the record and the counters after this case */
    sprintf_s(check, sizeof(check), "%s-UNCHANGED", t->name);
    query(c->h, &qa);
    read_state(c->h, &ta);
    read_stats(c->h, &sa);
    measured = query_readable(&qa) && ta.ok && sa.ok && c->s0->ok;
    if (measured) {
        differing = schedule_regs_differing(&c->q0->q.regs, &qa.q.regs);
    }
    unchanged = measured && differing == 0 && query_clean(&qa) && ta.s.armed == AVB_TAS_STATE_CLEAN && stats_unchanged(&c->s0->s, &sa.s);
    printf("    [EVIDENCE] %s after: query ntstatus=0x%08lX readable=%d regsDiffering=%u hw_mode=%u current=%u next=%u restoration=%u "
           "armed=%s BASET_H 0x%08X->0x%08X BASET_L 0x%08X->0x%08X (reported only, #365 12.4)\n", t->name, (unsigned long)qa.status,
           query_readable(&qa), differing, qa.q.hw_mode, qa.q.current_status, qa.q.next_status, qa.q.restoration,
           ta.ok ? (ta.s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not clean") : "unavailable", c->q0->q.regs.baset_h,
           qa.q.regs.baset_h, c->q0->q.regs.baset_l, qa.q.regs.baset_l);
    if (!measured) {
        print_query(t->name, &qa);
        sprintf_s(reason, sizeof(reason), "not measured: readable query %s, TAS record %s, statistics %s",
                  query_readable(&qa) ? "yes" : "no", ta.ok ? "read" : "unavailable", sa.ok ? "read" : "unavailable");
        verdict("BLOCKED", check, c->index, did, reason);
    } else if (!unchanged) {
        print_query(t->name, &qa);
        sprintf_s(reason, sizeof(reason), "changed against the pre-test capture: registers differing %u, clean %d, record %s, "
                  "counters %s", differing, query_clean(&qa), ta.s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not clean",
                  stats_unchanged(&c->s0->s, &sa.s) ? "unchanged" : "changed");
        verdict("FAIL", check, c->index, did, reason);
    } else {
        verdict("PASS", check, c->index, did, "registers, record and counters as captured before the cases");
    }

    /* COMPLETION: conformance to the operation's completion contract, kept apart from REFUSED and UNCHANGED */
    sprintf_s(check, sizeof(check), "%s-COMPLETION", t->name);
    if (st == AVB_NTSTATUS_UNAVAILABLE) {
        verdict("INCONCLUSIVE", check, c->index, did, "raw NTSTATUS not captured");
    } else if (st == t->required && got == 0) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX with no output, as %s requires", (unsigned long)st, t->required_src);
        verdict("PASS", check, c->index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "%s requires ntstatus=0x%08lX with no output; observed ntstatus=0x%08lX information=%lu%s",
                  t->required_src, (unsigned long)t->required, (unsigned long)st, got,
                  reply && req.result == AVB_TAS_ACT_INVALID ? " (interim #357 reply STATUS_SUCCESS + result INVALID; completion "
                  "model K2/#352 not implemented)" : "");
        verdict("FAIL", check, c->index, did, reason);
    }
    /* A later request needs an established refusal, not only an unchanged snapshot (PR #451 review); COMPLETION never stops */
    return refused && measured && unchanged && !wrote;
}

/** TX of the I226 under test, received by the I210 wire observer: a checked window against the baseline before the cases */
static void inv_tx_verdict(const char *check, UINT32 index, avb_u16 did, const TAS_TX_WINDOW *base, const TAS_TX_WINDOW *probe)
{
    char reason[256];

    switch (tas_tx_judge(base, probe, TAS_ACT_SEEN_MIN_PERCENT)) {
    case TAS_TX_BLOCKED_BASELINE:
        sprintf_s(reason, sizeof(reason), "baseline not established: seen %u of %u attempted, %u send errors; the wire proves nothing here",
                  base->seen, base->attempted, base->errors);
        verdict("BLOCKED", check, index, did, reason);
        return;
    case TAS_TX_PASS:
        verdict("PASS", check, index, did, "frames on the wire as in the baseline, interleaved queries report the pre-test registers");
        return;
    default:
        break;
    }
    sprintf_s(reason, sizeof(reason), "seen %u of %u attempted, %u send errors, %u of %u queries failed or reported other registers",
              probe->seen, probe->attempted, probe->errors, probe->changed, probe->queries);
    verdict("FAIL", check, index, did, reason);
}

/** --invalid, TC-TASAPI-003 (#368, Verifies #351): each invalid request refused, TAS query, record and TX unchanged */
static void run_invalid(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, IN_ADDR ipv4,
                        const MIB_IF_ROW2 *row)
{
    QUERY_READ q0;
    STATS_READ s0;
    STATE_READ t0;
    PHC_READ p0;
    INV_CTX c;
    AVB_VERDICT v;
    SOCKET sock = INVALID_SOCKET;
    TAS_TX_WINDOW base, probe;
    unsigned nonce = 0;
    BOOL wire = FALSE, wire_started = FALSE;
    int k = 0;
    const char *stop = NULL;
    char reason[320];

    ZeroMemory(&c, sizeof(c));
    ZeroMemory(&base, sizeof(base));
    read_phc(h, &p0);
    read_stats(h, &s0);
    read_state(h, &t0);
    query(h, &q0);
    print_query("pre-test", &q0);
    printf("    [EVIDENCE] DTXMXPKTSZ raw=0x%08X decoded=%u bytes state=%u (measurement, #365 12.7)\n",
           q0.q.regs.dtxmxpktsz, q0.q.dtx_max_frame_bytes, q0.q.dtx_max_frame_state);

    /* PRESTATE: the test proves its own pre-state through the TAS query before it submits anything (#368) */
    v = avb_verdict_from_status((NTSTATUS)q0.status, AVB_CAP_PRESENT, query_valid(&q0), TRUE);
    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "pre-test query ntstatus=0x%08lX information=%lu%s", (unsigned long)q0.status, q0.got,
                  q0.status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" : "");
        verdict(avb_verdict_tag(v), "PRESTATE", index, id->device_id, reason);
        stop = "no valid pre-test query";
    } else if (!query_clean(&q0) || !t0.ok || t0.s.armed != AVB_TAS_STATE_CLEAN || !s0.ok || !p0.ok) {
        sprintf_s(reason, sizeof(reason), "pre-state not established: clean query %d, TAS record %s, statistics %s, PHC %s",
                  query_clean(&q0), t0.ok ? (t0.s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not clean") : "unavailable",
                  s0.ok ? "read" : "unavailable", p0.ok ? "read" : "unavailable");
        verdict("BLOCKED", "PRESTATE", index, id->device_id, reason);
        stop = "the clean pre-state was not established";
    } else {
        verdict("PASS", "PRESTATE", index, id->device_id, "clean, readable query; TAS record CLEAN; statistics and PHC read");
    }

    if (stop == NULL) {
        c.hw = open_device(GENERIC_READ | GENERIC_WRITE);   /* TAS_ACTIVATE is FILE_WRITE_ACCESS (#362) */
        if (c.hw == INVALID_HANDLE_VALUE || !bind_luid(c.hw, id->net_luid)) {
            if (c.hw != INVALID_HANDLE_VALUE) { CloseHandle(c.hw); }
            c.hw = INVALID_HANDLE_VALUE;
            stop = "cannot bind a handle with write access for IOCTL_AVB_TAS_ACTIVATE";
        }
    }
    /* OBSERVER (#368 "I210 observer alive"): an I210 identity is not a live observer. Before any request the capture must have
     * started and must have received the baseline frames the I226 sent; otherwise nothing is submitted */
    if (stop == NULL) {
        sock = open_tx_socket(ipv4);
        nonce = GetTickCount() ^ GetCurrentProcessId();
        if (sock != INVALID_SOCKET) {
            wire_started = TRUE;   /* wire_start can start its ICMP worker even when it returns 0 observers: stop it in every case */
            wire = wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) != 0;
        }
        if (wire) {
            send_window(sock, nonce, h, NULL, &base);
            print_window("before the cases", &base);
        }
        if (!wire) {
            verdict("BLOCKED", "OBSERVER", index, id->device_id, "no TX socket, or the I210 capture did not start: the receiving wire "
                    "observer is not established (#368 precondition)");
            stop = "the receiving wire observer was not established";
        } else if (!tas_tx_on_wire(&base, TAS_ACT_SEEN_MIN_PERCENT)) {
            sprintf_s(reason, sizeof(reason), "baseline window: the I210 received %u of %u frames the I226 sent, %u send errors; the "
                      "receiving wire observer is not proven alive (#368 precondition)", base.seen, base.attempted, base.errors);
            verdict("BLOCKED", "OBSERVER", index, id->device_id, reason);
            stop = "the receiving wire observer did not receive the baseline window";
        } else {
            sprintf_s(reason, sizeof(reason), "the I210 received %u of %u baseline frames the I226 sent", base.seen, base.attempted);
            verdict("PASS", "OBSERVER", index, id->device_id, reason);
        }
    }
    if (stop == NULL) {
        c.h = h;
        c.index = index;
        c.id = id;
        c.row = row;
        c.q0 = &q0;
        c.s0 = &s0;
        g_pending_did = id->device_id;
        InterlockedExchange(&g_pending_adapter, (LONG)index);   /* an accepted request commits a schedule: the cleanup owes a cycle */
        for (k = 0; k < INV_END_EQ_CYCLE; k++) {
            if (!run_inv_case(&c, (INV_ID)k)) {
                stop = g_inv[k].name;
                k++;
                break;
            }
        }
    }
    for (; k < INV_END_EQ_CYCLE; k++) {   /* only after a stop: every case not submitted gets its own BLOCKED line */
        if (c.h != NULL) {
            sprintf_s(reason, sizeof(reason), "not submitted: after case %s a refusal or the measured clean pre-state was not established", stop);
        } else {
            sprintf_s(reason, sizeof(reason), "not submitted: %s", stop);
        }
        inv_blocked(g_inv[k].name, index, id->device_id, reason);
    }

    /* TX after the field cases, before END_EQ_CYCLE, which can commit a schedule */
    if (stop != NULL) {
        sprintf_s(reason, sizeof(reason), "the field cases were not all submitted and refused in the clean pre-state (%s)", stop);
        verdict("BLOCKED", "TX", index, id->device_id, reason);
    } else if (!wire) {
        verdict("BLOCKED", "TX", index, id->device_id, "no TX socket, or the I210 capture could not be started");
    } else {
        send_window(sock, nonce, h, &q0.q.regs, &probe);
        print_window("after the field cases", &probe);
        inv_tx_verdict("TX", index, id->device_id, &base, &probe);
    }

    /* END_EQ_CYCLE (TAS-DR-06) last; accepted, it is the RED of this case and stops it: no TX window after a schedule */
    if (stop != NULL) {
        sprintf_s(reason, sizeof(reason), "not submitted: %s", stop);
        inv_blocked(g_inv[INV_END_EQ_CYCLE].name, index, id->device_id, reason);
        verdict("BLOCKED", "TX-END_EQ_CYCLE", index, id->device_id, reason);
    } else if (!run_inv_case(&c, INV_END_EQ_CYCLE)) {
        verdict("BLOCKED", "TX-END_EQ_CYCLE", index, id->device_id, "case stopped: the request was not refused or the state changed or "
                "was not measured (see its lines); the wire after it is not attributable. EG-06 is not tested");
    } else if (!wire) {
        verdict("BLOCKED", "TX-END_EQ_CYCLE", index, id->device_id, "no TX socket, or the I210 capture could not be started");
    } else {
        send_window(sock, nonce, h, &q0.q.regs, &probe);
        print_window("after END_EQ_CYCLE", &probe);
        inv_tx_verdict("TX-END_EQ_CYCLE", index, id->device_id, &base, &probe);
    }

    if (wire_started) { wire_stop(); }   /* also after 0 observers: the ICMP worker may run (PR #451 review) */
    if (sock != INVALID_SOCKET) { closesocket(sock); }
    if (c.hw != NULL && c.hw != INVALID_HANDLE_VALUE) { CloseHandle(c.hw); }
    cleanup_and_verify(h, index, id, inst, ipv4, &q0, &p0);
}

/* --restore, TC-TASAPI-012 (#377). The restoration set of DES-C-TAS-010 12.5: the v1 register set without DTXMXPKTSZ, BASET_L
 * and BASET_H included. Registers that differ between two replies */
static unsigned restoration_set_differing(const AVB_TAS_REGS *a, const AVB_TAS_REGS *b)
{
    unsigned n = 0;
    int i;

    n += a->tqavctrl != b->tqavctrl;
    n += a->baset_l != b->baset_l;
    n += a->baset_h != b->baset_h;
    n += a->qbvcyclet != b->qbvcyclet;
    n += a->qbvcyclet_s != b->qbvcyclet_s;
    for (i = 0; i < 4; i++) {
        n += a->stqt[i] != b->stqt[i];
        n += a->endqt[i] != b->endqt[i];
        n += a->txqctl[i] != b->txqctl[i];
    }
    return n;
}

static const char *restoration_name(avb_u32 v)
{
    switch (v) {
    case AVB_TAS_RESTORE_COMPLETE:         return "COMPLETE";
    case AVB_TAS_RESTORE_PARTIAL_OPEN_ALL: return "PARTIAL_OPEN_ALL";
    case AVB_TAS_RESTORE_PARTIAL_RESIDUE:  return "PARTIAL_RESIDUE";
    case AVB_TAS_RESTORE_NONE:             return "NONE";
    case AVB_TAS_RESTORE_UNKNOWN:          return "UNKNOWN";
    case AVB_TAS_RESTORE_NO_SSOT_BASELINE: return "NO_SSOT_BASELINE";
    default:                               return "not reported";
    }
}

/* read_failed bits 0 to 16 of AVB_TAS_REGS (include/avb_ioctl.h): the restoration set of DES-C-TAS-010 12.5. DTXMXPKTSZ is
 * bit 17 and not a restoration input; the PHC bracket is not an input either (PR #454 review) */
#define TAS_RESTORATION_SET_BITS ((1u << 17) - 1u)

/** The reply observed every restoration-set register: hw_read READ, or PARTIAL because only DTXMXPKTSZ or a PHC read failed */
static BOOL restoration_set_valid(const QUERY_READ *r)
{
    return query_valid(r) && (r->q.hw_read == AVB_TAS_HWREAD_READ || r->q.hw_read == AVB_TAS_HWREAD_PARTIAL) &&
           (r->q.regs.read_failed & TAS_RESTORATION_SET_BITS) == 0;
}


/** --restore, TC-TASAPI-012 (#377, Verifies #361): reported restoration against the test's own observations, PHC separately */
static void run_restore(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, IN_ADDR ipv4,
                        const MIB_IF_ROW2 *row)
{
    QUERY_READ q0, q1, qa;
    STATS_READ s0;
    STATE_READ t0;
    PHC_READ p0, pc, pb, pa;
    AVB_TAS_ACTIVATE_REQUEST req;
    AVB_VERDICT v;
    TAS_TX_WINDOW base;
    SOCKET sock = INVALID_SOCKET;
    HANDLE hw = INVALID_HANDLE_VALUE, hn = INVALID_HANDLE_VALUE;
    DWORD br = 0, err = ERROR_SUCCESS;
    unsigned nonce = 0;
    BOOL ok = FALSE, wire = FALSE, wire_started = FALSE, committed = FALSE, observed = FALSE;
    const char *stop = NULL;
    const avb_u16 did = id->device_id;
    char reason[400];

    ZeroMemory(&q1, sizeof(q1));
    ZeroMemory(&qa, sizeof(qa));
    ZeroMemory(&base, sizeof(base));
    ZeroMemory(&req, sizeof(req));
    ZeroMemory(&pc, sizeof(pc));
    ZeroMemory(&pb, sizeof(pb));
    ZeroMemory(&pa, sizeof(pa));
    read_phc(h, &p0);
    read_stats(h, &s0);
    read_state(h, &t0);
    query(h, &q0);
    print_query("pre-test", &q0);
    printf("    [EVIDENCE] pre-test reported restoration=%s (%u), advisory\n", restoration_name(q0.q.restoration), q0.q.restoration);

    /* PRESTATE: the test proves its own pre-state through the TAS query before it activates anything (#377) */
    v = avb_verdict_from_status((NTSTATUS)q0.status, AVB_CAP_PRESENT, query_valid(&q0), TRUE);
    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "pre-test query ntstatus=0x%08lX information=%lu%s", (unsigned long)q0.status, q0.got,
                  q0.status == (LONG)STATUS_INVALID_DEVICE_REQUEST ? " (the driver does not serve IOCTL_AVB_TAS_QUERY_HW)" : "");
        verdict(avb_verdict_tag(v), "PRESTATE", index, did, reason);
        stop = "no valid pre-test query";
    } else if (!query_clean(&q0) || !t0.ok || t0.s.armed != AVB_TAS_STATE_CLEAN || !s0.ok || !p0.ok) {
        sprintf_s(reason, sizeof(reason), "pre-state not established: clean query %d, TAS record %s, statistics %s, PHC %s",
                  query_clean(&q0), t0.ok ? (t0.s.armed == AVB_TAS_STATE_CLEAN ? "CLEAN" : "not clean") : "unavailable",
                  s0.ok ? "read" : "unavailable", p0.ok ? "read" : "unavailable");
        verdict("BLOCKED", "PRESTATE", index, did, reason);
        stop = "the clean pre-state was not established";
    } else {
        verdict("PASS", "PRESTATE", index, did, "clean, readable query; TAS record CLEAN; statistics and PHC read");
    }

    /* OBSERVER (#377 "I210 observer alive"): a started capture that received the baseline frames the I226 sent */
    if (stop == NULL) {
        sock = open_tx_socket(ipv4);
        nonce = GetTickCount() ^ GetCurrentProcessId();
        if (sock != INVALID_SOCKET) {
            wire_started = TRUE;   /* wire_start can start its ICMP worker even when it returns 0 observers: stop it in every case */
            wire = wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) != 0;
        }
        if (wire) {
            send_window(sock, nonce, h, NULL, &base);
            print_window("before the activation", &base);
        }
        if (!wire) {
            verdict("BLOCKED", "OBSERVER", index, did, "no TX socket, or the I210 capture did not start: the receiving wire observer is "
                    "not established (#377 precondition)");
            stop = "the receiving wire observer was not established";
        } else if (!tas_tx_on_wire(&base, TAS_ACT_SEEN_MIN_PERCENT)) {
            sprintf_s(reason, sizeof(reason), "baseline window: the I210 received %u of %u frames the I226 sent, %u send errors; the "
                      "receiving wire observer is not proven alive (#377 precondition)", base.seen, base.attempted, base.errors);
            verdict("BLOCKED", "OBSERVER", index, did, reason);
            stop = "the receiving wire observer did not receive the baseline window";
        } else {
            sprintf_s(reason, sizeof(reason), "the I210 received %u of %u baseline frames the I226 sent", base.seen, base.attempted);
            verdict("PASS", "OBSERVER", index, did, reason);
        }
    }
    if (wire_started) { wire_stop(); }   /* the cleanup starts its own capture */
    if (sock != INVALID_SOCKET) { closesocket(sock); }

    /* The non-restored state: IOCTL_AVB_TAS_ACTIVATE HOLD with the verified set, BaseT from a fresh PHC read */
    if (stop == NULL) {
        read_phc(h, &pc);
        if (!pc.ok) {
            stop = "PHC unavailable: no BaseT can be placed";
        } else {
            inv_request(&req, row, pc.phc);
            hw = open_device(GENERIC_READ | GENERIC_WRITE);   /* TAS_ACTIVATE is FILE_WRITE_ACCESS (#362) */
            if (hw == INVALID_HANDLE_VALUE || !bind_luid(hw, id->net_luid)) {
                if (hw != INVALID_HANDLE_VALUE) { CloseHandle(hw); }
                stop = "cannot bind a handle with write access for the activation";
            } else {
                req.result = 0xFFFFFFFFu;   /* values the driver never writes: an unwritten reply cannot read as COMMITTED (0) */
                req.status = 0xFFFFFFFFu;
                g_pending_did = did;
                InterlockedExchange(&g_pending_adapter, (LONG)index);   /* from here the cleanup owes a NIC cycle */
                ok = DeviceIoControl(hw, IOCTL_AVB_TAS_ACTIVATE, &req, sizeof(req), &req, sizeof(req), &br, NULL);
                if (!ok) { err = GetLastError(); }
                CloseHandle(hw);
                printf("    [EVIDENCE] activation: base=%llu (PHC %llu) cycle=%u window=[%u, %u) ok=%d win32=%lu bytes=%lu result=0x%X "
                       "status=0x%08X phcAtCommit=%llu\n", req.base_time_ns, pc.phc, req.cycle_ns, req.window_start_ns[0],
                       req.window_end_ns[0], ok, err, br, req.result, req.status, req.phc_at_commit_ns);
                committed = ok && br == sizeof(req) && req.status == 0u && req.result == AVB_TAS_ACT_COMMITTED &&
                            req.phc_at_commit_ns != 0 && req.phc_at_commit_ns < req.base_time_ns;
                if (!committed) {
                    stop = "the activation was not established as COMMITTED; no non-restored state was created";
                }
            }
        }
    }

    /* RESIDUE: an observed non-restored state (TSN on) never reports COMPLETE (#361 AC2) */
    if (stop != NULL) {
        sprintf_s(reason, sizeof(reason), "not observed: %s", stop);
        verdict("BLOCKED", "RESIDUE", index, did, reason);
    } else {
        query(h, &q1);
        print_query("committed", &q1);
        observed = restoration_set_valid(&q1) && q1.q.hw_mode == AVB_TAS_HWMODE_TSN;
        if (query_valid(&q1) && q1.q.restoration == AVB_TAS_RESTORE_COMPLETE) {
            sprintf_s(reason, sizeof(reason), "COMPLETE reported with a committed schedule (restoration set valid %d, hw_mode %u, %u "
                      "restoration-set registers differ from the pre-test capture)", restoration_set_valid(&q1), q1.q.hw_mode,
                      restoration_set_differing(&q0.q.regs, &q1.q.regs));
            verdict("FAIL", "RESIDUE", index, did, reason);
        } else if (observed) {
            sprintf_s(reason, sizeof(reason), "TSN on observed (%u restoration-set registers differ from the pre-test capture); reported "
                      "restoration=%s (%u), not COMPLETE", restoration_set_differing(&q0.q.regs, &q1.q.regs),
                      restoration_name(q1.q.restoration), q1.q.restoration);
            verdict("PASS", "RESIDUE", index, did, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "the committed state was not observed as non-restored: query ntstatus=0x%08lX restoration "
                      "set valid %d hw_mode %u", (unsigned long)q1.status, restoration_set_valid(&q1), q1.q.hw_mode);
            verdict("BLOCKED", "RESIDUE", index, did, reason);
        }
    }

    /* CLEANUP and RESTORE: the owned NIC cycle and the test's own restoration verdict against its pre-test capture */
    read_phc(h, &pb);
    cleanup_and_verify(h, index, id, inst, ipv4, &q0, pb.ok ? &pb : &p0);

    /* After the cycle: a new handle, the PHC and the reported restoration, each read once */
    hn = open_bound_read(id->net_luid);
    if (hn != INVALID_HANDLE_VALUE) {
        read_phc(hn, &pa);
        query(hn, &qa);
        CloseHandle(hn);
        print_query("after the cycle, REPORTED", &qa);
    }

    /* REPORTED: the advisory value judged only by generic relations of the contract (DES 12.5: a function of the restoration
     * set), never by model or family (testing contract A). No baseline constant is applied here (PR #454 thread 4599329980) */
    if (!committed) {
        sprintf_s(reason, sizeof(reason), "the #377 scenario did not run: %s", stop != NULL ? stop : "no committed activation");
        verdict("BLOCKED", "REPORTED", index, did, reason);
    } else if (hn == INVALID_HANDLE_VALUE || !query_valid(&qa)) {
        sprintf_s(reason, sizeof(reason), "no valid query after the cycle (handle %s, ntstatus=0x%08lX information=%lu)",
                  hn == INVALID_HANDLE_VALUE ? "not bound" : "bound", (unsigned long)qa.status, qa.got);
        verdict("BLOCKED", "REPORTED", index, did, reason);
    } else {
        BOOL valid = restoration_set_valid(&qa);
        BOOL pre_valid = restoration_set_valid(&q0);   /* PRESTATE established it clean, readable and COMPLETE */
        unsigned diff = (valid && pre_valid) ? restoration_set_differing(&q0.q.regs, &qa.q.regs) : 0;
        BOOL same = valid && pre_valid && diff == 0;

        printf("    [EVIDENCE] after the cycle: reported restoration=%s (%u), pre-test %s (%u); hw_read %u, read_failed 0x%X (restoration "
               "set valid %d), hw_mode %u, restoration-set registers differing from the pre-test observation %u\n",
               restoration_name(qa.q.restoration), qa.q.restoration, restoration_name(q0.q.restoration), q0.q.restoration,
               qa.q.hw_read, qa.q.regs.read_failed, valid, qa.q.hw_mode, diff);
        if (qa.q.restoration == AVB_TAS_RESTORE_COMPLETE && (!valid || qa.q.hw_mode != AVB_TAS_HWMODE_OFF)) {
            sprintf_s(reason, sizeof(reason), "COMPLETE reported although the reply's restoration set is not valid (%d) or TSN is not off "
                      "(hw_mode %u); COMPLETE needs both (DES 12.5)", valid, qa.q.hw_mode);
            verdict("FAIL", "REPORTED", index, did, reason);
        } else if (valid && qa.q.hw_mode == AVB_TAS_HWMODE_TSN) {
            sprintf_s(reason, sizeof(reason), "TSN on after the cycle and not reported COMPLETE (%s); see RESTORE",
                      restoration_name(qa.q.restoration));
            verdict("PASS", "REPORTED", index, did, reason);
        } else if (same && qa.q.restoration != q0.q.restoration) {
            sprintf_s(reason, sizeof(reason), "the restoration set equals the test's own pre-test observation, yet reported %s (%u) where "
                      "the pre-test reply reported %s (%u); the advisory is a function of that set (DES 12.5)",
                      restoration_name(qa.q.restoration), qa.q.restoration, restoration_name(q0.q.restoration), q0.q.restoration);
            verdict("FAIL", "REPORTED", index, did, reason);
        } else if (same) {
            sprintf_s(reason, sizeof(reason), "reported %s, as for the identical clean pre-test observation; TSN off, restoration set "
                      "valid", restoration_name(qa.q.restoration));
            verdict("PASS", "REPORTED", index, did, reason);
        } else if (!valid) {
            sprintf_s(reason, sizeof(reason), "restoration set not observed after the cycle (hw_read %u, read_failed 0x%X): agreement "
                      "not measurable; reported %s", qa.q.hw_read, qa.q.regs.read_failed, restoration_name(qa.q.restoration));
            verdict("BLOCKED", "REPORTED", index, did, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "TSN off, but %u restoration-set registers differ from the clean pre-test observation; the "
                      "query exposes neither the baseline values nor per-register agreement, so whether %s is right cannot be decided "
                      "through the generic interface (contract gap)", diff, restoration_name(qa.q.restoration));
            verdict("INCONCLUSIVE", "REPORTED", index, did, reason);
        }
    }

    /* PHC: measured and reported separately, never a restoration verdict, no tolerance assumed */
    {
        char b1[32], b2[32];
        long long wall_ns = 0;

        fmt_u64(b1, sizeof(b1), pb.ok, pb.phc);
        fmt_u64(b2, sizeof(b2), pa.ok, pa.phc);
        if (pb.ok && pa.ok) {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            wall_ns = (long long)((double)(pa.qpc.QuadPart - pb.qpc.QuadPart) * 1e9 / (double)freq.QuadPart);
        }
        if (!committed) {
            sprintf_s(reason, sizeof(reason), "the #377 scenario did not run (before=%s after=%s); not a restoration verdict", b1, b2);
            verdict("INCONCLUSIVE", "PHC", index, did, reason);
        } else if (!pb.ok || !pa.ok) {
            sprintf_s(reason, sizeof(reason), "PHC not read before or after the cycle (before=%s after=%s); not a restoration verdict", b1, b2);
            verdict("INCONCLUSIVE", "PHC", index, did, reason);
        } else if (pa.phc < pb.phc) {
            sprintf_s(reason, sizeof(reason), "discontinuity measured and reported separately: the PHC stepped back across the cycle "
                      "(before=%s after=%s wallNs=%lld); not a restoration verdict", b1, b2, wall_ns);
            verdict("PASS", "PHC", index, did, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "no backward step (before=%s after=%s wallNs=%lld): without an approved tolerance neither "
                      "continuity nor discontinuity is established; not a restoration verdict", b1, b2, wall_ns);
            verdict("INCONCLUSIVE", "PHC", index, did, reason);
        }
    }
    printf("    [COVERAGE] not exercised by this run: TSN-off residue (PARTIAL_RESIDUE), PARTIAL_OPEN_ALL, unreadable registers "
           "(UNKNOWN), NO_SSOT_BASELINE, a NIC cycle that leaves residue, PHC continuity, and every other failure path. Not "
           "decidable through the generic query: a restoration set that differs from the pre-test observation (e.g. ENDQT at _ALT)\n");
}

/* ---- --link-speed (TC-TASAPI-021, #472): the driver's link speed against the OS observation of the same interface ---- */

/** One bracketed observation: OS reading, query, OS reading, retried while the two OS readings differ */
typedef struct {
    BOOL os_agreed;          /* both OS readings succeeded and report the same state and transmit rate */
    DWORD err1, err2;        /* raw GetIfEntry2 return codes of the last try */
    MIB_IF_ROW2 os;          /* the agreeing OS reading (valid only when os_agreed) */
    QUERY_READ q;            /* the query of the last try */
    unsigned tries;
} LINK_OBS;

static double qpc_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}

static const char *media_name(NET_IF_MEDIA_CONNECT_STATE s)
{
    return s == MediaConnectStateConnected ? "connected" : s == MediaConnectStateDisconnected ? "disconnected" : "unknown";
}

static DWORD read_if_raw(avb_u64 luid, MIB_IF_ROW2 *row)
{
    ZeroMemory(row, sizeof(*row));
    row->InterfaceLuid.Value = luid;   /* the validated target NET_LUID (#472) */
    return GetIfEntry2(row);
}

static void link_observe(HANDLE h, avb_u64 luid, const char *label, LINK_OBS *o)
{
    MIB_IF_ROW2 a, b;
    double t0, t1, t2;

    ZeroMemory(o, sizeof(*o));
    while (o->tries < LINK_BRACKET_TRIES) {
        o->tries++;
        t0 = qpc_ms();
        o->err1 = read_if_raw(luid, &a);
        t1 = qpc_ms();
        query(h, &o->q);
        t2 = qpc_ms();
        o->err2 = read_if_raw(luid, &b);
        printf("    [EVIDENCE] %s try %u: os1 err=%lu state=%s tx=%llu rx=%llu bit/s @%.1f ms | query ntstatus=0x%08lX information=%lu "
               "link_speed_mbps=", label, o->tries, o->err1, media_name(a.MediaConnectState), a.TransmitLinkSpeed, a.ReceiveLinkSpeed,
               t0, (unsigned long)o->q.status, o->q.got);
        if (query_valid(&o->q)) {
            printf("%u", o->q.q.link_speed_mbps);
        } else {
            printf("(no v1 reply)");
        }
        printf(" @%.1f ms | os2 err=%lu state=%s tx=%llu rx=%llu bit/s @%.1f ms\n", t1, o->err2, media_name(b.MediaConnectState),
               b.TransmitLinkSpeed, b.ReceiveLinkSpeed, t2);
        if (o->err1 == NO_ERROR && o->err2 == NO_ERROR && a.MediaConnectState == b.MediaConnectState &&
            a.TransmitLinkSpeed == b.TransmitLinkSpeed) {
            o->os_agreed = TRUE;
            o->os = a;
            return;
        }
        Sleep(LINK_BRACKET_RETRY_MS);
    }
}

/** The verdict of one observation, with its reason. The query is judged first by its raw NTSTATUS (avb_verdict.h); the
 *  comparison is made only on a valid reply inside an agreeing OS bracket */
static AVB_VERDICT link_judge(const LINK_OBS *o, char *reason, size_t size)
{
    AVB_VERDICT v = avb_verdict_from_status((NTSTATUS)o->q.status, AVB_CAP_PRESENT, query_valid(&o->q), TRUE);
    unsigned long long bps, expected;

    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, size, "query ntstatus=0x%08lX information=%lu%s", (unsigned long)o->q.status, o->q.got,
                  o->q.status == 0 ? " (success without the v1 reply)" : "");
        return v;
    }
    if (o->q.q.net_luid != o->os.InterfaceLuid.Value && o->os_agreed) {
        sprintf_s(reason, size, "the reply names NET_LUID 0x%016llX, not the observed interface", o->q.q.net_luid);
        return AVB_VERDICT_FAIL;
    }
    if (!o->os_agreed) {
        sprintf_s(reason, size, "no agreeing OS bracket in %u tries (GetIfEntry2 %lu/%lu, or the state or rate changed across each "
                  "bracket): not compared; a measurement gap, not agreement", o->tries, o->err1, o->err2);
        return AVB_VERDICT_INCONCLUSIVE;
    }
    if (o->os.MediaConnectState == MediaConnectStateDisconnected) {
        if (o->q.q.link_speed_mbps == 0) {
            sprintf_s(reason, size, "OS disconnected and link_speed_mbps 0 (the disconnected case; a known positive rate is not "
                      "verified by it)");
            return AVB_VERDICT_PASS;
        }
        sprintf_s(reason, size, "OS disconnected, link_speed_mbps %u (expected 0)", o->q.q.link_speed_mbps);
        return AVB_VERDICT_FAIL;
    }
    if (o->os.MediaConnectState != MediaConnectStateConnected) {
        sprintf_s(reason, size, "OS connection state unknown (link_speed_mbps %u): no reference to compare with", o->q.q.link_speed_mbps);
        return AVB_VERDICT_INCONCLUSIVE;
    }
    bps = o->os.TransmitLinkSpeed;
    if (bps == 0 || bps == ~0ULL) {
        sprintf_s(reason, size, "OS connected with an unknown rate (TransmitLinkSpeed %llu, link_speed_mbps %u): no measured "
                  "connected-speed control", bps, o->q.q.link_speed_mbps);
        return AVB_VERDICT_INCONCLUSIVE;
    }
    if (bps % BITS_PER_MBIT != 0 || bps / BITS_PER_MBIT > 0xFFFFFFFFULL) {
        sprintf_s(reason, size, "OS rate %llu bit/s is not representable in integer Mbit/s and the conversion is not specified "
                  "(#472); link_speed_mbps %u", bps, o->q.q.link_speed_mbps);
        return AVB_VERDICT_INCONCLUSIVE;
    }
    expected = (unsigned long long)o->q.q.link_speed_mbps * BITS_PER_MBIT;
    if (expected == bps) {
        sprintf_s(reason, size, "connected: link_speed_mbps %u x 1,000,000 = TransmitLinkSpeed %llu", o->q.q.link_speed_mbps, bps);
        return AVB_VERDICT_PASS;
    }
    sprintf_s(reason, size, "connected: link_speed_mbps %u (x 1,000,000 = %llu) differs from TransmitLinkSpeed %llu bit/s",
              o->q.q.link_speed_mbps, expected, bps);
    return AVB_VERDICT_FAIL;
}

/** One GET_CLOCK_CONFIG read with its raw status: values count only when the call, the size and the reply status are valid */
typedef struct { BOOL ok; LONG status; DWORD got; AVB_CLOCK_CONFIG c; } CLOCK_READ;

static void read_clock(HANDLE h, CLOCK_READ *r)
{
    ZeroMemory(r, sizeof(*r));
    r->status = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_CLOCK_CONFIG, &r->c, sizeof(r->c), &r->c, sizeof(r->c), &r->got);
    r->ok = r->status == 0 && r->got == sizeof(r->c) && r->c.status == 0;
}

static void print_clock(const char *label, const CLOCK_READ *r)
{
    printf("    [EVIDENCE] clock %s: ntstatus=0x%08lX information=%lu", label, (unsigned long)r->status, r->got);
    if (r->ok) {
        printf(" status=0x%08X valid TIMINCA=0x%08X TSAUXC=0x%08X\n", r->c.status, r->c.timinca, r->c.tsauxc);
    } else {
        printf(" status=0x%08X NOT VALID (values not reported)\n", r->c.status);
    }
}

/** Cleanup-only report of the clock configuration across the test (runner convention [CLEANUP:...], Run-Tests-CI.ps1): read-only,
 *  never a functional verdict, never a write-back. Neither the incoming nor the post-cycle values are an established
 *  restoration target, so equality is reported as observed, not as a restored target */
static void link_clock_cleanup(UINT32 index, avb_u16 did, const CLOCK_READ *before, const CLOCK_READ *after, BOOL after_taken)
{
    if (!before->ok || !after_taken || !after->ok) {
        printf("  [CLEANUP:NOT_VERIFIED] %s-CLOCK/adapter:%u did=0x%04X: TIMINCA/TSAUXC not measured %s (before: ntstatus=0x%08lX "
               "information=%lu status=0x%08X; after: %s ntstatus=0x%08lX information=%lu status=0x%08X); cleanup only, the functional "
               "verdicts stand\n", CASE_ID, index, did, !before->ok ? "before the first NIC operation" : "after cleanup",
               (unsigned long)before->status, before->got, before->c.status, after_taken ? "read" : "not taken",
               (unsigned long)after->status, after->got, after->c.status);
    } else if (before->c.timinca != after->c.timinca || before->c.tsauxc != after->c.tsauxc) {
        printf("  [CLEANUP:FAILED] %s-CLOCK/adapter:%u did=0x%04X: clock configuration differs from the state found: TIMINCA 0x%08X -> "
               "0x%08X, TSAUXC 0x%08X -> 0x%08X; read-only, no write-back (neither value is an established restoration target); "
               "cleanup only, the functional verdicts stand\n", CASE_ID, index, did, before->c.timinca, after->c.timinca,
               before->c.tsauxc, after->c.tsauxc);
    } else {
        printf("  [CLEANUP:SUCCESS] %s-CLOCK/adapter:%u did=0x%04X: TIMINCA 0x%08X and TSAUXC 0x%08X read equal before the first NIC "
               "operation and after cleanup\n", CASE_ID, index, did, after->c.timinca, after->c.tsauxc);
    }
    fflush(stdout);
}

static void run_link_speed(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst)
{
    CLOCK_READ ck0, ck1;
    LINK_OBS pre, att, s1, s2, fin;
    STATS_READ st_att, st_fin;
    STATE_READ tas;
    MIB_IF_ROW2 pre_os;
    DWORD pre_err, start;
    HANDLE h;
    BOOL cycled, returned, pre_connected;
    AVB_VERDICT v1, v2;
    char reason[400], r2[400];

    ZeroMemory(&st_att, sizeof(st_att));
    ZeroMemory(&st_fin, sizeof(st_fin));
    ZeroMemory(&tas, sizeof(tas));
    ZeroMemory(&ck0, sizeof(ck0));
    ZeroMemory(&ck1, sizeof(ck1));
    ck0.status = ck1.status = (LONG)AVB_NTSTATUS_UNAVAILABLE;   /* "not read" until a read happens, never a fake success */

    /* PRESTATE: evidence and FINAL's link reference; nothing is judged here */
    pre_err = read_if_raw(id->net_luid, &pre_os);
    pre_connected = pre_err == NO_ERROR && pre_os.MediaConnectState == MediaConnectStateConnected;
    printf("    [EVIDENCE] PRESTATE OS: err=%lu state=%s tx=%llu rx=%llu bit/s oper=%d\n", pre_err, media_name(pre_os.MediaConnectState),
           pre_os.TransmitLinkSpeed, pre_os.ReceiveLinkSpeed, (int)pre_os.OperStatus);
    h = open_bound_read(id->net_luid);
    if (h != INVALID_HANDLE_VALUE) {
        link_observe(h, id->net_luid, "PRESTATE", &pre);
        print_query("PRESTATE", &pre.q);
        /* The incoming TAS state as found, origin not known to the test: recorded, never judged or assumed clean */
        if (query_valid(&pre.q)) {
            printf("    [EVIDENCE] incoming TAS state (origin unknown): %s; hw_mode=%u current=%u/0x%X next=%u/0x%X restoration=%u "
                   "commit=%u/%u\n", query_clean(&pre.q) ? "clean" : "NOT clean", pre.q.q.hw_mode, pre.q.q.current_status,
                   pre.q.q.current_reasons, pre.q.q.next_status, pre.q.q.next_reasons, pre.q.q.restoration,
                   pre.q.q.current_commit.state, pre.q.q.next_commit.state);
        }
        read_clock(h, &ck0);   /* before the first NIC operation */
        print_clock("before the first NIC operation", &ck0);
        CloseHandle(h);   /* an open bound handle must not hold the adapter context across the detach */
    } else {
        printf("    [EVIDENCE] PRESTATE query: no read-only handle bound by NET_LUID\n");
    }

    /* CYCLE: the test's own NIC cycle, the attach that ATTACH needs */
    printf("\n  --- Owned NIC cycle (#472 ATTACH setup) ---\n");
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);
    cycled = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
    if (cycled) {
        Sleep(3000);
        cycled = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
    } else {
        printf("    enable not attempted: the disable did not complete and may still be pending in Windows\n");
    }
    h = cycled ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
    returned = h != INVALID_HANDLE_VALUE;
    InterlockedExchange(&g_pending_adapter, -1);
    if (!cycled || !returned) {
        verdict("CLEANUP_FAILED", "CYCLE", index, id->device_id,
                "the NIC cycle did not complete or the adapter did not return with the same NET_LUID, device ID and MAC; a timed-out "
                "disable may still be pending in Windows. Operator: NIC cycle once it ends, then check it with the TAS query");
        verdict("BLOCKED", "ATTACH", index, id->device_id, "requires the test's own NIC cycle and the adapter's return");
        verdict("BLOCKED", "STEADY", index, id->device_id, "requires the test's own NIC cycle and the adapter's return");
        verdict("INCONCLUSIVE", "EVENT", index, id->device_id, "not exercised: no owned link transition without a detach (see the header)");
        verdict("BLOCKED", "FINAL", index, id->device_id, "the adapter did not return; its final state is not established");
        link_clock_cleanup(index, id->device_id, &ck0, &ck1, FALSE);
        return;
    }
    verdict("PASS", "CYCLE", index, id->device_id, "disabled, enabled, returned with the same NET_LUID, device ID and MAC");

    /* ATTACH: a pre-test connected link must be reported connected again first; a disconnected one is compared as found */
    if (pre_connected) {
        MIB_IF_ROW2 row;
        ZeroMemory(&row, sizeof(row));
        start = GetTickCount();
        while (GetTickCount() - start < LINK_BOUND_MS) {
            if (read_if_raw(id->net_luid, &row) == NO_ERROR && row.MediaConnectState == MediaConnectStateConnected) {
                break;
            }
            Sleep(500);
        }
        printf("    OS link after the cycle: %s after %lu ms (bound %d ms)\n", media_name(row.MediaConnectState), GetTickCount() - start,
               LINK_BOUND_MS);
    }
    link_observe(h, id->net_luid, "ATTACH", &att);
    print_query("ATTACH", &att.q);
    read_stats(h, &st_att);
    v1 = link_judge(&att, reason, sizeof(reason));
    verdict(avb_verdict_tag(v1), "ATTACH", index, id->device_id, reason);

    /* STEADY: two observations LINK_STEADY_INTERVAL_MS apart, each judged against its own bracket */
    link_observe(h, id->net_luid, "STEADY-1", &s1);
    Sleep(LINK_STEADY_INTERVAL_MS);
    link_observe(h, id->net_luid, "STEADY-2", &s2);
    if (s1.os_agreed && s2.os_agreed && (s1.os.MediaConnectState != s2.os.MediaConnectState ||
                                         s1.os.TransmitLinkSpeed != s2.os.TransmitLinkSpeed)) {
        printf("    [EVIDENCE] measured link event between the STEADY observations: %s %llu -> %s %llu bit/s\n",
               media_name(s1.os.MediaConnectState), s1.os.TransmitLinkSpeed, media_name(s2.os.MediaConnectState), s2.os.TransmitLinkSpeed);
    }
    v1 = link_judge(&s1, reason, sizeof(reason));
    v2 = link_judge(&s2, r2, sizeof(r2));
    if (v1 == AVB_VERDICT_PASS && v2 == AVB_VERDICT_PASS) {
        verdict("PASS", "STEADY", index, id->device_id, "both observations agree with their OS brackets");
    } else {
        char both[820];
        AVB_VERDICT v = (v1 == AVB_VERDICT_FAIL || v2 == AVB_VERDICT_FAIL) ? AVB_VERDICT_FAIL :
                        (v1 != AVB_VERDICT_PASS ? v1 : v2);
        sprintf_s(both, sizeof(both), "first: %s %s; second: %s %s", avb_verdict_tag(v1), reason, avb_verdict_tag(v2), r2);
        verdict(avb_verdict_tag(v), "STEADY", index, id->device_id, both);
    }

    /* EVENT: required by #472 but not exercised by this test; never a pass by omission */
    verdict("INCONCLUSIVE", "EVENT", index, id->device_id,
            "not exercised: the test has no owned procedure that changes the link without detaching the filter and restores it "
            "(a cable or link-partner change depends on topology it cannot establish, #463); ATTACH is not EVENT coverage");

    /* FINAL: bound, link as before the test, TAS clean, no fault; a difference is a cleanup defect, not a functional verdict */
    link_observe(h, id->net_luid, "FINAL", &fin);
    print_query("FINAL", &fin.q);
    read_state(h, &tas);
    read_stats(h, &st_fin);
    read_clock(h, &ck1);   /* after cleanup: the test's last operation on the adapter */
    print_clock("after cleanup", &ck1);
    CloseHandle(h);
    if (pre_err != NO_ERROR || !fin.os_agreed || !query_valid(&fin.q) || !tas.ok || !st_att.ok || !st_fin.ok) {
        sprintf_s(reason, sizeof(reason), "not measured: pre-test OS err=%lu, final OS bracket %s, final query %s, TAS record %s, "
                  "statistics %s/%s", pre_err, fin.os_agreed ? "agreed" : "unresolved", query_valid(&fin.q) ? "valid" : "invalid",
                  tas.ok ? "read" : "unavailable", st_att.ok ? "read" : "unavailable", st_fin.ok ? "read" : "unavailable");
        verdict("INCONCLUSIVE_RESTORATION", "FINAL", index, id->device_id, reason);
    } else if (fin.os.MediaConnectState != pre_os.MediaConnectState || fin.os.TransmitLinkSpeed != pre_os.TransmitLinkSpeed ||
               !query_clean(&fin.q) || tas.s.armed != AVB_TAS_STATE_CLEAN || st_att.s.HardwareFaults != st_fin.s.HardwareFaults) {
        sprintf_s(reason, sizeof(reason), "link %s %llu -> %s %llu bit/s, query clean %s, TAS record armed=%u, HardwareFaults %llu -> %llu",
                  media_name(pre_os.MediaConnectState), pre_os.TransmitLinkSpeed, media_name(fin.os.MediaConnectState),
                  fin.os.TransmitLinkSpeed, query_clean(&fin.q) ? "yes" : "no", tas.s.armed, st_att.s.HardwareFaults,
                  st_fin.s.HardwareFaults);
        verdict("CLEANUP_FAILED", "FINAL", index, id->device_id, reason);
    } else {
        verdict("PASS", "FINAL", index, id->device_id,
                "bound, OS link state and rate as before the test, TAS query clean and readable, record CLEAN, no new hardware fault");
    }
    printf("    [EVIDENCE] PHC: the owned NIC cycle reseeds it (E12); reported, not restored\n");
    link_clock_cleanup(index, id->device_id, &ck0, &ck1, TRUE);
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
        verdict("BLOCKED", g_link_mode ? "CYCLE" : "QUERY", index, id->device_id,
                g_link_mode ? "cannot resolve the adapter device node required for the owned NIC cycle"
                            : "cannot resolve the adapter device node required for the owned NIC-cycle cleanup");
        return;
    }
    if (g_link_mode) {   /* #472: no IPv4, observer or link precondition; a disconnected adapter is a valid ATTACH case */
        run_link_speed(index, id, inst);
        return;
    }
    if (!read_if(id->net_luid, &row) || row.OperStatus != IfOperStatusUp || !find_addressing(id->net_luid, &ipv4, &other_link)) {
        verdict("BLOCKED", "QUERY", index, id->device_id, "adapter link is down or it has no IPv4 address");
        return;
    }
    printf("    [EVIDENCE] another adapter up with a gateway: %s (diagnostic only, not a precondition: the owned NIC cycle may interrupt the host network; evidence is kept locally, #463)\n", other_link ? "yes" : "no");
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
    if (g_tiers_mode) {
        run_tiers(h, index, id, inst, ipv4);
        return;
    }
    if (g_invalid_mode) {
        run_invalid(h, index, id, inst, ipv4, &row);
        return;
    }
    if (g_restore_mode) {
        run_restore(h, index, id, inst, ipv4, &row);
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
    AVB_ADAPTER_IDENTITY_REQUEST *ids = NULL, *all = NULL, first;
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i = 0, count = 0, stored = 0, nall = 0;
    BOOL complete = FALSE;
    char reason[200];

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    if (argc == 2 && strcmp(argv[1], "--hw-verified") == 0) {
        g_hw_verified_mode = TRUE;
        g_case = "TC-TASAPI-016";
    } else if (argc == 2 && strcmp(argv[1], "--tiers-unbound") == 0) {
        g_tiers_mode = TRUE;
        g_case = "TC-TASAPI-002";
    } else if (argc == 2 && strcmp(argv[1], "--invalid") == 0) {
        g_invalid_mode = TRUE;
        g_case = "TC-TASAPI-003";
    } else if (argc == 2 && strcmp(argv[1], "--restore") == 0) {
        g_restore_mode = TRUE;
        g_case = "TC-TASAPI-012";
    } else if (argc == 2 && strcmp(argv[1], "--link-speed") == 0) {
        g_link_mode = TRUE;
        g_case = "TC-TASAPI-021";
    } else if (argc != 1) {
        printf("[BLOCKED] %s: usage: test_tas_query_clean [--hw-verified | --tiers-unbound | --invalid | --restore | --link-speed]\n", CASE_ID);
        return 3;
    }
    printf("=== IntelAvbFilter - IOCTL_AVB_TAS_QUERY_HW, %s (%s) ===\n", CASE_ID,
           g_hw_verified_mode ? "#381: HARDWARE_VERIFIED never returned, clean, BaseT ahead and PHC past BaseT" :
           g_tiers_mode ? "#367: unbound handle refused, capability, support tiers" :
           g_invalid_mode ? "#368: invalid TAS_ACTIVATE requests refused, query, record and TX unchanged" :
           g_restore_mode ? "#377: reported restoration against the test's own observations, PHC separately" :
           g_link_mode ? "#472: driver-owned link speed against the OS observation (K8)" : "#366: clean adapter");
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
        all = (AVB_ADAPTER_IDENTITY_REQUEST *)calloc(count, sizeof(*all));
        complete = ids != NULL && all != NULL;
        for (i = 0; complete && i < count; i++) {
            AVB_ADAPTER_IDENTITY_REQUEST id;
            if (!identity_at(h, i, &id) || id.count != count) {
                complete = FALSE;
                break;
            }
            printf("    [EVIDENCE] adapter:%u did=0x%04X luid=0x%016llX caps=0x%08X\n", i, id.device_id, id.net_luid, id.capabilities);
            all[nall++] = id;
            if (g_capture_luid == 0 && id.device_id == INTEL_DEV_I210_AT) {
                g_capture_luid = id.net_luid;
            }
            if (id.capabilities & INTEL_CAP_TSN_TAS) {
                ids[stored++] = id;
            } else if (!g_tiers_mode) {
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
    if (g_tiers_mode) {
        /* #367: the unbound handle first (read-only), then every adapter without the capability (read-only), then the TAS
         * adapters with their owned cleanup below. Operations other than the query are not exercised on non-TAS adapters in
         * this mode: a coverage gap of #367 (network topology is not a reason to omit them, #463) */
        check_unbound(all, nall, complete);
        for (i = 0; i < nall; i++) {
            if (!(all[i].capabilities & INTEL_CAP_TSN_TAS)) {
                check_capability_absent(all[i].index, &all[i]);
            }
        }
        if (stored == 0) {   /* #367 needs its TAS part: unbound and non-TAS results never stand in for it (077ba64) */
            verdict_global("BLOCKED", "QUERY", "no TAS-capable adapter found; required TAS query coverage not executed (#367)");
            verdict_global("BLOCKED", "TIERS", "requires a TAS-capable adapter (#367)");
        }
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
    free(all);
    WSACleanup();

    printf("\nadapters run=%d nonpassing verdicts=%d blocked=%d\n", g_ran, g_failed, g_blocked);
    if (g_ran == 0) { return 2; }
    if (g_failed != 0) { return 1; }
    return g_blocked != 0 ? 3 : 0;
}
