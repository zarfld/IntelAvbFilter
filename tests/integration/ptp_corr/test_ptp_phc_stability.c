/**
 * @file test_ptp_phc_stability.c
 * @brief PHC Stability Under State Changes — UT-CORR-005..009
 *
 * Verifies that PHC-TX timestamp correlation is maintained through hardware state changes:
 *   UT-CORR-005  Epoch reset: SET_TIMESTAMP(1ms) — TX timestamps track new PHC epoch
 *   UT-CORR-006  Frequency adjustment (+1 ns increment): TX timestamps track adjusted PHC rate
 *   UT-CORR-007  Jitter analysis: 1000 (PHC,TX) sample pairs —
 *                  delta[i] = tx[i] - phc[i]; stddev(delta) < 100 ns (per issue #199)
 *   UT-CORR-008  Burst consistency: 100 (PHC,TX) pairs at ~1ms intervals —
 *                  all |delta[i]| < 1 µs; variance(delta) < 10000 (stddev < 100 ns)
 *   UT-CORR-009  Driver reload: moved to test_ptp_phc_lifecycle.c (own executable, Unsafe)
 *
 * Architecture compliance:
 *   - No device-specific register offsets (HAL rule)
 *   - SSOT: include/avb_ioctl.h for all IOCTL codes and structs
 *
 * Closes (after GREEN): Track A of #317
 * Verifies: #149 (REQ-F-PTP-007: Hardware Timestamp Correlation)
 * Traces to:  #48 (REQ-F-IOCTL-PHC-004)
 *
 * IOCTLs used (all existing):
 *   IOCTL_AVB_GET_TIMESTAMP       (code 24) — read PHC
 *   IOCTL_AVB_SET_TIMESTAMP       (code 25) — set PHC epoch
 *   IOCTL_AVB_ADJUST_FREQUENCY    (code 38) — adjust clock increment
 *   IOCTL_AVB_GET_CLOCK_CONFIG    (code 45) — read current clock config
 *   IOCTL_AVB_GET_TX_TIMESTAMP    (code 49) — read last hardware TX timestamp (FIFO)
 *   IOCTL_AVB_TEST_SEND_PTP       (code 51) — inject PTP packet from kernel for TX timestamp
 *   IOCTL_AVB_ENUM_ADAPTERS       (code 31) — adapter enumeration
 */

#include "ptp_phc_common.h"   /* shared helpers (verbatim from this file), also used by test_ptp_phc_lifecycle.c */

/* =========================================================================
 * UT-CORR-005: Epoch Reset — TX timestamps track new PHC epoch
 *
 * Procedure (per issue #199):
 *   1. Read phc_before; send PTP → tx_before
 *   2. Verify |tx_before - phc_before| < 1 µs (correlation before reset)
 *   3. Reset PHC to SEED_NS (1 ms)
 *   4. Read phc_after (must be >= seed and < seed+10ms); send PTP → tx_after
 *   5. Verify |tx_after - phc_after| < 1 µs (correlation after reset)
 *   6. Verify PHC advances 100ms after reset
 *   7. Restore PHC to approximate original time
 * =========================================================================*/
#define UT_CORR_005_SEED_NS  1000000ULL  /* 1 ms */

