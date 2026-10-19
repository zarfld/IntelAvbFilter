/*
 * avb_ntstatus.h - raw NTSTATUS of one IOCTL (NtDeviceIoControlFile)
 *
 * WHY THIS EXISTS (and why it is deliberately tiny, not a universal IOCTL wrapper):
 *   - IOCTLs are METHOD_BUFFERED.  The I/O manager copies the output buffer back only for
 *     non-error completions, so a `status` field the driver fills in before returning an
 *     error NTSTATUS is NEVER delivered to the caller.
 *   - GetLastError() is lossy: STATUS_NOT_IMPLEMENTED and STATUS_INVALID_DEVICE_REQUEST both
 *     become ERROR_INVALID_FUNCTION (1).  The verdict contract needs to tell NOT_IMPLEMENTED
 *     (SKIP_NOT_IMPLEMENTED) from NOT_SUPPORTED (SKIP_UNSUPPORTED / FAIL) and to preserve the
 *     ORIGINAL status in the evidence, so the exact NTSTATUS must be captured.
 *   Use it only where a verdict depends on the exact error status; ordinary calls keep using
 *   DeviceIoControl.
 *
 * Contract: docs/testing/test-verdict-contract.md.
 * The handle must be synchronous (as every AVB test opens it); a STATUS_PENDING completion is
 * waited for, never abandoned (the IO_STATUS_BLOCK lives on this stack frame).
 */
#pragma once

#include <windows.h>
#include <winternl.h>
#include "avb_verdict.h"

typedef NTSTATUS (NTAPI *AVB_PFN_NtDeviceIoControlFile)(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode,
    PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);

/* Returns the exact NTSTATUS the driver completed the IRP with, or AVB_NTSTATUS_UNAVAILABLE
 * if it could not be observed (never a fabricated 0). */
static __inline LONG avb_ioctl_ntstatus(HANDLE h, DWORD code,
                                        LPVOID in_buf, DWORD in_sz,
                                        LPVOID out_buf, DWORD out_sz,
                                        LPDWORD bytes_ret)
{
    static AVB_PFN_NtDeviceIoControlFile pfn = NULL;
    IO_STATUS_BLOCK iosb;
    HANDLE ev;
    NTSTATUS st;

    if (bytes_ret) *bytes_ret = 0;
    if (!pfn) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) pfn = (AVB_PFN_NtDeviceIoControlFile)(void *)GetProcAddress(nt, "NtDeviceIoControlFile");
    }
    if (!pfn) return AVB_NTSTATUS_UNAVAILABLE;

    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev) return AVB_NTSTATUS_UNAVAILABLE;

    memset(&iosb, 0, sizeof(iosb));
    st = pfn(h, ev, NULL, NULL, &iosb, code, in_buf, in_sz, out_buf, out_sz);
    if (st == STATUS_PENDING) {
        WaitForSingleObject(ev, INFINITE);
        st = iosb.Status;
    }
    CloseHandle(ev);
    if (bytes_ret) *bytes_ret = (DWORD)iosb.Information;
    return st;
}
