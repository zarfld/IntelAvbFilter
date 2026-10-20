/**
 * @file avb_test_common.h
 * @brief Shared test helpers for IntelAvbFilter user-mode test executables
 *
 * Provides:
 *  - Adapter enumeration via IOCTL_AVB_ENUM_ADAPTERS (global index ordering)
 *  - Per-adapter handle opening via IOCTL_AVB_OPEN_ADAPTER
 *  - Capability gate macros (AVB_SKIP_TEST_IF_NO_CAP)
 *  - Register read/write/verify wrappers
 *  - TEST_PASS / TEST_FAIL / TEST_SKIP reporting and statistics
 *
 * SSOT: All IOCTL definitions come from #include "../../include/avb_ioctl.h"
 *
 * Index semantics:
 *   IOCTL_AVB_ENUM_ADAPTERS returns adapters at global positions 0..N-1 in
 *   FilterModuleList order.  IOCTL_AVB_OPEN_ADAPTER accepts the same global
 *   index in req->index.  AvbAdapterInfo.global_index is passed directly to
 *   OPEN_ADAPTER; no per-DID translation is required.
 */

#ifndef AVB_TEST_COMMON_H
#define AVB_TEST_COMMON_H

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"
/* Single Source of Truth for PCI device ID constants */
#include "../../include/intel_pci_ids.h"

/* =========================================================
 * Configuration
 * ========================================================= */

#define AVB_MAX_ADAPTERS    8
#define AVB_DEVICE_PATH     "\\\\.\\IntelAvbFilter"

/* =========================================================
 * Per-adapter info filled by AvbEnumerateAdapters()
 * ========================================================= */

typedef struct AvbAdapterInfo {
    avb_u16  vendor_id;             /* Always 0x8086 for Intel */
    avb_u16  device_id;             /* PCI DID: 0x125B=I226, 0x1533=I210, 0x15B7=I219, etc. */
    avb_u32  capabilities;          /* INTEL_CAP_* bitmask */
    avb_u32  global_index;          /* Position in ENUM_ADAPTERS global order; pass to OPEN_ADAPTER */
    char     device_name[16];       /* E.g. "I226-LM", "I210", "I219-LM" */
} AvbAdapterInfo;

/* =========================================================
 * Test result type
 * ========================================================= */

typedef enum {
    AVB_TEST_RESULT_PASS = 0,
    AVB_TEST_RESULT_FAIL = 1,
    AVB_TEST_RESULT_SKIP = 2
} AvbTestResult;

/* =========================================================
 * Test statistics
 * ========================================================= */

typedef struct AvbTestStats {
    int total;
    int passed;
    int failed;
    int skipped;
} AvbTestStats;

/* =========================================================
 * Reporting macros
 * ========================================================= */

#define AVB_REPORT_PASS(stats, name) do { \
    printf("  [PASS] %s\n", (name)); \
    (stats)->passed++; (stats)->total++; \
} while(0)

#define AVB_REPORT_FAIL(stats, name, reason) do { \
    printf("  [FAIL] %s: %s\n", (name), (reason)); \
    (stats)->failed++; (stats)->total++; \
} while(0)

#define AVB_REPORT_SKIP(stats, name, reason) do { \
    printf("  [SKIP] %s: %s\n", (name), (reason)); \
    (stats)->skipped++; (stats)->total++; \
} while(0)

/* =========================================================
 * Capability gate macros
 * ========================================================= */

/** Non-zero if adapter has the named capability */
#define AVB_HAS_CAP(info, cap)  (((info)->capabilities & (cap)) != 0)

/** Skip the current test function if adapter lacks a capability.
 *  Returns AVB_TEST_RESULT_SKIP from the calling function. */
#define AVB_SKIP_TEST_IF_NO_CAP(info, cap, test_name) \
    do { \
        if (!AVB_HAS_CAP((info), (cap))) { \
            printf("  [SKIP] %s: capability 0x%08X not present on DID=0x%04X\n", \
                   (test_name), (unsigned)(cap), (unsigned)(info)->device_id); \
            return AVB_TEST_RESULT_SKIP; \
        } \
    } while(0)

/* =========================================================
 * Public API
 * ========================================================= */

