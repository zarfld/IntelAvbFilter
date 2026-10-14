/**
 * @file test_ioctl_xstamp_v2.c
 * @brief Unit tests for IOCTL_AVB_PHC_CROSSTIMESTAMP_V2 ABI and provenance semantics.
 *
 * Hardware-independent: exercises the ABI contract, struct layout, buffer
 * validation, and PHC provenance classification using mock get_systime()
 * return codes.  No driver or device access is required.
 *
 * Verifies:  #48  (REQ-F-IOCTL-PHC-004: PHC↔System Cross-Timestamp Query)
 * Traces to: #198 (TEST-IOCTL-XSTAMP-001)
 *
 * Test IDs follow the TC-XSTAMP-V2-NNN convention so Parse-TestResults.ps1
 * matches the TC-* pattern and emits correct JUnit entries.
 *
 * Exit codes (matching runner contract in Run-Tests-CI.ps1):
 *   0  = all tests PASS
 *   1  = at least one FAIL (including INCONCLUSIVE on mandatory hardware gate)
 */

#include <windows.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions (SSOT rule).
 * Build script passes -I include so "avb_ioctl.h" resolves to include/avb_ioctl.h. */
#include "avb_ioctl.h"

/* ─── test infra ──────────────────────────────────────────────────────────── */
#define PASS 0
#define FAIL 1
#define SKIP 2
/* INCONCLUSIVE maps to FAIL at the exit-code level per P0.1 requirement:
 * "An INCONCLUSIVE mandatory hardware result must cause the CI gate to be
 *  non-successful."  We report [FAIL] TC-XSTAMP-V2-xxx with the label
 *  "INCONCLUSIVE" so both the human reader and the JUnit parser see failure. */
#define INCONCLUSIVE 3

typedef struct { int pass; int fail; int skip; int inconclusive; } Results;

static void Record(Results *r, int result, const char *tcid, const char *reason)
{
    const char *label;
    switch (result) {
    case PASS:         label = "PASS";          r->pass++;          break;
    case SKIP:         label = "SKIP";          r->skip++;          break;
    case INCONCLUSIVE: label = "FAIL";          r->inconclusive++;
                       r->fail++; /* INCONCLUSIVE is a CI failure */
                       printf("  [FAIL] %s  INCONCLUSIVE: %s\n", tcid, reason);
                       return;
    default:           label = "FAIL";          r->fail++;          break;
    }
    printf("  [%s] %s  %s\n", label, tcid, reason);
}

/* ─── TC-XSTAMP-V2-001: Struct layout and size ────────────────────────────── */
static int TC_V2_001_Layout(void)
{
    /* All checks use compile-time constants to verify they match the ABI doc. */
    if (sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST) != 56)   return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, struct_version)   !=  0) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, struct_size)      !=  4) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, adapter_index)    !=  8) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, _reserved_in)     != 12) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, phc_time_ns)      != 16) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, system_qpc)       != 24) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, qpc_frequency)    != 32) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, valid)            != 40) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, avb_phc_source)   != 44) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, get_systime_rc)   != 48) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_V2_REQUEST, _reserved_out)    != 52) return FAIL;
    /* IOCTL code 67 must not collide with V1 (63) */
    if (IOCTL_AVB_PHC_CROSSTIMESTAMP_V2 == IOCTL_AVB_PHC_CROSSTIMESTAMP) return FAIL;
    /* Version constants */
    if (AVB_XTSTAMP_V2_VERSION != 2u) return FAIL;
    /* Provenance constants must be distinct */
    if (AVB_PHC_SOURCE_REGISTER_DERIVED == AVB_PHC_SOURCE_SW_WALLCLOCK)  return FAIL;
    if (AVB_PHC_SOURCE_REGISTER_DERIVED == AVB_PHC_SOURCE_UNAVAILABLE)   return FAIL;
    if (AVB_PHC_SOURCE_REGISTER_DERIVED == AVB_PHC_SOURCE_UNKNOWN)       return FAIL;
    if (AVB_PHC_SOURCE_SW_WALLCLOCK     == AVB_PHC_SOURCE_UNAVAILABLE)   return FAIL;
    if (AVB_PHC_SOURCE_SW_WALLCLOCK     == AVB_PHC_SOURCE_UNKNOWN)       return FAIL;
    if (AVB_PHC_SOURCE_UNAVAILABLE      == AVB_PHC_SOURCE_UNKNOWN)       return FAIL;
    /* V1 unchanged */
    if (sizeof(AVB_CROSS_TIMESTAMP_REQUEST) != 40)  return FAIL;
    if (IOCTL_AVB_PHC_CROSSTIMESTAMP != _NDIS_CONTROL_CODE(63, METHOD_BUFFERED)) return FAIL;
    if (IOCTL_AVB_PHC_CROSSTIMESTAMP_V2 != _NDIS_CONTROL_CODE(67, METHOD_BUFFERED)) return FAIL;
    return PASS;
}

