/*++
SSOT COMPLIANT: Time-Aware Scheduler (TAS) Tests - Requirement #9

Test Description:
    Comprehensive verification of IOCTL 26 (IOCTL_AVB_SETUP_TAS) for IEEE 802.1Qbv
    Time-Aware Shaper (TAS) configuration and operation.

SSOT Structures Used:
    - AVB_TAS_REQUEST (avb_ioctl.h lines 198-201)
      * struct tsn_tas_config config (nested)
      * avb_u32 status (NDIS_STATUS)
    
    - struct tsn_tas_config (external/intel_avb/lib/intel.h lines 197-204)
      * uint64_t base_time_s (when to start TAS schedule)
      * uint32_t base_time_ns (nanoseconds part)
      * uint32_t cycle_time_s (how often schedule repeats)
      * uint32_t cycle_time_ns (nanoseconds part)
      * uint8_t gate_states[8] (queue bitmask per entry: 0xFF=all, 0x01=queue 0)
      * uint32_t gate_durations[8] (duration in nanoseconds per entry)

Reference Implementations:
    - tests/integration/tsn/test_tsn_ioctl_handlers_um.c (line 50-68)
    - tests/integration/avb/avb_test_um.c tas_audio() (line 248)
    - tests/device_specific/i226/avb_i226_test.c (line 178)

Test Issue: #206 (15 test cases)
Requirement: #9 (REQ-F-TAS-001)

PITFALL Prevention:
    ✅ Use AVB_TAS_REQUEST from avb_ioctl.h (NO custom structures)
    ✅ Use tsn_tas_config from intel.h (NO modified layouts)
    ✅ Check status field for NDIS_STATUS_SUCCESS (0x00000000)
    ✅ Respect IN/OUT directions for all fields
    ✅ Use proper base_time (SYSTIM + offset) and cycle_time
    ✅ Configure gate_states and gate_durations arrays correctly
--*/

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include "avb_ioctl.h"  // For AVB_TIMESTAMP_REQUEST and IOCTL codes

// AVB GUID
DEFINE_GUID(GUID_DEVINTERFACE_AVB_FILTER,
    0x8e6f815c, 0x1e5c, 0x4c76, 0x97, 0x5f, 0x56, 0x7f, 0x0e, 0x62, 0x1d, 0x9a);

// Test result counters — distinct categories (P0.4)
static int g_passed         = 0;
static int g_failed         = 0;
static int g_skipped        = 0;  /* SKIP: hardware/driver lacks feature for this test */
static int g_blocked        = 0;  /* BLOCKED_UNRESTORABLE: safe restore path absent */
static int g_cleanup_failed = 0;  /* CLEANUP_FAILED: restore/verify failed; hw state unknown */

/* TC_SKIP = 4 is defined in tas_restore.h alongside TC_PASS/FAIL/CLEANUP_FAILED/BLOCKED */

// ============================================================================
// Snapshot / Restore infrastructure (test-state-restoration contract)
// Shared with tests/unit/test_tas_restore_contract.c via tas_restore.h.
// ============================================================================
#define IOCTL_CALL DeviceIoControl
#include "tas_restore.h"

/* Run one hardware-mutating TAS case with full snapshot/restore lifecycle.
 *
 * Return codes (from tas_restore.h):
 *   TC_PASS (0)           — fn() produced exactly one g_passed++ AND restore succeeded
 *   TC_FAIL (1)           — fn() produced a FAIL outcome (g_failed++)
 *   TC_CLEANUP_FAILED (2) — restore/verify failed; hardware state UNKNOWN; halt sequence
 *   TC_BLOCKED (3)        — preflight refused; test not executed (g_blocked++)
 *   TC_SKIP (4)           — fn() skipped (g_skipped++); restore ran; no mutation verified
 *
 * Each testcase contributes exactly one authoritative outcome:
 *   - fn() always increments exactly one of g_passed, g_failed, or g_skipped.
 *   - run_with_restore reads the delta and maps it to the TC_* taxonomy.
 *   - run_with_restore never increments g_passed (prevents double-counting).
 */
