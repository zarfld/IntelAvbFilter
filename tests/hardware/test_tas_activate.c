/**
 * @file test_tas_activate.c
 * @brief IOCTL_AVB_TAS_ACTIVATE on a real adapter: first gate schedule, wire behaviour, state, refusal, cleanup (#357)
 *
 * One mode per run, each its own registered runner entry:
 *   --hold   TEST-TAS-API-004 (#369, Verifies #357 and #350), case TC-TASAPI-004, per #369's amendments of 2026-11-18 (P2 step 5)
 *            and 2026-11-19 (oracle: executable accepted-unverified HOLD) against #365 13.1-13.4 and its published R1/R2/R4/R5/R7
 *            (#352 R1, #351, #362): the versioned AVB_TAS_OP_REQUEST (144 bytes, version 1) on IOCTL_AVB_TAS_ACTIVATE_80C
 *            (0x0017A030). Each request carries a distinct non-zero client_op_id, the bound net_luid, pre_activation HOLD,
 *            debug_fault 0, AVB_TAS_F_ACCEPT_UNVERIFIED (#362: it authorizes execution of an unqualified HOLD control, never
 *            HARDWARE_VERIFIED) and the generation and epoch of the test's own query taken immediately before the race. When that
 *            query is not valid the proved pre-state's pair is sent as the last observed one and printed NOT measured; 0 = not
 *            reported, never a wildcard and never fabricated. The request carries no link speed: that query's driver-owned
 *            link_speed_mbps (#351 R2) and limits.tier_hold are measured facts, printed. Checks of this mode (the legacy lines
 *            below describe --pass and --fault):
 *              ACCESS      a read-only bound handle: STATUS_ACCESS_DENIED (raw NTSTATUS) with no output on 0x80C, GET_TAS_STATE
 *                          still served.
 *              METADATA    the query before the race reports a non-zero generation and epoch (#352 R1 L13): 0 is FAIL (an
 *                          implementation RED, never a valid control), a query that is not valid INCONCLUSIVE (not measured). Its own
 *                          line; the race still runs with what was measured.
 *              COMMIT      two bound open instances race the same schedule (tas_hold_commit, TC-MOCK-062): exactly one
 *                          STATUS_SUCCESS with Information 144, bytes 0-135 equal to what it sent, reserved1 0, no out_flags bit but
 *                          bit 0, and AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE exactly when acceptance acted (#365 R4): tier
 *                          IMPLEMENTED_UNVERIFIED bit 0, IMPLEMENTED_VERIFIED 0; the other STATUS_DEVICE_BUSY or
 *                          STATUS_INVALID_DEVICE_STATE with Information 0. With the tier not reported, or the query not valid, the
 *                          output is printed and COMMIT is INCONCLUSIVE, never PASS. FAIL: STATUS_INVALID_DEVICE_REQUEST (0x80C not
 *                          served, never SKIP); an incomplete success or a wrong output; two successes; a success of a request
 *                          carrying 0; a tier refusal STATUS_REQUEST_NOT_ACCEPTED of the accepted control (R4: the flag acts);
 *                          STATUS_IO_TIMEOUT or STATUS_IO_DEVICE_ERROR (R5 BASE_PASSED / COMMITTED_UNTIMED, named; a shared status
 *                          is no outcome detail); both refused STATE with a 0 sent (the zero metadata named as the likely cause,
 *                          never an excuse); a STATE refusal with an established pair unchanged across the race; NOT_READY at a
 *                          speed measured unchanged and non-zero around it; any other completion or output. INCONCLUSIVE: a
 *                          completion not captured; a STATE refusal with the pair changed or not measured around the race; NOT_READY
 *                          with the speed unknown, changed or not measured; each measurement printed. Observed violations are kept
 *                          when another measurement is missing.
 *              STATE       GET_TAS_STATE ARMED with the requested gate list. REFUSE: a further accepted activation on the armed
 *                          adapter is STATUS_INVALID_DEVICE_STATE with Information 0 and no reported state change. Both need a
 *                          committed schedule (exactly one STATUS_SUCCESS with the 144-byte reply; its output is COMMIT's to judge);
 *                          without one they are BLOCKED. The further request carries the generation and epoch of the query after
 *                          the race (BLOCKED, not sent, when that query fails). NOT_CLEAN and STATE_CONFLICT share that status
 *                          (#365 13.3, 13.4), so a refusal is PASS only when that query reports a non-zero generation and epoch;
 *                          with 0 (not reported) it is INCONCLUSIVE. Acceptance, output or a changed schedule is FAIL.
 *              GENERATION  (tas_hold_generation, TC-MOCK-063; #352 R1 L2/L7/L9) the query after the race reports the generation of
 *                          the query before it plus one per commit and none per refusal before any write, in the same context
 *                          (epoch unchanged). A 0 or a query not valid before or after, a changed epoch, or a completion whose write
 *                          attempt is not established (not captured; a status shared by outcomes before and after a write, R5/R8)
 *                          is INCONCLUSIVE, named; a decrease is FAIL.
 *              LOOKUP      evidence only: a query carrying the committed client_op_id, on the committing open instance (#365
 *                          13.1), prints lookup_result (the producer stage expects NOT_IMPLEMENTED; success-record retrieval is
 *                          #460, never asserted here).
 *              WIRE, STATUS, CLEANUP, RESTORE  as below; WIRE needs a committed schedule and an established observer.
 *            The competing request carries the same schedule, so after a commit its own hardware effect cannot be separated from
 *            the winner's: the query after the race is printed as evidence of one schedule, not as proof that the refusal wrote
 *            nothing. Expected RED against a driver without the 0x80C producer: COMMIT FAIL (not served), METADATA FAIL while the
 *            query reports 0, GENERATION INCONCLUSIVE (not reported), the dependent lines BLOCKED, ACCESS PASS (the I/O manager
 *            refuses the read-only handle whatever the handler). Passing COMMIT does not establish K2 (#352), and an accepted
 *            unverified commit is not HARDWARE_VERIFIED.
 *   --pass   TEST-TAS-API-005 (#370, Verifies #357), case TC-TASAPI-005, per its amendment of 2026-11-19: the 144-byte
 *            version-1 AVB_TAS_OP_REQUEST on IOCTL_AVB_TAS_ACTIVATE_80C (0x0017A030) with pre_activation PASS (2), debug_fault 0,
 *            flags 0, a distinct non-zero client_op_id, the bound net_luid and the generation and epoch of the test's own query
 *            (0 = not reported, printed as such, never fabricated), the HOLD schedule and the same BaseT lead. Function 71 is not
 *            called by this mode (its withdrawal is #368's LEGACY_FUNCTION_71).
 *              ACCESS   as for --hold: a read-only bound handle gets STATUS_ACCESS_DENIED on 0x80C, GET_TAS_STATE still served.
 *              COMMIT   under #365's R7 order a header-valid PASS completes STATUS_NOT_IMPLEMENTED, Information 0, before the
 *                       metadata, schedule, link and tier checks and before any register or PHC access, until ADR-TSN-003
 *                       (#363). Measured against the proved-clean pre-state: the TAS record stays CLEAN, the restoration set
 *                       (DES 12.5) stays as captured register by register (a register unread in either reply is a missing
 *                       measurement and never suppresses the comparison of the others, PR #484), and a reported generation
 *                       stays unchanged (0 is neither a value nor
 *                       evidence of no change). SKIP_NOT_IMPLEMENTED only for that completion with every applicable
 *                       observation established, never PASS. STATUS_INVALID_DEVICE_REQUEST (0x80C not served), any other
 *                       completion or output, or a measured change is FAIL, also STATE_CONFLICT with zero metadata (PASS
 *                       is recognized first under R7). Expected completion without an observed change but with a
 *                       measurement missing is INCONCLUSIVE, each missing measurement named; observed violations are kept
 *                       even when other measurements are missing. STATE, REFUSE and WIRE do not apply.
 *            No link is required by the request (it carries no link speed); the cleanup's TX probe still needs one.
 *   --fault  the failed-rollback path of #357 (PR #397 review), case TC-TASACT-FAULT-001. Debug drivers fail schedule write
 *            13 and the first restore write on request (devices/i226_tas_activate.h); Release drivers must refuse that request.
 *            LEGACY function-71 regression, kept executable until the producer's atomic cutover (#482, #357).
 *   --fault-versioned  TEST-TASACT-FAULT-001 (#482, Verifies #357 and #352), cases TC-TASACT-FAULT-001-F1 and -F2 on the 144-byte
 *            version-1 AVB_TAS_OP_REQUEST on IOCTL_AVB_TAS_ACTIVATE_80C (0x0017A030): HOLD, the bound NET_LUID, a distinct non-zero
 *            client_op_id, generation and epoch from the test's own query (0 printed "(not reported)", never fabricated),
 *            AVB_TAS_F_ACCEPT_UNVERIFIED (#362/#351/#365: it authorizes execution of an unqualified HOLD control, not
 *            HARDWARE_VERIFIED). The common numbered-write contract (#482): N = 1 is the first schedule write for every backend;
 *            no commit-write index is assumed. Per TAS adapter F1 then F2, each with its own handles, proved pre-state (recovered
 *            once by the owned NIC cycle when not proved), observer trial and owned NIC-cycle cleanup with its CLEANUP/RESTORE lines.
 *            A complete GET_VERSION reply classifies the driver: only a Debug driver gets a non-zero debug_fault (the Release
 *            refusal is #368's negative control, not claimed here); an unknown build sends nothing.
 *              METADATA    (both cases) the control needs a valid, current, non-zero generation/epoch pair from the test's own
 *                          pre-state query; without it the case is nonpassing (FAIL) and stays visible, whatever the request
 *                          completes with. The request is still sent with the query's values, never fabricated ones.
 *              F1  debug_fault = N 1, restore not failed (bit 8 clear):
 *                  COMPLETION  STATUS_IO_DEVICE_ERROR, Information 0 (FAILED_RESTORED 3 is the record's outcome; error completions
 *                              carry no output, record retrieval is #352/#379/#460 and not inferred from the status).
 *                  ROLLBACK    every measured restoration-set register equal to the test's own pre-state query; the record CLEAN
 *                              when restoration is established (#365 R8: a failure before the transmit-mode write preserves the
 *                              previous state if restoration is established). A measured difference is FAIL whatever else is
 *                              unread; a non-CLEAN record is FAIL once the registers establish restoration, or when no write was
 *                              attempted. (After a mode write reached hardware a later failure stays DIRTY even with matching
 *                              read-back, #362 decision 4 - not reachable with N = 1.)
 *                  GENERATION  advanced exactly once when reported before and after; 0 is not a value (INCONCLUSIVE).
 *              F2  debug_fault = N 1 | AVB_TAS_DEBUG_FAULT_RESTORE:
 *                  COMPLETION  STATUS_UNSUCCESSFUL, Information 0 (FAILED_NOT_RESTORED 4 is the record's outcome).
 *                  STATE       the record DIRTY (GET_TAS_STATE): the deliberately failed restore leaves restoration unestablished
 *                              (#365 R8, #362 decisions 4 and 5).
 *                  REFUSE      a further otherwise-valid HOLD (debug_fault 0, a new client_op_id) with the post-failure query's
 *                              metadata: STATUS_INVALID_DEVICE_STATE, Information 0, record and restoration set unchanged. #369's
 *                              rule: with reported metadata PASS; with generation or epoch 0 INCONCLUSIVE (NOT_CLEAN cannot be told
 *                              from STATE_CONFLICT); acceptance, output or a measured change FAIL.
 *            A STATUS_INVALID_DEVICE_STATE refusal (no output) of a request carrying the query's unreported metadata (generation or
 *            epoch 0) leaves only COMPLETION's fault-path attribution INCONCLUSIVE: 0 is never a valid value and is refused
 *            STATE_CONFLICT before any write (#352 L2, #365 13.6 D.1), so it does not show that the injected fault ran. The missing
 *            metadata stays the separate METADATA failure; with reported metadata that refusal is FAIL. A refusal never proves
 *            injection or rollback, and no generation advance is expected for a refusal before any write. #369's ambiguity rule
 *            applies to F2's subsequent REFUSE probe only.
 *            0x80C absent (STATUS_INVALID_DEVICE_REQUEST) is FAIL, never SKIP, and the experiment is not suppressed: every
 *            independently measurable result is still taken and reported, and a check whose fault path was not reached is
 *            INCONCLUSIVE (not exercised), never PASS, unless a measured change makes it FAIL. Coverage limits (#482): N = 1 only;
 *            no late-write, partial-schedule, PHC-recheck or read-back-failure control.
 *
 * Pre-state and setup (#369 "pre-state proved by the test itself through the TAS query"; #365 13.7, tests own setup): before
 * TAS_ACTIVATE the driver record is CLEAN (GET_TAS_STATE) and IOCTL_AVB_TAS_QUERY_HW returns a readable, clean reply (hw_read
 * READ, no register invalid, TSN mode off, both slots NONE, restoration COMPLETE). A pre-state that is not clean is not a veto:
 * the test recovers it once through its own NIC cycle, the recovery its cleanup uses, and proves it again. Only a pre-state
 * still not proved after that recovery is BLOCKED, with both observations printed. The proved reply is the restoration reference.
 * The recovery's own outcome is a separate SETUP line: CLEANUP_FAILED when the cycle did not complete or left observed residue
 * (record not CLEAN, or a readable query not clean), INCONCLUSIVE_RESTORATION when the state after it could not be measured. An
 * abort during the recovery cycle reports SETUP INCONCLUSIVE_RESTORATION with the operator action (report_abort): the recovery
 * is tracked from before the first disable call until its final state is verified or its failure recorded. A blocked adapter
 * does not stop the others; each runs its own setup and cleanup.
 *
 * Checks, each with its own verdict line <case>-<check>:
 *   ACCESS   a handle opened for reading only is refused TAS_ACTIVATE (FILE_WRITE_ACCESS, #362) and still serves GET_TAS_STATE.
 *            The request is invalid, so nothing is written even if the access check were missing.
 *   COMMIT   two bound handles race the same request: exactly one COMMITTED, the other BUSY or NOT_CLEAN (ADR-TSN-002).
 *   STATE    GET_TAS_STATE reports ARMED and the windows as the gate list it must report (#350, PR #397 review).
 *   REFUSE   a further activation on the armed adapter is NOT_CLEAN and changes no reported state.
 *   WIRE     tagged broadcast seen by the established wire observer (see OBSERVER), judged by tests/ioctl/tas_activate_eval.h against
 *            BaseT and the window.
 *   STATUS   (#369 only) PENDING then INFERRED_ACTIVE: #350's status model is not implemented, so SKIP_NOT_IMPLEMENTED.
 *   CLEANUP  the test's own NIC cycle ran and the adapter came back: primary cleanup, owned by this test.
 *   RESTORE  restoration as #361 states it (tas_act_restore in tests/ioctl/tas_activate_eval.h): restoration-set registers at
 *            baseline, TSN mode off, driver record clean, TX on the wire. The restoration set is DES-C-TAS-010 12.5's: the v1
 *            register set without DTXMXPKTSZ, BASET_L and BASET_H included. It is read back through IOCTL_AVB_TAS_QUERY_HW after the
 *            owned NIC cycle and compared register by register with the test's own pre-state reply, under the validity rule of
 *            test_tas_query_clean --restore (#377). Without a valid set before and after, the registers stay UNVERIFIED and
 *            restoration is at best INCONCLUSIVE_RESTORATION. It is never claimed from GET_TAS_STATE or from the driver's advisory
 *            restoration field. TX on the wire is judged only from tagged frames sendto accepted after the cycle (seen ratio,
 *            no long delay); unrelated captured traffic never decides it. Every attempt failing in sendto is an observed
 *            transmission failure; nothing attempted leaves the wire unmeasured. PHC continuity across the cycle is a separate
 *            evidence line, never a restoration input (#361).
 *   HOLD characterization  (--hold, evidence only, no verdict) the measurements a HOLD gate-window assertion needs: the raw
 *            tagged frames sent and seen by the capture after the activation (lost = sent - seen), the baseline's own sent/seen
 *            (the capture's loss before any mutation), sendto failures of both and the restoration class; and, only when every
 *            such frame was mapped to
 *            PHC time, the early, released, inside, near-edge and outside counts (otherwise reported unavailable, never as zeros).
 *            Limits, printed with it:
 *            the test cannot observe the driver's register write order (#365 13.5 records the source order); the capture is a
 *            passive, software-timestamped Npcap capture on the host clock through a switch, mapped to PHC time by two bracketed
 *            anchors; arrivals within TAS_ACT_EDGE_MARGIN_NS of an edge decide nothing; low frame rate (SEND_PERIOD_MS). So 0
 *            outside here does not by itself prove that no frame left outside a hardware window, and a WIRE PASS tolerates loss
 *            down to TAS_ACT_SEEN_MIN_PERCENT. These observations are not EG-02 qualification or a tier claim on their own.
 *
 * Schedule: 1 s cycle, one window 200 to 400 ms on all four queues, BaseT the first cycle boundary at least 1.5 s after the
 * PHC (or limits.min_margin_ns plus 500 ms when the query establishes a larger margin). That is the verified set of the E6
 * characterisation (LOCAL_DIAGNOSTIC, I226 0x125B REV_04, Debug), within the window rule of #357. Adapters are chosen by
 * INTEL_CAP_TSN_TAS; every TAS adapter runs (no fixed inventory: the identities are allocated from the driver's count).
 * OBSERVER (#369 amendment, #463): no fixed observer model and no execution veto. Every other enumerated adapter is tried,
 * without a count limit and one evidence line each, as the passive capture until one receives the baseline frames the adapter
 * under test sends. Without one, or without a link and an IPv4 address, WIRE and the cleanup's TX probe are not measured and the
 * other checks still run (--pass and --fault still need a connected link: their legacy request carries a caller link speed).
 * The PCI revision and the loaded driver image with its SHA256 are printed as evidence, never used for selection.
 *
 * Mutations: TAS_ACTIVATE, bounded broadcast UDP and ICMP on the adapter under test, and the NIC cycle of that adapter
 * (CM_Disable_DevNode / CM_Enable_DevNode), which resets its PHC. The cycle runs after every TAS_ACTIVATE issued, whatever
 * it returned, and once before it when the pre-state needs recovery. Dedicated IOCTLs only: no READ_REGISTER, no
 * WRITE_REGISTER. Runs on Debug and Release drivers.
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
#include "../ioctl/tas_activate_eval.h"
#include "../common/avb_ntstatus.h"   /* exact NTSTATUS: STATUS_NOT_IMPLEMENTED and an unknown IOCTL share Win32 error 1 */
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
#define SEND_BOUND_MS        66000                   /* the send phase ends here even if the PHC cannot be read; covers the
                                                        largest admitted lead (LEAD_MAX_NS, C_ASSERT below the lead bounds) */
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

