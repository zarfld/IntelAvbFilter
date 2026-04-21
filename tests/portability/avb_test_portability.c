/**
 * @file avb_test_portability.c
 * @brief QA-SC-PORT-001: Cross-adapter hardware portability test
 *
 * Verifies: #114 (QA-SC-PORT-001: Hardware Portability Across Intel Controllers)
 * Traces to: #84 (REQ-NF-PORTABILITY-001)
 * Test Type: Integration / Portability
 * Priority: P1 (Important)
 *
 * STRATEGY — Adapter-agnostic, capability-driven testing:
 *
 *   This test NEVER pre-filters by PCI Device ID.  For every Intel adapter
 *   found by the driver it:
 *
 *   1. Reads the capability bitmask from IOCTL_AVB_ENUM_ADAPTERS.
 *   2. For each capability / IOCTL pair:
 *        • Calls the IOCTL.
 *        • If capability IS set → driver MUST NOT return ERROR_NOT_SUPPORTED.
 *          Any other result (success OR config-error) is accepted as PASS
 *          because the gate did not wrongly block the feature.
 *        • If capability is NOT set → driver MUST return ERROR_NOT_SUPPORTED.
 *          Any other result (success OR other error) is a FAIL because the
 *          driver allowed access to a feature it doesn't have.
 *   3. Prints a coverage matrix so incomplete hardware rigs don't hide gaps.
 *
 * MIXED-RIG DESIGN: No single machine is expected to have all 5 adapter types
 * simultaneously.  This test runs the subset of adapters that ARE present and
 * reports the coverage honestly.  SKIP results (capability absent + driver
 * correctly returns NOT_SUPPORTED) count as PASS for the gate test.
 *
 * SSOT: ../../../include/avb_ioctl.h  (all IOCTL definitions)
 *       ../../../external/intel_avb/lib/intel.h (INTEL_CAP_* flags)
 *
 * MULTI-ADAPTER: tests all enumerated adapters
 */

#include "../common/avb_test_common.h"
#include <stdlib.h>

/* =========================================================
 * INTEL_CAP_* flags (from external/intel_avb/lib/intel.h)
 * Re-declared here so the test is self-contained for CI.
 * ========================================================= */
#ifndef INTEL_CAP_BASIC_1588
#define INTEL_CAP_BASIC_1588    (1 << 0)   /* Basic IEEE 1588 timestamping */
#define INTEL_CAP_ENHANCED_TS   (1 << 1)   /* Enhanced TS: TSYNCRXCTL/TSYNCTXCTL */
#define INTEL_CAP_TSN_TAS       (1 << 2)   /* Time-Aware Shaper (802.1Qbv) */
#define INTEL_CAP_TSN_FP        (1 << 3)   /* Frame Preemption (802.1Qbu) */
#define INTEL_CAP_PCIE_PTM      (1 << 4)   /* PCIe Precision Time Measurement */
#define INTEL_CAP_2_5G          (1 << 5)   /* 2.5 Gbps speed support */
#define INTEL_CAP_MDIO          (1 << 6)   /* MDIO PHY register access */
#define INTEL_CAP_MMIO          (1 << 7)   /* Memory-mapped I/O (BAR0 mapped) */
#define INTEL_CAP_EEE           (1 << 8)   /* Energy Efficient Ethernet */
#endif

/* =========================================================
 * Per-adapter per-capability test result
 * ========================================================= */
typedef enum {
    TC_PASS = 0,   /* capability gate and IOCTL behaved as expected */
    TC_FAIL,       /* capability gate or IOCTL mismatch */
    TC_NA          /* capability not tested (no adapter of that type present) */
} TcResult;

/* Track which capability gates were verified per adapter */
typedef struct AdapterCoverage {
    avb_u16   device_id;
    avb_u32   capabilities;
    TcResult  tc_001;   /* Capability report sanity */
    TcResult  tc_002;   /* PTP clock read (BASIC_1588) */
    TcResult  tc_003;   /* HW timestamping enable (ENHANCED_TS) */
    TcResult  tc_004;   /* TAS capability gate (TSN_TAS) */
    TcResult  tc_005;   /* FP  capability gate (TSN_FP)  */
    TcResult  tc_006;   /* MDIO register read  (MDIO)    */
    TcResult  tc_007;   /* PTM capability gate (PCIE_PTM) */
} AdapterCoverage;

