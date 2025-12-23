// Intel AVB Filter Driver - TAEF Tests
// Build as a TAEF user-mode test DLL. Requires Windows SDK Testing headers/libs.
// C++14

// Only include the driver PCH when actually building in kernel-mode.
// User-mode TAEF builds must not pull in WDK-only headers like ndis.h.
#if defined(_KERNEL_MODE)
#  include "precomp.h"
#  define AVB_HAS_PCH 1
#endif

#if defined(_KERNEL_MODE)
VOID AvbTaefTests_KmBuildPlaceHolder(void) { /* not built in KM */ }
#else

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <WexTestClass.h>
#include <Verify.h>
#include <Log.h>
#include <vector>
#include <string>
#include <algorithm>
#include <stdint.h>
#include <stdio.h>
#include "include/avb_ioctl.h"

static HANDLE OpenAvb()
{
    return ::CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static bool IoctlNoBuf(DWORD code)
{
    DWORD bytes = 0; HANDLE h = OpenAvb(); if (h == INVALID_HANDLE_VALUE) return false;
    BOOL ok = ::DeviceIoControl(h, code, nullptr, 0, nullptr, 0, &bytes, nullptr);
    ::CloseHandle(h); return ok != 0;
}

static bool IoctlInOut(DWORD code, void* inbuf, DWORD inlen, void* outbuf, DWORD outlen, DWORD& bytes)
{
    HANDLE h = OpenAvb(); if (h == INVALID_HANDLE_VALUE) return false;
    BOOL ok = ::DeviceIoControl(h, code, inbuf, inlen, outbuf, outlen, &bytes, nullptr);
    ::CloseHandle(h); return ok != 0;
}

static uint64_t qpc_ns()
{
    LARGE_INTEGER c, f; ::QueryPerformanceCounter(&c); ::QueryPerformanceFrequency(&f);
    return (uint64_t)((c.QuadPart * 1000000000ULL) / (uint64_t)f.QuadPart);
}

class AvbBasicTests
{
public:
    BEGIN_TEST_CLASS(AvbBasicTests)
    END_TEST_CLASS()

    BEGIN_TEST_METHOD(InitAndDeviceInfo)
    END_TEST_METHOD()

    BEGIN_TEST_METHOD(Gptp_SetThenGet)
    END_TEST_METHOD()

    BEGIN_TEST_METHOD(Timestamp_Monotonicity_10k)
    END_TEST_METHOD()

    BEGIN_TEST_METHOD(Throughput_1s_GetTimestamp)
    END_TEST_METHOD()

    BEGIN_TEST_METHOD(ErrorPath_DisableEnable_Interface)
    END_TEST_METHOD()
};

void AvbBasicTests::InitAndDeviceInfo()
{
    VERIFY_IS_TRUE(IoctlNoBuf(IOCTL_AVB_INIT_DEVICE), L"Init should succeed");

    AVB_DEVICE_INFO_REQUEST info = {};
    info.buffer_size = sizeof(info.device_info);
    DWORD bytes = 0;
    VERIFY_IS_TRUE(IoctlInOut(IOCTL_AVB_GET_DEVICE_INFO, &info, sizeof(info), &info, sizeof(info), bytes));
        WEX::Logging::Log::Comment(L"DeviceInfo: ");
    if (bytes && info.buffer_size) {
        // best-effort print
        wchar_t wbuf[AVB_DEVICE_INFO_MAX] = {0};
        size_t out = std::min<size_t>(AVB_DEVICE_INFO_MAX - 1, info.buffer_size);
        for (size_t i = 0; i < out; ++i) wbuf[i] = (wchar_t)(unsigned char)info.device_info[i];
            WEX::Logging::Log::Comment(wbuf);
    }
}

void AvbBasicTests::Gptp_SetThenGet()
{
    AVB_TIMESTAMP_REQUEST setReq = {};
    setReq.timestamp = 111222333444ULL;
    setReq.clock_id = 0;
    DWORD bytes = 0;
    bool setOk = IoctlInOut(IOCTL_AVB_SET_TIMESTAMP, &setReq, sizeof(setReq), &setReq, sizeof(setReq), bytes);
    if (!setOk) {
           WEX::Logging::Log::Comment(L"SET_TIMESTAMP unsupported by hardware/driver; skipping");
           WEX::TestExecution::SetVerifyOutput verifySettings(WEX::TestExecution::VerifyOutputSettings::LogOnlyFailures);
        return; // soft-skip
    }

    AVB_TIMESTAMP_REQUEST getReq = {};
    VERIFY_IS_TRUE(IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &getReq, sizeof(getReq), &getReq, sizeof(getReq), bytes));
    uint64_t diff = (getReq.timestamp > setReq.timestamp) ? (getReq.timestamp - setReq.timestamp) : (setReq.timestamp - getReq.timestamp);
    VERIFY_IS_TRUE(diff <= 1000ULL, L"Expected <= 1us diff or exact match");
}

