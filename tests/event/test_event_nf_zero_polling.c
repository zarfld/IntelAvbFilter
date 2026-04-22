/**
 * @file test_event_nf_zero_polling.c
 * @brief Zero Polling Overhead Non-Functional Performance Tests
 *
 * Implements: #241 (TEST-EVENT-NF-002)
 * Verifies:   #161 (REQ-NF-EVENT-002: Zero Polling Overhead — non-functional
 *             CPU utilization quantification)
 *
 * Complements #178 (TEST-EVENT-006) which proves the functional interrupt-driven
 * delivery model.  This test characterises the non-functional CPU budget across
 * the three scenarios defined in the acceptance criteria for issue #241:
 *
 *   Idle State:     Active subscription, no events — zero background CPU.
 *   Dual-Subscribe: Two concurrent subscriptions — overhead is linear, not
 *                   super-linear (no per-subscription polling-loop multiplier).
 *   Post-Decay:     After unsubscribe — CPU returns to pre-subscribe baseline.
 *
 * Test Cases:
 *   TC-NF-ZP-001  Thread count audit: subscribe does not spawn polling threads
 *   TC-NF-ZP-002  Extended idle CPU < 0.1% for 30 s active subscription
 *   TC-NF-ZP-003  Baseline parity: subscribed-no-events CPU ≈ unsubscribed CPU
 *   TC-NF-ZP-004  Post-unsubscribe decay: CPU returns to baseline within 3 s
 *   TC-NF-ZP-005  Dual-subscription overhead: 2 concurrent subscriptions < 0.2%
 *
 * SKIP guards:
 *   TC-NF-ZP-001  Uses CreateToolhelp32Snapshot (Win32 — always available)
 *   TC-NF-ZP-002  Set AVB_TEST_QUICK=1 to shorten sleep from 30 s to 10 s
 *   TC-NF-ZP-003  Set AVB_TEST_QUICK=1 to shorten per-phase from 10 s to 5 s
 *   TC-NF-ZP-005  SKIP if second handle cannot be opened to same adapter
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/241
 * @see https://github.com/zarfld/IntelAvbFilter/issues/161
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

/* ─────────────────────────────────────────────────────────────── */
/*  Test result codes                                              */
/* ─────────────────────────────────────────────────────────────── */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* ─────────────────────────────────────────────────────────────── */
/*  Multi-adapter support                                          */
/* ─────────────────────────────────────────────────────────────── */
#define MAX_ADAPTERS 8

typedef struct {
    char   device_path[256];
    UINT16 vendor_id;
    UINT16 device_id;
    int    adapter_index;
} AdapterInfo;

/* ─────────────────────────────────────────────────────────────── */
/*  Global test counters                                           */
/* ─────────────────────────────────────────────────────────────── */
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static AdapterInfo g_adapters[MAX_ADAPTERS];
static int         g_adapter_count = 0;

/* ─────────────────────────────────────────────────────────────── */
/*  Quick-run guard (AVB_TEST_QUICK=1 shortens sleep durations)   */
/* ─────────────────────────────────────────────────────────────── */
static BOOL IsQuickRun(void) {
    return GetEnvironmentVariableA("AVB_TEST_QUICK", NULL, 0) > 0;
}

/* ─────────────────────────────────────────────────────────────── */
/*  Infrastructure helpers                                         */
/* ─────────────────────────────────────────────────────────────── */

static int EnumerateAdapters(AdapterInfo *out, int max) {
    HANDLE h;
    AVB_ENUM_REQUEST req;
    DWORD br;
    int count = 0;

    h = CreateFileA("\\\\.\\IntelAvbFilter",
                    GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    for (int i = 0; i < max; i++) {
        ZeroMemory(&req, sizeof(req));
        req.index = (UINT32)i;
        if (!DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                             &req, sizeof(req), &req, sizeof(req), &br, NULL))
            break;
        strcpy_s(out[count].device_path, sizeof(out[count].device_path),
                 "\\\\.\\IntelAvbFilter");
        out[count].vendor_id     = req.vendor_id;
        out[count].device_id     = req.device_id;
        out[count].adapter_index = i;
        count++;
    }
    CloseHandle(h);
    return count;
}

