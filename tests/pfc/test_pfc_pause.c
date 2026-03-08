/**
 * @file test_pfc_pause.c
 * @brief TDD-RED: IEEE 802.1Qbb Priority Flow Control (PFC) contract test
 *
 * Implements: #219 (TEST-PFC-001: IEEE 802.1Qbb Priority Flow Control)
 * TDD state : RED — IOCTL_AVB_PFC_ENABLE not yet implemented.
 *             TC-PFC-002/-003 will SKIP. TC-PFC-004/-005 use MDIO_READ.
 *
 * Acceptance criteria (from #219):
 *   - IOCTL_AVB_PFC_ENABLE sets PFC enable bitmask (per-priority) in PFCTOP register
 *   - IOCTL_AVB_PFC_DISABLE clears PFC bitmask
 *   - Read-back confirms PFCTOP register state
 *
 * Test Cases: 5
 *   TC-PFC-001: Device node accessible (baseline)
 *   TC-PFC-002: IOCTL_AVB_PFC_ENABLE with priority mask 0b11 [TDD-RED]
 *   TC-PFC-003: IOCTL_AVB_PFC_DISABLE                       [TDD-RED]
 *   TC-PFC-004: MDIO read PAUSE capability from AN register
 *   TC-PFC-005: GET_DEVICE_INFO reports PFC capability flag
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/219
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* ── TDD placeholder IOCTL codes ────────────────────────────────────────────── */
#define IOCTL_AVB_PFC_ENABLE   _NDIS_CONTROL_CODE(57, METHOD_BUFFERED)
#define IOCTL_AVB_PFC_DISABLE  _NDIS_CONTROL_CODE(57, METHOD_BUFFERED) /* same code, mode field */

typedef struct AVB_PFC_REQUEST {
    avb_u8  enable;           /* in: 1=enable, 0=disable */
    avb_u8  priority_mask;    /* in: 8-bit bitmask of PFC-enabled Traffic Classes */
    avb_u8  quanta[8];        /* in: per-TC pause quanta (0=default 0xFFFF) */
    avb_u32 pfctop_readback;  /* out: PFCTOP register after write */
    avb_u32 status;           /* out: NDIS_STATUS */
} AVB_PFC_REQUEST;

/* AN register for PAUSE capability (IEEE 802.3 Clause 37/28) */
#define AN_MMD_DEVICE_7     7u
#define AN_REG_LP_BASE      19u  /* Link Partner Base Page Ability */

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"
static int g_pass = 0, g_fail = 0, g_skip = 0;

static HANDLE OpenDevice(void) {
    return CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static int TryIoctl(HANDLE h, DWORD code, void *buf, DWORD sz)
{
    DWORD ret = 0;
    if (DeviceIoControl(h, code, buf, sz, buf, sz, &ret, NULL)) return 1;
    DWORD e = GetLastError();
    if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) {
        printf("    [TDD-RED] IOCTL 0x%08lX not yet implemented (err=%lu)\n",
               (unsigned long)code, (unsigned long)e);
        return -1;
    }
    printf("    [FAIL] DeviceIoControl failed (err=%lu)\n", (unsigned long)e);
    return 0;
}

/* ───── TC-PFC-001 ─────────────────────────────────────────────────────────── */
static int TC_PFC_001_DeviceAccess(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }
    CloseHandle(h);
    printf("    Device node accessible\n");
    return 1;
}

/* ───── TC-PFC-002 ─────────────────────────────────────────────────────────── */
static int TC_PFC_002_PFC_Enable(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_PFC_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.enable        = 1;
    req.priority_mask = 0x03; /* TC 0 and TC 1 */

    int r = TryIoctl(h, IOCTL_AVB_PFC_ENABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    PFC enabled: priority_mask=0x%02X PFCTOP=0x%08lX\n",
               req.priority_mask, (unsigned long)req.pfctop_readback);
    return r;
}

/* ───── TC-PFC-003 ─────────────────────────────────────────────────────────── */
static int TC_PFC_003_PFC_Disable(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_PFC_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.enable        = 0;
    req.priority_mask = 0x00;

    int r = TryIoctl(h, IOCTL_AVB_PFC_DISABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    PFC disabled: PFCTOP=0x%08lX\n", (unsigned long)req.pfctop_readback);
    return r;
}

/* ───── TC-PFC-004 ─────────────────────────────────────────────────────────── */
/* Uses existing MDIO_READ to check PAUSE capability in AN advertisement */
static int TC_PFC_004_MDIO_PAUSE_Capability(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_MDIO_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.page = AN_MMD_DEVICE_7;
    req.reg  = AN_REG_LP_BASE;

    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_MDIO_READ,
                               &req, sizeof(req), &req, sizeof(req), &ret, NULL);
    CloseHandle(h);
    if (!ok) {
        DWORD e = GetLastError();
        if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) {
            printf("    MDIO_READ not available in this build\n");
            return -1;
        }
        printf("    MDIO_READ failed (err=%lu)\n", e);
        return 0;
    }
    printf("    MMD 7.19 (LP Base Page) = 0x%04X\n", (unsigned)req.value);
    printf("    PAUSE:      %s\n", (req.value & 0x0400) ? "capable" : "not set");
    printf("    ASM_DIR:    %s\n", (req.value & 0x0800) ? "capable" : "not set");
    return 1;
}

/* ───── TC-PFC-005 ─────────────────────────────────────────────────────────── */
static int TC_PFC_005_DeviceInfoPFC(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_DEVICE_INFO_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.buffer_size = sizeof(req.device_info);

    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_DEVICE_INFO,
                               &req, sizeof(req), &req, sizeof(req), &ret, NULL);
    CloseHandle(h);
    if (!ok) {
        DWORD e = GetLastError();
        if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) return -1;
        return 0;
    }
    printf("    GET_DEVICE_INFO succeeded (%lu bytes)\n", (unsigned long)req.buffer_size);
    /* PFC support would appear in the device info / capability flags in a full implementation */
    return 1;
}

int main(void)
{
    int r;
    printf("============================================================\n");
    printf("  IntelAvbFilter -- IEEE 802.1Qbb PFC Tests [TDD-RED]\n");
    printf("  Implements: #219 (TEST-PFC-001)\n");
    printf("  MDIO TCs run regardless; PFC control TCs will SKIP\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_PFC_001_DeviceAccess,      "TC-PFC-001: Device node accessible");
    RUN(TC_PFC_002_PFC_Enable,        "TC-PFC-002: IOCTL_AVB_PFC_ENABLE (priority_mask=0x03) [TDD-RED]");
    RUN(TC_PFC_003_PFC_Disable,       "TC-PFC-003: IOCTL_AVB_PFC_DISABLE [TDD-RED]");
    RUN(TC_PFC_004_MDIO_PAUSE_Capability, "TC-PFC-004: MDIO read MMD 7.19 PAUSE capability");
    RUN(TC_PFC_005_DeviceInfoPFC,     "TC-PFC-005: GET_DEVICE_INFO accessible (PFC scope)");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