static AvbTestStats g_stats;
static AdapterCoverage g_cov[AVB_MAX_ADAPTERS];

/* =========================================================
 * Helper: pretty-print a TcResult
 * ========================================================= */
static const char *TcStr(TcResult r)
{
    switch (r) {
        case TC_PASS: return "PASS";
        case TC_FAIL: return "FAIL";
        default:      return "----";
    }
}

/* =========================================================
 * Helper: map Win32 GetLastError() to a readable shortname
 * ========================================================= */
static const char *ErrName(DWORD e)
{
    switch (e) {
        case ERROR_NOT_SUPPORTED:   return "NOT_SUPPORTED";
        case ERROR_INVALID_FUNCTION:return "INVALID_FUNCTION";
        case ERROR_ACCESS_DENIED:   return "ACCESS_DENIED";
        case ERROR_INVALID_PARAMETER: return "INVALID_PARAMETER";
        case ERROR_IO_DEVICE:       return "IO_DEVICE";
        default:                    return "OTHER";
    }
}

/* =========================================================
 * TC-PORT-001: Capability Report Sanity
 *
 * Verifies the enumerated adapter has:
 *   - VID = 0x8086 (Intel)
 *   - Non-zero capabilities bitmask
 *   - At least INTEL_CAP_MMIO set (BAR0 must be accessible)
 *   - At least INTEL_CAP_BASIC_1588 set (all supported Intel NICs have PTP)
 * ========================================================= */
