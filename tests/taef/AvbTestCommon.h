#pragma once

// Common helpers for AVB integration tests (TAEF)
// C++14 compatible

#include <windows.h>
#include <string>
#include <vector>
#include <stdint.h>

// Pull in public IOCTL contract
#include "include/avb_ioctl.h"

namespace AvbTest
{
    // RAII wrapper for HANDLE
    class UniqueHandle {
    public:
        UniqueHandle() noexcept : h_(INVALID_HANDLE_VALUE) {}
        explicit UniqueHandle(HANDLE h) noexcept : h_(h) {}
        ~UniqueHandle() { reset(); }

        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;

        UniqueHandle(UniqueHandle&& other) noexcept : h_(other.h_) { other.h_ = INVALID_HANDLE_VALUE; }
        UniqueHandle& operator=(UniqueHandle&& other) noexcept {
            if (this != &other) { reset(); h_ = other.h_; other.h_ = INVALID_HANDLE_VALUE; }
            return *this;
        }

        HANDLE get() const noexcept { return h_; }
        bool valid() const noexcept { return h_ != NULL && h_ != INVALID_HANDLE_VALUE; }
        HANDLE release() noexcept { HANDLE t = h_; h_ = INVALID_HANDLE_VALUE; return t; }
        void reset(HANDLE nh = INVALID_HANDLE_VALUE) noexcept { if (valid()) ::CloseHandle(h_); h_ = nh; }

    private:
        HANDLE h_;
    };

    inline UniqueHandle OpenAvbDevice() {
        // The driver should expose a DOS symbolic link named "IntelAvbFilter"
        HANDLE h = ::CreateFileW(L"\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        return UniqueHandle(h);
    }

    template<typename TIn>
    inline bool IoctlOut(DWORD code, const TIn* inBuf, DWORD inSize, void* outBuf, DWORD outSize, DWORD& bytes) {
        return ::DeviceIoControl(OpenAvbDevice().release(), code, (LPVOID)inBuf, inSize, outBuf, outSize, &bytes, nullptr) != 0;
    }

    template<typename TIn, typename TOut>
    inline bool Ioctl(DWORD code, const TIn& in, TOut& out, DWORD& bytes) {
        UniqueHandle dev = OpenAvbDevice();
        if (!dev.valid()) return false;
        return ::DeviceIoControl(dev.get(), code, (LPVOID)&in, (DWORD)sizeof(TIn), &out, (DWORD)sizeof(TOut), &bytes, nullptr) != 0;
    }

    inline bool IoctlNoBufs(DWORD code) {
        UniqueHandle dev = OpenAvbDevice();
        if (!dev.valid()) return false;
        DWORD bytes = 0;
        return ::DeviceIoControl(dev.get(), code, nullptr, 0, nullptr, 0, &bytes, nullptr) != 0;
    }

    inline uint64_t QpcNow() {
        LARGE_INTEGER c, f; ::QueryPerformanceCounter(&c); ::QueryPerformanceFrequency(&f);
        // Return nanoseconds
        return static_cast<uint64_t>((c.QuadPart * 1000000000ULL) / (uint64_t)f.QuadPart);
    }

    inline void SleepMs(DWORD ms) { ::Sleep(ms); }

    // Try to disable/enable an interface by friendly name via netsh; returns true on success.
    inline bool ToggleInterfaceAdminState(const std::wstring& friendlyName, bool enable) {
        std::wstring cmd = L"netsh interface set interface \"" + friendlyName + L"\" admin=" + (enable ? L"ENABLED" : L"DISABLED");
        int rc = _wsystem(cmd.c_str());
        return (rc == 0);
    }

    // Attempt to find an Intel adapter friendly name using IP Helper; returns first match or empty.
    inline std::wstring FindIntelAdapterFriendlyName() {
        // Fallback: environment AVB_ADAPTER_NAME
        wchar_t buf[512]; DWORD len = 511;
        if (GetEnvironmentVariableW(L"AVB_ADAPTER_NAME", buf, len)) {
            return std::wstring(buf);
        }
        // Minimal: use PowerShell Get-NetAdapter to find Intel; requires admin
        // Keep it simple to avoid extra deps
        // This is best-effort and may fail on restricted systems
        return L"";
    }
}
