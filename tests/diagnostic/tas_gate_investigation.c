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
 * --pattern <startMs> <endMs> asks whether the adapter obeys a real gate pattern. Capture times are host-side, so a 1 ms
 * cycle cannot be resolved: this uses a 1 s cycle, permitted by the manual's cycle table, with one window. Every frame's
 * send and capture time and bracketed PHC reads are printed for offline analysis. Shifting the window between runs is the
 * control: arrivals must move with it, which needs no absolute time alignment. Window end leaves a gap before the cycle
 * end, as the manual requires. BaseT is the latest cycle boundary, so slightly in the past: a boundary ahead would be up
 * to 1 s in the future, which upstream warns about on I226.
 *
 * --pattern ... --no-shadow leaves QBVCYCLET_S at 0: frames passing outside the window would mean gating was off, not on.
 * --pattern ... --after-rearm applies the pattern after an in-place disarm and a second SETUP_TAS. BaseT is then already
 * running, so the analysis takes its phase from the register, not from the value written.
 *
 * --pattern ... --mode-last does not use SETUP_TAS: windows, shadow, cycle and BaseT go in while in Legacy mode, and
 * TQAVCTRL is written last, so nothing is ever held by a half-programmed schedule. The driver is not told, so tasArmed stays 0.
 *
 * Both of these start from a verified pattern, because a pattern after an in-place disarm was seen ungated:
 * --pattern ... --toggle-only writes TQAVCTRL to 0 and back and nothing else, to isolate the mode change.
 * --pattern ... --soft-disarm never leaves TSN mode: it opens the windows over the whole cycle, then moves them to 600-800 ms.
 * That would respect the manual's rule on the transmit mode, at the cost of the adapter staying in TSN mode until a NIC cycle.
 *
 * --soft-disarm showed that window writes alone read back but do not reach a running schedule. These ask what does, by
 * moving a verified 200-400 ms pattern to 600-800 ms, where the wire answers regardless of what the registers say:
 * --pattern ... --move-cycle writes windows, shadow and cycle. --pattern ... --move-base also writes the latest, past boundary
 * to BaseT. --pattern ... --move-future writes a boundary ahead instead, which is upstream's path for a running schedule,
 * without its TXQCTL strict flags. --pattern ... --move-futscd adds FutScdDis and the BASET_L zero write, which upstream uses only
 * when no schedule is running. --pattern ... --move-toggle writes the windows with TQAVCTRL at 0, then restores TQAVCTRL.
 *
 * --move-future moved a running schedule at the intended boundary, so these extend that route. --pattern ... --move-open-future
 * opens the whole cycle with a future BaseT, stays in TSN mode, then applies 600-800 ms the same way, which replaces a schedule
 * twice and tries it as an in-place disarm and re-arm. --pattern ... --move-cycle-future changes the cycle to 500 ms, with a
 * 100-300 ms window, a value in the manual's cycle table. --pattern ... --move-windows-future writes only windows and a future
 * BaseT, to show whether the shadow and cycle writes are needed when the cycle stays the same.
 *
 * --pattern <startMs> <endMs> --short-cycle uses a 125 ms cycle, a value in the manual's cycle table.
 *
 * --pattern ... --move-strict-end narrows the window to 400 ns by a future BaseT, without and with Strict_End. No frame fits
 * that window, so it cannot tell Strict_End from a separate defect, and its drain of about 360 frames outruns the observer.
 *
 * --pattern ... --move-strict-matrix replaces it. Frame time at 1 Gbit/s is (payload + 54) * 8 ns: 0.94 us for 64 bytes,
 * 11.6 us for 1400. A 5 us window fits the small frame only, and a 50 us window fits both. Stages: small in 5 us without and
 * with Strict_End, large in 50 us with it, large in 5 us without and with it, then a wide window with the flag clear. The
 * fitting stages under Strict_End are the control: if they stall too, the cause is not Strict_End. Sending at 2.5 frames per
 * second keeps what is held near 50, far below the drain that outran the observer. Capture times are host-side, so this
 * counts frames per window and cannot see a frame cross the closing edge.
 *
 * --pattern ... --future-base starts the first schedule from a BaseT 1 to 2 s ahead, without FutScdDis, and
 * --pattern ... --future-base-futscd does the same with FutScdDis and the BASET_L zero write, as upstream does for a first
 * schedule. Upstream's comments say a future BaseT holds packets until then and that this causes TX hangs. The hold here
 * stays under its 5 s watchdog, so these ask when the schedule starts and what happens to frames before it, not about a hang.
 *
 * --disarm opens egress, then writes the original values back without a NIC cycle and checks TX, PHC and driver state.
 * The manual says the transmit mode cannot change during dynamic operation, and upstream resets the adapter here, so this
 * tests behaviour against that guideline and a good result is not a supported sequence. The NIC cycle still runs afterwards.
 *
 * --rearm <n> disarms in place and arms again n times without a NIC cycle. TQAVCTRL=0 alone proves nothing, so each cycle
 * checks registers, driver state, TX past the fill level and PHC, then holds frames after SETUP_TAS long enough to reach the
 * fill level again. That level is the only indirect view of internal buffer state.
 *
 * Except with --rearm, each step stays below the fill level seen on 2026-11-05, so a stalled ring cannot hide an opening window.
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
#define PATTERN_MS           12000
#define PATTERN_SETTLE_MS    1500
#define PATTERN_MAX_END_MS   900
#define DISARM_TX_MS         6000
#define REARM_FILL_MS        6000
#define MAX_REARM_CYCLES     20
#define PHC_PAIRS            5
#define BASELINE_HEAD_FRAMES 5
#define FUTURE_BOUNDARIES    2
#define SAMPLE_MS            50
#define SAMPLER_JOIN_MS      5000
#define TCTL_COLD_SHIFT      12
#define TCTL_COLD_FIELD      0x3FFu
#define SOFT_REOPEN_START_NS 600000000u
#define SOFT_REOPEN_END_NS   800000000u
#define NEW_CYCLE_NS         500000000u
#define NEW_CYCLE_START_NS   100000000u
#define NEW_CYCLE_END_NS     300000000u
#define SHORT_CYCLE_NS       125000000u
#define NARROW_START_NS      200000000u
#define NARROW_WINDOW_NS     400u
#define LARGE_PAYLOAD_BYTES  1400u
#define WINDOW_SMALL_ONLY_NS 5000u
#define WINDOW_BOTH_NS       50000u
#define MATRIX_PERIOD_MS     400
#define WIDE_START_NS        200000000u
#define WIDE_END_NS          400000000u
#define FILETIME_TO_UNIX_US  11644473600000000LL
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
static const uint32_t g_txqctl[QUEUES] = {I226_TXQCTL0, I226_TXQCTL1, I226_TXQCTL2, I226_TXQCTL3};