/**
 * Enumerate all Intel adapters visible to the driver.
 *
 * @param out       Caller-supplied array of at least AVB_MAX_ADAPTERS entries.
 * @param max_count Maximum entries to fill (use AVB_MAX_ADAPTERS).
 * @return          Number of adapters found (0 on failure or no adapters).
 *
 * Fills out[i].global_index with the ENUM_ADAPTERS position, which is the
 * correct value to pass to AvbOpenAdapter() / IOCTL_AVB_OPEN_ADAPTER.
 */
int AvbEnumerateAdapters(AvbAdapterInfo *out, int max_count);

/**
 * Open an exclusive handle to the adapter described by @p info.
 *
 * Uses IOCTL_AVB_OPEN_ADAPTER with info->global_index and info->device_id so
 * subsequent IOCTLs on the returned handle are routed to the correct NIC.
 *
 * @return  Open HANDLE on success, INVALID_HANDLE_VALUE on failure.
 *          Call GetLastError() for the Win32 error code.
 */
HANDLE AvbOpenAdapter(const AvbAdapterInfo *info);

/**
 * Return a human-readable name for a PCI Device ID.
 * The returned pointer is a string literal — do not free it.
 */
const char *AvbDeviceName(avb_u16 device_id);

/**
 * Format the INTEL_CAP_* capability bitmask into a comma-separated string.
 *
 * @param caps  Bitmask from AVB_ENUM_REQUEST.capabilities.
 * @param buf   Caller-supplied buffer.
 * @param sz    Size of @p buf in bytes.
 * @return      @p buf (always NUL-terminated).
 */
char *AvbCapabilityString(avb_u32 caps, char *buf, size_t sz);

/**
 * Read one 32-bit BAR0 register via IOCTL_AVB_READ_REGISTER.
 *
 * @return  0 on success, Win32 error code on failure.
 */
int AvbReadReg(HANDLE h, avb_u32 offset, avb_u32 *value);

/**
 * Write one 32-bit BAR0 register via IOCTL_AVB_WRITE_REGISTER.
 *
 * @return  0 on success, Win32 error code on failure.
 */
int AvbWriteReg(HANDLE h, avb_u32 offset, avb_u32 value);

/**
 * Write then read-back a register, printing PASS/FAIL with @p tag.
 *
 * @return  0 on success (write succeeded and readback matches), non-zero on error.
 */
int AvbWriteRegVerified(HANDLE h, avb_u32 offset, avb_u32 value, const char *tag);

/**
 * Print a summary banner and return 0 if all tests passed/skipped,
 * 1 if any failed.
 */
int AvbPrintSummary(const AvbTestStats *stats);

/* =========================================================
 * I210 TIMINCA test oracle (issue #343)
 * =========================================================
 * Test-side, header-only and INDEPENDENT of the driver: expected values come from the Intel I210
 * datasheet 333016 v3.7 section 7.8.3.2 / 8.15.11, not from driver code.
 *
 *     INC_TIME = 8 ns +/- Incvalue * 2^-32 ns         (every 8 ns / 125 MHz cycle)
 *     Incvalue = TIMINCA bits 30:0, ISGN = bit 31 (1b = subtract), reset value 0 = exactly 8 ns
 *
 * Consequences tests must respect on an I210:
 *   - raw TIMINCA == 0 is the NOMINAL value; it is never evidence of an unconfigured clock;
 *   - the old "(increment_ns << 24)" interpretation and "TIMINCA >> 24" are invalid;
 *   - only increments within +/-6.25 percent of 8 ns are representable (increment_ns 7 or 8);
 *   - the authorized public API range is PROVISIONALLY +/-100,000 ppb (#149).  #3 and #296 state +/-999,999 ppb;
 *     the conflict is recorded in #326 / #343.  Requests outside the policy must be REJECTED with
 *     NDIS_STATUS_INVALID_PARAMETER and the TIMINCA register must stay UNCHANGED (#326 AC-4).
 * The oracle itself is cross-validated offline in tests/unit/ioctl/test_i210_device_ops.c.
 */
/* Output of the verified-safety helpers below. Defaults to printf (the HIL tests print their verdict tags);
 * an offline test may override it to keep deliberate negative cases from emitting real [FAIL] tags. */
#ifndef AVB_TEST_LOG
#define AVB_TEST_LOG printf
#endif

