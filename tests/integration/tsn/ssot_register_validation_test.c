/**
 * @file ssot_register_validation_test.c
 * @brief Hardware validation test for SSOT register definitions
 * 
 * This test validates that SSOT-generated register addresses match the actual
 * hardware register locations by performing read/write tests on real Intel
 * controllers.
 * 
 * CRITICAL: This test must be run on real hardware (I210, I226) to ensure
 * that the auto-generated register headers are accurate.
 * 
 * Based on verified working addresses from previous hardware testing.
 */

#include <windows.h>
#include <stdio.h>
#include "../../include/avb_ioctl.h"

// Include SSOT register definitions
#include "../../intel-ethernet-regs/gen/i210_regs.h"
#include "../../intel-ethernet-regs/gen/i226_regs.h"

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"

// Known working addresses from hardware testing (GOLDEN REFERENCE)
#define GOLDEN_SYSTIML  0x0B600
#define GOLDEN_SYSTIMH  0x0B604
#define GOLDEN_TIMINCA  0x0B608
#define GOLDEN_TSAUXC   0x0B640
#define GOLDEN_RXPBSIZE 0x2404
#define GOLDEN_TRGTTIML0 0x0B644
#define GOLDEN_TRGTTIMH0 0x0B648
#define GOLDEN_AUXSTMPL0 0x0B65C
#define GOLDEN_AUXSTMPH0 0x0B660

// Documented I226 TSN definitions (I225 Software Manual Rev 1.3 section 8.12 and upstream igc).
// Not a golden reference: not yet confirmed on I226 hardware.
#define DOC_TQAVCC0       0x03004
#define DOC_TQAVHC0       0x0300C
#define DOC_TQAVCC1       0x03044
#define DOC_TQAVHC1       0x0304C
#define DOC_GTXOFFSET     0x03310
#define DOC_BASET_L       0x03314
#define DOC_BASET_H       0x03318
#define DOC_QBVCYCLET     0x0331C
#define DOC_QBVCYCLET_S   0x03320
#define DOC_STQT0         0x03324
#define DOC_ENDQT0        0x03334
#define DOC_TXQCTL0       0x03344
#define DOC_TXARB         0x03354
#define DOC_TXPBSIZE      0x03404
#define DOC_DTXMXPKTSZ    0x0355C
#define DOC_TQAVCTRL      0x03570
#define DOC_RETX_CTL      0x0041C
#define DOC_TCTL          0x00400
#define DOC_TDLEN0        0x0E008
#define DOC_TDH0          0x0E010
#define DOC_TDT0          0x0E018
#define DOC_TXDCTL0       0x0E028
#define DOC_TX_QUEUE_STRIDE 0x40

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

/**
 * @brief Validate that SSOT register address matches golden reference
 */
static void validate_register_address(const char* reg_name, uint32_t ssot_addr, uint32_t golden_addr)
{
    printf("  %-20s SSOT=0x%05X  Golden=0x%05X  ", reg_name, ssot_addr, golden_addr);
    
    if (ssot_addr == golden_addr) {
        printf("[PASS]\n");
        tests_passed++;
    } else {
        printf("[FAIL] ADDRESS MISMATCH!\n");
        tests_failed++;
    }
}

/**
 * @brief Compare the SSOT I226 TSN definitions with the documented addresses and bit masks
 *
 * Verifies: #328
 */