static unsigned g_next_seq;
static volatile LONG g_armed;
static uint32_t g_cycle_ns = CYCLE_NS, g_open_start_ns = 0, g_open_end_ns = CYCLE_NS;
static BOOL g_pattern, g_disarm, g_after_rearm, g_mode_last, g_toggle_only, g_soft_disarm;
static const char *g_move;
static BOOL g_future_base, g_futscd;
static unsigned g_payload_bytes = PAYLOAD_BYTES;
static DWORD g_send_period_ms = SEND_PERIOD_MS;
static avb_u64 g_capture_luid;
static HANDLE g_sampler;
static volatile LONG g_sampler_stop;
static unsigned g_rearm_cycles;
static LONGLONG g_send_us[WIRE_MAX_FRAMES];

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

/** Host clock in the same epoch as the capture times */
static LONGLONG host_us(void)
{
    FILETIME ft;
    ULARGE_INTEGER t;

    GetSystemTimePreciseAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (LONGLONG)(t.QuadPart / 10) - FILETIME_TO_UNIX_US;
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
    static unsigned char payload[LARGE_PAYLOAD_BYTES];
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
        tas_wire_fill(payload, g_payload_bytes, g_wire_nonce, g_next_seq);
        if (g_next_seq < WIRE_MAX_FRAMES) {
            g_send_us[g_next_seq] = host_us();
        }
        if (sendto(sock, (const char *)payload, (int)g_payload_bytes, 0, (SOCKADDR *)&peer, sizeof(peer)) != SOCKET_ERROR) {
            sent++;
            g_next_seq++;
        } else {
            errors++;
        }
        Sleep(g_send_period_ms);
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
        if (!write_reg(h, g_stqt[q], g_open_start_ns, "STQT") || !write_reg(h, g_endqt[q], g_open_end_ns, "ENDQT")) {
            return FALSE;
        }
    }
    return TRUE;
}

/** s: shadow cycle first, then cycle, as upstream */
static BOOL step_shadow(HANDLE h)
{
    return write_reg(h, I226_QBVCYCLET_S, g_cycle_ns, "QBVCYCLET_S") && write_reg(h, I226_QBVCYCLET, g_cycle_ns, "QBVCYCLET");
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
    base = (systim / g_cycle_ns + (g_future_base ? FUTURE_BOUNDARIES : (g_pattern ? 0 : 1))) * g_cycle_ns;
    ok = write_reg(h, I226_BASET_H, (uint32_t)(base / NS_PER_SEC), "BASET_H") &&
         (!g_futscd || write_reg(h, I226_BASET_L, 0, "BASET_L")) &&
         write_reg(h, I226_BASET_L, (uint32_t)(base % NS_PER_SEC), "BASET_L");
    if (read_systim(h, &after)) {
        printf("    PHC before=%llu baseT=%llu after=%llu, baseT - PHC after write = %lld ns\n", systim, base, after, (long long)base - (long long)after);
    }
    return ok;
}

/** Upstream's order: after the windows, before the cycle and BaseT */
static BOOL step_futscd(HANDLE h)
{
    uint32_t mode = 0;

    return read_reg(h, I226_TQAVCTRL, &mode) && write_reg(h, I226_TQAVCTRL, mode | (uint32_t)I226_TQAVCTRL_FUTSCDDIS_MASK, "TQAVCTRL");
}

/** Writes the groups named in letters, in the order w, s, c, b. c is the cycle alone, for a run without the shadow */
static BOOL apply_groups(HANDLE h, const char *letters)
{
    return (strchr(letters, 'w') == NULL || step_windows(h)) && (!g_futscd || step_futscd(h)) && (strchr(letters, 's') == NULL || step_shadow(h)) &&
           (strchr(letters, 'c') == NULL || write_reg(h, I226_QBVCYCLET, g_cycle_ns, "QBVCYCLET")) &&
           (strchr(letters, 'b') == NULL || step_base(h));
}

