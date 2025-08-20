#pragma warning(disable:4201)  //nonstandard extension used : nameless struct/union

// Define for Intel library Windows kernel compatibility
#define INTEL_WIN32_KERNEL_MODE 1

#include <ndis.h>
#include <filteruser.h>
#include "flt_dbg.h"
#include "filter.h"

// Windows kernel compatibility definitions for Intel AVB library
#ifndef _WIN32_KERNEL_COMPAT_H_
#define _WIN32_KERNEL_COMPAT_H_

// Define POSIX error codes that are missing in Windows kernel
#define ENOTSUP         129     // Operation not supported
#define ETIMEDOUT       110     // Connection timed out
#define EINVAL          22      // Invalid argument
#define EBUSY           16      // Device or resource busy
#define EIO             5       // I/O error
#define ENOMEM          12      // Out of memory

// Simple snprintf replacement for kernel mode - just use basic formatting
// This is a minimal implementation for the Intel library's simple usage
static inline int kernel_snprintf(char *buffer, size_t size, const char *format, ...)
{
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(format);
    // For now, just clear the buffer - the Intel library only uses this for device info strings
    if (buffer && size > 0) {
        buffer[0] = '\0';
    }
    return 0;
}

#define snprintf kernel_snprintf

#endif // _WIN32_KERNEL_COMPAT_H_

#include "avb_integration.h"
#include "tsn_config.h"

// Auto-generated Intel Ethernet register maps (SSOT)
// Headers are generated from intel-ethernet-regs/devices/*.yaml
// Do not edit manually; run the generator task to update.
#include "intel-ethernet-regs/gen/i210_regs.h"
#include "intel-ethernet-regs/gen/i217_regs.h"
#include "intel-ethernet-regs/gen/i219_regs.h"
#include "intel-ethernet-regs/gen/i225_regs.h"
#include "intel-ethernet-regs/gen/i226_regs.h"

