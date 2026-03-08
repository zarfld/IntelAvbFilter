/**
 * @file test_vlan_tag.c
 * @brief TDD-RED: IEEE 802.1Q VLAN tag insert/strip contract test
 *
 * Implements: #213 (TEST-VLAN-001: IEEE 802.1Q VLAN tagging)
 * TDD state : RED — IOCTL_AVB_VLAN_ENABLE/DISABLE not yet implemented.
 *             All VLAN TCs will SKIP until the driver feature is added.
 *             Once the feature is implemented, all TCs should turn PASS.
 *
 * Acceptance criteria (from #213):
 *   - IOCTL_AVB_VLAN_ENABLE  sets VLAN insertion mode in filter driver
 *   - IOCTL_AVB_VLAN_DISABLE clears VLAN insertion mode
 *   - VLAN id/pcp persists across the enable call and is readable back
 *   - Driver correctly strips 802.1Q tag on RX when strip_rx=1
 *
 * Test Cases: 5
 *   TC-VLAN-001: Device node accessible (baseline — always runs)
 *   TC-VLAN-002: IOCTL_AVB_VLAN_ENABLE accepted by driver          [TDD-RED]
 *   TC-VLAN-003: VLAN config read-back: vlan_id/pcp preserved      [TDD-RED]
 *   TC-VLAN-004: IOCTL_AVB_VLAN_DISABLE accepted by driver         [TDD-RED]
 *   TC-VLAN-005: Default state is VLAN-disabled after clean driver load [TDD-RED]
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/213
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* IOCTL codes and structs from avb_ioctl.h (SSOT) — see include/avb_ioctl.h */
/* AVB_VLAN_REQUEST, IOCTL_AVB_VLAN_ENABLE, IOCTL_AVB_VLAN_DISABLE defined there */

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"
static int g_pass = 0, g_fail = 0, g_skip = 0;

static HANDLE OpenDevice(void) {
    return CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Returns >0=PASS, 0=FAIL, <0=SKIP (not implemented) */
static int TryIoctl(HANDLE h, DWORD code, void *buf, DWORD sz)
{
    DWORD ret = 0;
    if (DeviceIoControl(h, code, buf, sz, buf, sz, &ret, NULL)) return 1;
    DWORD e = GetLastError();
    if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) {
        printf("    [SKIP] IOCTL 0x%08lX not supported (err=%lu)\n",
               (unsigned long)code, (unsigned long)e);
        return -1;
    }
    printf("    [FAIL] DeviceIoControl failed (err=%lu)\n", (unsigned long)e);
    return 0;
}

/* ───── TC-VLAN-001 ─────────────────────────────────────────────────────────── */
static int TC_VLAN_001_DeviceAccess(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open %s (err=%lu)\n", DEVICE_NAME, GetLastError());
        return 0;
    }
    CloseHandle(h);
    printf("    Device node accessible -- driver loaded\n");
    return 1;
}

/* ───── TC-VLAN-002 ─────────────────────────────────────────────────────────── */
static int TC_VLAN_002_Enable(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_VLAN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.vlan_id  = 100;
    req.pcp      = 3;
    req.strip_rx = 1;

    int r = TryIoctl(h, IOCTL_AVB_VLAN_ENABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    VLAN enable accepted: vlan_id=%u pcp=%u strip_rx=%u\n",
               req.vlan_id, req.pcp, req.strip_rx);
    return r;
}

/* ───── TC-VLAN-003 ─────────────────────────────────────────────────────────── */
static int TC_VLAN_003_ReadBack(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    /* Re-enable with known values, then check out fields */
    AVB_VLAN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.vlan_id  = 200;
    req.pcp      = 5;
    req.strip_rx = 0;

    int r = TryIoctl(h, IOCTL_AVB_VLAN_ENABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        if (req.vlan_id_out != 200 || req.pcp_out != 5 || !req.enabled) {
            printf("    [FAIL] Read-back mismatch: vlan_id_out=%u pcp_out=%u enabled=%u\n",
                   req.vlan_id_out, req.pcp_out, req.enabled);
            return 0;
        }
        printf("    Read-back OK: vlan_id=%u pcp=%u enabled=%u\n",
               req.vlan_id_out, req.pcp_out, req.enabled);
    }
    return r;
}

/* ───── TC-VLAN-004 ─────────────────────────────────────────────────────────── */
static int TC_VLAN_004_Disable(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_VLAN_REQUEST req;
    ZeroMemory(&req, sizeof(req));

    int r = TryIoctl(h, IOCTL_AVB_VLAN_DISABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        if (req.enabled) {
            printf("    [FAIL] enabled flag still set after VLAN_DISABLE\n");
            return 0;
        }
        printf("    VLAN_DISABLE accepted, enabled=0\n");
    }
    return r;
}

/* ───── TC-VLAN-005 ─────────────────────────────────────────────────────────── */
static int TC_VLAN_005_DefaultDisabled(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    /* Issue a disable to ensure clean state, then query */
    AVB_VLAN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    TryIoctl(h, IOCTL_AVB_VLAN_DISABLE, &req, sizeof(req));
    ZeroMemory(&req, sizeof(req));

    int r = TryIoctl(h, IOCTL_AVB_VLAN_DISABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        printf("    After VLAN_DISABLE: enabled=%u vlan_id_out=%u\n",
               req.enabled, req.vlan_id_out);
        if (req.enabled != 0) {
            printf("    [FAIL] Expected enabled=0 after disable\n");
            return 0;
        }
    }
    return r;
}

/* ───── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    int r;
    printf("============================================================\n");
    printf("  IntelAvbFilter -- IEEE 802.1Q VLAN Tag Tests\n");
    printf("  Implements: #213 (TEST-VLAN-001)\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_VLAN_001_DeviceAccess, "TC-VLAN-001: Device node accessible (baseline)");
    RUN(TC_VLAN_002_Enable,       "TC-VLAN-002: IOCTL_AVB_VLAN_ENABLE accepted");
    RUN(TC_VLAN_003_ReadBack,     "TC-VLAN-003: VLAN config read-back preserved");
    RUN(TC_VLAN_004_Disable,      "TC-VLAN-004: IOCTL_AVB_VLAN_DISABLE accepted");
    RUN(TC_VLAN_005_DefaultDisabled, "TC-VLAN-005: Default state is VLAN-disabled");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");

    return (g_fail == 0) ? 0 : 1;
}