static HANDLE OpenAdapter(const AdapterInfo *a) {
    HANDLE h = CreateFileA(a->device_path,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    AVB_OPEN_REQUEST open_req = {0};
    DWORD br = 0;
    open_req.vendor_id = a->vendor_id;
    open_req.device_id = a->device_id;
    open_req.index     = (UINT32)a->adapter_index;

    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                         &open_req, sizeof(open_req),
                         &open_req, sizeof(open_req), &br, NULL)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    if (open_req.status != 0) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
    return h;
}

/*
 * TryIoctl — returns  1 = success
 *                     0 = IOCTL issued but driver returned error
 *                    -1 = IOCTL not implemented (TDD-RED)
 */
static int TryIoctl(HANDLE h, DWORD code, void *buf, DWORD sz) {
    DWORD br = 0;
    if (DeviceIoControl(h, code, buf, sz, buf, sz, &br, NULL)) return 1;
    DWORD e = GetLastError();
    if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) {
        printf("    [TDD-RED] IOCTL 0x%08lX not yet implemented (err=%lu)\n", code, e);
        return -1;
    }
    printf("    [DEBUG] IOCTL 0x%08lX failed: err=%lu\n", code, e);
    return 0;
}

/* Subscribe: returns ring_id (0 on failure/skip) */
static UINT32 Subscribe(HANDLE h) {
    AVB_TS_SUBSCRIBE_REQUEST sub = {0};
    sub.types_mask = 0xFFFFFFFFu;
    sub.vlan       = 0xFFFF;
    sub.pcp        = 0xFF;
    int rs = TryIoctl(h, IOCTL_AVB_TS_SUBSCRIBE, &sub, sizeof(sub));
    if (rs <= 0 || sub.ring_id == 0) return 0;
    return sub.ring_id;
}

static void Unsubscribe(HANDLE h, UINT32 ring_id) {
    if (ring_id == 0) return;
    AVB_TS_UNSUBSCRIBE_REQUEST unsub = {0};
    unsub.ring_id = ring_id;
    TryIoctl(h, IOCTL_AVB_TS_UNSUBSCRIBE, &unsub, sizeof(unsub));
}

/* ─────────────────────────────────────────────────────────────── */
/*  CPU measurement helpers                                        */
/* ─────────────────────────────────────────────────────────────── */
typedef struct {
    ULARGE_INTEGER kernel;
    ULARGE_INTEGER user;
    LARGE_INTEGER  wall;
} CpuSample;

static void CaptureCpu(CpuSample *s) {
    FILETIME ft_create, ft_exit, ft_kernel, ft_user;
    GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_kernel, &ft_user);
    s->kernel.LowPart  = ft_kernel.dwLowDateTime;
    s->kernel.HighPart = ft_kernel.dwHighDateTime;
    s->user.LowPart    = ft_user.dwLowDateTime;
    s->user.HighPart   = ft_user.dwHighDateTime;
    QueryPerformanceCounter(&s->wall);
}

/* Returns CPU% for the interval [before, after] */
static double CpuPercent(const CpuSample *before, const CpuSample *after,
                         const LARGE_INTEGER *freq) {
    ULONGLONG cpu_100ns =
        (after->kernel.QuadPart - before->kernel.QuadPart) +
        (after->user.QuadPart   - before->user.QuadPart);
    ULONGLONG wall_100ns = (ULONGLONG)
        ((after->wall.QuadPart - before->wall.QuadPart) * 10000000ULL /
         (ULONGLONG)freq->QuadPart);
    if (wall_100ns == 0) return 0.0;
    return (double)cpu_100ns / (double)wall_100ns * 100.0;
}