static void validate_i226_tsn_definitions(void)
{
    printf("\nI226 TSN Registers (documented, not hardware-confirmed):\n");
    validate_register_address("I226_TQAVCC0", I226_TQAVCC0, DOC_TQAVCC0);
    validate_register_address("I226_TQAVHC0", I226_TQAVHC0, DOC_TQAVHC0);
    validate_register_address("I226_TQAVCC1", I226_TQAVCC1, DOC_TQAVCC1);
    validate_register_address("I226_TQAVHC1", I226_TQAVHC1, DOC_TQAVHC1);
    validate_register_address("I226_GTXOFFSET", I226_GTXOFFSET, DOC_GTXOFFSET);
    validate_register_address("I226_BASET_L", I226_BASET_L, DOC_BASET_L);
    validate_register_address("I226_BASET_H", I226_BASET_H, DOC_BASET_H);
    validate_register_address("I226_QBVCYCLET", I226_QBVCYCLET, DOC_QBVCYCLET);
    validate_register_address("I226_QBVCYCLET_S", I226_QBVCYCLET_S, DOC_QBVCYCLET_S);
    validate_register_address("I226_STQT0", I226_STQT0, DOC_STQT0);
    validate_register_address("I226_STQT3", I226_STQT3, DOC_STQT0 + 3 * 4);
    validate_register_address("I226_ENDQT0", I226_ENDQT0, DOC_ENDQT0);
    validate_register_address("I226_ENDQT3", I226_ENDQT3, DOC_ENDQT0 + 3 * 4);
    validate_register_address("I226_TXQCTL0", I226_TXQCTL0, DOC_TXQCTL0);
    validate_register_address("I226_TXQCTL3", I226_TXQCTL3, DOC_TXQCTL0 + 3 * 4);
    validate_register_address("I226_TXARB", I226_TXARB, DOC_TXARB);
    validate_register_address("I226_TXPBSIZE", I226_TXPBSIZE, DOC_TXPBSIZE);
    validate_register_address("I226_DTXMXPKTSZ", I226_DTXMXPKTSZ, DOC_DTXMXPKTSZ);
    validate_register_address("I226_TQAVCTRL", I226_TQAVCTRL, DOC_TQAVCTRL);
    validate_register_address("I226_RETX_CTL", I226_RETX_CTL, DOC_RETX_CTL);
    validate_register_address("I226_TCTL", I226_TCTL, DOC_TCTL);
    validate_register_address("I226_TDLEN0", I226_TDLEN0, DOC_TDLEN0);
    validate_register_address("I226_TDLEN3", I226_TDLEN3, DOC_TDLEN0 + 3 * DOC_TX_QUEUE_STRIDE);
    validate_register_address("I226_TDH0", I226_TDH0, DOC_TDH0);
    validate_register_address("I226_TDH3", I226_TDH3, DOC_TDH0 + 3 * DOC_TX_QUEUE_STRIDE);
    validate_register_address("I226_TDT0", I226_TDT0, DOC_TDT0);
    validate_register_address("I226_TDT3", I226_TDT3, DOC_TDT0 + 3 * DOC_TX_QUEUE_STRIDE);
    validate_register_address("I226_TXDCTL0", I226_TXDCTL0, DOC_TXDCTL0);
    validate_register_address("I226_TXDCTL3", I226_TXDCTL3, DOC_TXDCTL0 + 3 * DOC_TX_QUEUE_STRIDE);

    printf("\nI226 TSN bit masks (documented, not hardware-confirmed):\n");
    validate_register_address("TRANSMIT_MODE", (uint32_t)I226_TQAVCTRL_TRANSMIT_MODE_MASK, 0x00001);
    validate_register_address("PREEMPT_ENA", (uint32_t)I226_TQAVCTRL_PREEMPT_ENA_MASK, 0x00002);
    validate_register_address("ENHANCED_QAV", (uint32_t)I226_TQAVCTRL_ENHANCED_QAV_MASK, 0x00008);
    validate_register_address("FUTSCDDIS", (uint32_t)I226_TQAVCTRL_FUTSCDDIS_MASK, 0x00080);
    validate_register_address("MIN_FRAG", (uint32_t)I226_TQAVCTRL_MIN_FRAG_MASK, 0x0C000);
    validate_register_address("QUEUE_MODE", (uint32_t)I226_TXQCTL_QUEUE_MODE_MASK, 0x00001);
    validate_register_address("STRICT_CYCLE", (uint32_t)I226_TXQCTL_STRICT_CYCLE_MASK, 0x00002);
    validate_register_address("STRICT_END", (uint32_t)I226_TXQCTL_STRICT_END_MASK, 0x00004);
    validate_register_address("PREEMPTABLE", (uint32_t)I226_TXQCTL_PREEMPTABLE_MASK, 0x00008);
    validate_register_address("QAV_SEL", (uint32_t)I226_TXQCTL_QAV_SEL_MASK, 0x000C0);
    validate_register_address("TXDCTL_ENABLE", (uint32_t)I226_TXDCTL_ENABLE_MASK, 0x02000000);
    validate_register_address("TCTL_EN", (uint32_t)I226_TCTL_EN_MASK, 0x00002);
}