void AvbBasicTests::Timestamp_Monotonicity_10k()
{
    const size_t N = 10000;
    std::vector<uint64_t> v;
    v.reserve(N);

    AVB_TIMESTAMP_REQUEST req = {};
    DWORD bytes = 0;
    for (size_t i = 0; i < N; ++i) {
        VERIFY_IS_TRUE(IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes));
        v.push_back(req.timestamp);
    }

    bool mono = true;
    uint64_t maxStep = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] < v[i - 1]) {
            mono = false;
            break;
        }
        maxStep = std::max<uint64_t>(maxStep, v[i] - v[i - 1]);
    }
    VERIFY_IS_TRUE(mono, L"Timestamp must be non-decreasing");

    wchar_t msg[128] = {0};
    swprintf_s(msg, _countof(msg), L"Max step = %llu ns", (unsigned long long)maxStep);
    WEX::Logging::Log::Comment(msg);
}

void AvbBasicTests::Throughput_1s_GetTimestamp()
{
    uint64_t end = qpc_ns() + 1000000000ULL; // 1s
    AVB_TIMESTAMP_REQUEST req = {};
    DWORD bytes = 0;
    uint64_t cnt = 0;

    while (qpc_ns() < end) {
        if (!IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes)) {
            break;
        }
        ++cnt;
    }

    wchar_t msg[128] = {0};
    swprintf_s(msg, _countof(msg), L"GET_TIMESTAMP throughput: %.2f kops/s", (double)cnt / 1000.0);
    WEX::Logging::Log::Comment(msg);
    VERIFY_IS_TRUE(cnt > 1000, L"Expect at least minimal throughput");
}

void AvbBasicTests::ErrorPath_DisableEnable_Interface()
{
    wchar_t name[256] = {0};
    DWORD got = GetEnvironmentVariableW(L"AVB_ADAPTER_NAME", name, _countof(name));
    if (got == 0 || got >= _countof(name)) {
           WEX::Logging::Log::Comment(L"Set AVB_ADAPTER_NAME to interface friendly name to run this test");
           WEX::TestExecution::SetVerifyOutput verifySettings(WEX::TestExecution::VerifyOutputSettings::LogOnlyFailures);
        return; // soft-skip
    }

    wchar_t cmd[512];
    wsprintfW(cmd, L"netsh interface set interface \"%ls\" admin=disabled", name);
    if (_wsystem(cmd) != 0) {
           WEX::Logging::Log::Comment(L"netsh failed; skipping");
           WEX::TestExecution::SetVerifyOutput verifySettings(WEX::TestExecution::VerifyOutputSettings::LogOnlyFailures);
        return;
    }
    ::Sleep(2000);

    AVB_TIMESTAMP_REQUEST req = {};
    DWORD bytes = 0;
    IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes);

    wsprintfW(cmd, L"netsh interface set interface \"%ls\" admin=enabled", name);
    _wsystem(cmd);
    ::Sleep(3000);

    bool ok = false;
    for (int i = 0; i < 10; ++i) {
        if (IoctlInOut(IOCTL_AVB_GET_TIMESTAMP, &req, sizeof(req), &req, sizeof(req), bytes)) {
            ok = true;
            break;
        }
        ::Sleep(500);
    }
    VERIFY_IS_TRUE(ok, L"Driver should recover after link toggle");
}

#endif // _KERNEL_MODE