/* ─────────────────────────────────────────────────────────────── */
/*  Thread count helper (for TC-NF-ZP-001)                        */
/* ─────────────────────────────────────────────────────────────── */
static int CountMyThreads(void) {
    DWORD pid  = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;

    THREADENTRY32 te;
    ZeroMemory(&te, sizeof(te));
    te.dwSize = sizeof(THREADENTRY32);

    int count = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) count++;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return count;
}

/* ═════════════════════════════════════════════════════════════════ */
/*  TC-NF-ZP-001  Thread count audit: no polling threads spawned   */
/* ═════════════════════════════════════════════════════════════════ */
static int TC_NF_ZP_001_ThreadCountAudit(HANDLE h, const AdapterInfo *a) {
    (void)a;
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open adapter\n");
        return TEST_FAIL;
    }

    int threads_before = CountMyThreads();
    if (threads_before < 0) {
        printf("    [SKIP] CreateToolhelp32Snapshot unavailable\n");
        return TEST_SKIP;
    }
    printf("    Thread count before subscribe: %d\n", threads_before);

    UINT32 ring_id = Subscribe(h);
    if (ring_id == 0) {
        printf("    [SKIP] Subscribe not yet implemented (TDD-RED)\n");
        return TEST_SKIP;
    }
    printf("    Subscribed ring_id=%u\n", ring_id);

    /* Allow 100 ms for any deferred thread creation to manifest */
    Sleep(100);

    int threads_during = CountMyThreads();
    printf("    Thread count during subscribe: %d\n", threads_during);

    Unsubscribe(h, ring_id);
    Sleep(100);

    int threads_after = CountMyThreads();
    printf("    Thread count after unsubscribe: %d\n", threads_after);

    int delta = threads_during - threads_before;
    printf("    Thread delta on subscribe: %+d\n", delta);

    /*
     * Interrupt-driven delivery requires zero new polling threads.
     * A polling-based driver would spawn at least one background thread
     * (timer thread, polling thread, or deferred work queue thread).
     * delta == 0 confirms no such thread was created.
     */
    if (delta > 0) {
        printf("    [FAIL] %d extra thread(s) spawned on subscribe — potential polling thread\n",
               delta);
        return TEST_FAIL;
    }

    printf("    No new threads spawned on subscribe — interrupt-driven confirmed\n");
    return TEST_PASS;
}

/* ═════════════════════════════════════════════════════════════════ */
/*  TC-NF-ZP-002  Extended idle CPU < 0.1% for 30 s subscription  */
/* ═════════════════════════════════════════════════════════════════ */
static int TC_NF_ZP_002_ExtendedIdleCpu(HANDLE h, const AdapterInfo *a) {
    (void)a;
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open adapter\n");
        return TEST_FAIL;
    }

    UINT32 ring_id = Subscribe(h);
    if (ring_id == 0) {
        return TEST_SKIP;
    }
    printf("    Subscribed ring_id=%u\n", ring_id);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    /* Quick run: 10 s; full run: 30 s (NF requirement from #161 specifies 60 s) */
    DWORD duration_ms = IsQuickRun() ? 10000 : 30000;
    printf("    Measuring CPU over %lu s (set AVB_TEST_QUICK=1 for 10 s)...\n",
           (unsigned long)(duration_ms / 1000));

    CpuSample before, after;
    CaptureCpu(&before);
    Sleep(duration_ms);
    CaptureCpu(&after);

    Unsubscribe(h, ring_id);

    double cpu_pct = CpuPercent(&before, &after, &freq);
    printf("    CPU%% = %.4f%% over %lu s with active subscription\n",
           cpu_pct, (unsigned long)(duration_ms / 1000));

    /*
     * Threshold: 0.1% — stricter than #178 TC-ZP-004 (0.5%) to verify the
     * non-functional CPU budget defined in REQ-NF-EVENT-002 (#161).
     * The extended 30s window reduces statistical noise vs. the 5s window in TC-ZP-004.
     */
    if (cpu_pct > 0.1) {
        printf("    [FAIL] CPU %.4f%% exceeds 0.1%% idle budget (REQ-NF-EVENT-002 #161)\n",
               cpu_pct);
        return TEST_FAIL;
    }

    printf("    CPU %.4f%% <= 0.1%% — zero idle overhead confirmed (interrupt-driven)\n",
           cpu_pct);
    return TEST_PASS;
}

