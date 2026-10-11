/**
 * @file test_tx_timestamp_provenance.c
 * @brief Provenance correctness regression tests — pure-logic and hardware-integrated.
 *
 * Section L (Logic): hardware-free mock tests.  No CreateFile / DeviceIoControl.
 *   TEST-PROV-L01  Independent capture: driver must record two distinct PHC reads,
 *                  not the same variable copied twice.  Simulates self-comparison bug.
 *   TEST-PROV-L02  Provenance gating: a struct with VERIFIED_HARDWARE_TX from
 *                  SEND_PTP is recognised as a masquerade and rejected by check logic.
 *   TEST-PROV-L03  Multi-adapter routing invariant: each handle bound to a specific
 *                  adapter index routes only to that adapter's context.
 *   TEST-PROV-L04  Unavailable provenance: valid==0 AND ts_provenance==UNAVAILABLE
 *                  is the only acceptable response when no timestamp exists.
 *   TEST-PROV-L05  Struct layout (compile-time static_assert).
 *
 * Section H (Hardware): require driver present; FAIL on setup errors except
 * "driver absent" which is SKIP.  These run after Section L.
 *   TEST-PROV-H01  Self-comparison regression: driver must not return identical
 *                  values for timestamp_ns and phc_at_send_ns with PRE_SEND_PHC.
 *   TEST-PROV-H02  Masquerade regression: SEND_PTP must never return
 *                  ts_provenance == VERIFIED_HARDWARE_TX.
 *   TEST-PROV-H03  Multi-adapter isolation: two handles bound to distinct adapters
 *                  service SEND_PTP independently (verified via provenance source tag,
 *                  not by comparing VID/DID copied from ENUM_ADAPTERS).
 *   TEST-PROV-H04  Unavailable hardware: fresh handle before any SEND_PTP →
 *                  GET_TX_TIMESTAMP returns valid==0 and ts_provenance==UNAVAILABLE.
 *
 * Verifies: #149 (REQ-F-PTP-007) | Corrects: #199, #317 false-positives
 * Traces to: #48 (REQ-F-IOCTL-PHC-004)
 * Related: #328, #199
 *
 * Exit codes:
 *   0  All executed tests passed (at least one definitive PASS or FAIL).
 *   1  At least one test FAILED.
 *   2  All runtime tests SKIPPED (driver absent); compile-time assertions passed.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#ifndef NDIS_STATUS_SUCCESS
#define NDIS_STATUS_SUCCESS  ((NDIS_STATUS)0x00000000L)
#endif
typedef ULONG NDIS_STATUS;

#include "../../../include/avb_ioctl.h"

/* =========================================================================
 * TEST-PROV-L05: Compile-time struct layout and ABI assertions.
 *
 * Category: Pure compile-time.  No device required.
 * Exercises: shared production struct layout; avb_ioctl.h constants.
 * =========================================================================*/
static_assert(
    offsetof(AVB_TX_TIMESTAMP_REQUEST, ts_provenance) ==
        offsetof(AVB_TX_TIMESTAMP_REQUEST, status) + sizeof(avb_u32),
    "TEST-PROV-L05: ts_provenance must follow status in AVB_TX_TIMESTAMP_REQUEST");

static_assert(
    offsetof(AVB_TEST_SEND_PTP_REQUEST, ts_provenance) ==
        offsetof(AVB_TEST_SEND_PTP_REQUEST, phc_at_send_ns) + sizeof(avb_u64),
    "TEST-PROV-L05: ts_provenance must follow phc_at_send_ns in AVB_TEST_SEND_PTP_REQUEST");

static_assert(
    offsetof(AVB_TEST_SEND_PTP_REQUEST, timestamp_ns) !=
        offsetof(AVB_TEST_SEND_PTP_REQUEST, phc_at_send_ns),
    "TEST-PROV-L05: timestamp_ns and phc_at_send_ns must be at distinct offsets");

/* V1 sizes must match the pre-extension struct sizes exactly. */
static_assert(AVB_TX_TIMESTAMP_REQUEST_V1_SIZE   == 24u, "TEST-PROV-L05: V1 size mismatch");
static_assert(AVB_TEST_SEND_PTP_REQUEST_V1_SIZE  == 32u, "TEST-PROV-L05: V1 size mismatch");
/* V2 sizes = V1 + 4 (ts_provenance) + 4 (_reserved) = V1 + 8. */
static_assert(sizeof(AVB_TX_TIMESTAMP_REQUEST)   == AVB_TX_TIMESTAMP_REQUEST_V1_SIZE  + 8u, "TEST-PROV-L05: V2 must be V1 + 8");
static_assert(sizeof(AVB_TEST_SEND_PTP_REQUEST)  == AVB_TEST_SEND_PTP_REQUEST_V1_SIZE + 8u, "TEST-PROV-L05: V2 must be V1 + 8");
/* ts_provenance and _reserved must be the final 8 bytes (4+4, naturally aligned). */
static_assert(offsetof(AVB_TX_TIMESTAMP_REQUEST, ts_provenance) + sizeof(avb_u32) + sizeof(avb_u32)
              == sizeof(AVB_TX_TIMESTAMP_REQUEST), "TEST-PROV-L05: V2 tail fields not at end");