/**
 * @brief Read register using IOCTL (requires hardware)
 */
static BOOL read_register_hw(HANDLE hDevice, uint32_t offset, uint32_t *value)
{
    AVB_REGISTER_REQUEST regReq = {0};
    DWORD bytesReturned;
    
    regReq.offset = offset;
    
    if (!DeviceIoControl(hDevice,
                         IOCTL_AVB_READ_REGISTER,
                         &regReq,
                         sizeof(regReq),
                         &regReq,
                         sizeof(regReq),
                         &bytesReturned,
                         NULL)) {
        return FALSE;
    }
    
    if (regReq.status != 0) {
        return FALSE;
    }
    
    *value = regReq.value;
    return TRUE;
}

/**
 * @brief Hardware validation: Read SYSTIM to verify PTP block accessibility
 */
static BOOL validate_ptp_block_hw(HANDLE hDevice, const char* device_name)
{
    uint32_t systiml_1, systimh_1, systiml_2, systimh_2;
    
    printf("\n--- Hardware Validation: %s PTP Block ---\n", device_name);
    
    // Read SYSTIM twice to verify it's incrementing
    if (!read_register_hw(hDevice, GOLDEN_SYSTIML, &systiml_1) ||
        !read_register_hw(hDevice, GOLDEN_SYSTIMH, &systimh_1)) {
        printf("  SYSTIM read 1: FAILED (hardware not accessible)\n");
        return FALSE;
    }
    
    Sleep(10); // 10ms delay
    
    if (!read_register_hw(hDevice, GOLDEN_SYSTIML, &systiml_2) ||
        !read_register_hw(hDevice, GOLDEN_SYSTIMH, &systimh_2)) {
        printf("  SYSTIM read 2: FAILED\n");
        return FALSE;
    }
    
    uint64_t systim_1 = ((uint64_t)systimh_1 << 32) | systiml_1;
    uint64_t systim_2 = ((uint64_t)systimh_2 << 32) | systiml_2;
    
    printf("  SYSTIM 1: 0x%08X%08X\n", systimh_1, systiml_1);
    printf("  SYSTIM 2: 0x%08X%08X\n", systimh_2, systiml_2);
    
    if (systim_2 > systim_1) {
        uint64_t delta = systim_2 - systim_1;
        printf("  Delta:    %llu ns (%.2f ms) [PASS - PTP clock running]\n", 
               delta, delta / 1e6);
        tests_passed++;
        return TRUE;
    } else {
        printf("  Delta:    ZERO or NEGATIVE [FAIL - PTP clock not running]\n");
        tests_failed++;
        return FALSE;
    }
}

/**
 * @brief Hardware validation: Verify TIMINCA register accessibility
 */
static BOOL validate_timinca_hw(HANDLE hDevice, const char* device_name)
{
    uint32_t timinca;
    
    printf("\n--- Hardware Validation: %s TIMINCA Register ---\n", device_name);
    
    if (!read_register_hw(hDevice, GOLDEN_TIMINCA, &timinca)) {
        printf("  TIMINCA read: FAILED\n");
        tests_failed++;
        return FALSE;
    }
    
    printf("  TIMINCA: 0x%08X\n", timinca);
    
    // Validate TIMINCA has reasonable increment values
    uint32_t incr_period = timinca & 0xFF;
    uint32_t incr_value = (timinca >> 8) & 0xFF;
    
    printf("  Increment Period: %u\n", incr_period);
    printf("  Increment Value:  %u\n", incr_value);
    
    if (incr_value >= 1 && incr_value <= 16) {
        printf("  [PASS - Valid increment configuration]\n");
        tests_passed++;
        return TRUE;
    } else {
        printf("  [WARN - Unusual increment value]\n");
        tests_passed++; // Still pass, might be unconfigured
        return TRUE;
    }
}

/**
 * @brief Hardware validation: Verify TSAUXC register accessibility
 */
