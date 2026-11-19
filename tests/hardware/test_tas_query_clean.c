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
 * --invalid: TEST-TAS-API-003 (#368, Verifies #351), case TC-TASAPI-003; #368 amendment 2026-11-18 (P2 step 5) against #365 13.1-13.6
 * (provisional). Each case is a one-field change of a baseline HOLD AVB_TAS_OP_REQUEST (144 bytes, version 1) on
 * IOCTL_AVB_TAS_ACTIVATE_80C (0x0017A030): the bound adapter's net_luid, a non-zero client_op_id distinct per request, the
 * expected_generation and context_epoch the test's own query reports (0 = not reported until D.1), the verified schedule set with
 * BaseT recomputed per case from a fresh PHC read and a lead of 1.5 s, or limits.min_margin_ns plus 500 ms headroom when the query
 * establishes a larger margin (#365 13.3; a margin beyond the test's 60 s bound is LEAD BLOCKED). The baseline is not claimed valid while D.1 (metadata), D.2 (link speed for
 * TAS-DR-05), D.4 (tier admission) and D.7 (order among semantic checks) are unsettled. Per case, three separate lines, never merged:
 *   <case>-REFUSED     an error completion with no output whose status a #365 13.3 rule names. STATUS_SUCCESS is FAIL (the
 *                      operation may have written). STATUS_INVALID_DEVICE_REQUEST (0x80C not served) is FAIL, never SKIP: the code
 *                      is allocated to the adapter's TAS function (#362, #357).
 *   <case>-UNCHANGED   the query after the case against the test's own pre-test capture: every schedule register equal (BASET_L/H
 *                      reported only, #365 12.4), still clean, the TAS record CLEAN and the PHC, fault and pause/restart counters
 *                      unchanged. Not measured: BLOCKED. A missing-handler status is never taken as proof of UNCHANGED.
 *   <case>-COMPLETION  exactly the NTSTATUS #365 13.3 requires with no output. Header cases (13.3 down to net_luid, in 13.5's
 *                      defined order: UNBOUND, SHORT_BUFFER, SHORT_OUTPUT, VERSION, SIZE, RESERVED, SCHEDULE_RESERVED,
 *                      FLAGS_UNKNOWN, QUEUE_FLAGS_UNKNOWN, FLAGS_OPEN_ALL, CLIENT_OP_ID_ZERO, DEBUG_FAULT_RESERVED,
 *                      DEBUG_FAULT_RELEASE, NET_LUID_MISMATCH) PASS on it. Semantic cases (PRE_ACTIVATION, CYCLE_ZERO, CYCLE_FIELD,
 *                      BASET_SECONDS, BASET_NEAR, FRAME_MIN, WINDOW_EMPTY, WINDOW_PAST_CYCLE, END_EQ_CYCLE): the required status,
 *                      or STATUS_INVALID_DEVICE_STATE the unsettled metadata rule can produce, or STATUS_REQUEST_NOT_ACCEPTED (#365
 *                      13.4 NOT_HARDWARE_VERIFIED, D.4), is INCONCLUSIVE (isolation not established: D.1, D.4, D.7), never PASS;
 *                      any other status is FAIL. UNBOUND: STATUS_INVALID_DEVICE_STATE is INCONCLUSIVE too, because NOT_BOUND and
 *                      ADAPTER_MISMATCH share it and a dispatcher fallback could produce the latter. Passing COMPLETION does not
 *                      establish K2.
 *   <case>-OTHERS      after UNBOUND and NET_LUID_MISMATCH, which name or may reach another adapter: every other TAS adapter's
 *                      query against its own capture before the cases. A change is FAIL and is recovered by that adapter's owned
 *                      NIC cycle (<case>-OTHERS-RECOVERY). Adapters without INTEL_CAP_TSN_TAS have no TAS state the query can
 *                      observe: reported as not measured. NET_LUID_MISMATCH prefers another TAS adapter's NET_LUID for that reason.
 *                      Bounds (budget): at most INV_OTHERS_MAX (8) other TAS adapters observed (any beyond: OTHERS BLOCKED, never PASS)
 *                      and INV_OTHER_RECOVERIES_MAX (1) recovery cycle per adapter under test; a changed adapter not recovered stays in
 *                      the test's OUTSTANDING list, reported at the end of the adapter run and on an abort. An allocation failure is
 *                      OTHERS-SETUP BLOCKED.
 * Not submitted, three BLOCKED lines each, never PASS: STALE_GENERATION, STALE_EPOCH (D.1) and WINDOW_MIN (D.2 consumption).
 * DEBUG_FAULT_RELEASE runs on a Release driver only (Debug: not applicable, the fault path is #357's --fault case).
 * NET_LUID_MISMATCH names another enumerated adapter's NET_LUID when one exists, otherwise an identity no adapter has (the
 * different-existing-adapter subcase is then reported not exercised). UNBOUND sends the valid request on an unbound write handle.
 * A case stops the remaining ones (BLOCKED) unless it was refused, or the code was not served (nothing reached the TAS path), and
 * the state was measured unchanged. COMPLETION never stops.
 * OBSERVER (no fixed observer model, no execution veto, #463): every other enumerated adapter is tried, at most
 * INV_OBSERVER_TRIES, as the passive capture (Npcap, flooded broadcast, tas_wire_observer.h) until one receives the baseline frames
 * the adapter under test sends. Without one, or without an IPv4 address, only the TX lines are BLOCKED; the cases still run.
 * TX: a window after the field cases, with queries interleaved against the pre-test registers; supplementary wire evidence, no
 * gate timing. END_EQ_CYCLE (TAS-DR-06) is the last field case, with its own TX-END_EQ_CYCLE window when refused and unchanged.
 * LEGACY_FUNCTION_71: a separate compatibility phase after the field sequence, also when that sequence could not start, from its
 * own proven-clean baseline (recovered once by the test's own NIC cycle when not proved clean: LEGACY_SETUP; never on a possibly
 * armed adapter), with its own bound write handle and a fresh interface row (a connected link and a rate exactly representable
 * in the legacy Mbit/s field, otherwise BLOCKED). The known-valid 80-byte
 * request on 0x0017811C must complete STATUS_INVALID_DEVICE_REQUEST (function 71 withdrawn, #365 13.1). Today's driver may commit
 * it: the owned cleanup follows. Then CLEANUP and RESTORE as below. Every TAS adapter runs (no TAS_QUERY_CLEAN_MAX_ADAPTERS cap).
 * Expected RED against a driver without the 0x80C producer: the versioned cases REFUSED and COMPLETION FAIL (not served), UNCHANGED
 * judged from the actual readings; LEGACY_FUNCTION_71 FAIL (still served). A hypothesis until a hardware run shows it.
 * Not tested: a duplicate client_op_id (#352 proposal); tier outcomes (#351 table); EG-06; REPLACE (#358).
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
 * A query that fails by itself (nonpassing status verdict, malformed reply, reply for another NET_LUID) ends that observation
 * with its raw NTSTATUS, length and validity: a retry for the OS bracket never replaces an established query failure.
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
 *             no fault. The query requirement is judged field by field under #365 12.2/12.3 validity (reply and target
 *             first; hw_mode from a valid TQAVCTRL; slot statuses and the advisory restoration as reported; every register
 *             valid), so a valid TSN-on observation survives an unrelated register read failure and an invalid field's 0 is
 *             never an observation. Each requirement is judged on its own valid measurement: any observed violation is CLEANUP_FAILED (with
 *             every violation and every missing measurement listed), even when another read is unavailable; only with no
 *             violation does a missing measurement give INCONCLUSIVE_RESTORATION; PASS needs every observation valid and
 *             conforming. Reported apart from the functional checks.
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
 * TAS_QUERY_CLEAN_MAX_ADAPTERS TAS adapters run per process (its runner time budget); any further one is BLOCKED. --link-speed
 * is exempt: it runs every TAS adapter, and its runner budget scales with the enumerated count, uncapped. The SHA256
 * of the on-disk file at the loaded driver module's path is printed as evidence, never used for selection; it is not a hash of
 * the image in memory, whose identity stays unverified (the file can be replaced while the old image remains loaded).
 *
 * IOCTLs sent: GET_VERSION, GET_ADAPTER_IDENTITY, OPEN_ADAPTER_BY_LUID (binds the handle only: no global context switch, no
 * PTP initialization, src/device.c), TAS_QUERY_HW, GET_CLOCK_CONFIG, GET_STATISTICS, GET_TAS_STATE. No READ_REGISTER, no
 * WRITE_REGISTER, no TAS write. Mutations: bounded broadcast UDP and ICMP on the adapter under test, and the owned NIC cycle of
 * that adapter, which resets its PHC. --hw-verified also sends IOCTL_AVB_TAS_ACTIVATE (a TAS schedule) on a clean adapter,
 * always followed by that NIC cycle. --invalid sends invalid IOCTL_AVB_TAS_ACTIVATE_80C requests and, in its legacy phase, the
 * valid 80-byte request on function 71 (which today's driver can commit), always followed by that NIC cycle. --restore commits the verified HOLD schedule, always followed by
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
#include <stdarg.h>
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
#define LINK_RF_TQAVCTRL          0x00000001u  /* regs.read_failed bit 0 = TQAVCTRL invalid (avb_ioctl.h AVB_TAS_REGS, #365 12.2/12.3) */
/* FINAL verdict summaries (PR #477 r4619159432): fixed text plus counts; the items themselves are separate lines */
#define FINAL_SUMMARY_VIOLATIONS "%u violation(s) observed and %u required measurement(s) missing; each is listed above as a " \
                                 "FINAL violation or FINAL missing measurement line"
#define FINAL_SUMMARY_MISSING    "no violation observed, but %u required measurement(s) missing; each is listed above as a " \
                                 "FINAL missing measurement line"
#define FINAL_SUMMARY_FIXED      136u   /* the longer summary's characters besides its %u conversions (violations: 136, missing: 117; C_ASSERT) */
/* Supported inventory: TAS adapters this process runs. The runner budget is 560 s x min(enumerated adapters, this value)
 * ($HilTestTimeoutPerAdapter, checked by TEST-HILGATE-098), so every TAS adapter run fits it; discovery stays complete and a
 * TAS adapter beyond this value is reported BLOCKED, never left out silently. Not applied to --link-speed (#472): it runs every
 * TAS adapter, with a runner budget of 560 s x the enumerated count, uncapped. */
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
static const AVB_ADAPTER_IDENTITY_REQUEST *g_all;   /* every validated identity (--invalid: observer and NET_LUID_MISMATCH) */
static UINT32 g_nall;
static int g_driver_build = -1;    /* GET_VERSION: 1 Debug, 0 Release, -1 not known */
static BOOL g_cleanup_link_was_up = TRUE;   /* cleanup_and_verify: the link before the test (--invalid sets it; reset after) */
static unsigned g_next_seq;

/* Non-negative from an activation (--hw-verified) or the start of the owned NIC cycle until the cycle has reported */
static volatile LONG g_pending_adapter = -1;
static volatile LONG g_pending_did;

/* Other adapters whose owned recovery (--invalid, <case>-OTHERS) did not complete: the test keeps them listed until the process
 * ends, so neither a later step of the adapter run nor an abort can lose them. Bounded; the count beyond the bound is kept */
#define OUTSTANDING_MAX 16
static volatile LONG g_outstanding_n;
static LONG g_outstanding_idx[OUTSTANDING_MAX];
static LONG g_outstanding_did[OUTSTANDING_MAX];

static void outstanding_add(UINT32 index, avb_u16 did)
{
    LONG n = InterlockedIncrement(&g_outstanding_n) - 1;
    if (n < OUTSTANDING_MAX) {
        g_outstanding_idx[n] = (LONG)index;
        g_outstanding_did[n] = (LONG)did;
    }
}

/** Every outstanding recovery, as one line each: the operator's instruction (at the end of a run and on an abort) */
static void report_outstanding(const char *when)
{
    LONG n = g_outstanding_n, i;
    for (i = 0; i < n && i < OUTSTANDING_MAX; i++) {
        printf("[CLEANUP_FAILED] %s-OUTSTANDING/adapter:%ld did=0x%04lX: %s: an owned recovery cycle of this adapter did not complete "
               "and may still be pending in Windows. Operator: NIC cycle it, then check it with the TAS query\n", CASE_ID,
               g_outstanding_idx[i], g_outstanding_did[i], when);
    }
    if (n > OUTSTANDING_MAX) {
        printf("[CLEANUP_FAILED] %s-OUTSTANDING: %ld further outstanding recoveries beyond the list bound; see the OTHERS lines\n",
               CASE_ID, n - OUTSTANDING_MAX);
    }
    fflush(stdout);
}

static void report_abort(void)
{
    LONG adapter = InterlockedExchange(&g_pending_adapter, -1);
    if (adapter >= 0) {
        printf("[CLEANUP_FAILED] %s-CLEANUP/adapter:%ld did=0x%04lX: test aborted after an activation or during its NIC cycle; the "
               "adapter may hold a schedule or be disabled. Operator: NIC cycle (enable it), then check it with the TAS query\n",
               CASE_ID, adapter, g_pending_did);
        fflush(stdout);
    }
    report_outstanding("test aborted");
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
        if (!link_up && !g_cleanup_link_was_up) {   /* PR #479 r4621509075: no link before the test, none after: unmeasured */
            e.tx_probed = FALSE;
            printf("    TX after the cycle: not measured, the link was down before the test as well\n");
        } else if (!link_up) {
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

            int observers = 0;
            BOOL attempted = sock != INVALID_SOCKET && g_capture_luid != 0;   /* PR #479 r4621509174: no capture without an observer */
            if (attempted) {
                observers = wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4);
            }
            if (observers != 0) {
                send_window(sock, nonce, fresh, NULL, &w);
                wire_counts(&captured, &from_target);
                wire_stop();
                e.tx_probed = captured != 0;
                e.tx_on_wire = tas_tx_on_wire(&w, TAS_ACT_SEEN_MIN_PERCENT);   /* a send error fails it, never shrinks the denominator */
                print_window("after the cycle", &w);
                printf("    TX after the cycle: captured=%ld\n", captured);
            } else {
                if (attempted) {
                    wire_stop();   /* zero observers: wire_start may still have started its ICMP worker */
                }
                printf("    TX after the cycle: not probed, %s\n", sock == INVALID_SOCKET ? "no socket" : "no established observer");
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

/* --invalid, TC-TASAPI-003 (#368, amendment 2026-11-18, P2 step 5): the versioned request of #365 13.1-13.3 */
#define IEEE8023_MIN_FRAME_BYTES  64u       /* smallest Ethernet frame */
#define BASET_H_SECONDS_LIMIT     0x100000000ULL   /* BaseT seconds must fit the 32-bit BASET_H register (TAS-DR-07) */
#define INV_BIT31                 (1u << 31)       /* an unknown flag bit: no flag is published above bit 1 (#365 13.2) */
#define INV_OBSERVER_TRIES        8u               /* other adapters tried as the wire observer per adapter under test (time bound) */
#define INV_OTHERS_MAX            8u               /* other TAS adapters observed per adapter under test (budget bound) */
#define INV_OTHER_RECOVERIES_MAX  1u               /* owned recovery cycles of changed other adapters per adapter under test */
#define INV_LEAD_HEADROOM_NS      (500LL * NS_PER_MS)   /* processing between the PHC read and the driver's check, above min_margin_ns */
#define INV_LEAD_MAX_NS           (60LL * NS_PER_SEC)   /* a reported margin needing more lead than this is not used (reported) */

/* Dependencies of a case's oracle on rules #365 13.6 has not settled. Header cases (13.3 down to net_luid, in the order 13.5
 * defines) have none; the order among semantic checks is D.7, and the baseline request's generation and epoch (D.1) and tier
 * admission (D.4) are unsettled, so a semantic field's isolation is not established (amendment review constraints) */
#define INV_DEP_ORDER 0x1u   /* D.7: order among the semantic checks */
#define INV_DEP_META  0x2u   /* D.1: generation/epoch production; the baseline carries the query's values (0 = not reported) */
#define INV_DEP_TIER  0x4u   /* D.4: tier admission of the HOLD baseline */
#define INV_SEMANTIC  (INV_DEP_ORDER | INV_DEP_META | INV_DEP_TIER)

typedef enum {
    /* header checks (#365 13.3, defined order 13.5) */
    INV_UNBOUND, INV_SHORT_BUFFER, INV_SHORT_OUTPUT, INV_VERSION, INV_SIZE, INV_RESERVED, INV_SCHEDULE_RESERVED, INV_FLAGS_UNKNOWN,
    INV_QUEUE_FLAGS_UNKNOWN, INV_FLAGS_OPEN_ALL, INV_CLIENT_OP_ID_ZERO, INV_DEBUG_FAULT_RESERVED, INV_DEBUG_FAULT_RELEASE,
    INV_NET_LUID_MISMATCH,
    /* semantic checks (order not decided, D.7) */
    INV_PRE_ACTIVATION, INV_STALE_GENERATION, INV_STALE_EPOCH, INV_CYCLE_ZERO, INV_CYCLE_FIELD, INV_BASET_SECONDS, INV_BASET_NEAR,
    INV_FRAME_MIN, INV_WINDOW_EMPTY, INV_WINDOW_PAST_CYCLE, INV_WINDOW_MIN,
    INV_END_EQ_CYCLE,   /* last of the invalid-field sequence: the function-71 path did not refuse it and committed a schedule */
    INV_COUNT
} INV_ID;

/* One case: its rule, the completion #365 13.3 requires (no output), its unsettled dependencies, and why it is not submitted */
typedef struct {
    const char *name;
    const char *rule;
    LONG required;
    const char *required_src;
    unsigned deps;
    const char *not_submitted;
} INV_CASE;

static const INV_CASE g_inv[INV_COUNT] = {
    { "UNBOUND", "#365 13.3: the open instance must be bound; the valid request on an unbound write handle sent",
      (LONG)STATUS_INVALID_DEVICE_STATE, "#365 13.3 (NOT_BOUND)", 0, NULL },
    { "SHORT_BUFFER", "#365 13.3: input length at least 144; 143 bytes sent", (LONG)STATUS_BUFFER_TOO_SMALL, "#365 13.3", 0, NULL },
    { "SHORT_OUTPUT", "#365 13.3: output length at least 144; 143 bytes offered", (LONG)STATUS_BUFFER_TOO_SMALL, "#365 13.3", 0, NULL },
    { "VERSION", "#365 13.3: version 1; 2 sent", (LONG)STATUS_REVISION_MISMATCH, "#365 13.3", 0, NULL },
    { "SIZE", "#365 13.3: size 144; 80, the former request size, sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", 0, NULL },
    { "RESERVED", "#365 13.3: reserved0 0; 0xFFFFFFFF sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", 0, NULL },
    { "SCHEDULE_RESERVED", "#365 13.3: schedule.reserved 0; 1 sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", 0, NULL },
    { "FLAGS_UNKNOWN", "#365 13.3: unknown flags bits 0; bit 31 sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", 0, NULL },
    { "QUEUE_FLAGS_UNKNOWN", "#365 13.3: unknown queue[].flags bits 0; bit 31 on every queue sent", (LONG)STATUS_INVALID_PARAMETER,
      "#365 13.3", 0, NULL },
    { "FLAGS_OPEN_ALL", "#365 13.3, #359: flags.OPEN_ALL not on ACTIVATE; bit 1 sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", 0, NULL },
    { "CLIENT_OP_ID_ZERO", "#365 13.3, #352 AC1: client_op_id non-zero; 0 sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", 0, NULL },
    { "DEBUG_FAULT_RESERVED", "#365 13.3, #357 AC4: debug_fault bits 9-31 are 0 on any driver; bit 9 sent", (LONG)STATUS_INVALID_PARAMETER,
      "#365 13.3", 0, NULL },
    { "DEBUG_FAULT_RELEASE", "#365 13.3, #357 AC4: debug_fault 0 on a Release driver; 1 sent (Release only)", (LONG)STATUS_INVALID_PARAMETER,
      "#365 13.3", 0, NULL },
    { "NET_LUID_MISMATCH", "#365 13.3, #352 AC2: net_luid is the bound adapter; a different NET_LUID sent", (LONG)STATUS_INVALID_DEVICE_STATE,
      "#365 13.3 (ADAPTER_MISMATCH)", 0, NULL },
    { "PRE_ACTIVATION", "#365 13.3, TAS-DR-14: pre_activation 1 or 2; 0 sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3", INV_SEMANTIC, NULL },
    { "STALE_GENERATION", "#365 13.3, #352 AC4: expected_generation matches; a changed value", (LONG)STATUS_INVALID_DEVICE_STATE,
      "#365 13.3 (STATE_CONFLICT)", INV_SEMANTIC,
      "generation production before the #352 store is undecided (#365 13.6 D.1): a stale value cannot be constructed against a defined "
      "current value; never PASS" },
    { "STALE_EPOCH", "#365 13.3, #352 AC4: context_epoch matches; a changed value", (LONG)STATUS_INVALID_DEVICE_STATE,
      "#365 13.3 (STATE_CONFLICT)", INV_SEMANTIC,
      "context-epoch production before the #352 store is undecided (#365 13.6 D.1): a stale value cannot be constructed against a "
      "defined current value; never PASS" },
    { "CYCLE_ZERO", "TAS-DR-04: cycle within the hardware range; 0 sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3 (schedule)", INV_SEMANTIC, NULL },
    { "CYCLE_FIELD", "TAS-DR-04: cycle within the hardware range; one above the QBVCYCLET.QBV_CYCLE_TIME field (SSOT) sent",
      (LONG)STATUS_INVALID_PARAMETER, "#365 13.3 (schedule)", INV_SEMANTIC, NULL },
    { "BASET_SECONDS", "TAS-DR-07: BaseT seconds fit BASET_H; 2^32 s sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3 (schedule)",
      INV_SEMANTIC, NULL },
    { "BASET_NEAR", "TAS-DR-08, #365 13.3 BaseT lead: at least limits.min_margin_ns after a PHC read; the cycle boundary at or before "
      "the PHC sent", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3 (BaseT lead; enforced value D.3)", INV_SEMANTIC, NULL },
    { "FRAME_MIN", "max_frame_bytes below the IEEE 802.3 minimum frame of 64 bytes on every queue; 63 sent", (LONG)STATUS_INVALID_PARAMETER,
      "#365 13.3 (schedule)", INV_SEMANTIC, NULL },
    { "WINDOW_EMPTY", "TAS-DR-05: start_ns < end_ns; every queue start equal to its end sent", (LONG)STATUS_INVALID_PARAMETER,
      "#365 13.3 (schedule)", INV_SEMANTIC, NULL },
    { "WINDOW_PAST_CYCLE", "TAS-DR-05: end_ns <= cycle_ns; every queue end one ns past the cycle sent", (LONG)STATUS_INVALID_PARAMETER,
      "#365 13.3 (schedule)", INV_SEMANTIC, NULL },
    { "WINDOW_MIN", "TAS-DR-05, #351 AC3: window above 1.1 x the transmit time of max(512 bytes, max_frame_bytes) at the current link speed",
      (LONG)STATUS_INVALID_PARAMETER, "#365 13.3 (schedule at the current link speed)", INV_SEMANTIC,
      "the link speed TAS-DR-05 uses is not settled for validation (#365 13.6 D.2: K8 acquisition and its encoding exist, their "
      "consumption by TAS-DR-05 is not decided): the window bound has no settled oracle; never PASS" },
    { "END_EQ_CYCLE", "TAS-DR-06: end_ns < cycle_ns with a gap, except OPEN_ALL (a REPLACE schedule, #359, that ACTIVATE cannot express); "
      "every queue end equal to the cycle sent (EG-06 stays UNVERIFIED)", (LONG)STATUS_INVALID_PARAMETER, "#365 13.3 (schedule)",
      INV_SEMANTIC, NULL },
};

typedef struct {
    HANDLE h, hw;
    UINT32 index;
    const AVB_ADAPTER_IDENTITY_REQUEST *id;
    avb_u32 frame_bytes;
    const QUERY_READ *q0;
    const STATS_READ *s0;
    long long lead_ns;               /* BaseT lead: BASE_LEAD_NS, or the reported min_margin_ns plus headroom (#365 13.3, D.3) */
    avb_u64 other_luid;
    char other_desc[160];
    UINT32 nothers;                  /* other TAS adapters, observed around UNBOUND and NET_LUID_MISMATCH (PR #479 r4621508767) */
    UINT32 others_unobserved;        /* other TAS adapters beyond INV_OTHERS_MAX: not observed, so OTHERS is never PASS */
    BOOL others_alloc_failed;        /* the observation arrays could not be allocated: nothing observed, reported */
    const AVB_ADAPTER_IDENTITY_REQUEST **others;
    QUERY_READ *others_q0;
} INV_CTX;

static avb_u64 g_inv_op_id;   /* client_op_id of the next request: non-zero, distinct per request */

static avb_u64 inv_next_op_id(void)
{
    if (g_inv_op_id == 0) {
        g_inv_op_id = ((avb_u64)GetCurrentProcessId() << 32) | (avb_u64)GetTickCount();
    }
    if (++g_inv_op_id == 0) {
        g_inv_op_id = 1;
    }
    return g_inv_op_id;
}

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

/** Results after which the legacy activation may have written registers */
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

/** The baseline HOLD request of #365 13.2 for the bound adapter: the verified schedule set of #381 and test_tas_activate.c,
 *  BaseT the first cycle boundary at least BASE_LEAD_NS past phc, expected_generation and context_epoch as the test's own query
 *  reports them (0 = not reported today, D.1: the baseline is therefore not established as valid, see INV_DEP_META) */
static void inv_op_request(AVB_TAS_OP_REQUEST *r, const INV_CTX *c, avb_u64 phc)
{
    unsigned q;

    ZeroMemory(r, sizeof(*r));
    r->version = AVB_TAS_OP_VERSION;
    r->size = (avb_u32)sizeof(*r);
    r->net_luid = c->id->net_luid;
    r->client_op_id = inv_next_op_id();
    r->expected_generation = c->q0->q.generation;
    r->context_epoch = c->q0->q.context_epoch;
    r->pre_activation = AVB_TAS_PRE_HOLD;
    r->schedule.base_time_ns = (avb_u64)((((long long)phc + c->lead_ns + CYCLE_NS - 1) / CYCLE_NS) * CYCLE_NS);
    r->schedule.cycle_ns = (avb_u32)CYCLE_NS;
    for (q = 0; q < AVB_TAS_QUEUES; q++) {
        r->schedule.queue[q].start_ns = (avb_u32)WINDOW_START_NS;
        r->schedule.queue[q].end_ns = (avb_u32)WINDOW_END_NS;
        r->schedule.queue[q].max_frame_bytes = c->frame_bytes;
    }
}

/** The legacy 80-byte request on function 71: the verified HOLD set with the caller link speed that protocol carries */
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

/** Three BLOCKED lines of one case, never merged */
static void inv_blocked3(const char *name, UINT32 index, avb_u16 did, const char *reason)
{
    char check[48];
    sprintf_s(check, sizeof(check), "%s-REFUSED", name);
    verdict("BLOCKED", check, index, did, reason);
    sprintf_s(check, sizeof(check), "%s-UNCHANGED", name);
    verdict("BLOCKED", check, index, did, reason);
    sprintf_s(check, sizeof(check), "%s-COMPLETION", name);
    verdict("BLOCKED", check, index, did, reason);
}

/** An error completion with no output whose status a #365 13.3 refusal rule names */
static BOOL inv_refusal_status(LONG st)
{
    return st == (LONG)STATUS_INVALID_PARAMETER || st == (LONG)STATUS_REVISION_MISMATCH || st == (LONG)STATUS_BUFFER_TOO_SMALL ||
           st == (LONG)STATUS_INVALID_DEVICE_STATE || st == (LONG)STATUS_ACCESS_DENIED ||
           st == (LONG)STATUS_REQUEST_NOT_ACCEPTED;   /* #365 13.4 NOT_HARDWARE_VERIFIED: nothing written (PR #479 r4621508956) */
}

static void inv_deps_text(unsigned deps, char *buf, size_t size)
{
    buf[0] = '\0';
    if (deps & INV_DEP_META)  { strcat_s(buf, size, " D.1 (generation/epoch, the baseline carries the query's values)"); }
    if (deps & INV_DEP_TIER)  { strcat_s(buf, size, " D.4 (tier admission)"); }
    if (deps & INV_DEP_ORDER) { strcat_s(buf, size, " D.7 (order among the semantic checks)"); }
}

/** The owned NIC cycle of another adapter and its proof: the recovery for a change a misrouted probe made there */
static BOOL inv_recover_other(const AVB_ADAPTER_IDENTITY_REQUEST *o)
{
    DEVINST inst = 0;
    QUERY_READ q;
    HANDLE h;
    BOOL ok;

    if (!find_devinst(o->net_luid, &inst)) {
        return FALSE;
    }
    g_pending_did = o->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)o->index);
    ok = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
    if (ok) {
        Sleep(3000);
        ok = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
    }
    h = ok ? wait_for_adapter(o) : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) {
        return FALSE;   /* g_pending stays set: the abort handler names the adapter */
    }
    Sleep(SETTLE_MS);
    query(h, &q);
    CloseHandle(h);
    print_query("other adapter after its recovery cycle", &q);
    return query_clean(&q);
}

/** <case>-OTHERS (PR #479 r4621508767): every other TAS adapter observed before the cases is queried again after a probe that
 *  names or may reach another adapter (UNBOUND through a dispatcher fallback, NET_LUID_MISMATCH by its request). A change is
 *  FAIL and is recovered by that adapter's own NIC cycle (<case>-OTHERS-RECOVERY). Adapters without INTEL_CAP_TSN_TAS have no TAS
 *  state the query can observe (NOT_SUPPORTED): a change there is not measured, which is reported. TRUE when nothing changed */
static BOOL inv_check_others(const INV_CTX *c, const char *name)
{
    const avb_u16 did = c->id->device_id;
    UINT32 i, changed = 0, unmeasured = 0, recovered_failed = 0;
    char check[64], reason[480], part[96];

    sprintf_s(check, sizeof(check), "%s-OTHERS", name);
    if (c->others_alloc_failed) {
        verdict("BLOCKED", check, c->index, did, "not observed: the other-adapter observation could not be allocated (see OTHERS-SETUP)");
        return TRUE;
    }
    if (c->nothers == 0 && c->others_unobserved == 0) {
        printf("    [EVIDENCE] %s: no other TAS adapter is enumerated; adapters without INTEL_CAP_TSN_TAS have no TAS state the query "
               "can observe (NOT_SUPPORTED), so a misrouted write there is not measured\n", check);
        return TRUE;
    }
    reason[0] = '\0';
    for (i = 0; i < c->nothers; i++) {
        const AVB_ADAPTER_IDENTITY_REQUEST *o = c->others[i];
        QUERY_READ qa;
        HANDLE h = open_bound_read(o->net_luid);
        BOOL before = query_readable(&c->others_q0[i]);

        ZeroMemory(&qa, sizeof(qa));
        if (h != INVALID_HANDLE_VALUE) {
            query(h, &qa);
            CloseHandle(h);
        }
        if (!before || !query_readable(&qa)) {
            unmeasured++;
            sprintf_s(part, sizeof(part), "adapter:%u not measured; ", o->index);
        } else if (schedule_regs_differing(&c->others_q0[i].q.regs, &qa.q.regs) != 0 ||
                   query_clean(&c->others_q0[i]) != query_clean(&qa)) {
            changed++;
            print_query("other adapter after the probe", &qa);
            if (changed > INV_OTHER_RECOVERIES_MAX) {
                /* beyond the per-adapter recovery bound (budget): owned and reported, not cycled now */
                outstanding_add(o->index, o->device_id);
                recovered_failed++;
                sprintf_s(part, sizeof(part), "adapter:%u did=0x%04X changed, not recovered (bound); ", o->index, o->device_id);
            } else if (!inv_recover_other(o)) {
                outstanding_add(o->index, o->device_id);   /* owned until the process ends; reported on abort and at the end */
                recovered_failed++;
                sprintf_s(part, sizeof(part), "adapter:%u did=0x%04X changed, recovery incomplete; ", o->index, o->device_id);
            } else {
                sprintf_s(part, sizeof(part), "adapter:%u did=0x%04X changed, recovered; ", o->index, o->device_id);
            }
            g_pending_did = did;   /* the adapter under test is the pending one again; the others are in the outstanding list */
            InterlockedExchange(&g_pending_adapter, (LONG)c->index);
        } else {
            part[0] = '\0';
        }
        if (strlen(reason) + strlen(part) + 1 < sizeof(reason)) {   /* bounded: at most the evidence list is shortened */
            strcat_s(reason, sizeof(reason), part);
        }
    }
    if (changed != 0) {
        verdict("FAIL", check, c->index, did, reason);
        sprintf_s(check, sizeof(check), "%s-OTHERS-RECOVERY", name);
        verdict(recovered_failed == 0 ? "PASS" : "CLEANUP_FAILED", check, c->index, did, recovered_failed == 0
                ? "every changed adapter was cycled by the test and its query reports it clean"
                : "a changed adapter was not recovered (incomplete cycle, or beyond the per-adapter recovery bound): it stays in the "
                  "OUTSTANDING list. Operator: NIC cycle it, then check it with the TAS query");
        return FALSE;
    }
    if (unmeasured != 0 || c->others_unobserved != 0) {
        char more[96];
        sprintf_s(more, sizeof(more), "%u other TAS adapter(s) beyond the observation bound of %u not observed; ", c->others_unobserved,
                  INV_OTHERS_MAX);
        if (c->others_unobserved != 0 && strlen(reason) + strlen(more) + 1 < sizeof(reason)) {
            strcat_s(reason, sizeof(reason), more);
        }
        verdict("BLOCKED", check, c->index, did, reason);
        return TRUE;
    }
    sprintf_s(reason, sizeof(reason), "%u other TAS adapter(s): schedule registers and clean state as before the cases", c->nothers);
    verdict("PASS", check, c->index, did, reason);
    return TRUE;
}

/** One case: REFUSED, UNCHANGED and COMPLETION lines. TRUE when the next case may follow: the request was refused, or the
 *  code was not served (nothing reached the TAS path), and the measured state is the clean pre-state */
static BOOL run_inv_case(const INV_CTX *c, INV_ID k)
{
    const INV_CASE *t = &g_inv[k];
    const avb_u16 did = c->id->device_id;
    AVB_TAS_OP_REQUEST req;
    PHC_READ p;
    QUERY_READ qa;
    STATE_READ ta;
    STATS_READ sa;
    HANDLE hreq = c->hw, unbound = INVALID_HANDLE_VALUE;
    DWORD got = 0, inlen = (DWORD)sizeof(req), outlen = (DWORD)sizeof(req);
    LONG st;
    BOOL wrote = FALSE, refused = FALSE, not_served = FALSE, measured, unchanged;
    unsigned differing = 0, q;
    char check[48], reason[480], deps[200];

    printf("\n  --- %s: %s ---\n", t->name, t->rule);
    if (t->not_submitted != NULL) {
        inv_blocked3(t->name, c->index, did, t->not_submitted);
        return TRUE;   /* nothing sent: the clean pre-state is unchanged */
    }
    if (k == INV_DEBUG_FAULT_RELEASE && g_driver_build != 0) {
        if (g_driver_build == 1) {
            printf("    [EVIDENCE] %s: not applicable on a Debug driver, where debug_fault 1 is a valid fault request (the fault path "
                   "is #357's --fault case); not submitted, no verdict\n", t->name);
        } else {
            inv_blocked3(t->name, c->index, did, "the driver build (Debug or Release) is not known: GET_VERSION unavailable");
        }
        return TRUE;
    }
    read_phc(c->h, &p);
    if (!p.ok) {
        inv_blocked3(t->name, c->index, did, "PHC unavailable: no valid BaseT can be placed, so the case cannot isolate its field");
        return FALSE;
    }
    inv_op_request(&req, c, p.phc);
    switch (k) {
    case INV_UNBOUND:
        unbound = open_device(GENERIC_READ | GENERIC_WRITE);
        hreq = unbound;
        break;
    case INV_SHORT_BUFFER:      inlen = (DWORD)sizeof(req) - 1u; break;
    case INV_SHORT_OUTPUT:      outlen = (DWORD)sizeof(req) - 1u; break;
    case INV_VERSION:           req.version = AVB_TAS_OP_VERSION + 1u; break;
    case INV_SIZE:              req.size = (avb_u32)sizeof(AVB_TAS_ACTIVATE_REQUEST); break;
    case INV_RESERVED:          req.reserved0 = 0xFFFFFFFFu; break;
    case INV_SCHEDULE_RESERVED: req.schedule.reserved = 1u; break;
    case INV_FLAGS_UNKNOWN:     req.flags = INV_BIT31; break;
    case INV_QUEUE_FLAGS_UNKNOWN: for (q = 0; q < AVB_TAS_QUEUES; q++) { req.schedule.queue[q].flags = INV_BIT31; } break;
    case INV_FLAGS_OPEN_ALL:    req.flags = AVB_TAS_F_OPEN_ALL; break;
    case INV_CLIENT_OP_ID_ZERO: req.client_op_id = 0; break;
    case INV_DEBUG_FAULT_RESERVED:
        req.debug_fault = AVB_TAS_DEBUG_FAULT_RESERVED & (~AVB_TAS_DEBUG_FAULT_RESERVED + 1u);   /* its lowest bit: bit 9 */
        break;
    case INV_DEBUG_FAULT_RELEASE: req.debug_fault = 1u; break;
    case INV_NET_LUID_MISMATCH: req.net_luid = c->other_luid; break;
    case INV_PRE_ACTIVATION:    req.pre_activation = 0; break;
    case INV_CYCLE_ZERO:        req.schedule.cycle_ns = 0; break;
    case INV_CYCLE_FIELD:
        req.schedule.cycle_ns = (avb_u32)((I226_QBVCYCLET_QBV_CYCLE_TIME_MASK >> I226_QBVCYCLET_QBV_CYCLE_TIME_SHIFT) + 1ULL);
        break;
    case INV_BASET_SECONDS:     req.schedule.base_time_ns = BASET_H_SECONDS_LIMIT * (avb_u64)NS_PER_SEC; break;
    case INV_BASET_NEAR:        req.schedule.base_time_ns = (p.phc / (avb_u64)CYCLE_NS) * (avb_u64)CYCLE_NS; break;
    case INV_FRAME_MIN:
        for (q = 0; q < AVB_TAS_QUEUES; q++) { req.schedule.queue[q].max_frame_bytes = IEEE8023_MIN_FRAME_BYTES - 1u; }
        break;
    /* Window cases change every queue alike, as the verified set has identical windows: differing windows could make the
     * gate-list conversion refuse the request for another reason than the rule under test */
    case INV_WINDOW_EMPTY:
        for (q = 0; q < AVB_TAS_QUEUES; q++) { req.schedule.queue[q].end_ns = req.schedule.queue[q].start_ns; }
        break;
    case INV_WINDOW_PAST_CYCLE:
        for (q = 0; q < AVB_TAS_QUEUES; q++) { req.schedule.queue[q].end_ns = req.schedule.cycle_ns + 1u; }
        break;
    case INV_END_EQ_CYCLE:
        for (q = 0; q < AVB_TAS_QUEUES; q++) { req.schedule.queue[q].end_ns = req.schedule.cycle_ns; }
        break;
    default: break;
    }
    printf("    [EVIDENCE] %s request: IOCTL_AVB_TAS_ACTIVATE_80C 0x%08lX version=%u size=%u luid=0x%016llX op=0x%016llX gen=%llu "
           "epoch=%llu flags=0x%X pre=%u fault=0x%X reserved0=0x%X base=%llu (PHC %llu) cycle=%u sreserved=%u q0=[%u, %u) frame=%u "
           "qflags=0x%X in=%lu out=%lu handle=%s\n", t->name, (unsigned long)IOCTL_AVB_TAS_ACTIVATE_80C, req.version, req.size,
           req.net_luid, req.client_op_id, req.expected_generation, req.context_epoch, req.flags, req.pre_activation, req.debug_fault,
           req.reserved0, req.schedule.base_time_ns, p.phc, req.schedule.cycle_ns, req.schedule.reserved, req.schedule.queue[0].start_ns,
           req.schedule.queue[0].end_ns, req.schedule.queue[0].max_frame_bytes, req.schedule.queue[0].flags, inlen, outlen,
           k == INV_UNBOUND ? "unbound" : "bound");
    if (k == INV_NET_LUID_MISMATCH) {
        printf("    [EVIDENCE] %s: %s\n", t->name, c->other_desc);
    }
    if (hreq == INVALID_HANDLE_VALUE) {
        inv_blocked3(t->name, c->index, did, "cannot open an unbound handle with write access");
        return TRUE;   /* nothing sent */
    }
    st = avb_ioctl_ntstatus(hreq, IOCTL_AVB_TAS_ACTIVATE_80C, &req, inlen, &req, outlen, &got);
    if (unbound != INVALID_HANDLE_VALUE) { CloseHandle(unbound); }
    not_served = st == (LONG)STATUS_INVALID_DEVICE_REQUEST;
    printf("    [EVIDENCE] %s reply: ntstatus=0x%08lX information=%lu%s\n", t->name, (unsigned long)st, got,
           st == 0 && got == sizeof(req) ? "" : (st == 0 ? " (success without the complete reply)" : ""));

    /* REFUSED: the observation alone, whatever the completion contract says */
    sprintf_s(check, sizeof(check), "%s-REFUSED", t->name);
    if (st == AVB_NTSTATUS_UNAVAILABLE) {
        wrote = TRUE;   /* unknown: never assume nothing was written */
        verdict("INCONCLUSIVE", check, c->index, did, "raw NTSTATUS not captured; whether the request was refused is unknown");
    } else if (st >= 0) {
        wrote = TRUE;   /* accepted, or a success of unknown extent: the operation may have written registers */
        sprintf_s(reason, sizeof(reason), "not refused: ntstatus=0x%08lX information=%lu; the rule requires a refusal; the operation "
                  "may have written registers", (unsigned long)st, got);
        verdict("FAIL", check, c->index, did, reason);
    } else if (not_served) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu: IOCTL_AVB_TAS_ACTIVATE_80C is not served; the code is "
                  "allocated to the adapter's TAS function (#362, #357), so this is no refusal and no skip", (unsigned long)st, got);
        verdict("FAIL", check, c->index, did, reason);
    } else if (got == 0 && inv_refusal_status(st)) {
        refused = TRUE;
        sprintf_s(reason, sizeof(reason), "refused: error completion ntstatus=0x%08lX, no output", (unsigned long)st);
        verdict("PASS", check, c->index, did, reason);
    } else {
        AVB_VERDICT v = avb_verdict_from_status((NTSTATUS)st, AVB_CAP_PRESENT, FALSE, TRUE);
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu: not a refusal any #365 13.3 rule names", (unsigned long)st, got);
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

    /* COMPLETION: the status #365 13.3 requires, kept apart from REFUSED and UNCHANGED. A header case's oracle is established by
     * the defined order (13.5); a semantic case's required status is observable but its field's isolation is not (D.1, D.4, D.7),
     * so a match is INCONCLUSIVE, never PASS, and a status the unsettled metadata rule can produce is INCONCLUSIVE too */
    sprintf_s(check, sizeof(check), "%s-COMPLETION", t->name);
    inv_deps_text(t->deps, deps, sizeof(deps));
    if (st == AVB_NTSTATUS_UNAVAILABLE) {
        verdict("INCONCLUSIVE", check, c->index, did, "raw NTSTATUS not captured");
    } else if (k == INV_UNBOUND && st == t->required && got == 0) {   /* before the header PASS below (#479 round 2) */
        /* PR #479 r4621508729: NOT_BOUND and ADAPTER_MISMATCH complete with the same status and no output; on a dispatcher that
         * falls back to an adapter for an unbound handle, the later net_luid check can produce it too. The status alone does not
         * show that the unbound gate ran; the result value (#352 record, #379) or a deterministic fallback would */
        verdict("INCONCLUSIVE", check, c->index, did, "STATUS_INVALID_DEVICE_STATE with no output, as #365 13.3 requires, but NOT_BOUND "
                "and ADAPTER_MISMATCH share it and an error completion carries no result: that the unbound gate refused it is not "
                "established (other TAS adapters are checked in UNBOUND-OTHERS)");
    } else if (st == t->required && got == 0 && t->deps == 0) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX with no output, as %s requires", (unsigned long)st, t->required_src);
        verdict("PASS", check, c->index, did, reason);
    } else if (t->deps != 0 && got == 0 && (st == t->required || st == (LONG)STATUS_INVALID_DEVICE_STATE ||
                                            ((t->deps & INV_DEP_TIER) && st == (LONG)STATUS_REQUEST_NOT_ACCEPTED))) {
        sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX with no output (%s requires 0x%08lX); the field's isolation is not "
                  "established, unsettled:%s", (unsigned long)st, t->required_src, (unsigned long)t->required, deps);
        verdict("INCONCLUSIVE", check, c->index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "%s requires ntstatus=0x%08lX with no output; observed ntstatus=0x%08lX information=%lu%s",
                  t->required_src, (unsigned long)t->required, (unsigned long)st, got,
                  not_served ? " (IOCTL_AVB_TAS_ACTIVATE_80C not served)" : "");
        verdict("FAIL", check, c->index, did, reason);
    }
    /* A later request needs an established refusal (or a code that nothing served) and the measured clean pre-state (PR #451
     * review; #368 amendment "code not served"); COMPLETION never stops. A changed other adapter stops it too */
    if ((k == INV_UNBOUND || k == INV_NET_LUID_MISMATCH) && !inv_check_others(c, t->name)) {
        return FALSE;
    }
    return (refused || (not_served && got == 0)) && measured && unchanged && !wrote;
}

