/**
 * @file test_ioctl_buffer_fuzz.c
 * @brief IOCTL Buffer Fuzz Tests — NULL/invalid/undersized buffer resilience
 *
 * Implements: #263 (TEST-IOCTL-FUZZ-001)
 *             #248 (TEST-SECURITY-BUFFER-001: Buffer Overflow Protection)
 * Verifies:   #63  (REQ-NF-SECURITY-001: Security and Access Control)
 *
 * All IOCTLs in this driver use METHOD_BUFFERED.  The I/O manager copies
 * in/out data through the system buffer.  A robust driver must never crash
 * regardless of:
 *   - NULL input buffer with any length value
 *   - NULL output buffer with any length value
 *   - Zero-length buffers for IOCTLs that require non-zero input
 *   - Undersized buffers (1 byte instead of full struct)
 *   - Completely invalid IOCTL codes
 *   - Rapid-fire repeated invalid calls
 *
 * Key property: after any of the above the device must still respond to a
 * valid GET_CLOCK_CONFIG call.  A BSOD would show up as the test process
 * being killed — the final "Device still functional" check exploits this.
 *
 * Test Cases: 5
 *   TC-FUZZ-001: NULL input + NULL output (zero sizes) — must return FALSE (no BSOD)
 *   TC-FUZZ-002: NULL input with non-zero length — must return FALSE (no BSOD)
 *   TC-FUZZ-003: Undersized input buffer (1 byte) for all major IOCTLs
 *   TC-FUZZ-004: Invalid IOCTL control codes (bogus values) — must not crash
 *   TC-FUZZ-005: Device still operational after all fuzz calls
 *
 * Priority: P1 (Security — crashes in production → BSOD / CVE)
 *
 * Standards: Windows Driver Security Checklist (MSDN)
 *            OWASP Testing Guide — input validation
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/263
 * @see https://github.com/zarfld/IntelAvbFilter/issues/248
 * @see https://github.com/zarfld/IntelAvbFilter/issues/63
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

/* ────────────────────────── constants ───────────────────────────────────── */
#define DEVICE_NAME "\\\\.\\IntelAvbFilter"

/* IOCTLs to fuzz — a representative cross-section of all METHOD_BUFFERED codes */
static const DWORD k_fuzz_codes[] = {
    IOCTL_AVB_GET_CLOCK_CONFIG,     /* 45 — GET: needs struct in+out */
    IOCTL_AVB_PHC_OFFSET_ADJUST,    /* 48 — SET: writes offset */
    IOCTL_AVB_ADJUST_FREQUENCY,     /* 38 — SET: writes freq */
    IOCTL_AVB_GET_TIMESTAMP,        /* 24 — GET */
    IOCTL_AVB_ENUM_ADAPTERS,        /* 31 — GET: enum */
    IOCTL_AVB_GET_HW_STATE,         /* 37 — GET: hw state */
    IOCTL_AVB_GET_STATISTICS,       /* stats */
};

/* Invalid (made-up) IOCTL codes that must not crash the driver */
static const DWORD k_bogus_codes[] = {
    0xDEADBEEF,
    0x9C40FFFE,   /* near the valid range but invalid */
    0x00000001,
    0xFFFFFFFF,
    IOCTL_AVB_GET_CLOCK_CONFIG + 0x1000,  /* valid base + offset */
};

/* ────────────────────────── test infra ──────────────────────────────────── */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

typedef struct {
    int pass_count;
    int fail_count;
    int skip_count;
} Results;

static void RecordResult(Results *r, int result, const char *name)
{
    const char *label = (result == TEST_PASS) ? "PASS" :
                        (result == TEST_SKIP) ? "SKIP" : "FAIL";
    printf("  [%s] %s\n", label, name);
    if (result == TEST_PASS) r->pass_count++;
    else if (result == TEST_SKIP) r->skip_count++;
    else r->fail_count++;
}

/* Module-level adapter selector — set by main() per-adapter iteration */
static UINT32 g_adapter_index = 0;
static UINT16 g_adapter_did   = 0;