/** Bracketed PHC reads: the mid host time and half width give the host-to-PHC mapping and its uncertainty */
static void phc_pairs(HANDLE h, const char *label)
{
    int i;

    for (i = 0; i < PHC_PAIRS; i++) {
        unsigned long long phc = 0;
        LONGLONG t0 = host_us(), t1;
        BOOL ok = read_systim(h, &phc);
        t1 = host_us();
        printf("    phcpair %s hostMidUs=%lld halfUs=%lld phcNs=%llu ok=%d\n", label, (t0 + t1) / 2, (t1 - t0) / 2, phc, ok);
    }
}

/** Raw per-frame evidence; rxUs=-1 means not seen */
static void dump_frames(void)
{
    unsigned seq;

    for (seq = 0; seq < g_next_seq && seq < WIRE_MAX_FRAMES; seq++) {
        printf("    frame seq=%u sendUs=%lld rxUs=%lld\n", seq, g_send_us[seq], g_wire_seen[seq] != 0 ? g_wire_rx_us[seq] : -1LL);
    }
}

/** The first baseline frames, in every run: an early frame was seen missing or delayed after the machine sat idle */
static void dump_baseline_head(unsigned first)
{
    unsigned seq;

    for (seq = first; seq < first + BASELINE_HEAD_FRAMES && seq < g_next_seq; seq++) {
        printf("    baseline frame seq=%u sendUs=%lld rxUs=%lld delayUs=%lld\n", seq, g_send_us[seq],
               g_wire_seen[seq] != 0 ? g_wire_rx_us[seq] : -1LL, g_wire_seen[seq] != 0 ? g_wire_rx_us[seq] - g_send_us[seq] : -1LL);
    }
}

static uint32_t baseline_of(const uint32_t *baseline, uint32_t offset)
{
    unsigned i;

    for (i = 0; i < REG_COUNT; i++) {
        if (g_regs[i].offset == offset) {
            return baseline[i];
        }
    }
    return 0;
}

/** Writes the original values back, in upstream's disable order. A fuller mode change would also reset; that is the question */
static BOOL step_disarm(HANDLE h, const uint32_t *baseline)
{
    static const REG order[] = {
        {"TQAVCTRL", I226_TQAVCTRL},
        {"STQT0", I226_STQT0}, {"ENDQT0", I226_ENDQT0}, {"STQT1", I226_STQT1}, {"ENDQT1", I226_ENDQT1},
        {"STQT2", I226_STQT2}, {"ENDQT2", I226_ENDQT2}, {"STQT3", I226_STQT3}, {"ENDQT3", I226_ENDQT3},
        {"QBVCYCLET_S", I226_QBVCYCLET_S}, {"QBVCYCLET", I226_QBVCYCLET}, {"BASET_H", I226_BASET_H}, {"BASET_L", I226_BASET_L},
    };
    unsigned i;
    BOOL ok = TRUE;

    for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        ok &= write_reg(h, order[i].offset, baseline_of(baseline, order[i].offset), order[i].name);
    }
    return ok;
}

/** PHC against elapsed time since before arming, without any NIC cycle */
static void report_phc(HANDLE h, unsigned long long phc_before, LARGE_INTEGER qpc_before, const char *label)
{
    LARGE_INTEGER qpc, frequency;
    unsigned long long phc = 0;
    long long wall_ns, drift_ns;

    if (!read_systim(h, &phc)) {
        printf(TAG " %s: PHC unreadable\n", label);
        return;
    }
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&frequency);
    wall_ns = (long long)((double)(qpc.QuadPart - qpc_before.QuadPart) * 1e9 / (double)frequency.QuadPart);
    drift_ns = ((long long)phc - (long long)phc_before) - wall_ns;
    printf(TAG " %s: PHC before=%llu now=%llu wallNs=%lld driftNs=%lld\n", label, phc_before, phc, wall_ns, drift_ns);
}

/** The existing driver path, to record what it does today; no hardware write happens if it reports not supported */
static void report_driver_disarm(HANDLE h)
{
    AVB_DISARM_TAS_REQUEST d = {0};
    DWORD br = 0, error;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_DISARM_TAS, &d, sizeof(d), &d, sizeof(d), &br, NULL);
    unsigned armed = 99;

    error = ok ? ERROR_SUCCESS : GetLastError();
    (void)read_armed(h, &armed);
    printf(TAG " driver DISARM_TAS: ok=%d error=%lu status=0x%08X; tasArmed=%u\n", ok, error, d.status, armed);
}

