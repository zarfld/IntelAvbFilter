/**
 * @file test_ioctl_phc_crosstimestamp_v2.c
 * @brief Real HIL tests for IOCTL_AVB_PHC_CROSSTIMESTAMP_V2 provenance reporting.
 *
 * Tests the actual kernel driver via DeviceIoControl on real Intel NICs.
 * Adapter-specific behaviour is verified per-adapter using ENUM_ADAPTERS.
 *
 * Verifies:  #48  (REQ-F-IOCTL-PHC-004: PHC↔System Cross-Timestamp Query)
 * Implements: TEST-IOCTL-XSTAMP-V2-001 (Hardware provenance gate)
 * Traces to:  #198 (TEST-IOCTL-XSTAMP-001)
 *
 * DID-specific classification:
 *   0x1533 (I210)  : hardware PHC counter — expect REGISTER_DERIVED, rate ≈ 1.004
 *   0x15BB (I219)  : PCH pseudo-clock   — provenance may be REGISTER_DERIVED or SW_WALLCLOCK;
 *                    do NOT accept wall-clock rate as evidence of hardware coherence
 *   0x125B (I226)  : hardware PHC counter — expect REGISTER_DERIVED; may be BLOCKED
 *
 * Exit codes (runner contract):
 *   0   = all PASS (or SKIP for genuinely absent hardware)
 *   1   = at least one FAIL or INCONCLUSIVE (mandatory gate fails closed)
 *   2   = all hardware tests SKIPPED (driver absent; non-mandatory suites only)
 *
 * Run elevated (requires \\.\IntelAvbFilter access).
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Single Source of Truth (SSOT) */
#include "avb_ioctl.h"

/* ─── constants ──────────────────────────────────────────────────────────── */
#define DID_I210   0x1533u
#define DID_I219   0x15BBu
#define DID_I226   0x125Bu

/* Rate bounds: [0.95, 1.10] is the TC-5b sanity window.
 * For REGISTER_DERIVED, the I210 crystal runs ~3900 ppm fast → ~1.004.
 * The SW_WALLCLOCK fallback advances at ~1.000 ±0.1%.
 * A genuine near-frozen I219 hardware counter has rate ≈ 5e-5.        */
#define RATE_LO        0.95
#define RATE_HI        1.10
#define RATE_HW_MIN    0.95    /* minimum for REGISTER_DERIVED on a working clock */
#define FROZEN_RATE_THRESHOLD  0.01  /* below this: counter considered frozen/near-frozen */

/* Minimum PHC delta between two reads 200ms apart, if hardware is working */
#define MIN_PHC_DELTA_200MS_NS   (50000000ULL)  /* 50ms — generous; failing clock may need less */

/* ─── types ──────────────────────────────────────────────────────────────── */
typedef struct {
    int pass;
    int fail;
    int skip;
    int inconclusive;  /* maps to FAIL at exit-code level */
    int blocked;       /* maps to FAIL; distinct for reporting */
} Results;

typedef struct {
    UINT32 adapter_index;
    UINT16 vendor_id;
    UINT16 device_id;
    UINT32 capabilities;
} AdapterInfo;

/* ─── infra ──────────────────────────────────────────────────────────────── */
static LARGE_INTEGER g_qpc_freq;
static UINT32        g_adapter_index = 0;
static UINT16        g_adapter_did   = 0;

static const char *PhcSourceName(avb_u32 src) {
    switch (src) {
    case AVB_PHC_SOURCE_REGISTER_DERIVED: return "REGISTER_DERIVED";
    case AVB_PHC_SOURCE_SW_WALLCLOCK:     return "SW_WALLCLOCK";
    case AVB_PHC_SOURCE_UNAVAILABLE:      return "UNAVAILABLE";
    default:                              return "UNKNOWN";
    }
}

static void Record(Results *r, int result, const char *tcid, const char *reason) {
    const char *label;
    switch (result) {
    case 0:  label = "PASS";  r->pass++;           break;
    case 2:  label = "SKIP";  r->skip++;           break;
    case 3:  label = "FAIL";  r->inconclusive++;
             r->fail++;
             printf("  [FAIL] %s  INCONCLUSIVE: %s\n", tcid, reason);
             return;
    case 4:  label = "FAIL";  r->blocked++;
             r->fail++;
             printf("  [FAIL] %s  BLOCKED: %s\n", tcid, reason);
             return;
    default: label = "FAIL";  r->fail++;           break;
    }
    printf("  [%s] %s  %s\n", label, tcid, reason);
}

