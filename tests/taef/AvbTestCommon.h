#pragma once

// Common helpers for AVB integration tests (TAEF)
// C++14 compatible

#include <windows.h>
#include <string>
#include <vector>
#include <stdint.h>

// Pull in SSOT public IOCTL contract 
#include "../../include/avb_ioctl.h"

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

    template<typename T>
    inline bool SimpleIoctl(DWORD code, T& req, DWORD& bytes) {
        UniqueHandle dev = OpenAvbDevice();
        if (!dev.valid()) return false;
        return ::DeviceIoControl(dev.get(), code, &req, (DWORD)sizeof(T), &req, (DWORD)sizeof(T), &bytes, nullptr) != 0;
    }

    inline std::string GetLastErrorString() {
        DWORD error = ::GetLastError();
        CHAR buffer[512];
        if (::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, buffer, sizeof(buffer), nullptr) == 0) {
            return "Unknown error";
        }
        return std::string(buffer);
    }
}
