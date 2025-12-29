#pragma warning(disable:4201)  //nonstandard extension used : nameless struct/union

// Define for Intel library Windows kernel compatibility
#define INTEL_WIN32_KERNEL_MODE 1

#include <ndis.h>
#include <filteruser.h>
#include "flt_dbg.h"
#include "filter.h"

// Use Intel library headers for common types and device enums
#include "external/intel_avb/lib/intel.h"

// Kernel-mode memory allocation shims for Intel library (user-mode code)
// Intel library uses malloc/calloc/free which don't exist in kernel mode
#ifndef INTEL_KERNEL_MEMORY_SHIMS
#define INTEL_KERNEL_MEMORY_SHIMS

#ifdef __cplusplus
extern "C" {
#endif

_IRQL_requires_max_(DISPATCH_LEVEL)
__drv_allocatesMem(Mem)
_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(size)
static __inline void* malloc(size_t size) {
    if (size == 0) return NULL;
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FILTER_ALLOC_TAG);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
__drv_allocatesMem(Mem)
_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(num * size)
static __inline void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    if (total == 0) return NULL;
    void* ptr = ExAllocatePool2(POOL_FLAG_NON_PAGED, total, FILTER_ALLOC_TAG);
    if (ptr) {
        RtlZeroMemory(ptr, total);
    }
    return ptr;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
static __inline void free(void* ptr) {
    if (ptr) {
        ExFreePoolWithTag(ptr, FILTER_ALLOC_TAG);
    }
}

#ifdef __cplusplus
}
#endif

#endif // INTEL_KERNEL_MEMORY_SHIMS

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