static_assert(offsetof(AVB_TEST_SEND_PTP_REQUEST, ts_provenance) + sizeof(avb_u32) + sizeof(avb_u32)
              == sizeof(AVB_TEST_SEND_PTP_REQUEST), "TEST-PROV-L05: V2 tail fields not at end");
static_assert(AVB_TX_PROV_UNAVAILABLE            == 0u,  "TEST-PROV-L05: constant");
static_assert(AVB_TX_PROV_PRE_SEND_PHC           == 1u,  "TEST-PROV-L05: constant");
static_assert(AVB_TX_PROV_SOFTWARE_FALLBACK      == 2u,  "TEST-PROV-L05: constant");
static_assert(AVB_TX_PROV_NDIS_TX_COMPLETION     == 3u,  "TEST-PROV-L05: constant");
static_assert(AVB_TX_PROV_VERIFIED_HARDWARE_TX   == 4u,  "TEST-PROV-L05: constant");

/* =========================================================================
 * Result tracking
 * =========================================================================*/
#define DEVICE_PATH_W  L"\\\\.\\IntelAvbFilter"

static int s_total   = 0;
static int s_passed  = 0;
static int s_failed  = 0;
static int s_skipped = 0;

static void tc_pass(const char *id, const char *detail)
{
    s_total++; s_passed++;
    printf("  [PASS] %s: %s\n", id, detail);
}
static void tc_fail(const char *id, const char *detail)
{
    s_total++; s_failed++;
    printf("  [FAIL] %s: %s\n", id, detail);
}
static void tc_skip(const char *id, const char *detail)
{
    s_total++; s_skipped++;
    printf("  [SKIP] %s: %s\n", id, detail);
}

/* =========================================================================
 * Section L: Pure-logic mock tests — no device handle required
 * =========================================================================*/

/* -------------------------------------------------------------------------
 * TEST-PROV-L01: Independent capture invariant.
 *
 * The driver is required to make TWO separate PHC reads: one at IOCTL entry
 * (phc_at_send_ns) and one just before NdisFSendNetBufferLists (timestamp_ns).
 * The provenance tag PRE_SEND_PHC signals that TWO distinct reads occurred.
 *
 * Self-comparison regression: if a driver sets both fields from the same
 * variable (e.g. both = captureTs), the delta is always exactly 0 regardless
 * of when the IOCTL is processed.  This test verifies the CHECK LOGIC: given
 * a simulated driver response, the harness correctly identifies the bug.
 *
 * Note: equal numeric values CAN legitimately occur (same QPC tick, same ns
 * resolution boundary).  The check is based on provenance + structural
 * properties, not just value equality.
 * -------------------------------------------------------------------------*/
static void test_l01(void)
{
    printf("\n[TEST-PROV-L01] Independent capture invariant (mock)\n");

    /* Case A: Simulated self-comparison bug.
     * Driver set both fields to the same variable.  The check logic must flag
     * this when ts_provenance == PRE_SEND_PHC AND delta is exactly 0 across
     * many samples (statistically impossible for two independent SYSTIM reads
     * because the SYSTIM register advances at ~8 ns per tick at 125 MHz). */
    {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.timestamp_ns   = 1791575000000000000ULL;   /* simulated SYSTIM (ns) */
        r.phc_at_send_ns = 1791575000000000000ULL;   /* identical — bug */
        r.ts_provenance  = AVB_TX_PROV_PRE_SEND_PHC;
        r.packets_sent   = 1;
        r.status         = NDIS_STATUS_SUCCESS;

        /* Check: delta == 0 with PRE_SEND_PHC provenance is suspicious */
        int64_t delta = (int64_t)r.timestamp_ns - (int64_t)r.phc_at_send_ns;
        bool self_compare_bug = (delta == 0) && (r.ts_provenance == AVB_TX_PROV_PRE_SEND_PHC);
        if (!self_compare_bug) {
            tc_fail("TEST-PROV-L01", "case A: self-comparison bug not detected by check logic");
            return;
        }
    }

    /* Case B: Correct two-snapshot response (no bug).
     * phc_at_send_ns (entry) is 350 ns earlier than timestamp_ns (pre-send). */
    {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.timestamp_ns   = 1791575000000000350ULL;
        r.phc_at_send_ns = 1791575000000000000ULL;
        r.ts_provenance  = AVB_TX_PROV_PRE_SEND_PHC;
        r.packets_sent   = 1;
        r.status         = NDIS_STATUS_SUCCESS;

        int64_t delta = (int64_t)r.timestamp_ns - (int64_t)r.phc_at_send_ns;
        bool self_compare_bug = (delta == 0) && (r.ts_provenance == AVB_TX_PROV_PRE_SEND_PHC);
        if (self_compare_bug) {
            tc_fail("TEST-PROV-L01", "case B: correct response wrongly flagged as bug");
            return;
        }
        if (delta <= 0) {
            tc_fail("TEST-PROV-L01", "case B: delta non-positive — ordering violated");
            return;
        }
    }

    /* Case C: SOFTWARE_FALLBACK — both in QPC ticks; delta may be tiny but non-zero. */
    {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.timestamp_ns   = 1234567890ULL;
        r.phc_at_send_ns = 1234567800ULL;   /* 90 ticks earlier */
        r.ts_provenance  = AVB_TX_PROV_SOFTWARE_FALLBACK;
        r.packets_sent   = 1;
        r.status         = NDIS_STATUS_SUCCESS;

        int64_t delta = (int64_t)r.timestamp_ns - (int64_t)r.phc_at_send_ns;
        /* With SOFTWARE_FALLBACK, both in QPC ticks; accept non-negative delta */
        if (delta < 0) {
            tc_fail("TEST-PROV-L01", "case C: SOFTWARE_FALLBACK delta negative");
            return;
        }
    }

    tc_pass("TEST-PROV-L01", "self-comparison detection and independent-capture cases correct");
}

