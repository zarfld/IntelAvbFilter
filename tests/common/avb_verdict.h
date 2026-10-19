/*
 * avb_verdict.h - testcase verdict classification (pure, no I/O)
 *
 * Contract: docs/testing/test-verdict-contract.md.  The tags printed here are the exact
 * tags tools/test/TestVerdict.ps1 parses, so classification survives into JUnit.
 *
 *   status / evidence                                    verdict
 *   -----------------------------------------------------------------------------
 *   safety precondition unmet                            BLOCKED
 *   raw NTSTATUS not captured                            INCONCLUSIVE
 *   success + payload valid                              PASS
 *   success + payload invalid                            FAIL
 *   STATUS_NOT_IMPLEMENTED                               SKIP_NOT_IMPLEMENTED (never converted)
 *   STATUS_NOT_SUPPORTED + capability ABSENT             SKIP_UNSUPPORTED
 *   STATUS_NOT_SUPPORTED + capability PRESENT            FAIL   (advertised, yet "not supported")
 *   STATUS_NOT_SUPPORTED + capability UNKNOWN            INCONCLUSIVE
 *   STATUS_DEVICE_NOT_READY / STATUS_INVALID_DEVICE_STATE BLOCKED (never an unsupported-HW skip)
 *   any other error                                      FAIL
 *
 * "Capability" is evidence about THE OPERATION under test, from an authoritative
 * per-operation contract (e.g. INTEL_CAP_TSN_TAS for TAS).  A generic nonzero capability
 * mask is NOT evidence for a specific operation; use AVB_CAP_UNKNOWN for that case.
 *
 * NTSTATUS constants come from the Windows SDK <ntstatus.h>; none are redefined here.
 */
#pragma once

#include <windows.h>
#include <stdio.h>
#pragma warning(push)
#pragma warning(disable : 4005)   /* STATUS_* subset is also defined by winnt.h */
#include <ntstatus.h>
#pragma warning(pop)

typedef enum {
    AVB_VERDICT_PASS = 0,
    AVB_VERDICT_FAIL,
    AVB_VERDICT_SKIP_UNSUPPORTED,
    AVB_VERDICT_SKIP_NOT_IMPLEMENTED,
    AVB_VERDICT_BLOCKED,
    AVB_VERDICT_INCONCLUSIVE
} AVB_VERDICT;

/* Evidence about the hardware capability of the operation being tested. */
typedef enum {
    AVB_CAP_UNKNOWN = 0,   /* no authoritative per-operation contract available */
    AVB_CAP_ABSENT,        /* proven absent for this device and operation */
    AVB_CAP_PRESENT        /* advertised / proven present */
} AVB_CAP_EVIDENCE;

/* The raw NTSTATUS could not be observed (e.g. call path that only exposes a Win32 error). */
#define AVB_NTSTATUS_UNAVAILABLE ((NTSTATUS)0x7FFFFFFF)

static __inline const char *avb_verdict_tag(AVB_VERDICT v)
{
    switch (v) {
    case AVB_VERDICT_PASS:                 return "PASS";
    case AVB_VERDICT_FAIL:                 return "FAIL";
    case AVB_VERDICT_SKIP_UNSUPPORTED:     return "SKIP_UNSUPPORTED";
    case AVB_VERDICT_SKIP_NOT_IMPLEMENTED: return "SKIP_NOT_IMPLEMENTED";
    case AVB_VERDICT_BLOCKED:              return "BLOCKED";
    case AVB_VERDICT_INCONCLUSIVE:         return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";   /* unreachable for valid enum values; never a pass */
}

static __inline AVB_VERDICT avb_verdict_from_status(NTSTATUS ntstatus,
                                                    AVB_CAP_EVIDENCE cap,
                                                    int payload_valid,
                                                    int safe_to_proceed)
{
    if (!safe_to_proceed)
        return AVB_VERDICT_BLOCKED;
    if (ntstatus == AVB_NTSTATUS_UNAVAILABLE)
        return AVB_VERDICT_INCONCLUSIVE;
    /* NT_SUCCESS(): severity success/informational (non-negative as a signed value).
     * Warning (0x8...) and error (0xC...) severities are negative and fall through. */
    if (ntstatus >= 0)
        return payload_valid ? AVB_VERDICT_PASS : AVB_VERDICT_FAIL;

    switch (ntstatus) {
    case STATUS_NOT_IMPLEMENTED:
        return AVB_VERDICT_SKIP_NOT_IMPLEMENTED;
    case STATUS_NOT_SUPPORTED:
        if (cap == AVB_CAP_ABSENT)  return AVB_VERDICT_SKIP_UNSUPPORTED;
        if (cap == AVB_CAP_PRESENT) return AVB_VERDICT_FAIL;
        return AVB_VERDICT_INCONCLUSIVE;
    case STATUS_DEVICE_NOT_READY:
    case STATUS_INVALID_DEVICE_STATE:
        return AVB_VERDICT_BLOCKED;
    default:
        return AVB_VERDICT_FAIL;
    }
}

/*
 * Print-ready evidence line:
 *   [VERDICT] TC-ID[/adapter:N]: did=0xDDDD ntstatus=0xSSSSSSSS op=NAME reason
 * adapter < 0 omits the /adapter suffix.  did == 0 omits did=.
 * ntstatus == AVB_NTSTATUS_UNAVAILABLE prints "ntstatus=unavailable" (never a fake 0).
 */
static __inline void avb_verdict_format(char *buf, size_t n, AVB_VERDICT v,
                                        const char *tc_id, int adapter, unsigned did,
                                        NTSTATUS ntstatus, const char *op, const char *reason)
{
    char adapter_s[24] = "";
    char did_s[24]     = "";
    char status_s[32];
    if (adapter >= 0) _snprintf_s(adapter_s, sizeof(adapter_s), _TRUNCATE, "/adapter:%d", adapter);
    if (did != 0)     _snprintf_s(did_s, sizeof(did_s), _TRUNCATE, " did=0x%04X", did);
    if (ntstatus == AVB_NTSTATUS_UNAVAILABLE)
        _snprintf_s(status_s, sizeof(status_s), _TRUNCATE, "ntstatus=unavailable");
    else
        _snprintf_s(status_s, sizeof(status_s), _TRUNCATE, "ntstatus=0x%08X", (unsigned)ntstatus);
    _snprintf_s(buf, n, _TRUNCATE, "[%s] %s%s:%s %s op=%s %s",
                avb_verdict_tag(v), tc_id, adapter_s, did_s, status_s,
                op ? op : "-", reason ? reason : "");
}
