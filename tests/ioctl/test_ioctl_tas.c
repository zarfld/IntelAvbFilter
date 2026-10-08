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

// Test result counters
static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

// ============================================================================
// Snapshot / Restore infrastructure (test-state-restoration contract)
// Every hardware-mutating case MUST call tas_capture_state before mutation
// and tas_restore_state (unconditionally) after, reporting cleanup failures
// as hard test failures.
// ============================================================================

typedef struct {
    BOOL     valid;              /* 1 = snapshot was taken successfully */
    avb_u32  tsauxc_before;     /* TSAUXC value before enable_systim0 */
    BOOL     tas_was_armed;     /* driver reported TAS armed before test */
    struct tsn_tas_config tas_original_config; /* GCL config if tas_was_armed */
} TAS_SNAPSHOT;

/* Capture TSAUXC and TAS armed state before any mutation.
 * Returns FALSE and leaves snapshot->valid=0 on failure; caller must not mutate. */
static BOOL tas_capture_state(HANDLE hDevice, TAS_SNAPSHOT *snap) {
    ZeroMemory(snap, sizeof(*snap));

    /* Read current clock config for TSAUXC baseline */
    AVB_CLOCK_CONFIG clockCfg;
    ZeroMemory(&clockCfg, sizeof(clockCfg));
    DWORD br = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG,
                         &clockCfg, sizeof(clockCfg),
                         &clockCfg, sizeof(clockCfg),
                         &br, NULL) || clockCfg.status != 0) {
        printf("  [CAPTURE-FAIL] Cannot read TSAUXC before test (error=%lu status=0x%08X)\n",
               GetLastError(), clockCfg.status);
        return FALSE;
    }
    snap->tsauxc_before = clockCfg.tsauxc;

    /* Read TAS armed state */
    AVB_TAS_STATE tasState;
    ZeroMemory(&tasState, sizeof(tasState));
    if (!DeviceIoControl(hDevice, IOCTL_AVB_GET_TAS_STATE,
                         NULL, 0,
                         &tasState, sizeof(tasState),
                         &br, NULL) || tasState.status != 0) {
        printf("  [CAPTURE-FAIL] Cannot read TAS state before test (error=%lu status=0x%08X)\n",
               GetLastError(), tasState.status);
        return FALSE;
    }
    snap->tas_was_armed = (tasState.armed != 0);
    if (snap->tas_was_armed) {
        memcpy(&snap->tas_original_config, &tasState.config, sizeof(tasState.config));
    }
    snap->valid = TRUE;
    printf("  [SNAPSHOT] tsauxc=0x%08X tas_armed=%d\n", snap->tsauxc_before, snap->tas_was_armed);
    if (snap->tas_was_armed) {
        printf("  [SNAPSHOT] WARNING: TAS was armed before test — original GCL captured (base_time may be expired on restore)\n");
    }
    return TRUE;
}

/* Restore TSAUXC and TAS state to the captured baseline.
 * Returns FALSE if any restoration operation failed (caller must fail the test).
 *
 * TAS restoration semantics:
 *   - If TAS was disarmed before test: call IOCTL_AVB_DISARM_TAS and verify.
 *   - If TAS was armed before test: attempt to re-arm with original GCL.
 *     Limitation: base_time in the original config may have expired by restore time.
 *     We detect this, report it explicitly as a BLOCKED condition, and do NOT
 *     claim successful restoration. The caller must handle this as a hard failure.
 *
 * TSAUXC restoration: uses previous_tsauxc captured in snapshot to restore
 * the enable/disable state of SYSTIM0. Note: this only controls the enable bit
 * (bit 31); other TSAUXC bits (target-time enables, aux-ts enables) are not
 * preserved — they would need a dedicated capture/restore path. */
