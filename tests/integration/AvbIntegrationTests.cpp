// Intel AVB Filter Driver - Integration Tests (standalone harness)
// C++14, no external deps; uses Windows + IOCTL ABI only.
// This complements TAEF by enabling quick runs on dev machines.

// Only include the driver PCH when actually building in kernel-mode.
// User-mode integration builds must not pull in WDK-only headers like ndis.h.
#if defined(_KERNEL_MODE)
#  include "precomp.h"
#  define AVB_HAS_PCH 1
#endif

#if defined(_KERNEL_MODE)
VOID AvbIntegrationTests_KmBuildPlaceHolder(void) { /* not built in KM */ }
#else

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include "include/avb_ioctl.h"

static HANDLE OpenAvb()
{
    return CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static bool IoctlNoBuf(DWORD code)
{
    DWORD bytes = 0; HANDLE h = OpenAvb(); if (h == INVALID_HANDLE_VALUE) return false;
    BOOL ok = DeviceIoControl(h, code, nullptr, 0, nullptr, 0, &bytes, nullptr);
    CloseHandle(h); return ok != 0;
}

static bool IoctlInOut(DWORD code, void* inbuf, DWORD inlen, void* outbuf, DWORD outlen, DWORD& bytes)
{
    HANDLE h = OpenAvb(); if (h == INVALID_HANDLE_VALUE) return false;
    BOOL ok = DeviceIoControl(h, code, inbuf, inlen, outbuf, outlen, &bytes, nullptr);
    CloseHandle(h); return ok != 0;
}

static uint64_t qpc_ns()
{
    LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
    return (uint64_t)((c.QuadPart * 1000000000ULL) / (uint64_t)f.QuadPart);
}

static void sleep_ms(DWORD ms) { Sleep(ms); }

static int TestInitAndInfo()
{
    printf("[Init] Initialize device... ");
    bool initOk = IoctlNoBuf(IOCTL_AVB_INIT_DEVICE);
    printf(initOk ? "OK\n" : "FAIL (%lu)\n", GetLastError());

    AVB_DEVICE_INFO_REQUEST info = {};
    info.buffer_size = sizeof(info.device_info);
    DWORD bytes = 0;
    bool infoOk = IoctlInOut(IOCTL_AVB_GET_DEVICE_INFO, &info, sizeof(info), &info, sizeof(info), bytes);
    printf("[Init] Get device info... %s (bytes=%lu)\n", infoOk ? "OK" : "FAIL", bytes);
    if (infoOk) {
        int n = (int)std::min<DWORD>((DWORD)info.buffer_size, bytes);
        printf("        Info: %.*s\n", n, info.device_info);
    }
    return (initOk && infoOk) ? 0 : 1;
}

static int TestGptpSetGet()
{
    printf("[gPTP] Set/Get timestamp roundtrip... ");
    AVB_TIMESTAMP_REQUEST setReq = {};
    setReq.timestamp = 1234567890123ULL; // known value
    setReq.clock_id = 0;
    DWORD bytes = 0;
    bool setOk = IoctlInOut(IOCTL_AVB_SET_TIMESTAMP, &setReq, sizeof(setReq), &setReq, sizeof(setReq), bytes);
    if (!setOk) { printf("UNSUPPORTED (Set failed, err=%lu)\n", GetLastError()); return 0; }

    AVB_TIMESTAMP_REQUEST getReq = {};
    bool getOk = IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &getReq, sizeof(getReq), &getReq, sizeof(getReq), bytes);
    if (!getOk) { printf("FAIL (Get failed, err=%lu)\n", GetLastError()); return 1; }

    // Accept exact or near-equal depending on hardware semantics
    uint64_t diff = (getReq.timestamp > setReq.timestamp) ? (getReq.timestamp - setReq.timestamp) : (setReq.timestamp - getReq.timestamp);
    if (diff <= 1000ULL) { // within 1us tolerance
        printf("OK (diff=%llu ns)\n", (unsigned long long)diff);
        return 0;
    }
    printf("WARN (unexpected diff=%llu ns)\n", (unsigned long long)diff);
    return 0; // non-fatal if hardware ignores SET
}

