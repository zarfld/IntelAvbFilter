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
 *   TC-ABI-011: sizeof(AVB_TX_TIMESTAMP_REQUEST) == 24  (uint64 + 4 x uint32)
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
    TEST_ASSERT(AVB_IOCTL_ABI_VERSION == 0x00020000u,
                "AVB_IOCTL_ABI_VERSION == 0x00020000 (v2.0 - stats struct extended to 192 bytes)");
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
    /* struct { uint64 timestamp_ns; uint32 x4 }                              */
    /* uint64@0(8) + 4x uint32@8 (16) = 24, aligned to 8.                    */
    TEST_CASE("TC-ABI-011: sizeof(AVB_TX_TIMESTAMP_REQUEST) == 24");
    TEST_ASSERT(sizeof(AVB_TX_TIMESTAMP_REQUEST) == 24,
                "sizeof(AVB_TX_TIMESTAMP_REQUEST) == 24  (uint64 + 4 x uint32)");

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
