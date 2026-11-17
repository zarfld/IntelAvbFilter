/**
 * @file test_ioctl_abi.c
 * @brief IOCTL ABI Stability and Uniqueness Verification
 *
 * Test ID: TEST-ABI-001
 * Part of: #265 (TEST-COVERAGE-001: Test Coverage Improvement)
 * Verifies: avb_ioctl.h wire-format guarantees (struct sizes, code uniqueness,
 *           ABI version format)
 *
 * Test Cases:
 *   TC-ABI-001: All IOCTL code values are unique (no collisions)
 *   TC-ABI-002: AVB_IOCTL_ABI_VERSION format is valid (non-zero, major != 0)
 *   TC-ABI-003: sizeof(IOCTL_VERSION) == 12         (6 x uint16: Major,Minor,Build,Revision,Flags,Reserved)
 *   TC-ABI-004: sizeof(AVB_REQUEST_HEADER) == 8     (2 x uint32, no padding)
 *   TC-ABI-005: sizeof(AVB_FREQUENCY_REQUEST) == 16 (4 x uint32)
 *   TC-ABI-006: sizeof(AVB_TIMESTAMP_REQUEST) == 16 (uint64 + 2 x uint32)
 *   TC-ABI-007: sizeof(AVB_CLOCK_CONFIG) == 24      (uint64 + 4 x uint32)
 *   TC-ABI-008: sizeof(AVB_OFFSET_REQUEST) == 16    (INT64 + uint32 + 4-byte trailing pad)
 *   TC-ABI-009: sizeof(AVB_RX_TIMESTAMP_REQUEST) == 20  (5 x uint32)
 *   TC-ABI-010: sizeof(AVB_QUEUE_TIMESTAMP_REQUEST) == 20 (5 x uint32)
 *   TC-ABI-011: sizeof(AVB_TX_TIMESTAMP_REQUEST) == 32  (uint64 + 6 x uint32); V1_SIZE == 24
 *   TC-ABI-012: sizeof(AVB_DEVICE_INFO_REQUEST) == 1032 (char[1024] + 2 x uint32)
 *   TC-ABI-013: sizeof(AVB_OPEN_REQUEST) == 12      (2 x uint16 + 2 x uint32)
 *   TC-ABI-014: sizeof(AVB_ENUM_REQUEST) == 20      (2 x uint32 + 2 x uint16 + 2 x uint32)
 *   TC-ABI-015: sizeof(AVB_TS_SUBSCRIBE_REQUEST) == 16  (uint32 + uint16 + 2 x uint8 + 2 x uint32)
 *   TC-ABI-016: sizeof(AVB_QAV_REQUEST) == 24       (uint8 + uint8[3] + 5 x uint32)
 *   TC-ABI-017: sizeof(AVB_TS_UNSUBSCRIBE_REQUEST) == 8 (2 x uint32)
 *
 * CI-safe: No hardware access, no driver device handle, no DeviceIoControl.
 * Requires only: avb_ioctl.h (user-mode) and its dependencies from intel_avb.
 *
 * Build: cl /nologo /W4 /Zi -I include -I external/intel_avb/lib
 *              tests/unit/abi/test_ioctl_abi.c /Fe:test_ioctl_abi.exe
 */

#include <windows.h>
#include <winioctl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avb_ioctl.h"

/* ---------------------------------------------------------------------------
 * Test framework — matches test_hal_unit.c / test_hal_errors.c pattern
 * --------------------------------------------------------------------------- */
typedef struct {
    int passed;
    int failed;
    int total;
} TestResults;

static TestResults g_results = {0, 0, 0};

#define TEST_ASSERT(condition, message) \
    do { \
        g_results.total++; \
        if ((condition)) { \
            printf("  [PASS] %s\n", (message)); \
            g_results.passed++; \
        } else { \
            printf("  [FAIL] %s\n", (message)); \
            g_results.failed++; \
        } \
    } while (0)

#define TEST_CASE(name) printf("\n--- %s ---\n", (name))

/* ---------------------------------------------------------------------------
 * TC-ABI-001: All IOCTL codes have distinct numeric values
 *
 * Rationale: A duplicate IOCTL code silently routes two different operations
 * to the same handler in AvbDispatch.  The kernel won't report the conflict.
 * This check detects it at build-time of the test suite, before any hardware
 * is involved.
 *
 * Note: Debug-only IOCTLs (IOCTL_AVB_READ_REGISTER, IOCTL_AVB_WRITE_REGISTER)
 * and simulation-only IOCTLs (IOCTL_AVB_REG_READ_UBER) are excluded because
 * they may or may not be defined depending on build flags.  The always-defined
 * set is sufficient to catch accidental code reuse.
 * --------------------------------------------------------------------------- */