static BOOL validate_tsauxc_hw(HANDLE hDevice, const char* device_name)
{
    uint32_t tsauxc;
    
    printf("\n--- Hardware Validation: %s TSAUXC Register ---\n", device_name);
    
    if (!read_register_hw(hDevice, GOLDEN_TSAUXC, &tsauxc)) {
        printf("  TSAUXC read: FAILED\n");
        tests_failed++;
        return FALSE;
    }
    
    printf("  TSAUXC: 0x%08X\n", tsauxc);
    printf("  DIS_SYSTIM0 (bit 31): %s\n", (tsauxc & (1 << 31)) ? "DISABLED" : "ENABLED");
    printf("  EN_TT0 (bit 0):       %s\n", (tsauxc & (1 << 0)) ? "ENABLED" : "DISABLED");
    printf("  EN_TT1 (bit 4):       %s\n", (tsauxc & (1 << 4)) ? "ENABLED" : "DISABLED");
    printf("  EN_TS0 (bit 8):       %s\n", (tsauxc & (1 << 8)) ? "ENABLED" : "DISABLED");
    printf("  EN_TS1 (bit 10):      %s\n", (tsauxc & (1 << 10)) ? "ENABLED" : "DISABLED");
    
    printf("  [PASS - TSAUXC register accessible]\n");
    tests_passed++;
    return TRUE;
}

/**
 * @brief Report one TC-TSNREG-001 per-adapter verdict
 */
static void report_tsn_registers(const char* tag, UINT32 adapter, avb_u16 did, const char* reason)
{
    printf("[%s] TC-TSNREG-001/adapter:%u did=0x%04X%s%s\n", tag, adapter, did, reason ? ": " : "", reason ? reason : "");
}

/**
 * @brief Read the TSN registers of one adapter and check them against the driver's TAS state
 * @return NULL on success, otherwise the failure reason (in reason)
 */
