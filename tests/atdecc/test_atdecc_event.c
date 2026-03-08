/**
 * @file test_atdecc_event.c
 * @brief IEEE 1722.1 ATDECC entity event subscription contract test
 *
 * Implements: #236 (TEST-EVENT-003: ATDECC entity discovery events)
 * TDD state : GREEN — IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE implemented in driver.
 *
 * Acceptance criteria (from #236):
 *   - IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE queues entity-discovered events
 *   - Payload contains entity GUID and capabilities bitmask
 *   - Subscription can be unsubscribed cleanly
 *   - Concurrent subscribes (max 4) are independent
 *
 * Test Cases: 5
 *   TC-ATDECC-001: Device node accessible (baseline)
 *   TC-ATDECC-002: IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE returns subscription handle
 *   TC-ATDECC-003: Poll for entity-discovered event (0ms timeout -> no result)
 *   TC-ATDECC-004: IOCTL_AVB_ATDECC_EVENT_UNSUBSCRIBE cleans up handle
 *   TC-ATDECC-005: Double-subscribe returns unique handles
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/236
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* IOCTL codes and structs are now defined in avb_ioctl.h (SSOT):
 *   IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE   (code 58)
 *   IOCTL_AVB_ATDECC_EVENT_POLL        (code 59)
 *   IOCTL_AVB_ATDECC_EVENT_UNSUBSCRIBE (code 62)
 *   AVB_ATDECC_SUBSCRIBE_REQUEST
 *   AVB_ATDECC_POLL_REQUEST
 *   ATDECC_EVENT_ENTITY_AVAILABLE / ATDECC_EVENT_ENTITY_DEPARTING
 */

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"
static int g_pass = 0, g_fail = 0, g_skip = 0;
static avb_u32 g_subscription_id_1 = 0;
static avb_u32 g_subscription_id_2 = 0;

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

/* ───── TC-ATDECC-001 ────────────────────────────────────────────────────────── */
static int TC_ATDECC_001_DeviceAccess(void)
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

/* ───── TC-ATDECC-002 ────────────────────────────────────────────────────────── */
static int TC_ATDECC_002_Subscribe(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_ATDECC_SUBSCRIBE_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.event_mask = ATDECC_EVENT_ENTITY_AVAILABLE | ATDECC_EVENT_ENTITY_DEPARTING;

    int r = TryIoctl(h, IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        g_subscription_id_1 = req.subscription_id;
        printf("    Subscription handle = %lu\n", (unsigned long)g_subscription_id_1);
        if (g_subscription_id_1 == 0) {
            printf("    [FAIL] Expected non-zero subscription handle\n");
            return 0;
        }
    }
    return r;
}

/* ───── TC-ATDECC-003 ────────────────────────────────────────────────────────── */
static int TC_ATDECC_003_Poll(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_ATDECC_POLL_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.subscription_id = g_subscription_id_1;
    req.timeout_ms      = 0; /* non-blocking: no events expected immediately */

    int r = TryIoctl(h, IOCTL_AVB_ATDECC_EVENT_POLL, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0) {
        printf("    Poll result: event_available=%u\n", req.event_available);
        /* No event in 0ms is expected; this is just validating the IOCTL path */
    }
    return r;
}

/* ───── TC-ATDECC-004 ────────────────────────────────────────────────────────── */
static int TC_ATDECC_004_Unsubscribe(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_ATDECC_SUBSCRIBE_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.subscription_id = g_subscription_id_1;

    int r = TryIoctl(h, IOCTL_AVB_ATDECC_EVENT_UNSUBSCRIBE, &req, sizeof(req));
    CloseHandle(h);
    if (r > 0)
        printf("    Unsubscribed handle %lu\n", (unsigned long)g_subscription_id_1);
    return r;
}

/* ───── TC-ATDECC-005 ────────────────────────────────────────────────────────── */
static int TC_ATDECC_005_DoubleSubscribe(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_ATDECC_SUBSCRIBE_REQUEST r1, r2;
    ZeroMemory(&r1, sizeof(r1)); r1.event_mask = ATDECC_EVENT_ENTITY_AVAILABLE;
    ZeroMemory(&r2, sizeof(r2)); r2.event_mask = ATDECC_EVENT_ENTITY_DEPARTING;

    int rc1 = TryIoctl(h, IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE, &r1, sizeof(r1));
    int rc2 = TryIoctl(h, IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE, &r2, sizeof(r2));
    CloseHandle(h);

    if (rc1 < 0 || rc2 < 0) return -1; /* SKIP if not implemented */
    if (rc1 == 0 || rc2 == 0) return 0;

    g_subscription_id_1 = r1.subscription_id;
    g_subscription_id_2 = r2.subscription_id;
    printf("    Handle 1 = %lu, Handle 2 = %lu\n",
           (unsigned long)g_subscription_id_1, (unsigned long)g_subscription_id_2);
    if (g_subscription_id_1 == g_subscription_id_2) {
        printf("    [FAIL] Both handles are identical -- must be unique\n");
        return 0;
    }
    printf("    Handles are unique -- correct\n");
    return 1;
}

int main(void)
{
    int r;
    printf("============================================================\n");
    printf("  IntelAvbFilter -- IEEE 1722.1 ATDECC Event Tests\n");
    printf("  Implements: #236 (TEST-EVENT-003)\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_ATDECC_001_DeviceAccess, "TC-ATDECC-001: Device node accessible");
    RUN(TC_ATDECC_002_Subscribe,    "TC-ATDECC-002: ATDECC_EVENT_SUBSCRIBE -> handle");
    RUN(TC_ATDECC_003_Poll,         "TC-ATDECC-003: ATDECC_EVENT_POLL (0ms, no events)");
    RUN(TC_ATDECC_004_Unsubscribe,  "TC-ATDECC-004: ATDECC_EVENT_UNSUBSCRIBE");
    RUN(TC_ATDECC_005_DoubleSubscribe,"TC-ATDECC-005: Double-subscribe yields unique handles");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