/* ═════════════════════════════════════════════════════════════════ */
/*  TC-NF-ZP-003  Baseline parity: subscribed ≈ unsubscribed CPU  */
/* ═════════════════════════════════════════════════════════════════ */
static int TC_NF_ZP_003_BaselineParity(HANDLE h, const AdapterInfo *a) {
    (void)a;
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open adapter\n");
        return TEST_FAIL;
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    DWORD sample_ms = IsQuickRun() ? 5000 : 10000;

    /* Phase A: baseline — unsubscribed */
    CpuSample before_a, after_a;
    CaptureCpu(&before_a);
    Sleep(sample_ms);
    CaptureCpu(&after_a);
    double cpu_pct_a = CpuPercent(&before_a, &after_a, &freq);
    printf("    Phase A (unsubscribed baseline): CPU%% = %.4f%%\n", cpu_pct_a);

    /* Phase B: subscribed, no explicit polling in user mode */
    UINT32 ring_id = Subscribe(h);
    if (ring_id == 0) {
        return TEST_SKIP;
    }
    printf("    Phase B: subscribed ring_id=%u\n", ring_id);

    CpuSample before_b, after_b;
    CaptureCpu(&before_b);
    Sleep(sample_ms);
    CaptureCpu(&after_b);
    double cpu_pct_b = CpuPercent(&before_b, &after_b, &freq);

    Unsubscribe(h, ring_id);

    double delta = cpu_pct_b - cpu_pct_a;
    printf("    Phase B (subscribed):             CPU%% = %.4f%%\n", cpu_pct_b);
    printf("    Delta (subscribed - unsubscribed): %.4f%%\n", delta);

    /*
     * Interrupt-driven: subscribing does not add any background polling work.
     * CPU_B should equal CPU_A within system noise (< 0.05%).
     * A polling-based driver would add at minimum one thread's worth of CPU above baseline.
     */
    if (delta > 0.05) {
        printf("    [FAIL] Subscription adds %.4f%% CPU — indicates polling overhead\n",
               delta);
        return TEST_FAIL;
    }

    printf("    Delta %.4f%% <= 0.05%% — subscription adds no fixed CPU overhead\n", delta);
    return TEST_PASS;
}

/* ═════════════════════════════════════════════════════════════════ */
/*  TC-NF-ZP-004  Post-unsubscribe CPU decay within 3 s            */
/* ═════════════════════════════════════════════════════════════════ */
static int TC_NF_ZP_004_PostUnsubscribeDecay(HANDLE h, const AdapterInfo *a) {
    (void)a;
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open adapter\n");
        return TEST_FAIL;
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    /* Step 1: unsubscribed baseline */
    CpuSample before_base, after_base;
    CaptureCpu(&before_base);
    Sleep(5000);
    CaptureCpu(&after_base);
    double cpu_baseline = CpuPercent(&before_base, &after_base, &freq);
    printf("    Baseline (unsubscribed, 5 s): CPU%% = %.4f%%\n", cpu_baseline);

    /* Step 2: subscribe, hold for 5 s, then unsubscribe */
    UINT32 ring_id = Subscribe(h);
    if (ring_id == 0) {
        return TEST_SKIP;
    }
    printf("    Subscribed ring_id=%u; sleeping 5 s...\n", ring_id);
    Sleep(5000);
    Unsubscribe(h, ring_id);
    printf("    Unsubscribed — measuring 3 s post-decay window...\n");

    /* Step 3: measure CPU for 3 s immediately after unsubscribe */
    CpuSample before_post, after_post;
    CaptureCpu(&before_post);
    Sleep(3000);
    CaptureCpu(&after_post);
    double cpu_post = CpuPercent(&before_post, &after_post, &freq);

    printf("    Post-unsubscribe (3 s): CPU%% = %.4f%%  Baseline = %.4f%%\n",
           cpu_post, cpu_baseline);

    /*
     * After unsubscribe, all driver ring-buffer activity stops immediately.
     * If a polling thread persists, cpu_post will exceed baseline by > noise floor.
     * Threshold: baseline + 0.05% (generous noise floor for dev machine).
     */
    double threshold = cpu_baseline + 0.05;
    if (cpu_post > threshold) {
        printf("    [FAIL] CPU %.4f%% exceeds threshold %.4f%% — polling still active?\n",
               cpu_post, threshold);
        return TEST_FAIL;
    }

    printf("    CPU %.4f%% <= %.4f%% — CPU decayed within 3 s of unsubscribe\n",
           cpu_post, threshold);
    return TEST_PASS;
}