#define AVB_TEST_I210_TIMINCA_ISGN       0x80000000u
#define AVB_TEST_I210_TIMINCA_INCVALUE   0x7FFFFFFFu
#define AVB_TEST_I210_CYCLE_NS           8u
#define AVB_TEST_I210_POLICY_PPB         100000u    /* PROVISIONAL (#149); not a hardware limit */

/* The device IDs the driver maps to the I210 family (AvbGetIntelDeviceType).  I211/flashless parts are NOT included. */
static __inline BOOL AvbTestIsI210Did(unsigned short did)
{
    switch (did) {
    case INTEL_DEV_I210_AT: case INTEL_DEV_I210_AT2: case INTEL_DEV_I210_FIBER:
    case INTEL_DEV_I210_IS: case INTEL_DEV_I210_IT:  case INTEL_DEV_I210_CS:
        return TRUE;
    default:
        return FALSE;
    }
}

/* |ppb| -> Incvalue units (2^-32 ns): round(|ppb| * 8 ns * 2^32 / 1e9).  |ppb| <= 1e7 keeps this inside 64 bits. */
static __inline unsigned long AvbTestI210UnitsFromPpb(unsigned long long abs_ppb)
{
    return (unsigned long)((abs_ppb * ((unsigned long long)AVB_TEST_I210_CYCLE_NS << 32) + 500000000ULL) / 1000000000ULL);
}

/* Expected raw TIMINCA for a +/-ppb offset from nominal. */
static __inline unsigned AvbTestI210RawFromPpb(long long ppb)
{
    unsigned long units = AvbTestI210UnitsFromPpb((unsigned long long)(ppb < 0 ? -ppb : ppb));
    return (ppb < 0 && units != 0u) ? (AVB_TEST_I210_TIMINCA_ISGN | (unsigned)units) : (unsigned)units;
}

/* Logical request (increment_ns, increment_frac) for a +/-ppb offset from nominal. */
static __inline void AvbTestI210RequestFromPpb(long long ppb, unsigned *ns, unsigned *frac)
{
    unsigned long long nominal = (unsigned long long)AVB_TEST_I210_CYCLE_NS << 32;
    unsigned long units = AvbTestI210UnitsFromPpb((unsigned long long)(ppb < 0 ? -ppb : ppb));
    unsigned long long total = (ppb < 0) ? (nominal - units) : (nominal + units);
    *ns   = (unsigned)(total >> 32);
    *frac = (unsigned)(total & 0xFFFFFFFFULL);
}

/* Decode a raw TIMINCA into the logical request that reproduces it (used for SAFE RESTORE of a previous value). */
static __inline void AvbTestI210RawToRequest(unsigned raw, unsigned *ns, unsigned *frac)
{
    unsigned long long nominal = (unsigned long long)AVB_TEST_I210_CYCLE_NS << 32;
    unsigned long long mag = raw & AVB_TEST_I210_TIMINCA_INCVALUE;
    unsigned long long total = (raw & AVB_TEST_I210_TIMINCA_ISGN) ? (nominal - mag) : (nominal + mag);
    *ns   = (unsigned)(total >> 32);
    *frac = (unsigned)(total & 0xFFFFFFFFULL);
}

/* Is |Incvalue| inside the provisional +/-100,000 ppb policy? */
static __inline BOOL AvbTestI210RawInPolicy(unsigned raw)
{
    return (raw & AVB_TEST_I210_TIMINCA_INCVALUE) <= AvbTestI210UnitsFromPpb(AVB_TEST_I210_POLICY_PPB);
}

/* Two raw values describe the same increment (a "negative zero" 0x80000000 equals nominal 0). */
static __inline BOOL AvbTestI210RawEquivalent(unsigned a, unsigned b)
{
    unsigned ma = a & AVB_TEST_I210_TIMINCA_INCVALUE, mb = b & AVB_TEST_I210_TIMINCA_INCVALUE;
    if (ma != mb) return FALSE;
    return (ma == 0u) ? TRUE : ((a & AVB_TEST_I210_TIMINCA_ISGN) == (b & AVB_TEST_I210_TIMINCA_ISGN));
}
/* Offset from nominal in ppb (signed, rounded to nearest) - for reporting and tolerance checks only. */
static __inline long long AvbTestI210RawToPpb(unsigned raw)
{
    long long mag = (long long)((((unsigned long long)(raw & AVB_TEST_I210_TIMINCA_INCVALUE)) * 1000000000ULL
                                  + ((unsigned long long)AVB_TEST_I210_CYCLE_NS << 31)) / ((unsigned long long)AVB_TEST_I210_CYCLE_NS << 32));
    return (raw & AVB_TEST_I210_TIMINCA_ISGN) ? -mag : mag;
}