typedef enum { MODE_NONE = 0, MODE_HOLD, MODE_PASS, MODE_FAULT, MODE_FAULTV } RUN_MODE;

/* #482: the common numbered-write contract - N = 1 is the first schedule write for every backend (published, not a commit index) */
#define FAULTV_FIRST_SCHEDULE_WRITE 1u

static RUN_MODE g_mode;
static int g_fault_case;                             /* --fault-versioned: 1 = F1, 2 = F2 (the run_adapter call in progress) */
static const char *g_case;
static int g_failed, g_blocked, g_ran;   /* g_ran: case executions (run_adapter calls); --fault-versioned runs F1 and F2 per adapter */
static int g_tas_adapters;                /* physical TAS-capable adapters run, counted once each (PR #489) */
static avb_u64 g_capture_luid;
static unsigned g_next_seq;
static LONGLONG g_send_us[WIRE_MAX_FRAMES];
static const AVB_ADAPTER_IDENTITY_REQUEST *g_all;   /* every validated identity: the observer candidates */
static UINT32 g_nall;
static BOOL g_cleanup_link_was_up = TRUE;            /* cleanup: the link before the test (an adapter down before is not a failure) */
static avb_u64 g_op_id;                              /* client_op_id of the next versioned request: non-zero, distinct */

#define LEAD_HEADROOM_NS     (500LL * NS_PER_MS)    /* test scheduling headroom above an established limits.min_margin_ns */
#define LEAD_MAX_NS          (60LL * NS_PER_SEC)    /* a reported margin needing more lead than this is not used (BLOCKED) */
/* PR #481: BaseT is at most LEAD_MAX_NS plus one cycle (rounding to a boundary) after the PHC read, so the send phase reaches
 * BaseT + SEND_CYCLES_AFTER cycles and a half within SEND_BOUND_MS for every admitted lead, and its frames, the observer
 * baseline and the cleanup's TX probe fit the capture's tag slots */
C_ASSERT((long long)SEND_BOUND_MS * NS_PER_MS >= LEAD_MAX_NS + CYCLE_NS + SEND_CYCLES_AFTER * CYCLE_NS + CYCLE_NS / 2);
C_ASSERT(SEND_BOUND_MS / SEND_PERIOD_MS + 1 + 2 * (BASELINE_MS / SEND_PERIOD_MS + 1) <= WIRE_MAX_FRAMES);

/* Non-negative while the test owns an unreported state change of an adapter: from its setup recovery NIC cycle until that
 * recovery is verified or its failure reported (g_pending_setup 1), and from TAS_ACTIVATE until the cleanup has reported */
static volatile LONG g_pending_adapter = -1;
static volatile LONG g_pending_did;
static volatile LONG g_pending_setup;

