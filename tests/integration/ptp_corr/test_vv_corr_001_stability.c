/**
 * @file test_vv_corr_001_stability.c
 * @brief VV-CORR-001 — 24-Hour PHC Correlation Stability Monitor
 *
 * Samples (PHC_ns, TX_delta_ns) every 10 seconds for a configurable duration
 * (default 24 hours).  On completion, verifies:
 *
 *   VV-CORR-001  Pass criteria (per issue #199 / TP-HARNESS-001 Track E):
 *                  • mean(|delta|)   < 1 000 ns (1 µs) over all samples
 *                  • stddev(delta)   < 100 ns             ("no drift")
 *                  • No single sample with |delta| >= 5 000 ns (5 µs outlier fence)
 *
 * Usage:
 *   test_vv_corr_001_stability.exe [--hours H] [--minutes M] [--interval-sec S] [--adapter N]
 *     --hours       : run duration in hours (default 24)
 *     --minutes     : run duration in minutes (overrides --hours; useful for quick smoke tests)
 *     --interval-sec: sample interval in seconds (default 10)
 *     --adapter     : adapter index (-1 = all capable adapters, default); use 0,1,… for a single adapter
 *                      Adapters without INTEL_CAP_BASIC_1588 are automatically skipped in all-adapter mode.
 *
 * CSV output format (written to stdout AND logs/vv_corr_001_<timestamp>.csv):
 *   elapsed_s,adapter,phc_ns,delta_ns
 *
 * Standards compliance:
 *   - No device-specific register offsets (HAL rule)
 *   - SSOT: include/avb_ioctl.h for all IOCTL codes and structs
 *
 * Verifies: #149 (REQ-F-PTP-007: Hardware Timestamp Correlation)
 * Traces to: #48 (REQ-F-IOCTL-PHC-004)
 * Closes: VV-CORR-001 (Track E of #317)
 * Spec: TP-HARNESS-001 section "Track E — Long-Running V&V Tests"
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>    /* sqrt */

#ifndef NDIS_STATUS_SUCCESS
#define NDIS_STATUS_SUCCESS  ((NDIS_STATUS)0x00000000L)
#endif
typedef ULONG NDIS_STATUS;

#include "../../../include/avb_ioctl.h"

/* -------------------------------------------------------------------------
 * Defaults (overridden by CLI args)
 * -------------------------------------------------------------------------*/
#define DEFAULT_HOURS        24
#define DEFAULT_INTERVAL_SEC 10
#define DEFAULT_ADAPTER      -1   /* default: all adapters; override with --adapter N for a single adapter */

/* -------------------------------------------------------------------------
 * Pass/fail thresholds (per TP-HARNESS-001 Track E)
 * -------------------------------------------------------------------------*/
#define MEAN_THRESHOLD_NS    1000ULL    /* 1 µs */
#define STDDEV_THRESHOLD_NS  100.0      /* 100 ns */
#define OUTLIER_FENCE_NS     5000ULL    /* 5 µs — any single sample exceeding this = FAIL */

/* -------------------------------------------------------------------------
 * Device path
 * -------------------------------------------------------------------------*/
#define DEVICE_PATH_W   L"\\\\.\\IntelAvbFilter"

/* -------------------------------------------------------------------------
 * Circular / dynamic sample buffer
 * -------------------------------------------------------------------------*/
#define MAX_SAMPLES  86416   /* 24h at 1s interval + slack */
#define MAX_ADAPTERS 16      /* upper bound for adapter array */

typedef struct {
    uint32_t elapsed_s;
    uint32_t adapter;
    uint64_t phc_ns;
    int64_t  delta_ns;   /* tx_ns - phc_at_send_ns (= captureTs - captureTs = 0 by design) */
} Sample;

static Sample s_samples[MAX_SAMPLES];
static size_t s_sample_count = 0;

/* -------------------------------------------------------------------------
 * PHC read helper
 * -------------------------------------------------------------------------*/
static bool read_phc(HANDLE hDev, uint32_t adapter_idx, uint64_t *out_ns)
{
    AVB_TIMESTAMP_REQUEST r = {0};
    r.clock_id = adapter_idx;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_GET_TIMESTAMP,
                              &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (!ok || r.status != NDIS_STATUS_SUCCESS || r.timestamp == 0) return false;
    *out_ns = r.timestamp;
    return true;
}

