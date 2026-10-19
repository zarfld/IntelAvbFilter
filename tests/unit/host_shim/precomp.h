/*
 * Test-only host shim (NOT a production header): lets a device implementation under devices/ be compiled
 * unchanged in a user-mode unit test.  It replaces the kernel precompiled header with just the types and
 * macros devices/*.c needs.  Use with: -I tests/unit/host_shim -I . -I include -I external/intel_avb/lib
 */
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef RtlCopyMemory
#define RtlCopyMemory(d, s, n) memcpy((d), (s), (n))
#endif
#ifndef RtlZeroMemory
#define RtlZeroMemory(d, n) memset((d), 0, (n))
#endif

#define DL_ERROR 0
#define DL_WARN  1
#define DL_INFO  2
#define DL_TRACE 3
#define DEBUGP(lvl, ...) ((void)0)

#include "external/intel_avb/lib/intel.h"
#include "external/intel_avb/lib/intel_private.h"
#include "include/intel_pci_ids.h"
#include "intel-ethernet-regs/gen/i210_regs.h"
#include "intel-ethernet-regs/gen/i217_regs.h"
#include "intel-ethernet-regs/gen/i219_regs.h"
#include "intel-ethernet-regs/gen/i225_regs.h"
#include "intel-ethernet-regs/gen/i226_regs.h"