/* -------------------------------------------------------------------------
 * TEST-PROV-L02: Provenance gating — SEND_PTP cannot claim VERIFIED_HARDWARE_TX.
 *
 * A SEND_PTP response always carries a pre-send snapshot, never a hardware
 * egress latch.  The check logic must reject VERIFIED_HARDWARE_TX from
 * SEND_PTP as a masquerade.
 * -------------------------------------------------------------------------*/
static void test_l02(void)
{
    printf("\n[TEST-PROV-L02] Provenance gating — SEND_PTP cannot claim VERIFIED_HARDWARE_TX (mock)\n");

    /* check_masquerade: returns true if the SEND_PTP response is claiming a
     * hardware provenance that SEND_PTP cannot legitimately provide. */
    #define check_masquerade(r) ((r).ts_provenance == AVB_TX_PROV_VERIFIED_HARDWARE_TX || \
                                 (r).ts_provenance == AVB_TX_PROV_NDIS_TX_COMPLETION)

    /* Case A: bug — SEND_PTP claims VERIFIED_HARDWARE_TX */
    {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.ts_provenance = AVB_TX_PROV_VERIFIED_HARDWARE_TX;
        r.packets_sent  = 1;
        r.status        = NDIS_STATUS_SUCCESS;
        if (!check_masquerade(r)) {
            tc_fail("TEST-PROV-L02", "case A: masquerade not detected");
            return;
        }
    }
    /* Case B: correct — SEND_PTP claims PRE_SEND_PHC */
    {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.ts_provenance = AVB_TX_PROV_PRE_SEND_PHC;
        r.packets_sent  = 1;
        r.status        = NDIS_STATUS_SUCCESS;
        if (check_masquerade(r)) {
            tc_fail("TEST-PROV-L02", "case B: correct PRE_SEND_PHC flagged as masquerade");
            return;
        }
    }
    /* Case C: correct — SEND_PTP claims SOFTWARE_FALLBACK */
    {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.ts_provenance = AVB_TX_PROV_SOFTWARE_FALLBACK;
        r.packets_sent  = 1;
        r.status        = NDIS_STATUS_SUCCESS;
        if (check_masquerade(r)) {
            tc_fail("TEST-PROV-L02", "case C: correct SOFTWARE_FALLBACK flagged as masquerade");
            return;
        }
    }
    #undef check_masquerade

    tc_pass("TEST-PROV-L02", "masquerade detection logic correct for all provenance values");
}

/* -------------------------------------------------------------------------
 * TEST-PROV-L03: Multi-adapter routing invariant (mock).
 *
 * Each handle bound to a specific adapter index via OPEN_ADAPTER must route
 * to that adapter's context exclusively.  This is tested by simulating a
 * context table and verifying the routing invariants.
 * -------------------------------------------------------------------------*/

typedef struct { avb_u16 vid; avb_u16 did; uint32_t idx; } MockAdapterCtx;

/* Simulate driver routing: return true if h_idx routes to the adapter at tbl[expected_idx]. */
static bool mock_routes_to(const MockAdapterCtx *tbl, int tbl_len,
                            uint32_t h_idx, uint32_t expected_idx)
{
    if ((int)h_idx >= tbl_len || (int)expected_idx >= tbl_len) return false;
    return tbl[h_idx].idx == tbl[expected_idx].idx;
}

static void test_l03(void)
{
    printf("\n[TEST-PROV-L03] Multi-adapter routing invariant (mock)\n");

    MockAdapterCtx adapters[3] = {
        {0x8086, 0x125C, 0},  /* I226-LM */
        {0x8086, 0x15F3, 1},  /* I219-LM */
        {0x8086, 0x157B, 2},  /* I219-V  */
    };

    /* h0 bound to adapter 0; h1 bound to adapter 1; h2 bound to adapter 2 */
    for (int hi = 0; hi < 3; hi++) {
        /* Each handle must route exclusively to its own adapter */
        if (!mock_routes_to(adapters, 3, (uint32_t)hi, (uint32_t)hi)) {
            tc_fail("TEST-PROV-L03", "handle routed to wrong adapter");
            return;
        }
        /* And must NOT route to any other adapter */
        for (int oi = 0; oi < 3; oi++) {
            if (oi == hi) continue;
            if (mock_routes_to(adapters, 3, (uint32_t)hi, (uint32_t)oi)) {
                tc_fail("TEST-PROV-L03", "handle incorrectly routes to a different adapter");
                return;
            }
        }
    }

    /* Distinct contexts: each adapter has a different DID */
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            if (adapters[i].idx == adapters[j].idx) {
                tc_fail("TEST-PROV-L03", "two adapter indices collide");
                return;
            }
        }
    }

    tc_pass("TEST-PROV-L03", "mock routing invariants verified for 3 simulated adapters");
}