/** TX of the adapter under test, received by the established wire observer: a checked window against the baseline */
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

/** The clean pre-state as the --invalid PRESTATE defines it: a clean, readable query, record CLEAN */
static BOOL inv_state_clean(HANDLE h, QUERY_READ *q, STATE_READ *t)
{
    query(h, q);
    read_state(h, t);
    return query_clean(q) && t->ok && t->s.armed == AVB_TAS_STATE_CLEAN;
}

/** LEGACY_FUNCTION_71 (#368 amendment): a separate compatibility phase after the invalid-field sequence, from its own proven
 *  clean baseline. A baseline not proved clean is recovered once by the test's own NIC cycle and proved again; the phase never
 *  continues on a possibly armed adapter. The known-valid 80-byte request on 0x0017811C must complete STATUS_INVALID_DEVICE_REQUEST
 *  (function 71 withdrawn). Today's driver may commit it: the final owned cleanup follows. Returns the bound read handle to use
 *  for that cleanup (a new one after a recovery cycle, INVALID_HANDLE_VALUE if the adapter did not return) */
static HANDLE inv_legacy_phase(HANDLE h, HANDLE *hw, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst)
{
    AVB_TAS_ACTIVATE_REQUEST req;
    MIB_IF_ROW2 lrow;
    DWORD ifrc;
    unsigned long long bps;
    QUERY_READ ql;
    STATE_READ tl;
    PHC_READ p;
    DWORD got = 0;
    LONG st;
    BOOL clean, reply;
    char reason[400];

    printf("\n  --- LEGACY_FUNCTION_71: separate compatibility phase, from its own proven-clean baseline ---\n");
    clean = inv_state_clean(h, &ql, &tl);
    if (!clean) {
        BOOL cycled;
        print_query("legacy baseline", &ql);
        printf("    legacy baseline not proved clean: recovered once by the test's own NIC cycle, then proved again\n");
        if (*hw != INVALID_HANDLE_VALUE) { CloseHandle(*hw); *hw = INVALID_HANDLE_VALUE; }
        CloseHandle(h);
        g_pending_did = id->device_id;
        InterlockedExchange(&g_pending_adapter, (LONG)index);
        cycled = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
        if (cycled) {
            Sleep(3000);
            cycled = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
        }
        h = cycled ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
        if (h == INVALID_HANDLE_VALUE) {
            verdict("CLEANUP_FAILED", "LEGACY_SETUP", index, id->device_id, "the recovery cycle did not complete or the adapter did not "
                    "return. Operator: NIC cycle once it ends, then check it with the TAS query");
            verdict("BLOCKED", "LEGACY_FUNCTION_71", index, id->device_id, "no proven-clean baseline: the recovery did not complete");
            return INVALID_HANDLE_VALUE;
        }
        Sleep(SETTLE_MS);
        clean = inv_state_clean(h, &ql, &tl);
        *hw = open_device(GENERIC_READ | GENERIC_WRITE);
        if (*hw != INVALID_HANDLE_VALUE && !bind_luid(*hw, id->net_luid)) { CloseHandle(*hw); *hw = INVALID_HANDLE_VALUE; }
        if (!clean) {
            print_query("legacy baseline after recovery", &ql);
            verdict("CLEANUP_FAILED", "LEGACY_SETUP", index, id->device_id, "still not clean after the recovery cycle");
            verdict("BLOCKED", "LEGACY_FUNCTION_71", index, id->device_id, "no proven-clean baseline after the recovery cycle");
            return h;
        }
        verdict("PASS", "LEGACY_SETUP", index, id->device_id, "recovered by the owned NIC cycle and proved clean again");
    }
    if (*hw == INVALID_HANDLE_VALUE) {   /* its own bound write handle, also when the field sequence never opened one */
        *hw = open_device(GENERIC_READ | GENERIC_WRITE);
        if (*hw != INVALID_HANDLE_VALUE && !bind_luid(*hw, id->net_luid)) { CloseHandle(*hw); *hw = INVALID_HANDLE_VALUE; }
    }
    ZeroMemory(&lrow, sizeof(lrow));
    lrow.InterfaceLuid.Value = id->net_luid;
    ifrc = GetIfEntry2(&lrow);   /* fresh: a recovery cycle may have renegotiated the link (PR #479 r4621508996) */
    bps = lrow.TransmitLinkSpeed;
    read_phc(h, &p);
    if (!p.ok || *hw == INVALID_HANDLE_VALUE || ifrc != NO_ERROR || lrow.MediaConnectState != MediaConnectStateConnected ||
        bps == 0 || bps == ~0ULL || bps % 1000000ULL != 0 || bps / 1000000ULL > 0xFFFFFFFFULL) {
        sprintf_s(reason, sizeof(reason), "%s (GetIfEntry2 %lu, state %d, TransmitLinkSpeed %llu)", !p.ok ? "PHC unavailable: no valid "
                  "BaseT can be placed" : *hw == INVALID_HANDLE_VALUE ? "no bound write handle" : "no connected link with a known rate "
                  "exactly representable in the legacy request's Mbit/s field", ifrc, (int)lrow.MediaConnectState, bps);
        verdict("BLOCKED", "LEGACY_FUNCTION_71", index, id->device_id, reason);
        return h;
    }
    inv_request(&req, &lrow, p.phc);
    req.phc_at_commit_ns = 0;
    req.result = 0xFFFFFFFFu;   /* values the driver never writes: an unwritten reply cannot read as a result */
    req.status = 0xFFFFFFFFu;
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);   /* an accepted request commits a schedule: the cleanup owes a cycle */
    st = avb_ioctl_ntstatus(*hw, IOCTL_AVB_TAS_ACTIVATE, &req, (DWORD)sizeof(req), &req, (DWORD)sizeof(req), &got);
    reply = st == 0 && got == sizeof(req);
    printf("    [EVIDENCE] LEGACY_FUNCTION_71 request: IOCTL_AVB_TAS_ACTIVATE 0x%08lX 80 bytes base=%llu (PHC %llu); reply ntstatus=0x%08lX "
           "information=%lu result=%s (%u)\n", (unsigned long)IOCTL_AVB_TAS_ACTIVATE, req.base_time_ns, p.phc, (unsigned long)st, got,
           reply ? act_result_name(req.result) : "not delivered", reply ? req.result : 0u);
    if (st == (LONG)STATUS_INVALID_DEVICE_REQUEST && got == 0) {
        verdict("PASS", "LEGACY_FUNCTION_71", index, id->device_id, "function 71 withdrawn: STATUS_INVALID_DEVICE_REQUEST, no output");
    } else {
        sprintf_s(reason, sizeof(reason), "function 71 still served: ntstatus=0x%08lX information=%lu result=%s%s; #365 13.1 withdraws it "
                  "(STATUS_INVALID_DEVICE_REQUEST)", (unsigned long)st, got, reply ? act_result_name(req.result) : "not delivered",
                  reply && act_result_wrote(req.result) ? "; the adapter may hold a schedule, the owned cleanup follows" : "");
        verdict("FAIL", "LEGACY_FUNCTION_71", index, id->device_id, reason);
    }
    return h;
}