/* ADJUST_FREQUENCY returning every observable: DeviceIoControl result, driver status field and the raw
 * TIMINCA reported BEFORE the change.  Returns TRUE only when the IOCTL succeeded AND req.status == 0. */
static __inline BOOL AvbTestAdjustFrequency(HANDLE h, unsigned ns, unsigned frac, unsigned *prev_raw, unsigned *drv_status, DWORD *gle)
{
    AVB_FREQUENCY_REQUEST req;
    DWORD br = 0;
    BOOL ok;
    ZeroMemory(&req, sizeof(req));
    req.increment_ns   = ns;
    req.increment_frac = frac;
    SetLastError(0);
    ok = DeviceIoControl(h, IOCTL_AVB_ADJUST_FREQUENCY, &req, sizeof(req), &req, sizeof(req), &br, NULL);
    if (gle)        *gle = ok ? 0 : GetLastError();
    if (prev_raw)   *prev_raw = ok ? req.current_increment : 0u;
    if (drv_status) *drv_status = ok ? req.status : 0u;
    return ok && req.status == 0;
}

/* Raw TIMINCA via the production API (GET_CLOCK_CONFIG), not via raw register IOCTLs.  *gle (optional) receives the
 * Win32 error of a failed call so callers can tell "device not ready" (ERROR_NOT_READY = STATUS_DEVICE_NOT_READY,
 * an unmet prerequisite => BLOCKED) from a genuine failure of the IOCTL (=> FAIL). */
static __inline BOOL AvbTestReadTimincaRawEx(HANDLE h, unsigned *raw, DWORD *gle)
{
    AVB_CLOCK_CONFIG cfg;
    DWORD br = 0;
    BOOL ok;
    ZeroMemory(&cfg, sizeof(cfg));
    SetLastError(0);
    ok = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg, sizeof(cfg), &cfg, sizeof(cfg), &br, NULL);
    if (gle) *gle = ok ? 0 : GetLastError();
    if (!ok || cfg.status != 0) return FALSE;
    *raw = cfg.timinca;
    return TRUE;
}
static __inline BOOL AvbTestReadTimincaRaw(HANDLE h, unsigned *raw)
{
    return AvbTestReadTimincaRawEx(h, raw, NULL);
}

/* Boundary rejection: the request must be refused with STATUS_INVALID_PARAMETER (Win32 87) AND the TIMINCA register
 * must stay UNCHANGED (#326 AC-4).  Returns 1 = PASS, 0 = FAIL (accepted / wrong rejection / register changed),
 * -1 = BLOCKED (device not ready: no request is sent), -2 = INCONCLUSIVE (post-state unreadable).  A baseline that\n * cannot be read for any other reason is a FAIL of GET_CLOCK_CONFIG (no request is sent either).
 * The verdict tag is printed by the helper; callers count anything but 1 as non-passing. */
static __inline int AvbTestI210ExpectRejectedUnchanged(HANDLE h, unsigned ns, unsigned frac, const char *what)
{
    unsigned before = 0, after = 0, prev = 0, st = 0;
    DWORD gle = 0;
    BOOL ok;
    if (!AvbTestReadTimincaRawEx(h, &before, &gle)) {
        if (gle == ERROR_NOT_READY) {   /* STATUS_DEVICE_NOT_READY: unmet prerequisite, not a defect */
            AVB_TEST_LOG("  [BLOCKED] %s: device not ready (GLE=%lu) - baseline TIMINCA unreadable, no request sent\n", what, gle);
            return -1;
        }
        AVB_TEST_LOG("  [FAIL] %s: GET_CLOCK_CONFIG failed (GLE=%lu) - required IOCTL broken; no request sent because the baseline cannot be saved\n", what, gle);
        return 0;
    }
    ok = AvbTestAdjustFrequency(h, ns, frac, &prev, &st, &gle);
    if (!AvbTestReadTimincaRaw(h, &after)) {
        AVB_TEST_LOG("  [INCONCLUSIVE] %s: TIMINCA unreadable after the request (ok=%d gle=%lu)\n", what, (int)ok, gle);
        return -2;
    }
    if (ok) {
        AVB_TEST_LOG("  [FAIL] %s: request (ns=%u frac=%u) was ACCEPTED outside the representable/policy range; TIMINCA 0x%08X -> 0x%08X\n",
               what, ns, frac, before, after);
        return 0;
    }
    if (gle != ERROR_INVALID_PARAMETER) {
        AVB_TEST_LOG("  [FAIL] %s: rejected with GLE=%lu, expected ERROR_INVALID_PARAMETER (87 = STATUS_INVALID_PARAMETER)\n", what, gle);
        return 0;
    }
    if (after != before) {
        AVB_TEST_LOG("  [FAIL] %s: rejected but TIMINCA CHANGED 0x%08X -> 0x%08X\n", what, before, after);
        return 0;
    }
    AVB_TEST_LOG("  [PASS] %s: rejected (GLE=87), TIMINCA unchanged at 0x%08X\n", what, after);
    return 1;
}