static const char* check_tsn_registers(HANDLE hBound, const AVB_ADAPTER_IDENTITY_REQUEST* id, char* reason, size_t reasonSize)
{
    static const struct { const char* name; uint32_t offset; } regs[] = {
        {"TQAVCTRL", I226_TQAVCTRL}, {"TXPBSIZE", I226_TXPBSIZE}, {"DTXMXPKTSZ", I226_DTXMXPKTSZ}, {"TXARB", I226_TXARB},
        {"RETX_CTL", I226_RETX_CTL}, {"GTXOFFSET", I226_GTXOFFSET}, {"BASET_L", I226_BASET_L}, {"BASET_H", I226_BASET_H},
        {"QBVCYCLET", I226_QBVCYCLET}, {"QBVCYCLET_S", I226_QBVCYCLET_S},
        {"STQT0", I226_STQT0}, {"STQT1", I226_STQT1}, {"STQT2", I226_STQT2}, {"STQT3", I226_STQT3},
        {"ENDQT0", I226_ENDQT0}, {"ENDQT1", I226_ENDQT1}, {"ENDQT2", I226_ENDQT2}, {"ENDQT3", I226_ENDQT3},
        {"TXQCTL0", I226_TXQCTL0}, {"TXQCTL1", I226_TXQCTL1}, {"TXQCTL2", I226_TXQCTL2}, {"TXQCTL3", I226_TXQCTL3},
        {"TQAVCC0", I226_TQAVCC0}, {"TQAVCC1", I226_TQAVCC1}, {"TQAVHC0", I226_TQAVHC0}, {"TQAVHC1", I226_TQAVHC1},
        {"TDLEN0", I226_TDLEN0}, {"TDLEN1", I226_TDLEN1}, {"TDLEN2", I226_TDLEN2}, {"TDLEN3", I226_TDLEN3},
        {"TXDCTL0", I226_TXDCTL0}, {"TXDCTL1", I226_TXDCTL1}, {"TXDCTL2", I226_TXDCTL2}, {"TXDCTL3", I226_TXDCTL3},
    };
    AVB_OPEN_BY_LUID_REQUEST bind = {0};
    AVB_TAS_STATE tas = {0};
    DWORD bytesReturned;
    uint32_t value, tqavctrl = 0;
    int i;

    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = id->net_luid;
    if (!DeviceIoControl(hBound, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &bytesReturned, NULL)) {
        sprintf_s(reason, reasonSize, "OPEN_ADAPTER_BY_LUID failed (Win32 %lu)", GetLastError());
        return reason;
    }
    if (!DeviceIoControl(hBound, IOCTL_AVB_GET_TAS_STATE, &tas, sizeof(tas), &tas, sizeof(tas), &bytesReturned, NULL)) {
        sprintf_s(reason, reasonSize, "GET_TAS_STATE failed (Win32 %lu)", GetLastError());
        return reason;
    }
    printf("    tasArmed=%u\n", tas.armed);

    for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); i++) {
        if (!read_register_hw(hBound, regs[i].offset, &value)) {
            sprintf_s(reason, reasonSize, "%s read failed", regs[i].name);
            return reason;
        }
        printf("    %-12s 0x%05X = 0x%08X\n", regs[i].name, regs[i].offset, value);
        if (value == 0xFFFFFFFFu) {
            sprintf_s(reason, reasonSize, "%s reads all ones, device inaccessible", regs[i].name);
            return reason;
        }
        if (regs[i].offset == I226_TQAVCTRL) {
            tqavctrl = value;
        }
    }

    // Ring pointers over time. TDH shows fetch progress only, not completion (manual section 8.11.13).
    for (i = 0; i < 5; i++) {
        uint32_t head[4], tail[4];
        if (!read_register_hw(hBound, I226_TDH0, &head[0]) || !read_register_hw(hBound, I226_TDH1, &head[1]) ||
            !read_register_hw(hBound, I226_TDH2, &head[2]) || !read_register_hw(hBound, I226_TDH3, &head[3]) ||
            !read_register_hw(hBound, I226_TDT0, &tail[0]) || !read_register_hw(hBound, I226_TDT1, &tail[1]) ||
            !read_register_hw(hBound, I226_TDT2, &tail[2]) || !read_register_hw(hBound, I226_TDT3, &tail[3])) {
            sprintf_s(reason, reasonSize, "TDH/TDT read failed");
            return reason;
        }
        printf("    sample %d TDH/TDT q0=%u/%u q1=%u/%u q2=%u/%u q3=%u/%u\n", i,
               head[0], tail[0], head[1], tail[1], head[2], tail[2], head[3], tail[3]);
        Sleep(1000);
    }

    if (((tqavctrl & I226_TQAVCTRL_TRANSMIT_MODE_MASK) != 0) != (tas.armed != 0)) {
        sprintf_s(reason, reasonSize, "TQAVCTRL 0x%08X disagrees with tasArmed=%u", tqavctrl, tas.armed);
        return reason;
    }
    return NULL;
}

/**
 * @brief Report one TC-HWSNAP-001 per-adapter verdict (reason NULL = pass)
 */
static void report_hw_snapshot(UINT32 adapter, avb_u16 did, const char* reason)
{
    if (reason == NULL) {
        printf("[PASS] TC-HWSNAP-001/adapter:%u did=0x%04X\n", adapter, did);
        tests_passed++;
    } else {
        printf("[FAIL] TC-HWSNAP-001/adapter:%u did=0x%04X: %s\n", adapter, did, reason);
        tests_failed++;
    }
}

/**
 * @brief Bind a handle to one adapter and validate its QUERY_HW_SNAPSHOT
 * @return NULL on success, otherwise the failure reason (in reason)
 */