/* -------------------------------------------------------------------------
 * TEST-PROV-L04: Unavailable provenance invariant (mock).
 *
 * When valid==0, the only acceptable provenance is UNAVAILABLE.
 * Any other provenance with valid==0 is a driver inconsistency.
 * -------------------------------------------------------------------------*/
static void test_l04(void)
{
    printf("\n[TEST-PROV-L04] Unavailable provenance invariant (mock)\n");

    /* Case A: correct — valid==0 and UNAVAILABLE */
    {
        AVB_TX_TIMESTAMP_REQUEST r = {0};
        r.valid         = 0;
        r.ts_provenance = AVB_TX_PROV_UNAVAILABLE;
        r.status        = NDIS_STATUS_SUCCESS;
        bool ok = (r.valid == 0) && (r.ts_provenance == AVB_TX_PROV_UNAVAILABLE);
        if (!ok) { tc_fail("TEST-PROV-L04", "case A: correct response rejected"); return; }
    }
    /* Case B: bug — valid==0 but PRE_SEND_PHC */
    {
        AVB_TX_TIMESTAMP_REQUEST r = {0};
        r.valid         = 0;
        r.ts_provenance = AVB_TX_PROV_PRE_SEND_PHC;
        r.status        = NDIS_STATUS_SUCCESS;
        bool is_bug = (r.valid == 0) && (r.ts_provenance != AVB_TX_PROV_UNAVAILABLE);
        if (!is_bug) { tc_fail("TEST-PROV-L04", "case B: inconsistency not detected"); return; }
    }
    /* Case C: valid==1 with PRE_SEND_PHC — legitimate */
    {
        AVB_TX_TIMESTAMP_REQUEST r = {0};
        r.timestamp_ns  = 1791575000000000000ULL;
        r.valid         = 1;
        r.ts_provenance = AVB_TX_PROV_PRE_SEND_PHC;
        r.status        = NDIS_STATUS_SUCCESS;
        bool ok = (r.valid == 1) && (r.ts_provenance != AVB_TX_PROV_UNAVAILABLE);
        if (!ok) { tc_fail("TEST-PROV-L04", "case C: valid timestamp incorrectly rejected"); return; }
    }

    tc_pass("TEST-PROV-L04", "unavailable provenance invariant verified for all cases");
}

/* =========================================================================
 * Section L summary
 * =========================================================================*/
static void run_section_l(void)
{
    printf("\n=== Section L: Pure-logic mock tests (no device required) ===\n");

    /* TEST-PROV-L05: compile-time, report unconditionally */
    s_total++; s_passed++;
    printf("  [PASS] TEST-PROV-L05: compile-time struct layout assertions verified\n");

    test_l01();
    test_l02();
    test_l03();
    test_l04();
}

/* =========================================================================
 * Section H: Hardware-integrated tests
 * =========================================================================*/
#define SHARE_FLAGS (FILE_SHARE_READ | FILE_SHARE_WRITE)

static HANDLE open_dev(void)
{
    return CreateFileW(DEVICE_PATH_W, GENERIC_READ | GENERIC_WRITE,
                       SHARE_FLAGS, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static bool bind_adapter(HANDLE h, uint32_t idx)
{
    AVB_ENUM_REQUEST er = {0};
    er.index = idx;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                         &er, sizeof(er), &er, sizeof(er), &br, NULL) ||
        er.status != NDIS_STATUS_SUCCESS)
        return false;
    AVB_OPEN_REQUEST or_ = {0};
    or_.vendor_id = er.vendor_id;
    or_.device_id = er.device_id;
    or_.index     = idx;
    return DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                           &or_, sizeof(or_), &or_, sizeof(or_), &br, NULL) &&
           or_.status == 0;
}

/* -------------------------------------------------------------------------
 * TEST-PROV-H01: Self-comparison regression (hardware-integrated).
 *
 * Over 20 SEND_PTP calls, no packet may have timestamp_ns == phc_at_send_ns
 * while ts_provenance == PRE_SEND_PHC (which would indicate the driver
 * assigned the same variable to both fields).  Legitimate equal values from
 * a SOFTWARE_FALLBACK are acceptable because QPC resolution is coarser.
 * -------------------------------------------------------------------------*/