/* SAFE RESTORE of a previously saved raw TIMINCA, verified by readback.  A value inside the provisional policy is
 * restored exactly; anything else (e.g. the legacy 0x08000000, +3906 ppm) is restored to NOMINAL, never re-applied.
 * Returns 1 = restored and verified, 0 = FAIL (the PHC frequency is NOT restored: manual intervention needed). */
static __inline int AvbTestI210RestoreVerified(HANDLE h, unsigned saved_raw, const char *what)
{
    unsigned want = ((saved_raw & AVB_TEST_I210_TIMINCA_INCVALUE) == 0u) ? 0u : (AvbTestI210RawInPolicy(saved_raw) ? saved_raw : 0u);
    unsigned ns = 0, frac = 0, prev = 0, st = 0, now = 0;
    DWORD adj_gle = 0, rd_gle = 0;
    BOOL adj_ok, rd_ok;
    char rb[48];
    AvbTestI210RawToRequest(want, &ns, &frac);
    adj_ok = AvbTestAdjustFrequency(h, ns, frac, &prev, &st, &adj_gle);
    /* ALWAYS attempt the readback, also after a failed restore request: the report must state what the register
     * actually holds.  PASS only on adjust OK + successful readback + exact match; an unread value is never used. */
    rd_ok = AvbTestReadTimincaRawEx(h, &now, &rd_gle);
    if (!adj_ok || !rd_ok || now != want) {
        if (rd_ok) _snprintf_s(rb, sizeof(rb), _TRUNCATE, "readback 0x%08X", now);
        else       _snprintf_s(rb, sizeof(rb), _TRUNCATE, "readback UNREAD (gle=%lu)", rd_gle);
        AVB_TEST_LOG("  [FAIL] %s: RESTORE FAILED (saved 0x%08X, wanted 0x%08X, restore request %s (status=0x%08X gle=%lu), %s)"
                     " - the PHC frequency is NOT verified as restored\n",
                     what, saved_raw, want, adj_ok ? "ok" : "FAILED", st, adj_gle, rb);
        return 0;
    }
    if (want != saved_raw)
        AVB_TEST_LOG("  [INFO] %s: saved TIMINCA 0x%08X is outside the +/-%u ppb policy; restored to nominal 0x%08X\n",
               what, saved_raw, (unsigned)AVB_TEST_I210_POLICY_PPB, want);
    AVB_TEST_LOG("  [PASS] %s: restored TIMINCA 0x%08X (verified by readback)\n", what, now);
    return 1;
}

/* Device ID of the adapter bound to this handle (IOCTL_AVB_GET_HW_STATE reports the actual PCI IDs). */
static __inline BOOL AvbTestGetBoundDid(HANDLE h, unsigned short *did)
{
    AVB_HW_STATE_QUERY q;
    DWORD br = 0;
    ZeroMemory(&q, sizeof(q));
    if (!DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE, &q, sizeof(q), &q, sizeof(q), &br, NULL)) return FALSE;
    *did = q.device_id;
    return TRUE;
}