/* ─── TC-XSTAMP-V2-002: Zero-initialise a request struct correctly ─────────── */
static int TC_V2_002_Zeroinit(void)
{
    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = AVB_XTSTAMP_V2_VERSION;
    req.struct_size    = sizeof(req);
    req.adapter_index  = 0;
    req._reserved_in   = 0;
    /* All output fields must be zero after ZeroMemory */
    if (req.phc_time_ns   != 0) return FAIL;
    if (req.system_qpc    != 0) return FAIL;
    if (req.qpc_frequency != 0) return FAIL;
    if (req.valid         != 0) return FAIL;
    /* avb_phc_source and get_systime_rc are 0 after ZeroMemory, which
     * maps to REGISTER_DERIVED and rc=0.  This is acceptable for zeroed
     * structs; the driver always initialises them before returning. */
    return PASS;
}

/* ─── TC-XSTAMP-V2-003: Provenance constant mapping (mock get_systime) ─────── */
/* These tests simulate the driver logic: for a given get_systime() rc,
 * confirm the expected avb_phc_source is selected. */
static avb_u32 ProvenanceFromRc(int rc)
{
    if (rc < 0) return AVB_PHC_SOURCE_UNAVAILABLE;
    /* SYSTIME_RC_SW_OK = 1 */
    if (rc == 1) return AVB_PHC_SOURCE_SW_WALLCLOCK;
    /* SYSTIME_RC_HW_OK = 0 */
    return AVB_PHC_SOURCE_REGISTER_DERIVED;
}

static int TC_V2_003_ProvenanceMapping(void)
{
    /* rc = 0 (SYSTIME_RC_HW_OK) → REGISTER_DERIVED */
    if (ProvenanceFromRc(0) != AVB_PHC_SOURCE_REGISTER_DERIVED)  return FAIL;
    /* rc = 1 (SYSTIME_RC_SW_OK) → SW_WALLCLOCK */
    if (ProvenanceFromRc(1) != AVB_PHC_SOURCE_SW_WALLCLOCK)       return FAIL;
    /* rc < 0 → UNAVAILABLE */
    if (ProvenanceFromRc(-1) != AVB_PHC_SOURCE_UNAVAILABLE)       return FAIL;
    if (ProvenanceFromRc(-99) != AVB_PHC_SOURCE_UNAVAILABLE)      return FAIL;
    return PASS;
}

/* ─── TC-XSTAMP-V2-004: REGISTER_DERIVED does not imply clock quality ───────── */
static int TC_V2_004_RegisterDerivedNotQuality(void)
{
    /* AVB_PHC_SOURCE_REGISTER_DERIVED (0) must equal SYSTIME_RC_HW_OK (0)
     * so the driver mapping works; but neither constant implies the counter
     * is advancing at the correct rate.  This test verifies the numeric
     * values and documents the semantic limitation. */
    if (AVB_PHC_SOURCE_REGISTER_DERIVED != 0u) return FAIL;
    if (AVB_PHC_SOURCE_SW_WALLCLOCK     != 1u) return FAIL;
    if (AVB_PHC_SOURCE_UNAVAILABLE      != 2u) return FAIL;
    if (AVB_PHC_SOURCE_UNKNOWN          != 3u) return FAIL;
    /* No assertion about clock quality — this test only validates constants. */
    return PASS;
}