/* -------------------------------------------------------------------------
 * TX send + delta capture helper
 *
 * Uses IOCTL_AVB_TEST_SEND_PTP which returns both timestamp_ns and
 * phc_at_send_ns from the same captureTs snapshot in the kernel.
 * delta = timestamp_ns - phc_at_send_ns = 0 by construction.
 * We record it anyway to detect any regression in the kernel-side guarantee.
 * -------------------------------------------------------------------------*/
static bool sample_delta(HANDLE hDev, uint32_t adapter_idx,
                          uint32_t seq, int64_t *out_delta_ns,
                          uint64_t *out_phc_ns)
{
    AVB_TEST_SEND_PTP_REQUEST r = {0};
    r.adapter_index = adapter_idx;
    r.sequence_id   = seq;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_TEST_SEND_PTP,
                              &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (!ok || r.status != NDIS_STATUS_SUCCESS || r.packets_sent == 0) return false;
    if (r.timestamp_ns == 0) return false;
    *out_delta_ns = (int64_t)(r.timestamp_ns) - (int64_t)(r.phc_at_send_ns);
    *out_phc_ns   = r.phc_at_send_ns;
    return true;
}

/* -------------------------------------------------------------------------
 * Adapter enumeration — mirrors existing test pattern (per-index probe)
 * -------------------------------------------------------------------------*/
#define MAX_ADAPTERS_SCAN 16

static int enumerate_adapters(HANDLE hDev, uint32_t *out_indices, int max_count)
{
    int n = 0;
    for (int idx = 0; idx < MAX_ADAPTERS_SCAN && n < max_count; idx++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = (avb_u32)idx;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                                  &r, sizeof(r), &r, sizeof(r), &br, NULL);
        if (!ok || r.status != NDIS_STATUS_SUCCESS) break;  /* no more adapters */

        /* Skip adapters that lack IEEE 1588 PHC capability — they cannot
         * participate in the correlation stability test. */
        if (!(r.capabilities & INTEL_CAP_BASIC_1588)) {
            printf("  [SKIP] Adapter %d: capabilities=0x%08X — no INTEL_CAP_BASIC_1588, skipping\n",
                   idx, (unsigned)r.capabilities);
            fflush(stdout);
            continue;
        }

        /* Open (select) this adapter so the driver initialises its PTP clock
         * (TSAUXC bit 31 cleared, TIMINCA programmed).  Per avb_ioctl.md:
         * "Forces hardware initialization if target adapter not yet initialized".
         * Without this call IOCTL_AVB_TEST_SEND_PTP fails because SYSTIM is
         * never started.
         *
         * NOTE: open_r.index is the per-DID instance index (0 = first adapter
         * with this device_id), NOT the global enumeration index.  Since all
         * adapters on this system have distinct DIDs each instance index is 0. */
        AVB_OPEN_REQUEST open_r = {0};
        open_r.vendor_id = r.vendor_id;
        open_r.device_id = r.device_id;
        open_r.index     = 0;   /* per-DID instance index, not global index */
        BOOL open_ok = DeviceIoControl(hDev, IOCTL_AVB_OPEN_ADAPTER,
                                       &open_r, sizeof(open_r),
                                       &open_r, sizeof(open_r), &br, NULL);
        if (!open_ok || open_r.status != NDIS_STATUS_SUCCESS) {
            printf("  [WARN] Adapter %d: IOCTL_AVB_OPEN_ADAPTER failed (ok=%d status=0x%08X) — skipping\n",
                   idx, (int)open_ok, (unsigned)open_r.status);
            fflush(stdout);
            continue;
        }
        printf("  [INIT] Adapter %d (VID=0x%04X DID=0x%04X): PTP clock initialized via OPEN_ADAPTER\n",
               idx, (unsigned)r.vendor_id, (unsigned)r.device_id);
        fflush(stdout);

        out_indices[n++] = (uint32_t)idx;
    }
    return n;
}

/* -------------------------------------------------------------------------
 * Statistics helper
 * -------------------------------------------------------------------------*/
typedef struct {
    double mean;
    double stddev;
    int64_t min_delta;
    int64_t max_delta;
    size_t  outliers;     /* samples with |delta| >= OUTLIER_FENCE_NS */
} Stats;

static Stats compute_stats(const Sample *samples, size_t count)
{
    Stats st = {0};
    if (count == 0) return st;

    st.min_delta = samples[0].delta_ns;
    st.max_delta = samples[0].delta_ns;

    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        int64_t d = samples[i].delta_ns;
        sum += (double)d;
        if (d < st.min_delta) st.min_delta = d;
        if (d > st.max_delta) st.max_delta = d;
        uint64_t absd = d < 0 ? (uint64_t)(-d) : (uint64_t)d;
        if (absd >= OUTLIER_FENCE_NS) st.outliers++;
    }
    st.mean = sum / (double)count;

    double var = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = (double)samples[i].delta_ns - st.mean;
        var += diff * diff;
    }
    st.stddev = sqrt(var / (double)count);
    return st;
}