#define PASS         0
#define FAIL         1
#define SKIP         2
#define INCONCLUSIVE 3
#define BLOCKED      4

static HANDLE OpenAdapter(void) {
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    AVB_OPEN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.vendor_id = 0x8086u;
    req.device_id = g_adapter_did;
    req.index     = g_adapter_index;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                         &req, sizeof(req), &req, sizeof(req), &br, NULL)
        || req.status != 0) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

/* Issue one V2 cross-timestamp.
 * Returns TRUE if the IOCTL succeeded and valid==1; fills *out.
 * Returns FALSE (and sets *gle) on IOCTL failure. */
static BOOL IssueV2(HANDLE h, AVB_CROSS_TIMESTAMP_V2_REQUEST *out, DWORD *gle) {
    DWORD br = 0;
    ZeroMemory(out, sizeof(*out));
    out->struct_version = AVB_XTSTAMP_V2_VERSION;
    out->struct_size    = (avb_u32)sizeof(*out);
    out->adapter_index  = g_adapter_index;
    out->_reserved_in   = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
                              out, sizeof(*out), out, sizeof(*out), &br, NULL);
    *gle = GetLastError();
    return ok;
}

/* Compute PHC/QPC rate for a pair of V2 samples (r1 → r2, ~200ms apart).
 * Returns 0.0 if QPC delta is zero. */
static double Rate(const AVB_CROSS_TIMESTAMP_V2_REQUEST *r1,
                   const AVB_CROSS_TIMESTAMP_V2_REQUEST *r2) {
    LONGLONG qpc_d = (LONGLONG)(r2->system_qpc - r1->system_qpc);
    if (qpc_d <= 0 || r2->qpc_frequency == 0) return 0.0;
    double qpc_ns = (double)qpc_d * 1.0e9 / (double)r2->qpc_frequency;
    LONGLONG phc_d = (LONGLONG)(r2->phc_time_ns - r1->phc_time_ns);
    return (double)phc_d / qpc_ns;
}

/* ─── TC-XSTAMP-V2-HIL-001: V2 IOCTL available and returns valid data ────── */
static int TC_V2_HIL_001_Availability(UINT16 did) {
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [SKIP] Cannot open adapter DID=0x%04X\n", did);
        return SKIP;
    }
    AVB_CROSS_TIMESTAMP_V2_REQUEST r;
    DWORD gle = 0;
    BOOL ok = IssueV2(h, &r, &gle);
    CloseHandle(h);

    if (!ok) {
        DWORD err = gle;
        if (err == ERROR_NOT_SUPPORTED || err == ERROR_INVALID_FUNCTION) {
            printf("    Driver returned NOT_SUPPORTED for V2 IOCTL "
                   "(old driver — STATUS_NOT_SUPPORTED).\n");
            return INCONCLUSIVE;  /* old driver: INCONCLUSIVE, not SKIP */
        }
        printf("    DeviceIoControl failed GLE=%lu\n", err);
        return INCONCLUSIVE;
    }

    printf("    valid=%u phc_source=%u(%s) rc=%d phc=%llu qpc=%llu\n",
           r.valid, r.avb_phc_source, PhcSourceName(r.avb_phc_source),
           r.get_systime_rc,
           (unsigned long long)r.phc_time_ns,
           (unsigned long long)r.system_qpc);
    if (!r.valid) {
        printf("    FAIL: valid=0 (IOCTL succeeded but no data)\n");
        return FAIL;
    }
    if (r.avb_phc_source == AVB_PHC_SOURCE_UNKNOWN) {
        printf("    FAIL: avb_phc_source=UNKNOWN (driver did not populate)\n");
        return FAIL;
    }
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-002: Provenance is consistent with get_systime rc ─── */
static int TC_V2_HIL_002_ProvenanceConsistency(UINT16 did) {
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;
    AVB_CROSS_TIMESTAMP_V2_REQUEST r;
    DWORD gle = 0;
    BOOL ok = IssueV2(h, &r, &gle);
    CloseHandle(h);

    if (!ok) return INCONCLUSIVE;
    if (!r.valid) return FAIL;

    /* rc < 0 → UNAVAILABLE; rc==1 → SW_WALLCLOCK; rc==0 → REGISTER_DERIVED */
    BOOL consistent = TRUE;
    if (r.get_systime_rc < 0 && r.avb_phc_source != AVB_PHC_SOURCE_UNAVAILABLE)
        consistent = FALSE;
    if (r.get_systime_rc == 1 && r.avb_phc_source != AVB_PHC_SOURCE_SW_WALLCLOCK)
        consistent = FALSE;
    if (r.get_systime_rc == 0 && r.avb_phc_source != AVB_PHC_SOURCE_REGISTER_DERIVED)
        consistent = FALSE;

    printf("    rc=%d source=%s consistent=%s\n",
           r.get_systime_rc, PhcSourceName(r.avb_phc_source),
           consistent ? "YES" : "NO");
    return consistent ? PASS : FAIL;
}