static void test_h01(HANDLE hDev, bool driver_present)
{
    printf("\n[TEST-PROV-H01] Self-comparison regression (hardware-integrated)\n");
    if (!driver_present) { tc_skip("TEST-PROV-H01", "driver absent"); return; }

    HANDLE h = open_dev();
    if (h == INVALID_HANDLE_VALUE) {
        tc_fail("TEST-PROV-H01", "CreateFileW failed — driver present but device inaccessible");
        return;
    }
    if (!bind_adapter(h, 0)) {
        CloseHandle(h);
        tc_fail("TEST-PROV-H01", "OPEN_ADAPTER failed — setup error");
        return;
    }

    int sent = 0, self_cmp_phc = 0;
    for (int i = 0; i < 20; i++) {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.sequence_id = (avb_u32)(0xB100 + i);
        DWORD br = 0;
        if (!DeviceIoControl(h, IOCTL_AVB_TEST_SEND_PTP,
                             &r, sizeof(r), &r, sizeof(r), &br, NULL) ||
            r.status != NDIS_STATUS_SUCCESS || r.packets_sent == 0)
            continue;
        sent++;
        /* Self-comparison bug: identical values with PRE_SEND_PHC provenance */
        if (r.timestamp_ns == r.phc_at_send_ns &&
            r.ts_provenance == AVB_TX_PROV_PRE_SEND_PHC) {
            self_cmp_phc++;
            printf("    i=%d ts=%llu == phc=%llu prov=PRE_SEND_PHC  [SELF-COMPARISON BUG]\n",
                   i, (unsigned long long)r.timestamp_ns, (unsigned long long)r.phc_at_send_ns);
        }
    }
    CloseHandle(h);

    if (sent == 0) {
        tc_skip("TEST-PROV-H01", "no packets sent (adapter not PTP-ready)");
        return;
    }
    if (self_cmp_phc > 0) {
        char msg[128];
        (void)sprintf(msg, "%d/%d packets had PRE_SEND_PHC + identical timestamps", self_cmp_phc, sent);
        tc_fail("TEST-PROV-H01", msg);
    } else {
        tc_pass("TEST-PROV-H01", "no PRE_SEND_PHC self-comparison detected");
    }
}

/* -------------------------------------------------------------------------
 * TEST-PROV-H02: Masquerade regression (hardware-integrated).
 *
 * SEND_PTP must never return ts_provenance == VERIFIED_HARDWARE_TX or
 * NDIS_TX_COMPLETION; those are only valid from GET_TX_TIMESTAMP.
 * -------------------------------------------------------------------------*/
static void test_h02(HANDLE hDev, bool driver_present)
{
    printf("\n[TEST-PROV-H02] Masquerade regression (hardware-integrated)\n");
    if (!driver_present) { tc_skip("TEST-PROV-H02", "driver absent"); return; }

    HANDLE h = open_dev();
    if (h == INVALID_HANDLE_VALUE) {
        tc_fail("TEST-PROV-H02", "CreateFileW failed");
        return;
    }
    if (!bind_adapter(h, 0)) {
        CloseHandle(h);
        tc_fail("TEST-PROV-H02", "OPEN_ADAPTER failed");
        return;
    }

    int sent = 0, masquerade = 0;
    for (int i = 0; i < 20; i++) {
        AVB_TEST_SEND_PTP_REQUEST r = {0};
        r.sequence_id = (avb_u32)(0xB200 + i);
        DWORD br = 0;
        if (!DeviceIoControl(h, IOCTL_AVB_TEST_SEND_PTP,
                             &r, sizeof(r), &r, sizeof(r), &br, NULL) ||
            r.status != NDIS_STATUS_SUCCESS || r.packets_sent == 0)
            continue;
        sent++;
        if (r.ts_provenance == AVB_TX_PROV_VERIFIED_HARDWARE_TX ||
            r.ts_provenance == AVB_TX_PROV_NDIS_TX_COMPLETION) {
            masquerade++;
            printf("    i=%d ts_provenance=%u [MASQUERADE]\n", i, (unsigned)r.ts_provenance);
        }
    }
    CloseHandle(h);

    if (sent == 0) { tc_skip("TEST-PROV-H02", "no packets sent"); return; }
    if (masquerade > 0) {
        char msg[128];
        (void)sprintf(msg, "%d/%d packets claimed hardware provenance from SEND_PTP", masquerade, sent);
        tc_fail("TEST-PROV-H02", msg);
    } else {
        tc_pass("TEST-PROV-H02", "SEND_PTP never claims VERIFIED_HARDWARE_TX or NDIS_TX_COMPLETION");
    }
}

/* -------------------------------------------------------------------------
 * TEST-PROV-H03: Multi-adapter context isolation (hardware-integrated).
 *
 * Category: Windows device contract + per-adapter state isolation.
 *           Requires >=2 adapters; SKIP otherwise.
 *
 * SEND_PTP on h0 stores a timestamp in h0's adapter context (adapter 0).
 * GET_TX_TIMESTAMP on h1 (adapter 1) reads adapter 1's context, which was
 * NOT written by h0's SEND_PTP.  If both adapters share a single context,
 * h1 would return valid==1 after h0's send — exposing the routing fault.
 *
 * NOTE: dual SEND_PTP success alone does NOT prove per-adapter isolation.
 * Both SEND_PTP calls could target the same FsContext (adapter 0) and still
 * succeed.  This test uses cross-handle GET_TX_TIMESTAMP to independently
 * observe the per-context timestamp state.
 *
 * SKIP if fewer than 2 adapters are enumerated.
 * -------------------------------------------------------------------------*/