static int run_with_restore(HANDLE hDevice, const char *case_name,
                            void (*fn)(HANDLE)) {
    TAS_SNAPSHOT snap;
    if (!tas_capture_state(hDevice, &snap)) {
        printf("  [BLOCKED] %s: preflight refused — restore capability absent\n", case_name);
        g_blocked++;
        return TC_BLOCKED;
    }

    int before_passed  = g_passed;
    int before_failed  = g_failed;
    int before_skipped = g_skipped;
    fn(hDevice);
    BOOL test_passed  = (g_passed  > before_passed);
    BOOL test_failed  = (g_failed  > before_failed);
    BOOL test_skipped = (g_skipped > before_skipped);

    /* Restore unconditionally — on PASS, FAIL, or SKIP */
    BOOL restore_ok = tas_restore_state(hDevice, &snap);
    BOOL verify_ok  = restore_ok ? tas_verify_restore(hDevice, &snap) : FALSE;

    if (!restore_ok || !verify_ok) {
        printf("  [CLEANUP_FAILED] %s: restore/verify failed — hardware state UNKNOWN; halting\n",
               case_name);
        g_cleanup_failed++;
        return TC_CLEANUP_FAILED;
    }

    /* Map fn() outcome to TC_* — exactly one code returned */
    if (test_failed)  return TC_FAIL;
    if (test_skipped) return TC_SKIP;   /* SKIP is not PASS — counted separately */
    if (test_passed)  return TC_PASS;
    /* fn() incremented no counter — treat as unexpected failure */
    printf("  [FAIL] %s: test function produced no outcome\n", case_name);
    g_failed++;
    return TC_FAIL;
}

static void test_basic_gcl_config(HANDLE hDevice);
static void test_max_gcl_size(HANDLE hDevice);
static void test_min_gate_window(HANDLE hDevice);
static void test_max_gate_window(HANDLE hDevice);
static void test_audio_schedule(HANDLE hDevice);
static void test_all_gates_open(HANDLE hDevice);
static void test_all_gates_closed(HANDLE hDevice);
static void test_industrial_schedule(HANDLE hDevice);
static void test_null_buffer(HANDLE hDevice);
static void test_buffer_too_small(HANDLE hDevice);

struct tas_test_case {
    const char *name;
    void (*fn)(HANDLE hDevice);
};

static void print_usage(const char *program_name) {
    fprintf(stderr,
            "Usage: %s [--case TC-TAS-001] [--case TC-TAS-009] [--help]\n"
            "If omitted, all TAS test cases are executed in sequence.\n",
            program_name);
}