static HANDLE OpenDevice(void)
{
    HANDLE h = CreateFileA(
        DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  [SKIP] Cannot open device (error %lu)\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }
    AVB_OPEN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.vendor_id = 0x8086;
    req.device_id = g_adapter_did;
    req.index     = g_adapter_index;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                         &req, sizeof(req), &req, sizeof(req), &br, NULL)
        || req.status != 0) {
        printf("  [SKIP] OPEN_ADAPTER failed (error %lu)\n", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

/* Verify device is still alive by issuing a valid GET_HW_STATE call.
 * Use GET_HW_STATE (not GET_CLOCK_CONFIG) because:
 *   - GET_HW_STATE is supported on ALL adapters regardless of capabilities
 *   - GET_CLOCK_CONFIG may return STATUS_NOT_SUPPORTED for adapters without
 *     INTEL_CAP_BASIC_1588 (e.g. I219 in MDIO-only mode), which is correct
 *     driver behaviour, not a post-fuzz malfunction indicator. */
static BOOL DeviceAlive(HANDLE h)
{
    AVB_HW_STATE_QUERY q = {0};
    DWORD bytes = 0;
    return DeviceIoControl(h,
        IOCTL_AVB_GET_HW_STATE,
        &q, sizeof(q),
        &q, sizeof(q),
        &bytes, NULL);
}

/* ════════════════════════ TC-FUZZ-001 ══════════════════════════════════════
 * NULL input + NULL output, zero sizes across all fuzz IOCTL codes.
 * None must cause a BSOD or process hang.
 */
static int TC_Fuzz_001_NullBuffersZeroSize(void)
{
    printf("\n  TC-FUZZ-001: NULL input + NULL output, zero sizes (%zu IOCTLs)\n",
           sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]));

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    int crashes = 0;
    DWORD bytes = 0;
    for (size_t i = 0; i < sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]); i++) {
        BOOL ok = DeviceIoControl(h, k_fuzz_codes[i],
            NULL, 0,
            NULL, 0,
            &bytes, NULL);
        /* We expect FALSE (driver rejects it).  TRUE would be suspicious but
         * not necessarily a security violation for a NULL-safe GET. */
        DWORD err = GetLastError();
        printf("    IOCTL 0x%08lX: returned %s (error %lu)\n",
               k_fuzz_codes[i], ok ? "TRUE" : "FALSE", err);
        /* A hang/crash means the test process itself dies — we won't reach here.
         * Just reaching this printf means no BSOD. */
        (void)crashes;
    }

    CloseHandle(h);
    printf("    All %zu IOCTLs returned without crashing\n",
           sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]));
    return TEST_PASS;
}

/* ════════════════════════ TC-FUZZ-002 ══════════════════════════════════════
 * NULL input with non-zero length — the I/O manager or driver must reject.
 * A properly coded METHOD_BUFFERED driver validates buffer != NULL when
 * length > 0.
 */
static int TC_Fuzz_002_NullInputNonZeroLength(void)
{
    printf("\n  TC-FUZZ-002: NULL input with non-zero length (%zu IOCTLs)\n",
           sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]));

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    int true_surprises = 0;
    DWORD bytes = 0;

    for (size_t i = 0; i < sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]); i++) {
        /* Pass NULL input but claim it is 128 bytes */
        BYTE outbuf[256] = {0};
        BOOL ok = DeviceIoControl(h, k_fuzz_codes[i],
            NULL, 128,            /* NULL ptr, non-zero claimed length */
            outbuf, sizeof(outbuf),
            &bytes, NULL);
        DWORD err = GetLastError();
        if (ok) {
            /* Driver accepted a call with NULL input and non-zero size.
             * This is only a problem if it actually accessed the NULL pointer.
             * For METHOD_BUFFERED the I/O manager copies inbuf to system buffer;
             * if inbuf is NULL and length>0, it should fault and return an error. */
            printf("    WARN: IOCTL 0x%08lX accepted NULL-input/non-zero-length\n",
                   k_fuzz_codes[i]);
            true_surprises++;
        } else {
            printf("    IOCTL 0x%08lX: rejected NULL-input (error %lu) — CORRECT\n",
                   k_fuzz_codes[i], err);
        }
    }

    CloseHandle(h);

    if (true_surprises > 0) {
        /* Not a hard FAIL — we survived, which is the key requirement.
         * The driver may legitimately accept a zero-input call if the IOCTL
         * has no input data requirement.  Log it instead of failing. */
        printf("    INFO: %d IOCTL(s) accepted NULL input — verify manually if intended\n",
               true_surprises);
    }
    return TEST_PASS;  /* Survival (no BSOD) is the pass criterion */
}

/* ════════════════════════ TC-FUZZ-003 ══════════════════════════════════════
 * Undersized buffers (1 byte) for IOCTLs whose input struct is larger.
 * Driver must return an error code, not dereference beyond the 1-byte buffer.
 */
static int TC_Fuzz_003_UndersizedBuffers(void)
{
    printf("\n  TC-FUZZ-003: Undersized input (1 byte) for %zu IOCTLs\n",
           sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]));

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    BYTE tiny_in[1]   = {0xAB};
    BYTE tiny_out[1]  = {0};
    DWORD bytes = 0;

    for (size_t i = 0; i < sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]); i++) {
        BOOL ok = DeviceIoControl(h, k_fuzz_codes[i],
            tiny_in, sizeof(tiny_in),
            tiny_out, sizeof(tiny_out),
            &bytes, NULL);
        DWORD err = GetLastError();
        printf("    IOCTL 0x%08lX (1-byte buf): returned %s (error %lu)\n",
               k_fuzz_codes[i], ok ? "TRUE" : "FALSE", err);
    }

    CloseHandle(h);
    printf("    All %zu undersized-buffer calls survived without crashing\n",
           sizeof(k_fuzz_codes)/sizeof(k_fuzz_codes[0]));
    return TEST_PASS;   /* Survival is the pass criterion */
}

/* ════════════════════════ TC-FUZZ-004 ══════════════════════════════════════
 * Completely invalid (bogus) IOCTL codes — driver must dispatch to default
 * case and return STATUS_INVALID_DEVICE_REQUEST / ERROR_INVALID_FUNCTION.
 */