static void test_h03(HANDLE hDev, bool driver_present)
{
    printf("\n[TEST-PROV-H03] Multi-adapter context isolation via cross-handle GET_TX_TIMESTAMP\n");
    if (!driver_present) { tc_skip("TEST-PROV-H03", "driver absent"); return; }

    /* Enumerate adapters */
    int adapter_count = 0;
    for (int idx = 0; idx < 16; idx++) {
        AVB_ENUM_REQUEST er = {0};
        er.index = (avb_u32)idx;
        DWORD br = 0;
        if (!DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                             &er, sizeof(er), &er, sizeof(er), &br, NULL) ||
            er.status != NDIS_STATUS_SUCCESS)
            break;
        adapter_count++;
    }
    if (adapter_count < 2) {
        tc_skip("TEST-PROV-H03", "fewer than 2 adapters — isolation requires >=2");
        return;
    }

    HANDLE h0 = open_dev();
    HANDLE h1 = open_dev();
    if (h0 == INVALID_HANDLE_VALUE || h1 == INVALID_HANDLE_VALUE) {
        if (h0 != INVALID_HANDLE_VALUE) CloseHandle(h0);
        if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        tc_fail("TEST-PROV-H03", "CreateFileW failed for one or both handles");
        return;
    }

    bool b0 = bind_adapter(h0, 0);
    bool b1 = bind_adapter(h1, 1);
    if (!b0 || !b1) {
        CloseHandle(h0); CloseHandle(h1);
        tc_fail("TEST-PROV-H03", "OPEN_ADAPTER failed for one or both adapters");
        return;
    }

    /* Step 1: Drain any stale timestamp on h1's context before the test. */
    {
        AVB_TX_TIMESTAMP_REQUEST drain = {0};
        DWORD dbr = 0;
        DeviceIoControl(h1, IOCTL_AVB_GET_TX_TIMESTAMP,
                        &drain, sizeof(drain), &drain, sizeof(drain), &dbr, NULL);
    }

    /* Step 2: Send PTP packet via h0 (adapter 0 context). */
    AVB_TEST_SEND_PTP_REQUEST s0 = {0};
    s0.sequence_id = 0xB300;
    DWORD br = 0;
    BOOL send_ok = DeviceIoControl(h0, IOCTL_AVB_TEST_SEND_PTP,
                                    &s0, sizeof(s0), &s0, sizeof(s0), &br, NULL);

    /* Step 3: Read GET_TX_TIMESTAMP on h1 (adapter 1 context) immediately.
     * If contexts are isolated, h1 must return valid==0 (adapter 1 was not
     * written by h0's SEND_PTP).  If contexts are shared, h1 returns valid==1. */
    AVB_TX_TIMESTAMP_REQUEST g1 = {0};
    DWORD gbr = 0;
    BOOL get_ok = DeviceIoControl(h1, IOCTL_AVB_GET_TX_TIMESTAMP,
                                   &g1, sizeof(g1), &g1, sizeof(g1), &gbr, NULL);

    CloseHandle(h0);
    CloseHandle(h1);

    if (!send_ok) {
        tc_skip("TEST-PROV-H03", "SEND_PTP on h0 failed (adapter 0 link down / not ready)");
        return;
    }
    if (!get_ok) {
        tc_fail("TEST-PROV-H03", "GET_TX_TIMESTAMP on h1 IOCTL failed");
        return;
    }

    if (g1.valid != 0) {
        char msg[128];
        (void)sprintf(msg,
            "GET_TX_TIMESTAMP on h1 (adapter 1) returned valid=1 after SEND_PTP on h0 (adapter 0) — "
            "adapters appear to share the same context (routing fault)");
        tc_fail("TEST-PROV-H03", msg);
        return;
    }
    tc_pass("TEST-PROV-H03",
            "GET_TX_TIMESTAMP on h1 returned valid=0 after SEND_PTP on h0 — adapter contexts are isolated");
}

/* -------------------------------------------------------------------------
 * TEST-PROV-H04: Unavailable hardware provenance (hardware-integrated).
 *
 * On a fresh handle (no prior SEND_PTP), GET_TX_TIMESTAMP must return
 * valid==0 and ts_provenance == UNAVAILABLE (last_ndis_tx_timestamp is 0
 * and the FIFO should be empty).
 * -------------------------------------------------------------------------*/