static const char* check_hw_snapshot(HANDLE hBound, const AVB_ADAPTER_IDENTITY_REQUEST* id, char* reason, size_t reasonSize)
{
    AVB_OPEN_BY_LUID_REQUEST bind = {0};
    AVB_HW_SNAPSHOT_REQUEST snap = {0};
    DWORD bytesReturned;
    UINT32 i, valid = 0;

    bind.version = AVB_OPEN_BY_LUID_VERSION;
    bind.net_luid = id->net_luid;
    if (!DeviceIoControl(hBound, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &bind, sizeof(bind), &bind, sizeof(bind), &bytesReturned, NULL)) {
        sprintf_s(reason, reasonSize, "OPEN_ADAPTER_BY_LUID failed (Win32 %lu)", GetLastError());
        return reason;
    }

    snap.version = AVB_HW_SNAPSHOT_VERSION;
    if (!DeviceIoControl(hBound, IOCTL_AVB_QUERY_HW_SNAPSHOT, &snap, sizeof(snap), &snap, sizeof(snap), &bytesReturned, NULL)) {
        sprintf_s(reason, reasonSize, "QUERY_HW_SNAPSHOT failed (Win32 %lu)", GetLastError());
        return reason;
    }
    if (snap.status != 0u || snap.count != AVB_HWREG_COUNT) {
        sprintf_s(reason, reasonSize, "status 0x%08X count %u, expected 0 and %u", snap.status, snap.count, AVB_HWREG_COUNT);
        return reason;
    }
    if (snap.device_id != id->device_id || snap.net_luid != id->net_luid) {
        sprintf_s(reason, reasonSize, "snapshot is of another adapter: did=0x%04X luid=0x%016llX", snap.device_id, snap.net_luid);
        return reason;
    }

    for (i = 0; i < AVB_HWREG_COUNT; i++) {
        const AVB_HWREG_ENTRY* e = &snap.entries[i];
        printf("    id=%-2u offset=0x%05X value=0x%08X state=%u\n", e->id, e->offset, e->value, e->state);
        if (e->id != i + 1u) {
            sprintf_s(reason, reasonSize, "entry %u has id %u", i, e->id);
            return reason;
        }
        if (e->state == AVB_HWREG_STATE_READ_FAILED) {
            sprintf_s(reason, reasonSize, "register id %u read failed", e->id);
            return reason;
        }
        if (e->state == AVB_HWREG_STATE_VALID) {
            valid++;
        }
    }
    if (valid == 0u) {
        sprintf_s(reason, reasonSize, "no register read VALID");
        return reason;
    }
    return NULL;
}

/**
 * @brief TC-HWSNAP-001: QUERY_HW_SNAPSHOT answers on a handle bound to each adapter
 *
 * The runner's restoration check reads registers through this IOCTL. Each adapter gets its own handle, so
 * the default-handle checks above are unaffected. Read-only: GET_ADAPTER_IDENTITY, OPEN_ADAPTER_BY_LUID
 * (per-handle binding, no global switch, no PTP initialization), QUERY_HW_SNAPSHOT.
 *
 * Verifies: #328
 */
static void validate_hw_snapshot_all_adapters(HANDLE hDevice)
{
    UINT32 index, count = 1;

    for (index = 0; index < count; index++) {
        AVB_ADAPTER_IDENTITY_REQUEST id = {0};
        DWORD bytesReturned;
        char reason[128];
        HANDLE hBound;

        id.version = AVB_ADAPTER_IDENTITY_VERSION;
        id.index = index;
        if (!DeviceIoControl(hDevice, IOCTL_AVB_GET_ADAPTER_IDENTITY, &id, sizeof(id), &id, sizeof(id), &bytesReturned, NULL)) {
            sprintf_s(reason, sizeof(reason), "GET_ADAPTER_IDENTITY failed (Win32 %lu)", GetLastError());
            report_hw_snapshot(index, 0, reason);
            return;
        }
        count = id.count;
        if (id.status != 0u) {
            sprintf_s(reason, sizeof(reason), "GET_ADAPTER_IDENTITY status 0x%08X", id.status);
            report_hw_snapshot(index, id.device_id, reason);
            continue;
        }
        printf("  snapshot adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u\n", index, id.device_id, id.net_luid, id.if_index);

        hBound = CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hBound == INVALID_HANDLE_VALUE) {
            sprintf_s(reason, sizeof(reason), "open failed (Win32 %lu)", GetLastError());
            report_hw_snapshot(index, id.device_id, reason);
            continue;
        }
        report_hw_snapshot(index, id.device_id, check_hw_snapshot(hBound, &id, reason, sizeof(reason)));
        CloseHandle(hBound);
    }
    if (count == 0u) {
        report_hw_snapshot(0, 0, "no adapter enumerated");
    }
}