static void test_ioctl_uniqueness(void)
{
    DWORD codes[] = {
        IOCTL_AVB_GET_VERSION,
        IOCTL_AVB_INIT_DEVICE,
        IOCTL_AVB_GET_DEVICE_INFO,
        IOCTL_AVB_GET_TIMESTAMP,
        IOCTL_AVB_SET_TIMESTAMP,
        IOCTL_AVB_SETUP_TAS,
        IOCTL_AVB_TAS_ACTIVATE,
        IOCTL_AVB_TAS_QUERY_HW,
        IOCTL_AVB_SETUP_FP,
        IOCTL_AVB_SETUP_PTM,
        IOCTL_AVB_MDIO_READ,
        IOCTL_AVB_MDIO_WRITE,
        IOCTL_AVB_ENUM_ADAPTERS,
        IOCTL_AVB_OPEN_ADAPTER,
        IOCTL_AVB_TS_SUBSCRIBE,
        IOCTL_AVB_TS_RING_MAP,
        IOCTL_AVB_TS_UNSUBSCRIBE,
        IOCTL_AVB_SETUP_QAV,
        IOCTL_AVB_GET_HW_STATE,
        IOCTL_AVB_ADJUST_FREQUENCY,
        IOCTL_AVB_GET_CLOCK_CONFIG,
        IOCTL_AVB_SET_HW_TIMESTAMPING,
        IOCTL_AVB_SET_RX_TIMESTAMP,
        IOCTL_AVB_SET_QUEUE_TIMESTAMP,
        IOCTL_AVB_SET_TARGET_TIME,
        IOCTL_AVB_GET_AUX_TIMESTAMP,
        IOCTL_AVB_PHC_OFFSET_ADJUST,
        IOCTL_AVB_GET_TX_TIMESTAMP,
        IOCTL_AVB_GET_RX_TIMESTAMP,
        IOCTL_AVB_TEST_SEND_PTP,
        IOCTL_AVB_SET_PORT_LATENCY,
        IOCTL_AVB_GET_STATISTICS,
        IOCTL_AVB_RESET_STATISTICS,
        IOCTL_AVB_GET_ADAPTER_IDENTITY,
        IOCTL_AVB_OPEN_ADAPTER_BY_LUID,
        IOCTL_AVB_QUERY_HW_SNAPSHOT,
    };
    int n = (int)(sizeof(codes) / sizeof(codes[0]));
    int duplicates = 0;
    int i, j;

    TEST_CASE("TC-ABI-001: IOCTL code uniqueness");
    printf("  Checking %d IOCTL codes for duplicates...\n", n);

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (codes[i] == codes[j]) {
                printf("  DUPLICATE: codes[%d] == codes[%d] == 0x%08X\n",
                       i, j, (unsigned int)codes[i]);
                duplicates++;
            }
        }
    }

    TEST_ASSERT(duplicates == 0,
                "All IOCTL codes have distinct values (no collision)");
}

/* ---------------------------------------------------------------------------
 * TC-ABI-002: ABI version macro is non-zero and major field is populated
 * --------------------------------------------------------------------------- */
static void test_abi_version(void)
{
    TEST_CASE("TC-ABI-002: AVB_IOCTL_ABI_VERSION format");

    TEST_ASSERT(AVB_IOCTL_ABI_VERSION != 0u,
                "AVB_IOCTL_ABI_VERSION is non-zero");
    TEST_ASSERT((AVB_IOCTL_ABI_VERSION & 0xFFFF0000u) != 0u,
                "AVB_IOCTL_ABI_VERSION major field (high 16 bits) is non-zero");
    TEST_ASSERT(AVB_IOCTL_ABI_VERSION == 0x00020001u,
                "AVB_IOCTL_ABI_VERSION == 0x00020001 (v2.1 - IOCTL_AVB_TAS_QUERY_HW served, #350; v2.0 stats struct 192 bytes)");
}

/* ---------------------------------------------------------------------------
 * TC-ABI-003..017: Struct sizeof checks (wire-format stability)
 *
 * Rationale: avb_ioctl.h structs form the DeviceIoControl wire format shared
 * between the kernel driver and all user-mode clients (test tools, AVBServices,
 * etc.).  Any change in struct layout silently breaks the protocol.
 *
 * Expected sizes are for MSVC x64 default packing (no #pragma pack override).
 * If a size assertion fails:
 *   1. The struct layout changed — audit all kernel and UM callers.
 *   2. Update the size constant here ONLY after confirming backward compatibility.
 *
 * Layout reasoning is documented inline for each struct.
 * --------------------------------------------------------------------------- */