static void report_abort(void)
{
    LONG adapter = InterlockedExchange(&g_pending_adapter, -1);
    if (adapter >= 0 && InterlockedCompareExchange(&g_pending_setup, 0, 0) != 0) {
        printf("[INCONCLUSIVE_RESTORATION] %s-SETUP/adapter:%ld did=0x%04lX: test aborted during its setup recovery NIC cycle, before any "
               "TAS_ACTIVATE; the adapter may be left disabled; operator: NIC cycle of this adapter, then TC-TSNREG-001\n",
               g_case, adapter, g_pending_did);
        fflush(stdout);
    } else if (adapter >= 0) {
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

#define SEND_OK        1    /* sendto accepted the frame: it has a sequence number and a send time */
#define SEND_FAILED    0    /* sendto failed: an observed failure to transmit (g_last_send_error) */
#define SEND_NO_SLOT   (-1) /* no capture slot left (WIRE_MAX_FRAMES): nothing was attempted, nothing measured */

static int g_last_send_error;

/** One tagged broadcast frame; its host send time is kept by sequence number. Returns SEND_OK, SEND_FAILED or SEND_NO_SLOT */
static int send_tagged(SOCKET sock, unsigned nonce)
{
    static unsigned char payload[PAYLOAD_BYTES];
    SOCKADDR_IN peer = {0};

    if (g_next_seq >= WIRE_MAX_FRAMES) {
        return SEND_NO_SLOT;
    }
    peer.sin_family = AF_INET;
    peer.sin_port = htons(TAS_WIRE_UDP_PORT);
    peer.sin_addr.s_addr = INADDR_BROADCAST;
    tas_wire_fill(payload, sizeof(payload), nonce, g_next_seq);
    g_send_us[g_next_seq] = host_us();
    if (sendto(sock, (const char *)payload, (int)sizeof(payload), 0, (SOCKADDR *)&peer, sizeof(peer)) == SOCKET_ERROR) {
        g_last_send_error = WSAGetLastError();
        return SEND_FAILED;
    }
    g_next_seq++;
    return SEND_OK;
}

/** Sends for ms, drains, and returns how many of these frames the observer saw. sent: frames sendto accepted; failed: sendto
 *  failures; delayed: seen later than UNGATED_DELAY_US */
static unsigned send_window(SOCKET sock, unsigned nonce, DWORD ms, unsigned *sent, unsigned *failed, unsigned *delayed)
{
    unsigned first = g_next_seq, seq, seen = 0;
    DWORD start = GetTickCount();

    *delayed = 0;
    *failed = 0;
    while (GetTickCount() - start < ms) {
        if (send_tagged(sock, nonce) == SEND_FAILED) {
            (*failed)++;
        }
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

/* ---- IOCTL_AVB_TAS_QUERY_HW read-back (#365 12.1-12.5). The same rules as tests/hardware/test_tas_query_clean.c (PRESTATE and
 *      --restore, #368/#377), written again here for this test's pre-state and restoration rather than shared ---- */
typedef struct { LONG status; DWORD got; AVB_TAS_QUERY_HW_REQUEST q; } QUERY_READ;

/** One query on a bound handle: in = version, size, lookup id 0. Keeps the raw NTSTATUS and IoStatus.Information */
static void query(HANDLE h, QUERY_READ *r)
{
    ZeroMemory(r, sizeof(*r));
    r->q.version = AVB_TAS_QUERY_VERSION;
    r->q.size = (avb_u32)sizeof(r->q);
    r->status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_QUERY_HW, &r->q, sizeof(r->q), &r->q, sizeof(r->q), &r->got);
}

/** A reply whose fields mean something: STATUS_SUCCESS with the full reply */
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
        printf("    query %s: no valid reply, its fields are not reported\n", label);
        return;
    }
    printf("    query %s: hw_read=%u hw_mode=%u current=%u/0x%X next=%u/0x%X snapshot=0x%X restoration=%u flags=0x%X link=%u "
           "epoch=%llu gen=%llu phc=[%llu, %llu]\n", label, q->hw_read, q->hw_mode, q->current_status, q->current_reasons,
           q->next_status, q->next_reasons, q->snapshot_reasons, q->restoration, q->flags, q->link_speed_mbps, q->context_epoch,
           q->generation, q->phc_before_ns, q->phc_after_ns);
    printf("    regs %s: TQAVCTRL=0x%08X BASET_L=0x%08X BASET_H=0x%08X QBVCYCLET=0x%08X QBVCYCLET_S=0x%08X DTXMXPKTSZ=0x%08X "
           "read_failed=0x%X consistency=0x%X\n", label, g->tqavctrl, g->baset_l, g->baset_h, g->qbvcyclet, g->qbvcyclet_s,
           g->dtxmxpktsz, g->read_failed, g->consistency);
    for (i = 0; i < 4; i++) {
        printf("    regs %s: q%d STQT=0x%08X ENDQT=0x%08X TXQCTL=0x%08X\n", label, i, g->stqt[i], g->endqt[i], g->txqctl[i]);
    }
}

/* read_failed bits 0 to 16 of AVB_TAS_REGS (include/avb_ioctl.h): the restoration set of DES-C-TAS-010 12.5. DTXMXPKTSZ is
 * bit 17 and not a restoration input; the PHC bracket is not an input either. The rule of test_tas_query_clean --restore (#377) */
#define TAS_RESTORATION_SET_BITS ((1u << 17) - 1u)

/** The reply observed every restoration-set register: hw_read READ, or PARTIAL because only DTXMXPKTSZ or a PHC read failed */
static BOOL restoration_set_valid(const QUERY_READ *r)
{
    return query_valid(r) && (r->q.hw_read == AVB_TAS_HWREAD_READ || r->q.hw_read == AVB_TAS_HWREAD_PARTIAL) &&
           (r->q.regs.read_failed & TAS_RESTORATION_SET_BITS) == 0;
}

static unsigned reg_differs(const char *name, int queue, avb_u32 before, avb_u32 after)
{
    if (before == after) {
        return 0;
    }
    if (queue < 0) {
        printf("    [EVIDENCE] restoration set: %s before=0x%08X after=0x%08X\n", name, before, after);
    } else {
        printf("    [EVIDENCE] restoration set: %s%d before=0x%08X after=0x%08X\n", name, queue, before, after);
    }
    return 1;
}

/** The restoration set of DES-C-TAS-010 12.5: the v1 register set without DTXMXPKTSZ, BASET_L and BASET_H included. Counts and
 *  prints every register that differs between the two replies */
static unsigned restoration_set_differing(const AVB_TAS_REGS *a, const AVB_TAS_REGS *b)
{
    unsigned n = 0;
    int i;

    n += reg_differs("TQAVCTRL", -1, a->tqavctrl, b->tqavctrl);
    n += reg_differs("BASET_L", -1, a->baset_l, b->baset_l);
    n += reg_differs("BASET_H", -1, a->baset_h, b->baset_h);
    n += reg_differs("QBVCYCLET", -1, a->qbvcyclet, b->qbvcyclet);
    n += reg_differs("QBVCYCLET_S", -1, a->qbvcyclet_s, b->qbvcyclet_s);
    for (i = 0; i < 4; i++) {
        n += reg_differs("STQT", i, a->stqt[i], b->stqt[i]);
        n += reg_differs("ENDQT", i, a->endqt[i], b->endqt[i]);
        n += reg_differs("TXQCTL", i, a->txqctl[i], b->txqctl[i]);
    }
    return n;
}

/** The restoration set register by register (PR #484 r4626979931): every register whose read_failed bit is clear in both valid
 *  replies is compared and each difference printed and counted; every other register is counted and printed as unread. A
 *  register unread in one reply never suppresses the comparison of the others. Without two valid replies all 17 are unread */
static void restoration_set_compare(const QUERY_READ *a, const QUERY_READ *b, unsigned *differing, unsigned *unread)
{
    static const char *const names[17] = { "TQAVCTRL", "BASET_L", "BASET_H", "QBVCYCLET", "QBVCYCLET_S", "STQT0", "STQT1", "STQT2",
                                           "STQT3", "ENDQT0", "ENDQT1", "ENDQT2", "ENDQT3", "TXQCTL0", "TXQCTL1", "TXQCTL2", "TXQCTL3" };
    avb_u32 va[17], vb[17], failed;
    unsigned bit;
    int i;

    *differing = 0;
    *unread = 0;
    if (!query_valid(a) || !query_valid(b)) {
        *unread = 17;
        printf("    [EVIDENCE] restoration set: no valid query reply %s, none of the 17 registers measured\n",
               query_valid(a) ? "after" : "before");
        return;
    }
    va[0] = a->q.regs.tqavctrl; va[1] = a->q.regs.baset_l; va[2] = a->q.regs.baset_h; va[3] = a->q.regs.qbvcyclet;
    va[4] = a->q.regs.qbvcyclet_s;
    vb[0] = b->q.regs.tqavctrl; vb[1] = b->q.regs.baset_l; vb[2] = b->q.regs.baset_h; vb[3] = b->q.regs.qbvcyclet;
    vb[4] = b->q.regs.qbvcyclet_s;
    for (i = 0; i < 4; i++) {
        va[5 + i] = a->q.regs.stqt[i];   vb[5 + i] = b->q.regs.stqt[i];
        va[9 + i] = a->q.regs.endqt[i];  vb[9 + i] = b->q.regs.endqt[i];
        va[13 + i] = a->q.regs.txqctl[i]; vb[13 + i] = b->q.regs.txqctl[i];
    }
    failed = (a->q.regs.read_failed | b->q.regs.read_failed) & TAS_RESTORATION_SET_BITS;
    for (bit = 0; bit < 17; bit++) {
        if (failed & (1u << bit)) {
            (*unread)++;
            printf("    [EVIDENCE] restoration set: %s unread (%s%s%s), not compared\n", names[bit],
                   (a->q.regs.read_failed & (1u << bit)) ? "before" : "", ((a->q.regs.read_failed & b->q.regs.read_failed) & (1u << bit)) ? " and " : "",
                   (b->q.regs.read_failed & (1u << bit)) ? "after" : "");
        } else if (va[bit] != vb[bit]) {
            (*differing)++;
            printf("    [EVIDENCE] restoration set: %s before=0x%08X after=0x%08X\n", names[bit], va[bit], vb[bit]);
        }
    }
}

/**
 * Primary cleanup and restoration: NIC cycle, then everything this test can observe without raw register access. Closes h.
 * Restoration follows #361 (tas_act_restore): schedule registers, TSN mode, driver record and the wire decide it; the PHC is
 * reported separately and never decides it. The restoration set (DES 12.5, BASET_L/H included) and the TSN mode are read back
 * through IOCTL_AVB_TAS_QUERY_HW after the cycle and compared with the test's own pre-state reply (before); without a valid set on
 * both sides they are unmeasured and restoration cannot be COMPLETE.
 */
static TAS_ACT_RESTORE_CLASS cleanup_and_verify(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, IN_ADDR ipv4,
                               const AVB_DRIVER_STATISTICS *stats_before, long long phc_before, LARGE_INTEGER qpc_before,
                               const QUERY_READ *before)
{
    TAS_ACT_RESTORE_EVIDENCE e;
    TAS_ACT_RESTORE_CLASS restore;
    DEVINST inst = 0;
    HANDLE fresh;
    MIB_IF_ROW2 row;
    DWORD start;
    BOOL link_up = FALSE, attach = FALSE;
    long long phc_after = 0, wall_ns = 0;

    ZeroMemory(&e, sizeof(e));
    e.phc = TAS_ACT_PHC_UNMEASURED;
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

        start = GetTickCount();
        while (GetTickCount() - start < RETURN_BOUND_MS) {
            if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
                link_up = TRUE;
                break;
            }
            Sleep(1000);
        }
        Sleep(SETTLE_MS);
        if (read_stats(fresh, &stats_after)) {   /* the statistics live in the adapter context: lower means a new one */
            attach = stats_after.IoctlCount < stats_before->IoctlCount;
            printf("    IoctlCount %llu -> %llu\n", stats_before->IoctlCount, stats_after.IoctlCount);
        }
        if (read_state(fresh, &s)) {
            e.driver_clean = (s.armed == AVB_TAS_STATE_CLEAN);
            print_state("after the cycle", &s);
        } else {
            printf("    GET_TAS_STATE after the cycle: unreadable\n");
        }
        {   /* Read back through the TAS query and compared with the test's own pre-state reply, register by register; never
             * assumed, and never taken from the driver's advisory restoration field */
            QUERY_READ after;
            query(fresh, &after);
            print_query("after the cycle", &after);
            e.regs_measured = restoration_set_valid(&after) && restoration_set_valid(before);
            if (e.regs_measured) {
                e.regs_differing = restoration_set_differing(&before->q.regs, &after.q.regs);
                e.tsn_mode_off = after.q.hw_mode == AVB_TAS_HWMODE_OFF;
                printf("    [EVIDENCE] restoration set (DES 12.5, BASET_L/H included): %u of 17 registers differ from the pre-state query\n",
                       e.regs_differing);
            } else {
                printf("    [EVIDENCE] restoration set after the cycle: UNVERIFIED, no valid restoration set in the query before and after the cycle\n");
            }
            if (query_valid(&after) && query_valid(before) && !(after.q.regs.read_failed & ~TAS_RESTORATION_SET_BITS) &&
                after.q.regs.dtxmxpktsz != before->q.regs.dtxmxpktsz) {
                printf("    [EVIDENCE] DTXMXPKTSZ before=0x%08X after=0x%08X: outside the restoration set (DES 12.5), reported only\n",
                       before->q.regs.dtxmxpktsz, after.q.regs.dtxmxpktsz);
            }
        }
        if (read_phc(fresh, &phc_after)) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            wall_ns = (long long)((double)(now.QuadPart - qpc_before.QuadPart) * 1e9 / (double)freq.QuadPart);
            e.phc = tas_act_phc(TRUE, phc_after - phc_before, wall_ns, PHC_TOLERANCE_NS);
        }
        if (!link_up && !g_cleanup_link_was_up) {   /* no link before the test and none after: the wire is not measured */
            e.tx_probed = FALSE;
            printf("    TX after the cycle: not measured, the link was down before the test as well\n");
        } else if (!link_up) {
            e.tx_probed = TRUE;   /* the link was up before the test: no link now is an observed failure, not a gap */
            e.tx_on_wire = FALSE;
            printf("    TX after the cycle: link did not come back within %d ms\n", RETURN_BOUND_MS);
        } else {
            SOCKET sock = open_tx_socket(ipv4);
            unsigned nonce = GetTickCount() ^ (GetCurrentProcessId() << 8), sent = 0, failed = 0, delayed = 0, seen;
            LONG captured = 0, from_target = 0;
            BOOL attempted = sock != INVALID_SOCKET && g_capture_luid != 0;   /* no capture without an established observer */
            int observers = attempted ? wire_start(id->net_luid, g_capture_luid, id->mac, nonce, ipv4) : 0;

            if (observers != 0) {
                seen = send_window(sock, nonce, BASELINE_MS, &sent, &failed, &delayed);
                wire_counts(&captured, &from_target);
                wire_stop();
                printf("    TX after the cycle: sent=%u sendFailed=%u seen=%u delayedOver%lldms=%u captured=%ld\n", sent, failed, seen,
                       UNGATED_DELAY_US / 1000, delayed, captured);
                if (sent != 0) {
                    /* The ratio means something only for frames this test sent; unrelated captured traffic never decides it */
                    e.tx_probed = captured != 0;
                    e.tx_on_wire = (unsigned long long)seen * 100 >= (unsigned long long)sent * TAS_ACT_SEEN_MIN_PERCENT && delayed == 0;
                } else if (failed != 0) {
                    e.tx_probed = TRUE;   /* every attempt failed in sendto: an observed failure to transmit, not a gap */
                    e.tx_on_wire = FALSE;
                    printf("    TX after the cycle: every one of %u sends failed (last WSA error %d): observed transmission failure\n",
                           failed, g_last_send_error);
                } else {
                    e.tx_probed = FALSE;  /* nothing was attempted (no capture slot left): the wire was not measured */
                    e.tx_on_wire = FALSE;
                    printf("    TX after the cycle: not measured, no tagged frame was attempted (capture capacity)\n");
                }
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
    printf("    evidence: cycle=%d returned=%d attach=%d driverClean=%d regsMeasured=%d regsDiffering=%u tsnOff=%d link=%d txProbed=%d "
           "txOnWire=%d\n", e.cycle_completed, e.adapter_returned, attach, e.driver_clean, e.regs_measured, e.regs_differing,
           e.tsn_mode_off, link_up, e.tx_probed, e.tx_on_wire);
    restore = tas_act_restore(&e);
    /* A separate outcome (#361): the cycle reseeds the PHC, which is reported, not judged as restoration */
    printf("    [EVIDENCE] PHC across the cleanup: %s (before=%lld after=%lld wallNs=%lld)\n", tas_act_phc_name(e.phc), phc_before,
           phc_after, wall_ns);
    switch (restore) {
    case TAS_ACT_RESTORE_COMPLETE:
        verdict("PASS", "RESTORE", index, id->device_id,
                "restoration set (DES 12.5, BASET_L/H included) as in the pre-state query, TSN mode off, record clean, TX on the wire (#361)");
        break;
    case TAS_ACT_RESTORE_UNVERIFIED:
        verdict("INCONCLUSIVE_RESTORATION", "RESTORE", index, id->device_id,
                "nothing observed failed, but the restoration set (no valid query before and after) or the wire was not measured; see the evidence line");
        break;
    default:
        verdict("CLEANUP_FAILED", "RESTORE", index, id->device_id,
                "restoration incomplete: see the evidence line (cycle, return, driver record or TX on the wire failed)");
        break;
    }
    return restore;
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

/** OBSERVER (#369 amendment, #463): every other enumerated adapter is tried, without a count limit, as the passive capture until
 *  one receives the baseline frames the adapter under test sends. TRUE with that capture still running (g_capture_luid, *nonce) */
static BOOL find_observer(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, SOCKET sock, IN_ADDR ipv4, unsigned *nonce,
                          unsigned *sent, unsigned *failed, unsigned *seen)
{
    UINT32 o, tries = 0;
    unsigned delayed = 0;
    char reason[220];

    g_capture_luid = 0;
    for (o = 0; o < g_nall; o++) {
        int observers;
        if (g_all[o].net_luid == id->net_luid) {
            continue;
        }
        tries++;
        /* No capture is active here (none before the first candidate, wire_stop after each one), so every tag slot is free: each
         * candidate's baseline starts at slot 0 and the number of candidates never exhausts WIRE_MAX_FRAMES (PR #481) */
        g_next_seq = 0;
        *nonce = GetTickCount() ^ GetCurrentProcessId() ^ (o << 24);
        observers = wire_start(id->net_luid, g_all[o].net_luid, id->mac, *nonce, ipv4);
        if (observers != 0) {
            *sent = 0;
            *failed = 0;
            *seen = send_window(sock, *nonce, BASELINE_MS, sent, failed, &delayed);
            printf("    [EVIDENCE] OBSERVER candidate adapter:%u did=0x%04X: capture started, baseline sent=%u sendFailed=%u seen=%u\n",
                   g_all[o].index, g_all[o].device_id, *sent, *failed, *seen);
            if (*sent != 0 && (unsigned long long)*seen * 100 >= (unsigned long long)*sent * TAS_ACT_SEEN_MIN_PERCENT) {
                g_capture_luid = g_all[o].net_luid;
                sprintf_s(reason, sizeof(reason), "adapter:%u did=0x%04X received %u of %u baseline frames the adapter under test sent",
                          g_all[o].index, g_all[o].device_id, *seen, *sent);
                verdict("PASS", "OBSERVER", index, id->device_id, reason);
                return TRUE;
            }
        } else {
            printf("    [EVIDENCE] OBSERVER candidate adapter:%u did=0x%04X: capture not started\n", g_all[o].index, g_all[o].device_id);
        }
        wire_stop();   /* also after 0 observers: the ICMP worker may run */
    }
    sprintf_s(reason, sizeof(reason), "no other enumerated adapter received the baseline frames (%u candidate(s) tried, each listed above): "
              "WIRE and the cleanup's TX probe are not measured; the other checks still run", tries);
    verdict("BLOCKED", "OBSERVER", index, id->device_id, reason);
    return FALSE;
}

/* ---- --hold on the versioned request (#369 amendment, #365 13.1-13.4) ---- */
static avb_u64 next_op_id(void)
{
    if (g_op_id == 0) {
        g_op_id = ((avb_u64)GetCurrentProcessId() << 32) | (avb_u64)GetTickCount();
    }
    if (++g_op_id == 0) {
        g_op_id = 1;
    }
    return g_op_id;
}

/** One HOLD AVB_TAS_OP_REQUEST: the verified schedule set, the bound adapter, the metadata as the test's own query reports it */
static void build_op(AVB_TAS_OP_REQUEST *r, avb_u64 luid, const AVB_TAS_QUERY_HW_REQUEST *q, long long base, avb_u32 frame)
{
    unsigned i;

    ZeroMemory(r, sizeof(*r));
    r->version = AVB_TAS_OP_VERSION;
    r->size = (avb_u32)sizeof(*r);
    r->net_luid = luid;
    r->client_op_id = next_op_id();
    r->expected_generation = q->generation;
    r->context_epoch = q->context_epoch;
    r->pre_activation = AVB_TAS_PRE_HOLD;
    r->schedule.base_time_ns = (avb_u64)base;
    r->schedule.cycle_ns = (avb_u32)CYCLE_NS;
    for (i = 0; i < AVB_TAS_QUEUES; i++) {
        r->schedule.queue[i].start_ns = (avb_u32)WINDOW_START_NS;
        r->schedule.queue[i].end_ns = (avb_u32)WINDOW_END_NS;
        r->schedule.queue[i].max_frame_bytes = frame;
    }
}

typedef struct {
    HANDLE h, go;
    AVB_TAS_OP_REQUEST sent, r;
    LONG status;
    DWORD got;
} OP_CALL;

static DWORD WINAPI op_thread(LPVOID arg)
{
    OP_CALL *c = (OP_CALL *)arg;
    (void)WaitForSingleObject(c->go, INFINITE);
    c->status = avb_ioctl_ntstatus(c->h, IOCTL_AVB_TAS_ACTIVATE_80C, &c->r, sizeof(c->r), &c->r, sizeof(c->r), &c->got);
    return 0;
}

static void print_op(const char *label, const OP_CALL *c)
{
    char status_text[96];

    tas_fv_status_text(c->status != AVB_NTSTATUS_UNAVAILABLE, c->status, status_text, sizeof(status_text));
    printf("    %s: op=0x%016llX %s information=%lu%s out_flags=0x%X reserved1=0x%X\n", label, c->sent.client_op_id,
           status_text, c->got, c->status == 0 && c->got == sizeof(c->r) &&
           memcmp(&c->r, &c->sent, offsetof(AVB_TAS_OP_REQUEST, out_flags)) != 0 ? " (inputs NOT echoed)" : "", c->r.out_flags,
           c->r.reserved1);
}

/** ACCESS on the versioned code: a read-only handle (FILE_WRITE_ACCESS, #362, #365 13.3). The request is invalid (version 0), so
 *  nothing is written even if the access check were missing */
static void check_access_80c(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    AVB_TAS_OP_REQUEST r;
    AVB_TAS_STATE s;
    DWORD got = 0;
    LONG st;
    BOOL state_ok;
    char reason[200];
    HANDLE h = open_bound(GENERIC_READ, id->net_luid);

    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "ACCESS", index, id->device_id, "cannot open and bind a read-only handle");
        return;
    }
    ZeroMemory(&r, sizeof(r));
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &r, sizeof(r), &r, sizeof(r), &got);
    state_ok = read_state(h, &s);
    CloseHandle(h);
    printf("    read-only handle: IOCTL_AVB_TAS_ACTIVATE_80C ntstatus=0x%08lX information=%lu; GET_TAS_STATE ok=%d\n", (unsigned long)st,
           got, state_ok);
    if (st == (LONG)STATUS_ACCESS_DENIED && got == 0 && state_ok) {
        verdict("PASS", "ACCESS", index, id->device_id, "STATUS_ACCESS_DENIED with no output; GET_TAS_STATE still served");
    } else {
        sprintf_s(reason, sizeof(reason), "expected STATUS_ACCESS_DENIED with no output and GET_TAS_STATE served; ntstatus=0x%08lX "
                  "information=%lu, GET_TAS_STATE %s", (unsigned long)st, got, state_ok ? "served" : "not served");
        verdict("FAIL", "ACCESS", index, id->device_id, reason);
    }
}