static TcResult tc_port_001_capability_sanity(
        UINT32 idx, const AvbAdapterInfo *info)
{
    printf("\n  --- TC-PORT-001/adapter:%u (DID=0x%04X %s) ---\n",
           idx, info->device_id, info->device_name);
    printf("      Capabilities: 0x%08X", info->capabilities);

    /* Decode cap bits inline */
    char capbuf[128];
    AvbCapabilityString(info->capabilities, capbuf, sizeof(capbuf));
    printf(" [%s]\n", capbuf);

    int ok = 1;

    if (info->vendor_id != 0x8086) {
        printf("  [FAIL] TC-PORT-001/adapter:%u: VID=0x%04X, expected 0x8086\n",
               idx, info->vendor_id);
        g_stats.failed++; g_stats.total++;
        ok = 0;
    }

    if (info->capabilities == 0) {
        printf("  [FAIL] TC-PORT-001/adapter:%u: capabilities bitmask is 0\n", idx);
        g_stats.failed++; g_stats.total++;
        ok = 0;
    }

    if (!(info->capabilities & INTEL_CAP_MMIO)) {
        printf("  [FAIL] TC-PORT-001/adapter:%u: INTEL_CAP_MMIO not set "
               "(BAR0 must be accessible for any supported adapter)\n", idx);
        g_stats.failed++; g_stats.total++;
        ok = 0;
    }

    if (!(info->capabilities & INTEL_CAP_BASIC_1588)) {
        /* This would be unusual — all supported Intel NICs have PTP */
        printf("  [FAIL] TC-PORT-001/adapter:%u: INTEL_CAP_BASIC_1588 not set "
               "(all supported Intel adapters must have PTP)\n", idx);
        g_stats.failed++; g_stats.total++;
        ok = 0;
    }

    if (ok) {
        printf("  [PASS] TC-PORT-001/adapter:%u: VID=0x%04X DID=0x%04X caps OK\n",
               idx, info->vendor_id, info->device_id);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
    return TC_FAIL;
}

/* =========================================================
 * TC-PORT-002: PTP Clock Read (INTEL_CAP_BASIC_1588)
 *
 * If BASIC_1588 IS set:
 *   IOCTL_AVB_GET_TIMESTAMP must succeed and return a non-zero timestamp.
 * If BASIC_1588 is NOT set:
 *   IOCTL_AVB_GET_TIMESTAMP must return ERROR_NOT_SUPPORTED.
 * ========================================================= */
static TcResult tc_port_002_ptp_clock_read(
        UINT32 idx, const AvbAdapterInfo *info, HANDLE h)
{
    BOOL has_cap = (info->capabilities & INTEL_CAP_BASIC_1588) != 0;

    printf("\n  --- TC-PORT-002/adapter:%u (DID=0x%04X) BASIC_1588 gate ---\n",
           idx, info->device_id);
    printf("      Cap BASIC_1588: %s\n", has_cap ? "SET" : "NOT SET");

    AVB_TIMESTAMP_REQUEST ts;
    ZeroMemory(&ts, sizeof(ts));
    DWORD bytes = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                              &ts, sizeof(ts), &ts, sizeof(ts), &bytes, NULL);
    DWORD err = GetLastError();

    if (has_cap) {
        /* Capability present → IOCTL must succeed */
        if (!ok || ts.status != 0) {
            printf("  [FAIL] TC-PORT-002/adapter:%u: cap BASIC_1588 set but "
                   "GET_TIMESTAMP failed (ok=%d err=%s status=0x%08X)\n",
                   idx, ok, ErrName(err), ts.status);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        if (ts.timestamp == 0) {
            printf("  [FAIL] TC-PORT-002/adapter:%u: GET_TIMESTAMP returned 0 "
                   "(clock not running)\n", idx);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-002/adapter:%u: GET_TIMESTAMP = %llu ns "
               "(clock running)\n", idx, (unsigned long long)ts.timestamp);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    } else {
        /* Capability absent → IOCTL must return NOT_SUPPORTED */
        if (ok || err != ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-002/adapter:%u: cap BASIC_1588 NOT set "
                   "but GET_TIMESTAMP did not return NOT_SUPPORTED "
                   "(ok=%d err=%s)\n", idx, ok, ErrName(err));
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-002/adapter:%u: cap absent, driver correctly "
               "returned NOT_SUPPORTED\n", idx);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
}

/* =========================================================
 * TC-PORT-003: HW Timestamping Enable (INTEL_CAP_ENHANCED_TS)
 *
 * If ENHANCED_TS IS set:
 *   IOCTL_AVB_SET_HW_TIMESTAMPING with enable=1 must NOT return NOT_SUPPORTED.
 *   (It may return success or another error for other reasons.)
 * If ENHANCED_TS is NOT set:
 *   IOCTL_AVB_SET_HW_TIMESTAMPING must return ERROR_NOT_SUPPORTED.
 * ========================================================= */
static TcResult tc_port_003_enhanced_ts(
        UINT32 idx, const AvbAdapterInfo *info, HANDLE h)
{
    BOOL has_cap = (info->capabilities & INTEL_CAP_ENHANCED_TS) != 0;

    printf("\n  --- TC-PORT-003/adapter:%u (DID=0x%04X) ENHANCED_TS gate ---\n",
           idx, info->device_id);
    printf("      Cap ENHANCED_TS: %s\n", has_cap ? "SET" : "NOT SET");

    AVB_HW_TIMESTAMPING_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.enable             = 1;
    req.timer_mask         = 1;   /* SYSTIM0 */
    req.enable_target_time = 0;
    req.enable_aux_ts      = 0;
    DWORD bytes = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_SET_HW_TIMESTAMPING,
                              &req, sizeof(req), &req, sizeof(req), &bytes, NULL);
    DWORD err = GetLastError();

    if (has_cap) {
        if (!ok && err == ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-003/adapter:%u: cap ENHANCED_TS set but "
                   "SET_HW_TIMESTAMPING returned NOT_SUPPORTED (driver wrongly "
                   "gating supported feature)\n", idx);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        if (ok && req.status == 0) {
            printf("  [PASS] TC-PORT-003/adapter:%u: SET_HW_TIMESTAMPING succeeded "
                   "(TSAUXC: 0x%08X -> 0x%08X)\n",
                   idx, req.previous_tsauxc, req.current_tsauxc);
        } else {
            printf("  [PASS] TC-PORT-003/adapter:%u: ENHANCED_TS set, driver did NOT "
                   "return NOT_SUPPORTED (ok=%d err=%s status=0x%08X)\n",
                   idx, ok, ErrName(err), req.status);
        }
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    } else {
        if (ok || err != ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-003/adapter:%u: cap ENHANCED_TS NOT set "
                   "but SET_HW_TIMESTAMPING did not return NOT_SUPPORTED "
                   "(ok=%d err=%s)\n", idx, ok, ErrName(err));
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-003/adapter:%u: cap absent, driver correctly "
               "returned NOT_SUPPORTED\n", idx);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
}