static void test_h04(HANDLE hDev, bool driver_present)
{
    printf("\n[TEST-PROV-H04] Unavailable hardware provenance (hardware-integrated)\n");
    (void)hDev;
    if (!driver_present) { tc_skip("TEST-PROV-H04", "driver absent"); return; }

    HANDLE h = open_dev();
    if (h == INVALID_HANDLE_VALUE) {
        tc_fail("TEST-PROV-H04", "CreateFileW failed");
        return;
    }
    if (!bind_adapter(h, 0)) {
        CloseHandle(h);
        tc_fail("TEST-PROV-H04", "OPEN_ADAPTER failed");
        return;
    }

    AVB_TX_TIMESTAMP_REQUEST r = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TX_TIMESTAMP,
                              &r, sizeof(r), &r, sizeof(r), &br, NULL);
    CloseHandle(h);

    if (!ok) {
        tc_fail("TEST-PROV-H04", "GET_TX_TIMESTAMP IOCTL failed");
        return;
    }
    if (r.valid == 0) {
        if (r.ts_provenance == AVB_TX_PROV_UNAVAILABLE) {
            tc_pass("TEST-PROV-H04", "valid==0 → ts_provenance==UNAVAILABLE");
        } else {
            char msg[128];
            (void)sprintf(msg, "valid==0 but ts_provenance=%u (expected UNAVAILABLE)",
                          (unsigned)r.ts_provenance);
            tc_fail("TEST-PROV-H04", msg);
        }
    } else {
        /* Stale timestamp from a previous test run; cannot enforce UNAVAILABLE. */
        printf("  NOTE: valid==1 (stale from prior test), prov=%u — cannot enforce UNAVAILABLE\n",
               (unsigned)r.ts_provenance);
        tc_skip("TEST-PROV-H04", "stale timestamp present from prior test");
    }
}

/* -------------------------------------------------------------------------
 * TEST-PROV-H05: V1/V2 ABI regression — bytesReturned and canary bytes.
 *
 * Category: Windows device contract (FastIo + METHOD_BUFFERED semantics).
 *           Requires driver; hardware state irrelevant (adapter 0 used).
 *
 * Verifies:
 *   V1 client (24-byte GET_TX_TIMESTAMP buffer):
 *     - DeviceIoControl succeeds (not STATUS_BUFFER_TOO_SMALL)
 *     - bytesReturned == 24 (driver does not over-report)
 *     - canary bytes at offsets 24–31 are unchanged (driver does not overwrite)
 *   V2 client (32-byte GET_TX_TIMESTAMP buffer):
 *     - bytesReturned == 32
 *   V1 client (32-byte SEND_PTP output buffer):
 *     - DeviceIoControl succeeds
 *     - bytesReturned == 32
 *     - canary bytes at offsets 32–39 unchanged
 *   V2 client (40-byte SEND_PTP output buffer):
 *     - bytesReturned == 40
 * -------------------------------------------------------------------------*/