/** A query carrying a lookup id (#365 12.1): LOOKUP evidence */
static void query_lookup(HANDLE h, avb_u64 op, QUERY_READ *r)
{
    ZeroMemory(r, sizeof(*r));
    r->q.version = AVB_TAS_QUERY_VERSION;
    r->q.size = (avb_u32)sizeof(r->q);
    r->q.lookup_client_op_id = op;
    r->status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_QUERY_HW, &r->q, sizeof(r->q), &r->q, sizeof(r->q), &r->got);
}

/** Pre-state proved by the test's own reads (#369): GET_TAS_STATE CLEAN and a clean, readable TAS query. Prints both.
 *  *armed: the record's armed value, or -1 when GET_TAS_STATE could not be read */
static BOOL prove_prestate(HANDLE h, const char *label, QUERY_READ *q, LONG *armed)
{
    AVB_TAS_STATE s;
    BOOL record = read_state(h, &s);

    *armed = record ? (LONG)s.armed : -1;
    if (record) {
        print_state(label, &s);
    } else {
        printf("    GET_TAS_STATE %s: unreadable\n", label);
    }
    query(h, q);
    print_query(label, q);
    return record && s.armed == AVB_TAS_STATE_CLEAN && query_clean(q);
}

/**
 * Test-owned setup recovery (#369 pre-state, #365 13.7 "tests own setup"): a pre-state not proved clean is recovered once through
 * this test's own NIC cycle, the recovery its cleanup uses, then the link is waited for and the pre-state proved again. Closes h.
 * Returns a bound handle with a proved pre-state, or INVALID_HANDLE_VALUE with the reason in why. Nothing else is attempted.
 */
static HANDLE recover_prestate(HANDLE h, UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id, DEVINST inst, QUERY_READ *q,
                               const char **why)
{
    MIB_IF_ROW2 row;
    DWORD start;
    BOOL cycled;
    LONG armed;

    printf("\n  --- Setup: pre-state not proved clean, recovered through this test's own NIC cycle ---\n");
    /* From here the test owns a state change: an abort reports it (report_abort) until it is verified or reported below */
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_setup, 1);
    InterlockedExchange(&g_pending_adapter, (LONG)index);
    CloseHandle(h);   /* an open bound handle must not hold the adapter context across the detach */
    cycled = cm_bounded(inst, FALSE, DISABLE_BOUND_MS);
    if (cycled) {
        Sleep(3000);
        cycled = cm_bounded(inst, TRUE, ENABLE_BOUND_MS);
    } else {
        printf("    enable not attempted: the disable did not complete and may still be pending in Windows\n");
    }
    h = cycled ? wait_for_adapter(id) : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) {
        verdict("CLEANUP_FAILED", "SETUP", index, id->device_id,
                "the setup recovery's NIC cycle did not complete or the adapter did not return; a timed-out disable may still be "
                "pending in Windows. Operator: NIC cycle once it ends, then TC-TSNREG-001");
        /* Tracking ends only once the failure is recorded: an abort before this line still reports the setup phase */
        InterlockedExchange(&g_pending_adapter, -1);
        InterlockedExchange(&g_pending_setup, 0);
        *why = "the setup recovery's NIC cycle did not complete or the adapter did not return";
        return INVALID_HANDLE_VALUE;
    }
    start = GetTickCount();
    while (GetTickCount() - start < RETURN_BOUND_MS) {
        if (read_if(id->net_luid, &row) && row.OperStatus == IfOperStatusUp) {
            break;
        }
        Sleep(1000);
    }
    Sleep(SETTLE_MS);
    if (!prove_prestate(h, "after the setup recovery", q, &armed)) {
        /* The test cycled the NIC and did not establish a clean state: observed residue is a cleanup failure of its setup,
         * an unmeasurable state is inconclusive. Either way the activation stays BLOCKED (the caller) */
        BOOL residue = (armed >= 0 && armed != AVB_TAS_STATE_CLEAN) || (query_readable(q) && !query_clean(q));
        if (residue) {
            verdict("CLEANUP_FAILED", "SETUP", index, id->device_id,
                    "after the setup recovery NIC cycle the driver record is not CLEAN or the readable TAS query is not clean (TSN mode, "
                    "slots or restoration); see the observations above. Operator: NIC cycle, then TC-TSNREG-001");
        } else {
            verdict("INCONCLUSIVE_RESTORATION", "SETUP", index, id->device_id,
                    "after the setup recovery NIC cycle the state could not be measured (GET_TAS_STATE unreadable or the TAS query not "
                    "readable); see the observations above");
        }
        InterlockedExchange(&g_pending_adapter, -1);   /* only after the outcome is recorded */
        InterlockedExchange(&g_pending_setup, 0);
        *why = "the pre-state is still not proved clean after the test-owned setup recovery (GET_TAS_STATE CLEAN and a clean, "
               "readable TAS query); see both observations above";
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    printf("    setup recovery: pre-state proved clean after the NIC cycle\n");
    InterlockedExchange(&g_pending_adapter, -1);   /* only after the final state is verified */
    InterlockedExchange(&g_pending_setup, 0);
    return h;
}