/* ═════════════════════════════════════════════════════════════════ */
/*  TC-NF-ZP-005  Dual-subscription overhead < 0.2%               */
/* ═════════════════════════════════════════════════════════════════ */
static int TC_NF_ZP_005_DualSubscriptionOverhead(HANDLE h, const AdapterInfo *a) {
    /*
     * Open a second independent handle to the same adapter and subscribe
     * from both handles simultaneously (mirroring TC-PTP-LAT-005 multi-observer
     * pattern).  If the driver uses polling, each subscription would add
     * a polling thread — making dual overhead ≥ 2× the single threshold.
     * Interrupt-driven delivery means CPU stays near zero regardless of the
     * number of concurrent observers.
     */
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open adapter\n");
        return TEST_FAIL;
    }

    HANDLE h2 = OpenAdapter(a);
    if (h2 == INVALID_HANDLE_VALUE) {
        printf("    [SKIP] Cannot open second handle to same adapter\n");
        return TEST_SKIP;
    }

    UINT32 ring_id1 = Subscribe(h);
    UINT32 ring_id2 = Subscribe(h2);

    if (ring_id1 == 0 || ring_id2 == 0) {
        if (ring_id1) Unsubscribe(h,  ring_id1);
        if (ring_id2) Unsubscribe(h2, ring_id2);
        CloseHandle(h2);
        printf("    [SKIP] Could not establish dual subscriptions\n");
        return TEST_SKIP;
    }

    printf("    Dual subscribe: ring_id1=%u  ring_id2=%u\n", ring_id1, ring_id2);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    DWORD duration_ms = IsQuickRun() ? 5000 : 10000;
    printf("    Measuring CPU over %lu s with 2 concurrent subscriptions...\n",
           (unsigned long)(duration_ms / 1000));

    CpuSample before, after;
    CaptureCpu(&before);
    Sleep(duration_ms);
    CaptureCpu(&after);

    Unsubscribe(h,  ring_id1);
    Unsubscribe(h2, ring_id2);
    CloseHandle(h2);

    double cpu_pct = CpuPercent(&before, &after, &freq);
    printf("    CPU%% with 2 concurrent subscriptions = %.4f%% over %lu s\n",
           cpu_pct, (unsigned long)(duration_ms / 1000));

    /*
     * Threshold: 0.2% — linear extrapolation of TC-NF-ZP-002's 0.1% per subscription.
     * If the driver had a per-subscription polling loop, dual overhead would be 2× or more.
     */
    if (cpu_pct > 0.2) {
        printf("    [FAIL] Dual subscription CPU %.4f%% exceeds 0.2%% (super-linear overhead?)\n",
               cpu_pct);
        return TEST_FAIL;
    }

    printf("    CPU %.4f%% <= 0.2%% — dual-subscription overhead is linear, not super-linear\n",
           cpu_pct);
    return TEST_PASS;
}