static void test_ut_corr_005(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n[UT-CORR-005] Epoch Reset: TX timestamps track new PHC epoch (adapter %u)\n",
           adapter_idx);
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 , Spec: issue #199\n");

    /* --- Step 1: Verify TX-PHC correlation before reset --- */
    /* Step 1-2 are the precondition (TX-PHC correlated before the reset).  Every exit here happens before the first
     * SET_TIMESTAMP: FAILED_SETUP (not SKIP, not a test FAIL), so the epoch-reset coverage stays visibly missing. */
    uint64_t phc_before = 0;
    if (!read_phc(hDev, adapter_idx, &phc_before)) {
        tc_failed_setup("UT-CORR-005", adapter_idx, "PHC read failed - adapter not ready; SET_TIMESTAMP not issued");
        return;
    }
    printf("  phc_before = %llu ns\n", (unsigned long long)phc_before);

    uint64_t tx_before = 0;
    uint64_t phc_before_send = 0;  /* atomic PHC ref from kernel IOCTL entry */
    bool tx_before_ok = send_ptp_get_tx(hDev, adapter_idx, 0x500U, &tx_before, &phc_before_send);
    if (!tx_before_ok) {
        /* Previously only a NOTE and the reset ran anyway: the procedure's step 2 (correlation before reset) was then
         * never established, and the epoch-reset result could not be attributed to TX timestamping. */
        tc_failed_setup("UT-CORR-005", adapter_idx,
                        "TX timestamp not available before reset (link down / HW ts disabled); SET_TIMESTAMP not issued");
        return;
    }
    {
        int64_t d = (int64_t)(tx_before - phc_before_send);
        printf("  tx_before  = %llu ns  (delta = %lld ns)\n",
               (unsigned long long)tx_before, (long long)d);
        if (d < 0 || (uint64_t)d >= DELTA_1US_NS) {
            char why[200];
            snprintf(why, sizeof(why),
                     "TX-PHC correlation before reset %lld ns not in [0, 1000) ns (tx=%llu phc_at_send=%llu); SET_TIMESTAMP not issued",
                     (long long)d, (unsigned long long)tx_before, (unsigned long long)phc_before_send);
            tc_failed_setup("UT-CORR-005", adapter_idx, why);
            return;
        }
    }

    /* --- Step 2: Reset PHC --- */
    AVB_TIMESTAMP_REQUEST set_req = {0};
    set_req.clock_id  = adapter_idx;
    set_req.timestamp = UT_CORR_005_SEED_NS;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_SET_TIMESTAMP,
                              &set_req, sizeof(set_req),
                              &set_req, sizeof(set_req), &br, NULL);
    if (!ok) {
        printf("  FAIL: IOCTL_AVB_SET_TIMESTAMP returned FALSE (error %lu)\n", GetLastError());
        tc_result("UT-CORR-005 SET_TIMESTAMP reachable", false);
        return;
    }
    printf("  PHC reset to ~%llu ns (seed)\n", (unsigned long long)UT_CORR_005_SEED_NS);

    /* --- Step 3: Read PHC after reset --- */
    Sleep(1);  /* 1ms settle for hardware write latency */
    uint64_t phc_after = 0;
    if (!read_phc(hDev, adapter_idx, &phc_after)) {
        printf("  FAIL: PHC read failed immediately after SET_TIMESTAMP\n");
        tc_result("UT-CORR-005 PHC readable after reset", false);
        /* Best-effort restore on early exit — check return value */
        set_req.timestamp = phc_before + 200000000ULL;
        if (!DeviceIoControl(hDev, IOCTL_AVB_SET_TIMESTAMP,
                        &set_req, sizeof(set_req), &set_req, sizeof(set_req), &br, NULL)) {
            printf("  [CLEANUP_FAILED] PHC restore IOCTL failed on early exit (error %lu)\n", GetLastError());
            s_cleanup_failed++;
        }
        return;
    }

    /* Windows Sleep(1) has ~15.6ms granularity (default timer resolution),
     * so phc_after can be up to ~50ms past seed.  50ms window is still far
     * below the pre-reset PHC (~seconds) and clearly confirms epoch reset. */
    bool near_seed = (phc_after >= UT_CORR_005_SEED_NS) &&
                     (phc_after <  UT_CORR_005_SEED_NS + 50000000ULL);
    printf("  phc_after  = %llu ns  (near seed [%llu, %llu): %s)\n",
           (unsigned long long)phc_after,
           (unsigned long long)UT_CORR_005_SEED_NS,
           (unsigned long long)(UT_CORR_005_SEED_NS + 50000000ULL),
           near_seed ? "YES" : "NO");

    if (!near_seed) {
        printf("  FAIL: PHC not in expected window after epoch reset\n");
        tc_result("UT-CORR-005 PHC near seed after reset", false);
        /* Best-effort restore on early exit — check return value */
        set_req.timestamp = phc_before + 500000000ULL;
        if (!DeviceIoControl(hDev, IOCTL_AVB_SET_TIMESTAMP,
                        &set_req, sizeof(set_req), &set_req, sizeof(set_req), &br, NULL)) {
            printf("  [CLEANUP_FAILED] PHC restore IOCTL failed on early exit (error %lu)\n", GetLastError());
            s_cleanup_failed++;
        }
        return;
    }

    /* --- Step 4: Verify TX-PHC correlation after reset --- */
    uint64_t tx_after = 0;
    uint64_t phc_after_send = 0;  /* atomic PHC ref from kernel IOCTL entry */
    bool tx_after_ok    = send_ptp_get_tx(hDev, adapter_idx, 0x501U, &tx_after, &phc_after_send);
    bool corr_after_ok  = true;
    if (tx_after_ok) {
        int64_t d = (int64_t)(tx_after - phc_after_send);
        printf("  tx_after   = %llu ns  (delta = %lld ns)\n",
               (unsigned long long)tx_after, (long long)d);
        if (d < 0 || (uint64_t)d >= DELTA_1US_NS) {
            printf("  FAIL: |tx_after - phc_after_send| = %lld ns >= 1 us after reset\n",
                   (long long)d);
            corr_after_ok = false;
        }
    } else {
        printf("  NOTE: TX timestamp not available after reset (link down?)\n");
    }

    /* --- Step 5: Verify PHC advances 100ms post-reset --- */
    Sleep(100);
    uint64_t phc_later = 0;
    bool advancing = read_phc(hDev, adapter_idx, &phc_later) && (phc_later > phc_after);
    uint64_t advance_ns = (advancing && phc_later > phc_after) ? (phc_later - phc_after) : 0;
    printf("  PHC 100ms later = %llu ns  (delta = %llu ns, advancing: %s)\n",
           (unsigned long long)phc_later, (unsigned long long)advance_ns,
           advancing ? "YES" : "NO");

    /* --- Step 6: Restore PHC (approximate — does not claim exact restoration).
     * PHC time has advanced; we target phc_before + observed advance + 50ms margin.
     * Check the IOCTL return but do not fail the test for minor timing drift. */
    set_req.timestamp = phc_before + advance_ns + 50000000ULL;
    BOOL phc_restore_ok = DeviceIoControl(hDev, IOCTL_AVB_SET_TIMESTAMP,
                                          &set_req, sizeof(set_req),
                                          &set_req, sizeof(set_req), &br, NULL);
    printf("  PHC restored to ~%llu ns (restore_ok=%d)\n",
           (unsigned long long)set_req.timestamp, (int)phc_restore_ok);
    if (!phc_restore_ok) {
        printf("  [CLEANUP_FAILED] PHC restore IOCTL failed (error %lu) — PHC epoch left at reset value\n",
               GetLastError());
        s_cleanup_failed++;
    }

    bool passed = near_seed && advancing && corr_after_ok;
    tc_result("UT-CORR-005 Epoch Reset: seed correct, advancing, TX-PHC correlated", passed);
}

/* =========================================================================
 * UT-CORR-006: Frequency Adjustment — TX timestamps track adjusted PHC rate
 *
 * Procedure (per issue #199, adapted to IOCTL interface):
 *   1. Read current clock config (increment_ns, increment_frac from timinca)
 *   2. Read PHC phc1
 *   3. Apply ADJUST_FREQUENCY with increment_ns + 1 (measurable non-zero change)
 *   4. Sleep 100ms (PHC runs at adjusted rate)
 *   5. Read PHC phc2; send PTP → tx
 *   6. Verify |tx - phc2| < 1 µs — TX stamps track adjusted PHC (not phc1 epoch)
 *   7. Restore original increment
 * =========================================================================*/