static void run_adapter(UINT32 index, const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    static TAS_ACT_FRAME frames[WIRE_MAX_FRAMES];
    AVB_DRIVER_STATISTICS stats;
    AVB_TAS_STATE s;
    AVB_TAS_ACTIVATE_REQUEST req;
    AVB_TAS_OP_REQUEST req_op;
    BOOL committed = FALSE;
    TAS_ACT_ANCHOR before_anchor, after_anchor;
    QUERY_READ qbefore;
    TAS_ACT_WIRE_COUNTS wc = {0};
    TAS_ACT_WIRE_CLASS wclass = TAS_ACT_WIRE_UNOBSERVED;
    TAS_ACT_RESTORE_CLASS restore;
    LONG armed_before;
    unsigned raw_sent = 0, raw_seen = 0;   /* after the activation: frames sendto accepted, and of those the unique ones the
                                              capture saw; read before cleanup reuses the capture */
    unsigned raw_send_failed = 0;          /* after the activation: sendto failures (observed, not counted as sent) */
    BOOL mapped = FALSE;                   /* every such frame got a PHC time: the timing counts mean something */
    BOOL anchor_ok = FALSE;                /* the PHC anchor after the frames drained was read */
    MIB_IF_ROW2 row;
    LARGE_INTEGER qpc;
    IN_ADDR ipv4;
    SOCKET sock;
    HANDLE h, h2;
    BOOL other_link = FALSE;
    long long phc = 0, base;
    unsigned nonce, sent = 0, send_failed = 0, seen = 0, first_after = 0;
    avb_u32 mbps, frame;
    DEVINST inst;
    LONG captured = 0, from_target = 0;

    BOOL wire = FALSE, has_link, has_ip = FALSE;
    char why[300];

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u ===\n", index, id->device_id, id->net_luid, id->if_index);
    g_ran++;
    g_next_seq = 0;   /* per adapter run: no capture is active here, so every tag slot is free */
    g_capture_luid = 0;   /* no observer carried over from the previous adapter run; find_observer sets it only when one is found (PR #481) */
    ZeroMemory(&ipv4, sizeof(ipv4));
    if (!find_devinst(id->net_luid, &inst)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id,
                "cannot resolve the adapter device node required for NIC-cycle cleanup");
        return;
    }

    /* Up to the activation: reads, bounded TX and, only when the pre-state is not proved clean, the test-owned setup recovery */
    h = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (h == INVALID_HANDLE_VALUE) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "cannot bind a handle to the adapter");
        return;
    }
    /* Pre-state proved by the TAS query, as #369 requires; also the restoration reference. Not proved: recovered, not vetoed */
    if (!prove_prestate(h, "before", &qbefore, &armed_before)) {
        const char *why = NULL;
        h = recover_prestate(h, index, id, inst, &qbefore, &why);
        if (h == INVALID_HANDLE_VALUE) {
            verdict("BLOCKED", "COMMIT", index, id->device_id, why);
            return;
        }
    }
    /* The link and an IPv4 address serve the wire and the cleanup's TX probe only: without them those are unmeasured, the
     * activation still runs (#369 amendment, #463). The MTU sizes max_frame_bytes */
    if (!read_if(id->net_luid, &row)) {
        verdict("BLOCKED", "COMMIT", index, id->device_id, "GetIfEntry2 failed: no MTU for max_frame_bytes");
        CloseHandle(h);
        return;
    }
    has_link = row.OperStatus == IfOperStatusUp;
    has_ip = has_link && find_addressing(id->net_luid, &ipv4, &other_link);
    g_cleanup_link_was_up = has_link;
    printf("    [EVIDENCE] another adapter up with a gateway: %s (diagnostic only, not a precondition: the owned NIC cycle may interrupt the host network; evidence is kept locally, #463)\n", other_link ? "yes" : "no");
    mbps = has_link ? (avb_u32)(row.TransmitLinkSpeed / 1000000ULL) : 0u;
    frame = (avb_u32)row.Mtu + FRAME_OVERHEAD_BYTES;
    printf("    link=%s %u Mbit/s ipv4=%s mtu=%lu frameOnWire=%u\n", has_link ? "up" : "down", mbps, has_ip ? "yes" : "no", row.Mtu, frame);
    if (g_mode == MODE_FAULT && mbps == 0) {   /* the legacy function-71 request of --fault carries a caller link speed */
        verdict("BLOCKED", "COMMIT", index, id->device_id, "no connected link: the legacy function-71 request of this mode carries a caller "
                "link speed");
        CloseHandle(h);
        return;
    }
    if (g_mode == MODE_HOLD || g_mode == MODE_PASS) {   /* #370 amendment: --pass no longer calls function 71 */
        check_access_80c(index, id);
    }
    sock = has_ip ? open_tx_socket(ipv4) : INVALID_SOCKET;
    nonce = 0;
    if (sock != INVALID_SOCKET) {
        wire = find_observer(index, id, sock, ipv4, &nonce, &sent, &send_failed, &seen);
    } else {
        verdict("BLOCKED", "OBSERVER", index, id->device_id, has_ip ? "no TX socket: WIRE and the cleanup's TX probe are not measured; "
                "the other checks still run" : "no link or no IPv4 address: no frames can be sent, WIRE and the cleanup's TX probe are not "
                "measured; the other checks still run");
    }
    if (!wire && sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    printf("    baseline: sent=%u sendFailed=%u seen=%u (observer %s)\n", sent, send_failed, seen, wire ? "established" : "none");
    h2 = open_bound(GENERIC_READ | GENERIC_WRITE, id->net_luid);
    if (h2 == INVALID_HANDLE_VALUE || !read_stats(h, &stats) || !take_anchor(h, &before_anchor, "before") || !read_phc(h, &phc)) {
        if (h2 != INVALID_HANDLE_VALUE) { CloseHandle(h2); }
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        verdict("BLOCKED", "COMMIT", index, id->device_id, "second handle, statistics or PHC unavailable before the activation");
        CloseHandle(h);
        return;
    }
    QueryPerformanceCounter(&qpc);
    {   /* BaseT lead: BASE_LEAD_NS, or the queried limits.min_margin_ns plus headroom when established and larger (#369 amendment) */
        long long lead = BASE_LEAD_NS;
        if (qbefore.q.limits.min_margin_state == AVB_TAS_LIMIT_ESTABLISHED) {
            if (qbefore.q.limits.min_margin_ns > (avb_u64)(LEAD_MAX_NS - LEAD_HEADROOM_NS)) {
                sprintf_s(why, sizeof(why), "limits.min_margin_ns %llu needs a lead beyond the test's bound of %lld ns",
                          qbefore.q.limits.min_margin_ns, (long long)LEAD_MAX_NS);
                CloseHandle(h2);
                if (wire) { wire_stop(); }
                if (sock != INVALID_SOCKET) { closesocket(sock); }
                verdict("BLOCKED", "COMMIT", index, id->device_id, why);
                CloseHandle(h);
                return;
            }
            if ((long long)qbefore.q.limits.min_margin_ns + LEAD_HEADROOM_NS > lead) {
                lead = (long long)qbefore.q.limits.min_margin_ns + LEAD_HEADROOM_NS;
            }
        }
        printf("    [EVIDENCE] BaseT lead %lld ns (limits.min_margin_ns %llu, state %u: %s)\n", lead, qbefore.q.limits.min_margin_ns,
               qbefore.q.limits.min_margin_state, qbefore.q.limits.min_margin_state == AVB_TAS_LIMIT_ESTABLISHED ? "established" :
               "not established, D.3: the test's own 1.5 s lead");
        base = (phc + lead + CYCLE_NS - 1) / CYCLE_NS * CYCLE_NS;
    }
    build_request(&req, g_mode == MODE_PASS ? AVB_TAS_PRE_PASS : AVB_TAS_PRE_HOLD, base, mbps, frame);
    ZeroMemory(&req_op, sizeof(req_op));
    if (g_mode == MODE_FAULT) {
        printf("    request: pre=%u base=%lld (PHC %lld, lead %lld ms) cycle=%u window=%lld..%lld ms link=%u frame=%u\n", req.pre_activation, base,
               phc, (base - phc) / NS_PER_MS, req.cycle_ns, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, mbps, frame);
    }

    /* First mutation. From here the cleanup always runs, whatever TAS_ACTIVATE returns */
    g_pending_did = id->device_id;
    InterlockedExchange(&g_pending_adapter, (LONG)index);

    if (g_mode == MODE_FAULT) {
        AVB_TAS_ACTIVATE_REQUEST again;
        DWORD error, error2;
        BOOL ok, ok2, debug_driver = FALSE;
        IOCTL_VERSION version = {0};
        DWORD br = 0;

        BOOL build_known = DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) &&
                           br >= sizeof(version);   /* only a complete reply carries Flags */
        if (build_known) {
            debug_driver = (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;
        }
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        if (!build_known) {
            verdict("BLOCKED", "RESULT", index, id->device_id, "GET_VERSION gave no complete reply: Debug or Release is unknown, so the fault "
                    "request is not sent");
            (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
            return;
        }
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
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_FAULTV) {
        /* #482 F1/F2 on the versioned request. Every measurement keeps its own validity rule; a missing one is named, never read
         * as unchanged, and a measured violation stays FAIL whatever else is missing. The owned cleanup below always runs */
        const BOOL f2 = g_fault_case == 2;
        const LONG expected = f2 ? (LONG)STATUS_UNSUCCESSFUL : (LONG)STATUS_IO_DEVICE_ERROR;
        AVB_TAS_OP_REQUEST op;
        IOCTL_VERSION version = {0};
        QUERY_READ qa;
        DWORD br = 0, information = 0;
        LONG st;
        BOOL build_known, debug_driver = FALSE, state_ok, fault_ran, gen_reported, gen_measured;
        avb_u32 armed_after = 0;
        unsigned differing = 0, unread = 0;
        char viol[700], miss[400], reason[1200], status_text[96];

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        build_known = DeviceIoControl(h, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &br, NULL) &&
                      br >= sizeof(version);   /* only a complete reply carries Flags */
        if (build_known) {
            debug_driver = (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;
        }
        if (!build_known || !debug_driver) {
            verdict("BLOCKED", "COMPLETION", index, id->device_id, !build_known
                    ? "GET_VERSION gave no complete reply: Debug or Release is unknown, so the fault request is not sent"
                    : "Release driver: debug_fault injection is Debug-only (#365 13.3); the Release refusal of a non-zero debug_fault is "
                      "#368's negative control, not claimed here; the fault request is not sent");
            (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
            return;
        }
        build_op(&op, id->net_luid, &qbefore.q, base, frame);
        op.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
        op.debug_fault = (FAULTV_FIRST_SCHEDULE_WRITE & AVB_TAS_DEBUG_FAULT_WRITE_MASK) | (f2 ? AVB_TAS_DEBUG_FAULT_RESTORE : 0u);
        printf("\n  --- %s: IOCTL_AVB_TAS_ACTIVATE_80C, HOLD, the first schedule write fails%s (Debug driver) ---\n", f2 ? "F2" : "F1",
               f2 ? " and the first restore write fails" : ", restore writes not failed");
        printf("    request: op=0x%016llX pre=HOLD base=%lld (PHC %lld, lead %lld ms) cycle=%lld window=%lld..%lld ms frame=%u flags=0x%X "
               "(ACCEPT_UNVERIFIED: authorizes execution, not HARDWARE_VERIFIED) debug_fault=0x%X gen=%llu%s epoch=%llu%s\n",
               op.client_op_id, base, phc, (base - phc) / NS_PER_MS, CYCLE_NS, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS,
               frame, op.flags, op.debug_fault, op.expected_generation, op.expected_generation == 0 ? " (not reported)" : "",
               op.context_epoch, op.context_epoch == 0 ? " (not reported)" : "");
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &op, sizeof(op), &op, sizeof(op), &information);
        tas_fv_status_text(st != AVB_NTSTATUS_UNAVAILABLE, st, status_text, sizeof(status_text));
        printf("    fault request: %s information=%lu (expected 0x%08lX, no output)\n", status_text, information, (unsigned long)expected);
        fault_ran = st == expected && information == 0;
        state_ok = read_state(h, &s);
        if (state_ok) {
            armed_after = s.armed;
            print_state("after the fault request", &s);
        }
        query(h, &qa);
        print_query("after the fault request", &qa);
        restoration_set_compare(&qbefore, &qa, &differing, &unread);
        printf("    [EVIDENCE] restoration set (DES 12.5, BASET_L/H included): %u of %u compared registers differ from the pre-state "
               "query, %u of 17 unread\n", differing, 17u - unread, unread);
        gen_reported = query_valid(&qbefore) && qbefore.q.generation != 0;
        gen_measured = gen_reported && query_valid(&qa) && qa.q.generation != 0;
        if (!gen_reported) {
            printf("    [EVIDENCE] generation: not reported before the request (0): neither a value nor evidence of a transition\n");
        } else {
            printf("    [EVIDENCE] generation: before %llu, after %s%llu\n", qbefore.q.generation, gen_measured ? "" : "not reported ",
                   query_valid(&qa) ? qa.q.generation : 0ull);
        }

        /* METADATA (#482; Q 2026-11-19): whether setup obtained a valid, current, non-zero generation/epoch pair. Independent of the
         * completion: missing metadata stays nonpassing and visible; the request above carried the query's values as reported */
        if (!query_valid(&qbefore)) {
            verdict("FAIL", "METADATA", index, id->device_id, "the pre-state query is not valid: no current generation and epoch were "
                    "obtained; the control is not established (nothing fabricated)");
        } else if (qbefore.q.generation == 0 || qbefore.q.context_epoch == 0) {
            sprintf_s(reason, sizeof(reason), "the pre-state query reports generation %llu and epoch %llu: 0 is never a valid value, it "
                      "means not reported (#352 L2, #365 13.6 D.1); a valid current non-zero pair is required for the F1/F2 control, "
                      "which is therefore not established; the request carried the reported values, none fabricated",
                      qbefore.q.generation, qbefore.q.context_epoch);
            verdict("FAIL", "METADATA", index, id->device_id, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "generation %llu and epoch %llu from the test's own pre-state query", qbefore.q.generation,
                      qbefore.q.context_epoch);
            verdict("PASS", "METADATA", index, id->device_id, reason);
        }

        /* COMPLETION */
        if (st == AVB_NTSTATUS_UNAVAILABLE) {
            verdict("INCONCLUSIVE", "COMPLETION", index, id->device_id, "the raw NTSTATUS was not captured");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_REQUEST) {
            verdict("FAIL", "COMPLETION", index, id->device_id, "IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST): the "
                    "code is allocated to the adapter's TAS function (#362, #357), so this is FAIL, never SKIP; the fault path was not "
                    "reached");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_STATE && information == 0 && (op.expected_generation == 0 || op.context_epoch == 0)) {
            /* #352 L2 / #365 13.6 D.1: 0 is never a valid generation or epoch; a request carrying the query's unreported (0) metadata
             * is refused STATE_CONFLICT before any write. #369's rule: such a refusal is reported, not judged - the fault path was
             * not reached and nothing is attributed to it */
            verdict("INCONCLUSIVE", "COMPLETION", index, id->device_id, "fault-path attribution not established: STATUS_INVALID_DEVICE_STATE "
                    "with no output for a request carrying the query's generation or epoch 0 - refused before any write (STATE_CONFLICT, "
                    "#352 L2), so the injected fault is not shown to have run; the missing metadata is the separate METADATA failure");
        } else if (fault_ran) {
            verdict("PASS", "COMPLETION", index, id->device_id, f2 ? "STATUS_UNSUCCESSFUL with no output (the record's FAILED_NOT_RESTORED "
                    "is not retrievable yet, #352)" : "STATUS_IO_DEVICE_ERROR with no output (the record's FAILED_RESTORED is not "
                    "retrievable yet, #352)");
        } else {
            sprintf_s(reason, sizeof(reason), "completion ntstatus=0x%08lX information=%lu, expected 0x%08lX (%s) with no output",
                      (unsigned long)st, information, (unsigned long)expected, f2 ? "STATUS_UNSUCCESSFUL" : "STATUS_IO_DEVICE_ERROR");
            verdict("FAIL", "COMPLETION", index, id->device_id, reason);
        }

        if (!f2) {
            /* F1 ROLLBACK: registers at the test's own baseline and the record CLEAN */
            viol[0] = '\0';
            miss[0] = '\0';
            if (differing != 0) {
                sprintf_s(reason, sizeof(reason), "; %u measured restoration-set register(s) differ from the pre-state query (listed above)",
                          differing);
                strcat_s(viol, sizeof(viol), reason);
            }
            /* The record must be CLEAN when restoration is established (#365 R8): here, every restoration-set register measured
             * equal; and always when no write was attempted (fault path not reached). Otherwise a non-CLEAN record is not judged */
            if (!state_ok) {
                strcat_s(miss, sizeof(miss), "; the TAS record (GET_TAS_STATE) was not read");
            } else if (armed_after != AVB_TAS_STATE_CLEAN) {
                if (!fault_ran || (differing == 0 && unread == 0)) {
                    sprintf_s(reason, sizeof(reason), "; the TAS record is not CLEAN (armed=%u) although %s", armed_after,
                              fault_ran ? "every restoration-set register measured equal establishes restoration"
                                        : "the fault path was not reached (no write attempted)");
                    strcat_s(viol, sizeof(viol), reason);
                } else {
                    sprintf_s(reason, sizeof(reason), "; the TAS record is armed=%u while restoration is not established by the "
                              "measurement (registers unread)", armed_after);
                    strcat_s(miss, sizeof(miss), reason);
                }
            }
            if (unread != 0) {
                sprintf_s(reason, sizeof(reason), "; %u of 17 restoration-set register(s) unread before or after (listed above)", unread);
                strcat_s(miss, sizeof(miss), reason);
            }
            if (viol[0] != '\0') {
                sprintf_s(reason, sizeof(reason), "%s%s%s", viol + 2, miss[0] != '\0' ? "; also not measured" : "", miss);
                verdict("FAIL", "ROLLBACK", index, id->device_id, reason);
            } else if (!fault_ran) {
                sprintf_s(reason, sizeof(reason), "not exercised: the fault path was not reached (see COMPLETION), so restoration after a "
                          "failed schedule write is not established; measured: no restoration-set difference, record %s%s%s",
                          state_ok ? "CLEAN" : "unread", miss[0] != '\0' ? "; not measured" : "", miss);
                verdict("INCONCLUSIVE", "ROLLBACK", index, id->device_id, reason);
            } else if (miss[0] != '\0') {
                sprintf_s(reason, sizeof(reason), "no observed violation, but not established: %s", miss + 2);
                verdict("INCONCLUSIVE", "ROLLBACK", index, id->device_id, reason);
            } else {
                verdict("PASS", "ROLLBACK", index, id->device_id, "every restoration-set register equal to the pre-state query, record CLEAN");
            }
            /* F1 GENERATION: exactly one advance for the attempted mutation and its rollback, when reported */
            if (!gen_reported || !gen_measured) {
                verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, !gen_reported
                        ? "the generation was not reported before the request (0): the transition cannot be measured and is not fabricated"
                        : "the generation was reported before the request but not after it");
            } else if (!fault_ran) {
                if (qa.q.generation != qbefore.q.generation) {
                    sprintf_s(reason, sizeof(reason), "the fault path was not reached, yet the generation changed from %llu to %llu",
                              qbefore.q.generation, qa.q.generation);
                    verdict("FAIL", "GENERATION", index, id->device_id, reason);
                } else {
                    verdict("INCONCLUSIVE", "GENERATION", index, id->device_id, "not exercised: the fault path was not reached (see "
                            "COMPLETION); the generation is unchanged");
                }
            } else if (qa.q.generation == qbefore.q.generation + 1) {
                verdict("PASS", "GENERATION", index, id->device_id, "advanced exactly once");
            } else {
                sprintf_s(reason, sizeof(reason), "generation %llu -> %llu, expected exactly one advance for the attempted mutation",
                          qbefore.q.generation, qa.q.generation);
                verdict("FAIL", "GENERATION", index, id->device_id, reason);
            }
        } else {
            /* F2 STATE: the record DIRTY after a failed restore (classification: tas_fv_f2_state, TC-MOCK-058) */
            switch (tas_fv_f2_state(fault_ran, state_ok, armed_after, differing)) {
            case TAS_FV_STATE_DIRTY:
                verdict("PASS", "STATE", index, id->device_id, NULL);
                break;
            case TAS_FV_STATE_NOT_DIRTY:
                sprintf_s(reason, sizeof(reason), "GET_TAS_STATE reports armed=%u, expected DIRTY (2)", armed_after);
                verdict("FAIL", "STATE", index, id->device_id, reason);
                break;
            case TAS_FV_STATE_UNREAD:
                verdict("INCONCLUSIVE", "STATE", index, id->device_id, "the TAS record (GET_TAS_STATE) was not read after the fault request");
                break;
            case TAS_FV_STATE_CHANGED_UNREACHED:
                if (state_ok) {
                    sprintf_s(reason, sizeof(reason), "the fault path was not reached (see COMPLETION), yet the record is armed=%u and %u "
                              "restoration-set register(s) differ from the pre-state query", armed_after, differing);
                } else {
                    sprintf_s(reason, sizeof(reason), "the fault path was not reached (see COMPLETION), yet %u restoration-set register(s) "
                              "differ from the pre-state query (the record was not read; a measured change is not hidden by it)", differing);
                }
                verdict("FAIL", "STATE", index, id->device_id, reason);
                break;
            default:
                verdict("INCONCLUSIVE", "STATE", index, id->device_id, "not exercised: the fault path was not reached (see COMPLETION), so "
                        "DIRTY is not established; measured: record CLEAN, no restoration-set difference");
                break;
            }
            /* F2 REFUSE: a further valid activation with the post-failure metadata (#369's rule) */
            if (!query_valid(&qa)) {
                verdict("BLOCKED", "REFUSE", index, id->device_id, "the query after the fault request failed: no current generation and "
                        "epoch for the further request, which is not sent");
            } else {
                OP_CALL again;
                AVB_TAS_STATE s2;
                QUERY_READ qb;
                unsigned d2 = 0, u2 = 0;
                TAS_FV_REFUSE_EVIDENCE ev;
                BOOL s2_ok;

                ZeroMemory(&again, sizeof(again));
                build_op(&again.sent, id->net_luid, &qa.q, base + CYCLE_NS, frame);
                again.sent.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;   /* debug_fault 0: an otherwise-valid HOLD */
                again.r = again.sent;
                again.status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &again.r, sizeof(again.r), &again.r, sizeof(again.r),
                                                  &again.got);
                printf("    further request: op=0x%016llX base=%lld debug_fault=0 gen=%llu%s epoch=%llu%s (from the query after the failure)\n",
                       again.sent.client_op_id, base + CYCLE_NS, qa.q.generation, qa.q.generation == 0 ? " (not reported)" : "",
                       qa.q.context_epoch, qa.q.context_epoch == 0 ? " (not reported)" : "");
                print_op("activation after the failure", &again);
                s2_ok = read_state(h, &s2);
                if (s2_ok) { print_state("after the further request", &s2); }
                query(h, &qb);
                print_query("after the further request", &qb);
                restoration_set_compare(&qa, &qb, &d2, &u2);
                printf("    [EVIDENCE] further request: %u of %u compared restoration-set registers differ from the post-failure query, "
                       "%u of 17 unread\n", d2, 17u - u2, u2);
                /* Classification: tas_fv_f2_refuse (TC-MOCK-059) */
                ZeroMemory(&ev, sizeof(ev));
                ev.not_served = again.status == (LONG)STATUS_INVALID_DEVICE_REQUEST;
                ev.status_unavailable = again.status == AVB_NTSTATUS_UNAVAILABLE;
                ev.accepted = again.status == 0;
                ev.output = again.got != 0;
                ev.state_refusal = again.status == (LONG)STATUS_INVALID_DEVICE_STATE;
                ev.record_before = state_ok;
                ev.armed_before = armed_after;
                ev.record_after = s2_ok;
                ev.armed_after = s2_ok ? s2.armed : 0u;
                ev.differing = d2;
                ev.unread = u2;
                ev.fault_ran = fault_ran;
                ev.metadata_reported = qa.q.generation != 0 && qa.q.context_epoch != 0;
                switch (tas_fv_f2_refuse(&ev)) {
                case TAS_FV_REFUSE_NOT_SERVED:
                    verdict("FAIL", "REFUSE", index, id->device_id, "IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST)");
                    break;
                case TAS_FV_REFUSE_STATUS_UNAVAILABLE:
                    sprintf_s(reason, sizeof(reason), "the raw NTSTATUS of the further request was not captured, and no independent "
                              "violation was measured (information=%lu, record %s, %u restoration-set register(s) differ, %u of 17 unread); "
                              "neither a refusal nor an unchanged state is established", again.got, s2_ok && state_ok ? "unchanged" :
                              "not read", d2, u2);
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_CHANGED:
                    tas_fv_status_text(!ev.status_unavailable, again.status, status_text, sizeof(status_text));
                    sprintf_s(reason, sizeof(reason), "the further request must be refused with no output and change nothing: %s "
                              "information=%lu, record %u -> %s%u, %u restoration-set register(s) changed", status_text,
                              again.got, armed_after, s2_ok ? "" : "unread ", s2_ok ? s2.armed : 0u, d2);
                    verdict("FAIL", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_OTHER_STATUS:
                    sprintf_s(reason, sizeof(reason), "refused with ntstatus=0x%08lX, expected STATUS_INVALID_DEVICE_STATE", (unsigned long)again.status);
                    verdict("FAIL", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_UNCHANGED_UNESTABLISHED:
                    sprintf_s(reason, sizeof(reason), "STATUS_INVALID_DEVICE_STATE with no output, but the unchanged state is not established "
                              "(record %s, %u of 17 restoration-set registers unread)", s2_ok && state_ok ? "read" : "unread", u2);
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, reason);
                    break;
                case TAS_FV_REFUSE_NOT_DIRTY:
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, "refused with STATUS_INVALID_DEVICE_STATE and nothing changed, but "
                            "the DIRTY precondition was not established (see COMPLETION and STATE)");
                    break;
                case TAS_FV_REFUSE_METADATA_ZERO:
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, "reported, not judged: STATUS_INVALID_DEVICE_STATE with no output "
                            "and the state kept, but the post-failure query reports generation or epoch 0 (not reported, #365 13.6 D.1), so "
                            "NOT_CLEAN cannot be told from STATE_CONFLICT");
                    break;
                default:
                    verdict("PASS", "REFUSE", index, id->device_id, "STATUS_INVALID_DEVICE_STATE, no output, record DIRTY and restoration set "
                            "unchanged");
                    break;
                }
            }
        }
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    if (g_mode == MODE_PASS) {
        /* #370 amendment 2026-11-19: PASS on the versioned request. Under #365's R7 order a header-valid PASS completes
         * STATUS_NOT_IMPLEMENTED with no output before the metadata, schedule, link and tier checks and before any register or
         * PHC access, until ADR-TSN-003 (#363). The state is measured against the proved-clean pre-state; each measurement keeps
         * its own validity rule, a missing one is named and never read as unchanged, and an observed violation stays FAIL even
         * when another measurement is missing. The cleanup below still runs */
        AVB_TAS_OP_REQUEST op;
        QUERY_READ qa;
        DWORD information = 0;
        LONG st;
        BOOL state_ok, gen_reported, gen_measured;
        unsigned differing = 0, unread = 0;
        char viol[700], miss[300], reason[1100];

        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); }
        CloseHandle(h2);
        build_op(&op, id->net_luid, &qbefore.q, base, frame);
        op.pre_activation = AVB_TAS_PRE_PASS;
        printf("\n  --- IOCTL_AVB_TAS_ACTIVATE_80C with PASS (not implemented until ADR-TSN-003 is accepted, #363) ---\n");
        printf("    request: op=0x%016llX pre=PASS base=%lld (PHC %lld, lead %lld ms) cycle=%lld window=%lld..%lld ms frame=%u "
               "flags=0x%X fault=0x%X gen=%llu%s epoch=%llu%s\n", op.client_op_id, base, phc, (base - phc) / NS_PER_MS, CYCLE_NS,
               WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, frame, op.flags, op.debug_fault, op.expected_generation,
               op.expected_generation == 0 ? " (not reported)" : "", op.context_epoch, op.context_epoch == 0 ? " (not reported)" : "");
        st = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &op, sizeof(op), &op, sizeof(op), &information);
        printf("    PASS request: ntstatus=0x%08lX information=%lu\n", (unsigned long)st, information);
        state_ok = read_state(h, &s);
        if (state_ok) { print_state("after the PASS request", &s); }
        query(h, &qa);
        print_query("after the PASS request", &qa);
        restoration_set_compare(&qbefore, &qa, &differing, &unread);
        printf("    [EVIDENCE] restoration set (DES 12.5, BASET_L/H included): %u of %u compared registers differ from the pre-state "
               "query, %u of 17 unread\n", differing, 17u - unread, unread);
        gen_reported = query_valid(&qbefore) && qbefore.q.generation != 0;
        gen_measured = gen_reported && query_valid(&qa) && qa.q.generation != 0;
        if (!gen_reported) {
            printf("    [EVIDENCE] generation: not reported before the request (0): neither a value nor evidence that it did not "
                   "change; not applicable\n");
        } else {
            printf("    [EVIDENCE] generation: before %llu, after %s%llu\n", qbefore.q.generation, gen_measured ? "" : "not reported ",
                   query_valid(&qa) ? qa.q.generation : 0ull);
        }

        viol[0] = '\0';
        miss[0] = '\0';
        if (st == AVB_NTSTATUS_UNAVAILABLE) {
            strcat_s(miss, sizeof(miss), "; the raw NTSTATUS was not captured");
        } else if (st == (LONG)STATUS_INVALID_DEVICE_REQUEST) {
            strcat_s(viol, sizeof(viol), "; IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST): the code is allocated "
                     "to the adapter's TAS function (#362, #357), so this is FAIL, never SKIP");
        } else if (st != (LONG)STATUS_NOT_IMPLEMENTED || information != 0) {
            sprintf_s(reason, sizeof(reason), "; completion ntstatus=0x%08lX information=%lu, expected STATUS_NOT_IMPLEMENTED with no "
                      "output (a header-valid PASS is recognized before the metadata check under R7, so zero metadata explains "
                      "no other status)", (unsigned long)st, information);
            strcat_s(viol, sizeof(viol), reason);
        }
        if (!state_ok) {
            strcat_s(miss, sizeof(miss), "; the TAS record (GET_TAS_STATE) was not read");
        } else if (s.armed != AVB_TAS_STATE_CLEAN) {
            sprintf_s(reason, sizeof(reason), "; the TAS record is not CLEAN (armed=%u)", s.armed);
            strcat_s(viol, sizeof(viol), reason);
        }
        /* Each register on its own (PR #484 r4626979931): a measured difference is a violation whatever else is unread, and
         * unread registers are a missing measurement whatever was found in the others */
        if (differing != 0) {
            sprintf_s(reason, sizeof(reason), "; %u measured restoration-set register(s) differ from the pre-state query (listed above)",
                      differing);
            strcat_s(viol, sizeof(viol), reason);
        }
        if (unread != 0) {
            sprintf_s(reason, sizeof(reason), "; %u of 17 restoration-set register(s) unread before or after (listed above)", unread);
            strcat_s(miss, sizeof(miss), reason);
        }
        if (gen_reported && !gen_measured) {
            strcat_s(miss, sizeof(miss), "; the generation was reported before the request but not after it");
        } else if (gen_measured && qa.q.generation != qbefore.q.generation) {
            sprintf_s(reason, sizeof(reason), "; the generation changed from %llu to %llu", qbefore.q.generation, qa.q.generation);
            strcat_s(viol, sizeof(viol), reason);
        }
        if (viol[0] != '\0') {
            sprintf_s(reason, sizeof(reason), "%s%s%s", viol + 2, miss[0] != '\0' ? "; also not measured" : "", miss);
            verdict("FAIL", "COMMIT", index, id->device_id, reason);
        } else if (miss[0] != '\0') {
            sprintf_s(reason, sizeof(reason), "no observed violation, but not established: %s", miss + 2);
            verdict("INCONCLUSIVE", "COMMIT", index, id->device_id, reason);
        } else {
            verdict("SKIP_NOT_IMPLEMENTED", "COMMIT", index, id->device_id,
                    "PASS is not implemented until ADR-TSN-003 is accepted (#363, #370 amendment): STATUS_NOT_IMPLEMENTED, no output, "
                    "TAS record CLEAN, restoration set as captured, generation unchanged or not reported; #370's traffic objective "
                    "is not verified by this");
        }
        (void)cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
        return;
    }

    {
        /* The accepted HOLD race (#369 amendments 2026-11-18 and 2026-11-19), judged by tas_hold_commit and tas_hold_generation
         * (tests/ioctl/tas_activate_eval.h, TC-MOCK-062/063) from the two completions and the queries immediately before and after */
        OP_CALL calls[2];
        HANDLE threads[2], go = CreateEventA(NULL, TRUE, FALSE, NULL);
        unsigned success = 0, not_served = 0, k, winner = 0, transitions = 0;
        BOOL armed_ok, race_valid;
        QUERY_READ qr, qa, ql;
        const AVB_TAS_QUERY_HW_REQUEST *pair;
        TAS_HOLD_EVIDENCE ev;
        TAS_HOLD_OUT_EXPECT out;
        TAS_HOLD_COMMIT_CLASS cc;
        TAS_HOLD_GEN_CLASS gc;
        const char *tag;
        char reason[900], detail[300];

        printf("\n  --- IOCTL_AVB_TAS_ACTIVATE_80C from two bound open instances at once ---\n");
        /* The pair the driver compares under its claim, the driver-owned speed and the HOLD tier are measured immediately before the
         * race (as #368's cases are, PR #491 review): the observer trial lies between the proved pre-state and here. When this query
         * is not valid, the proved pre-state's pair is sent as the last observed one and printed NOT measured, never fabricated */
        query(h, &qr);
        print_query("immediately before the race", &qr);
        race_valid = query_valid(&qr);
        pair = race_valid ? &qr.q : &qbefore.q;
        if (race_valid) {
            printf("    [EVIDENCE] query before the race: valid; generation=%llu context_epoch=%llu (0 = not reported, never a value, #352 "
                   "R1) link_speed_mbps=%u (driver-owned, 0 = unknown, #351 R2) tier_hold=%u (AVB_TAS_TIER_*, 0 = not reported)\n",
                   qr.q.generation, qr.q.context_epoch, qr.q.link_speed_mbps, qr.q.limits.tier_hold);
        } else {
            printf("    [EVIDENCE] query before the race: NOT valid (ntstatus=0x%08lX information=%lu): pair, speed and tier NOT measured; "
                   "the requests carry the proved pre-state's generation=%llu context_epoch=%llu as the last observed pair\n",
                   (unsigned long)qr.status, qr.got, qbefore.q.generation, qbefore.q.context_epoch);
        }
        /* METADATA (#369 oracle amendment 2026-11-19, #352 R1 L13): once initialized the query reports the context's actual non-zero
         * generation and epoch. A 0 exposes missing implementation (or an unavailable context), never a valid value. Its own line;
         * the race still runs with what was measured */
        if (!race_valid) {
            verdict("INCONCLUSIVE", "METADATA", index, id->device_id, "the query before the race is not valid: generation and epoch "
                    "not measured");
        } else if (qr.q.generation == 0 || qr.q.context_epoch == 0) {
            sprintf_s(reason, sizeof(reason), "the query reports generation=%llu context_epoch=%llu: #352 R1 requires the actual non-zero "
                      "pair of the adapter context (0 = not reported, never a value). The accepted control carries it, so the contract "
                      "requires its refusal as STATE_CONFLICT at R7 step 4 (#352 R1); the actual completions are on the COMMIT "
                      "line", qr.q.generation, qr.q.context_epoch);
            verdict("FAIL", "METADATA", index, id->device_id, reason);
        } else {
            sprintf_s(reason, sizeof(reason), "generation=%llu context_epoch=%llu, both non-zero (#352 R1)", qr.q.generation,
                      qr.q.context_epoch);
            verdict("PASS", "METADATA", index, id->device_id, reason);
        }
        for (k = 0; k < 2; k++) {
            calls[k].h = k == 0 ? h : h2;
            calls[k].go = go;
            build_op(&calls[k].sent, id->net_luid, pair, base, frame);   /* the same schedule, a distinct client_op_id */
            calls[k].sent.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;   /* #362, #365 R4: executes unverified HOLD, never HARDWARE_VERIFIED */
            calls[k].r = calls[k].sent;
            calls[k].status = (LONG)AVB_NTSTATUS_UNAVAILABLE;
            calls[k].got = 0;
            threads[k] = go != NULL ? CreateThread(NULL, 0, op_thread, &calls[k], 0, NULL) : NULL;
        }
        printf("    request: pre=HOLD flags=0x%X (ACCEPT_UNVERIFIED: authorizes execution, not HARDWARE_VERIFIED) base=%lld (PHC %lld, lead "
               "%lld ms) cycle=%lld window=%lld..%lld ms frame=%u gen=%llu%s epoch=%llu%s\n", calls[0].sent.flags, base, phc,
               (base - phc) / NS_PER_MS, CYCLE_NS, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS, frame, pair->generation,
               pair->generation == 0 ? " (not reported)" : "", pair->context_epoch, pair->context_epoch == 0 ? " (not reported)" : "");
        if (go != NULL) { SetEvent(go); }
        for (k = 0; k < 2; k++) {
            if (threads[k] != NULL) {
                (void)WaitForSingleObject(threads[k], INFINITE);
                CloseHandle(threads[k]);
            }
            print_op(k == 0 ? "instance 1" : "instance 2", &calls[k]);
        }
        if (go != NULL) { CloseHandle(go); }
        /* h2 stays open through LOOKUP: the record is retrieved on the originating open instance (#365 13.1, PR #481) */
        first_after = g_next_seq;
        query(h, &qa);   /* before any further request: GENERATION, COMMIT's isolation and REFUSE's metadata */
        print_query("after the race", &qa);

        ZeroMemory(&ev, sizeof(ev));
        for (k = 0; k < 2; k++) {
            ev.call[k].captured = calls[k].status != AVB_NTSTATUS_UNAVAILABLE;
            ev.call[k].status = calls[k].status;
            ev.call[k].information = calls[k].got;
            ev.call[k].echoed = memcmp(&calls[k].r, &calls[k].sent, offsetof(AVB_TAS_OP_REQUEST, out_flags)) == 0;
            ev.call[k].out_flags = calls[k].r.out_flags;
            ev.call[k].reserved1 = calls[k].r.reserved1;
            if (ev.call[k].captured && calls[k].status == 0) { success++; winner = k; }
            if (ev.call[k].captured && calls[k].status == (LONG)STATUS_INVALID_DEVICE_REQUEST) { not_served++; }
        }
        ev.accept_sent = TRUE;
        ev.pre_valid = race_valid;
        ev.pre_generation = pair->generation;
        ev.pre_epoch = pair->context_epoch;
        ev.pre_mbps = race_valid ? qr.q.link_speed_mbps : 0u;
        ev.tier_hold = race_valid ? qr.q.limits.tier_hold : 0u;
        ev.post_valid = query_valid(&qa);
        ev.post_generation = ev.post_valid ? qa.q.generation : 0u;
        ev.post_epoch = ev.post_valid ? qa.q.context_epoch : 0u;
        ev.post_mbps = ev.post_valid ? qa.q.link_speed_mbps : 0u;
        out = tas_hold_out_expect(ev.pre_valid, ev.tier_hold);
        cc = tas_hold_commit(&ev);
        gc = tas_hold_generation(&ev, &transitions);
        printf("    [EVIDENCE] race facts: out_flags expectation %s (tier_hold %u%s); query after the race %s generation=%llu "
               "context_epoch=%llu link_speed_mbps=%u\n", out == TAS_HOLD_OUT_BIT0 ? "bit 0 (acceptance acts)" :
               (out == TAS_HOLD_OUT_ZERO ? "0 (acceptance does not act)" : "not judged"), ev.tier_hold,
               race_valid ? "" : ", not measured", ev.post_valid ? "valid" : "NOT valid:", ev.post_generation, ev.post_epoch, ev.post_mbps);

        detail[0] = '\0';
        if (cc == TAS_HOLD_COMMIT_PAIR_CHANGED || cc == TAS_HOLD_COMMIT_SPEED_UNDETERMINED || cc == TAS_HOLD_COMMIT_OUT_UNJUDGED) {
            sprintf_s(detail, sizeof(detail), "before the race %s generation=%llu epoch=%llu speed=%u tier_hold=%u; after it %s "
                      "generation=%llu epoch=%llu speed=%u", race_valid ? "valid" : "NOT measured", ev.pre_generation, ev.pre_epoch,
                      ev.pre_mbps, ev.tier_hold, ev.post_valid ? "valid" : "NOT measured", ev.post_generation, ev.post_epoch, ev.post_mbps);
        }
        switch (cc) {
        case TAS_HOLD_COMMIT_PASS:
            sprintf_s(reason, sizeof(reason), "one complete STATUS_SUCCESS reply (144 bytes, inputs echoed, reserved1 0, out_flags 0x%X as "
                      "tier_hold %u requires, #365 R4) and one STATUS_DEVICE_BUSY or STATUS_INVALID_DEVICE_STATE with no output; an "
                      "accepted unverified commit is not HARDWARE_VERIFIED and does not establish K2 (#352)", calls[winner].r.out_flags,
                      ev.tier_hold);
            break;
        case TAS_HOLD_COMMIT_NOT_SERVED:
            sprintf_s(reason, sizeof(reason), "IOCTL_AVB_TAS_ACTIVATE_80C not served (STATUS_INVALID_DEVICE_REQUEST on %u of 2 calls): the "
                      "code is allocated to the adapter's TAS function (#362, #357), so this is FAIL, never SKIP", not_served);
            break;
        case TAS_HOLD_COMMIT_BAD_SUCCESS:
            sprintf_s(reason, sizeof(reason), "a STATUS_SUCCESS reply is not complete (#365 13.2, R4): Information 144, inputs echoed, "
                      "reserved1 0 and no out_flags bit but bit 0 are required; see the two calls above");
            break;
        case TAS_HOLD_COMMIT_OUT_FLAGS:
            sprintf_s(reason, sizeof(reason), "out_flags 0x%X, but tier_hold %u requires %s: AVB_TAS_OUT_UNVERIFIED_CONFIG_ACTIVE is set "
                      "exactly when unverified acceptance acted (#365 R4), not because the flag was sent", calls[winner].r.out_flags,
                      ev.tier_hold, out == TAS_HOLD_OUT_BIT0 ? "bit 0" : "0");
            break;
        case TAS_HOLD_COMMIT_ZERO_ACCEPTED:
            sprintf_s(reason, sizeof(reason), "a request carrying generation=%llu context_epoch=%llu (0 = not reported) completed "
                      "STATUS_SUCCESS: #352 R1 requires a zero pair to be refused STATE_CONFLICT at R7 step 4", ev.pre_generation, ev.pre_epoch);
            break;
        case TAS_HOLD_COMMIT_TWO_SUCCESSES:
            sprintf_s(reason, sizeof(reason), "both calls completed STATUS_SUCCESS: the non-waiting per-adapter claim must admit "
                      "only one (#365 R7 step 3)");
            break;
        case TAS_HOLD_COMMIT_TIER_REFUSED:
            sprintf_s(reason, sizeof(reason), "STATUS_REQUEST_NOT_ACCEPTED although both requests carried AVB_TAS_F_ACCEPT_UNVERIFIED: "
                      "with it the driver must execute the HOLD operation (#365 R4, #362); no longer an undecided tier policy");
            break;
        case TAS_HOLD_COMMIT_R5:
            sprintf_s(reason, sizeof(reason), "STATUS_IO_TIMEOUT or STATUS_IO_DEVICE_ERROR with no output: #365 R5 maps BASE_PASSED and "
                      "COMMITTED_UNTIMED to these (STATUS_IO_DEVICE_ERROR is also R8's FAILED_RESTORED), so the status does not detail "
                      "the outcome; no complete commit (the adapter may hold a schedule, the owned cleanup follows)");
            break;
        case TAS_HOLD_COMMIT_META_ZERO:
            sprintf_s(reason, sizeof(reason), "both refused STATUS_INVALID_DEVICE_STATE: the requests carried generation=%llu "
                      "context_epoch=%llu (0 = not reported), which #352 R1 requires to be refused STATE_CONFLICT; the missing metadata (see "
                      "METADATA) is the likely cause, not an excuse: no commit", ev.pre_generation, ev.pre_epoch);
            break;
        case TAS_HOLD_COMMIT_STATE_REFUSED:
            sprintf_s(reason, sizeof(reason), "refused STATUS_INVALID_DEVICE_STATE although the pair sent (generation=%llu "
                      "context_epoch=%llu) was measured unchanged before and after the race on the proved-clean adapter: no commit",
                      ev.pre_generation, ev.pre_epoch);
            break;
        case TAS_HOLD_COMMIT_NOT_READY:
            sprintf_s(reason, sizeof(reason), "refused STATUS_DEVICE_NOT_READY although the driver-owned speed was measured %u Mbit/s "
                      "before and after the race (#351 R2): no commit", ev.pre_mbps);
            break;
        case TAS_HOLD_COMMIT_OUT_UNJUDGED:
            sprintf_s(reason, sizeof(reason), "one complete STATUS_SUCCESS reply (out_flags 0x%X) and one BUSY or STATE refusal, but "
                      "whether unverified acceptance acted is not measured (%s), so out_flags is reported, not judged (#365 R4); never "
                      "PASS", calls[winner].r.out_flags, detail);
            break;
        case TAS_HOLD_COMMIT_PAIR_CHANGED:
            sprintf_s(reason, sizeof(reason), "refused STATUS_INVALID_DEVICE_STATE, but the pair the driver compared under its claim is "
                      "not determined: %s", detail);
            break;
        case TAS_HOLD_COMMIT_SPEED_UNDETERMINED:
            sprintf_s(reason, sizeof(reason), "refused STATUS_DEVICE_NOT_READY, but the driver-owned speed it sampled is not determined "
                      "(#351 R2: NOT_READY is the defined refusal of an unknown speed): %s", detail);
            break;
        case TAS_HOLD_COMMIT_NOT_CAPTURED:
            sprintf_s(reason, sizeof(reason), "a raw NTSTATUS was not captured (no driver completion observed) and no violation was "
                      "observed: not established");
            break;
        default:
            sprintf_s(reason, sizeof(reason), "expected exactly one complete STATUS_SUCCESS reply and one STATUS_DEVICE_BUSY or "
                      "STATUS_INVALID_DEVICE_STATE with no output; see the two calls above%s", ev.pre_generation == 0 ||
                      ev.pre_epoch == 0 ? " (the requests carried generation or epoch 0, which #352 R1 requires to be refused STATE_CONFLICT; see "
                      "METADATA)" : "");
            break;
        }
        tag = cc == TAS_HOLD_COMMIT_PASS ? "PASS" : (cc == TAS_HOLD_COMMIT_OUT_UNJUDGED || cc == TAS_HOLD_COMMIT_PAIR_CHANGED ||
              cc == TAS_HOLD_COMMIT_SPEED_UNDETERMINED || cc == TAS_HOLD_COMMIT_NOT_CAPTURED ? "INCONCLUSIVE" : "FAIL");
        verdict(tag, "COMMIT", index, id->device_id, reason);

        /* The schedule is committed when exactly one call completed STATUS_SUCCESS with the 144-byte reply; its output fields are
         * COMMIT's to judge, so a wrong output never suppresses the independent STATE, REFUSE and WIRE observations */
        committed = success == 1 && calls[winner].got == sizeof(calls[winner].r);
        if (committed) {
            req_op = calls[winner].sent;
            printf("    [EVIDENCE] the competing request carried the same schedule: after the commit its own hardware effect cannot be "
                   "separated from the winner's; the query after the race is evidence of one schedule, not proof that the refusal wrote "
                   "nothing\n");
            armed_ok = read_state(h, &s);
            if (armed_ok) { print_state("after the activation", &s); }
            verdict(armed_ok && state_matches(&s, base) ? "PASS" : "FAIL", "STATE", index, id->device_id,
                    armed_ok && state_matches(&s, base) ? NULL : "GET_TAS_STATE does not report ARMED with the requested gate list");
            if (!query_valid(&qa)) {
                /* The further request carries the generation and epoch of the test's own query (#369 amendment); after the commit
                 * that is this query, so without it the request has no current metadata and is not sent (PR #481) */
                verdict("BLOCKED", "REFUSE", index, id->device_id, "the query after the commit failed: no current generation and "
                        "epoch for the further request, which is not sent");
            } else {
                /* NOT_CLEAN and STATE_CONFLICT both complete STATUS_INVALID_DEVICE_STATE (#365 13.3, 13.4), so a refusal is attributed
                 * to the armed state only when the request carried reported post-commit metadata (0 = not reported, #365 12.2
                 * offset 88; unknown values are not wildcards, #369). Acceptance, output or a changed schedule fails whatever the
                 * metadata: an armed adapter must refuse and keep its schedule (#369 REFUSE). The request carries the acceptance
                 * flag, so R7's NOT_CLEAN (step 9) is reached after the tier step (8) */
                OP_CALL again;
                BOOL meta_ok = qa.q.generation != 0 && qa.q.context_epoch != 0, refused;
                ZeroMemory(&again, sizeof(again));
                build_op(&again.sent, id->net_luid, &qa.q, base + CYCLE_NS, frame);
                again.sent.flags = AVB_TAS_F_ACCEPT_UNVERIFIED;
                again.r = again.sent;
                again.status = avb_ioctl_ntstatus(h, IOCTL_AVB_TAS_ACTIVATE_80C, &again.r, sizeof(again.r), &again.r, sizeof(again.r), &again.got);
                printf("    further request: base=%lld flags=0x%X gen=%llu epoch=%llu (from the query after the commit)\n", base + CYCLE_NS,
                       again.sent.flags, qa.q.generation, qa.q.context_epoch);
                print_op("activation on the armed adapter", &again);
                armed_ok = read_state(h, &s);
                refused = again.status == (LONG)STATUS_INVALID_DEVICE_STATE && again.got == 0 && armed_ok && state_matches(&s, base);
                if (refused && meta_ok) {
                    verdict("PASS", "REFUSE", index, id->device_id, NULL);
                } else if (refused) {
                    verdict("INCONCLUSIVE", "REFUSE", index, id->device_id, "reported, not judged: STATUS_INVALID_DEVICE_STATE with no "
                            "output and the schedule kept, but the query after the commit reports generation or epoch 0 (not reported, "
                            "#352 R1), so NOT_CLEAN cannot be told from STATE_CONFLICT");
                } else {
                    verdict("FAIL", "REFUSE", index, id->device_id, "an armed adapter must refuse activation with "
                            "STATUS_INVALID_DEVICE_STATE, no output, and keep the reported schedule");
                }
            }
            query_lookup(calls[winner].h, req_op.client_op_id, &ql);
            printf("    [EVIDENCE] LOOKUP: query on the committing open instance (%s) with client_op_id 0x%016llX: ntstatus=0x%08lX "
                   "information=%lu lookup_result=%u (the producer stage expects NOT_IMPLEMENTED 4; success-record retrieval is #460, "
                   "not asserted here)\n", winner == 0 ? "instance 1" : "instance 2", req_op.client_op_id, (unsigned long)ql.status, ql.got,
                   query_valid(&ql) ? ql.q.lookup_result : 0u);
        } else {
            const char *no = success != 0 ? "no single complete commit was established (see COMMIT); the adapter may hold a schedule, "
                                            "the owned cleanup follows" : "no committed schedule (see COMMIT)";
            verdict("BLOCKED", "STATE", index, id->device_id, no);
            verdict("BLOCKED", "REFUSE", index, id->device_id, no);
            printf("    [EVIDENCE] LOOKUP: not queried, %s\n", no);
        }
        CloseHandle(h2);

        /* GENERATION across the race (#352 R1 L2/L7/L9), from the queries immediately before and after it */
        switch (gc) {
        case TAS_HOLD_GEN_PASS:
            sprintf_s(reason, sizeof(reason), "generation %llu -> %llu in context %llu: %u transition(s), one per commit and none per "
                      "refusal before any write (#352 R1)", ev.pre_generation, ev.post_generation, ev.pre_epoch, transitions);
            break;
        case TAS_HOLD_GEN_FAIL:
            if (ev.post_generation < ev.pre_generation) {
                sprintf_s(reason, sizeof(reason), "generation %llu -> %llu in context %llu: a decrease (never, #352 R1 L12)",
                          ev.pre_generation, ev.post_generation, ev.pre_epoch);
            } else {
                sprintf_s(reason, sizeof(reason), "generation %llu -> %llu in context %llu, expected +%u: one per write-attempting "
                          "mutation and none per refusal before any write (#352 R1 L2/L7/L9)", ev.pre_generation, ev.post_generation,
                          ev.pre_epoch, transitions);
            }
            break;
        case TAS_HOLD_GEN_PRE_UNREPORTED:
            sprintf_s(reason, sizeof(reason), "not judged: the query before the race %s (0 = not reported, never a value; METADATA "
                      "carries it)", race_valid ? "reports generation or epoch 0" : "is not valid");
            break;
        case TAS_HOLD_GEN_POST_UNREPORTED:
            sprintf_s(reason, sizeof(reason), "not judged: the query after the race %s (0 = not reported, never a value)",
                      ev.post_valid ? "reports generation or epoch 0" : "is not valid");
            break;
        case TAS_HOLD_GEN_EPOCH_CHANGED:
            sprintf_s(reason, sizeof(reason), "not judged: the context epoch changed across the race (%llu -> %llu), another adapter "
                      "context (#352 R1 L11)", ev.pre_epoch, ev.post_epoch);
            break;
        default:
            sprintf_s(reason, sizeof(reason), "not judged: a completion's write attempt is not established (not captured, or a status "
                      "shared by outcomes before and after a write, #365 R5/R8); generation %llu -> %llu", ev.pre_generation,
                      ev.post_generation);
            break;
        }
        verdict(gc == TAS_HOLD_GEN_PASS ? "PASS" : (gc == TAS_HOLD_GEN_FAIL ? "FAIL" : "INCONCLUSIVE"), "GENERATION", index,
                id->device_id, reason);
    }

    if (committed && wire) {
        DWORD start = GetTickCount();
        long long now = 0;
        unsigned n, seq, end;
        char reason[256];

        printf("\n  --- Tagged frames across BaseT ---\n");
        while (GetTickCount() - start < SEND_BOUND_MS && (!read_phc(h, &now) || now < base + SEND_CYCLES_AFTER * CYCLE_NS + CYCLE_NS / 2)) {
            if (send_tagged(sock, nonce) == SEND_FAILED) {
                raw_send_failed++;
            }
            Sleep(SEND_PERIOD_MS);
        }
        Sleep(DRAIN_MS);
        wire_counts(&captured, &from_target);
        anchor_ok = take_anchor(h, &after_anchor, "after");
        wire_stop();
        closesocket(sock);
        sock = INVALID_SOCKET;
        n = collect_frames(first_after, g_next_seq, &before_anchor, &after_anchor, frames);
        /* Raw counts from the capture, independent of the PHC mapping; the cleanup's TX probe reuses the capture arrays */
        end = g_next_seq < WIRE_MAX_FRAMES ? g_next_seq : WIRE_MAX_FRAMES;
        for (seq = first_after; seq < end; seq++) {
            raw_sent++;
            raw_seen += g_wire_seen[seq] != 0;
        }
        mapped = anchor_ok && n == raw_sent;
        wclass = tas_act_wire(AVB_TAS_PRE_HOLD, frames, n, captured != 0, base, CYCLE_NS, WINDOW_START_NS, WINDOW_END_NS, TAS_ACT_EDGE_MARGIN_NS, &wc);
        printf("    wire: %s frames=%u seen=%u early=%u heldBeforeBase=%u flowed=%u released=%u inside=%u nearEdge=%u outside=%u captured=%ld "
               "fromTarget=%ld\n", tas_act_wire_name(wclass), wc.frames, wc.seen, wc.early, wc.held_before_base, wc.flowed, wc.released, wc.inside,
               wc.near_edge, wc.outside, captured, from_target);
        sprintf_s(reason, sizeof(reason), "%s; see the wire line", tas_act_wire_name(wclass));
        verdict(wclass == TAS_ACT_WIRE_GATED ? "PASS" : (wclass == TAS_ACT_WIRE_UNOBSERVED ? "INCONCLUSIVE" : "FAIL"), "WIRE", index, id->device_id,
                wclass == TAS_ACT_WIRE_GATED ? NULL : reason);
    } else {
        if (wire) { wire_stop(); }
        if (sock != INVALID_SOCKET) { closesocket(sock); sock = INVALID_SOCKET; }
        verdict("BLOCKED", "WIRE", index, id->device_id, !committed ? "no committed schedule (see COMMIT)" : "no established wire observer (see OBSERVER)");
    }
    verdict("SKIP_NOT_IMPLEMENTED", "STATUS", index, id->device_id,
            "#369 expects PENDING then INFERRED_ACTIVE; the schedule status model of #350 is not implemented, GET_TAS_STATE has no status");
    restore = cleanup_and_verify(h, index, id, ipv4, &stats, phc, qpc, &qbefore);
    g_cleanup_link_was_up = TRUE;
    if (g_mode == MODE_HOLD && committed && wire) {
        /* HOLD characterization: measurements only, no verdict, not EG-02 qualification. The raw counts come from the capture
         * itself: lost = tagged frames sent after the activation and not seen. The baseline pair is the capture's own loss before
         * any mutation. The timing counts need the PHC mapping; without it they are reported unavailable, never as zeros. Near-edge
         * arrivals are counted apart from inside and outside by the classifier; released is a subset of the first window */
        printf("    [EVIDENCE] HOLD characterization: sent=%u sendFailed=%u seen=%u lost=%u captured=%ld fromTarget=%ld baselineSent=%u "
               "baselineSendFailed=%u baselineSeen=%u restoration=%s\n", raw_sent, raw_send_failed, raw_seen, raw_sent - raw_seen,
               captured, from_target, sent, send_failed, seen,
               restore == TAS_ACT_RESTORE_COMPLETE ? "COMPLETE" : (restore == TAS_ACT_RESTORE_UNVERIFIED ? "UNVERIFIED" : "INCOMPLETE"));
        if (mapped) {
            printf("    [EVIDENCE] HOLD characterization timing (PHC-mapped): wire=%s frames=%u seen=%u early=%u released=%u inside=%u "
                   "nearEdge=%u outside=%u\n", tas_act_wire_name(wclass), wc.frames, wc.seen, wc.early, wc.released, wc.inside,
                   wc.near_edge, wc.outside);
        } else {
            printf("    [EVIDENCE] HOLD characterization timing: unavailable, the PHC mapping failed (after anchor %s, %u of %u frames "
                   "mapped); early, released, inside, nearEdge and outside are not reported. The WIRE verdict above is unchanged\n",
                   anchor_ok ? "read" : "not read", wc.frames, raw_sent);
        }
        printf("    [EVIDENCE] HOLD characterization limits: the driver's register write order is not observable by this test "
               "(#365 13.5 records the source order); capture = passive Npcap, software timestamps on the host clock, through a "
               "switch, mapped to PHC time through the two anchors above (see their bracketUs); arrivals within %lld ms of a window "
               "edge are nearEdge and decide nothing; one frame every %d ms; cycle %lld ms, window %lld..%lld ms. 0 outside does not "
               "by itself prove that no frame left outside a hardware window\n", TAS_ACT_EDGE_MARGIN_NS / NS_PER_MS, SEND_PERIOD_MS,
               CYCLE_NS / NS_PER_MS, WINDOW_START_NS / NS_PER_MS, WINDOW_END_NS / NS_PER_MS);
    }
}