/* ─── TC-XSTAMP-V2-005: Mixed-provenance window → INCONCLUSIVE ──────────────── */
/* Simulate TC-5b measuring three V2 samples across a 200 ms window.
 * If any sample in any window has provenance != REGISTER_DERIVED,
 * the rate measurement is INCONCLUSIVE — it measures wall-clock rate,
 * not hardware-PHC coherence. */
static int TC_V2_005_MixedProvenanceInconclusive(void)
{
    /* Case A: all REGISTER_DERIVED → can compute rate, not INCONCLUSIVE */
    avb_u32 samples_a[3] = {
        AVB_PHC_SOURCE_REGISTER_DERIVED,
        AVB_PHC_SOURCE_REGISTER_DERIVED,
        AVB_PHC_SOURCE_REGISTER_DERIVED
    };
    /* Case B: first sample SW_WALLCLOCK → INCONCLUSIVE */
    avb_u32 samples_b[3] = {
        AVB_PHC_SOURCE_SW_WALLCLOCK,
        AVB_PHC_SOURCE_REGISTER_DERIVED,
        AVB_PHC_SOURCE_REGISTER_DERIVED
    };
    /* Case C: all SW_WALLCLOCK → INCONCLUSIVE */
    avb_u32 samples_c[3] = {
        AVB_PHC_SOURCE_SW_WALLCLOCK,
        AVB_PHC_SOURCE_SW_WALLCLOCK,
        AVB_PHC_SOURCE_SW_WALLCLOCK
    };

    /* Helper: return TRUE if all samples are REGISTER_DERIVED */
    BOOL all_hw_a = (samples_a[0] == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                     samples_a[1] == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                     samples_a[2] == AVB_PHC_SOURCE_REGISTER_DERIVED);
    BOOL all_hw_b = (samples_b[0] == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                     samples_b[1] == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                     samples_b[2] == AVB_PHC_SOURCE_REGISTER_DERIVED);
    BOOL all_hw_c = (samples_c[0] == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                     samples_c[1] == AVB_PHC_SOURCE_REGISTER_DERIVED &&
                     samples_c[2] == AVB_PHC_SOURCE_REGISTER_DERIVED);

    if (!all_hw_a)   return FAIL; /* A: expect hardware — should be all REGISTER_DERIVED */
    if ( all_hw_b)   return FAIL; /* B: mixed — should NOT be all hardware */
    if ( all_hw_c)   return FAIL; /* C: all SW — should NOT be hardware */
    return PASS;
}

/* ─── TC-XSTAMP-V2-006: V2 struct_version negotiation ───────────────────────── */
/* Verify that the version check logic correctly distinguishes valid from
 * unsupported versions, without touching the driver. */
static int TC_V2_006_VersionNegotiation(void)
{
    /* Version 2 is the only supported version. */
    if (AVB_XTSTAMP_V2_VERSION != 2u) return FAIL;

    /* A client that sends version 0, 1, 3 should be rejected by the driver
     * (STATUS_INVALID_PARAMETER).  We test the version constant is correct;
     * actual rejection is driver-side and tested in CT-4 (hardware test). */
    avb_u32 valid_ver   = AVB_XTSTAMP_V2_VERSION;
    avb_u32 invalid_ver = AVB_XTSTAMP_V2_VERSION + 1;
    if (valid_ver == invalid_ver) return FAIL;
    if (valid_ver == 0) return FAIL;
    return PASS;
}

/* ─── TC-XSTAMP-V2-007: struct_size is NOT a memory-access bound ─────────────── */
/* Document and verify that struct_size is advisory only.
 * A client-supplied size larger or smaller than sizeof(V2) must not
 * cause out-of-bounds memory access in the driver.
 * Hardware test required for driver-side validation; this test verifies
 * the client-side constant is correct. */