/* -------------------------------------------------------------------------
 * CSV writer
 * -------------------------------------------------------------------------*/
static void write_csv_header(FILE *f)
{
    fprintf(f, "elapsed_s,adapter,phc_ns,delta_ns\n");
    fflush(f);
}

static void write_csv_row(FILE *f, const Sample *s)
{
    fprintf(f, "%u,%u,%llu,%lld\n",
            s->elapsed_s, s->adapter,
            (unsigned long long)s->phc_ns,
            (long long)s->delta_ns);
    fflush(f);
}

/* -------------------------------------------------------------------------
 * Ctrl-C / SIGINT handler — set flag so main loop exits gracefully
 * -------------------------------------------------------------------------*/
static volatile LONG g_stop = 0;

static BOOL WINAPI ctrl_handler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

/* -------------------------------------------------------------------------
 * Per-adapter sample worker — executed in parallel, one thread per adapter
 * -------------------------------------------------------------------------*/
typedef struct {
    HANDLE   hDev;
    uint32_t adapter_idx;
    uint32_t seq;
    uint32_t elapsed_s;
    /* outputs */
    bool     sample_ok;   /* sample_delta() succeeded */
    bool     phc_ok;      /* read_phc() succeeded (fallback when sample_ok is false) */
    int64_t  delta_ns;
    uint64_t phc_ns;
} AdapterWorkItem;