/* =========================================================
 * TC-PORT-004: TAS Capability Gate (INTEL_CAP_TSN_TAS)
 *
 * If TSN_TAS IS set:
 *   IOCTL_AVB_SETUP_TAS must NOT return ERROR_NOT_SUPPORTED.
 *   (May return success or another config-validation error.)
 * If TSN_TAS is NOT set:
 *   IOCTL_AVB_SETUP_TAS must return ERROR_NOT_SUPPORTED.
 *
 * NOTE: We use a minimal config (1ms cycle, all gates open) to avoid
 * division-by-zero in the driver, but we do not require the config to
 * be hardware-accepted — we only test the capability gate.
 * ========================================================= */
static TcResult tc_port_004_tas_gate(
        UINT32 idx, const AvbAdapterInfo *info, HANDLE h)
{
    BOOL has_cap = (info->capabilities & INTEL_CAP_TSN_TAS) != 0;

    printf("\n  --- TC-PORT-004/adapter:%u (DID=0x%04X) TSN_TAS gate ---\n",
           idx, info->device_id);
    printf("      Cap TSN_TAS: %s\n", has_cap ? "SET" : "NOT SET");

    AVB_TAS_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    /* Minimal non-degenerate config: 1ms cycle, all 8 gate states open */
    req.config.cycle_time_ns = 1000000u;  /* 1 ms */
    memset(req.config.gate_states, 0xFF, sizeof(req.config.gate_states));
    DWORD bytes = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_SETUP_TAS,
                              &req, sizeof(req), &req, sizeof(req), &bytes, NULL);
    DWORD err = GetLastError();

    if (has_cap) {
        if (!ok && err == ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-004/adapter:%u: cap TSN_TAS set but "
                   "SETUP_TAS returned NOT_SUPPORTED (driver wrongly blocking "
                   "supported feature)\n", idx);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        if (ok) {
            printf("  [PASS] TC-PORT-004/adapter:%u: TSN_TAS set, SETUP_TAS "
                   "accepted config (TAS functional)\n", idx);
        } else {
            printf("  [PASS] TC-PORT-004/adapter:%u: TSN_TAS set, driver did NOT "
                   "return NOT_SUPPORTED (ok=%d err=%s status=0x%08X — "
                   "config rejected but gate is correct)\n",
                   idx, ok, ErrName(err), req.status);
        }
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    } else {
        if (ok || err != ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-004/adapter:%u: cap TSN_TAS NOT set "
                   "but SETUP_TAS did not return NOT_SUPPORTED "
                   "(ok=%d err=%s)\n", idx, ok, ErrName(err));
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-004/adapter:%u: TSN_TAS absent, driver "
               "correctly returned NOT_SUPPORTED\n", idx);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
}

/* =========================================================
 * TC-PORT-005: Frame Preemption Gate (INTEL_CAP_TSN_FP)
 *
 * Same gate logic as TC-PORT-004 but for IOCTL_AVB_SETUP_FP.
 * ========================================================= */
static TcResult tc_port_005_fp_gate(
        UINT32 idx, const AvbAdapterInfo *info, HANDLE h)
{
    BOOL has_cap = (info->capabilities & INTEL_CAP_TSN_FP) != 0;

    printf("\n  --- TC-PORT-005/adapter:%u (DID=0x%04X) TSN_FP gate ---\n",
           idx, info->device_id);
    printf("      Cap TSN_FP: %s\n", has_cap ? "SET" : "NOT SET");

    AVB_FP_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.config.preemptable_queues = 0;  /* safest: no queues preempted */
    req.config.min_fragment_size  = 60; /* IEEE 802.3 minimum frame */
    DWORD bytes = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_SETUP_FP,
                              &req, sizeof(req), &req, sizeof(req), &bytes, NULL);
    DWORD err = GetLastError();

    if (has_cap) {
        if (!ok && err == ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-005/adapter:%u: cap TSN_FP set but "
                   "SETUP_FP returned NOT_SUPPORTED\n", idx);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-005/adapter:%u: TSN_FP set, SETUP_FP did NOT "
               "return NOT_SUPPORTED (ok=%d err=%s status=0x%08X)\n",
               idx, ok, ErrName(err), req.status);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    } else {
        if (ok || err != ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-005/adapter:%u: cap TSN_FP NOT set "
                   "but SETUP_FP did not return NOT_SUPPORTED "
                   "(ok=%d err=%s)\n", idx, ok, ErrName(err));
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-005/adapter:%u: TSN_FP absent, driver "
               "correctly returned NOT_SUPPORTED\n", idx);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
}

