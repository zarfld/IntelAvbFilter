/**
 * @file test_ioctl_fp_ptm.c
 * @brief Frame Preemption (FP) and PTM Tests - Requirement #11
 * 
 * Test Suite for IEEE 802.1Qbu Frame Preemption and IEEE 802.3br IET
 * 
 * SSOT COMPLIANCE:
 * - Uses AVB_FP_REQUEST from avb_ioctl.h (wrapper with config + status)
 * - Uses tsn_fp_config from external/intel_avb/lib/intel.h (3 fields)
 * - Uses AVB_PTM_REQUEST from avb_ioctl.h (wrapper with config + status)
 * - Uses ptm_config from external/intel_avb/lib/intel.h (2 fields)
 * 
 * SSOT Structure Definitions (from intel.h lines 206-215):
 *   struct tsn_fp_config {
 *       uint8_t preemptable_queues;  // Bitmask: 0x01=queue 0, 0xFF=all queues
 *       uint32_t min_fragment_size;  // Minimum fragment size (64-256 bytes)
 *       uint8_t verify_disable;      // 0=enable verification, 1=disable
 *   };
 *   
 *   struct ptm_config {
 *       uint8_t enabled;             // 1=PTM enabled, 0=disabled
 *       uint32_t clock_granularity;  // Clock granularity (e.g., 16)
 *   };
 * 
 * Test Coverage (Issue #212):
 * - FP Configuration Tests (8): Queue masks, fragment sizes, verification modes
 * - PTM Configuration Tests (4): Enable/disable, clock granularity
 * - Error Handling Tests (3): Null buffers, invalid parameters
 * 
 * Related Issues: #212 (Test Issue), #11 (Requirement)
 * Reference: tests/integration/avb/avb_test_um.c fp_on/off, ptm_on/off (lines 249-254)
 * 
 * IOCTLs Tested:
 * - IOCTL 27: IOCTL_AVB_SETUP_FP (Frame Preemption)
 * - IOCTL 28: IOCTL_AVB_SETUP_PTM (Precision Time Measurement)
 * 
 * Standards: IEEE 802.1Qbu, IEEE 802.3br, IEEE 1012-2016
 * Build: cl test_ioctl_fp_ptm.c /Fe:test_ioctl_fp_ptm.exe /I include /I external/intel_avb/lib /link setupapi.lib
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <setupapi.h>
#include <initguid.h>

/* SSOT Structure Compliance */
#include "avb_ioctl.h"  /* AVB_FP_REQUEST, AVB_PTM_REQUEST */
#include "intel.h"      /* tsn_fp_config, ptm_config */

/* AVB Filter Device GUID (matches driver's device interface) */
DEFINE_GUID(GUID_DEVINTERFACE_AVB_FILTER,
    0x8e6f815c, 0x1e5c, 0x4c76, 0x97, 0x5f, 0x56, 0x7f, 0x0e, 0x62, 0x1d, 0x9a);

/* Test Results */
static int tests_passed = 0;
static int tests_failed = 0;

/* Device Handle */
static HANDLE g_hDevice = INVALID_HANDLE_VALUE;

/**
 * Open AVB device - tries symbolic link first (simpler), then SetupAPI enumeration
 */
static HANDLE OpenAvbDevice(void)
{
    HANDLE hDevice;
    
    /* Method 1: Try symbolic link (typical driver interface) */
    hDevice = CreateFileA("\\\\.\\IntelAvbFilter",
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    
    if (hDevice != INVALID_HANDLE_VALUE) {
        printf("[INFO] AVB device opened via symbolic link\n");
        return hDevice;
    }
    
    /* Method 2: Fallback to SetupAPI enumeration */
    HDEVINFO hDevInfo = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_AVB_FILTER,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        printf("[SKIP] No AVB interface found (SetupDiGetClassDevs failed: %lu)\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_AVB_FILTER, 0, &ifData)) {
        printf("[SKIP] No AVB interface found (SetupDiEnumDeviceInterfaces failed: %lu)\n", GetLastError());
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return INVALID_HANDLE_VALUE;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetail(hDevInfo, &ifData, NULL, 0, &requiredSize, NULL);

    PSP_DEVICE_INTERFACE_DETAIL_DATA pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
    if (!pDetail) {
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return INVALID_HANDLE_VALUE;
    }

    pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &ifData, pDetail, requiredSize, NULL, NULL)) {
        free(pDetail);
        SetupDiDestroyDeviceInfoList(hDevInfo);
        return INVALID_HANDLE_VALUE;
    }

    hDevice = CreateFile(
        pDetail->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hDevice != INVALID_HANDLE_VALUE) {
        printf("[INFO] AVB device opened via SetupAPI enumeration\n");
    }

    free(pDetail);
    SetupDiDestroyDeviceInfoList(hDevInfo);

    return hDevice;
}