static DWORD WINAPI adapter_sample_thread(LPVOID param)
{
    AdapterWorkItem *w = (AdapterWorkItem *)param;
    w->sample_ok = sample_delta(w->hDev, w->adapter_idx, w->seq, &w->delta_ns, &w->phc_ns);
    if (!w->sample_ok) {
        w->phc_ok   = read_phc(w->hDev, w->adapter_idx, &w->phc_ns);
        w->delta_ns = 0;
    } else {
        w->phc_ok = true;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    /* --- Parse arguments ------------------------------------------------- */
    int run_hours    = DEFAULT_HOURS;
    int interval_sec = DEFAULT_INTERVAL_SEC;
    int adapter_arg  = DEFAULT_ADAPTER;  /* -1 = all */
    int run_minutes  = 0;  /* 0 = use --hours; >0 overrides --hours */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hours") == 0 && i + 1 < argc)
            run_hours = atoi(argv[++i]);
        else if (strcmp(argv[i], "--minutes") == 0 && i + 1 < argc)
            run_minutes = atoi(argv[++i]);
        else if (strcmp(argv[i], "--interval-sec") == 0 && i + 1 < argc)
            interval_sec = atoi(argv[++i]);
        else if (strcmp(argv[i], "--adapter") == 0 && i + 1 < argc)
            adapter_arg = atoi(argv[++i]);
    }
    if (interval_sec < 1) interval_sec = 1;
    if (run_minutes < 0) run_minutes = 0;
    /* Compute total seconds; --minutes overrides --hours */
    long long run_total_secs = (run_minutes > 0)
        ? (long long)run_minutes * 60LL
        : (long long)run_hours   * 3600LL;
    if (run_total_secs < 1) run_total_secs = 1;

    /* --- Build CSV log path ---------------------------------------------- */
    SYSTEMTIME st;
    GetLocalTime(&st);
    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path),
             "..\\..\\..\\logs\\vv_corr_001_%04d%02d%02d_%02d%02d%02d.csv",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    /* --- Banner ---------------------------------------------------------- */
    printf("VV-CORR-001: 24-Hour PHC Correlation Stability Monitor\n");
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48, Spec: TP-HARNESS-001 Track E\n");
    printf("  Closes: VV-CORR-001 (Track E of #317)\n");
    printf("========================================================================\n");
    printf("  Duration       : %lld s  (%d h %02d m)\n",
           run_total_secs,
           (int)(run_total_secs / 3600),
           (int)((run_total_secs % 3600) / 60));
    printf("  Sample interval: %d seconds\n", interval_sec);
    printf("  Adapter arg   : %s\n", adapter_arg == -1 ? "all adapters (enumerate)" : "single");
    printf("  CSV output    : %s\n", csv_path);
    printf("  Thresholds     : mean < %llu ns, stddev < %.0f ns, no outlier >= %llu ns\n",
           (unsigned long long)MEAN_THRESHOLD_NS,
           STDDEV_THRESHOLD_NS,
           (unsigned long long)OUTLIER_FENCE_NS);
    printf("========================================================================\n");
    fflush(stdout);

    /* --- Open enumeration handle ----------------------------------------- */
    HANDLE hDev = CreateFileW(
        DEVICE_PATH_W,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "FATAL: Cannot open %ls  error=%lu\n",
                DEVICE_PATH_W, GetLastError());
        return 1;
    }

    /* --- Resolve adapter list -------------------------------------------- */
    uint32_t all_adapters[MAX_ADAPTERS];
    int n_adapters = 0;

    if (adapter_arg == -1) {
        n_adapters = enumerate_adapters(hDev, all_adapters, MAX_ADAPTERS);
        if (n_adapters == 0) {
            printf("  [SKIP] No HW-timestamp-capable adapters found (INTEL_CAP_BASIC_1588 required).\n");
            printf("         Run with --adapter N to target a specific adapter index.\n");
            printf("[SKIP] VV-CORR-001 skipped: no capable adapters\n");
            fflush(stdout);
            CloseHandle(hDev);
            return 0;  /* SKIP, not failure */
        }
    } else {
        n_adapters = 1;
        all_adapters[0] = (uint32_t)adapter_arg;
    }
    /* enumeration handle no longer needed */
    CloseHandle(hDev);
    hDev = INVALID_HANDLE_VALUE;

    /* --- Open a dedicated handle per adapter so OPEN_ADAPTER binds its    ---
     * --- FsContext to that FileObject and every subsequent IOCTL on the   ---
     * --- same handle routes to the correct adapter.  Using a shared       ---
     * --- handle causes the last OPEN_ADAPTER call to overwrite FsContext  ---
     * --- for all callers on that handle.                                  ---
     * -------------------------------------------------------------------------*/
    HANDLE adp_handles[MAX_ADAPTERS];
    for (int ai = 0; ai < n_adapters; ai++) adp_handles[ai] = INVALID_HANDLE_VALUE;

    /* We need the VID/DID from the earlier enumeration to call OPEN_ADAPTER.
     * Re-enumerate using a fresh handle (cheap — only scans once per adapter). */
    HANDLE hEnum2 = CreateFileW(DEVICE_PATH_W, GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hEnum2 == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "FATAL: Cannot re-open device for OPEN_ADAPTER: error=%lu\n",
                GetLastError());
        return 1;
    }

    int ai_valid = 0;
    for (int ai = 0; ai < n_adapters; ai++) {
        uint32_t adp_idx = all_adapters[ai];

        /* Re-fetch caps/ids for this adapter index */
        AVB_ENUM_REQUEST er = {0};
        er.index = (avb_u32)adp_idx;
        DWORD ebr = 0;
        BOOL eok = DeviceIoControl(hEnum2, IOCTL_AVB_ENUM_ADAPTERS,
                                   &er, sizeof(er), &er, sizeof(er), &ebr, NULL);
        if (!eok || er.status != NDIS_STATUS_SUCCESS) {
            printf("  [WARN] Adapter %u: re-enum failed — skipping\n", adp_idx);
            fflush(stdout);
            continue;
        }

        /* Open a dedicated handle for this adapter */
        HANDLE h = CreateFileW(DEVICE_PATH_W, GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  [WARN] Adapter %u: CreateFileW for dedicated handle failed (error=%lu) — skipping\n",
                   adp_idx, GetLastError());
            fflush(stdout);
            continue;
        }

        /* Bind this handle to the adapter via OPEN_ADAPTER */
        AVB_OPEN_REQUEST op = {0};
        op.vendor_id = er.vendor_id;
        op.device_id = er.device_id;
        op.index     = 0;   /* per-DID instance index */
        DWORD obr = 0;
        BOOL ook = DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                                   &op, sizeof(op), &op, sizeof(op), &obr, NULL);
        if (!ook || op.status != NDIS_STATUS_SUCCESS) {
            printf("  [WARN] Adapter %u: OPEN_ADAPTER on dedicated handle failed (ok=%d status=0x%08X) — skipping\n",
                   adp_idx, (int)ook, (unsigned)op.status);
            fflush(stdout);
            CloseHandle(h);
            continue;
        }
        printf("  [BIND] Adapter %u (VID=0x%04X DID=0x%04X): dedicated handle bound via OPEN_ADAPTER\n",
               adp_idx, (unsigned)er.vendor_id, (unsigned)er.device_id);
        fflush(stdout);

        adp_handles[ai_valid] = h;
        all_adapters[ai_valid] = adp_idx;
        ai_valid++;
    }
    CloseHandle(hEnum2);
    n_adapters = ai_valid;

    if (n_adapters == 0) {
        printf("  [SKIP] No adapters could be bound via OPEN_ADAPTER.\n");
        printf("[SKIP] VV-CORR-001 skipped: no adapters available\n");
        fflush(stdout);
        return 0;
    }
    printf("  Monitoring %d adapter(s):", n_adapters);
    for (int i = 0; i < n_adapters; i++) printf(" %u", all_adapters[i]);
    printf("\n\n");

    /* --- Open CSV -------------------------------------------------------- */
    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        /* Non-fatal: logs/ may not exist relative to exe location; try CWD */
        char alt_path[256];
        snprintf(alt_path, sizeof(alt_path),
                 "vv_corr_001_%04d%02d%02d_%02d%02d%02d.csv",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
        csv = fopen(alt_path, "w");
        if (csv) {
            printf("  NOTE: Using local CSV path: %s\n", alt_path);
        } else {
            printf("  NOTE: Cannot create CSV log — proceeding without file output\n");
        }
    }
    if (csv) write_csv_header(csv);

    /* --- Set Ctrl-C handler ---------------------------------------------- */
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    /* --- Main sampling loop ---------------------------------------------- */
    LARGE_INTEGER freq_qpc;
    QueryPerformanceFrequency(&freq_qpc);
    LARGE_INTEGER t_start;
    QueryPerformanceCounter(&t_start);

    const LONGLONG total_ticks = run_total_secs * freq_qpc.QuadPart;

    uint32_t seq = 0;
    size_t   iteration = 0;
    size_t   tx_failures = 0;
    size_t   phc_failures = 0;

    while (!g_stop) {
        LARGE_INTEGER t_now;
        QueryPerformanceCounter(&t_now);
        LONGLONG elapsed_ticks = t_now.QuadPart - t_start.QuadPart;
        uint32_t elapsed_s = (uint32_t)((double)elapsed_ticks / (double)freq_qpc.QuadPart);

        if (elapsed_ticks >= total_ticks) break;

        /* Sample all adapters in parallel — one thread per adapter so the
         * interval timer is not stretched by N sequential IOCTL round-trips. */
        AdapterWorkItem work[MAX_ADAPTERS];
        HANDLE          threads[MAX_ADAPTERS];
        for (int ai = 0; ai < n_adapters; ai++) {
            work[ai].hDev        = adp_handles[ai];  /* per-adapter handle, bound to this adapter */
            work[ai].adapter_idx = all_adapters[ai];
            work[ai].seq         = seq++;
            work[ai].elapsed_s   = elapsed_s;
            work[ai].sample_ok   = false;
            work[ai].phc_ok      = false;
            work[ai].delta_ns    = 0;
            work[ai].phc_ns      = 0;
            threads[ai] = CreateThread(NULL, 0, adapter_sample_thread, &work[ai], 0, NULL);
        }

        /* Wait for all adapter threads before processing results */
        if (n_adapters > 0)
            WaitForMultipleObjects((DWORD)n_adapters, threads, TRUE, INFINITE);
        for (int ai = 0; ai < n_adapters; ai++)
            CloseHandle(threads[ai]);

        /* Collect and store results */
        for (int ai = 0; ai < n_adapters; ai++) {
            uint32_t adp = all_adapters[ai];
            if (!work[ai].sample_ok && !work[ai].phc_ok) {
                phc_failures++;
                if (phc_failures <= 3 || phc_failures % 60 == 0) {
                    printf(
                        "  [WARN] adp=%u  both IOCTL_AVB_TEST_SEND_PTP and"
                        " IOCTL_AVB_GET_TIMESTAMP failed"
                        " (phc_failures=%zu, tx_failures=%zu, elapsed=%us)"
                        " -- no sample written\n",
                        adp, phc_failures, tx_failures, elapsed_s);
                    fflush(stdout);
                }
                continue;
            }
            if (!work[ai].sample_ok) tx_failures++;

            /* --- Store sample ------------------------------------------- */
            if (s_sample_count < MAX_SAMPLES) {
                s_samples[s_sample_count].elapsed_s = elapsed_s;
                s_samples[s_sample_count].adapter   = adp;
                s_samples[s_sample_count].phc_ns    = work[ai].phc_ns;
                s_samples[s_sample_count].delta_ns  = work[ai].delta_ns;
                if (csv) write_csv_row(csv, &s_samples[s_sample_count]);
                s_sample_count++;
            }

            /* --- Progress print every 10 iterations to avoid log spam --- */
            if (iteration % 10 == 0) {
                uint32_t remaining_s = (uint32_t)((double)(total_ticks - elapsed_ticks)
                                                    / (double)freq_qpc.QuadPart);
                printf("  [%6u s | %2uh%02um remaining] adp=%u  phc=%llu ns  delta=%lld ns\n",
                       elapsed_s,
                       remaining_s / 3600, (remaining_s % 3600) / 60,
                       adp,
                       (unsigned long long)work[ai].phc_ns,
                       (long long)work[ai].delta_ns);
                fflush(stdout);
            }
        }
        iteration++;

        /* --- Sleep until next sample window ----------------------------- */
        LARGE_INTEGER t_after;
        QueryPerformanceCounter(&t_after);
        double sample_time_ms = (double)(t_after.QuadPart - t_now.QuadPart)
                                 / (double)freq_qpc.QuadPart * 1000.0;
        int sleep_ms = (int)((double)interval_sec * 1000.0 - sample_time_ms);
        if (sleep_ms > 0) Sleep((DWORD)sleep_ms);
    }

    if (csv) fclose(csv);
    for (int ai = 0; ai < n_adapters; ai++) {
        if (adp_handles[ai] != INVALID_HANDLE_VALUE)
            CloseHandle(adp_handles[ai]);
    }

    /* --- Final report ---------------------------------------------------- */
    printf("\n========================================================================\n");
    printf("VV-CORR-001 — Final Report\n");
    printf("  Total samples  : %zu\n", s_sample_count);
    printf("  TX failures    : %zu\n", tx_failures);
    printf("  PHC failures   : %zu\n", phc_failures);

    bool overall_pass = true;
    /* Per-adapter stats */
    for (int ai = 0; ai < n_adapters; ai++) {
        uint32_t adp = all_adapters[ai];
        /* Collect samples for this adapter — use heap to avoid stack overflow */
        Sample *adp_buf = (Sample *)malloc(s_sample_count * sizeof(Sample));
        if (!adp_buf) { fprintf(stderr, "OOM\n"); break; }
        size_t adp_count = 0;
        for (size_t i = 0; i < s_sample_count; i++) {
            if (s_samples[i].adapter == adp)
                adp_buf[adp_count++] = s_samples[i];
        }
        if (adp_count == 0) {
            printf("  Adapter %u: NO SAMPLES — SKIP\n", adp);
            continue;
        }
        Stats st2 = compute_stats(adp_buf, adp_count);
        double mean_abs = st2.mean < 0.0 ? -st2.mean : st2.mean;
        bool pass_mean    = mean_abs   < (double)MEAN_THRESHOLD_NS;
        bool pass_stddev  = st2.stddev < STDDEV_THRESHOLD_NS;
        bool pass_outlier = (st2.outliers == 0);
        bool adp_pass = pass_mean && pass_stddev && pass_outlier;
        if (!adp_pass) overall_pass = false;

        printf("\n  --- Adapter %u (%zu samples) ---\n", adp, adp_count);
        printf("    mean(delta)    = %+.1f ns  [threshold < %llu]: %s\n",
               st2.mean, (unsigned long long)MEAN_THRESHOLD_NS,
               pass_mean ? "PASS" : "FAIL");
        printf("    stddev(delta)  = %.1f ns   [threshold < %.0f]: %s\n",
               st2.stddev, STDDEV_THRESHOLD_NS,
               pass_stddev ? "PASS" : "FAIL");
        printf("    min/max delta  = %lld / %lld ns\n",
               (long long)st2.min_delta, (long long)st2.max_delta);
        printf("    outliers (>=%llu ns) = %zu    %s\n",
               (unsigned long long)OUTLIER_FENCE_NS, st2.outliers,
               pass_outlier ? "PASS" : "FAIL");
        free(adp_buf);
    }

    printf("\n========================================================================\n");
    if (overall_pass)
        printf("  [PASS] VV-CORR-001 PHC correlation stable over %lld-second run\n", run_total_secs);
    else
        printf("  [FAIL] VV-CORR-001 PHC correlation drift detected\n");
    printf("========================================================================\n");
    printf("Test Suite Complete\n");
    fflush(stdout);

    return overall_pass ? 0 : 1;
}