static void test_struct_sizes(void)
{
    /* TC-ABI-003 ------------------------------------------------------------ */
    /* struct { uint16 Major; uint16 Minor; uint16 Build; uint16 Revision;     */
    /*           uint16 Flags; uint16 Reserved; }                              */
    /* 6 x 2 = 12 bytes, struct alignment = 2, no padding.                    */
    TEST_CASE("TC-ABI-003: sizeof(IOCTL_VERSION) == 12");
    TEST_ASSERT(sizeof(IOCTL_VERSION) == 12,
                "sizeof(IOCTL_VERSION) == 12  (Major,Minor,Build,Revision,Flags,Reserved as uint16)");

    /* TC-ABI-004 ------------------------------------------------------------ */
    /* struct { uint32 abi_version; uint32 header_size; }                      */
    /* 2 x 4 = 8 bytes, no padding.                                            */
    TEST_CASE("TC-ABI-004: sizeof(AVB_REQUEST_HEADER) == 8");
    TEST_ASSERT(sizeof(AVB_REQUEST_HEADER) == 8,
                "sizeof(AVB_REQUEST_HEADER) == 8  (abi_version + header_size)");

    /* TC-ABI-005 ------------------------------------------------------------ */
    /* struct { uint32 x4 }; 4 x 4 = 16, no padding.                          */
    TEST_CASE("TC-ABI-005: sizeof(AVB_FREQUENCY_REQUEST) == 16");
    TEST_ASSERT(sizeof(AVB_FREQUENCY_REQUEST) == 16,
                "sizeof(AVB_FREQUENCY_REQUEST) == 16  (4 x uint32)");

    /* TC-ABI-006 ------------------------------------------------------------ */
    /* struct { uint64 timestamp; uint32 clock_id; uint32 status; }            */
    /* uint64@0(8) + uint32@8(4) + uint32@12(4) = 16, aligned to 8.          */
    TEST_CASE("TC-ABI-006: sizeof(AVB_TIMESTAMP_REQUEST) == 16");
    TEST_ASSERT(sizeof(AVB_TIMESTAMP_REQUEST) == 16,
                "sizeof(AVB_TIMESTAMP_REQUEST) == 16  (uint64 + clock_id + status)");

    /* TC-ABI-007 ------------------------------------------------------------ */
    /* struct { uint64 systim; uint32 x4 }                                     */
    /* uint64@0(8) + 4x uint32@8 (16) = 24, aligned to 8.                    */
    TEST_CASE("TC-ABI-007: sizeof(AVB_CLOCK_CONFIG) == 24");
    TEST_ASSERT(sizeof(AVB_CLOCK_CONFIG) == 24,
                "sizeof(AVB_CLOCK_CONFIG) == 24  (uint64 + timinca,tsauxc,rate,status)");

    /* TC-ABI-008 ------------------------------------------------------------ */
    /* struct { INT64 offset_ns; uint32 status; }                              */
    /* INT64@0(8) + uint32@8(4) + trailing pad@12(4) = 16, aligned to 8.     */
    TEST_CASE("TC-ABI-008: sizeof(AVB_OFFSET_REQUEST) == 16");
    TEST_ASSERT(sizeof(AVB_OFFSET_REQUEST) == 16,
                "sizeof(AVB_OFFSET_REQUEST) == 16  (INT64 + uint32 + 4-byte pad)");

    /* TC-ABI-009 ------------------------------------------------------------ */
    /* struct { uint32 x5 }; 5 x 4 = 20, aligned to 4.                       */
    TEST_CASE("TC-ABI-009: sizeof(AVB_RX_TIMESTAMP_REQUEST) == 20");
    TEST_ASSERT(sizeof(AVB_RX_TIMESTAMP_REQUEST) == 20,
                "sizeof(AVB_RX_TIMESTAMP_REQUEST) == 20  (5 x uint32)");

    /* TC-ABI-010 ------------------------------------------------------------ */
    /* struct { uint32 x5 }; 5 x 4 = 20, aligned to 4.                       */
    TEST_CASE("TC-ABI-010: sizeof(AVB_QUEUE_TIMESTAMP_REQUEST) == 20");
    TEST_ASSERT(sizeof(AVB_QUEUE_TIMESTAMP_REQUEST) == 20,
                "sizeof(AVB_QUEUE_TIMESTAMP_REQUEST) == 20  (5 x uint32)");

    /* TC-ABI-011 ------------------------------------------------------------ */
    /* struct { uint64 timestamp_ns; uint32 x6 }                              */
    /* uint64@0(8) + 6x uint32@8 (24) = 32, aligned to 8.                    */
    /* V1 (before ts_provenance/_reserved): 8 + 4x uint32 = 24 bytes.        */
    TEST_CASE("TC-ABI-011: sizeof(AVB_TX_TIMESTAMP_REQUEST) == 32 (V2)");
    TEST_ASSERT(sizeof(AVB_TX_TIMESTAMP_REQUEST) == 32,
                "sizeof(AVB_TX_TIMESTAMP_REQUEST) == 32  (uint64 + 6 x uint32)");
    TEST_ASSERT(AVB_TX_TIMESTAMP_REQUEST_V1_SIZE == 24,
                "AVB_TX_TIMESTAMP_REQUEST_V1_SIZE == 24  (V1 backward-compat size without ts_provenance/_reserved)");

    /* TC-ABI-012 ------------------------------------------------------------ */
    /* struct { char[1024]; uint32 buffer_size; uint32 status; }              */
    /* 1024 + 4 + 4 = 1032, aligned to 4.                                    */
    TEST_CASE("TC-ABI-012: sizeof(AVB_DEVICE_INFO_REQUEST) == 1032");
    TEST_ASSERT(sizeof(AVB_DEVICE_INFO_REQUEST) == 1032,
                "sizeof(AVB_DEVICE_INFO_REQUEST) == 1032  (char[1024] + 2 x uint32)");

    /* TC-ABI-013 ------------------------------------------------------------ */
    /* struct { uint16 vendor_id; uint16 device_id; uint32 index; uint32 status;} */
    /* uint16@0(2) + uint16@2(2) + uint32@4(4) + uint32@8(4) = 12, aligned to 4.*/
    TEST_CASE("TC-ABI-013: sizeof(AVB_OPEN_REQUEST) == 12");
    TEST_ASSERT(sizeof(AVB_OPEN_REQUEST) == 12,
                "sizeof(AVB_OPEN_REQUEST) == 12  (vendor_id,device_id,index,status)");

    /* TC-ABI-014 ------------------------------------------------------------ */
    /* struct { uint32 index; uint32 count;                                    */
    /*          uint16 vendor_id; uint16 device_id;                            */
    /*          uint32 capabilities; uint32 status; }                          */
    /* All fields naturally aligned; 4+4+2+2+4+4 = 20, aligned to 4.        */
    TEST_CASE("TC-ABI-014: sizeof(AVB_ENUM_REQUEST) == 20");
    TEST_ASSERT(sizeof(AVB_ENUM_REQUEST) == 20,
                "sizeof(AVB_ENUM_REQUEST) == 20  (index,count,vendor_id,device_id,capabilities,status)");

    /* TC-ABI-015 ------------------------------------------------------------ */
    /* struct { uint32 types_mask; uint16 vlan; uint8 pcp; uint8 reserved0;   */
    /*          uint32 ring_id; uint32 status; }                               */
    /* 4+2+1+1+4+4 = 16, aligned to 4.  reserved0 is explicit padding.       */
    TEST_CASE("TC-ABI-015: sizeof(AVB_TS_SUBSCRIBE_REQUEST) == 16");
    TEST_ASSERT(sizeof(AVB_TS_SUBSCRIBE_REQUEST) == 16,
                "sizeof(AVB_TS_SUBSCRIBE_REQUEST) == 16  (types_mask,vlan,pcp,reserved0,ring_id,status)");

    /* TC-ABI-016 ------------------------------------------------------------ */
    /* struct { uint8 tc; uint8 reserved1[3]; uint32 x5 }                    */
    /* 1+3+4*5 = 24, aligned to 4.  reserved1[3] is explicit padding.        */
    TEST_CASE("TC-ABI-016: sizeof(AVB_QAV_REQUEST) == 24");
    TEST_ASSERT(sizeof(AVB_QAV_REQUEST) == 24,
                "sizeof(AVB_QAV_REQUEST) == 24  (tc,reserved1[3],idle_slope,send_slope,hi_credit,lo_credit,status)");

    /* TC-ABI-017 ------------------------------------------------------------ */
    /* struct { uint32 ring_id; uint32 status; }; 2 x 4 = 8, no padding.     */
    TEST_CASE("TC-ABI-017: sizeof(AVB_TS_UNSUBSCRIBE_REQUEST) == 8");
    TEST_ASSERT(sizeof(AVB_TS_UNSUBSCRIBE_REQUEST) == 8,
                "sizeof(AVB_TS_UNSUBSCRIBE_REQUEST) == 8  (ring_id + status)");

    /* TC-ABI-018 ------------------------------------------------------------ */
    /* 24 x avb_u64 (13 original ABI 1.0 + 11 extended ABI 2.0) x 8 = 192.  */
    TEST_CASE("TC-ABI-018: sizeof(AVB_DRIVER_STATISTICS) == 192");
    TEST_ASSERT(sizeof(AVB_DRIVER_STATISTICS) == 192,
                "sizeof(AVB_DRIVER_STATISTICS) == 192  (24 x avb_u64, ABI 2.0)");

    /* TC-ABI-019 ------------------------------------------------------------ */
    /* AVB_FREQUENCY_REQUEST field offsets.  current_increment (offset 8) carries
     * the RAW TIMINCA register value read before the change (I219: normalised to
     * logical_ns << 24).  Its offset, width and raw-register meaning are part of
     * the public contract; device-layer refactors must not move or reinterpret it. */
    TEST_CASE("TC-ABI-019: AVB_FREQUENCY_REQUEST field offsets 0/4/8/12, current_increment is 32-bit");
    TEST_ASSERT(offsetof(AVB_FREQUENCY_REQUEST, increment_ns) == 0,
                "offsetof(increment_ns) == 0");
    TEST_ASSERT(offsetof(AVB_FREQUENCY_REQUEST, increment_frac) == 4,
                "offsetof(increment_frac) == 4");
    TEST_ASSERT(offsetof(AVB_FREQUENCY_REQUEST, current_increment) == 8,
                "offsetof(current_increment) == 8");
    TEST_ASSERT(offsetof(AVB_FREQUENCY_REQUEST, status) == 12,
                "offsetof(status) == 12");
    {
        AVB_FREQUENCY_REQUEST probe;
        TEST_ASSERT(sizeof(probe.current_increment) == 4,
                    "sizeof(current_increment) == 4  (raw 32-bit TIMINCA)");
    }

    /* TC-ABI-020..023: HIL state verification ABI (Q 2026-10-22).  No implicit padding: identical layout in user mode
     * and kernel; the NET_LUID is the stable identity, the codes are new function numbers 68..70. */
    TEST_CASE("TC-ABI-020: sizeof(AVB_ADAPTER_IDENTITY_REQUEST) == 48, net_luid at 8, status at 40");
    TEST_ASSERT(sizeof(AVB_ADAPTER_IDENTITY_REQUEST) == 48, "sizeof(AVB_ADAPTER_IDENTITY_REQUEST) == 48");
    TEST_ASSERT(offsetof(AVB_ADAPTER_IDENTITY_REQUEST, net_luid) == 8, "offsetof(net_luid) == 8");
    TEST_ASSERT(offsetof(AVB_ADAPTER_IDENTITY_REQUEST, mac) == 32, "offsetof(mac) == 32");
    TEST_ASSERT(offsetof(AVB_ADAPTER_IDENTITY_REQUEST, status) == 40, "offsetof(status) == 40");
    TEST_CASE("TC-ABI-021: sizeof(AVB_OPEN_BY_LUID_REQUEST) == 24, net_luid at 8");
    TEST_ASSERT(sizeof(AVB_OPEN_BY_LUID_REQUEST) == 24, "sizeof(AVB_OPEN_BY_LUID_REQUEST) == 24");
    TEST_ASSERT(offsetof(AVB_OPEN_BY_LUID_REQUEST, net_luid) == 8, "offsetof(net_luid) == 8");
    TEST_CASE("TC-ABI-022: sizeof(AVB_HWREG_ENTRY) == 16, sizeof(AVB_HW_SNAPSHOT_REQUEST) == 24 + 16 * AVB_HWREG_COUNT");
    TEST_ASSERT(sizeof(AVB_HWREG_ENTRY) == 16, "sizeof(AVB_HWREG_ENTRY) == 16");
    TEST_ASSERT(sizeof(AVB_HW_SNAPSHOT_REQUEST) == 24 + 16 * AVB_HWREG_COUNT, "sizeof(AVB_HW_SNAPSHOT_REQUEST) == 280");
    TEST_ASSERT(offsetof(AVB_HW_SNAPSHOT_REQUEST, entries) == 24, "offsetof(entries) == 24");
    TEST_CASE("TC-ABI-023: HIL state IOCTL function numbers 68/69/70");
    TEST_ASSERT(IOCTL_AVB_GET_ADAPTER_IDENTITY == 0x00170110u, "IOCTL_AVB_GET_ADAPTER_IDENTITY == 0x00170110");
    TEST_ASSERT(IOCTL_AVB_OPEN_ADAPTER_BY_LUID == 0x00170114u, "IOCTL_AVB_OPEN_ADAPTER_BY_LUID == 0x00170114");
    TEST_ASSERT(IOCTL_AVB_QUERY_HW_SNAPSHOT == 0x00170118u, "IOCTL_AVB_QUERY_HW_SNAPSHOT == 0x00170118");
    TEST_CASE("TC-ABI-024: IOCTL_AVB_TAS_ACTIVATE function 71, write access, sizeof(AVB_TAS_ACTIVATE_REQUEST) == 80");
    /* A mutating operation: FILE_WRITE_ACCESS in CTL_CODE, so the I/O manager delivers it only on a handle opened for writing (#357, #362) */
    TEST_ASSERT(IOCTL_AVB_TAS_ACTIVATE == 0x0017811Cu, "IOCTL_AVB_TAS_ACTIVATE == 0x0017811C");
    TEST_ASSERT(((IOCTL_AVB_TAS_ACTIVATE >> 14) & 3u) == FILE_WRITE_ACCESS, "IOCTL_AVB_TAS_ACTIVATE requires FILE_WRITE_ACCESS");
    TEST_ASSERT(((IOCTL_AVB_TAS_ACTIVATE >> 2) & 0xFFFu) == 71u && (IOCTL_AVB_TAS_ACTIVATE >> 16) == FILE_DEVICE_PHYSICAL_NETCARD, "function 71, device type unchanged");
    TEST_ASSERT(((IOCTL_AVB_SETUP_TAS >> 14) & 3u) == FILE_ANY_ACCESS, "IOCTL_AVB_SETUP_TAS keeps FILE_ANY_ACCESS: existing codes unchanged");
    TEST_ASSERT(sizeof(AVB_TAS_ACTIVATE_REQUEST) == 80, "sizeof(AVB_TAS_ACTIVATE_REQUEST) == 80");
    TEST_ASSERT(offsetof(AVB_TAS_ACTIVATE_REQUEST, base_time_ns) == 8, "offsetof(base_time_ns) == 8");
    TEST_ASSERT(offsetof(AVB_TAS_ACTIVATE_REQUEST, phc_at_commit_ns) == 64, "offsetof(phc_at_commit_ns) == 64");
    TEST_ASSERT(offsetof(AVB_TAS_ACTIVATE_REQUEST, result) == 72, "offsetof(result) == 72");
    /* Verifies: #350 (REQ-F-TAS-STATE-001). The read-only TAS hardware query, allocated by #362 (ADR-TSN-002, accepted)
     * decision 1: CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 0x80B, METHOD_BUFFERED, FILE_READ_ACCESS) = 0x0017602C, in the
     * vendor range 0x800-0xFFF. FILE_READ_ACCESS (not FILE_ANY_ACCESS) states the read-only contract in the code: the
     * I/O manager serves it only on a handle holding read access. Only the code is pinned here; the request layout is
     * provisional until DES-C-TAS-010 (#365) is accepted (#362 decision 1). */
    TEST_CASE("TC-ABI-025: IOCTL_AVB_TAS_QUERY_HW function 0x80B, read access, buffered (#350, #362)");
    TEST_ASSERT(IOCTL_AVB_TAS_QUERY_HW == 0x0017602Cu, "IOCTL_AVB_TAS_QUERY_HW == 0x0017602C");
    TEST_ASSERT(((IOCTL_AVB_TAS_QUERY_HW >> 14) & 3u) == FILE_READ_ACCESS, "IOCTL_AVB_TAS_QUERY_HW requires FILE_READ_ACCESS");
    TEST_ASSERT((IOCTL_AVB_TAS_QUERY_HW & 3u) == METHOD_BUFFERED, "IOCTL_AVB_TAS_QUERY_HW is METHOD_BUFFERED");
    TEST_ASSERT(((IOCTL_AVB_TAS_QUERY_HW >> 2) & 0xFFFu) == 0x80Bu && (IOCTL_AVB_TAS_QUERY_HW >> 16) == FILE_DEVICE_PHYSICAL_NETCARD,
                "function 0x80B (vendor range), device type unchanged");

    /* Verifies: #350 (REQ-F-TAS-STATE-001). The v1 query wire layout published in #365 ("Query v1 contract", 12.2):
     * 536 bytes, explicit padding only, identical on x86 and x64. Provisional per #362 decision 1 until #365 is accepted:
     * a change here is a version change (AVB_TAS_QUERY_VERSION), never a silent re-layout. #365 Amendment 1 (12.10)
     * raised the version to 2 for a semantic change of hw_read PARTIAL; layout, size and enum values are unchanged.
     * Offline layout only; it proves nothing about the driver's behaviour. */
    TEST_CASE("TC-ABI-026: AVB_TAS_QUERY_HW_REQUEST v1 layout, 536 bytes (#350, #365 12.2)");
    TEST_ASSERT(AVB_TAS_QUERY_VERSION == 2u, "AVB_TAS_QUERY_VERSION == 2 (#365 Amendment 1)");
    TEST_ASSERT(sizeof(AVB_TAS_QUEUE_WINDOW) == 16, "sizeof(AVB_TAS_QUEUE_WINDOW) == 16");
    TEST_ASSERT(sizeof(AVB_TAS_SCHEDULE) == 80, "sizeof(AVB_TAS_SCHEDULE) == 80");
    TEST_ASSERT(sizeof(AVB_TAS_REGS) == 80, "sizeof(AVB_TAS_REGS) == 80");
    TEST_ASSERT(sizeof(AVB_TAS_LIMITS) == 48, "sizeof(AVB_TAS_LIMITS) == 48");
    TEST_ASSERT(sizeof(AVB_TAS_OP_RECORD) == 72, "sizeof(AVB_TAS_OP_RECORD) == 72");
    TEST_ASSERT(sizeof(AVB_TAS_COMMIT_INFO) == 24, "sizeof(AVB_TAS_COMMIT_INFO) == 24");
    TEST_ASSERT(sizeof(AVB_TAS_QUERY_HW_REQUEST) == 536, "sizeof(AVB_TAS_QUERY_HW_REQUEST) == 536");
    {
        static const struct { size_t actual, expected; const char *name; } off[] = {
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, version),             0, "version" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, size),                4, "size" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, lookup_client_op_id), 8, "lookup_client_op_id" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, net_luid),           16, "net_luid" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, vendor_id),          24, "vendor_id" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, device_id),          26, "device_id" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, revision_id),        28, "revision_id" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, revision_state),     30, "revision_state" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, hw_read),            32, "hw_read" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, hw_mode),            36, "hw_mode" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, origin),             40, "origin" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, gate_policy),        44, "gate_policy" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, current_status),     48, "current_status" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, current_reasons),    52, "current_reasons" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, next_status),        56, "next_status" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, next_reasons),       60, "next_reasons" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, snapshot_reasons),   64, "snapshot_reasons" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, restoration),        68, "restoration" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, flags),              72, "flags" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, lookup_result),      76, "lookup_result" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, link_speed_mbps),    80, "link_speed_mbps" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, legacy_conflicts),   84, "legacy_conflicts" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, context_epoch),      88, "context_epoch" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, generation),         96, "generation" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, phc_before_ns),     104, "phc_before_ns" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, phc_after_ns),      112, "phc_after_ns" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, dtx_max_frame_bytes), 120, "dtx_max_frame_bytes" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, dtx_max_frame_state), 124, "dtx_max_frame_state" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, limits),            128, "limits" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, current),           176, "current" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, next),              256, "next" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, regs),              336, "regs" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, lookup),            416, "lookup" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, current_commit),    488, "current_commit" },
            { offsetof(AVB_TAS_QUERY_HW_REQUEST, next_commit),       512, "next_commit" },
            { offsetof(AVB_TAS_SCHEDULE, base_time_ns),                0, "SCHEDULE.base_time_ns (retained target)" },
            { offsetof(AVB_TAS_SCHEDULE, cycle_ns),                    8, "SCHEDULE.cycle_ns" },
            { offsetof(AVB_TAS_SCHEDULE, queue),                      16, "SCHEDULE.queue" },
            { offsetof(AVB_TAS_REGS, stqt),                           20, "REGS.stqt" },
            { offsetof(AVB_TAS_REGS, endqt),                          36, "REGS.endqt" },
            { offsetof(AVB_TAS_REGS, txqctl),                         52, "REGS.txqctl" },
            { offsetof(AVB_TAS_REGS, dtxmxpktsz),                     68, "REGS.dtxmxpktsz" },
            { offsetof(AVB_TAS_REGS, read_failed),                    72, "REGS.read_failed" },
            { offsetof(AVB_TAS_REGS, consistency),                    76, "REGS.consistency" },
            { offsetof(AVB_TAS_LIMITS, min_margin_ns),                16, "LIMITS.min_margin_ns" },
            { offsetof(AVB_TAS_LIMITS, min_margin_state),             24, "LIMITS.min_margin_state" },
            { offsetof(AVB_TAS_LIMITS, tier_per_queue),               44, "LIMITS.tier_per_queue" },
            { offsetof(AVB_TAS_OP_RECORD, operation),                 48, "OP_RECORD.operation" },
            { offsetof(AVB_TAS_OP_RECORD, op_flags),                  68, "OP_RECORD.op_flags" },
            { offsetof(AVB_TAS_COMMIT_INFO, state),                   16, "COMMIT_INFO.state" },
        };
        char msg[96];
        size_t i;
        for (i = 0; i < sizeof(off) / sizeof(off[0]); i++) {
            (void)snprintf(msg, sizeof(msg), "offsetof(%s) == %u", off[i].name, (unsigned)off[i].expected);
            TEST_ASSERT(off[i].actual == off[i].expected, msg);
        }
    }

    /* Verifies: #350. The published values of #365 12.2 / 12.5. 0 is never a state of an enumerated field (U1); where 0 is
     * a meaningful count, "not reported" is 0xFFFFFFFF. HARDWARE_VERIFIED is defined, but no v1 path may return it (#381). */
    TEST_CASE("TC-ABI-027: TAS query v1 published values (#350, #365 12.2, 12.5)");
    TEST_ASSERT(AVB_TAS_HWREAD_READ == 1u && AVB_TAS_HWREAD_NOT_RUNNING == 2u && AVB_TAS_HWREAD_PARTIAL == 3u &&
                AVB_TAS_HWREAD_NO_MMIO == 4u, "hw_read READ 1, NOT_RUNNING 2, PARTIAL 3, NO_MMIO 4");
    TEST_ASSERT(AVB_TAS_HWMODE_OFF == 1u && AVB_TAS_HWMODE_TSN == 2u && AVB_TAS_HWMODE_UNREADABLE == 3u,
                "hw_mode OFF 1, TSN 2, UNREADABLE 3");
    TEST_ASSERT(AVB_TAS_ORIGIN_NONE == 1u && AVB_TAS_ORIGIN_SETUP_TAS == 2u && AVB_TAS_ORIGIN_CONTRACT == 3u &&
                AVB_TAS_ORIGIN_FOUND_AT_ATTACH == 4u, "origin NONE 1, SETUP_TAS 2, CONTRACT 3, FOUND_AT_ATTACH 4");
    TEST_ASSERT(AVB_TAS_GATE_NONE == 1u && AVB_TAS_GATE_SCHEDULED == 2u && AVB_TAS_GATE_OPEN_ALL == 3u,
                "gate_policy NONE 1, SCHEDULED 2, OPEN_ALL 3");
    TEST_ASSERT(AVB_TAS_ST_NONE == 1u && AVB_TAS_ST_PROGRAMMED == 2u && AVB_TAS_ST_PENDING == 3u &&
                AVB_TAS_ST_INFERRED_ACTIVE == 4u && AVB_TAS_ST_HARDWARE_VERIFIED == 5u && AVB_TAS_ST_INCONSISTENT == 6u &&
                AVB_TAS_ST_UNKNOWN == 7u, "status NONE 1 .. UNKNOWN 7, HARDWARE_VERIFIED 5");
    TEST_ASSERT(AVB_TAS_RESTORE_COMPLETE == 1u && AVB_TAS_RESTORE_PARTIAL_OPEN_ALL == 2u && AVB_TAS_RESTORE_PARTIAL_RESIDUE == 3u &&
                AVB_TAS_RESTORE_NONE == 4u && AVB_TAS_RESTORE_UNKNOWN == 5u && AVB_TAS_RESTORE_NO_SSOT_BASELINE == 6u,
                "restoration COMPLETE 1 .. NO_SSOT_BASELINE 6 (advisory)");
    TEST_ASSERT(AVB_TAS_FLAG_OP_IN_PROGRESS == 0x1u && AVB_TAS_FLAG_UNVERIFIED_CONFIG_ACTIVE == 0x2u &&
                AVB_TAS_FLAG_PHC_RESET_SEEN == 0x4u, "flags OP_IN_PROGRESS 0x1, UNVERIFIED_CONFIG_ACTIVE 0x2, PHC_RESET_SEEN 0x4");
    TEST_ASSERT(AVB_TAS_LOOKUP_NOT_REQUESTED == 1u && AVB_TAS_LOOKUP_FOUND == 2u && AVB_TAS_LOOKUP_NOT_FOUND == 3u &&
                AVB_TAS_LOOKUP_NOT_IMPLEMENTED == 4u, "lookup_result NOT_REQUESTED 1, FOUND 2, NOT_FOUND 3, NOT_IMPLEMENTED 4");
    TEST_ASSERT(AVB_TAS_DTX_DECODED == 1u && AVB_TAS_DTX_READ_FAILED == 2u && AVB_TAS_DTX_NO_SSOT_SCALE == 3u &&
                AVB_TAS_DTX_NOT_READ == 4u, "dtx_max_frame_state DECODED 1, READ_FAILED 2, NO_SSOT_SCALE 3, NOT_READ 4");
    TEST_ASSERT(AVB_TAS_REV_VALID == 1u, "revision_state VALID 1");
    TEST_ASSERT(AVB_TAS_CONS_BASET_TORN == 0x1u, "consistency BASET_TORN 0x1");
    TEST_ASSERT(AVB_TAS_NOT_REPORTED_U32 == 0xFFFFFFFFu, "not-reported counter 0xFFFFFFFF");
    TEST_ASSERT(AVB_TAS_TIER_HW_UNSUPPORTED == 1u && AVB_TAS_TIER_NOT_IMPLEMENTED == 2u &&
                AVB_TAS_TIER_IMPLEMENTED_UNVERIFIED == 3u && AVB_TAS_TIER_IMPLEMENTED_VERIFIED == 4u, "tiers 1..4");
    TEST_ASSERT(AVB_TAS_COMMIT_NONE == 1u && AVB_TAS_COMMIT_RETAINED == 2u && AVB_TAS_COMMIT_PHC_DISCONTINUOUS == 3u &&
                AVB_TAS_COMMIT_HW_MISMATCH == 4u && AVB_TAS_COMMIT_LOST == 5u, "commit state NONE 1 .. LOST 5");
    TEST_ASSERT(AVB_TAS_LIMIT_NOT_ESTABLISHED == 1u && AVB_TAS_LIMIT_ESTABLISHED == 2u, "limit state NOT_ESTABLISHED 1, ESTABLISHED 2");
    TEST_ASSERT(AVB_TAS_SNAP_RSN_NOT_RUNNING == 0x001u && AVB_TAS_SNAP_RSN_RESERVED_02 == 0x002u &&
                AVB_TAS_SNAP_RSN_READ_FAILED == 0x004u && AVB_TAS_SNAP_RSN_READ_INCONSISTENT == 0x008u &&
                AVB_TAS_SNAP_RSN_PHC_DISCONTINUITY == 0x010u && AVB_TAS_SNAP_RSN_EVENT_UNRECONCILED == 0x020u &&
                AVB_TAS_SNAP_RSN_NO_MMIO == 0x040u && AVB_TAS_SNAP_RSN_PHC_BEFORE_INVALID == 0x080u &&
                AVB_TAS_SNAP_RSN_PHC_AFTER_INVALID == 0x100u && AVB_TAS_SNAP_RSN_REG_UNMAPPED == 0x200u,
                "snapshot reasons 0x001 .. 0x200");
    TEST_ASSERT(AVB_TAS_SLOT_RSN_HW_ARMED_NO_RECORD == 0x0001u && AVB_TAS_SLOT_RSN_DRIVER_ARMED_HW_OFF == 0x0002u &&
                AVB_TAS_SLOT_RSN_BASE_FAR_AHEAD == 0x0004u && AVB_TAS_SLOT_RSN_WINDOW_MISMATCH == 0x0008u &&
                AVB_TAS_SLOT_RSN_CYCLE_MISMATCH == 0x0010u && AVB_TAS_SLOT_RSN_TXQCTL_MISMATCH == 0x0020u &&
                AVB_TAS_SLOT_RSN_LINK_SPEED_CHANGED == 0x0040u && AVB_TAS_SLOT_RSN_PHC_RESET_SINCE_COMMIT == 0x0080u &&
                AVB_TAS_SLOT_RSN_ACTIVATION_AMBIGUOUS == 0x0100u && AVB_TAS_SLOT_RSN_COMMIT_INFO_LOST == 0x0200u &&
                AVB_TAS_SLOT_RSN_TRANSITION_DURING_SNAPSHOT == 0x0400u && AVB_TAS_SLOT_RSN_LINK_EVENTS_UNOBSERVED == 0x0800u &&
                AVB_TAS_SLOT_RSN_SLOT_UNATTRIBUTED == 0x1000u, "slot reasons 0x0001 .. 0x1000");
}

int main(void)
{
    printf("=======================================================\n");
    printf("TEST-ABI-001: IOCTL ABI Stability Tests\n");
    printf("  Part of: Issue #265 (TEST-COVERAGE-001)\n");
    printf("  Verifies:   #24  (REQ-NF-SSOT-001: Single Source of Truth for IOCTL Interface)\n");
    printf("  Implements: #265 (TEST-COVERAGE-001: Verify Unit Test Coverage)\n");
    printf("=======================================================\n");

    test_ioctl_uniqueness();
    test_abi_version();
    test_struct_sizes();

    printf("\n=======================================================\n");
    printf("Results: %d/%d passed", g_results.passed, g_results.total);
    if (g_results.failed > 0) {
        printf(", %d FAILED", g_results.failed);
    }
    printf("\n=======================================================\n");

    return (g_results.failed > 0) ? 1 : 0;
}