static void test_h05(bool driver_present)
{
    printf("\n[TEST-PROV-H05] V1/V2 ABI bytesReturned and canary bytes\n");
    if (!driver_present) { tc_skip("TEST-PROV-H05", "driver absent"); return; }

    HANDLE h = open_dev();
    if (h == INVALID_HANDLE_VALUE) {
        tc_fail("TEST-PROV-H05", "CreateFileW failed");
        return;
    }
    if (!bind_adapter(h, 0)) {
        CloseHandle(h);
        tc_fail("TEST-PROV-H05", "OPEN_ADAPTER failed");
        return;
    }

    bool all_ok = true;

    /* ---- GET_TX_TIMESTAMP V1 (24 bytes) ---- */
    {
        /* Allocate 40 bytes; place canary pattern in bytes 24-39 */
        uint8_t buf[40];
        memset(buf, 0xCD, sizeof(buf));
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TX_TIMESTAMP,
                                  buf, sizeof(AVB_TX_TIMESTAMP_REQUEST),   /* in: V2 size */
                                  buf, AVB_TX_TIMESTAMP_REQUEST_V1_SIZE,   /* out: V1 size */
                                  &br, NULL);
        if (!ok) {
            tc_fail("TEST-PROV-H05", "GET_TX_TIMESTAMP V1 buffer rejected (STATUS_BUFFER_TOO_SMALL?)");
            all_ok = false;
        } else if (br != AVB_TX_TIMESTAMP_REQUEST_V1_SIZE) {
            char msg[128];
            (void)sprintf(msg, "GET_TX_TIMESTAMP V1: bytesReturned=%lu expected %u", br, AVB_TX_TIMESTAMP_REQUEST_V1_SIZE);
            tc_fail("TEST-PROV-H05", msg);
            all_ok = false;
        } else {
            /* Verify canary at bytes 24-39 is untouched */
            bool canary_ok = true;
            for (int i = AVB_TX_TIMESTAMP_REQUEST_V1_SIZE; i < (int)sizeof(buf); i++) {
                if (buf[i] != 0xCD) { canary_ok = false; break; }
            }
            if (!canary_ok) {
                tc_fail("TEST-PROV-H05", "GET_TX_TIMESTAMP V1: driver wrote past V1 buffer (canary corrupted)");
                all_ok = false;
            }
        }
    }

    /* ---- GET_TX_TIMESTAMP V2 (32 bytes) ---- */
    {
        uint8_t buf[32];
        memset(buf, 0, sizeof(buf));
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TX_TIMESTAMP,
                                  buf, sizeof(AVB_TX_TIMESTAMP_REQUEST),
                                  buf, sizeof(AVB_TX_TIMESTAMP_REQUEST),
                                  &br, NULL);
        if (!ok) {
            tc_fail("TEST-PROV-H05", "GET_TX_TIMESTAMP V2 buffer failed");
            all_ok = false;
        } else if (br != sizeof(AVB_TX_TIMESTAMP_REQUEST)) {
            char msg[128];
            (void)sprintf(msg, "GET_TX_TIMESTAMP V2: bytesReturned=%lu expected %u", br, (unsigned)sizeof(AVB_TX_TIMESTAMP_REQUEST));
            tc_fail("TEST-PROV-H05", msg);
            all_ok = false;
        }
    }

    /* ---- SEND_PTP V1 (32-byte output buffer) ---- */
    {
        uint8_t buf[48];
        memset(buf, 0xCD, sizeof(buf));
        /* Zero out V1 input fields so the driver can process the request */
        memset(buf, 0, AVB_TEST_SEND_PTP_REQUEST_V1_SIZE);
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_AVB_TEST_SEND_PTP,
                                  buf, AVB_TEST_SEND_PTP_REQUEST_V1_SIZE,  /* in: V1 */
                                  buf, AVB_TEST_SEND_PTP_REQUEST_V1_SIZE,  /* out: V1 */
                                  &br, NULL);
        if (!ok) {
            tc_fail("TEST-PROV-H05", "SEND_PTP V1 buffer rejected (STATUS_BUFFER_TOO_SMALL?)");
            all_ok = false;
        } else if (br != AVB_TEST_SEND_PTP_REQUEST_V1_SIZE) {
            char msg[128];
            (void)sprintf(msg, "SEND_PTP V1: bytesReturned=%lu expected %u", br, AVB_TEST_SEND_PTP_REQUEST_V1_SIZE);
            tc_fail("TEST-PROV-H05", msg);
            all_ok = false;
        } else {
            /* Verify canary at bytes 32-47 is untouched */
            bool canary_ok = true;
            for (int i = AVB_TEST_SEND_PTP_REQUEST_V1_SIZE; i < (int)sizeof(buf); i++) {
                if (buf[i] != 0xCD) { canary_ok = false; break; }
            }
            if (!canary_ok) {
                tc_fail("TEST-PROV-H05", "SEND_PTP V1: driver wrote past V1 buffer (canary corrupted)");
                all_ok = false;
            }
        }
    }

    /* ---- SEND_PTP V2 (40-byte output buffer) ---- */
    {
        uint8_t buf[40];
        memset(buf, 0, sizeof(buf));
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_AVB_TEST_SEND_PTP,
                                  buf, sizeof(AVB_TEST_SEND_PTP_REQUEST),
                                  buf, sizeof(AVB_TEST_SEND_PTP_REQUEST),
                                  &br, NULL);
        if (!ok) {
            tc_fail("TEST-PROV-H05", "SEND_PTP V2 buffer failed");
            all_ok = false;
        } else if (br != sizeof(AVB_TEST_SEND_PTP_REQUEST)) {
            char msg[128];
            (void)sprintf(msg, "SEND_PTP V2: bytesReturned=%lu expected %u", br, (unsigned)sizeof(AVB_TEST_SEND_PTP_REQUEST));
            tc_fail("TEST-PROV-H05", msg);
            all_ok = false;
        }
    }

    CloseHandle(h);
    if (all_ok)
        tc_pass("TEST-PROV-H05", "V1/V2 bytesReturned correct; V1 canary bytes untouched");
}

/* =========================================================================
 * main
 * =========================================================================*/
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("=======================================================================\n");
    printf("TX Timestamp Provenance Regression — TEST-PROV-L01..L05, H01..H05\n");
    printf("  Verifies: #149 (REQ-F-PTP-007) | Corrects: #199, #317 false-positives\n");
    printf("  Section L: pure-logic mock tests (always run)\n");
    printf("  Section H: Windows device contract + hardware (SKIP when driver absent; FAIL on setup error)\n");
    printf("=======================================================================\n");

    /* Section L: always runs regardless of hardware */
    run_section_l();

    /* Detect driver presence */
    HANDLE hDev = CreateFileW(DEVICE_PATH_W, GENERIC_READ | GENERIC_WRITE,
                              SHARE_FLAGS, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    bool driver_present = (hDev != INVALID_HANDLE_VALUE);

    printf("\n=== Section H: Hardware-integrated tests (%s) ===\n",
           driver_present ? "driver present" : "SKIP — driver absent");

    test_h01(hDev, driver_present);
    test_h02(hDev, driver_present);
    test_h03(hDev, driver_present);
    test_h04(hDev, driver_present);
    test_h05(driver_present);

    if (hDev != INVALID_HANDLE_VALUE) CloseHandle(hDev);

    printf("\n=======================================================================\n");
    printf("Summary — TX Timestamp Provenance Regression\n");
    printf("  Total: %d  Passed: %d  Failed: %d  Skipped: %d\n",
           s_total, s_passed, s_failed, s_skipped);

    if (s_failed > 0) {
        printf("  STATUS: FAIL — do not close #199/#317\n");
        return 1;
    }
    /* Only Section L passed and all Section H skipped — no definitive hardware evidence. */
    if (!driver_present) {
        printf("  STATUS: SKIP_ONLY — driver absent; Section L passed (compile+logic), Section H skipped\n");
        return 2;
    }
    printf("  STATUS: PASS\n");
    return 0;
}