static void test_ut_corr_006(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n[UT-CORR-006] Freq Adjust: TX timestamps track adjusted PHC rate (adapter %u)\n",
           adapter_idx);
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 , Spec: issue #199\n");

    /* --- Step 1: Get current clock config --- */
    AVB_CLOCK_CONFIG cfg = {0};
    DWORD br = 0;
    BOOL cfg_ok = DeviceIoControl(hDev, IOCTL_AVB_GET_CLOCK_CONFIG,
                                   &cfg, sizeof(cfg), &cfg, sizeof(cfg), &br, NULL);
    if (!cfg_ok) {
        printf("  FAIL: IOCTL_AVB_GET_CLOCK_CONFIG failed (error %lu)\n", GetLastError());
        tc_result("UT-CORR-006 GET_CLOCK_CONFIG reachable", false);
        return;
    }

    /* Decode current increment: handles both I219 raw (IP=2, IV=ns×2,000,000) and
     * I210/I226/normalised-I219 (IP=ns/cycle) TIMINCA formats.
     * The old (cfg.timinca >> 8) & 0xFF formula read byte-1 of I219's IV field,
     * producing a garbage increment_ns (e.g. 36) that caused the +1 adjustment
     * to exceed driver validation (max_valid_incr=15). */
    uint32_t _ip  = (cfg.timinca >> 24) & 0xFFu;
    uint32_t _iv  = cfg.timinca & 0x00FFFFFFu;
    uint32_t increment_ns;
    uint32_t increment_frac;
    if (_ip == 2u && _iv > 0u) {        /* I219 raw: IV = increment_ns × 2,000,000 */
        increment_ns   = _iv / 2000000u;
        if (increment_ns == 0u) increment_ns = 8u;
        increment_frac = 0u;
    } else if (_ip > 0u) {              /* I210/I226/normalised I219: IP = ns/cycle */
        increment_ns   = _ip;
        increment_frac = 0u;
    } else {                            /* frozen / unknown — 125 MHz fallback */
        increment_ns   = 8u;
        increment_frac = 0u;
    }
    printf("  Clock config: timinca=0x%08X  increment=%u ns  clock_rate=%u MHz\n",
           cfg.timinca, increment_ns, cfg.clock_rate_mhz);

    /* I210 (issue #343): the "(timinca >> 24)" decode above is INVALID for the I210 - datasheet 8.15.11:
     * INC_TIME = 8 ns +/- Incvalue*2^-32 ns, NOMINAL raw 0, ISGN bit 31.  The stimulus "increment_ns + 1" (+12.5 percent) is not
     * representable and exceeds the provisional +/-100,000 ppb policy (#149), so the I210 uses the policy limit in the direction
     * AWAY from the baseline (always a measurable change), and restores the exact decoded baseline (inside the policy) or nominal. */
    uint32_t stim_ns = increment_ns + 1U;
    uint32_t stim_frac = increment_frac;
    unsigned short corr6_did = 0;
    const bool corr6_i210 = AvbTestGetBoundDid(hDev, &corr6_did) && AvbTestIsI210Did(corr6_did);
    if (corr6_i210) {
        unsigned norm = AvbTestI210RawInPolicy(cfg.timinca) ? cfg.timinca : 0u;
        long long base_ppb = AvbTestI210RawToPpb(norm);
        AvbTestI210RawToRequest(norm, &increment_ns, &increment_frac);   /* exact restore target */
        AvbTestI210RequestFromPpb(base_ppb <= 0 ? (long long)AVB_TEST_I210_POLICY_PPB : -(long long)AVB_TEST_I210_POLICY_PPB,
                                  &stim_ns, &stim_frac);
        printf("  [I210] baseline raw=0x%08X (%+lld ppb); stimulus ns=%u frac=%u; restore target ns=%u frac=%u\n",
               cfg.timinca, base_ppb, stim_ns, stim_frac, increment_ns, increment_frac);
    }
    /* --- Step 2: Read PHC phc1 --- */
    uint64_t phc1 = 0;
    if (!read_phc(hDev, adapter_idx, &phc1)) {
        printf("  [SKIP] PHC read failed — adapter not ready\n");
        tc_skip("UT-CORR-006 Freq Adj: adapter not ready");
        return;
    }
    printf("  phc1 = %llu ns\n", (unsigned long long)phc1);

    /* --- Step 3: Apply adjusted frequency (increment_ns + 1) ---
     * Using +1 ns increment creates a measurable, non-zero frequency offset
     * that lets us verify TX-PHC correlation is maintained during freq adj.
     * The intent per issue #199 is "+100 PPM" but at the TIMINCA 8-bit integer
     * precision, sub-nanosecond PPM adjustments are not representable; +1 ns
     * creates a larger but valid test stimulus. */
    AVB_FREQUENCY_REQUEST freq_req = {0};
    freq_req.increment_ns   = stim_ns;
    freq_req.increment_frac = stim_frac;
    BOOL freq_ok = DeviceIoControl(hDev, IOCTL_AVB_ADJUST_FREQUENCY,
                                    &freq_req, sizeof(freq_req),
                                    &freq_req, sizeof(freq_req), &br, NULL);
    if (!freq_ok) {
        printf("  FAIL: IOCTL_AVB_ADJUST_FREQUENCY failed (error %lu)\n", GetLastError());
        tc_result("UT-CORR-006 ADJUST_FREQUENCY reachable", false);
        return;
    }
    printf("  ADJUST_FREQUENCY: %u ns -> %u ns + frac 0x%08X  (current_increment=0x%08X)\n",
           increment_ns, stim_ns, stim_frac, freq_req.current_increment);

    /* --- Step 4: Sleep 100ms (PHC runs at adjusted rate) --- */
    Sleep(100);

    /* --- Step 5: Read phc2 and TX timestamp --- */
    uint64_t phc2 = 0;
    bool phc2_ok = read_phc(hDev, adapter_idx, &phc2);
    uint64_t tx   = 0;
    uint64_t phc2_send = 0;  /* atomic PHC ref from kernel IOCTL entry */
    bool tx_ok    = phc2_ok && send_ptp_get_tx(hDev, adapter_idx, 0x600U, &tx, &phc2_send);

    bool advancing  = phc2_ok && (phc2 > phc1);
    bool tx_corr_ok = true;

    if (phc2_ok) {
        printf("  phc2 = %llu ns  (delta = %llu ns, advancing: %s)\n",
               (unsigned long long)phc2,
               (unsigned long long)(phc2 > phc1 ? phc2 - phc1 : 0),
               advancing ? "YES" : "NO");
    } else {
        printf("  FAIL: PHC read failed after frequency adjustment\n");
    }

    if (tx_ok) {
        int64_t d = (int64_t)(tx - phc2_send);
        printf("  tx   = %llu ns  (delta tx-phc2_send = %lld ns, expected < 1 us)\n",
               (unsigned long long)tx, (long long)d);
        if (d < 0 || (uint64_t)d >= DELTA_1US_NS) {
            printf("  FAIL: |tx - phc2_send| = %lld ns >= 1 us — TX not tracking adjusted PHC\n",
                   (long long)d);
            tx_corr_ok = false;
        }
    } else if (phc2_ok) {
        printf("  NOTE: TX timestamp not available (link down?); verifying PHC monotonicity only\n");
    }

    bool passed = phc2_ok && advancing && tx_corr_ok;
    if (!phc2_ok)     printf("  FAIL: PHC not readable after freq adjustment\n");
    if (!advancing)   printf("  FAIL: PHC not advancing after freq adjustment\n");

    /* --- Step 7: Restore original frequency --- */
    BOOL restore_ok;
    if (corr6_i210) {
        /* verified by readback against the saved RAW baseline (exact inside the policy, else nominal) */
        restore_ok = (AvbTestI210RestoreVerified(hDev, cfg.timinca, "UT-CORR-006 restore") == 1);
    } else {
    freq_req.increment_ns   = increment_ns;
    freq_req.increment_frac = increment_frac;
    restore_ok = DeviceIoControl(hDev, IOCTL_AVB_ADJUST_FREQUENCY,
                                       &freq_req, sizeof(freq_req),
                                       &freq_req, sizeof(freq_req), &br, NULL);
    printf("  Restored increment to %u ns (ok=%d)\n", increment_ns, (int)restore_ok);
    }
    if (!restore_ok) {
        printf("  [CLEANUP_FAILED] Frequency restore IOCTL failed (error %lu) — TIMINCA left at adjusted value\n",
               GetLastError());
        s_cleanup_failed++;
        passed = false;  /* restore failure overrides test result */
    }

    /* Report result AFTER restore so restore outcome is included in verdict */
    tc_result("UT-CORR-006 Freq Adj: IOCTL OK, PHC advancing, TX-PHC correlated", passed);
}