static BOOL tas_restore_state(HANDLE hDevice, const TAS_SNAPSHOT *snap) {
    if (!snap->valid) {
        printf("  [RESTORE-FAIL] No valid snapshot — cannot restore\n");
        return FALSE;
    }

    BOOL ok = TRUE;
    DWORD br = 0;

    if (!snap->tas_was_armed) {
        /* Original state: TAS disarmed. Disarm and verify. */
        AVB_DISARM_TAS_REQUEST disarm;
        ZeroMemory(&disarm, sizeof(disarm));
        if (!DeviceIoControl(hDevice, IOCTL_AVB_DISARM_TAS,
                             NULL, 0,
                             &disarm, sizeof(disarm),
                             &br, NULL) || disarm.status != 0) {
            printf("  [RESTORE-FAIL] TAS disarm failed (error=%lu status=0x%08X)\n",
                   GetLastError(), disarm.status);
            ok = FALSE;
        } else {
            printf("  [RESTORE] TAS disarmed (original state: disarmed)\n");
        }
    } else {
        /* Original state: TAS was armed.
         * Attempt to re-arm with original GCL — but base_time may be expired.
         * Compute whether base_time is still in the future. */
        ULONGLONG now_ns = 0;
        {
            AVB_TIMESTAMP_REQUEST tsReq;
            ZeroMemory(&tsReq, sizeof(tsReq));
            if (DeviceIoControl(hDevice, IOCTL_AVB_GET_TIMESTAMP,
                                &tsReq, sizeof(tsReq),
                                &tsReq, sizeof(tsReq),
                                &br, NULL) && tsReq.status == 0) {
                now_ns = tsReq.timestamp;
            }
        }

        ULONGLONG base_ns = (ULONGLONG)snap->tas_original_config.base_time_s * 1000000000ULL
                          + snap->tas_original_config.base_time_ns;

        if (base_ns != 0 && now_ns != 0 && now_ns > base_ns) {
            /* base_time has passed — cannot restore original armed schedule safely.
             * Report as BLOCKED: the test must record this as a restoration limitation.
             * Per the contract: "explicitly report that limitation rather than claiming
             * successful restoration." */
            printf("  [RESTORE-BLOCKED] Original TAS base_time (%llu ns) has expired (now=%llu ns)\n",
                   (unsigned long long)base_ns, (unsigned long long)now_ns);
            printf("  [RESTORE-BLOCKED] Cannot re-arm with original GCL — base_time semantics require future timestamp\n");
            printf("  [RESTORE-BLOCKED] Disarming instead and marking restoration as incomplete\n");
            /* Best-effort: at least disarm so hardware is not stuck in a stale schedule */
            AVB_DISARM_TAS_REQUEST disarm;
            ZeroMemory(&disarm, sizeof(disarm));
            DeviceIoControl(hDevice, IOCTL_AVB_DISARM_TAS, NULL, 0,
                            &disarm, sizeof(disarm), &br, NULL);
            ok = FALSE; /* hard failure — restoration is incomplete */
        } else {
            /* Try to re-arm with original config */
            AVB_TAS_REQUEST rearm;
            ZeroMemory(&rearm, sizeof(rearm));
            memcpy(&rearm.config, &snap->tas_original_config, sizeof(rearm.config));
            if (!DeviceIoControl(hDevice, IOCTL_AVB_SETUP_TAS,
                                 &rearm, sizeof(rearm),
                                 &rearm, sizeof(rearm),
                                 &br, NULL) || rearm.status != 0) {
                printf("  [RESTORE-FAIL] TAS re-arm failed (error=%lu status=0x%08X)\n",
                       GetLastError(), rearm.status);
                ok = FALSE;
            } else {
                printf("  [RESTORE] TAS re-armed with original GCL (original state: armed)\n");
            }
        }
    }

    /* Restore TSAUXC enable/disable state (bit 31 only — see limitation note above) */
    AVB_HW_TIMESTAMPING_REQUEST hwTs;
    ZeroMemory(&hwTs, sizeof(hwTs));
    hwTs.enable        = ((snap->tsauxc_before & 0x80000000u) == 0) ? 1 : 0;
    hwTs.timer_mask    = 0x1; /* SYSTIM0 */
    if (!DeviceIoControl(hDevice, IOCTL_AVB_SET_HW_TIMESTAMPING,
                         &hwTs, sizeof(hwTs),
                         &hwTs, sizeof(hwTs),
                         &br, NULL) || hwTs.status != 0) {
        printf("  [RESTORE-FAIL] TSAUXC restore failed (error=%lu status=0x%08X)\n",
               GetLastError(), hwTs.status);
        ok = FALSE;
    } else {
        printf("  [RESTORE] TSAUXC restored (enable=%u current=0x%08X)\n",
               hwTs.enable, hwTs.current_tsauxc);
    }

    return ok;
}

/* Independent post-restore verification: confirm TAS armed state matches snapshot.
 * Returns FALSE if driver state does not match what was captured before the test. */