/* =========================================================
 * TC-PORT-006: MDIO Register Read (INTEL_CAP_MDIO)
 *
 * If MDIO IS set:
 *   IOCTL_AVB_MDIO_READ on PHY reg 0 (Control) must succeed with
 *   a value that is not 0xFFFF (bus non-responsive).
 * If MDIO is NOT set:
 *   IOCTL_AVB_MDIO_READ must return ERROR_NOT_SUPPORTED.
 * ========================================================= */
static TcResult tc_port_006_mdio_read(
        UINT32 idx, const AvbAdapterInfo *info, HANDLE h)
{
    BOOL has_cap = (info->capabilities & INTEL_CAP_MDIO) != 0;

    printf("\n  --- TC-PORT-006/adapter:%u (DID=0x%04X) MDIO gate ---\n",
           idx, info->device_id);
    printf("      Cap MDIO: %s\n", has_cap ? "SET" : "NOT SET");

    AVB_MDIO_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.page  = 0;   /* Clause 22 page 0 */
    req.reg   = 0;   /* PHY Control register */
    DWORD bytes = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_MDIO_READ,
                              &req, sizeof(req), &req, sizeof(req), &bytes, NULL);
    DWORD err = GetLastError();

    if (has_cap) {
        if (!ok || req.status != 0) {
            if (err == ERROR_NOT_SUPPORTED) {
                printf("  [FAIL] TC-PORT-006/adapter:%u: cap MDIO set but "
                       "MDIO_READ returned NOT_SUPPORTED\n", idx);
            } else {
                printf("  [FAIL] TC-PORT-006/adapter:%u: cap MDIO set but "
                       "MDIO_READ failed (ok=%d err=%s status=0x%08X)\n",
                       idx, ok, ErrName(err), req.status);
            }
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        if (req.value == 0xFFFF) {
            printf("  [FAIL] TC-PORT-006/adapter:%u: MDIO_READ returned 0xFFFF "
                   "(PHY bus non-responsive)\n", idx);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-006/adapter:%u: MDIO_READ PHY ctrl reg = 0x%04X "
               "(MDIO bus functional)\n", idx, req.value);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    } else {
        if (ok || err != ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-006/adapter:%u: cap MDIO NOT set "
                   "but MDIO_READ did not return NOT_SUPPORTED "
                   "(ok=%d err=%s)\n", idx, ok, ErrName(err));
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-006/adapter:%u: MDIO absent, driver correctly "
               "returned NOT_SUPPORTED\n", idx);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
}

/* =========================================================
 * TC-PORT-007: PTM Capability Gate (INTEL_CAP_PCIE_PTM)
 *
 * If PCIE_PTM IS set:
 *   IOCTL_AVB_SETUP_PTM must NOT return ERROR_NOT_SUPPORTED.
 * If PCIE_PTM is NOT set:
 *   IOCTL_AVB_SETUP_PTM must return ERROR_NOT_SUPPORTED.
 * ========================================================= */
static TcResult tc_port_007_ptm_gate(
        UINT32 idx, const AvbAdapterInfo *info, HANDLE h)
{
    BOOL has_cap = (info->capabilities & INTEL_CAP_PCIE_PTM) != 0;

    printf("\n  --- TC-PORT-007/adapter:%u (DID=0x%04X) PCIE_PTM gate ---\n",
           idx, info->device_id);
    printf("      Cap PCIE_PTM: %s\n", has_cap ? "SET" : "NOT SET");

    AVB_PTM_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.config.enabled = 0;   /* probe only — do not activate */
    DWORD bytes = 0;

    BOOL ok = DeviceIoControl(h, IOCTL_AVB_SETUP_PTM,
                              &req, sizeof(req), &req, sizeof(req), &bytes, NULL);
    DWORD err = GetLastError();

    if (has_cap) {
        if (!ok && err == ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-007/adapter:%u: cap PCIE_PTM set but "
                   "SETUP_PTM returned NOT_SUPPORTED\n", idx);
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-007/adapter:%u: PCIE_PTM set, SETUP_PTM did NOT "
               "return NOT_SUPPORTED (ok=%d err=%s status=0x%08X)\n",
               idx, ok, ErrName(err), req.status);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    } else {
        if (ok || err != ERROR_NOT_SUPPORTED) {
            printf("  [FAIL] TC-PORT-007/adapter:%u: cap PCIE_PTM NOT set "
                   "but SETUP_PTM did not return NOT_SUPPORTED "
                   "(ok=%d err=%s)\n", idx, ok, ErrName(err));
            g_stats.failed++; g_stats.total++;
            return TC_FAIL;
        }
        printf("  [PASS] TC-PORT-007/adapter:%u: PCIE_PTM absent, driver "
               "correctly returned NOT_SUPPORTED\n", idx);
        g_stats.passed++; g_stats.total++;
        return TC_PASS;
    }
}