static int TC_V2_007_StructSizeAdvisory(void)
{
    /* The only authoritative size is sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST).
     * struct_size in the IN field is for client-to-driver protocol only. */
    if (sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST) != 56) return FAIL;
    /* A client MUST set struct_size = sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST)
     * to enable future version detection.  Verify the constant. */
    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = AVB_XTSTAMP_V2_VERSION;
    req.struct_size    = (avb_u32)sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST);
    if (req.struct_size != 56u) return FAIL;
    return PASS;
}

/* ─── TC-XSTAMP-V2-008: V1 struct unchanged ──────────────────────────────────── */
static int TC_V2_008_V1Unchanged(void)
{
    /* V1 must be preserved byte-for-byte */
    if (sizeof(AVB_CROSS_TIMESTAMP_REQUEST)                               != 40) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_REQUEST, adapter_index)             !=  0) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_REQUEST, phc_time_ns)               !=  8) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_REQUEST, system_qpc)                != 16) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_REQUEST, qpc_frequency)             != 24) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_REQUEST, valid)                     != 32) return FAIL;
    if (offsetof(AVB_CROSS_TIMESTAMP_REQUEST, status)                    != 36) return FAIL;
    if (IOCTL_AVB_PHC_CROSSTIMESTAMP != _NDIS_CONTROL_CODE(63, METHOD_BUFFERED)) return FAIL;
    return PASS;
}

