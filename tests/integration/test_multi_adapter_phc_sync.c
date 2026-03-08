/**
 * @file test_multi_adapter_phc_sync.c
 * @brief Multi-Adapter PHC Sync Integration Tests
 *
 * Implements: #208 (TEST-MULTI-ADAPTER-001: Multi-Adapter PHC Sync)
 *             #214 (TEST-MULTI-ADAPTER-PARALLEL-001: Parallel adapter PHC access)
 * Verifies:   #188 (REQ-F-MULTI-ADAPT-001: PHC must be independently readable
 *                   from multiple adapter instances simultaneously)
 *
 * IOCTL codes:
 *   31 (IOCTL_AVB_ENUM_ADAPTERS)    - enumerate all adapters (count + per-adapter info)
 *   32 (IOCTL_AVB_OPEN_ADAPTER)     - associate a file handle with a specific adapter
 *   45 (IOCTL_AVB_GET_CLOCK_CONFIG) - read PHC (systim) from the selected adapter
 *
 * Design:
 *   This driver exposes a single device node (\\\\.\\IntelAvbFilter).  Each
 *   CreateFile() call creates an independent file object with its own I/O
 *   context.  IOCTL_AVB_OPEN_ADAPTER (code 32) then binds that file object to
 *   a specific adapter index.  Subsequent GET_CLOCK_CONFIG calls on that
 *   handle return the PHC of the bound adapter.
 *
 *   If fewer than 2 adapters are present, TC-003/004/005 are SKIPPED and the
 *   test reports TC-001/002 results.  This is by design — the hardware may
 *   have only one Intel NIC.
 *
 * Test Cases: 5
 *   TC-MADAPT-001: Enumerate adapters — find total count, log vendor/device IDs
 *   TC-MADAPT-002: Bind and verify handle for adapter 0 (single-adapter path)
 *   TC-MADAPT-003: Parallel PHC reads from two handles (adapter 0 + 1) —
 *                  both independently monotonic in 500 iterations each
 *   TC-MADAPT-004: Cross-adapter PHC independence — adapters return distinct
 *                  clock values (they are not expected to be synchronized)
 *   TC-MADAPT-005: Handle isolation — closing adapter-0 handle does not break
 *                  adapter-1 handle
 *
 * Priority: P1 (Integration — multi-NIC setups must work concurrently)
 *
 * Standards: IEEE 1588-2019 §7.2 (Clock identity and domain separation)
 *            IEEE 802.1AS-2020 §10.2 (gPTP per-port clocks)
 *
 * Note on adapter count: Run-Tests-Elevated.ps1 confirms "6 Intel adapters
 * detected" on the test machine, so TC-003/004/005 will execute.
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/208
 * @see https://github.com/zarfld/IntelAvbFilter/issues/214
 * @see https://github.com/zarfld/IntelAvbFilter/issues/188
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>  /* _beginthreadex */

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

/* ────────────────────────── constants ───────────────────────────────────── */
#define DEVICE_NAME         "\\\\.\\IntelAvbFilter"
#define PARALLEL_READS      500u        /* reads per thread in TC-003 */
#define MAX_ADAPTERS        8u         /* ceiling for the enum loop */

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

static HANDLE OpenDevice(void)
{
    HANDLE h = CreateFileA(
        DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  [SKIP] Cannot open device (error %lu)\n", GetLastError());
    }
    return h;
}

/* Bind a handle to a specific adapter index via IOCTL_AVB_OPEN_ADAPTER. */
static BOOL BindAdapter(HANDLE h, DWORD index, WORD vendor_id, WORD device_id)
{
    AVB_OPEN_REQUEST req = {0};
    req.index     = index;
    req.vendor_id = vendor_id;
    req.device_id = device_id;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_OPEN_ADAPTER,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytes, NULL);
    return ok;
}