/* =========================================================
 * Exact target selection for MUTATING HIL tests (issue #343; FullSuite 2026-10-19 finding: "adapter 0" was the
 * I210 in one pass and the I226 in the next, so a mutation lane silently changed a different NIC).
 *
 * A test that changes hardware state (TIMINCA, SYSTIM, TAS, PnP ...) must act on exactly ONE adapter that the
 * operator named explicitly in AVB_HIL_TARGET_DID (hex PCI device ID, e.g. 0x1533).  There is no default and no
 * fallback to enumeration index 0.  NOT_REQUESTED / ABSENT / AMBIGUOUS / BAD_VALUE are unmet safety prerequisites:
 * the caller reports BLOCKED and performs no mutation.  The DID must be unique among the enumerated adapters,
 * because the driver ABI exposes no PCI location or MAC (two NICs with one DID cannot be told apart).
 * ========================================================= */
#define AVB_TEST_TARGET_ENV "AVB_HIL_TARGET_DID"

typedef enum {
    AVB_TARGET_OK = 0,
    AVB_TARGET_NOT_REQUESTED,   /* variable unset or empty */
    AVB_TARGET_BAD_VALUE,       /* not 1..4 hex digits, or zero */
    AVB_TARGET_ABSENT,          /* requested DID not enumerated */
    AVB_TARGET_AMBIGUOUS        /* requested DID enumerated more than once */
} AvbTargetSelection;

static __inline const char *AvbTargetSelectionName(int s)
{
    switch (s) {
    case AVB_TARGET_OK:            return "OK";
    case AVB_TARGET_NOT_REQUESTED: return "NOT_REQUESTED (set " AVB_TEST_TARGET_ENV ")";
    case AVB_TARGET_BAD_VALUE:     return "BAD_VALUE";
    case AVB_TARGET_ABSENT:        return "ABSENT";
    case AVB_TARGET_AMBIGUOUS:     return "AMBIGUOUS (DID not unique)";
    default:                       return "UNKNOWN";
    }
}