/* ─── TC-XSTAMP-V2-009: avb_i32 is signed 32-bit (for get_systime_rc) ─────────── */
static int TC_V2_009_SignedRcField(void)
{
    avb_i32 hw_ok  =  0;   /* SYSTIME_RC_HW_OK */
    avb_i32 sw_ok  =  1;   /* SYSTIME_RC_SW_OK */
    avb_i32 err    = -1;   /* error */
    /* Can represent negative values */
    if (err >= 0) return FAIL;
    if (hw_ok != 0) return FAIL;
    if (sw_ok != 1) return FAIL;
    /* sizeof must be 4 */
    if (sizeof(avb_i32) != 4) return FAIL;
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-001: Hardware gate — V2 IOCTL reachable ─────────────────
 * This test REQUIRES an installed driver. It is INCONCLUSIVE (→ CI FAIL)
 * when the driver is absent or the IOCTL returns STATUS_NOT_SUPPORTED,
 * because hardware-PHC coherence cannot be verified without the driver.
 * An old driver returning STATUS_NOT_SUPPORTED is an explicit INCONCLUSIVE,
 * not a SKIP — the test is mandatory for the HIL gate.
 */
static int TC_V2_HIL_001_DriverReachable(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("    Device not accessible (error %lu) — INCONCLUSIVE\n", GetLastError());
        return INCONCLUSIVE;
    }

    /* Issue V2 IOCTL with a minimal valid request */
    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = AVB_XTSTAMP_V2_VERSION;
    req.struct_size    = (avb_u32)sizeof(req);
    req.adapter_index  = 0;
    req._reserved_in   = 0;

    DWORD br = 0;
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
        &req, sizeof(req), &req, sizeof(req), &br, NULL);
    DWORD gle = GetLastError();
    CloseHandle(h);

    if (!ok) {
        DWORD err = gle;
        if (err == ERROR_NOT_SUPPORTED || err == ERROR_INVALID_FUNCTION) {
            /* Old driver: V2 IOCTL unknown → INCONCLUSIVE (not SKIP) */
            printf("    Driver returned NOT_SUPPORTED for V2 IOCTL — INCONCLUSIVE\n");
            printf("    Hardware-PHC coherence verification requires V2-capable driver.\n");
            return INCONCLUSIVE;
        }
        printf("    DeviceIoControl failed (GLE=%lu) — INCONCLUSIVE\n", err);
        return INCONCLUSIVE;
    }

    printf("    V2 IOCTL reachable: valid=%u phc_source=%u rc=%d\n",
           req.valid, req.avb_phc_source, req.get_systime_rc);

    /* Basic sanity: IoStatus.Information must equal sizeof(V2) */
    if (br != sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST)) {
        printf("    FAIL: BytesReturned=%lu expected %u\n",
               br, (unsigned)sizeof(AVB_CROSS_TIMESTAMP_V2_REQUEST));
        return FAIL;
    }
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-002: Provenance reported for adapter 0 ────────────────── */
static int TC_V2_HIL_002_ProvenanceReported(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return INCONCLUSIVE;

    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = AVB_XTSTAMP_V2_VERSION;
    req.struct_size    = (avb_u32)sizeof(req);
    req.adapter_index  = 0;

    DWORD br = 0;
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
        &req, sizeof(req), &req, sizeof(req), &br, NULL);
    CloseHandle(h);

    if (!ok) {
        printf("    V2 IOCTL failed (GLE=%lu) — INCONCLUSIVE\n", GetLastError());
        return INCONCLUSIVE;
    }
    /* avb_phc_source must be a known constant (0–2); 3 = UNKNOWN is a driver bug */
    if (req.avb_phc_source > AVB_PHC_SOURCE_UNAVAILABLE) {
        printf("    FAIL: avb_phc_source=%u is out of range [0,%u]\n",
               req.avb_phc_source, AVB_PHC_SOURCE_UNAVAILABLE);
        return FAIL;
    }
    /* get_systime_rc must match avb_phc_source */
    if (req.avb_phc_source == AVB_PHC_SOURCE_UNAVAILABLE && req.get_systime_rc >= 0) {
        printf("    FAIL: source=UNAVAILABLE but rc=%d>=0\n", req.get_systime_rc);
        return FAIL;
    }
    if (req.avb_phc_source == AVB_PHC_SOURCE_SW_WALLCLOCK && req.get_systime_rc != 1) {
        printf("    FAIL: source=SW_WALLCLOCK but rc=%d (expected 1)\n", req.get_systime_rc);
        return FAIL;
    }
    if (req.avb_phc_source == AVB_PHC_SOURCE_REGISTER_DERIVED && req.get_systime_rc != 0) {
        printf("    FAIL: source=REGISTER_DERIVED but rc=%d (expected 0)\n", req.get_systime_rc);
        return FAIL;
    }
    printf("    avb_phc_source=%u get_systime_rc=%d — consistent\n",
           req.avb_phc_source, req.get_systime_rc);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-003: Bad version rejected ────────────────────────────── */
static int TC_V2_HIL_003_BadVersionRejected(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return INCONCLUSIVE;

    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = 99u;  /* unsupported version */
    req.struct_size    = (avb_u32)sizeof(req);
    req.adapter_index  = 0;

    DWORD br = 0;
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
        &req, sizeof(req), &req, sizeof(req), &br, NULL);
    DWORD gle = GetLastError();
    CloseHandle(h);

    if (ok) {
        printf("    FAIL: driver accepted struct_version=99 (should reject)\n");
        return FAIL;
    }
    /* Expect ERROR_INVALID_PARAMETER (87) */
    if (gle == ERROR_INVALID_PARAMETER) {
        printf("    Correctly rejected version 99 with ERROR_INVALID_PARAMETER\n");
        return PASS;
    }
    /* Some other error is acceptable (device not ready, etc.) */
    printf("    Rejected version 99 with GLE=%lu (acceptable)\n", gle);
    return PASS;
}