/** Disarm in place, prove Legacy TX past the fill level, arm again, hold to the fill level, reopen and prove the flush */
static void rearm_cycles(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, unsigned long long phc_before,
                         LARGE_INTEGER qpc_before, BOOL *capture_alive)
{
    uint32_t now[REG_COUNT];
    char label[64];
    unsigned i, armed, first;

    for (i = 1; i <= g_rearm_cycles; i++) {
        printf("  --- cycle %u: disarm in place ---\n", i);
        (void)step_disarm(h, baseline);
        sprintf_s(label, sizeof(label), "cycle %u after disarm", i);
        (void)read_regs(h, now, label, baseline);
        armed = 99;
        (void)read_armed(h, &armed);
        printf(TAG " %s: driver tasArmed=%u\n", label, armed);
        sprintf_s(label, sizeof(label), "cycle %u legacy TX", i);
        (void)tx_step(h, luid, sock, DISARM_TX_MS, label, g_next_seq, capture_alive);
        report_phc(h, phc_before, qpc_before, label);

        printf("  --- cycle %u: re-arm ---\n", i);
        first = g_next_seq;
        if (!arm_tas(h)) {
            printf(TAG " cycle %u: SETUP_TAS failed (error %lu); stopping the cycles\n", i, GetLastError());
            return;
        }
        sprintf_s(label, sizeof(label), "cycle %u after SETUP_TAS", i);
        (void)read_regs(h, now, label, baseline);
        sprintf_s(label, sizeof(label), "cycle %u held to fill level", i);
        (void)tx_step(h, luid, sock, REARM_FILL_MS, label, first, capture_alive);
        if (!apply_groups(h, "wsb")) {
            printf(TAG " cycle %u: schedule writes failed; stopping the cycles\n", i);
            return;
        }
        sprintf_s(label, sizeof(label), "cycle %u after schedule", i);
        (void)read_regs(h, now, label, baseline);
        sprintf_s(label, sizeof(label), "cycle %u reopened", i);
        (void)tx_step(h, luid, sock, STEP_MS, label, first, capture_alive);
        report_phc(h, phc_before, qpc_before, label);
    }
}

/** The schedule registers as read, equal to baseline or not, beside the PHC. Reading them back does not show the schedule in force */
static void schedule_line(HANDLE h, const char *label)
{
    static const REG fields[] = {
        {"TQAVCTRL", I226_TQAVCTRL}, {"BASET_H", I226_BASET_H}, {"BASET_L", I226_BASET_L}, {"QBVCYCLET", I226_QBVCYCLET}, {"QBVCYCLET_S", I226_QBVCYCLET_S},
        {"STQT0", I226_STQT0}, {"ENDQT0", I226_ENDQT0}, {"STQT1", I226_STQT1}, {"ENDQT1", I226_ENDQT1},
        {"STQT2", I226_STQT2}, {"ENDQT2", I226_ENDQT2}, {"STQT3", I226_STQT3}, {"ENDQT3", I226_ENDQT3},
    };
    unsigned long long phc = 0;
    uint32_t value;
    unsigned i;

    (void)read_systim(h, &phc);
    printf("    schedule %s phcNs=%llu", label, phc);
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (read_reg(h, fields[i].offset, &value)) {
            printf(" %s=0x%08X", fields[i].name, value);
        } else {
            printf(" %s=unreadable", fields[i].name);
        }
    }
    printf("\n");
}

/** The observer adapter's own counters, which show whether frames reached it when the capture did not report them */
static void observer_line(const char *label)
{
    MIB_IF_ROW2 row;

    ZeroMemory(&row, sizeof(row));
    row.InterfaceLuid.Value = g_capture_luid;
    if (GetIfEntry2(&row) == NO_ERROR) {
        printf("    observer %s inNUcast=%llu inDiscards=%llu inErrors=%llu\n", label, row.InNUcastPkts, row.InDiscards, row.InErrors);
    } else {
        printf("    observer %s counters unreadable\n", label);
    }
}

/** The timed part of a pattern run, with its raw per-frame evidence */
static unsigned pattern_run(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, unsigned armed_first, BOOL *capture_alive)
{
    uint32_t now[REG_COUNT];
    unsigned seen;

    printf(TAG " pattern: cycle=%u ns window=[%u,%u) ns patternFirstSeq=%u\n", g_cycle_ns, g_open_start_ns, g_open_end_ns, g_next_seq);
    schedule_line(h, "start");
    observer_line("start");
    phc_pairs(h, "start");
    seen = tx_step(h, luid, sock, PATTERN_MS, "pattern", armed_first, capture_alive);
    Sleep(PATTERN_SETTLE_MS);
    phc_pairs(h, "end");
    observer_line("end");
    schedule_line(h, "end");
    (void)read_regs(h, now, "after the pattern", baseline);
    dump_frames();
    return seen;
}

/** The mode alone, leaving the schedule registers as they are */
static void toggle_only(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, BOOL *capture_alive)
{
    uint32_t now[REG_COUNT], mode = 0;

    printf("  --- TQAVCTRL only: to 0 and back ---\n");
    if (!read_reg(h, I226_TQAVCTRL, &mode) || !write_reg(h, I226_TQAVCTRL, 0, "TQAVCTRL")) {
        return;
    }
    (void)read_regs(h, now, "TQAVCTRL 0", baseline);
    (void)tx_step(h, luid, sock, STEP_MS, "TQAVCTRL 0", g_next_seq, capture_alive);
    if (write_reg(h, I226_TQAVCTRL, mode, "TQAVCTRL")) {
        (void)read_regs(h, now, "TQAVCTRL restored", baseline);
        (void)pattern_run(h, luid, sock, baseline, g_next_seq, capture_alive);
    }
}