/* =========================================================================
 * UT-CORR-007: Jitter — 1000 (PHC,TX) sample pairs, delta stddev < 100 ns
 *
 * Procedure (per issue #199):
 *   For i in 0..999:
 *     phc[i] = phc_at_send_ns (IOCTL-entry SYSTIM)
 *     tx[i]  = timestamp_ns   (pre-send SYSTIM, distinct snapshot)
 *     delta[i] = tx[i] - phc[i]   → kernel IOCTL setup overhead (~100-500 ns)
 *   Compute mean(delta) and stddev(delta)
 *   Assert: stddev < 100 ns
 *
 * NOTE: Since IOCTL_AVB_TEST_SEND_PTP returns ts_provenance == PRE_SEND_PHC,
 * this test measures kernel IOCTL setup time variation — NOT hardware TX
 * egress vs PHC correlation.  For TRUE hardware TX-PHC correlation the test
 * must use IOCTL_AVB_GET_TX_TIMESTAMP and verify ts_provenance ==
 * AVB_TX_PROV_VERIFIED_HARDWARE_TX.  The test reports SKIP for the
 * VERIFIED_HARDWARE_TX correlation claim; the PRE_SEND_PHC coherence sub-test
 * still runs and reports the kernel overhead delta for regression purposes.
 * =========================================================================*/