/** --invalid, TC-TASAPI-003 (#368, Verifies #351): each invalid versioned request refused, TAS query, record and TX unchanged */
static void run_invalid(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst)
{
    QUERY_READ q0;
    STATS_READ s0;
    STATE_READ t0;
    PHC_READ p0;
    MIB_IF_ROW2 row;
    IN_ADDR ipv4;
    INV_CTX c;
    AVB_VERDICT v;
    HANDLE h;
    SOCKET sock = INVALID_SOCKET;
    TAS_TX_WINDOW base, probe;
    unsigned nonce = 0;
    BOOL wire = FALSE, has_ip, other_link = FALSE;
    UINT32 o, tries = 0;
    int k = 0;
    const char *stop = NULL;
    /* tried: at most INV_OBSERVER_TRIES (8) entries of at most 31 characters ("adapter:4294967295 did=0xFFFF" plus ", ") and the
     * 25-character bound suffix = 273 < 320; reason: its longest format (about 115 characters) plus tried plus 15 < 600 */
    char reason[600], tried[320];

    ZeroMemory(&c, sizeof(c));
    ZeroMemory(&base, sizeof(base));
    ZeroMemory(&ipv4, sizeof(ipv4));
    c.hw = INVALID_HANDLE_VALUE;
    c.lead_ns = BASE_LEAD_NS;
    g_capture_luid = 0;   /* this mode establishes its own observer below, never by adapter model */
    g_next_seq = 0;       /* PR #479 r4621509027: per adapter run; no capture is active here, so the tag slots are free */
    if (!read_if(id->net_luid, &row)) {
        verdict("BLOCKED", "PRESTATE", index, id->device_id, "GetIfEntry2 failed for the adapter: no MTU for max_frame_bytes");
        return;   /* nothing sent to the adapter, nothing to clean */
    }
    h = open_bound_read(id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "PRESTATE", index, id->device_id, "cannot open a read-only handle and bind it by NET_LUID");
        return;
    }
    has_ip = row.OperStatus == IfOperStatusUp && find_addressing(id->net_luid, &ipv4, &other_link);
    g_cleanup_link_was_up = row.OperStatus == IfOperStatusUp;   /* PR #479 r4621509075: the cleanup's link reference */
    read_phc(h, &p0);
    read_stats(h, &s0);
    read_state(h, &t0);
    query(h, &q0);
    print_query("pre-test", &q0);
    printf("    [EVIDENCE] DTXMXPKTSZ raw=0x%08X decoded=%u bytes state=%u; limits.min_margin_ns=%llu state=%u (D.3); link_speed_mbps=%u; "
           "generation=%llu context_epoch=%llu (0 = not reported, D.1)\n", q0.q.regs.dtxmxpktsz, q0.q.dtx_max_frame_bytes,
           q0.q.dtx_max_frame_state, q0.q.limits.min_margin_ns, q0.q.limits.min_margin_state, q0.q.link_speed_mbps, q0.q.generation,
           q0.q.context_epoch);

    /* PRESTATE: the test proves its own pre-state through the TAS query before it submits anything (#368) */
    v = avb_verdict_from_status((NTSTATUS)q0.status, AVB_CAP_PRESENT, query_valid(&q0), TRUE);
    if (v != AVB_VERDICT_PASS) {
        sprintf_s(reason, sizeof(reason), "pre-test query ntstatus=0x%08lX information=%lu", (unsigned long)q0.status, q0.got);
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
    /* BaseT lead (#365 13.3, PR #479 r4621508892): the reported limits.min_margin_ns plus processing headroom when the query
     * establishes it and it needs more than BASE_LEAD_NS; checked against INV_LEAD_MAX_NS before any arithmetic */
    if (stop == NULL && q0.q.limits.min_margin_state == AVB_TAS_LIMIT_ESTABLISHED) {
        if (q0.q.limits.min_margin_ns > (avb_u64)(INV_LEAD_MAX_NS - INV_LEAD_HEADROOM_NS)) {
            sprintf_s(reason, sizeof(reason), "limits.min_margin_ns %llu needs a lead beyond the test's bound of %lld ns",
                      q0.q.limits.min_margin_ns, (long long)INV_LEAD_MAX_NS);
            verdict("BLOCKED", "LEAD", index, id->device_id, reason);
            stop = "the reported minimum BaseT margin cannot be honoured";
        } else if ((long long)q0.q.limits.min_margin_ns + INV_LEAD_HEADROOM_NS > c.lead_ns) {
            c.lead_ns = (long long)q0.q.limits.min_margin_ns + INV_LEAD_HEADROOM_NS;
        }
    }
    printf("    [EVIDENCE] BaseT lead %lld ns (limits.min_margin_ns %llu, state %u: %s)\n", c.lead_ns, q0.q.limits.min_margin_ns,
           q0.q.limits.min_margin_state, q0.q.limits.min_margin_state == AVB_TAS_LIMIT_ESTABLISHED ? "established" :
           "not established, D.3: the test's own 1.5 s lead");
    if (stop == NULL) {
        c.hw = open_device(GENERIC_READ | GENERIC_WRITE);   /* the activation is FILE_WRITE_ACCESS (#362, #365 13.1) */
        if (c.hw == INVALID_HANDLE_VALUE || !bind_luid(c.hw, id->net_luid)) {
            if (c.hw != INVALID_HANDLE_VALUE) { CloseHandle(c.hw); }
            c.hw = INVALID_HANDLE_VALUE;
            stop = "cannot bind a handle with write access";
        }
    }

    /* OBSERVER (#368 amendment: interface-based, no fixed observer model, no execution veto): every other enumerated adapter is
     * tried as the passive capture until one receives the baseline frames the adapter under test sends. Without one, only the
     * TX lines are BLOCKED; the field cases, which judge the query, record and counters, still run */
    tried[0] = '\0';
    if (stop == NULL && !has_ip) {
        verdict("BLOCKED", "OBSERVER", index, id->device_id, "the adapter is down or has no IPv4 address: no frames can be sent, the TX "
                "lines are BLOCKED; the field cases still run");
    } else if (stop == NULL) {
        sock = open_tx_socket(ipv4);
        for (o = 0; sock != INVALID_SOCKET && o < g_nall && !wire; o++) {
            char one[48];
            if (g_all[o].net_luid == id->net_luid) { continue; }
            if (tries == INV_OBSERVER_TRIES) {   /* the TX lines are evidence: the bound keeps the per-adapter budget linear */
                strcat_s(tried, sizeof(tried), strstr(tried, ", ...") == NULL ? ", ... (try bound reached)" : "");
                break;
            }
            tries++;
            nonce = GetTickCount() ^ GetCurrentProcessId() ^ (o << 24);
            sprintf_s(one, sizeof(one), "%sadapter:%u did=0x%04X", tried[0] ? ", " : "", g_all[o].index, g_all[o].device_id);
            if (strlen(tried) + strlen(one) + 5 < sizeof(tried)) {   /* bounded: the list is evidence only, the attempts continue */
                strcat_s(tried, sizeof(tried), one);
            } else if (strstr(tried, ", ...") == NULL) {
                strcat_s(tried, sizeof(tried), ", ...");
            }
            if (wire_start(id->net_luid, g_all[o].net_luid, id->mac, nonce, ipv4) != 0) {
                ZeroMemory(&base, sizeof(base));
                send_window(sock, nonce, h, NULL, &base);
                wire = tas_tx_on_wire(&base, TAS_ACT_SEEN_MIN_PERCENT);
                if (wire) {
                    g_capture_luid = g_all[o].net_luid;
                    print_window("before the cases", &base);
                    sprintf_s(reason, sizeof(reason), "adapter:%u did=0x%04X received %u of %u baseline frames the adapter under test sent",
                              g_all[o].index, g_all[o].device_id, base.seen, base.attempted);
                    verdict("PASS", "OBSERVER", index, id->device_id, reason);
                    break;
                }
            }
            wire_stop();   /* also after 0 observers: the ICMP worker may run (PR #451 review) */
        }
        if (!wire) {
            sprintf_s(reason, sizeof(reason), "no other enumerated adapter received the baseline frames (tried: %s%s); the TX lines are "
                      "BLOCKED, the field cases still run", tried[0] ? tried : "none", sock == INVALID_SOCKET ? "; no TX socket" : "");
            verdict("BLOCKED", "OBSERVER", index, id->device_id, reason);
        }
    }

    if (stop == NULL) {
        c.h = h;
        c.index = index;
        c.id = id;
        c.frame_bytes = (avb_u32)row.Mtu + FRAME_OVERHEAD_BYTES;
        c.q0 = &q0;
        c.s0 = &s0;
        /* Every other TAS adapter, observed before the cases (PR #479 r4621508767) */
        c.others = (const AVB_ADAPTER_IDENTITY_REQUEST **)calloc(INV_OTHERS_MAX, sizeof(*c.others));
        c.others_q0 = (QUERY_READ *)calloc(INV_OTHERS_MAX, sizeof(*c.others_q0));
        if (c.others == NULL || c.others_q0 == NULL) {   /* #479 round 2: reported, never silent */
            c.others_alloc_failed = TRUE;
            verdict("BLOCKED", "OTHERS-SETUP", index, id->device_id, "allocation of the other-adapter observation failed: other TAS "
                    "adapters are not observed around UNBOUND and NET_LUID_MISMATCH (their OTHERS lines are BLOCKED)");
        }
        for (o = 0; !c.others_alloc_failed && o < g_nall; o++) {
            if (g_all[o].net_luid != id->net_luid && (g_all[o].capabilities & INTEL_CAP_TSN_TAS)) {
                HANDLE ho;
                if (c.nothers == INV_OTHERS_MAX) {
                    c.others_unobserved++;
                    continue;
                }
                ho = open_bound_read(g_all[o].net_luid);
                c.others[c.nothers] = &g_all[o];
                if (ho != INVALID_HANDLE_VALUE) {
                    query(ho, &c.others_q0[c.nothers]);
                    CloseHandle(ho);
                }
                c.nothers++;
            }
        }
        printf("    [EVIDENCE] other TAS adapters observed around UNBOUND and NET_LUID_MISMATCH: %u (beyond the bound of %u: %u)\n",
               c.nothers, INV_OTHERS_MAX, c.others_unobserved);
        /* NET_LUID_MISMATCH: another TAS adapter's NET_LUID first (its state is observable), then any other enumerated adapter's,
         * otherwise an identity no adapter has */
        c.other_luid = 0;
        if (c.nothers != 0) {
            c.other_luid = c.others[0]->net_luid;
            sprintf_s(c.other_desc, sizeof(c.other_desc), "net_luid 0x%016llX identifies another enumerated TAS adapter (adapter:%u did=0x%04X)",
                      c.other_luid, c.others[0]->index, c.others[0]->device_id);
        }
        for (o = 0; c.other_luid == 0 && o < g_nall; o++) {
            if (g_all[o].net_luid != id->net_luid && g_all[o].net_luid != 0) {
                c.other_luid = g_all[o].net_luid;
                sprintf_s(c.other_desc, sizeof(c.other_desc), "net_luid 0x%016llX identifies another enumerated adapter (adapter:%u did=0x%04X) "
                          "without INTEL_CAP_TSN_TAS, whose TAS state the query cannot observe", c.other_luid, g_all[o].index, g_all[o].device_id);
                break;
            }
        }
        if (c.other_luid == 0) {
            c.other_luid = id->net_luid ^ 1ULL;
            sprintf_s(c.other_desc, sizeof(c.other_desc), "net_luid 0x%016llX identifies no enumerated adapter (no other adapter exists): the "
                      "different-existing-adapter subcase is not exercised", c.other_luid);
        }
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
            sprintf_s(reason, sizeof(reason), "not submitted: after case %s a refusal (or an unserved code) or the measured clean pre-state "
                      "was not established", stop);
        } else {
            sprintf_s(reason, sizeof(reason), "not submitted: %s", stop);
        }
        inv_blocked3(g_inv[k].name, index, id->device_id, reason);   /* PR #479 r4621509113: three lines, never merged */
    }

    /* TX after the field cases, before END_EQ_CYCLE, which the earlier protocol accepted */
    if (stop != NULL) {
        sprintf_s(reason, sizeof(reason), "the field cases were not all submitted in the clean pre-state (%s)", stop);
        verdict("BLOCKED", "TX", index, id->device_id, reason);
    } else if (!wire) {
        verdict("BLOCKED", "TX", index, id->device_id, "no established wire observer (see OBSERVER)");
    } else {
        send_window(sock, nonce, h, &q0.q.regs, &probe);
        print_window("after the field cases", &probe);
        inv_tx_verdict("TX", index, id->device_id, &base, &probe);
    }

    /* END_EQ_CYCLE (TAS-DR-06) last of the invalid-field sequence */
    if (stop != NULL) {
        sprintf_s(reason, sizeof(reason), "not submitted: %s", stop);
        inv_blocked3(g_inv[INV_END_EQ_CYCLE].name, index, id->device_id, reason);
        verdict("BLOCKED", "TX-END_EQ_CYCLE", index, id->device_id, reason);
    } else if (!run_inv_case(&c, INV_END_EQ_CYCLE)) {
        verdict("BLOCKED", "TX-END_EQ_CYCLE", index, id->device_id, "case stopped: the request was not refused or the state changed or "
                "was not measured (see its lines); the wire after it is not attributable. EG-06 is not tested");
    } else if (!wire) {
        verdict("BLOCKED", "TX-END_EQ_CYCLE", index, id->device_id, "no established wire observer (see OBSERVER)");
    } else {
        send_window(sock, nonce, h, &q0.q.regs, &probe);
        print_window("after END_EQ_CYCLE", &probe);
        inv_tx_verdict("TX-END_EQ_CYCLE", index, id->device_id, &base, &probe);
    }
    if (wire) { wire_stop(); }
    if (sock != INVALID_SOCKET) { closesocket(sock); }

    /* LEGACY_FUNCTION_71: its own phase, outside the invalid-field sequence, from its own proven-clean baseline, also when the
     * field sequence could not start (PR #479 r4621509144): LEGACY_SETUP gets its recovery attempt in every case */
    h = inv_legacy_phase(h, &c.hw, index, id, inst);
    if (c.hw != INVALID_HANDLE_VALUE) { CloseHandle(c.hw); }
    free((void *)c.others);
    free(c.others_q0);
    if (h == INVALID_HANDLE_VALUE) {
        g_cleanup_link_was_up = TRUE;
        InterlockedExchange(&g_pending_adapter, -1);
        verdict("BLOCKED", "RESTORE", index, id->device_id, "the adapter did not return from the legacy recovery cycle; see LEGACY_SETUP");
        return;
    }
    cleanup_and_verify(h, index, id, inst, ipv4, &q0, &p0);
    g_cleanup_link_was_up = TRUE;   /* the other modes keep their link-up precondition */
    if (g_outstanding_n != 0) {
        report_outstanding("after this adapter run");
    }
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
    avb_u64 luid;            /* the validated target NET_LUID */
    BOOL os_agreed;          /* both OS readings succeeded and report the same state and transmit rate */
    BOOL query_failed;       /* a query failure established on its own (status, malformed reply, foreign NET_LUID): kept, never retried away */
    DWORD err1, err2;        /* raw GetIfEntry2 return codes of the last try */
    MIB_IF_ROW2 os;          /* the agreeing OS reading (valid only when os_agreed) */
    QUERY_READ q;            /* the failed query if query_failed, otherwise the query of the last try */
    unsigned tries;
} LINK_OBS;