/* ========================================================================
 * Frame Preemption (FP) Tests - IEEE 802.1Qbu
 * ======================================================================== */

/**
 * TC-FP-001: Basic FP Enable (Queue 0 Preemptable)
 * 
 * Reference: avb_test_um.c fp_on() (line 249)
 * - preemptable_queues = 0x01 (queue 0 only)
 * - min_fragment_size = 128 bytes
 * - verify_disable = 0 (enable verification)
 */
static void test_fp_basic_enable(void)
{
    printf("\n[TC-FP-001] Basic FP Enable (Queue 0 Preemptable)\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    /* SSOT: tsn_fp_config fields */
    fpReq.config.preemptable_queues = 0x01;  /* Queue 0 preemptable */
    fpReq.config.min_fragment_size = 128;    /* 128-byte fragments */
    fpReq.config.verify_disable = 0;         /* Enable verification */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result) {
        DWORD err = GetLastError();
        printf("[FAIL] DeviceIoControl failed (error %lu)\n", err);
        tests_failed++;
        return;
    }

    /* SSOT: Check status field for NDIS_STATUS_SUCCESS */
    if (fpReq.status != 0) {
        printf("[FAIL] IOCTL returned error status 0x%08lX\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] FP enabled (queue 0, 128-byte fragments, verify on)\n");
    tests_passed++;
}

/**
 * TC-FP-002: All Queues Preemptable
 * - preemptable_queues = 0xFF (all 8 queues)
 * - min_fragment_size = 64 bytes (minimum)
 * - verify_disable = 0
 */
static void test_fp_all_queues(void)
{
    printf("\n[TC-FP-002] All Queues Preemptable\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0xFF;  /* All queues preemptable */
    fpReq.config.min_fragment_size = 64;     /* Minimum fragment size */
    fpReq.config.verify_disable = 0;

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] FP all queues failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] All queues preemptable (64-byte fragments)\n");
    tests_passed++;
}

/**
 * TC-FP-003: FP Disable (All Express)
 * 
 * Reference: avb_test_um.c fp_off() (line 251)
 * - preemptable_queues = 0x00 (no queues preemptable)
 * - verify_disable = 1 (disable verification)
 */
static void test_fp_disable(void)
{
    printf("\n[TC-FP-003] FP Disable (All Express)\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0x00;  /* No queues preemptable */
    fpReq.config.verify_disable = 1;         /* Disable verification */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] FP disable failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] FP disabled (all queues express)\n");
    tests_passed++;
}

/**
 * TC-FP-004: Maximum Fragment Size
 * - min_fragment_size = 256 bytes (maximum)
 * - preemptable_queues = 0x03 (queues 0 and 1)
 */
static void test_fp_max_fragment_size(void)
{
    printf("\n[TC-FP-004] Maximum Fragment Size (256 bytes)\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0x03;  /* Queues 0 and 1 */
    fpReq.config.min_fragment_size = 256;    /* Maximum fragment size */
    fpReq.config.verify_disable = 0;

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] Max fragment size failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] 256-byte fragments configured\n");
    tests_passed++;
}

/**
 * TC-FP-005: High Priority Express (TC7)
 * - preemptable_queues = 0x7F (TC0-TC6 preemptable, TC7 express)
 * - min_fragment_size = 128 bytes
 */
static void test_fp_express_priority(void)
{
    printf("\n[TC-FP-005] High Priority Express (TC7)\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0x7F;  /* TC0-TC6 preemptable, TC7 express */
    fpReq.config.min_fragment_size = 128;
    fpReq.config.verify_disable = 0;

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] Express priority failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] TC7 express, TC0-TC6 preemptable\n");
    tests_passed++;
}

/**
 * TC-FP-006: Verification Disabled Mode
 * - verify_disable = 1 (skip 802.3br verification)
 * - preemptable_queues = 0x01
 */
static void test_fp_no_verify(void)
{
    printf("\n[TC-FP-006] Verification Disabled Mode\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0x01;
    fpReq.config.min_fragment_size = 128;
    fpReq.config.verify_disable = 1;  /* Disable verification */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] No-verify mode failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] Verification disabled\n");
    tests_passed++;
}

/**
 * TC-FP-007: Intermediate Fragment Size (192 bytes)
 */
static void test_fp_intermediate_fragment(void)
{
    printf("\n[TC-FP-007] Intermediate Fragment Size (192 bytes)\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0x01;
    fpReq.config.min_fragment_size = 192;  /* 192 bytes */
    fpReq.config.verify_disable = 0;

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] 192-byte fragments failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] 192-byte fragments configured\n");
    tests_passed++;
}