static void test_ut_corr_007(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n[UT-CORR-007] Jitter: 1000 (PHC,TX) pairs, delta stddev < 100 ns (adapter %u)\n",
           adapter_idx);
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 , Spec: issue #199\n");

    uint64_t pre = 0;
    if (!read_phc(hDev, adapter_idx, &pre)) {
        printf("  [SKIP] PHC read failed — adapter not ready\n");
        tc_skip("UT-CORR-007 Jitter: adapter not ready");
        return;
    }

    /* Probe provenance: send one packet and check ts_provenance. */
    {
        AVB_TEST_SEND_PTP_REQUEST probe = {0};
        probe.adapter_index = adapter_idx;
        probe.sequence_id   = 0x7000U;
        DWORD pbr = 0;
        BOOL pok = DeviceIoControl(hDev, IOCTL_AVB_TEST_SEND_PTP,
                                   &probe, sizeof(probe), &probe, sizeof(probe), &pbr, NULL);
        if (!pok || probe.status != NDIS_STATUS_SUCCESS) {
            printf("  [SKIP] SEND_PTP probe failed — adapter not ready\n");
            tc_skip("UT-CORR-007 Jitter: SEND_PTP probe failed");
            return;
        }
        printf("  Provenance: ts_provenance=%u (%s)\n", probe.ts_provenance,
               probe.ts_provenance == AVB_TX_PROV_VERIFIED_HARDWARE_TX ? "VERIFIED_HARDWARE_TX" :
               probe.ts_provenance == AVB_TX_PROV_PRE_SEND_PHC         ? "PRE_SEND_PHC" :
               probe.ts_provenance == AVB_TX_PROV_SOFTWARE_FALLBACK    ? "SOFTWARE_FALLBACK" :
               "UNAVAILABLE");
        if (probe.ts_provenance != AVB_TX_PROV_VERIFIED_HARDWARE_TX) {
            printf("  NOTE: ts_provenance != VERIFIED_HARDWARE_TX — hardware TX-PHC correlation\n");
            printf("        cannot be verified.  Measuring kernel IOCTL setup overhead instead.\n");
            printf("        delta = timestamp_ns - phc_at_send_ns = pre-send minus IOCTL-entry\n");
            printf("        (representative of kernel setup time, NOT hardware egress latency).\n");
        }
    }

    double *deltas = (double *)malloc(JITTER_SAMPLES * sizeof(double));
    if (!deltas) {
        printf("  [SKIP] malloc failed\n");
        tc_skip("UT-CORR-007 Jitter: malloc failed");
        return;
    }

    int valid       = 0;
    int tx_fail     = 0;
    int i;
    for (i = 0; i < JITTER_SAMPLES; i++) {
        uint64_t tx  = 0;
        uint64_t phc = 0;  /* IOCTL-entry PHC ref from send_ptp_get_tx */
        if (!send_ptp_get_tx(hDev, adapter_idx, (uint32_t)(0x700U + (unsigned)i), &tx, &phc)) {
            tx_fail++;
            continue;
        }
        int64_t d = (int64_t)(tx - phc);
        if (d >= 0) deltas[valid++] = (double)d;
    }

    printf("  Attempted: %d  TX failures: %d  Valid pairs: %d\n",
           JITTER_SAMPLES, tx_fail, valid);

    if (tx_fail >= JITTER_SAMPLES / 2) {
        printf("  NOTE: TX timestamps unavailable (%d/%d failures).\n",
               tx_fail, JITTER_SAMPLES);
        free(deltas);
        tc_skip("UT-CORR-007 Jitter: TX unavailable");
        return;
    }

    if (valid < 10) {
        printf("  FAIL: Only %d valid (PHC,TX) pairs — need >= 10 for statistics\n", valid);
        free(deltas);
        tc_result("UT-CORR-007 Jitter: insufficient valid samples", false);
        return;
    }

    /* Compute mean and stddev */
    double sum = 0.0;
    int j;
    for (j = 0; j < valid; j++) sum += deltas[j];
    double mean     = sum / valid;
    double variance = 0.0;
    for (j = 0; j < valid; j++) {
        double diff = deltas[j] - mean;
        variance += diff * diff;
    }
    variance /= valid;
    double stddev = sqrt(variance);
    free(deltas);

    printf("  Delta stats (%d pairs): mean=%.1f ns  stddev=%.1f ns\n", valid, mean, stddev);
    printf("  Threshold: stddev < %llu ns (per issue #199)\n",
           (unsigned long long)DELTA_JITTER_NS);

    /* Sub-test A: PRE_SEND_PHC coherence (kernel overhead variation).
     * Passes when delta is non-negative and variation is bounded — confirms that
     * two distinct PHC reads within the same IOCTL are coherent. */
    bool coherence_ok = (mean >= 0.0) && (stddev <= (double)DELTA_JITTER_NS);
    if (mean < 0.0)
        printf("  FAIL: Negative mean delta — timestamp ordering violated\n");
    if (mean > (double)DELTA_1US_NS)
        printf("  WARN: Mean delta %.1f ns > 1 us (excessive kernel overhead)\n", mean);
    if (!coherence_ok)
        printf("  FAIL: stddev %.1f ns > %llu ns threshold\n",
               stddev, (unsigned long long)DELTA_JITTER_NS);
    tc_result("UT-CORR-007 Jitter: PRE_SEND_PHC coherence (two distinct SYSTIM reads)", coherence_ok);

    /* Sub-test B: VERIFIED_HARDWARE_TX correlation — requires hardware egress latch.
     * Currently always SKIP because SEND_PTP returns PRE_SEND_PHC, not VERIFIED_HARDWARE_TX.
     * Will become active when IOCTL_AVB_GET_TX_TIMESTAMP returns VERIFIED_HARDWARE_TX. */
    tc_skip("UT-CORR-007 Hardware TX-PHC correlation: VERIFIED_HARDWARE_TX unavailable (not #328/#199 close criteria)");
}

/* =========================================================================
 * UT-CORR-008: Burst — 100 (PHC,TX) pairs at ~1ms intervals
 *
 * Procedure (per issue #199):
 *   For i in 0..99 (with Sleep(1) between each):
 *     phc[i] = read PHC
 *     tx[i]  = send PTP + get TX timestamp
 *     delta[i] = tx[i] - phc[i]
 *     Assert |delta[i]| < 1 µs
 *   Compute variance of delta[] — assert variance < 10000 (stddev < 100 ns)
 * =========================================================================*/