/** A query result that fails by itself, independent of any OS reading: a nonpassing status verdict (avb_verdict.h), or a
 *  valid reply that names another adapter */
static BOOL link_query_failed(const QUERY_READ *q, avb_u64 luid)
{
    return avb_verdict_from_status((NTSTATUS)q->status, AVB_CAP_PRESENT, query_valid(q), TRUE) != AVB_VERDICT_PASS ||
           q->q.net_luid != luid;
}

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
    o->luid = luid;
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
        /* A query that fails by itself ends the observation: a retry for the OS bracket must never replace it (r4619159368) */
        if (link_query_failed(&o->q, luid)) {
            o->query_failed = TRUE;
            printf("    [EVIDENCE] %s: the query failed on try %u; the observation stops here and keeps that reply\n", label, o->tries);
            return;
        }
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
        sprintf_s(reason, size, "query on try %u: ntstatus=0x%08lX information=%lu reply %s", o->tries, (unsigned long)o->q.status,
                  o->q.got, query_valid(&o->q) ? "valid" : "not a valid v1 reply");
        return v;
    }
    if (o->q.q.net_luid != o->luid) {
        sprintf_s(reason, size, "query on try %u: the reply names NET_LUID 0x%016llX, not the target 0x%016llX", o->tries,
                  o->q.q.net_luid, o->luid);
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

/** FINAL's findings (PR #477 review r4619159432, reporting capacity). Every finding is printed at once as its own complete line
 *  through vprintf, which writes to stdout with no caller buffer, and is counted in an integer first. The FINAL outcome is
 *  decided from the two counts only, never from whether a diagnostic text could be built, so no formatting limit can drop a
 *  finding, turn a violation into PASS or abort the report. The verdict line carries only a bounded summary (run_link_speed). */
typedef struct { unsigned violations, missing; } FINAL_FINDINGS;

static void final_item(unsigned *count, const char *kind, const char *fmt, va_list ap)
{
    (*count)++;   /* counted first: the finding exists whatever the printing does */
    printf("    [EVIDENCE] FINAL %s: ", kind);
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
}

static void final_violation(FINAL_FINDINGS *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    final_item(&f->violations, "violation", fmt, ap);
    va_end(ap);
}

static void final_missing(FINAL_FINDINGS *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    final_item(&f->missing, "missing measurement", fmt, ap);
    va_end(ap);
}

/** FINAL's TAS-query requirement (clean and readable), judged field by field under #365 12.2/12.3 validity, after the reply
 *  itself (status, size, version) and its target identity are valid (review r4619159432 follow-up). A field counts only when
 *  its own validity rule holds, so an unrelated register read failure never hides an observed violation, and an invalid
 *  field's 0 is never read as a hardware observation:
 *    hw_mode   from TQAVCTRL; valid when read_failed bit 0 is clear and it is not 0 or UNREADABLE. TSN is a violation.
 *    current_status, next_status   always reported (U1, never 0). NONE conforms; UNKNOWN establishes nothing (missing);
 *              any other status is residual slot state (violation).
 *    restoration   advisory (12.5), always reported. COMPLETE conforms; UNKNOWN or NO_SSOT_BASELINE establish nothing
 *              (missing); NONE and the PARTIAL values report a residue (violation).
 *    registers  every v1 register valid (hw_read READ, read_failed 0); otherwise listed as missing evidence. */
static void link_final_query(const QUERY_READ *r, avb_u64 luid, FINAL_FINDINGS *f)
{
    const AVB_TAS_QUERY_HW_REQUEST *q = &r->q;

    if (!query_valid(r) || q->net_luid != luid) {
        final_missing(f, "TAS query: ntstatus=0x%08lX information=%lu, %s", (unsigned long)r->status, r->got,
                      !query_valid(r) ? "no valid v1 reply" : "reply for another NET_LUID");
        return;
    }
    if ((q->regs.read_failed & LINK_RF_TQAVCTRL) != 0 || q->hw_mode == 0 || q->hw_mode == AVB_TAS_HWMODE_UNREADABLE) {
        final_missing(f, "TSN mode: TQAVCTRL invalid (hw_mode=%u, read_failed bit 0 %s)", q->hw_mode,
                      (q->regs.read_failed & LINK_RF_TQAVCTRL) ? "set" : "clear");
    } else if (q->hw_mode != AVB_TAS_HWMODE_OFF) {
        final_violation(f, "TSN mode on: valid TQAVCTRL=0x%08X, hw_mode=%u", q->regs.tqavctrl, q->hw_mode);
    }
    if (q->current_status == 0 || q->current_status == AVB_TAS_ST_UNKNOWN) {
        final_missing(f, "current slot: status %u, reasons 0x%X, not established", q->current_status, q->current_reasons);
    } else if (q->current_status != AVB_TAS_ST_NONE) {
        final_violation(f, "current slot status %u (reasons 0x%X), not NONE", q->current_status, q->current_reasons);
    }
    if (q->next_status == 0 || q->next_status == AVB_TAS_ST_UNKNOWN) {
        final_missing(f, "next slot: status %u, reasons 0x%X, not established", q->next_status, q->next_reasons);
    } else if (q->next_status != AVB_TAS_ST_NONE) {
        final_violation(f, "next slot status %u (reasons 0x%X), not NONE", q->next_status, q->next_reasons);
    }
    if (q->restoration == 0 || q->restoration == AVB_TAS_RESTORE_UNKNOWN || q->restoration == AVB_TAS_RESTORE_NO_SSOT_BASELINE) {
        final_missing(f, "advisory restoration %u, not established", q->restoration);
    } else if (q->restoration != AVB_TAS_RESTORE_COMPLETE) {
        final_violation(f, "advisory restoration %u reports a residue, not COMPLETE", q->restoration);
    }
    if (q->hw_read != AVB_TAS_HWREAD_READ || q->regs.read_failed != 0) {
        final_missing(f, "registers not all valid: hw_read=%u read_failed=0x%05X", q->hw_read, q->regs.read_failed);
    }
}

static void run_link_speed(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst)
{
    CLOCK_READ ck0, ck1;
    LINK_OBS pre, att, s1, s2;
    QUERY_READ finq;
    STATS_READ st_att, st_fin;
    STATE_READ tas;
    MIB_IF_ROW2 pre_os, fin_os;
    DWORD pre_err, fin_err, start;
    HANDLE h;
    BOOL cycled, returned, pre_connected;
    AVB_VERDICT v1, v2;
    FINAL_FINDINGS ff;
    char reason[900], r2[400];

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

    /* FINAL: bound, link as before the test, TAS clean, no fault; a difference is a cleanup defect, not a functional verdict.
     * Each requirement is judged on its own valid measurement first (r4619159432): an observed violation stays a violation
     * whatever else could not be read; only with no violation does a missing measurement make the outcome not verified. */
    fin_err = read_if_raw(id->net_luid, &fin_os);
    printf("    [EVIDENCE] FINAL OS: err=%lu state=%s tx=%llu rx=%llu bit/s\n", fin_err, media_name(fin_os.MediaConnectState),
           fin_os.TransmitLinkSpeed, fin_os.ReceiveLinkSpeed);
    query(h, &finq);
    print_query("FINAL", &finq);
    read_state(h, &tas);
    read_stats(h, &st_fin);
    read_clock(h, &ck1);   /* after cleanup: the test's last operation on the adapter */
    print_clock("after cleanup", &ck1);
    CloseHandle(h);
    ZeroMemory(&ff, sizeof(ff));
    /* bound: established, the handle was bound to this NET_LUID after the adapter returned with the same identity */
    if (pre_err != NO_ERROR || fin_err != NO_ERROR) {           /* link state and rate as before the test */
        final_missing(&ff, "link state: GetIfEntry2 before err=%lu, after err=%lu", pre_err, fin_err);
    } else if (fin_os.MediaConnectState != pre_os.MediaConnectState || fin_os.TransmitLinkSpeed != pre_os.TransmitLinkSpeed) {
        final_violation(&ff, "link %s %llu -> %s %llu bit/s", media_name(pre_os.MediaConnectState), pre_os.TransmitLinkSpeed,
                        media_name(fin_os.MediaConnectState), fin_os.TransmitLinkSpeed);
    }
    link_final_query(&finq, id->net_luid, &ff);                 /* TAS query clean and readable, field by field */
    if (!tas.ok) {                                              /* TAS record CLEAN */
        final_missing(&ff, "TAS record: GET_TAS_STATE unavailable");
    } else if (tas.s.armed != AVB_TAS_STATE_CLEAN) {
        final_violation(&ff, "TAS record armed=%u, not CLEAN", tas.s.armed);
    }
    if (!st_att.ok || !st_fin.ok) {                             /* HardwareFaults unchanged from ATTACH to FINAL */
        final_missing(&ff, "HardwareFaults: statistics at ATTACH %s, at FINAL %s", st_att.ok ? "read" : "unavailable",
                      st_fin.ok ? "read" : "unavailable");
    } else if (st_att.s.HardwareFaults != st_fin.s.HardwareFaults) {
        final_violation(&ff, "HardwareFaults %llu -> %llu", st_att.s.HardwareFaults, st_fin.s.HardwareFaults);
    }
    /* The verdict line: a bounded summary; the outcome comes from the counts alone. Capacity: FINAL_SUMMARY_VIOLATIONS and
     * FINAL_SUMMARY_MISSING have FINAL_SUMMARY_FIXED characters or fewer besides their %u conversions; each %u prints at most
     * 10 digits (UINT_MAX 4294967295), so a summary needs at most FINAL_SUMMARY_FIXED + 2 * 10 + 1 bytes, which the
     * compile-time check below holds against sizeof(reason). */
    C_ASSERT(sizeof(FINAL_SUMMARY_VIOLATIONS) - 1 - 4 <= FINAL_SUMMARY_FIXED && sizeof(FINAL_SUMMARY_MISSING) - 1 - 2 <= FINAL_SUMMARY_FIXED &&
             FINAL_SUMMARY_FIXED + 2 * 10 + 1 <= sizeof(reason));
    if (ff.violations != 0) {
        sprintf_s(reason, sizeof(reason), FINAL_SUMMARY_VIOLATIONS, ff.violations, ff.missing);
        verdict("CLEANUP_FAILED", "FINAL", index, id->device_id, reason);
    } else if (ff.missing != 0) {
        sprintf_s(reason, sizeof(reason), FINAL_SUMMARY_MISSING, ff.missing);
        verdict("INCONCLUSIVE_RESTORATION", "FINAL", index, id->device_id, reason);
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
    if (g_invalid_mode) {   /* #368 amendment: no fixed observer, no IPv4 or link veto for the checks that judge the query */
        run_invalid(index, id, inst);
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
    if (DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) &&
        br >= sizeof(version)) {   /* PR #479 r4621508804: only a complete reply carries Flags; otherwise the build stays unknown */
        printf("    [EVIDENCE] driver version %u.%u flags=0x%04X (%s build)\n", version.Major, version.Minor, version.Flags,
               (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) ? "Debug" : "Release");
        g_driver_build = (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) ? 1 : 0;
    } else {
        printf("    [EVIDENCE] GET_VERSION: no complete reply (%lu of %u bytes); driver build not classified\n", br, (unsigned)sizeof(version));
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
    g_all = all;
    g_nall = nall;
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
        /* --link-speed and --invalid run every TAS adapter: their runner budgets scale with the enumerated count, uncapped
         * (#472 review r4619159471; #368 amendment "all supported instances") */
        if (g_link_mode || g_invalid_mode || i < TAS_QUERY_CLEAN_MAX_ADAPTERS) {
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