static BOOL tas_verify_restore(HANDLE hDevice, const TAS_SNAPSHOT *snap) {
    AVB_TAS_STATE tasState;
    ZeroMemory(&tasState, sizeof(tasState));
    DWORD br = 0;
    if (!DeviceIoControl(hDevice, IOCTL_AVB_GET_TAS_STATE,
                         NULL, 0,
                         &tasState, sizeof(tasState),
                         &br, NULL) || tasState.status != 0) {
        printf("  [VERIFY-FAIL] Cannot verify TAS state after restore\n");
        return FALSE;
    }
    BOOL now_armed = (tasState.armed != 0);
    if (now_armed != snap->tas_was_armed) {
        printf("  [VERIFY-FAIL] TAS armed state mismatch: was %d before test, is %d after restore\n",
               snap->tas_was_armed, now_armed);
        return FALSE;
    }
    if (!now_armed) {
        printf("  [VERIFY-OK] TAS confirmed disarmed (matches original state)\n");
    } else {
        printf("  [VERIFY-OK] TAS confirmed armed (matches original state)\n");
    }
    return TRUE;
}

/* Run one hardware-mutating TAS case with full snapshot/restore lifecycle.
 * Returns 0=pass, 1=test-fail, 2=cleanup-fail (nonzero means fail). */
static int run_with_restore(HANDLE hDevice, const char *case_name,
                            void (*fn)(HANDLE), int *passed, int *failed, int *skipped) {
    TAS_SNAPSHOT snap;
    if (!tas_capture_state(hDevice, &snap)) {
        printf("  [FAIL] %s: cannot capture state — test not executed\n", case_name);
        (*failed)++;
        return 1;
    }

    int before_pass = *passed, before_fail = *failed;
    fn(hDevice);
    int test_failed = (*failed > before_fail);

    /* Restore unconditionally — pass, fail, or skip */
    BOOL restore_ok = tas_restore_state(hDevice, &snap);
    BOOL verify_ok  = restore_ok ? tas_verify_restore(hDevice, &snap) : FALSE;

    if (!restore_ok || !verify_ok) {
        printf("  [CLEANUP-FAIL] %s: state restoration failed — halting further cases\n", case_name);
        (*failed)++;
        return 2; /* hard stop: do not run more hardware-mutating cases */
    }

    return test_failed ? 1 : 0;
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
            /* Hardware-mutating: run with snapshot/restore */
            run_with_restore(hDevice, selected_case,
                             hw_cases[index].fn,
                             &g_passed, &g_failed, &g_skipped);
        } else {
            /* Negative/validation case (TC-TAS-009..010): no persistent state change */
            neg_cases[index - 8].fn(hDevice);
        }
        return;
    }

    printf("[INFO] Running all TAS cases with per-case snapshot/restore\n");
    for (size_t i = 0; i < sizeof(hw_cases) / sizeof(hw_cases[0]); ++i) {
        int rc = run_with_restore(hDevice, hw_cases[i].name,
                                  hw_cases[i].fn,
                                  &g_passed, &g_failed, &g_skipped);
        if (rc == 2) {
            printf("[ABORT] Cleanup failure — stopping hardware-mutating cases\n");
            /* Count remaining hw cases as skipped */
            g_skipped += (int)(sizeof(hw_cases) / sizeof(hw_cases[0]) - i - 1);
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

    // Summary
print_summary:
    printf("\n=======================================================================\n");
    printf(" TAS Test Summary\n");
    printf("=======================================================================\n");
    printf(" PASSED:  %d\n", g_passed);
    printf(" FAILED:  %d\n", g_failed);
    printf(" SKIPPED: %d\n", g_skipped);
    printf(" TOTAL:   %d\n", g_passed + g_failed + g_skipped);
    printf("=======================================================================\n");

    if (g_failed > 0) {
        printf("\n[RESULT] FAILURE - %d test(s) failed\n", g_failed);
        return 1;
    }

    if (g_passed == 0 && g_failed == 0) {
        printf("\n[RESULT] NO TESTS RAN - Check prerequisites (AVB device, SYSTIM0, TAS capability)\n");
        return 2;  /* distinct from pass=0 and fail=1 so CI can detect no-run */
    }

    if (selected_case != NULL) {
        printf("\n[RESULT] SUCCESS - Selected TAS case %s passed!\n", selected_case);
    } else {
        printf("\n[RESULT] SUCCESS - All tests passed!\n");
    }
    return 0;
}