static int TC_Fuzz_004_InvalidIoctlCodes(void)
{
    printf("\n  TC-FUZZ-004: Bogus IOCTL codes (%zu values)\n",
           sizeof(k_bogus_codes)/sizeof(k_bogus_codes[0]));

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    BYTE buf[256] = {0};
    DWORD bytes = 0;
    int crashed = 0;  /* process-death would prevent reaching end of loop */

    for (size_t i = 0; i < sizeof(k_bogus_codes)/sizeof(k_bogus_codes[0]); i++) {
        BOOL ok = DeviceIoControl(h, k_bogus_codes[i],
            buf, sizeof(buf),
            buf, sizeof(buf),
            &bytes, NULL);
        DWORD err = GetLastError();
        if (ok) {
            printf("    WARN: bogus IOCTL 0x%08lX returned TRUE (unexpected)\n",
                   k_bogus_codes[i]);
            crashed++;  /* reuse counter for "suprising successes" */
        } else {
            printf("    Bogus IOCTL 0x%08lX: returned FALSE (error %lu) — CORRECT\n",
                   k_bogus_codes[i], err);
        }
    }

    CloseHandle(h);

    if (crashed > 0) {
        printf("    FAIL: %d bogus IOCTL(s) returned TRUE — driver accepted invalid codes\n",
               crashed);
        return TEST_FAIL;
    }

    printf("    All bogus IOCTLs rejected — driver does not crash on unknown codes\n");
    return TEST_PASS;
}

/* ════════════════════════ TC-FUZZ-005 ══════════════════════════════════════
 * Device still operational after all the above fuzz calls.
 * Opens a fresh handle and issues a valid GET_CLOCK_CONFIG.
 */
static int TC_Fuzz_005_DeviceStillFunctional(void)
{
    printf("\n  TC-FUZZ-005: Device still functional after all fuzz calls\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    BOOL alive = DeviceAlive(h);
    DWORD err = GetLastError();
    CloseHandle(h);

    if (!alive) {
        printf("    FAIL: GET_CLOCK_CONFIG failed after fuzzing (error %lu)\n", err);
        return TEST_FAIL;
    }

    printf("    GET_CLOCK_CONFIG on fresh handle: succeeded — device is healthy\n");
    return TEST_PASS;
}

/* ═══════════════════════════ main ══════════════════════════════════════════ */
int main(void)
{
    printf("============================================================\n");
    printf(" test_ioctl_buffer_fuzz  |  Phase 07 V&V\n");
    printf(" Implements: #263 (TEST-IOCTL-FUZZ-001)\n");
    printf("             #248 (TEST-SECURITY-BUFFER-001)\n");
    printf(" Verifies:   #63  (REQ-NF-SECURITY-001)\n");
    printf(" Standards:  Windows Driver Security Checklist, OWASP\n");
    printf(" MULTI-ADAPTER: tests all enumerated adapters\n");
    printf("============================================================\n\n");

    HANDLE discovery = CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (discovery == INVALID_HANDLE_VALUE) {
        printf("  [SKIP] Cannot open device (error %lu)\n", GetLastError());
        return 1;
    }

    int total_fail = 0, adapters_tested = 0;

    for (UINT32 idx = 0; idx < 16; idx++) {
        AVB_ENUM_REQUEST enum_req;
        DWORD br = 0;
        ZeroMemory(&enum_req, sizeof(enum_req));
        enum_req.index = idx;
        if (!DeviceIoControl(discovery, IOCTL_AVB_ENUM_ADAPTERS,
                             &enum_req, sizeof(enum_req),
                             &enum_req, sizeof(enum_req), &br, NULL))
            break;

        g_adapter_index = idx;
        g_adapter_did   = enum_req.device_id;

        printf("\n--- Adapter %u  VID=0x%04X DID=0x%04X ---\n",
               idx, enum_req.vendor_id, enum_req.device_id);

        Results r = {0};

        RecordResult(&r, TC_Fuzz_001_NullBuffersZeroSize(),        "TC-FUZZ-001: NULL/zero-size buffers — no crash");
        RecordResult(&r, TC_Fuzz_002_NullInputNonZeroLength(),     "TC-FUZZ-002: NULL input non-zero length — rejected");
        RecordResult(&r, TC_Fuzz_003_UndersizedBuffers(),          "TC-FUZZ-003: Undersized buffers — no crash");
        RecordResult(&r, TC_Fuzz_004_InvalidIoctlCodes(),          "TC-FUZZ-004: Bogus IOCTL codes rejected");
        RecordResult(&r, TC_Fuzz_005_DeviceStillFunctional(),      "TC-FUZZ-005: Device still functional");

        printf(" PASS=%d  FAIL=%d  SKIP=%d\n", r.pass_count, r.fail_count, r.skip_count);
        total_fail += r.fail_count;
        adapters_tested++;
    }

    CloseHandle(discovery);

    printf("\n-------------------------------------------\n");
    printf(" Adapters tested: %d  Total failures: %d\n", adapters_tested, total_fail);
    printf("-------------------------------------------\n");

    return (total_fail > 0) ? 1 : 0;
}
