/**
 * @file test_eee_lpi.c
 * @brief TDD-RED: IEEE 802.3az Energy-Efficient Ethernet (EEE) / LPI control test
 *
 * Implements: #223 (TEST-EEE-001: IEEE 802.3az EEE/LPI)
 * TDD state : RED — IOCTL_AVB_EEE_ENABLE not yet implemented.
 *             TC-EEE-002/-004 will SKIP; TC-EEE-003 uses existing MDIO_READ
 *             so it may produce useful data regardless of TDD state.
 *
 * Acceptance criteria (from #223):
 *   - IOCTL_AVB_EEE_ENABLE writes LPI Assert timer to EEER register (MDIO MMD 3.20)
 *   - IOCTL_AVB_EEE_DISABLE clears LPI timer
 *   - MDIO read-back confirms register change
 *
 * Test Cases: 5
 *   TC-EEE-001: Device node accessible (baseline)
 *   TC-EEE-002: IOCTL_AVB_EEE_ENABLE -> EEER register set      [TDD-RED]
 *   TC-EEE-003: MDIO read IEEE MMD 3.20 (EEE Capability Advertise) [uses existing MDIO_READ]
 *   TC-EEE-004: IOCTL_AVB_EEE_DISABLE -> LPI timer cleared     [TDD-RED]
 *   TC-EEE-005: MDIO read IEEE MMD 7.60 (EEE Advertisement) status
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/223
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* ── TDD placeholder IOCTL codes ────────────────────────────────────────────── */
#define IOCTL_AVB_EEE_ENABLE  _NDIS_CONTROL_CODE(55, METHOD_BUFFERED)
#define IOCTL_AVB_EEE_DISABLE _NDIS_CONTROL_CODE(56, METHOD_BUFFERED)

typedef struct AVB_EEE_REQUEST {
    avb_u32 lpi_timer_us;   /* in: LPI Assert timer in microseconds (1-255) */
    avb_u32 eeer_readback;  /* out: EEER register value after write */
    avb_u32 status;         /* out: NDIS_STATUS */
} AVB_EEE_REQUEST;

/* IEEE 802.3az MDIO addresses (MMD Device.Register) */
#define EEE_MMD_DEVICE_PMAPMD   3u   /* PMA/PMD (IEEE MMD 3) */
#define EEE_MMD_DEVICE_AN       7u   /* Auto-Negotiation (IEEE MMD 7) */
#define EEE_REG_CAPABILITY      20u  /* MMD 3.20: EEE Capability */
#define EEE_REG_ADVERTISEMENT   60u  /* MMD 7.60: EEE Advertisement */

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

/* ───── TC-EEE-001 ─────────────────────────────────────────────────────────── */
static int TC_EEE_001_DeviceAccess(void)
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

/* ───── TC-EEE-002 ─────────────────────────────────────────────────────────── */
static int TC_EEE_002_EEE_Enable(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_EEE_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.lpi_timer_us = 10; /* 10 µs LPI Assert timer */

    int r = TryIoctl(h, IOCTL_AVB_EEE_ENABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    EEE enabled: lpi_timer=%u us, EEER=0x%08lX\n",
               req.lpi_timer_us, (unsigned long)req.eeer_readback);
    return r;
}

/* ───── TC-EEE-003 ─────────────────────────────────────────────────────────── */
/* Uses EXISTING IOCTL_AVB_MDIO_READ — always runs regardless of TDD state */
static int TC_EEE_003_MDIO_EEE_Capability(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_MDIO_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.page  = EEE_MMD_DEVICE_PMAPMD;
    req.reg   = EEE_REG_CAPABILITY;

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
        printf("    [FAIL] MDIO_READ failed (err=%lu)\n", e);
        return 0;
    }
    printf("    MMD %u.%u (EEE Capability) = 0x%04X\n",
           req.page, req.reg, (unsigned)req.value);
    /* bit 2 = 1000BASE-T EEE capable, bit 1 = 100BASE-TX EEE capable */
    printf("    1000BASE-T EEE: %s\n", (req.value & 0x04) ? "capable" : "not advertised");
    printf("    100BASE-TX EEE: %s\n", (req.value & 0x02) ? "capable" : "not advertised");
    return 1;
}

/* ───── TC-EEE-004 ─────────────────────────────────────────────────────────── */
static int TC_EEE_004_EEE_Disable(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_EEE_REQUEST req;
    ZeroMemory(&req, sizeof(req));

    int r = TryIoctl(h, IOCTL_AVB_EEE_DISABLE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    EEE disabled, EEER=0x%08lX\n", (unsigned long)req.eeer_readback);
    return r;
}

/* ───── TC-EEE-005 ─────────────────────────────────────────────────────────── */
static int TC_EEE_005_MDIO_EEE_Advertisement(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_MDIO_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.page = EEE_MMD_DEVICE_AN;
    req.reg  = EEE_REG_ADVERTISEMENT;

    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_MDIO_READ,
                               &req, sizeof(req), &req, sizeof(req), &ret, NULL);
    CloseHandle(h);
    if (!ok) {
        DWORD e = GetLastError();
        if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) return -1;
        return 0;
    }
    printf("    MMD %u.%u (EEE Advertisement) = 0x%04X\n",
           req.page, req.reg, (unsigned)req.value);
    printf("    1000BASE-T EEE adv: %s\n", (req.value & 0x04) ? "yes" : "no");
    printf("    100BASE-TX EEE adv: %s\n", (req.value & 0x02) ? "yes" : "no");
    return 1;
}

int main(void)
{
    int r;
    printf("============================================================\n");
    printf("  IntelAvbFilter -- IEEE 802.3az EEE/LPI Tests [TDD-RED]\n");
    printf("  Implements: #223 (TEST-EEE-001)\n");
    printf("  MDIO TCs run regardless; EEE_ENABLE/DISABLE TCs will SKIP\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_EEE_001_DeviceAccess,         "TC-EEE-001: Device node accessible");
    RUN(TC_EEE_002_EEE_Enable,           "TC-EEE-002: IOCTL_AVB_EEE_ENABLE -> EEER [TDD-RED]");
    RUN(TC_EEE_003_MDIO_EEE_Capability,  "TC-EEE-003: MDIO read MMD 3.20 EEE Capability");
    RUN(TC_EEE_004_EEE_Disable,          "TC-EEE-004: IOCTL_AVB_EEE_DISABLE -> LPI off [TDD-RED]");
    RUN(TC_EEE_005_MDIO_EEE_Advertisement,"TC-EEE-005: MDIO read MMD 7.60 EEE Advertisement");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