/* ─── TC-XSTAMP-V2-HIL-003: I210 expects REGISTER_DERIVED ────────────────── */
static int TC_V2_HIL_003_I210RegisterDerived(void) {
    if (g_adapter_did != DID_I210) return SKIP;
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;
    AVB_CROSS_TIMESTAMP_V2_REQUEST r;
    DWORD gle = 0;
    BOOL ok = IssueV2(h, &r, &gle);
    CloseHandle(h);
    if (!ok) return INCONCLUSIVE;
    if (!r.valid) return FAIL;
    if (r.avb_phc_source != AVB_PHC_SOURCE_REGISTER_DERIVED) {
        printf("    FAIL: I210 reported source=%s (expected REGISTER_DERIVED)\n",
               PhcSourceName(r.avb_phc_source));
        return FAIL;
    }
    printf("    I210 source=REGISTER_DERIVED rc=%d phc=%llu\n",
           r.get_systime_rc, (unsigned long long)r.phc_time_ns);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-004: I219 provenance accurately reflects rc ─────────
 * NOTE: REGISTER_DERIVED only means ops->get_systime() read a register.
 * It does NOT mean the clock is advancing correctly at the expected rate.
 * The counter quality is assessed separately in TC-XSTAMP-V2-HIL-006.    */
static int TC_V2_HIL_004_I219ProvRcAccurate(void) {
    if (g_adapter_did != DID_I219) return SKIP;
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;

    /* Take 3 samples to check consistency of provenance over time */
    AVB_CROSS_TIMESTAMP_V2_REQUEST r[3];
    BOOL ok[3];
    DWORD gle = 0;
    for (int i = 0; i < 3; i++) {
        ok[i] = IssueV2(h, &r[i], &gle);
        if (i < 2) Sleep(10);
    }
    CloseHandle(h);

    for (int i = 0; i < 3; i++) {
        if (!ok[i]) {
            printf("    INCONCLUSIVE: sample %d IOCTL failed GLE=%lu\n", i, gle);
            return INCONCLUSIVE;
        }
        if (!r[i].valid) {
            printf("    FAIL: sample %d valid=0\n", i);
            return FAIL;
        }
        /* Check rc ↔ source consistency for each sample */
        BOOL cons = ( (r[i].get_systime_rc <  0 && r[i].avb_phc_source == AVB_PHC_SOURCE_UNAVAILABLE) ||
                      (r[i].get_systime_rc == 1 && r[i].avb_phc_source == AVB_PHC_SOURCE_SW_WALLCLOCK) ||
                      (r[i].get_systime_rc == 0 && r[i].avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED) );
        if (!cons) {
            printf("    FAIL: sample %d rc=%d source=%s inconsistent\n",
                   i, r[i].get_systime_rc, PhcSourceName(r[i].avb_phc_source));
            return FAIL;
        }
    }

    printf("    I219 provenance consistent across 3 samples: source=%s rc=%d\n",
           PhcSourceName(r[0].avb_phc_source), r[0].get_systime_rc);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-005: I226 availability (BLOCKED if disabled) ─────── */
static int TC_V2_HIL_005_I226Availability(void) {
    if (g_adapter_did != DID_I226) return SKIP;

    /* Check OS adapter state without modifying it */
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) {
        DWORD gle = GetLastError();
        if (gle == ERROR_ACCESS_DENIED) {
            printf("    I226-LM: adapter disabled (CM_PROB_DISABLED) — BLOCKED\n");
        } else {
            printf("    I226-LM: cannot open (GLE=%lu) — BLOCKED\n", gle);
        }
        return BLOCKED;
    }
    AVB_CROSS_TIMESTAMP_V2_REQUEST r;
    DWORD gle = 0;
    BOOL ok = IssueV2(h, &r, &gle);
    CloseHandle(h);

    if (!ok) {
        printf("    I226: IOCTL failed GLE=%lu — BLOCKED\n", gle);
        return BLOCKED;
    }
    printf("    I226 source=%s rc=%d phc=%llu\n",
           PhcSourceName(r.avb_phc_source), r.get_systime_rc,
           (unsigned long long)r.phc_time_ns);
    if (r.avb_phc_source != AVB_PHC_SOURCE_REGISTER_DERIVED) {
        printf("    FAIL: I226 expected REGISTER_DERIVED, got %s\n",
               PhcSourceName(r.avb_phc_source));
        return FAIL;
    }
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-006: PHC/QPC rate — per adapter ──────────────────── *
 * Measures rate for two 200ms windows.  Classifies result by provenance.   *
 * For SW_WALLCLOCK: rate ≈ 1.0 is EXPECTED — it measures wall-clock, not   *
 * hardware PHC.  This does NOT prove the hardware clock is working.         *
 * For REGISTER_DERIVED: rate must be in [RATE_HW_MIN, RATE_HI] to PASS.    *
 * A frozen/near-frozen counter (rate < FROZEN_RATE_THRESHOLD) is a FAIL     *
 * even if source==REGISTER_DERIVED.                                          */
static int TC_V2_HIL_006_RateAndMonotonicity(UINT16 did) {
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;

    AVB_CROSS_TIMESTAMP_V2_REQUEST r1, r2, r3;
    DWORD gle = 0;

    if (!IssueV2(h, &r1, &gle) || !r1.valid) { CloseHandle(h); return INCONCLUSIVE; }
    Sleep(200);
    if (!IssueV2(h, &r2, &gle) || !r2.valid) { CloseHandle(h); return INCONCLUSIVE; }
    Sleep(200);
    if (!IssueV2(h, &r3, &gle) || !r3.valid) { CloseHandle(h); return INCONCLUSIVE; }
    CloseHandle(h);

    /* Mixed provenance across a rate window invalidates rate measurement */
    BOOL hw_w1 = (r1.avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                  r2.avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED);
    BOOL hw_w2 = (r2.avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                  r3.avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED);

    double rate12 = Rate(&r1, &r2);
    double rate23 = Rate(&r2, &r3);

    printf("    DID=0x%04X  src_r1=%s  src_r2=%s  src_r3=%s\n",
           did, PhcSourceName(r1.avb_phc_source),
           PhcSourceName(r2.avb_phc_source), PhcSourceName(r3.avb_phc_source));
    printf("    phc r1=%llu r2=%llu r3=%llu ns\n",
           (unsigned long long)r1.phc_time_ns,
           (unsigned long long)r2.phc_time_ns,
           (unsigned long long)r3.phc_time_ns);
    printf("    rate w1=%.6f  rate w2=%.6f\n", rate12, rate23);

    /* Monotonicity: PHC must not go backwards */
    if (r2.phc_time_ns < r1.phc_time_ns || r3.phc_time_ns < r2.phc_time_ns) {
        printf("    FAIL: PHC not monotone\n");
        return FAIL;
    }

    /* Rate classification */
    if (!hw_w1 && !hw_w2) {
        /* All software fallback — rate measures wall-clock, not hardware */
        printf("    All samples SW_WALLCLOCK: rate measurement is wall-clock rate, "
               "not hardware PHC quality.\n");
        printf("    INCONCLUSIVE: hardware clock quality cannot be assessed.\n");
        return INCONCLUSIVE;
    }

    if (hw_w1 && rate12 < FROZEN_RATE_THRESHOLD) {
        printf("    FAIL: REGISTER_DERIVED window-1 rate=%.6f — counter near-frozen (threshold %.4f)\n",
               rate12, FROZEN_RATE_THRESHOLD);
        return FAIL;
    }
    if (hw_w2 && rate23 < FROZEN_RATE_THRESHOLD) {
        printf("    FAIL: REGISTER_DERIVED window-2 rate=%.6f — counter near-frozen (threshold %.4f)\n",
               rate23, FROZEN_RATE_THRESHOLD);
        return FAIL;
    }

    /* At least one hardware window: check rate bounds */
    double rate_hw = hw_w1 ? rate12 : rate23;
    if (rate_hw < RATE_HW_MIN || rate_hw > RATE_HI) {
        printf("    FAIL: REGISTER_DERIVED rate=%.6f outside [%.2f, %.2f]\n",
               rate_hw, RATE_HW_MIN, RATE_HI);
        return FAIL;
    }

    printf("    PASS: REGISTER_DERIVED rate=%.6f in [%.2f, %.2f]\n",
           rate_hw, RATE_HW_MIN, RATE_HI);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-007: V1 behavior unchanged ──────────────────────── */
static int TC_V2_HIL_007_V1Unchanged(void) {
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;

    AVB_CROSS_TIMESTAMP_REQUEST v1;
    ZeroMemory(&v1, sizeof(v1));
    v1.adapter_index = g_adapter_index;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_PHC_CROSSTIMESTAMP,
                              &v1, sizeof(v1), &v1, sizeof(v1), &br, NULL);
    DWORD gle = GetLastError();
    CloseHandle(h);

    if (!ok) {
        printf("    V1 IOCTL failed GLE=%lu — INCONCLUSIVE\n", gle);
        return INCONCLUSIVE;
    }
    if (!v1.valid) {
        printf("    FAIL: V1 valid=0\n");
        return FAIL;
    }
    if (br != sizeof(AVB_CROSS_TIMESTAMP_REQUEST)) {
        printf("    FAIL: V1 BytesReturned=%lu expected %u\n",
               br, (unsigned)sizeof(AVB_CROSS_TIMESTAMP_REQUEST));
        return FAIL;
    }
    printf("    V1: valid=%u phc=%llu qpc=%llu BytesReturned=%lu\n",
           v1.valid, (unsigned long long)v1.phc_time_ns,
           (unsigned long long)v1.system_qpc, br);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-008: V2 bad version rejected ─────────────────────── */
static int TC_V2_HIL_008_BadVersionRejected(void) {
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;

    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = 99u;   /* unsupported */
    req.struct_size    = (avb_u32)sizeof(req);
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
                              &req, sizeof(req), &req, sizeof(req), &br, NULL);
    DWORD gle = GetLastError();
    CloseHandle(h);

    if (ok) {
        printf("    FAIL: driver accepted struct_version=99\n");
        return FAIL;
    }
    printf("    Correctly rejected version 99 (GLE=%lu)\n", gle);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-009: Small buffer rejected ───────────────────────── */
static int TC_V2_HIL_009_SmallBufferRejected(void) {
    HANDLE h = OpenAdapter();
    if (h == INVALID_HANDLE_VALUE) return SKIP;

    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = AVB_XTSTAMP_V2_VERSION;
    req.struct_size    = (avb_u32)sizeof(req);
    DWORD smallBuf = 0;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
                              &req, sizeof(req), &smallBuf, sizeof(DWORD), &br, NULL);
    DWORD gle = GetLastError();
    CloseHandle(h);

    if (ok) {
        printf("    FAIL: driver accepted 4-byte output buffer\n");
        return FAIL;
    }
    printf("    Correctly rejected small buffer (GLE=%lu)\n", gle);
    return PASS;
}

/* ─── main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    QueryPerformanceFrequency(&g_qpc_freq);

    printf("\n");
    printf("============================================================\n");
    printf("  TEST-IOCTL-XSTAMP-V2-HIL: PHC Provenance V2 — Real HIL\n");
    printf("  Verifies: #48 (REQ-F-IOCTL-PHC-004)\n");
    printf("  Tests all enumerated Intel adapters\n");
    printf("  QPC freq: %lld Hz\n", g_qpc_freq.QuadPart);
    printf("============================================================\n\n");

    /* Open discovery handle */
    HANDLE disc = CreateFileA("\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (disc == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Cannot open \\\\.\\IntelAvbFilter (error %lu)\n", GetLastError());
        printf("  Check: driver installed? Running as Administrator?\n");
        /* All HIL tests INCONCLUSIVE — exit 1 so CI gate fails */
        printf("  [FAIL] TC-XSTAMP-V2-HIL-001  INCONCLUSIVE: device inaccessible\n");
        return 1;
    }

    Results total = {0};
    int adapters_tested = 0;

    /* Enumerate adapters — same pattern as test_ioctl_phc_query.c */
    for (UINT32 idx = 0; idx < 16; idx++) {
        AVB_ENUM_REQUEST er;
        DWORD br = 0;
        ZeroMemory(&er, sizeof(er));
        er.index = idx;
        if (!DeviceIoControl(disc, IOCTL_AVB_ENUM_ADAPTERS,
                             &er, sizeof(er), &er, sizeof(er), &br, NULL))
            break;

        g_adapter_index = idx;
        g_adapter_did   = er.device_id;

        printf("--- Adapter %u  VID=0x%04X DID=0x%04X Caps=0x%08X ---\n",
               idx, er.vendor_id, er.device_id, er.capabilities);

        /* Per-adapter TC-IDs use /adapter:N suffix for LCY metric tracking */
        char tc[64];
        Results r = {0};

        sprintf(tc, "TC-XSTAMP-V2-HIL-001/adapter:%u", idx);
        Record(&r, TC_V2_HIL_001_Availability(er.device_id), tc,
               "V2 IOCTL availability");

        sprintf(tc, "TC-XSTAMP-V2-HIL-002/adapter:%u", idx);
        Record(&r, TC_V2_HIL_002_ProvenanceConsistency(er.device_id), tc,
               "Provenance consistent with rc");

        sprintf(tc, "TC-XSTAMP-V2-HIL-007/adapter:%u", idx);
        Record(&r, TC_V2_HIL_007_V1Unchanged(), tc,
               "V1 IOCTL behavior unchanged");

        sprintf(tc, "TC-XSTAMP-V2-HIL-008/adapter:%u", idx);
        Record(&r, TC_V2_HIL_008_BadVersionRejected(), tc,
               "Bad version rejected");

        sprintf(tc, "TC-XSTAMP-V2-HIL-009/adapter:%u", idx);
        Record(&r, TC_V2_HIL_009_SmallBufferRejected(), tc,
               "Small buffer rejected");

        /* DID-specific tests */
        if (er.device_id == DID_I210) {
            sprintf(tc, "TC-XSTAMP-V2-HIL-003/adapter:%u", idx);
            Record(&r, TC_V2_HIL_003_I210RegisterDerived(), tc,
                   "I210 REGISTER_DERIVED provenance");
        }
        if (er.device_id == DID_I219) {
            sprintf(tc, "TC-XSTAMP-V2-HIL-004/adapter:%u", idx);
            Record(&r, TC_V2_HIL_004_I219ProvRcAccurate(), tc,
                   "I219 provenance matches rc");
        }
        if (er.device_id == DID_I226) {
            sprintf(tc, "TC-XSTAMP-V2-HIL-005/adapter:%u", idx);
            Record(&r, TC_V2_HIL_005_I226Availability(), tc,
                   "I226 availability check");
        }

        sprintf(tc, "TC-XSTAMP-V2-HIL-006/adapter:%u", idx);
        Record(&r, TC_V2_HIL_006_RateAndMonotonicity(er.device_id), tc,
               "PHC/QPC rate and monotonicity");

        printf("  PASS=%d  FAIL=%d  SKIP=%d  INCONCLUSIVE=%d  BLOCKED=%d\n\n",
               r.pass, r.fail, r.skip, r.inconclusive, r.blocked);

        total.pass        += r.pass;
        total.fail        += r.fail;
        total.skip        += r.skip;
        total.inconclusive += r.inconclusive;
        total.blocked     += r.blocked;
        adapters_tested++;
    }

    CloseHandle(disc);

    printf("============================================================\n");
    printf("  Adapters tested: %d\n", adapters_tested);
    printf("  PASS=%d  FAIL=%d  SKIP=%d  INCONCLUSIVE=%d  BLOCKED=%d\n",
           total.pass, total.fail, total.skip,
           total.inconclusive, total.blocked);
    if (total.inconclusive > 0 || total.blocked > 0)
        printf("  NOTE: INCONCLUSIVE/BLOCKED = CI failure (mandatory gate).\n");
    printf("============================================================\n");

    if (adapters_tested == 0) {
        printf("[ERROR] No adapters enumerated — check driver binding.\n");
        return 1;
    }

    return (total.fail > 0) ? 1 : 0;
}