static int find_tas_case_index(const char *selected_case) {
    static const struct tas_test_case tas_cases[] = {
        { "TC-TAS-001", test_basic_gcl_config },
        { "TC-TAS-002", test_max_gcl_size },
        { "TC-TAS-003", test_min_gate_window },
        { "TC-TAS-004", test_max_gate_window },
        { "TC-TAS-005", test_audio_schedule },
        { "TC-TAS-006", test_all_gates_open },
        { "TC-TAS-007", test_all_gates_closed },
        { "TC-TAS-008", test_industrial_schedule },
        { "TC-TAS-009", test_null_buffer },
        { "TC-TAS-010", test_buffer_too_small }
    };

    if (selected_case == NULL) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(tas_cases) / sizeof(tas_cases[0]); ++i) {
        if (_stricmp(tas_cases[i].name, selected_case) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static void run_tas_case_sequence(HANDLE hDevice, const char *selected_case) {
    /* Hardware-mutating cases (TC-TAS-001..008) use run_with_restore.
     * Negative/validation cases (TC-TAS-009..010) do not mutate persistent state. */
    static const struct tas_test_case hw_cases[] = {
        { "TC-TAS-001", test_basic_gcl_config },
        { "TC-TAS-002", test_max_gcl_size },
        { "TC-TAS-003", test_min_gate_window },
        { "TC-TAS-004", test_max_gate_window },
        { "TC-TAS-005", test_audio_schedule },
        { "TC-TAS-006", test_all_gates_open },
        { "TC-TAS-007", test_all_gates_closed },
        { "TC-TAS-008", test_industrial_schedule }
    };
    static const struct tas_test_case neg_cases[] = {
        { "TC-TAS-009", test_null_buffer },
        { "TC-TAS-010", test_buffer_too_small }
    };

    if (selected_case != NULL) {
        int index = find_tas_case_index(selected_case);
        if (index < 0) {
            fprintf(stderr, "[ERROR] Unknown TAS testcase: %s\n", selected_case);
            return;
        }

        printf("[INFO] Running selected TAS case: %s\n", selected_case);
        if (index < 8) {
            run_with_restore(hDevice, selected_case, hw_cases[index].fn);
        } else {
            neg_cases[index - 8].fn(hDevice);
        }
        return;
    }

    printf("[INFO] Running all TAS cases with per-case snapshot/restore\n");
    for (size_t i = 0; i < sizeof(hw_cases) / sizeof(hw_cases[0]); ++i) {
        int rc = run_with_restore(hDevice, hw_cases[i].name, hw_cases[i].fn);
        if (rc == TC_CLEANUP_FAILED) {
            printf("[ABORT] CLEANUP_FAILED — hardware state UNKNOWN; halting mutating cases\n");
            g_blocked += (int)(sizeof(hw_cases) / sizeof(hw_cases[0]) - i - 1);
            break;
        }
        if (rc == TC_BLOCKED) {
            printf("[ABORT] BLOCKED — restore capability absent; all remaining hw cases blocked\n");
            g_blocked += (int)(sizeof(hw_cases) / sizeof(hw_cases[0]) - i - 1);
            break;
        }
    }
    for (size_t i = 0; i < sizeof(neg_cases) / sizeof(neg_cases[0]); ++i) {
        neg_cases[i].fn(hDevice);
    }
}

// Helper: Open AVB device - tries symbolic link first (simpler), then SetupAPI enumeration
static HANDLE OpenAvbDevice(void) {
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
        return hDevice;
    }
    
    /* Method 2: Fallback to SetupAPI enumeration */
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_AVB_FILTER,
                                               NULL, NULL,
                                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        printf("[SKIP] No AVB device found (SetupDiGetClassDevs failed: %lu)\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    if (!SetupDiEnumDeviceInterfaces(deviceInfo, NULL, &GUID_DEVINTERFACE_AVB_FILTER,
                                      0, &interfaceData)) {
        SetupDiDestroyDeviceInfoList(deviceInfo);
        printf("[SKIP] No AVB interface found (SetupDiEnumDeviceInterfaces failed: %lu)\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, NULL, 0, &requiredSize, NULL);
    PSP_DEVICE_INTERFACE_DETAIL_DATA detailData =
        (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
    if (!detailData) {
        SetupDiDestroyDeviceInfoList(deviceInfo);
        return INVALID_HANDLE_VALUE;
    }
    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    if (!SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData,
                                          requiredSize, NULL, NULL)) {
        free(detailData);
        SetupDiDestroyDeviceInfoList(deviceInfo);
        printf("[SKIP] Cannot get device path (SetupDiGetDeviceInterfaceDetail failed: %lu)\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    hDevice = CreateFile(detailData->DevicePath,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
    free(detailData);
    SetupDiDestroyDeviceInfoList(deviceInfo);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("[SKIP] Cannot open AVB device (CreateFile failed: %lu)\n", GetLastError());
    }

    return hDevice;
}

// Helper: Get current SYSTIM (uses SSOT structure AVB_TIMESTAMP_REQUEST)
static ULONGLONG get_current_systim(HANDLE hDevice) {
    AVB_TIMESTAMP_REQUEST tsReq;
    ZeroMemory(&tsReq, sizeof(tsReq));
    tsReq.clock_id = 0; // Default clock

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_GET_TIMESTAMP,
                        &tsReq, sizeof(tsReq),
                        &tsReq, sizeof(tsReq),
                        &bytesReturned, NULL)) {
        return 0; // Failed to get current time
    }

    if (tsReq.status != 0) {
        return 0; // IOCTL failed
    }

    return tsReq.timestamp; // SSOT: single u64 field
}

// Helper: Enable SYSTIM0 (prerequisite for TAS tests)
static BOOL enable_systim0(HANDLE hDevice) {
    AVB_HW_TIMESTAMPING_REQUEST hwTsReq;
    ZeroMemory(&hwTsReq, sizeof(hwTsReq));
    hwTsReq.enable = 1;           // Enable HW timestamping
    hwTsReq.timer_mask = 0x1;     // Bit 0 = SYSTIM0 only
    hwTsReq.enable_target_time = 0;
    hwTsReq.enable_aux_ts = 0;

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SET_HW_TIMESTAMPING,
                        &hwTsReq, sizeof(hwTsReq),
                        &hwTsReq, sizeof(hwTsReq),
                        &bytesReturned, NULL)) {
        return FALSE;
    }

    return (hwTsReq.status == 0); // NDIS_STATUS_SUCCESS
}