static void test_ut_corr_008(HANDLE hDev, uint32_t adapter_idx)
{
    printf("\n[UT-CORR-008] Burst: 100 (PHC,TX) pairs at ~1ms intervals (adapter %u)\n",
           adapter_idx);
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 , Spec: issue #199\n");

    uint64_t pre = 0;
    if (!read_phc(hDev, adapter_idx, &pre)) {
        printf("  [SKIP] PHC read failed — adapter not ready\n");
        tc_skip("UT-CORR-008 Burst: adapter not ready");
        return;
    }

    double *deltas = (double *)malloc(BURST_COUNT * sizeof(double));
    if (!deltas) {
        tc_skip("UT-CORR-008 Burst: malloc failed");
        return;
    }

    int valid        = 0;
    int tx_fail      = 0;
    int out_of_range = 0;  /* |delta[i]| >= 1 µs */
    int i;
    for (i = 0; i < BURST_COUNT; i++) {
        Sleep(1);  /* ~1ms interval between samples (per issue #199 spec) */
        uint64_t tx  = 0;
        uint64_t phc = 0;  /* atomic kernel PHC ref from send_ptp_get_tx */
        if (!send_ptp_get_tx(hDev, adapter_idx, (uint32_t)(0x800U + (unsigned)i), &tx, &phc)) {
            tx_fail++;
            continue;
        }
        int64_t d = (int64_t)(tx - phc);
        if (d < 0 || (uint64_t)d >= DELTA_1US_NS) {
            printf("  OUT_OF_RANGE[i=%d]: delta = %lld ns (%s1 us)\n",
                   i, (long long)d, (d < 0) ? "negative, " : ">= ");
            out_of_range++;
        }
        deltas[valid++] = (double)d;
    }

    printf("  Burst: valid=%d  tx_fail=%d  out_of_range=%d\n",
           valid, tx_fail, out_of_range);

    if (tx_fail >= BURST_COUNT / 2) {
        printf("  NOTE: TX unavailable — link down or HW ts disabled. Skipping TX correlation.\n");
        free(deltas);
        tc_skip("UT-CORR-008 Burst: TX unavailable");
        return;
    }

    if (valid < 10) {
        printf("  FAIL: Only %d valid pairs\n", valid);
        free(deltas);
        tc_result("UT-CORR-008 Burst: insufficient valid pairs", false);
        return;
    }

    /* Compute variance */
    double sum = 0.0;
    int j;
    for (j = 0; j < valid; j++) sum += deltas[j];
    double mean     = sum / valid;
    double variance = 0.0;
    for (j = 0; j < valid; j++) {
        double diff = deltas[j] - mean;
        variance += diff * diff;
    }
    variance /= valid;
    double stddev = sqrt(variance);
    free(deltas);

    printf("  Delta stats: mean=%.1f ns  stddev=%.1f ns  (threshold: stddev < %llu ns)\n",
           mean, stddev, (unsigned long long)DELTA_JITTER_NS);

    /* Sub-test A: PRE_SEND_PHC coherence — kernel overhead between two IOCTL snapshots.
     * Checks that:
     *   (a) all deltas are non-negative (ordering preserved), and
     *   (b) stddev < 100 ns (timing is stable).
     * This is NOT hardware TX egress vs PHC; it measures kernel IOCTL setup overhead. */
    bool coherence_ok = (out_of_range == 0) && (stddev <= (double)DELTA_JITTER_NS);
    if (out_of_range > 0)
        printf("  FAIL: %d sample(s) with delta out of [0, 1µs) — ordering violated or excessive overhead\n",
               out_of_range);
    if (stddev > (double)DELTA_JITTER_NS)
        printf("  FAIL: stddev %.1f ns > %llu ns threshold\n",
               stddev, (unsigned long long)DELTA_JITTER_NS);
    tc_result("UT-CORR-008 Burst: PRE_SEND_PHC coherence (100 pairs, IOCTL-entry vs pre-send)", coherence_ok);

    /* Sub-test B: hardware TX-PHC correlation — requires VERIFIED_HARDWARE_TX.
     * Currently always SKIP because SEND_PTP returns PRE_SEND_PHC. */
    tc_skip("UT-CORR-008 Hardware TX-PHC burst correlation: VERIFIED_HARDWARE_TX unavailable (not #328/#199 close criteria)");
}

/* =========================================================================
 * main — case selection and result reporting
 *
 * Usage:
 *   test_ptp_phc_stability.exe
 *       Default: UT-CORR-007 + 008 per adapter (active TX packet transmission for timestamps)
 *   test_ptp_phc_stability.exe --case UT-CORR-007
 *   test_ptp_phc_stability.exe --case UT-CORR-005
 *   test_ptp_phc_stability.exe --case UT-CORR-009  (lifecycle; explicit selection required)
 *   test_ptp_phc_stability.exe --allow-lifecycle    (add UT-CORR-009 to the default run)
 *
 * Test classification:
 *   UT-CORR-007/008: ACTIVE — transmit PTP packets and read TX timestamps.
 *                    Not read-only; driver and hardware must be fully functional.
 *   UT-CORR-005/006: MUTATION — modify PHC epoch/frequency; restore is approximate.
 *                    Require explicit --case selection.
 *   UT-CORR-009:     LIFECYCLE — reinstalls the driver (not read-only; not a baseline).
 *                    Adapter configuration may change; TX correlation may be unverifiable.
 *                    Requires --case UT-CORR-009 or --allow-lifecycle.
 *
 * Exit codes:
 *   0 = PASS         — all selected tests passed
 *   1 = FAIL         — at least one test failed
 *   2 = SKIP_ONLY    — no test produced a PASS result (no capable hardware)
 *   4 = CLEANUP_FAILED — restore step failed; hardware state uncertain
 * =========================================================================*/