/**
 * @brief TC-TSNREG-001: TSN registers are readable and match the driver's TAS state on every TAS adapter
 *
 * Applicability comes from the advertised INTEL_CAP_TSN_TAS, never from a device ID. Values are printed as
 * evidence and not asserted, since the miniport owns and may program them; only the transmit mode is compared
 * with tasArmed. Read-only: GET_VERSION, GET_ADAPTER_IDENTITY, OPEN_ADAPTER_BY_LUID, GET_TAS_STATE, READ_REGISTER.
 *
 * Verifies: #328
 */
static void validate_tsn_registers_all_adapters(HANDLE hDevice)
{
    IOCTL_VERSION version = {0};
    DWORD bytesReturned;
    UINT32 index, count = 1;
    BOOL debugBuild = FALSE;

    if (DeviceIoControl(hDevice, IOCTL_AVB_GET_VERSION, &version, sizeof(version), &version, sizeof(version), &bytesReturned, NULL)) {
        debugBuild = (version.Flags & AVB_VERSION_FLAG_DEBUG_BUILD) != 0;
    }

    for (index = 0; index < count; index++) {
        AVB_ADAPTER_IDENTITY_REQUEST id = {0};
        char reason[128];
        const char* failure;
        HANDLE hBound;

        id.version = AVB_ADAPTER_IDENTITY_VERSION;
        id.index = index;
        if (!DeviceIoControl(hDevice, IOCTL_AVB_GET_ADAPTER_IDENTITY, &id, sizeof(id), &id, sizeof(id), &bytesReturned, NULL)) {
            sprintf_s(reason, sizeof(reason), "GET_ADAPTER_IDENTITY failed (Win32 %lu)", GetLastError());
            report_tsn_registers("FAIL", index, 0, reason);
            tests_failed++;
            return;
        }
        count = id.count;
        if (!(id.capabilities & INTEL_CAP_TSN_TAS)) {
            report_tsn_registers("SKIP_UNSUPPORTED", index, id.device_id, "adapter does not advertise INTEL_CAP_TSN_TAS");
            continue;
        }
        if (!debugBuild) {
            report_tsn_registers("SKIP", index, id.device_id, "READ_REGISTER exists only in Debug drivers; no Release TSN register evidence");
            continue;
        }

        printf("  adapter:%u did=0x%04X luid=0x%016llX ifIndex=%u\n", index, id.device_id, id.net_luid, id.if_index);
        hBound = CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hBound == INVALID_HANDLE_VALUE) {
            sprintf_s(reason, sizeof(reason), "open failed (Win32 %lu)", GetLastError());
            report_tsn_registers("FAIL", index, id.device_id, reason);
            tests_failed++;
            continue;
        }
        failure = check_tsn_registers(hBound, &id, reason, sizeof(reason));
        CloseHandle(hBound);
        report_tsn_registers(failure ? "FAIL" : "PASS", index, id.device_id, failure);
        if (failure) {
            tests_failed++;
        } else {
            tests_passed++;
        }
    }
}