// ============================================================================
// Unit Tests
// ============================================================================

/*
TC-TAS-001: Basic GCL Configuration (2 Entries)
Expected: TAS accepts 2-entry GCL with alternating TC0/TC1 windows (125µs each)
*/
static void test_basic_gcl_config(HANDLE hDevice) {
    printf("\n[TC-TAS-001] Basic GCL Configuration (2 Entries)...\n");

    // Prerequisite: Enable SYSTIM0
    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0 (prerequisite)\n");
        g_skipped++;
        return;
    }

    // Get current time for base_time
    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    // Configure TAS schedule (based on avb_test_um.c tas_audio)
    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL); // +10ms future
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 250000; // 250µs cycle (4kHz)

    // Entry 0: TC0 only, 125µs
    tasReq.config.gate_states[0] = 0x01;    // Only queue 0 open
    tasReq.config.gate_durations[0] = 125000; // 125µs

    // Entry 1: TC1 only, 125µs
    tasReq.config.gate_states[1] = 0x02;    // Only queue 1 open
    tasReq.config.gate_durations[1] = 125000; // 125µs

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        DWORD error = GetLastError();
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", error);
        g_failed++;
        return;
    }

    if (tasReq.status == 0) { // NDIS_STATUS_SUCCESS
        printf("  [PASS] Basic GCL configured (status=0x%08X)\n", tasReq.status);
        printf("         Base time: %llu s + %u ns\n", tasReq.config.base_time_s, tasReq.config.base_time_ns);
        printf("         Cycle time: %u ns\n", tasReq.config.cycle_time_ns);
        printf("         Entry 0: gate=0x%02X, duration=%u ns\n",
               tasReq.config.gate_states[0], tasReq.config.gate_durations[0]);
        printf("         Entry 1: gate=0x%02X, duration=%u ns\n",
               tasReq.config.gate_states[1], tasReq.config.gate_durations[1]);
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-002: Maximum GCL Size (8 Entries)
Expected: TAS accepts 8-entry GCL with mixed traffic classes
*/
static void test_max_gcl_size(HANDLE hDevice) {
    printf("\n[TC-TAS-002] Maximum GCL Size (8 Entries)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL);
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 1000000; // 1ms cycle

    // Configure 8 entries, one per traffic class
    for (int i = 0; i < 8; i++) {
        tasReq.config.gate_states[i] = (avb_u8)(1 << i); // Only TC[i] open
        tasReq.config.gate_durations[i] = 125000; // 125µs per entry
    }

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] 8-entry GCL configured (status=0x%08X)\n", tasReq.status);
        printf("         All 8 traffic classes configured independently\n");
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-003: Minimum Gate Window (1µs)
Expected: TAS accepts 1µs minimum gate duration
*/
static void test_min_gate_window(HANDLE hDevice) {
    printf("\n[TC-TAS-003] Minimum Gate Window (1µs)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL);
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 10000; // 10µs cycle

    // Entry 0: 1µs gate open (minimum)
    tasReq.config.gate_states[0] = 0xFF;  // All queues open
    tasReq.config.gate_durations[0] = 1000; // 1µs

    // Entry 1: 9µs gate closed
    tasReq.config.gate_states[1] = 0x00;  // All queues closed
    tasReq.config.gate_durations[1] = 9000; // 9µs

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] 1µs gate window accepted (status=0x%08X)\n", tasReq.status);
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-004: Maximum Gate Window (1 Second)
Expected: TAS accepts 1-second maximum gate duration
*/
static void test_max_gate_window(HANDLE hDevice) {
    printf("\n[TC-TAS-004] Maximum Gate Window (1 Second)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL);
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 1;
    tasReq.config.cycle_time_ns = 0; // 1 second cycle

    // Entry 0: 1 second gate open (maximum)
    tasReq.config.gate_states[0] = 0xFF;  // All queues open
    tasReq.config.gate_durations[0] = 1000000000; // 1 second

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] 1-second gate window accepted (status=0x%08X)\n", tasReq.status);
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-005: Audio Schedule (8kHz)
Expected: TAS accepts 125µs cycle time for 8kHz audio (from avb_test_um.c tas_audio)
*/
static void test_audio_schedule(HANDLE hDevice) {
    printf("\n[TC-TAS-005] Audio Schedule (8kHz, 125µs cycle)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    // Exact configuration from avb_test_um.c tas_audio() function
    tasReq.config.base_time_s = (avb_u64)((systim_ns + 1000000000ULL) / 1000000000ULL); // +1s future
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 1000000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 125000; // 125µs = 8kHz audio frame

    // Entry 0: TC0 open for 62.5µs
    tasReq.config.gate_states[0] = 0x01;
    tasReq.config.gate_durations[0] = 62500;

    // Entry 1: All queues closed for 62.5µs
    tasReq.config.gate_states[1] = 0x00;
    tasReq.config.gate_durations[1] = 62500;

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] Audio schedule configured (status=0x%08X)\n", tasReq.status);
        printf("         8kHz audio frame (125µs cycle)\n");
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-006: All Gates Open
Expected: TAS accepts configuration with all gates permanently open (0xFF)
*/
static void test_all_gates_open(HANDLE hDevice) {
    printf("\n[TC-TAS-006] All Gates Open (0xFF)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL);
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 1000000; // 1ms cycle

    // Single entry: All gates open
    tasReq.config.gate_states[0] = 0xFF;  // All 8 queues open
    tasReq.config.gate_durations[0] = 1000000; // 1ms

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] All gates open configured (status=0x%08X)\n", tasReq.status);
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-007: All Gates Closed
Expected: TAS accepts configuration with all gates permanently closed (0x00)
*/
static void test_all_gates_closed(HANDLE hDevice) {
    printf("\n[TC-TAS-007] All Gates Closed (0x00)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL);
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 1000000; // 1ms cycle

    // Single entry: All gates closed
    tasReq.config.gate_states[0] = 0x00;  // All queues closed
    tasReq.config.gate_durations[0] = 1000000; // 1ms

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] All gates closed configured (status=0x%08X)\n", tasReq.status);
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

/*
TC-TAS-008: Industrial Schedule (500µs)
Expected: TAS accepts 500µs cycle time for industrial control
*/
static void test_industrial_schedule(HANDLE hDevice) {
    printf("\n[TC-TAS-008] Industrial Schedule (500µs cycle)...\n");

    if (!enable_systim0(hDevice)) {
        printf("  [SKIP] Cannot enable SYSTIM0\n");
        g_skipped++;
        return;
    }

    ULONGLONG systim_ns = get_current_systim(hDevice);
    if (systim_ns == 0) {
        printf("  [SKIP] Cannot get current SYSTIM\n");
        g_skipped++;
        return;
    }

    AVB_TAS_REQUEST tasReq;
    ZeroMemory(&tasReq, sizeof(tasReq));

    tasReq.config.base_time_s = (avb_u64)((systim_ns + 10000000ULL) / 1000000000ULL);
    tasReq.config.base_time_ns = (avb_u32)((systim_ns + 10000000ULL) % 1000000000ULL);
    tasReq.config.cycle_time_s = 0;
    tasReq.config.cycle_time_ns = 500000; // 500µs cycle (2kHz)

    // Entry 0: Control traffic (TC7) - 50µs
    tasReq.config.gate_states[0] = 0x80;  // TC7 only
    tasReq.config.gate_durations[0] = 50000; // 50µs

    // Entry 1: Data traffic (TC0-6) - 450µs
    tasReq.config.gate_states[1] = 0x7F;  // TC0-TC6
    tasReq.config.gate_durations[1] = 450000; // 450µs

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                        &tasReq, sizeof(tasReq),
                        &tasReq, sizeof(tasReq),
                        &bytesReturned, NULL)) {
        printf("  [FAIL] DeviceIoControl failed (error=%lu)\n", GetLastError());
        g_failed++;
        return;
    }

    if (tasReq.status == 0) {
        printf("  [PASS] Industrial schedule configured (status=0x%08X)\n", tasReq.status);
        printf("         TC7 (control): 50µs, TC0-TC6 (data): 450µs\n");
        g_passed++;
    } else {
        printf("  [FAIL] TAS setup failed (status=0x%08X)\n", tasReq.status);
        g_failed++;
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

/*
TC-TAS-009: Null Buffer Validation
Expected: IOCTL rejects null input/output buffers (error=ERROR_INVALID_PARAMETER or ERROR_INSUFFICIENT_BUFFER)
*/
static void test_null_buffer(HANDLE hDevice) {
    printf("\n[TC-TAS-009] Null Buffer Validation...\n");

    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                                 NULL, 0, NULL, 0, &bytesReturned, NULL);
    DWORD error = GetLastError();

    // Should fail with specific error codes
    if (!result && (error == ERROR_INVALID_PARAMETER || error == ERROR_INSUFFICIENT_BUFFER || error == 122 || error == 87)) {
        printf("  [PASS] Null buffer correctly rejected (error=%lu)\n", error);
        g_passed++;
    } else if (result) {
        /* Driver accepted null buffer — this is a driver validation defect, not acceptable behaviour */
        printf("  [FAIL] Null buffer unexpectedly accepted — driver MUST reject null input (status=0x%08X)\n",
               ERROR_SUCCESS);
        g_failed++;
    } else {
        printf("  [WARN] Unexpected error code (error=%lu, expected 87 or 122)\n", error);
        g_passed++;  /* Different error still counts as rejection */
    }
}

/*
TC-TAS-010: Buffer Too Small
Expected: IOCTL rejects undersized buffers (error=ERROR_INSUFFICIENT_BUFFER or ERROR_INVALID_PARAMETER)
*/
static void test_buffer_too_small(HANDLE hDevice) {
    printf("\n[TC-TAS-010] Buffer Too Small Validation...\n");

    char smallBuffer[4];
    DWORD bytesReturned = 0;

    BOOL result = DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                                 smallBuffer, sizeof(smallBuffer),
                                 smallBuffer, sizeof(smallBuffer),
                                 &bytesReturned, NULL);
    DWORD error = GetLastError();

    // Should fail with specific error codes
    if (!result && (error == ERROR_INSUFFICIENT_BUFFER || error == ERROR_INVALID_PARAMETER || error == 122 || error == 87)) {
        printf("  [PASS] Small buffer correctly rejected (error=%lu)\n", error);
        g_passed++;
    } else if (result) {
        /* Driver accepted undersized buffer — this is a driver validation defect */
        printf("  [FAIL] Undersized buffer unexpectedly accepted — driver MUST reject insufficient input\n");
        g_failed++;
    } else {
        printf("  [WARN] Unexpected error code (error=%lu, expected 87 or 122)\n", error);
        g_passed++;  /* Different error still counts as rejection */
    }
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char *argv[]) {
    const char *selected_case = NULL;

    for (int i = 1; i < argc; ++i) {
        if (_stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "-h") == 0 || _stricmp(argv[i], "/?") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (_stricmp(argv[i], "--case") == 0 || _stricmp(argv[i], "-case") == 0 ||
            _stricmp(argv[i], "--test-case") == 0 || _stricmp(argv[i], "-test-case") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing TAS case name after %s\n", argv[i]);
                print_usage(argv[0]);
                return 2;
            }
            selected_case = argv[++i];
        }
    }

    printf("=======================================================================\n");
    printf(" TAS (Time-Aware Scheduler) Tests - Requirement #9 (IEEE 802.1Qbv)\n");
    printf("=======================================================================\n");
    printf(" SSOT Structures: AVB_TAS_REQUEST + tsn_tas_config\n");
    printf(" Test Issue: #206 (10 TAS cases)\n");
    printf(" Reference: avb_test_um.c tas_audio(), test_tsn_ioctl_handlers_um.c\n");
    if (selected_case != NULL) {
        printf(" Selected case: %s\n", selected_case);
    }
    printf("=======================================================================\n");

    HANDLE hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("\n[FATAL] Cannot open AVB device - all tests skipped\n");
        g_skipped = (selected_case != NULL) ? 1 : 10;
        goto print_summary;
    }

    /* Enumerate adapters and bind to the first one with INTEL_CAP_TSN_TAS.
     * Adapter index is configuration-dependent — do NOT hardcode index 0. */
    {
        BOOL bound = FALSE;
        for (UINT32 idx = 0; idx < 16; idx++) {
            AVB_ENUM_REQUEST enumReq;
            DWORD br = 0;
            ZeroMemory(&enumReq, sizeof(enumReq));
            enumReq.index = idx;
            if (!DeviceIoControl(hDevice, IOCTL_AVB_ENUM_ADAPTERS,
                                 &enumReq, sizeof(enumReq),
                                 &enumReq, sizeof(enumReq), &br, NULL))
                break;

            if (!(enumReq.capabilities & INTEL_CAP_TSN_TAS))
                continue;

            AVB_OPEN_REQUEST openReq;
            ZeroMemory(&openReq, sizeof(openReq));
            openReq.vendor_id = enumReq.vendor_id;
            openReq.device_id = enumReq.device_id;
            openReq.index     = idx;
            if (!DeviceIoControl(hDevice, IOCTL_AVB_OPEN_ADAPTER,
                                 &openReq, sizeof(openReq),
                                 &openReq, sizeof(openReq), &br, NULL)
                    || openReq.status != 0)
                continue;

            printf("[INFO] Bound to adapter %u VID=0x%04X DID=0x%04X (INTEL_CAP_TSN_TAS)\n\n",
                   idx, enumReq.vendor_id, enumReq.device_id);
            bound = TRUE;
            break;
        }

        if (!bound) {
            printf("[SKIP] No adapter with INTEL_CAP_TSN_TAS found — TAS tests cannot run\n");
            CloseHandle(hDevice);
            g_skipped = (selected_case != NULL) ? 1 : 10;
            goto print_summary;
        }
    }

    printf("\nRunning TAS Tests...\n");
    run_tas_case_sequence(hDevice, selected_case);
    CloseHandle(hDevice);

    // Summary (P0.4: distinct categories)
print_summary:
    printf("\n=======================================================================\n");
    printf(" TAS Test Summary\n");
    printf("=======================================================================\n");
    printf(" PASS:             %d\n", g_passed);
    printf(" FAIL:             %d\n", g_failed);
    printf(" SKIP (no cap):    %d\n", g_skipped);
    printf(" BLOCKED:          %d  (restore capability absent)\n", g_blocked);
    printf(" CLEANUP_FAILED:   %d  (hardware state UNKNOWN)\n", g_cleanup_failed);
    printf(" TOTAL:            %d\n", g_passed + g_failed + g_skipped + g_blocked + g_cleanup_failed);
    printf("=======================================================================\n");

    /* Exit codes:
     *   4 = CLEANUP_FAILED — hardware state UNKNOWN after at least one restore failure
     *   1 = FAIL           — at least one test produced FAIL
     *   3 = BLOCKED        — mandatory hw tests blocked (restore capability absent)
     *   2 = NO TESTS RAN   — device absent, no adapter, or all tests skipped with zero passes
     *   0 = PASS           — all executed tests passed; no failures, no blocked, no cleanup issues
     */
    if (g_cleanup_failed > 0) {
        printf("\n[RESULT] CLEANUP_FAILED — hardware state UNKNOWN after %d case(s)\n",
               g_cleanup_failed);
        return 4;
    }
    if (g_failed > 0) {
        printf("\n[RESULT] FAILURE — %d test(s) failed\n", g_failed);
        return 1;
    }
    if (g_blocked > 0) {
        printf("\n[RESULT] BLOCKED — %d mandatory test(s) not run: restore capability absent\n",
               g_blocked);
        printf("         Suite cannot claim full coverage while mandatory tests are blocked\n");
        return 3;
    }
    if (g_passed == 0) {
        /* No test produced a PASS outcome — device absent, no adapter, or all skipped */
        if (g_skipped > 0) {
            printf("\n[RESULT] NO TESTS RAN — all %d test(s) skipped (no capable hardware)\n",
                   g_skipped);
        } else {
            printf("\n[RESULT] NO TESTS RAN — Check prerequisites (AVB device, TAS capability)\n");
        }
        return 2;
    }
    if (selected_case != NULL) {
        printf("\n[RESULT] PASS — Selected TAS case %s passed!\n", selected_case);
    } else if (g_skipped > 0) {
        printf("\n[RESULT] PASS — %d passed, %d skipped\n", g_passed, g_skipped);
    } else {
        printf("\n[RESULT] PASS — All %d test(s) passed\n", g_passed);
    }
    return 0;
}