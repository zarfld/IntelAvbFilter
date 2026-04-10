/**
 * @file avb_test_common.h
 * @brief Shared test helpers for IntelAvbFilter user-mode test executables
 *
 * Provides:
 *  - Adapter enumeration via IOCTL_AVB_ENUM_ADAPTERS (global index ordering)
 *  - Per-adapter handle opening via IOCTL_AVB_OPEN_ADAPTER
 *  - Capability gate macros (AVB_SKIP_TEST_IF_NO_CAP)
 *  - Register read/write/verify wrappers
 *  - TEST_PASS / TEST_FAIL / TEST_SKIP reporting and statistics
 *
 * SSOT: All IOCTL definitions come from #include "../../include/avb_ioctl.h"
 *
 * Index semantics:
 *   IOCTL_AVB_ENUM_ADAPTERS returns adapters at global positions 0..N-1 in
 *   FilterModuleList order.  IOCTL_AVB_OPEN_ADAPTER accepts the same global
 *   index in req->index.  AvbAdapterInfo.global_index is passed directly to
 *   OPEN_ADAPTER; no per-DID translation is required.
 */

#ifndef AVB_TEST_COMMON_H
#define AVB_TEST_COMMON_H

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"
/* Single Source of Truth for PCI device ID constants */
#include "../../include/intel_pci_ids.h"

/* =========================================================
 * Configuration
 * ========================================================= */

#define AVB_MAX_ADAPTERS    8
#define AVB_DEVICE_PATH     "\\\\.\\IntelAvbFilter"

/* =========================================================
 * Per-adapter info filled by AvbEnumerateAdapters()
 * ========================================================= */

typedef struct AvbAdapterInfo {
    avb_u16  vendor_id;             /* Always 0x8086 for Intel */
    avb_u16  device_id;             /* PCI DID: 0x125B=I226, 0x1533=I210, 0x15B7=I219, etc. */
    avb_u32  capabilities;          /* INTEL_CAP_* bitmask */
    avb_u32  global_index;          /* Position in ENUM_ADAPTERS global order; pass to OPEN_ADAPTER */
    char     device_name[16];       /* E.g. "I226-LM", "I210", "I219-LM" */
} AvbAdapterInfo;

/* =========================================================
 * Test result type
 * ========================================================= */

typedef enum {
    AVB_TEST_RESULT_PASS = 0,
    AVB_TEST_RESULT_FAIL = 1,
    AVB_TEST_RESULT_SKIP = 2
} AvbTestResult;

/* =========================================================
 * Test statistics
 * ========================================================= */

typedef struct AvbTestStats {
    int total;
    int passed;
    int failed;
    int skipped;
} AvbTestStats;

/* =========================================================
 * Reporting macros
 * ========================================================= */

#define AVB_REPORT_PASS(stats, name) do { \
    printf("  [PASS] %s\n", (name)); \
    (stats)->passed++; (stats)->total++; \
} while(0)

#define AVB_REPORT_FAIL(stats, name, reason) do { \
    printf("  [FAIL] %s: %s\n", (name), (reason)); \
    (stats)->failed++; (stats)->total++; \
} while(0)

#define AVB_REPORT_SKIP(stats, name, reason) do { \
    printf("  [SKIP] %s: %s\n", (name), (reason)); \
    (stats)->skipped++; (stats)->total++; \
} while(0)

/* =========================================================
 * Capability gate macros
 * ========================================================= */

/** Non-zero if adapter has the named capability */
#define AVB_HAS_CAP(info, cap)  (((info)->capabilities & (cap)) != 0)

/** Skip the current test function if adapter lacks a capability.
 *  Returns AVB_TEST_RESULT_SKIP from the calling function. */
#define AVB_SKIP_TEST_IF_NO_CAP(info, cap, test_name) \
    do { \
        if (!AVB_HAS_CAP((info), (cap))) { \
            printf("  [SKIP] %s: capability 0x%08X not present on DID=0x%04X\n", \
                   (test_name), (unsigned)(cap), (unsigned)(info)->device_id); \
            return AVB_TEST_RESULT_SKIP; \
        } \
    } while(0)

/* =========================================================
 * Public API
 * ========================================================= */

/**
 * Enumerate all Intel adapters visible to the driver.
 *
 * @param out       Caller-supplied array of at least AVB_MAX_ADAPTERS entries.
 * @param max_count Maximum entries to fill (use AVB_MAX_ADAPTERS).
 * @return          Number of adapters found (0 on failure or no adapters).
 *
 * Fills out[i].global_index with the ENUM_ADAPTERS position, which is the
 * correct value to pass to AvbOpenAdapter() / IOCTL_AVB_OPEN_ADAPTER.
 */
int AvbEnumerateAdapters(AvbAdapterInfo *out, int max_count);

/**
 * Open an exclusive handle to the adapter described by @p info.
 *
 * Uses IOCTL_AVB_OPEN_ADAPTER with info->global_index and info->device_id so
 * subsequent IOCTLs on the returned handle are routed to the correct NIC.
 *
 * @return  Open HANDLE on success, INVALID_HANDLE_VALUE on failure.
 *          Call GetLastError() for the Win32 error code.
 */
HANDLE AvbOpenAdapter(const AvbAdapterInfo *info);

/**
 * Return a human-readable name for a PCI Device ID.
 * The returned pointer is a string literal — do not free it.
 */
const char *AvbDeviceName(avb_u16 device_id);

/**
 * Format the INTEL_CAP_* capability bitmask into a comma-separated string.
 *
 * @param caps  Bitmask from AVB_ENUM_REQUEST.capabilities.
 * @param buf   Caller-supplied buffer.
 * @param sz    Size of @p buf in bytes.
 * @return      @p buf (always NUL-terminated).
 */
char *AvbCapabilityString(avb_u32 caps, char *buf, size_t sz);

/**
 * Read one 32-bit BAR0 register via IOCTL_AVB_READ_REGISTER.
 *
 * @return  0 on success, Win32 error code on failure.
 */
int AvbReadReg(HANDLE h, avb_u32 offset, avb_u32 *value);

/**
 * Write one 32-bit BAR0 register via IOCTL_AVB_WRITE_REGISTER.
 *
 * @return  0 on success, Win32 error code on failure.
 */
int AvbWriteReg(HANDLE h, avb_u32 offset, avb_u32 value);

/**
 * Write then read-back a register, printing PASS/FAIL with @p tag.
 *
 * @return  0 on success (write succeeded and readback matches), non-zero on error.
 */
int AvbWriteRegVerified(HANDLE h, avb_u32 offset, avb_u32 value, const char *tag);

/**
 * Print a summary banner and return 0 if all tests passed/skipped,
 * 1 if any failed.
 */
int AvbPrintSummary(const AvbTestStats *stats);

#endif /* AVB_TEST_COMMON_H */