/** Stays in TSN mode: whole cycle open, then a different window */
static void soft_disarm(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, BOOL *capture_alive)
{
    uint32_t now[REG_COUNT];

    printf("  --- windows open over the whole cycle, TSN mode kept ---\n");
    g_open_start_ns = 0;
    g_open_end_ns = g_cycle_ns;
    if (!step_windows(h)) {
        return;
    }
    (void)read_regs(h, now, "soft disarmed", baseline);
    (void)pattern_run(h, luid, sock, baseline, g_next_seq, capture_alive);
    printf("  --- new window, TSN mode kept ---\n");
    g_open_start_ns = SOFT_REOPEN_START_NS;
    g_open_end_ns = SOFT_REOPEN_END_NS;
    if (step_windows(h)) {
        (void)read_regs(h, now, "soft re-armed", baseline);
        (void)pattern_run(h, luid, sock, baseline, g_next_seq, capture_alive);
    }
}

/** One replacement by a future BaseT, then a timed pattern */
static BOOL replace_schedule(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, uint32_t start_ns, uint32_t end_ns, BOOL *capture_alive)
{
    uint32_t now[REG_COUNT];

    g_future_base = TRUE;
    g_open_start_ns = start_ns;
    g_open_end_ns = end_ns;
    if (!apply_groups(h, "wsb")) {
        return FALSE;
    }
    (void)read_regs(h, now, "after the replacement", baseline);
    return pattern_run(h, luid, sock, baseline, g_next_seq, capture_alive) != 0;
}

static BOOL set_strict_end(HANDLE h, BOOL on)
{
    int q;

    for (q = 0; q < QUEUES; q++) {
        if (!write_reg(h, g_txqctl[q], on ? (uint32_t)I226_TXQCTL_STRICT_END_MASK : 0, "TXQCTL")) {
            return FALSE;
        }
    }
    return TRUE;
}

/** A window too short for a frame, without and with Strict_End, then a wide one to release what was held */
static void strict_end_run(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, BOOL *capture_alive)
{
    printf("  --- narrow window, Strict_End clear ---\n");
    (void)replace_schedule(h, luid, sock, baseline, NARROW_START_NS, NARROW_START_NS + NARROW_WINDOW_NS, capture_alive);
    printf("  --- narrow window, Strict_End set ---\n");
    if (set_strict_end(h, TRUE)) {
        (void)replace_schedule(h, luid, sock, baseline, NARROW_START_NS, NARROW_START_NS + NARROW_WINDOW_NS, capture_alive);
    }
    printf("  --- wide window, Strict_End clear ---\n");
    if (set_strict_end(h, FALSE)) {
        (void)replace_schedule(h, luid, sock, baseline, WIDE_START_NS, WIDE_END_NS, capture_alive);
    }
}

typedef struct { const char *name; unsigned payload; uint32_t window_ns; BOOL strict; } STRICT_STAGE;

/** Fitting and overrunning frames, with and without Strict_End, then a drain that accounts for every sequence number */
static void strict_matrix(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, BOOL *capture_alive)
{
    static const STRICT_STAGE stages[] = {
        {"small-fits-clear", PAYLOAD_BYTES, WINDOW_SMALL_ONLY_NS, FALSE},
        {"small-fits-strict", PAYLOAD_BYTES, WINDOW_SMALL_ONLY_NS, TRUE},
        {"large-fits-strict", LARGE_PAYLOAD_BYTES, WINDOW_BOTH_NS, TRUE},
        {"large-overruns-clear", LARGE_PAYLOAD_BYTES, WINDOW_SMALL_ONLY_NS, FALSE},
        {"large-overruns-strict", LARGE_PAYLOAD_BYTES, WINDOW_SMALL_ONLY_NS, TRUE},
    };
    unsigned i;

    g_send_period_ms = MATRIX_PERIOD_MS;
    for (i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
        printf(TAG " strict stage %s payload=%u windowNs=%u strictEnd=%d\n", stages[i].name, stages[i].payload, stages[i].window_ns, stages[i].strict);
        g_payload_bytes = stages[i].payload;
        if (!set_strict_end(h, stages[i].strict)) {
            break;
        }
        (void)replace_schedule(h, luid, sock, baseline, NARROW_START_NS, NARROW_START_NS + stages[i].window_ns, capture_alive);
    }
    printf(TAG " strict stage drain payload=%u windowNs=%u strictEnd=0\n", PAYLOAD_BYTES, WIDE_END_NS - WIDE_START_NS);
    g_payload_bytes = PAYLOAD_BYTES;
    if (set_strict_end(h, FALSE)) {
        (void)replace_schedule(h, luid, sock, baseline, WIDE_START_NS, WIDE_END_NS, capture_alive);
    }
    g_send_period_ms = SEND_PERIOD_MS;
}