/* =========================================================
 * Print adapter coverage matrix
 * ========================================================= */
static void print_coverage_matrix(int n_adapters, const AdapterCoverage *cov)
{
    printf("\n");
    printf("========================================\n");
    printf(" Adapter Coverage Matrix\n");
    printf("========================================\n");
    printf("  %-12s %-10s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
           "Adapter", "Caps", "TC-001", "TC-002", "TC-003",
           "TC-004", "TC-005", "TC-006", "TC-007");
    printf("  %-12s %-10s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
           "(DID)", "bitmask", "Enum", "PTP", "ENWTS",
           "TAS", "FP", "MDIO", "PTM");
    printf("  %s\n",
           "---------------------------------------------------------------------");

    for (int i = 0; i < n_adapters; i++) {
        printf("  DID=0x%04X   0x%08X %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
               cov[i].device_id,
               cov[i].capabilities,
               TcStr(cov[i].tc_001),
               TcStr(cov[i].tc_002),
               TcStr(cov[i].tc_003),
               TcStr(cov[i].tc_004),
               TcStr(cov[i].tc_005),
               TcStr(cov[i].tc_006),
               TcStr(cov[i].tc_007));
    }

    /* Coverage completeness note */
    printf("\n  Coverage note:\n");
    UINT32 all_caps_seen = 0;
    for (int i = 0; i < n_adapters; i++) {
        all_caps_seen |= cov[i].capabilities;
    }
    printf("    BASIC_1588 tested : %s\n",
           (all_caps_seen & INTEL_CAP_BASIC_1588) ? "YES" : "NO — no adapter with PTP present");
    printf("    ENHANCED_TS tested: %s\n",
           (all_caps_seen & INTEL_CAP_ENHANCED_TS) ? "YES" : "NO — need I219/I225/I226/I210");
    printf("    TSN_TAS tested    : %s\n",
           (all_caps_seen & INTEL_CAP_TSN_TAS) ? "YES" : "NO — need I226/I225");
    printf("    TSN_FP tested     : %s\n",
           (all_caps_seen & INTEL_CAP_TSN_FP) ? "YES" : "NO — need I226");
    printf("    PCIE_PTM tested   : %s\n",
           (all_caps_seen & INTEL_CAP_PCIE_PTM) ? "YES" : "NO — need I226");
    printf("    MDIO tested       : %s\n",
           (all_caps_seen & INTEL_CAP_MDIO) ? "YES" : "NO — need I219/I210");
    printf("\n  The negative (NOT_SUPPORTED) gate tests run on ALL present adapters\n");
    printf("  regardless of their capabilities. Gates tested on this run:\n");

    /* Count how many adapters had each cap absent */
    int tas_absent = 0, fp_absent = 0, ptm_absent = 0;
    int mdio_absent = 0, ets_absent = 0;
    for (int i = 0; i < n_adapters; i++) {
        if (!(cov[i].capabilities & INTEL_CAP_TSN_TAS)) tas_absent++;
        if (!(cov[i].capabilities & INTEL_CAP_TSN_FP))  fp_absent++;
        if (!(cov[i].capabilities & INTEL_CAP_PCIE_PTM)) ptm_absent++;
        if (!(cov[i].capabilities & INTEL_CAP_MDIO))    mdio_absent++;
        if (!(cov[i].capabilities & INTEL_CAP_ENHANCED_TS)) ets_absent++;
    }
    printf("    TAS NOT_SUPPORTED gate verified on %d adapter(s)\n", tas_absent);
    printf("    FP  NOT_SUPPORTED gate verified on %d adapter(s)\n", fp_absent);
    printf("    PTM NOT_SUPPORTED gate verified on %d adapter(s)\n", ptm_absent);
    printf("    MDIO NOT_SUPPORTED gate verified on %d adapter(s)\n", mdio_absent);
    printf("    ENHANCED_TS NOT_SUPPORTED gate verified on %d adapter(s)\n", ets_absent);
}