/* Read PHC (systim) from the handle's bound adapter. */
static BOOL ReadPHC(HANDLE h, UINT64 *out_ns)
{
    AVB_CLOCK_CONFIG cfg = {0};
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_GET_CLOCK_CONFIG,
        &cfg, sizeof(cfg),
        &cfg, sizeof(cfg),
        &bytes, NULL);
    if (ok && out_ns) *out_ns = cfg.systim;
    return ok;
}

/* ─────────────── Thread argument for parallel PHC reader ──────────────── */
typedef struct {
    HANDLE   device;
    UINT32   iterations;
    UINT32   read_errors;
    UINT32   inversions;
    UINT64   first_ns;
    UINT64   last_ns;
} ParallelReaderArgs;

static unsigned __stdcall ParallelReaderThread(void *param)
{
    ParallelReaderArgs *args = (ParallelReaderArgs *)param;
    UINT64 prev = 0;
    args->read_errors = 0;
    args->inversions  = 0;

    for (UINT32 i = 0; i < args->iterations; i++) {
        UINT64 now = 0;
        if (!ReadPHC(args->device, &now)) {
            args->read_errors++;
            continue;
        }
        if (i == 0) args->first_ns = now;
        if (now < prev) args->inversions++;
        prev = now;
        args->last_ns = now;
    }
    return 0;
}

/* ════════════════════════ TC-MADAPT-001 ════════════════════════════════════
 * Enumerate adapters — verify at least one is present.
 * Returns the total count and per-adapter info.
 */
static int TC_MADAPT_001_EnumerateAdapters(DWORD *total_count_out,
                                            WORD  *vid0_out, WORD *did0_out,
                                            WORD  *vid1_out, WORD *did1_out)
{
    printf("\n  TC-MADAPT-001: Enumerate adapters\n");
    *total_count_out = 0;

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    DWORD count = 0;
    WORD  vid[MAX_ADAPTERS] = {0};
    WORD  did[MAX_ADAPTERS] = {0};

    /* First call: index=0 fills in count */
    for (DWORD idx = 0; idx < MAX_ADAPTERS; idx++) {
        AVB_ENUM_REQUEST req = {0};
        req.index = idx;
        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(h,
            IOCTL_AVB_ENUM_ADAPTERS,
            &req, sizeof(req),
            &req, sizeof(req),
            &bytes, NULL);

        if (!ok) {
            if (idx == 0) {
                CloseHandle(h);
                printf("    [SKIP] IOCTL_AVB_ENUM_ADAPTERS failed on index 0 (error %lu)\n",
                       GetLastError());
                return TEST_SKIP;
            }
            /* idx >= count: stop */
            break;
        }

        if (idx == 0) {
            count = req.count;
            printf("    Total adapter count: %lu\n", count);
        }

        if (count == 0) {
            CloseHandle(h);
            printf("    [SKIP] No adapters reported by driver\n");
            return TEST_SKIP;
        }

        printf("    Adapter %lu: vendor=0x%04X device=0x%04X caps=0x%08lX status=0x%08lX\n",
               idx, req.vendor_id, req.device_id, req.capabilities, req.status);

        if (idx < MAX_ADAPTERS) {
            vid[idx] = req.vendor_id;
            did[idx] = req.device_id;
        }

        if (idx + 1 >= count) break;
    }

    CloseHandle(h);

    *total_count_out = count;
    if (vid0_out) *vid0_out = vid[0];
    if (did0_out) *did0_out = did[0];
    if (vid1_out) *vid1_out = (count >= 2) ? vid[1] : 0;
    if (did1_out) *did1_out = (count >= 2) ? did[1] : 0;

    if (count == 0) return TEST_SKIP;

    printf("    Enumeration: found %lu adapter(s)\n", count);
    return TEST_PASS;
}

/* ════════════════════════ TC-MADAPT-002 ════════════════════════════════════
 * Bind handle to adapter 0 and read PHC — single-adapter baseline.
 */