/* Pure selection (no I/O): *out_index is written only on AVB_TARGET_OK. */
static __inline int AvbTestSelectTargetIndex(const unsigned short *dids, int n, const char *value, int *out_index)
{
    unsigned v = 0;
    int digits = 0, i, hit = -1, hits = 0;
    const char *p = value;
    if (p == NULL) return AVB_TARGET_NOT_REQUESTED;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return AVB_TARGET_NOT_REQUESTED;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (; *p && *p != ' ' && *p != '\t'; p++) {
        int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 :
                (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : -1;
        if (d < 0 || ++digits > 4) return AVB_TARGET_BAD_VALUE;
        v = (v << 4) | (unsigned)d;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\0' || digits == 0 || v == 0u) return AVB_TARGET_BAD_VALUE;
    for (i = 0; i < n; i++) { if (dids[i] == (unsigned short)v) { hits++; hit = i; } }
    if (hits == 0) return AVB_TARGET_ABSENT;
    if (hits > 1)  return AVB_TARGET_AMBIGUOUS;
    if (out_index) *out_index = hit;
    return AVB_TARGET_OK;
}

/* Reads AVB_HIL_TARGET_DID and selects within an AvbEnumerateAdapters() list. */
static __inline int AvbTestSelectTarget(const AvbAdapterInfo *list, int n, int *out_index)
{
    char value[32];
    unsigned short dids[AVB_MAX_ADAPTERS];
    int i;
    DWORD len = GetEnvironmentVariableA(AVB_TEST_TARGET_ENV, value, (DWORD)sizeof(value));
    if (len == 0) return AVB_TARGET_NOT_REQUESTED;
    if (len >= sizeof(value)) return AVB_TARGET_BAD_VALUE;
    if (n > AVB_MAX_ADAPTERS) n = AVB_MAX_ADAPTERS;
    for (i = 0; i < n; i++) dids[i] = list[i].device_id;
    return AvbTestSelectTargetIndex(dids, n, value, out_index);
}

/* Exact-target filter used by AvbEnumerateAdapters() (the runner sets AVB_HIL_TARGET_DID only for
 * Mutating:Target work items).  Returns the new adapter count:
 *   variable not set                 -> n, list unchanged
 *   exactly one adapter has that DID -> 1, list[0] = that adapter (driver global_index preserved)
 *   bad value / absent / ambiguous   -> -1, list unchanged; the caller must fail, never fall back to all adapters */
static __inline int AvbTestFilterToTarget(AvbAdapterInfo *list, int n)
{
    int ti = -1;
    int sel = AvbTestSelectTarget(list, n, &ti);
    if (sel == AVB_TARGET_NOT_REQUESTED) return n;
    if (sel != AVB_TARGET_OK) return -1;
    if (ti != 0) list[0] = list[ti];
    return 1;
}

/* ---- Physical PHC frequency verification against the host QPC (issue #343) -----------------------------
 * A frequency request is verified by the RATE it produces, measured against an independent timebase (QPC),
 * not by reading back the register it wrote.  Windows N/P/N/M/N (nominal, +commanded, nominal, -commanded,
 * nominal): the nominal windows give the PHC/QPC ratio r0 (both crystal offsets), the commanded change is
 * measured relative to r0, so the QPC crystal error cancels.
 *   samples not all REGISTER_DERIVED or too few          -> INCONCLUSIVE
 *   PHC not monotonic / not advancing                    -> FAIL
 *   |r0 - 1| > AVB_RATE_NOMINAL_LIMIT_PPM                -> FAIL  (e.g. legacy nominal +3906.25 ppm)
 *   |delta - commanded| > max(AVB_RATE_MIN_TOL_PPB, 5 x spread of the nominal windows) -> FAIL
 *   nominal spread > AVB_RATE_NOMINAL_SPREAD_PPB         -> INCONCLUSIVE (deviation cannot be certified)
 *   otherwise                                            -> PASS */
#include <math.h>
#define AVB_RATE_MIN_SAMPLES         20
#define AVB_RATE_NOMINAL_SPREAD_PPB  500.0
#define AVB_RATE_MIN_TOL_PPB         2000.0
#define AVB_RATE_NOMINAL_LIMIT_PPM   1000.0
enum { AVB_RATE_PASS = 0, AVB_RATE_FAIL = 1, AVB_RATE_INCONCLUSIVE = 2 };

typedef struct {
    int    ok;              /* fit computed (>= 2 samples, nonzero QPC span) */
    int    n;
    int    monotonic;       /* PHC strictly increasing over the window */
    double rate;            /* least-squares d(PHC ns) / d(QPC ns) */
    double resid_rms_ns;
} AvbRateFit;

typedef struct {
    int    verdict;         /* AVB_RATE_* */
    double r0;              /* mean nominal PHC/QPC rate */
    double spread_ppb;      /* (max - min) / r0 of the nominal windows */
    double delta_ppb;       /* measured (rate / r0 - 1) */
    double tol_ppb;
    char   why[192];
} AvbRateEval;

static __inline const char *AvbRateVerdictTag(int v)
{
    return (v == AVB_RATE_PASS) ? "PASS" : (v == AVB_RATE_FAIL) ? "FAIL" : "INCONCLUSIVE";
}

static __inline void AvbRateFitWindow(const unsigned long long *phc_ns, const unsigned long long *qpc, int n,
                                      unsigned long long qpc_freq, AvbRateFit *fit)
{
    double mx = 0.0, my = 0.0, sxx = 0.0, sxy = 0.0, ssr = 0.0;
    int i;
    fit->ok = 0; fit->n = n; fit->monotonic = 1; fit->rate = 0.0; fit->resid_rms_ns = 0.0;
    if (n < 2 || qpc_freq == 0ULL) return;
    for (i = 0; i < n; i++) {
        mx += (double)(long long)(qpc[i] - qpc[0]) * 1e9 / (double)qpc_freq;
        my += (double)(long long)(phc_ns[i] - phc_ns[0]);
        if (i > 0 && phc_ns[i] <= phc_ns[i - 1]) fit->monotonic = 0;
    }
    mx /= n; my /= n;
    for (i = 0; i < n; i++) {
        double dx = (double)(long long)(qpc[i] - qpc[0]) * 1e9 / (double)qpc_freq - mx;
        double dy = (double)(long long)(phc_ns[i] - phc_ns[0]) - my;
        sxx += dx * dx; sxy += dx * dy;
    }
    if (sxx <= 0.0) return;
    fit->rate = sxy / sxx;
    for (i = 0; i < n; i++) {
        double dx = (double)(long long)(qpc[i] - qpc[0]) * 1e9 / (double)qpc_freq - mx;
        double dy = (double)(long long)(phc_ns[i] - phc_ns[0]) - my;
        double r = dy - fit->rate * dx;
        ssr += r * r;
    }
    fit->resid_rms_ns = sqrt(ssr / n);
    fit->ok = 1;
}

static __inline void AvbRateEvaluate(const AvbRateFit *nominal, int n_nominal, const AvbRateFit *win,
                                     long long commanded_ppb, int all_register_derived, AvbRateEval *ev)
{
    double lo = 0.0, hi = 0.0, sum = 0.0, err;
    int i;
    ev->verdict = AVB_RATE_INCONCLUSIVE; ev->r0 = 0.0; ev->spread_ppb = 0.0; ev->delta_ppb = 0.0; ev->tol_ppb = 0.0; ev->why[0] = '\0';
    if (!all_register_derived) {
        _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "PHC samples are not all REGISTER_DERIVED - the hardware clock was not measured");
        return;
    }
    if (n_nominal < 2 || !win->ok || win->n < AVB_RATE_MIN_SAMPLES) {
        _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "too few samples (window %d, need %d; %d nominal windows)", win->n, AVB_RATE_MIN_SAMPLES, n_nominal);
        return;
    }
    for (i = 0; i < n_nominal; i++) {
        if (!nominal[i].ok || nominal[i].n < AVB_RATE_MIN_SAMPLES) {
            _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "too few samples in nominal window %d (%d, need %d)", i, nominal[i].n, AVB_RATE_MIN_SAMPLES);
            return;
        }
    }
    ev->verdict = AVB_RATE_FAIL;
    for (i = 0; i < n_nominal; i++) {
        if (!nominal[i].monotonic) { _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "PHC not monotonic in nominal window %d (frozen or stepping)", i); return; }
    }
    if (!win->monotonic) { _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "PHC not monotonic in the commanded window (frozen or stepping)"); return; }
    for (i = 0; i < n_nominal; i++) {
        double r = nominal[i].rate;
        sum += r;
        if (i == 0 || r < lo) lo = r;
        if (i == 0 || r > hi) hi = r;
    }
    ev->r0 = sum / n_nominal;
    if (ev->r0 < 0.5 || win->rate < 0.5) {
        _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "PHC does not advance at the QPC rate (nominal %.6f, commanded %.6f)", ev->r0, win->rate);
        return;
    }
    if (fabs(ev->r0 - 1.0) * 1e6 > AVB_RATE_NOMINAL_LIMIT_PPM) {
        _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "nominal PHC/QPC rate off by %+.1f ppm (limit %.0f ppm) - nominal increment is wrong",
                    (ev->r0 - 1.0) * 1e6, AVB_RATE_NOMINAL_LIMIT_PPM);
        return;
    }
    ev->spread_ppb = (hi - lo) / ev->r0 * 1e9;
    ev->delta_ppb  = (win->rate / ev->r0 - 1.0) * 1e9;
    ev->tol_ppb    = (5.0 * ev->spread_ppb > AVB_RATE_MIN_TOL_PPB) ? 5.0 * ev->spread_ppb : AVB_RATE_MIN_TOL_PPB;
    err = fabs(ev->delta_ppb - (double)commanded_ppb);
    if (err > ev->tol_ppb) {
        _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "measured %+.0f ppb vs commanded %+lld ppb: error %.0f ppb > tolerance %.0f ppb",
                    ev->delta_ppb, commanded_ppb, err, ev->tol_ppb);
        return;
    }
    if (ev->spread_ppb > AVB_RATE_NOMINAL_SPREAD_PPB) {
        ev->verdict = AVB_RATE_INCONCLUSIVE;
        _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "nominal windows spread %.0f ppb > %.0f ppb - the %+.0f ppb result cannot be certified",
                    ev->spread_ppb, AVB_RATE_NOMINAL_SPREAD_PPB, ev->delta_ppb);
        return;
    }
    ev->verdict = AVB_RATE_PASS;
    _snprintf_s(ev->why, sizeof(ev->why), _TRUNCATE, "measured %+.0f ppb vs commanded %+lld ppb (error %.0f <= %.0f ppb; nominal %+.3f ppm, spread %.0f ppb)",
                ev->delta_ppb, commanded_ppb, err, ev->tol_ppb, (ev->r0 - 1.0) * 1e6, ev->spread_ppb);
}

#endif /* AVB_TEST_COMMON_H */