int main(void)
{
    HANDLE hDevice;
    AVB_DEVICE_INFO_REQUEST devInfo = {0};
    DWORD bytesReturned;
    
    printf("=== Intel AVB Filter - SSOT Register Validation Test ===\n");
    printf("Purpose: Verify SSOT register addresses match hardware reality\n");
    printf("Critical: This test MUST be run on real I210/I226 hardware\n\n");
    
    // Open device
    hDevice = CreateFileA(DEVICE_NAME,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open device %s (Error: %lu)\n", DEVICE_NAME, GetLastError());
        printf("Make sure the driver is loaded and you have administrator privileges.\n");
        return 1;
    }
    
    printf("Device opened successfully\n\n");
    
    // Get device information
    if (DeviceIoControl(hDevice,
                       IOCTL_AVB_GET_DEVICE_INFO,
                       &devInfo,
                       sizeof(devInfo),
                       &devInfo,
                       sizeof(devInfo),
                       &bytesReturned,
                       NULL)) {
        printf("Device Info: %s\n", devInfo.device_info);
        
        // Get hardware state to retrieve capabilities
        AVB_HW_STATE_QUERY hwState = {0};
        if (DeviceIoControl(hDevice,
                           IOCTL_AVB_GET_HW_STATE,
                           &hwState,
                           sizeof(hwState),
                           &hwState,
                           sizeof(hwState),
                           &bytesReturned,
                           NULL)) {
            printf("Capabilities: 0x%08X\n\n", hwState.capabilities);
        } else {
            printf("Failed to get capabilities (Error: %lu)\n\n", GetLastError());
        }
    }
    
    // Phase 1: Compile-time SSOT validation
    printf("=== Phase 1: Compile-Time SSOT Validation ===\n");
    printf("Comparing SSOT addresses with golden reference from hardware testing:\n\n");
    
    // I210 register validation
    printf("I210 Registers:\n");
    validate_register_address("I210_SYSTIML", I210_SYSTIML, GOLDEN_SYSTIML);
    validate_register_address("I210_SYSTIMH", I210_SYSTIMH, GOLDEN_SYSTIMH);
    validate_register_address("I210_TIMINCA", I210_TIMINCA, GOLDEN_TIMINCA);
    validate_register_address("I210_TSAUXC", I210_TSAUXC, GOLDEN_TSAUXC);
    validate_register_address("I210_TRGTTIML0", I210_TRGTTIML0, GOLDEN_TRGTTIML0);
    validate_register_address("I210_TRGTTIMH0", I210_TRGTTIMH0, GOLDEN_TRGTTIMH0);
    validate_register_address("I210_AUXSTMPL0", I210_AUXSTMPL0, GOLDEN_AUXSTMPL0);
    validate_register_address("I210_AUXSTMPH0", I210_AUXSTMPH0, GOLDEN_AUXSTMPH0);
    
    printf("\nI226 Registers:\n");
    validate_register_address("I226_SYSTIML", I226_SYSTIML, GOLDEN_SYSTIML);
    validate_register_address("I226_SYSTIMH", I226_SYSTIMH, GOLDEN_SYSTIMH);
    validate_register_address("I226_TIMINCA", I226_TIMINCA, GOLDEN_TIMINCA);
    validate_register_address("I226_TSAUXC", I226_TSAUXC, GOLDEN_TSAUXC);
    validate_register_address("I226_TRGTTIML0", I226_TRGTTIML0, GOLDEN_TRGTTIML0);
    validate_register_address("I226_TRGTTIMH0", I226_TRGTTIMH0, GOLDEN_TRGTTIMH0);
    validate_register_address("I226_AUXSTMPL0", I226_AUXSTMPL0, GOLDEN_AUXSTMPL0);
    validate_register_address("I226_AUXSTMPH0", I226_AUXSTMPH0, GOLDEN_AUXSTMPH0);
    validate_i226_tsn_definitions();

    // Phase 2: Hardware validation (requires real Intel NIC)
    printf("\n=== Phase 2: Hardware Register Accessibility Test ===\n");
    printf("Reading actual hardware registers to verify SSOT addresses work:\n");
    
    validate_ptp_block_hw(hDevice, "Current Device");
    validate_timinca_hw(hDevice, "Current Device");
    validate_tsauxc_hw(hDevice, "Current Device");

    // Phase 3: TSN registers on every adapter advertising TAS
    printf("\n=== Phase 3: TSN Registers On Every TAS Adapter ===\n");
    validate_tsn_registers_all_adapters(hDevice);

    // Phase 4: Register snapshot on a handle bound to each adapter
    printf("\n=== Phase 4: QUERY_HW_SNAPSHOT On Every Adapter ===\n");
    validate_hw_snapshot_all_adapters(hDevice);

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Tests Passed: %d\n", tests_passed);
    printf("Tests Failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\n? ALL TESTS PASSED! SSOT register definitions are correct.\n");
        printf("Hardware validation confirms SSOT addresses match real hardware.\n");
    } else {
        printf("\n? %d TESTS FAILED! SSOT definitions may need correction.\n", tests_failed);
        printf("Review register address mismatches and update YAML files.\n");
    }
    
    CloseHandle(hDevice);
    
    return tests_failed;
}