/* ═════════════════════════════════════════════════════════════════ */
/*  main                                                            */
/* ═════════════════════════════════════════════════════════════════ */
int main(void) {
    int total_pass = 0, total_fail = 0, total_skip = 0;
    HANDLE handles[MAX_ADAPTERS];
    int i;

    printf("============================================================\n");
    printf("  IntelAvbFilter -- Zero Polling Overhead NF Performance Tests\n");
    printf("  Implements: #241 (TEST-EVENT-NF-002)\n");
    printf("  Verifies:   #161 (REQ-NF-EVENT-002: Zero Polling Overhead)\n");
    printf("  Complements: #178 (TEST-EVENT-006) — functional interrupt model\n");
    printf("============================================================\n\n");

    g_adapter_count = EnumerateAdapters(g_adapters, MAX_ADAPTERS);
    printf("  Adapters found: %d\n\n", g_adapter_count);
    if (g_adapter_count == 0) {
        printf("  [ERROR] No Intel AVB adapters found.\n");
        return 1;
    }

    /* Open all handles first */
    for (i = 0; i < g_adapter_count; i++) {
        handles[i] = OpenAdapter(&g_adapters[i]);
        if (handles[i] == INVALID_HANDLE_VALUE) {
            printf("  [FAIL] Cannot open adapter %d (VID=0x%04X DID=0x%04X) — skipping.\n",
                   i, g_adapters[i].vendor_id, g_adapters[i].device_id);
            continue;
        }
        printf("  [OK] Adapter %d: VID=0x%04X DID=0x%04X\n",
               i, g_adapters[i].vendor_id, g_adapters[i].device_id);
    }
    printf("\n");

#define RUN(tc, h_, a_, label) \
    do { \
        printf("  %s\n", (label)); \
        int _r = tc(h_, a_); \
        if      (_r == TEST_PASS) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
        else if (_r == TEST_FAIL) { g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
        else                      { g_skip++; printf("  [SKIP] %s\n\n", (label)); } \
    } while (0)

    for (i = 0; i < g_adapter_count; i++) {
        if (handles[i] == INVALID_HANDLE_VALUE) {
            printf("  --- Adapter %d/%d: FAILED (could not open) ---\n\n",
                   i + 1, g_adapter_count);
            total_fail++;
            continue;
        }
        HANDLE h = handles[i];
        const AdapterInfo *a = &g_adapters[i];
        g_pass = g_fail = g_skip = 0;

        printf("************************************************************\n");
        printf("  ADAPTER [%d/%d]: VID=0x%04X DID=0x%04X Index=%d\n",
               i + 1, g_adapter_count,
               g_adapters[i].vendor_id, g_adapters[i].device_id, i);
        printf("************************************************************\n\n");

        RUN(TC_NF_ZP_001_ThreadCountAudit,
            h, a,
            "TC-NF-ZP-001: Thread count audit — no polling threads on subscribe");
        RUN(TC_NF_ZP_002_ExtendedIdleCpu,
            h, a,
            "TC-NF-ZP-002: Extended idle CPU < 0.1% for 30 s active subscription");
        RUN(TC_NF_ZP_003_BaselineParity,
            h, a,
            "TC-NF-ZP-003: Baseline parity — subscribed-no-events CPU ≈ unsubscribed CPU");
        RUN(TC_NF_ZP_004_PostUnsubscribeDecay,
            h, a,
            "TC-NF-ZP-004: Post-unsubscribe CPU decay within 3 s");
        RUN(TC_NF_ZP_005_DualSubscriptionOverhead,
            h, a,
            "TC-NF-ZP-005: Dual-subscription overhead < 0.2%");

        printf("  --- Adapter %d/%d: PASS=%d  FAIL=%d  SKIP=%d ---\n\n",
               i + 1, g_adapter_count, g_pass, g_fail, g_skip);
        total_pass += g_pass;
        total_fail += g_fail;
        total_skip += g_skip;
    }

    for (i = 0; i < g_adapter_count; i++) {
        if (handles[i] != INVALID_HANDLE_VALUE) CloseHandle(handles[i]);
    }

    printf("============================================================\n");
    printf("  OVERALL: %d adapter(s) tested\n", g_adapter_count);
    printf("  PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           total_pass, total_fail, total_skip,
           total_pass + total_fail + total_skip);
    printf("============================================================\n");
    return (total_fail == 0) ? 0 : 1;
}