/* =========================================================
 * main
 * ========================================================= */
int main(void)
{
    printf("=== QA-SC-PORT-001: Hardware Portability Test ===\n");
    printf("    Verifies: #114 (portability across Intel NIC families)\n");
    printf("    Tests INTEL_CAP_* capability gates for each IOCTL\n");
    printf("    MULTI-ADAPTER: tests all enumerated adapters\n\n");

    ZeroMemory(&g_stats, sizeof(g_stats));
    ZeroMemory(&g_cov, sizeof(g_cov));

    /* Step 1: enumerate all Intel adapters */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    ZeroMemory(adapters, sizeof(adapters));
    int count = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);

    if (count == 0) {
        printf("[SKIP] No Intel adapters found via IntelAvbFilter driver.\n");
        printf("       Ensure driver is installed: sc query IntelAvbFilter\n");
        return 0;
    }

    printf("Found %d Intel adapter(s):\n", count);
    for (int i = 0; i < count; i++) {
        printf("  [%d] VID=0x%04X DID=0x%04X %-12s caps=0x%08X\n",
               i, adapters[i].vendor_id, adapters[i].device_id,
               adapters[i].device_name, adapters[i].capabilities);
    }

    /* Step 2: run all TCs per adapter */
    int n_tested = 0;
    for (int i = 0; i < count; i++) {
        const AvbAdapterInfo *info = &adapters[i];
        UINT32 idx = (UINT32)info->global_index;

        printf("\n========================================\n");
        printf(" Adapter %d: DID=0x%04X %s (global index %u)\n",
               i, info->device_id, info->device_name, idx);
        printf("========================================\n");

        g_cov[i].device_id    = info->device_id;
        g_cov[i].capabilities = info->capabilities;

        /* TC-PORT-001: capability sanity (no open handle needed) */
        g_cov[i].tc_001 = tc_port_001_capability_sanity(idx, info);

        /* Open per-adapter handle */
        HANDLE h = AvbOpenAdapter(info);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  [FAIL] Could not open adapter handle (err=%lu)\n",
                   GetLastError());
            g_stats.failed++; g_stats.total++;
            /* Mark remaining TCs as fail since we couldn't open */
            g_cov[i].tc_002 = TC_FAIL;
            g_cov[i].tc_003 = TC_FAIL;
            g_cov[i].tc_004 = TC_FAIL;
            g_cov[i].tc_005 = TC_FAIL;
            g_cov[i].tc_006 = TC_FAIL;
            g_cov[i].tc_007 = TC_FAIL;
            continue;
        }

        g_cov[i].tc_002 = tc_port_002_ptp_clock_read(idx, info, h);
        g_cov[i].tc_003 = tc_port_003_enhanced_ts(idx, info, h);
        g_cov[i].tc_004 = tc_port_004_tas_gate(idx, info, h);
        g_cov[i].tc_005 = tc_port_005_fp_gate(idx, info, h);
        g_cov[i].tc_006 = tc_port_006_mdio_read(idx, info, h);
        g_cov[i].tc_007 = tc_port_007_ptm_gate(idx, info, h);

        CloseHandle(h);
        n_tested++;
    }

    /* Step 3: coverage matrix */
    print_coverage_matrix(count, g_cov);

    /* Step 4: final summary */
    printf("\n");
    printf("Total: %d  Passed: %d  Failed: %d  Skipped: %d",
           g_stats.total, g_stats.passed, g_stats.failed, g_stats.skipped);
    if (g_stats.total > 0) {
        printf("  Rate: %.1f%%",
               100.0 * g_stats.passed / (double)g_stats.total);
    }
    printf("\n");

    if (g_stats.failed > 0) {
        printf("RESULT: FAILED (%d failure(s))\n", g_stats.failed);
        return 1;
    }
    printf("RESULT: SUCCESS\n");
    return 0;
}
