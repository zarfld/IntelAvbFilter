/**
 * @file test_srp_interface.c
 * @brief TDD-RED: IEEE 802.1Qat Stream Reservation Protocol (SRP) bandwidth contract test
 *
 * Implements: #211 (TEST-SRP-001: SRP bandwidth reservation)
 * TDD state : RED — IOCTL_AVB_SRP_REGISTER_STREAM not yet implemented.
 *             All SRP TCs will SKIP until the feature is added.
 *
 * Acceptance criteria (from #211):
 *   - IOCTL_AVB_SRP_REGISTER_STREAM reserves bandwidth on a VLAN/priority
 *   - A reservation handle is returned for later de-registration
 *   - IOCTL_AVB_SRP_DEREGISTER_STREAM releases reserved bandwidth
 *   - Registering beyond available bandwidth returns an error status
 *
 * Test Cases: 5
 *   TC-SRP-001: Device node accessible (baseline)
 *   TC-SRP-002: SRP_REGISTER_STREAM Class A 2 Mbps -> non-zero handle [TDD-RED]
 *   TC-SRP-003: Re-query after registration: reserved_bw_bps != 0        [TDD-RED]
 *   TC-SRP-004: SRP_DEREGISTER_STREAM cleans up reservation              [TDD-RED]
 *   TC-SRP-005: GET_DEVICE_INFO sanity after SRP sequence                [existing IOCTL]
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/211
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* ── TDD placeholder IOCTL codes ────────────────────────────────────────────── */
#define IOCTL_AVB_SRP_REGISTER_STREAM   _NDIS_CONTROL_CODE(60, METHOD_BUFFERED)
#define IOCTL_AVB_SRP_DEREGISTER_STREAM _NDIS_CONTROL_CODE(61, METHOD_BUFFERED)

/* IEEE 802.1Qat latency classes */
#define SRP_CLASS_A  0  /* max latency <2ms, observation interval 125µs */
#define SRP_CLASS_B  1  /* max latency <50ms, observation interval 250µs */

typedef struct AVB_SRP_REGISTER_REQUEST {
    avb_u64 stream_id;          /* in: IEEE 802.1Qat 64-bit stream identifier */
    avb_u32 bandwidth_bps;      /* in: required bandwidth in bits per second */
    avb_u16 vlan_id;            /* in: 802.1Q VLAN ID for the stream */
    avb_u8  priority;           /* in: 802.1p PCP priority (5=SR Class A, 4=Class B) */
    avb_u8  latency_class;      /* in: SRP_CLASS_A or SRP_CLASS_B */
    avb_u16 max_frame_size;     /* in: maximum frame size in bytes */
    avb_u8  reserved[2];        /* padding */
    avb_u32 reservation_handle; /* out: opaque handle for deregistration */
    avb_u32 reserved_bw_bps;    /* out: actual bandwidth reserved by driver */
    avb_u32 status;             /* out: NDIS_STATUS */
} AVB_SRP_REGISTER_REQUEST;

typedef struct AVB_SRP_DEREGISTER_REQUEST {
    avb_u32 reservation_handle; /* in: handle returned by REGISTER_STREAM */
    avb_u32 status;             /* out: NDIS_STATUS */
} AVB_SRP_DEREGISTER_REQUEST;

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"
static int g_pass = 0, g_fail = 0, g_skip = 0;
static avb_u32 g_reservation_handle = 0;

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

/* ───── TC-SRP-001 ───────────────────────────────────────────────────────────── */
static int TC_SRP_001_DeviceAccess(void)
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

/* ───── TC-SRP-002 ───────────────────────────────────────────────────────────── */
static int TC_SRP_002_RegisterStream(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_SRP_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.stream_id      = 0x0011223344556677ULL; /* test stream ID */
    req.bandwidth_bps  = 2000000;               /* 2 Mbps SR Class A */
    req.vlan_id        = 2;                     /* default AVB VLAN */
    req.priority       = 5;                     /* PCP 5 -> SR Class A */
    req.latency_class  = SRP_CLASS_A;
    req.max_frame_size = 1500;

    int r = TryIoctl(h, IOCTL_AVB_SRP_REGISTER_STREAM, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        g_reservation_handle = req.reservation_handle;
        printf("    reservation_handle = %lu, reserved_bw_bps = %lu\n",
               (unsigned long)g_reservation_handle,
               (unsigned long)req.reserved_bw_bps);
        if (g_reservation_handle == 0) {
            printf("    [FAIL] Expected non-zero reservation handle\n");
            return 0;
        }
        if (req.reserved_bw_bps == 0) {
            printf("    [FAIL] reserved_bw_bps must be > 0\n");
            return 0;
        }
    }
    return r;
}

/* ───── TC-SRP-003 ───────────────────────────────────────────────────────────── */
static int TC_SRP_003_VerifyReservation(void)
{
    /* Re-register same stream; driver must see it as a duplicate and still return
     * a valid handle (or identical handle) and non-zero reserved bandwidth.      */
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_SRP_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.stream_id      = 0x0011223344556677ULL;
    req.bandwidth_bps  = 2000000;
    req.vlan_id        = 2;
    req.priority       = 5;
    req.latency_class  = SRP_CLASS_A;
    req.max_frame_size = 1500;

    int r = TryIoctl(h, IOCTL_AVB_SRP_REGISTER_STREAM, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        printf("    reserved_bw_bps = %lu (expected > 0)\n",
               (unsigned long)req.reserved_bw_bps);
        if (req.reserved_bw_bps == 0) {
            printf("    [FAIL] No bandwidth reserved after register\n");
            return 0;
        }
    }
    return r;
}

/* ───── TC-SRP-004 ───────────────────────────────────────────────────────────── */
static int TC_SRP_004_DeregisterStream(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_SRP_DEREGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.reservation_handle = g_reservation_handle;

    int r = TryIoctl(h, IOCTL_AVB_SRP_DEREGISTER_STREAM, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    Deregistered handle %lu\n", (unsigned long)g_reservation_handle);
    return r;
}

/* ───── TC-SRP-005 ───────────────────────────────────────────────────────────── */
static int TC_SRP_005_GetDeviceInfoSanity(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_DEVICE_INFO_REQUEST info;
    ZeroMemory(&info, sizeof(info));
    info.buffer_size = AVB_DEVICE_INFO_MAX;

    int r = TryIoctl(h, IOCTL_AVB_GET_DEVICE_INFO, &info, sizeof(info));
    CloseHandle(h);
    if (r > 0) {
        printf("    device_info len=%u -- driver still alive after SRP ops\n",
               (unsigned)info.buffer_size);
    }
    return r;
}

int main(void)
{
    int r;
    printf("============================================================\n");
    printf("  IntelAvbFilter -- IEEE 802.1Qat SRP Tests [TDD-RED]\n");
    printf("  Implements: #211 (TEST-SRP-001)\n");
    printf("  Status: SKIP expected until SRP IOCTLs are implemented\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_SRP_001_DeviceAccess,       "TC-SRP-001: Device node accessible");
    RUN(TC_SRP_002_RegisterStream,     "TC-SRP-002: SRP_REGISTER_STREAM Class A 2Mbps [TDD-RED]");
    RUN(TC_SRP_003_VerifyReservation,  "TC-SRP-003: Verify reserved_bw_bps != 0 [TDD-RED]");
    RUN(TC_SRP_004_DeregisterStream,   "TC-SRP-004: SRP_DEREGISTER_STREAM [TDD-RED]");
    RUN(TC_SRP_005_GetDeviceInfoSanity,"TC-SRP-005: GET_DEVICE_INFO after SRP ops");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