/**
 * TC-FP-008: Multiple Preemptable Queues (TC0, TC1, TC2)
 * - preemptable_queues = 0x07 (queues 0, 1, 2)
 */
static void test_fp_multiple_queues(void)
{
    printf("\n[TC-FP-008] Multiple Preemptable Queues (TC0-TC2)\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));
    
    fpReq.config.preemptable_queues = 0x07;  /* TC0, TC1, TC2 */
    fpReq.config.min_fragment_size = 128;
    fpReq.config.verify_disable = 0;

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq),
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (!result || fpReq.status != 0) {
        printf("[FAIL] Multiple queues failed (status 0x%08lX)\n", fpReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] TC0-TC2 preemptable configured\n");
    tests_passed++;
}

/* ========================================================================
 * Precision Time Measurement (PTM) Tests - IEEE 802.3br
 * ======================================================================== */

/**
 * TC-PTM-001: PTM Enable
 * 
 * Reference: avb_test_um.c ptm_on() (line 253)
 * - enabled = 1
 * - clock_granularity = 16
 */
static void test_ptm_enable(void)
{
    printf("\n[TC-PTM-001] PTM Enable\n");

    AVB_PTM_REQUEST ptmReq;
    ZeroMemory(&ptmReq, sizeof(ptmReq));
    
    /* SSOT: ptm_config fields */
    ptmReq.config.enabled = 1;               /* Enable PTM */
    ptmReq.config.clock_granularity = 16;    /* 16ns granularity */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_PTM,
        &ptmReq,
        sizeof(ptmReq),
        &ptmReq,
        sizeof(ptmReq),
        &bytesReturned,
        NULL
    );

    if (!result) {
        DWORD err = GetLastError();
        printf("[FAIL] DeviceIoControl failed (error %lu)\n", err);
        tests_failed++;
        return;
    }

    /* SSOT: Check status field */
    if (ptmReq.status != 0) {
        printf("[FAIL] IOCTL returned error status 0x%08lX\n", ptmReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] PTM enabled (16ns granularity)\n");
    tests_passed++;
}

/**
 * TC-PTM-002: PTM Disable
 * 
 * Reference: avb_test_um.c ptm_off() (line 255)
 * - enabled = 0
 */
static void test_ptm_disable(void)
{
    printf("\n[TC-PTM-002] PTM Disable\n");

    AVB_PTM_REQUEST ptmReq;
    ZeroMemory(&ptmReq, sizeof(ptmReq));
    
    ptmReq.config.enabled = 0;  /* Disable PTM */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_PTM,
        &ptmReq,
        sizeof(ptmReq),
        &ptmReq,
        sizeof(ptmReq),
        &bytesReturned,
        NULL
    );

    if (!result || ptmReq.status != 0) {
        printf("[FAIL] PTM disable failed (status 0x%08lX)\n", ptmReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] PTM disabled\n");
    tests_passed++;
}

/**
 * TC-PTM-003: Different Clock Granularity (32ns)
 */
static void test_ptm_granularity_32(void)
{
    printf("\n[TC-PTM-003] Clock Granularity (32ns)\n");

    AVB_PTM_REQUEST ptmReq;
    ZeroMemory(&ptmReq, sizeof(ptmReq));
    
    ptmReq.config.enabled = 1;
    ptmReq.config.clock_granularity = 32;  /* 32ns granularity */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_PTM,
        &ptmReq,
        sizeof(ptmReq),
        &ptmReq,
        sizeof(ptmReq),
        &bytesReturned,
        NULL
    );

    if (!result || ptmReq.status != 0) {
        printf("[FAIL] 32ns granularity failed (status 0x%08lX)\n", ptmReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] PTM enabled (32ns granularity)\n");
    tests_passed++;
}

/**
 * TC-PTM-004: Fine Granularity (8ns)
 */
static void test_ptm_granularity_8(void)
{
    printf("\n[TC-PTM-004] Fine Clock Granularity (8ns)\n");

    AVB_PTM_REQUEST ptmReq;
    ZeroMemory(&ptmReq, sizeof(ptmReq));
    
    ptmReq.config.enabled = 1;
    ptmReq.config.clock_granularity = 8;  /* 8ns granularity */

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_PTM,
        &ptmReq,
        sizeof(ptmReq),
        &ptmReq,
        sizeof(ptmReq),
        &bytesReturned,
        NULL
    );

    if (!result || ptmReq.status != 0) {
        printf("[FAIL] 8ns granularity failed (status 0x%08lX)\n", ptmReq.status);
        tests_failed++;
        return;
    }

    printf("[PASS] PTM enabled (8ns granularity)\n");
    tests_passed++;
}

/* ========================================================================
 * Error Handling Tests
 * ======================================================================== */

/**
 * TC-FP-ERR-001: Null Input Buffer
 */