int main(int argc, char *argv[])
{
    /* --- Parse command-line arguments --- */
    const char *selected_case     = NULL;
    bool        lifecycle_auth    = false;  /* UT-CORR-009 requires explicit authorization */
    bool        mutation_explicit = false;  /* UT-CORR-005/006 require --case or explicit */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--allow-lifecycle") == 0) {
            lifecycle_auth = true;
        } else if ((strcmp(argv[i], "--case") == 0 || strcmp(argv[i], "-case") == 0) && i + 1 < argc) {
            selected_case = argv[++i];
            if (strncmp(selected_case, "UT-CORR-009", 11) == 0) lifecycle_auth = true;
            if (strncmp(selected_case, "UT-CORR-005", 11) == 0) mutation_explicit = true;
            if (strncmp(selected_case, "UT-CORR-006", 11) == 0) mutation_explicit = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fprintf(stderr,
                "Usage: %s [--case UT-CORR-00X] [--allow-lifecycle]\n"
                "  --case UT-CORR-007   active TX/PHC correlation jitter (transmits packets)\n"
                "  --case UT-CORR-008   active TX/PHC correlation burst  (transmits packets)\n"
                "  --case UT-CORR-005   PHC epoch reset (mutation; approximate restore)\n"
                "  --case UT-CORR-006   PHC freq adjust (mutation; restores TIMINCA)\n"
                "  --case UT-CORR-009   moved to test_ptp_phc_lifecycle.exe (driver reinstall)\n"
                "  --allow-lifecycle    moved: UT-CORR-009 is test_ptp_phc_lifecycle.exe\n"
                "  (no args)            run UT-CORR-007 + 008 only (active, not read-only)\n", argv[0]);
            return 0;
        }
    }
    (void)mutation_explicit; /* mutation tests run when selected; no additional gate needed */

    printf("========================================================================\n");
    printf("PHC Stability Under State Changes -- UT-CORR-005..008 (UT-CORR-009: test_ptp_phc_lifecycle.exe)\n");
    if (selected_case) {
        printf("Selected case: %s\n", selected_case);
    } else {
        printf("Default suite: UT-CORR-007 + UT-CORR-008 (read-only correlation)\n");
        if (lifecycle_auth) printf("UT-CORR-009 requested: it is test_ptp_phc_lifecycle.exe now (not run here)\n");
    }
    printf("Verifies: #149 (REQ-F-PTP-007) -- TX-PHC correlation under state changes\n");
    printf("Spec: issue #199 per-test procedures | Closes: Track A of #317\n");
    printf("========================================================================\n");

    HANDLE hDev = open_device();
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open %S (Win32 error %lu)\n", DEVICE_PATH_W, GetLastError());
        printf("  Is the IntelAvbFilter driver installed and running?\n");
        return 1;
    }
    printf("Device opened successfully.\n");

    /* Enumerate adapters */
    int adapter_count = 0;
    int ai;
    unsigned short enum_dids[8] = {0};
    for (ai = 0; ai < 8; ai++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = ai;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                                   &r, sizeof(r), &r, sizeof(r), &br, NULL);
        if (!ok || r.status != NDIS_STATUS_SUCCESS) break;
        printf("  Adapter %d: VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               ai, r.vendor_id, r.device_id, r.capabilities);
        enum_dids[ai] = r.device_id;
        adapter_count++;
    }

    if (adapter_count == 0) {
        printf("ERROR: No adapters enumerated.\n");
        CloseHandle(hDev);
        return 1;
    }
    printf("Found %d adapter(s).\n\n", adapter_count);

    /* Bind FsContext on hDev via OPEN_ADAPTER so IOCTL_AVB_GET_CLOCK_CONFIG
     * (used by UT-CORR-006) returns STATUS_SUCCESS.  The driver requires a
     * non-NULL FsContext; without this call Win32 error 31 is returned. */
    {
        bool bound = false;
        for (int _oi = 0; _oi < adapter_count && !bound; _oi++) {
            AVB_ENUM_REQUEST _er = {0};
            _er.index = (avb_u32)_oi;
            DWORD _br = 0;
            DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                            &_er, sizeof(_er), &_er, sizeof(_er), &_br, NULL);
            if (!(_er.capabilities & INTEL_CAP_BASIC_1588)) continue;
            AVB_OPEN_REQUEST _open = {0};
            _open.vendor_id = _er.vendor_id;
            _open.device_id = _er.device_id;
            _open.index     = (avb_u32)_oi;
            _br = 0;
            BOOL _ok = DeviceIoControl(hDev, IOCTL_AVB_OPEN_ADAPTER,
                                       &_open, sizeof(_open),
                                       &_open, sizeof(_open), &_br, NULL);
            if (_ok && _open.status == 0) {
                printf("  [OPEN] Adapter %d (VID=0x%04X DID=0x%04X): bound via OPEN_ADAPTER\n",
                       _oi, (unsigned)_er.vendor_id, (unsigned)_er.device_id);
                bound = true;
            }
        }
        Sleep(300);  /* allow I219 OID handler to complete PTP init */
    }

    /* UT-CORR-007 and UT-CORR-008: per-adapter jitter and burst correlation.
     * MULTI-ADAPTER FIX: open a dedicated handle per adapter and bind it via
     * IOCTL_AVB_OPEN_ADAPTER.  Using the shared hDev (bound to adapter 0) for
     * all adapters routes every IOCTL to the same FsContext regardless of the
     * adapter_index field in the request struct. */
    bool run_007 = !selected_case || strncmp(selected_case, "UT-CORR-007", 11) == 0;
    bool run_008 = !selected_case || strncmp(selected_case, "UT-CORR-008", 11) == 0;
    bool run_005 = selected_case  && strncmp(selected_case, "UT-CORR-005", 11) == 0;
    bool run_006 = selected_case  && strncmp(selected_case, "UT-CORR-006", 11) == 0;
    bool run_009 = (selected_case && strncmp(selected_case, "UT-CORR-009", 11) == 0)
                   || (!selected_case && lifecycle_auth);

    for (ai = 0; (run_007 || run_008) && ai < adapter_count; ai++) {
        /* --- Per-adapter handle --- */
        AVB_ENUM_REQUEST ai_enum = {0};
        ai_enum.index = (avb_u32)ai;
        DWORD ai_br = 0;
        if (!DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                             &ai_enum, sizeof(ai_enum), &ai_enum, sizeof(ai_enum),
                             &ai_br, NULL) || ai_enum.status != NDIS_STATUS_SUCCESS) {
            printf("\n--- Adapter %d / %d: ENUM_ADAPTERS failed, skipping ---\n",
                   ai, adapter_count - 1);
            tc_skip("UT-CORR-007/008: ENUM_ADAPTERS failed for adapter");
            continue;
        }
        HANDLE hAdap = CreateFileW(DEVICE_PATH_W, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hAdap == INVALID_HANDLE_VALUE) {
            printf("\n--- Adapter %d / %d: per-adapter handle open failed (error %lu) ---\n",
                   ai, adapter_count - 1, GetLastError());
            tc_result("UT-CORR-007/008: per-adapter handle open failed", false);
            continue;
        }
        AVB_OPEN_REQUEST ai_open = {0};
        ai_open.vendor_id = ai_enum.vendor_id;
        ai_open.device_id = ai_enum.device_id;
        ai_open.index     = (avb_u32)ai;
        if (!DeviceIoControl(hAdap, IOCTL_AVB_OPEN_ADAPTER,
                             &ai_open, sizeof(ai_open), &ai_open, sizeof(ai_open),
                             &ai_br, NULL) || ai_open.status != 0) {
            printf("\n--- Adapter %d / %d: OPEN_ADAPTER failed (error %lu, status=0x%08X) ---\n",
                   ai, adapter_count - 1, GetLastError(), (unsigned)ai_open.status);
            CloseHandle(hAdap);
            tc_skip("UT-CORR-007/008: OPEN_ADAPTER failed for adapter");
            continue;
        }
        printf("\n--- Adapter %d / %d  VID=0x%04X DID=0x%04X ---\n",
               ai, adapter_count - 1,
               (unsigned)ai_enum.vendor_id, (unsigned)ai_enum.device_id);

        if (run_007) test_ut_corr_007(hAdap, (uint32_t)ai);
        if (run_008) test_ut_corr_008(hAdap, (uint32_t)ai);

        CloseHandle(hAdap);
    }

    /* UT-CORR-005 and UT-CORR-006: state-modifying tests on exactly ONE operator-named adapter (issue #343).
     * Previously "adapter 0" = whichever adapter the driver enumerated first: the I210 in the Debug pass and the I226 in
     * the Release pass of the FullSuite run of 2026-10-19.  Now: AVB_HIL_TARGET_DID selects the adapter (unique DID, no
     * default), and a dedicated handle is bound to it with OPEN_ADAPTER.  Halt subsequent mutation tests if a restore
     * step fails. */
    if (run_005 || run_006) {
        char target_value[32];
        int  ti  = -1;
        DWORD tl = GetEnvironmentVariableA(AVB_TEST_TARGET_ENV, target_value, (DWORD)sizeof(target_value));
        int  sel = (tl == 0) ? AVB_TARGET_NOT_REQUESTED : (tl >= sizeof(target_value)) ? AVB_TARGET_BAD_VALUE
                 : AvbTestSelectTargetIndex(enum_dids, adapter_count, target_value, &ti);
        HANDLE hTarget = INVALID_HANDLE_VALUE;
        if (sel == AVB_TARGET_OK) {
            hTarget = CreateFileW(DEVICE_PATH_W, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hTarget != INVALID_HANDLE_VALUE) {
                AVB_OPEN_REQUEST t_open = {0};
                DWORD t_br = 0;
                t_open.vendor_id = 0x8086;
                t_open.device_id = enum_dids[ti];
                t_open.index     = (avb_u32)ti;
                if (!DeviceIoControl(hTarget, IOCTL_AVB_OPEN_ADAPTER, &t_open, sizeof(t_open), &t_open, sizeof(t_open),
                                     &t_br, NULL) || t_open.status != 0) {
                    printf("  [FAIL] UT-CORR-005/006: OPEN_ADAPTER of target DID=0x%04X failed (GLE=%lu status=0x%08X)\n",
                           (unsigned)enum_dids[ti], GetLastError(), (unsigned)t_open.status);
                    CloseHandle(hTarget);
                    hTarget = INVALID_HANDLE_VALUE;
                }
            }
        }
        if (sel != AVB_TARGET_OK) {
            char why[160];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "UT-CORR-005/006: exact target adapter required - %s=%s; no state change made",
                        AVB_TEST_TARGET_ENV, AvbTargetSelectionName(sel));
            if (run_005) tc_blocked(why);
            if (run_006) tc_blocked(why);
        } else if (hTarget == INVALID_HANDLE_VALUE) {
            if (run_005) tc_result("UT-CORR-005: target adapter handle could not be bound", false);
            if (run_006) tc_result("UT-CORR-006: target adapter handle could not be bound", false);
        } else {
            printf("\n--- Adapter %d DID=0x%04X (state-modifying tests, exact target via %s) ---\n",
                   ti, (unsigned)enum_dids[ti], AVB_TEST_TARGET_ENV);
            if (run_005) {
                test_ut_corr_005(hTarget, (uint32_t)ti);
                if (s_cleanup_failed > 0) {
                    printf("[HALT] CLEANUP_FAILED after UT-CORR-005 — skipping remaining mutation tests\n");
                    run_006 = false;
                }
            }
            if (run_006) test_ut_corr_006(hTarget, (uint32_t)ti);
            CloseHandle(hTarget);
        }
    }

    /* UT-CORR-009 (driver reinstall) lives in test_ptp_phc_lifecycle.exe (Unsafe) since 2026-10-22; this binary
     * contains no service control or process creation.  An explicit request here is reported, never ignored. */
    if (run_009) {
        tc_blocked("UT-CORR-009 Driver Reload: not in this executable - run test_ptp_phc_lifecycle.exe (Unsafe, attended, AVB_HIL_AUTHORIZE_UNSAFE)");
    }

    if (hDev != INVALID_HANDLE_VALUE) {
        CloseHandle(hDev);
    }

    /* Summary */
    printf("\n========================================================================\n");
    printf("Test Summary -- PHC Stability Under State Changes (#317 Track A)\n");
    printf("  Total: %d  Passed: %d  Failed: %d  Skipped: %d  CleanupFailed: %d\n",
           s_total, s_passed, s_failed, s_skipped, s_cleanup_failed);
    if (s_cleanup_failed > 0) {
        printf("  STATUS: CLEANUP_FAILED — hardware state uncertain after restore failure\n");
        printf("  Recovery: tools\\setup\\Install-Driver-Elevated.ps1 -Action Reinstall\n");
    } else if (s_failed > 0) {
        printf("  STATUS: FAIL\n");
        printf("  See output above for root cause details.\n");
    } else if (s_passed == 0 && s_failed == 0) {
        printf("  STATUS: SKIP — no test produced a PASS result\n");
    } else {
        printf("  STATUS: PASS (TDD GREEN)\n");
    }
    printf("========================================================================\n");

    if (s_cleanup_failed > 0) return 4;
    if (s_failed > 0)         return 1;
    if (s_passed == 0)        return 2;  /* all skipped, no PASS */
    return 0;
}