int main(int argc, char **argv)
{
    AVB_ADAPTER_IDENTITY_REQUEST *ids = NULL, *all = NULL, first;
    BOOL complete = FALSE;
    IOCTL_VERSION version = {0};
    WSADATA wsa;
    DWORD br = 0;
    HANDLE h;
    UINT32 i = 0, count = 0, stored = 0, nall = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    SetUnhandledExceptionFilter(on_crash);
    if (argc == 2 && strcmp(argv[1], "--hold") == 0) { g_mode = MODE_HOLD; g_case = "TC-TASAPI-004"; }
    if (argc == 2 && strcmp(argv[1], "--pass") == 0) { g_mode = MODE_PASS; g_case = "TC-TASAPI-005"; }
    if (argc == 2 && strcmp(argv[1], "--fault") == 0) { g_mode = MODE_FAULT; g_case = "TC-TASACT-FAULT-001"; }
    if (argc == 2 && strcmp(argv[1], "--fault-versioned") == 0) { g_mode = MODE_FAULTV; g_case = "TC-TASACT-FAULT-001"; }
    printf("=== IntelAvbFilter - IOCTL_AVB_TAS_ACTIVATE on real hardware (#357) ===\n");
    if (g_mode == MODE_NONE) {
        printf("[BLOCKED] TC-TASAPI-004: usage: test_tas_activate --hold | --pass | --fault | --fault-versioned\n");
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

    /* Identities first (a NIC cycle may reorder the enumeration), every one of them: the arrays are sized by the driver's count,
     * no fixed inventory; an incomplete inventory is a nonpassing setup result, never silence. No observer is chosen by model */
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
            all[nall++] = id;
            if (id.capabilities & INTEL_CAP_TSN_TAS) {
                ids[stored++] = id;
            } else {
                printf("[SKIP_UNSUPPORTED] %s-COMMIT/adapter:%u did=0x%04X: adapter does not advertise INTEL_CAP_TSN_TAS\n", g_case, i, id.device_id);
            }
        }
    }
    CloseHandle(h);
    if (!complete) {
        printf("[BLOCKED] %s-DISCOVERY: adapter discovery incomplete: %u of %u identities validated; untested adapters are not passes\n",
               g_case, i, count);
        g_blocked++;
    }
    g_all = all;
    g_nall = nall;
    for (i = 0; i < stored; i++) {   /* every TAS adapter, no fixed limit */
        g_tas_adapters++;
        if (g_mode == MODE_FAULTV) {
            /* #482: F1 then F2, each a complete run with its own handles, pre-state, observer and owned cleanup */
            g_fault_case = 1;
            g_case = "TC-TASACT-FAULT-001-F1";
            run_adapter(ids[i].index, &ids[i]);
            g_fault_case = 2;
            g_case = "TC-TASACT-FAULT-001-F2";
            run_adapter(ids[i].index, &ids[i]);
            g_case = "TC-TASACT-FAULT-001";
        } else {
            run_adapter(ids[i].index, &ids[i]);
        }
    }
    free(ids);
    free(all);
    WSACleanup();

    printf("\nTAS adapters run=%d case executions=%d nonpassing verdicts=%d blocked=%d\n", g_tas_adapters, g_ran, g_failed, g_blocked);
    if (g_ran == 0) { return 2; }
    if (g_failed != 0) { return 1; }
    return g_blocked != 0 ? 3 : 0;
}