static void test_fp_null_buffer(void)
{
    printf("\n[TC-FP-ERR-001] Null Input Buffer\n");

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        NULL,  /* Null input */
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );

    if (result) {
        printf("[FAIL] Accepted null buffer (should reject)\n");
        tests_failed++;
        return;
    }

    DWORD err = GetLastError();
    if (err != ERROR_INVALID_PARAMETER && err != ERROR_INSUFFICIENT_BUFFER) {
        printf("[FAIL] Wrong error code (got %lu, expected %lu or %lu)\n", 
               err, ERROR_INVALID_PARAMETER, ERROR_INSUFFICIENT_BUFFER);
        tests_failed++;
        return;
    }

    printf("[PASS] Null buffer rejected (error %lu)\n", err);
    tests_passed++;
}

/**
 * TC-PTM-ERR-001: Null Input Buffer
 */
static void test_ptm_null_buffer(void)
{
    printf("\n[TC-PTM-ERR-001] Null Input Buffer\n");

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_PTM,
        NULL,  /* Null input */
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );

    if (result) {
        printf("[FAIL] Accepted null buffer (should reject)\n");
        tests_failed++;
        return;
    }

    DWORD err = GetLastError();
    if (err != ERROR_INVALID_PARAMETER && err != ERROR_INSUFFICIENT_BUFFER) {
        printf("[FAIL] Wrong error code (got %lu)\n", err);
        tests_failed++;
        return;
    }

    printf("[PASS] Null buffer rejected (error %lu)\n", err);
    tests_passed++;
}

/**
 * TC-FP-ERR-002: Buffer Too Small
 */
static void test_fp_small_buffer(void)
{
    printf("\n[TC-FP-ERR-002] Buffer Too Small\n");

    AVB_FP_REQUEST fpReq;
    ZeroMemory(&fpReq, sizeof(fpReq));

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_SETUP_FP,
        &fpReq,
        sizeof(fpReq) - 1,  /* Too small */
        &fpReq,
        sizeof(fpReq),
        &bytesReturned,
        NULL
    );

    if (result) {
        printf("[FAIL] Accepted small buffer (should reject)\n");
        tests_failed++;
        return;
    }

    DWORD err = GetLastError();
    if (err != ERROR_INVALID_PARAMETER && err != ERROR_INSUFFICIENT_BUFFER) {
        printf("[FAIL] Wrong error code (got %lu)\n", err);
        tests_failed++;
        return;
    }

    printf("[PASS] Small buffer rejected (error %lu)\n", err);
    tests_passed++;
}

/* ========================================================================
 * Main Test Runner
 * ======================================================================== */

int main(void)
{
    printf("=======================================================================\n");
    printf(" Frame Preemption & PTM Tests - Requirement #11 (IEEE 802.1Qbu/802.3br)\n");
    printf("=======================================================================\n");
    printf(" SSOT Structures: AVB_FP_REQUEST + tsn_fp_config, AVB_PTM_REQUEST + ptm_config\n");
    printf(" Test Issue: #212 (15 test cases)\n");
    printf("=======================================================================\n");

    /* Open AVB device */
    g_hDevice = OpenAvbDevice();
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        printf("[FATAL] Cannot open AVB device - all tests skipped\n");
        return 1;
    }

    printf("[INFO] AVB device opened successfully\n");

    /* Run Frame Preemption tests */
    printf("\n--- Frame Preemption (FP) Tests ---\n");
    test_fp_basic_enable();
    test_fp_all_queues();
    test_fp_disable();
    test_fp_max_fragment_size();
    test_fp_express_priority();
    test_fp_no_verify();
    test_fp_intermediate_fragment();
    test_fp_multiple_queues();

    /* Run PTM tests */
    printf("\n--- Precision Time Measurement (PTM) Tests ---\n");
    test_ptm_enable();
    test_ptm_disable();
    test_ptm_granularity_32();
    test_ptm_granularity_8();

    /* Run error handling tests */
    printf("\n--- Error Handling Tests ---\n");
    test_fp_null_buffer();
    test_ptm_null_buffer();
    test_fp_small_buffer();

    /* Close device */
    CloseHandle(g_hDevice);

    /* Summary */
    printf("\n=======================================================================\n");
    printf(" Test Summary\n");
    printf("=======================================================================\n");
    printf(" Total Tests:  %d\n", tests_passed + tests_failed);
    printf(" Passed:       %d\n", tests_passed);
    printf(" Failed:       %d\n", tests_failed);
    printf(" Pass Rate:    %.1f%%\n", 
           (tests_passed + tests_failed > 0) 
           ? (100.0 * tests_passed / (tests_passed + tests_failed)) 
           : 0.0);
    printf("=======================================================================\n");

    return (tests_failed > 0) ? 1 : 0;
}