/** Moves a running schedule to a different window, by the route named in g_move */
static void move_window(HANDLE h, avb_u64 luid, SOCKET sock, const uint32_t *baseline, BOOL *capture_alive)
{
    uint32_t now[REG_COUNT], mode = 0;
    BOOL toggle = strcmp(g_move, "--move-toggle") == 0;
    BOOL ok;

    printf("  --- %s ---\n", g_move);
    if (strcmp(g_move, "--move-open-future") == 0) {
        if (replace_schedule(h, luid, sock, baseline, 0, g_cycle_ns, capture_alive)) {
            (void)replace_schedule(h, luid, sock, baseline, SOFT_REOPEN_START_NS, SOFT_REOPEN_END_NS, capture_alive);
        }
        return;
    }
    if (strcmp(g_move, "--move-strict-matrix") == 0) {
        strict_matrix(h, luid, sock, baseline, capture_alive);
        return;
    }
    if (strcmp(g_move, "--move-strict-end") == 0) {
        strict_end_run(h, luid, sock, baseline, capture_alive);
        return;
    }
    if (strcmp(g_move, "--move-cycle-future") == 0) {
        g_cycle_ns = NEW_CYCLE_NS;
        (void)replace_schedule(h, luid, sock, baseline, NEW_CYCLE_START_NS, NEW_CYCLE_END_NS, capture_alive);
        return;
    }
    g_futscd = strcmp(g_move, "--move-futscd") == 0;
    g_future_base = g_futscd || strcmp(g_move, "--move-future") == 0 || strcmp(g_move, "--move-windows-future") == 0;
    g_open_start_ns = SOFT_REOPEN_START_NS;
    g_open_end_ns = SOFT_REOPEN_END_NS;
    if (toggle) {
        ok = read_reg(h, I226_TQAVCTRL, &mode) && write_reg(h, I226_TQAVCTRL, 0, "TQAVCTRL") && step_windows(h) &&
             write_reg(h, I226_TQAVCTRL, mode, "TQAVCTRL");
    } else {
        ok = apply_groups(h, strcmp(g_move, "--move-cycle") == 0 ? "ws" : strcmp(g_move, "--move-windows-future") == 0 ? "wb" : "wsb");
    }
    if (ok) {
        (void)read_regs(h, now, "after the move", baseline);
        (void)pattern_run(h, luid, sock, baseline, g_next_seq, capture_alive);
    }
}

/*
 * Post-cycle timeline on its own handle, printed on every change with the host time the frames use. It starts after the
 * enable and only observes, so the first read, the TX check and the exit code keep their timing and meaning.
 */