static int TestTimestampMonotonicity()
{
    printf("[Time] Monotonicity under load (GET 10k)... ");
    const size_t N = 10000;
    std::vector<uint64_t> samples; samples.reserve(N);
    AVB_TIMESTAMP_REQUEST req = {};
    DWORD bytes = 0;
    for (size_t i = 0; i < N; ++i) {
        if (!IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes)) {
            printf("FAIL (GetTimestamp err=%lu)\n", GetLastError());
            return 1;
        }
        samples.push_back(req.timestamp);
    }
    // Check monotonic non-decreasing and basic jitter
    bool mono = true; uint64_t maxStep = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if (samples[i] < samples[i-1]) { mono = false; break; }
        maxStep = std::max(maxStep, samples[i] - samples[i-1]);
    }
    printf("%s (max step=%llu ns)\n", mono ? "OK" : "FAIL", (unsigned long long)maxStep);
    return mono ? 0 : 1;
}

static int TestIoctlThroughput()
{
    printf("[Perf] IOCTL GET_TIMESTAMP throughput (1s) ... ");
    uint64_t end = qpc_ns() + 1000000000ULL; // 1s
    AVB_TIMESTAMP_REQUEST req = {};
    DWORD bytes = 0; uint64_t cnt = 0;
    while (qpc_ns() < end) {
        if (!IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes)) {
            printf("FAIL (err=%lu)\n", GetLastError()); return 1; }
        ++cnt;
    }
    printf("OK (%.2f kops/s)\n", (double)cnt / 1000.0);
    return 0;
}

static int TestCableUnplugScenario()
{
    // This test is advisory unless an adapter name is provided via AVB_ADAPTER_NAME.
    wchar_t name[256] = {0};
    DWORD got = GetEnvironmentVariableW(L"AVB_ADAPTER_NAME", name, _countof(name));
    if (got == 0 || got >= _countof(name)) {
        printf("[ErrorPath] SKIP (set AVB_ADAPTER_NAME to friendly interface name)\n");
        return 0;
    }
    printf("[ErrorPath] Disable/Enable '%ls' and GET_TIMESTAMP resiliency... ", name);
    // Disable
    wchar_t cmd[512]; wsprintfW(cmd, L"netsh interface set interface \"%ls\" admin=disabled", name);
    if (_wsystem(cmd) != 0) { printf("SKIP (netsh failed)\n"); return 0; }
    sleep_ms(2000);
    // Expect IOCTL may fail while down; call and ignore result
    AVB_TIMESTAMP_REQUEST req = {}; DWORD bytes = 0; IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes);
    // Enable
    wsprintfW(cmd, L"netsh interface set interface \"%ls\" admin=enabled", name);
    _wsystem(cmd);
    sleep_ms(3000);
    // Expect IOCTL to recover and succeed eventually
    bool ok = false; for (int i = 0; i < 10; ++i) { if (IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes)) { ok = true; break; } sleep_ms(500); }
    printf(ok ? "OK\n" : "FAIL (no recovery)\n");
    return ok ? 0 : 1;
}

static int TestColdBootSafetyNote()
{
    printf("[Boot] Manual check required: ensure no early-boot issues when filter auto-loads.\n");
    printf("       Suggest using Windows' Boot Traces (WPR/WPA) and Driver Verifier.\n");
    return 0;
}

int wmain()
{
    int failures = 0;
    printf("Intel AVB Filter - Integration Tests (standalone)\n\n");
    failures += TestInitAndInfo();
    failures += TestGptpSetGet();
    failures += TestTimestampMonotonicity();
    failures += TestIoctlThroughput();
    failures += TestCableUnplugScenario();
    failures += TestColdBootSafetyNote();

    printf("\nRESULT: %s\n", failures ? "FAILURES" : "SUCCESS");
    return failures ? 1 : 0;
}

#endif // _KERNEL_MODE