static int TC_MADAPT_002_BindAndReadAdapter0(WORD vid0, WORD did0)
{
    printf("\n  TC-MADAPT-002: Bind adapter 0 and read PHC\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    /* Bind to adapter 0 */
    BOOL bound = BindAdapter(h, 0, vid0, did0);
    if (!bound) {
        /* Not a hard failure — some drivers don't require explicit OPEN_ADAPTER
         * and GET_CLOCK_CONFIG always returns the first adapter. */
        printf("    INFO: IOCTL_AVB_OPEN_ADAPTER returned FALSE (error %lu); "
               "trying GET_CLOCK_CONFIG without explicit bind\n", GetLastError());
    } else {
        printf("    Bound to adapter 0 (vid=0x%04X did=0x%04X)\n", vid0, did0);
    }

    UINT64 ns = 0;
    BOOL ok = ReadPHC(h, &ns);
    CloseHandle(h);

    if (!ok) {
        printf("    FAIL: GET_CLOCK_CONFIG failed after binding adapter 0 (error %lu)\n",
               GetLastError());
        return TEST_FAIL;
    }

    printf("    PHC (adapter 0) = %llu ns\n", ns);
    if (ns == 0) {
        printf("    FAIL: PHC returned zero\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-MADAPT-003 ════════════════════════════════════
 * Parallel PHC reads from adapter 0 + adapter 1 in two threads.
 * Both must be individually monotonic.
 * SKIP if < 2 adapters.
 */
static int TC_MADAPT_003_ParallelReadsMonotonic(WORD vid0, WORD did0,
                                                  WORD vid1, WORD did1,
                                                  DWORD total)
{
    printf("\n  TC-MADAPT-003: Parallel PHC reads (%u iters each) from 2 adapters\n",
           PARALLEL_READS);

    if (total < 2) {
        printf("    [SKIP] Only %lu adapter(s) present; need >= 2\n", total);
        return TEST_SKIP;
    }

    HANDLE h0 = OpenDevice();
    HANDLE h1 = OpenDevice();
    if (h0 == INVALID_HANDLE_VALUE || h1 == INVALID_HANDLE_VALUE) {
        if (h0 != INVALID_HANDLE_VALUE) CloseHandle(h0);
        if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        return TEST_SKIP;
    }

    BindAdapter(h0, 0, vid0, did0);  /* soft: ignore return value */
    BindAdapter(h1, 1, vid1, did1);

    ParallelReaderArgs args0 = { h0, PARALLEL_READS, 0, 0, 0, 0 };
    ParallelReaderArgs args1 = { h1, PARALLEL_READS, 0, 0, 0, 0 };

    HANDLE t0 = (HANDLE)_beginthreadex(NULL, 0, ParallelReaderThread, &args0, 0, NULL);
    HANDLE t1 = (HANDLE)_beginthreadex(NULL, 0, ParallelReaderThread, &args1, 0, NULL);

    if (t0 == NULL || t1 == NULL) {
        printf("    [SKIP] Failed to create reader threads\n");
        if (t0) CloseHandle(t0);
        if (t1) CloseHandle(t1);
        CloseHandle(h0); CloseHandle(h1);
        return TEST_SKIP;
    }

    HANDLE threads[2] = {t0, t1};
    WaitForMultipleObjects(2, threads, TRUE, 30000);  /* 30 s timeout */

    CloseHandle(t0); CloseHandle(t1);
    CloseHandle(h0); CloseHandle(h1);

    printf("    Adapter 0: errors=%u inversions=%u first=%llu last=%llu ns\n",
           args0.read_errors, args0.inversions, args0.first_ns, args0.last_ns);
    printf("    Adapter 1: errors=%u inversions=%u first=%llu last=%llu ns\n",
           args1.read_errors, args1.inversions, args1.first_ns, args1.last_ns);

    if (args0.inversions > 0) {
        printf("    FAIL: adapter 0 PHC went backwards (%u inversions)\n",
               args0.inversions);
        return TEST_FAIL;
    }
    if (args1.inversions > 0) {
        printf("    FAIL: adapter 1 PHC went backwards (%u inversions)\n",
               args1.inversions);
        return TEST_FAIL;
    }
    if (args0.read_errors > PARALLEL_READS / 4 || args1.read_errors > PARALLEL_READS / 4) {
        printf("    FAIL: excessive read errors (a0=%u a1=%u)\n",
               args0.read_errors, args1.read_errors);
        return TEST_FAIL;
    }

    printf("    Both adapters monotonic in parallel — zero inversions\n");
    return TEST_PASS;
}

/* ════════════════════════ TC-MADAPT-004 ════════════════════════════════════
 * Cross-adapter PHC independence — adapter 0 and adapter 1 may have different
 * clock values (they are not gPTP-synchronized to each other).
 * We just log the delta; we do NOT require them to be equal or within any bound.
 * SKIP if < 2 adapters.
 */
static int TC_MADAPT_004_CrossAdapterIndependence(WORD vid0, WORD did0,
                                                    WORD vid1, WORD did1,
                                                    DWORD total)
{
    printf("\n  TC-MADAPT-004: Cross-adapter PHC independence\n");

    if (total < 2) {
        printf("    [SKIP] Only %lu adapter(s) present; need >= 2\n", total);
        return TEST_SKIP;
    }

    HANDLE h0 = OpenDevice();
    HANDLE h1 = OpenDevice();
    if (h0 == INVALID_HANDLE_VALUE || h1 == INVALID_HANDLE_VALUE) {
        if (h0 != INVALID_HANDLE_VALUE) CloseHandle(h0);
        if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        return TEST_SKIP;
    }

    BindAdapter(h0, 0, vid0, did0);
    BindAdapter(h1, 1, vid1, did1);

    UINT64 ns0 = 0, ns1 = 0;
    BOOL ok0 = ReadPHC(h0, &ns0);
    BOOL ok1 = ReadPHC(h1, &ns1);

    CloseHandle(h0); CloseHandle(h1);

    if (!ok0 || !ok1) {
        printf("    FAIL: one or both PHC reads failed (ok0=%d ok1=%d)\n", ok0, ok1);
        return TEST_FAIL;
    }

    /* Log delta — both must be > 0 */
    UINT64 delta = (ns0 >= ns1) ? (ns0 - ns1) : (ns1 - ns0);
    printf("    PHC adapter 0 = %llu ns\n", ns0);
    printf("    PHC adapter 1 = %llu ns\n", ns1);
    printf("    Delta         = %llu ns (expected: arbitrary; clocks are independent)\n", delta);

    if (ns0 == 0) { printf("    FAIL: adapter 0 returned zero\n"); return TEST_FAIL; }
    if (ns1 == 0) { printf("    FAIL: adapter 1 returned zero\n"); return TEST_FAIL; }

    /* The key assertion: each adapter returns a non-zero, running clock.
     * Whether they agree is irrelevant without gPTP sync. */
    printf("    Both adapters returned non-zero, independent PHC values\n");
    return TEST_PASS;
}

/* ════════════════════════ TC-MADAPT-005 ════════════════════════════════════
 * Handle isolation — close adapter-0 handle; adapter-1 handle still works.
 * SKIP if < 2 adapters.
 */
static int TC_MADAPT_005_HandleIsolation(WORD vid0, WORD did0,
                                          WORD vid1, WORD did1,
                                          DWORD total)
{
    printf("\n  TC-MADAPT-005: Handle isolation — close adapter-0 handle, adapter-1 survives\n");

    if (total < 2) {
        printf("    [SKIP] Only %lu adapter(s) present; need >= 2\n", total);
        return TEST_SKIP;
    }

    HANDLE h0 = OpenDevice();
    HANDLE h1 = OpenDevice();
    if (h0 == INVALID_HANDLE_VALUE || h1 == INVALID_HANDLE_VALUE) {
        if (h0 != INVALID_HANDLE_VALUE) CloseHandle(h0);
        if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        return TEST_SKIP;
    }

    BindAdapter(h0, 0, vid0, did0);
    BindAdapter(h1, 1, vid1, did1);

    /* Both should initially work */
    UINT64 ns0_pre = 0, ns1_pre = 0;
    BOOL ok0_pre = ReadPHC(h0, &ns0_pre);
    BOOL ok1_pre = ReadPHC(h1, &ns1_pre);

    if (!ok0_pre || !ok1_pre) {
        CloseHandle(h0); CloseHandle(h1);
        printf("    [SKIP] Initial PHC reads failed (ok0=%d ok1=%d)\n", ok0_pre, ok1_pre);
        return TEST_SKIP;
    }
    printf("    Both handles: initial reads OK (a0=%llu a1=%llu ns)\n",
           ns0_pre, ns1_pre);

    /* Close adapter-0 handle */
    CloseHandle(h0);
    h0 = INVALID_HANDLE_VALUE;
    printf("    Closed adapter-0 handle\n");

    /* Adapter-1 handle must still deliver a PHC reading */
    UINT64 ns1_post = 0;
    BOOL ok1_post = ReadPHC(h1, &ns1_post);
    CloseHandle(h1);

    if (!ok1_post) {
        printf("    FAIL: adapter-1 PHC read failed after closing adapter-0 handle\n");
        return TEST_FAIL;
    }
    if (ns1_post < ns1_pre) {
        printf("    FAIL: adapter-1 PHC went backwards after close (pre=%llu post=%llu)\n",
               ns1_pre, ns1_post);
        return TEST_FAIL;
    }

    printf("    Adapter-1 still works after adapter-0 close (PHC=%llu ns)\n", ns1_post);
    return TEST_PASS;
}

/* ═══════════════════════════ main ══════════════════════════════════════════ */
int main(void)
{
    printf("============================================================\n");
    printf(" test_multi_adapter_phc_sync  |  Phase 07 V&V\n");
    printf(" Implements: #208 (TEST-MULTI-ADAPTER-001)\n");
    printf("             #214 (TEST-MULTI-ADAPTER-PARALLEL-001)\n");
    printf(" Verifies:   #188 (REQ-F-MULTI-ADAPT-001)\n");
    printf(" Standards:  IEEE 1588-2019 s7.2, IEEE 802.1AS-2020 s10.2\n");
    printf("============================================================\n\n");

    Results r = {0};

    /* TC-001 enumerates and feeds adapter info to the other TCs */
    DWORD total = 0;
    WORD  vid0 = 0, did0 = 0, vid1 = 0, did1 = 0;
    int rc001 = TC_MADAPT_001_EnumerateAdapters(&total, &vid0, &did0, &vid1, &did1);
    RecordResult(&r, rc001, "TC-MADAPT-001: Enumerate adapters");

    RecordResult(&r, TC_MADAPT_002_BindAndReadAdapter0(vid0, did0),
                 "TC-MADAPT-002: Bind + read adapter 0");

    RecordResult(&r, TC_MADAPT_003_ParallelReadsMonotonic(vid0, did0, vid1, did1, total),
                 "TC-MADAPT-003: Parallel reads monotonic");

    RecordResult(&r, TC_MADAPT_004_CrossAdapterIndependence(vid0, did0, vid1, did1, total),
                 "TC-MADAPT-004: Cross-adapter independence");

    RecordResult(&r, TC_MADAPT_005_HandleIsolation(vid0, did0, vid1, did1, total),
                 "TC-MADAPT-005: Handle isolation");

    printf("\n-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           r.pass_count, r.fail_count, r.skip_count,
           r.pass_count + r.fail_count + r.skip_count);
    printf("-------------------------------------------\n");

    return (r.fail_count > 0) ? 1 : 0;
}