static DWORD WINAPI sampler_thread(LPVOID arg)
{
    avb_u64 luid = *(const avb_u64 *)arg;
    HANDLE h = INVALID_HANDLE_VALUE;
    uint32_t tctl = 0, rx = 0, last_tctl = 0, last_rx = 0, mode = 0, base_h = 0, last_mode = 0;
    int connect, oper, last_connect = -2, last_oper = -2;
    BOOL have, had = FALSE, first = TRUE;

    while (InterlockedCompareExchange(&g_sampler_stop, 0, 0) == 0) {
        MIB_IF_ROW2 row;

        ZeroMemory(&row, sizeof(row));
        row.InterfaceLuid.Value = luid;
        connect = GetIfEntry2(&row) == NO_ERROR ? (int)row.MediaConnectState : -1;
        oper = connect == -1 ? -1 : (int)row.OperStatus;
        if (h == INVALID_HANDLE_VALUE) {
            h = open_device();
            if (h != INVALID_HANDLE_VALUE && !bind_luid(h, luid)) {
                CloseHandle(h);
                h = INVALID_HANDLE_VALUE;
            }
        }
        have = h != INVALID_HANDLE_VALUE && read_reg(h, I226_TCTL, &tctl) && read_reg(h, I226_TSYNCRXCTL, &rx) &&
               read_reg(h, I226_TQAVCTRL, &mode) && read_reg(h, I226_BASET_H, &base_h);
        if (first || connect != last_connect || oper != last_oper || have != had || (have && (tctl != last_tctl || rx != last_rx || mode != last_mode))) {
            printf("    postcycle hostUs=%lld mediaConnect=%d operStatus=%d regs=%d TCTL=0x%08X COLD=0x%03X TSYNCRXCTL=0x%08X TQAVCTRL=0x%08X BASET_H=0x%08X\n", host_us(), connect, oper, have,
                   have ? tctl : 0, have ? (tctl >> TCTL_COLD_SHIFT) & TCTL_COLD_FIELD : 0, have ? rx : 0, have ? mode : 0, have ? base_h : 0);
            fflush(stdout);
        }
        first = FALSE;
        last_connect = connect;
        last_oper = oper;
        had = have;
        last_tctl = tctl;
        last_rx = rx;
        last_mode = mode;
        Sleep(SAMPLE_MS);
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    return 0;
}

static void postcycle_mark(const char *what)
{
    printf("    postcycle mark %s hostUs=%lld\n", what, host_us());
    fflush(stdout);
}

static void sampler_start(const avb_u64 *luid)
{
    InterlockedExchange(&g_sampler_stop, 0);
    g_sampler = CreateThread(NULL, 0, sampler_thread, (LPVOID)luid, 0, NULL);
    if (g_sampler == NULL) {
        printf(TAG " post-cycle timeline not available: thread error %lu\n", GetLastError());
    }
}

static void sampler_stop(void)
{
    if (g_sampler != NULL) {
        InterlockedExchange(&g_sampler_stop, 1);
        if (WaitForSingleObject(g_sampler, SAMPLER_JOIN_MS) != WAIT_OBJECT_0) {
            printf(TAG " post-cycle timeline did not stop within %d ms\n", SAMPLER_JOIN_MS);
        }
        CloseHandle(g_sampler);
        g_sampler = NULL;
    }
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
    postcycle_mark("enable-returned");
    sampler_start(&id->net_luid);
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
                postcycle_mark("first-read-done");
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
    uint32_t pattern_start_ns = 0, pattern_end_ns = 0;
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
    } else if ((argc == 4 || argc == 5) && strcmp(argv[1], "--pattern") == 0) {
        unsigned long start_ms = strtoul(argv[2], NULL, 10), end_ms = strtoul(argv[3], NULL, 10);
        if (start_ms >= end_ms || end_ms > PATTERN_MAX_END_MS) {
            printf(TAG " not run: --pattern needs start < end <= %d ms\n", PATTERN_MAX_END_MS);
            return 3;
        }
        if (argc == 5 && strcmp(argv[4], "--no-shadow") != 0 && strcmp(argv[4], "--after-rearm") != 0 && strcmp(argv[4], "--mode-last") != 0 &&
            strcmp(argv[4], "--toggle-only") != 0 && strcmp(argv[4], "--soft-disarm") != 0 && strcmp(argv[4], "--move-cycle") != 0 &&
            strcmp(argv[4], "--move-base") != 0 && strcmp(argv[4], "--move-future") != 0 && strcmp(argv[4], "--move-futscd") != 0 &&
            strcmp(argv[4], "--move-toggle") != 0 && strcmp(argv[4], "--move-open-future") != 0 && strcmp(argv[4], "--move-cycle-future") != 0 &&
            strcmp(argv[4], "--move-windows-future") != 0 && strcmp(argv[4], "--move-strict-end") != 0 && strcmp(argv[4], "--move-strict-matrix") != 0 && strcmp(argv[4], "--short-cycle") != 0 &&
            strcmp(argv[4], "--future-base") != 0 && strcmp(argv[4], "--future-base-futscd") != 0) {
            printf(TAG " not run: --pattern takes --no-shadow, --after-rearm, --mode-last, --toggle-only, --soft-disarm, --move-cycle, --move-base, --move-future, --move-futscd, --move-toggle, --move-open-future, --move-cycle-future, --move-windows-future, --move-strict-end, --move-strict-matrix, --short-cycle, --future-base or --future-base-futscd\n");
            return 3;
        }
        if (argc == 5 && strcmp(argv[4], "--short-cycle") == 0 && (uint32_t)end_ms * 1000000u >= SHORT_CYCLE_NS) {
            printf(TAG " not run: --short-cycle needs end < %u ms\n", SHORT_CYCLE_NS / 1000000u);
            return 3;
        }
        g_futscd = argc == 5 && strcmp(argv[4], "--future-base-futscd") == 0;
        g_future_base = g_futscd || (argc == 5 && strcmp(argv[4], "--future-base") == 0);
        g_move = (argc == 5 && strncmp(argv[4], "--move-", 7) == 0) ? argv[4] : NULL;
        g_toggle_only = argc == 5 && strcmp(argv[4], "--toggle-only") == 0;
        g_soft_disarm = argc == 5 && strcmp(argv[4], "--soft-disarm") == 0;
        g_after_rearm = argc == 5 && strcmp(argv[4], "--after-rearm") == 0;
        g_mode_last = argc == 5 && strcmp(argv[4], "--mode-last") == 0;
        pattern_start_ns = (uint32_t)start_ms * 1000000u;
        pattern_end_ns = (uint32_t)end_ms * 1000000u;
        letters = (argc == 5 && strcmp(argv[4], "--no-shadow") == 0) ? "wcb" : "wsb";
        if (!g_after_rearm) {
            g_pattern = TRUE;
            g_cycle_ns = (argc == 5 && strcmp(argv[4], "--short-cycle") == 0) ? SHORT_CYCLE_NS : (uint32_t)NS_PER_SEC;
            g_open_start_ns = pattern_start_ns;
            g_open_end_ns = pattern_end_ns;
        }
    } else if (argc == 2 && strcmp(argv[1], "--disarm") == 0) {
        g_disarm = TRUE;
        letters = "wsb";
    } else if (argc == 3 && strcmp(argv[1], "--rearm") == 0) {
        g_rearm_cycles = strtoul(argv[2], NULL, 10);
        if (g_rearm_cycles == 0 || g_rearm_cycles > MAX_REARM_CYCLES) {
            printf(TAG " not run: --rearm needs 1..%d\n", MAX_REARM_CYCLES);
            return 3;
        }
        letters = "wsb";
    } else if (argc != 1) {
        printf(TAG " not run: usage: tas_gate_investigation [--apply <any of w, s, b> | --pattern <startMs> <endMs> | --disarm | --rearm <n>]\n");
        return 3;
    }
    for (n = 0; argc == 3 && letters != NULL && n < strlen(letters); n++) {
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
    /* A schedule once survived a NIC cycle with tasArmed=0, and restoration is measured against this baseline */
    if (baseline_of(baseline, I226_TQAVCTRL) != 0 || baseline_of(baseline, I226_BASET_H) != 0 || baseline_of(baseline, I226_BASET_L) != 0) {
        printf(TAG " not run: the adapter is not in a clean pre-state: TQAVCTRL and BaseT must read 0\n");
        CloseHandle(h);
        return 3;
    }
    sock = open_tx_socket(ipv4);
    if (sock == INVALID_SOCKET) {
        printf(TAG " not run: socket setup failed (%d)\n", WSAGetLastError());
        CloseHandle(h);
        return 3;
    }
    g_capture_luid = capture;
    if (wire_start(target.net_luid, capture, target.mac, GetTickCount() ^ GetCurrentProcessId(), ipv4) == 0) {
        printf(TAG " not run: the I210 capture could not be started\n");
        wire_stop();
        CloseHandle(h);
        return 3;
    }

    baseline_seq = g_next_seq;
    seen = tx_step(h, target.net_luid, sock, BASELINE_MS, "baseline", g_next_seq, &capture_alive);
    dump_baseline_head(baseline_seq);
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
    if (g_mode_last) {
        printf("  --- schedule first, TAS mode last ---\n");
        seen = 0;
        if (apply_groups(h, letters)) {
            (void)read_regs(h, now, "schedule written, still Legacy", baseline);
            if (write_reg(h, I226_TQAVCTRL, (uint32_t)(I226_TQAVCTRL_TRANSMIT_MODE_MASK | I226_TQAVCTRL_ENHANCED_QAV_MASK), "TQAVCTRL")) {
                (void)read_regs(h, now, "after TQAVCTRL", baseline);
                (void)read_armed(h, &armed);
                printf(TAG " driver tasArmed=%u with hardware in TSN mode\n", armed);
                seen = pattern_run(h, target.net_luid, sock, baseline, armed_first, &capture_alive);
            }
        }
    } else if (!arm_tas(h)) {
        printf(TAG " SETUP_TAS failed (error %lu); no register group applied\n", GetLastError());
        seen = 0;
    } else {
        (void)read_regs(h, now, "after SETUP_TAS", baseline);
        seen = tx_step(h, target.net_luid, sock, g_rearm_cycles != 0 ? REARM_FILL_MS : ARMED_MS, "SETUP_TAS only", armed_first, &capture_alive);
        if (letters != NULL) {
            printf("  --- applying %s ---\n", letters);
            if (seen == 0 && apply_groups(h, letters)) {
                (void)read_regs(h, now, "after --apply", baseline);
                if (g_pattern) {
                    seen = pattern_run(h, target.net_luid, sock, baseline, armed_first, &capture_alive);
                } else {
                    seen = tx_step(h, target.net_luid, sock, STEP_MS, letters, armed_first, &capture_alive);
                }
                if (g_toggle_only && seen != 0) {
                    toggle_only(h, target.net_luid, sock, baseline, &capture_alive);
                }
                if (g_soft_disarm && seen != 0) {
                    soft_disarm(h, target.net_luid, sock, baseline, &capture_alive);
                }
                if (g_move != NULL && seen != 0) {
                    move_window(h, target.net_luid, sock, baseline, &capture_alive);
                }
                if ((g_toggle_only || g_soft_disarm || g_move != NULL) && seen != 0) {
                    report_phc(h, phc_before, qpc_before, "after the transitions");
                }
                if (g_after_rearm && seen != 0) {
                    printf("  --- disarm in place, then re-arm for the pattern ---\n");
                    (void)step_disarm(h, baseline);
                    (void)read_regs(h, now, "after disarm", baseline);
                    (void)tx_step(h, target.net_luid, sock, STEP_MS, "legacy TX", g_next_seq, &capture_alive);
                    if (arm_tas(h)) {
                        (void)read_regs(h, now, "after second SETUP_TAS", baseline);
                        g_pattern = TRUE;
                        g_cycle_ns = (uint32_t)NS_PER_SEC;
                        g_open_start_ns = pattern_start_ns;
                        g_open_end_ns = pattern_end_ns;
                        if (apply_groups(h, letters)) {
                            (void)read_regs(h, now, "after the pattern writes", baseline);
                            seen = pattern_run(h, target.net_luid, sock, baseline, g_next_seq, &capture_alive);
                        }
                    }
                }
                if (g_rearm_cycles != 0 && seen != 0) {
                    rearm_cycles(h, target.net_luid, sock, baseline, phc_before, qpc_before, &capture_alive);
                }
                if (g_disarm && seen != 0) {
                    printf("  --- disarm without a NIC cycle ---\n");
                    report_phc(h, phc_before, qpc_before, "before disarm");
                    (void)step_disarm(h, baseline);
                    (void)read_regs(h, now, "after disarm", baseline);
                    report_driver_disarm(h);
                    (void)tx_step(h, target.net_luid, sock, DISARM_TX_MS, "after disarm", g_next_seq, &capture_alive);
                    (void)read_regs(h, now, "after TX following disarm", baseline);
                    report_phc(h, phc_before, qpc_before, "after disarm");
                }
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
            postcycle_mark("tx-start");
            seen = tx_step(fresh, target.net_luid, sock, BASELINE_MS, "after the cycle", after_seq, &capture_alive);
            postcycle_mark("tx-end");
            tx_restored = (unsigned long long)seen * 100 >= (unsigned long long)(g_next_seq - after_seq) * WIRE_MIN_PERCENT;
            closesocket(sock);
            dump_baseline_head(after_seq);
        } else {
            printf(TAG " post-cycle TX not attempted: the socket could not bind within %d ms (%d)\n", RETURN_BOUND_MS, WSAGetLastError());
        }
        /* Evidence only: rc keeps the first read, which may fall before link-up */
        (void)read_regs(fresh, now, "after the post-cycle TX", baseline);
        CloseHandle(fresh);
    }
    sampler_stop();
    wire_stop();
    if (!tx_restored) {
        printf(TAG " TX to the wire not shown after the cycle\n");
        rc = 1;
    }
    printf(TAG " result: exit=%d (0 all restored, 1 registers/TX/cycle failed, 2 registers and TX restored, PHC not continuous)\n", rc);
    WSACleanup();
    return rc;
}