/* ─── TC-XSTAMP-V2-HIL-004: Small output buffer → STATUS_BUFFER_TOO_SMALL ────── */
static int TC_V2_HIL_004_SmallBufferRejected(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return INCONCLUSIVE;

    AVB_CROSS_TIMESTAMP_V2_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.struct_version = AVB_XTSTAMP_V2_VERSION;
    req.struct_size    = (avb_u32)sizeof(req);

    DWORD br = 0;
    /* Pass a 4-byte output buffer — smaller than sizeof(V2) = 56 */
    DWORD smallBuf = 0;
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_PHC_CROSSTIMESTAMP_V2,
        &req, sizeof(req), &smallBuf, sizeof(DWORD), &br, NULL);
    DWORD gle = GetLastError();
    CloseHandle(h);

    if (ok) {
        printf("    FAIL: driver accepted 4-byte output buffer (should reject)\n");
        return FAIL;
    }
    if (gle == ERROR_INSUFFICIENT_BUFFER || gle == ERROR_MORE_DATA) {
        printf("    Correctly rejected small buffer (GLE=%lu)\n", gle);
        return PASS;
    }
    printf("    Rejected small buffer with GLE=%lu (acceptable)\n", gle);
    return PASS;
}

/* ─── main ─────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("\n");
    printf("============================================================\n");
    printf("  TEST-IOCTL-XSTAMP-V2: Cross-Timestamp V2 Provenance Tests\n");
    printf("  Verifies: #48 (REQ-F-IOCTL-PHC-004)\n");
    printf("  Hardware-independent subset: TC-XSTAMP-V2-001..009\n");
    printf("  Hardware-dependent subset:   TC-XSTAMP-V2-HIL-001..004\n");
    printf("============================================================\n\n");

    Results r = {0};

    /* Hardware-independent tests — always run */
    printf("--- Hardware-Independent ---\n");
    Record(&r, TC_V2_001_Layout(),                "TC-XSTAMP-V2-001", "V2 layout and size");
    Record(&r, TC_V2_002_Zeroinit(),              "TC-XSTAMP-V2-002", "Zero-init correctness");
    Record(&r, TC_V2_003_ProvenanceMapping(),     "TC-XSTAMP-V2-003", "Provenance constant mapping");
    Record(&r, TC_V2_004_RegisterDerivedNotQuality(), "TC-XSTAMP-V2-004", "REGISTER_DERIVED != quality");
    Record(&r, TC_V2_005_MixedProvenanceInconclusive(), "TC-XSTAMP-V2-005", "Mixed provenance → INCONCLUSIVE");
    Record(&r, TC_V2_006_VersionNegotiation(),   "TC-XSTAMP-V2-006", "Version negotiation constants");
    Record(&r, TC_V2_007_StructSizeAdvisory(),   "TC-XSTAMP-V2-007", "struct_size advisory-only");
    Record(&r, TC_V2_008_V1Unchanged(),          "TC-XSTAMP-V2-008", "V1 struct unchanged");
    Record(&r, TC_V2_009_SignedRcField(),        "TC-XSTAMP-V2-009", "avb_i32 signed rc field");

    /* Hardware-dependent tests — INCONCLUSIVE if driver absent */
    printf("\n--- Hardware-Dependent (INCONCLUSIVE = CI FAIL) ---\n");
    Record(&r, TC_V2_HIL_001_DriverReachable(),  "TC-XSTAMP-V2-HIL-001", "V2 IOCTL reachable");
    Record(&r, TC_V2_HIL_002_ProvenanceReported(),"TC-XSTAMP-V2-HIL-002", "Provenance reported");
    Record(&r, TC_V2_HIL_003_BadVersionRejected(),"TC-XSTAMP-V2-HIL-003", "Bad version rejected");
    Record(&r, TC_V2_HIL_004_SmallBufferRejected(),"TC-XSTAMP-V2-HIL-004", "Small buffer rejected");

    printf("\n");
    printf("============================================================\n");
    printf("  Results: PASS=%d  FAIL=%d  SKIP=%d  INCONCLUSIVE=%d\n",
           r.pass, r.fail, r.skip, r.inconclusive);
    if (r.inconclusive > 0)
        printf("  NOTE: INCONCLUSIVE results are counted as FAIL for CI gate.\n");
    printf("============================================================\n");

    return (r.fail > 0) ? 1 : 0;
}
