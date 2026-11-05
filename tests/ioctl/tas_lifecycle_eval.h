/*
 * tas_lifecycle_eval.h - what a FilterPause/FilterRestart run may claim from its evidence (#364).
 *
 * Pure logic, shared by tests/diagnostic/tas_gate_investigation.c and its offline cases in test_tas_restore_contract.c
 * (TC-MOCK-014 to 017). Passing those cases says nothing about hardware. Output of the tool is LOCAL_DIAGNOSTIC.
 *
 * IOCTL_FILTER_RESTART_ONE_INSTANCE returns success when no instance matched and when NdisFRestartFilter failed, so its
 * result is not evidence. FilterPauseCount and FilterRestartCount count entries only, and FilterRestartCount moves before
 * the bring-up that FilterRestart runs, so they are not completion either. NDIS restarts the drivers above a filter after
 * it, so frames sent by a protocol, carried through the stack and seen on an observer after the Restart entry are the
 * only completion evidence the driver's interfaces allow. That is an inference from documented NDIS order: NDIS's own
 * state events are the independent check.
 *
 * Verifies: #364
 */
#pragma once

#include <windows.h>
#include <wchar.h>
#include "../../include/avb_ioctl.h"

typedef enum {
    TAS_INSTANCE_FOUND = 0,
    TAS_INSTANCE_NONE,
    TAS_INSTANCE_AMBIGUOUS,   /* the driver could restart a different instance than the one meant */
    TAS_INSTANCE_MALFORMED
} TAS_INSTANCE_RESULT;

typedef enum {
    TAS_LIFECYCLE_NOT_TRIGGERED = 0,  /* no counter moved: the trigger reached nothing */
    TAS_LIFECYCLE_PAUSE_ONLY,         /* Pause entered, Restart not entered within the bound */
    TAS_LIFECYCLE_RESTART_ENTERED,    /* both entered once, completion not shown */
    TAS_LIFECYCLE_RESUMED,            /* both entered once, then traffic through the whole stack */
    TAS_LIFECYCLE_CONTEXT_REPLACED,   /* detach, attach or a counter that fell: not a pause and restart of this context */
    TAS_LIFECYCLE_OVERLAPPED          /* counters cannot be attributed to one trigger */
} TAS_LIFECYCLE_OUTCOME;

typedef enum {
    TAS_PHC_UNMEASURED = 0,
    TAS_PHC_DISCONTINUOUS,    /* did not advance: needs no tolerance to claim */
    TAS_PHC_NOT_JUDGED        /* advanced: continuity needs a hardware-derived tolerance, and none exists */
} TAS_PHC_FACT;

typedef struct {
    LONGLONG pause_delta;       /* FilterPauseCount */
    LONGLONG restart_delta;     /* FilterRestartCount */
    LONGLONG generation_delta;  /* PauseRestartGeneration */
    LONGLONG detach_delta;      /* FilterDetachCount */
    LONGLONG attach_delta;      /* FilterAttachCount */
    BOOL traffic_shown;         /* frames sent after the Restart entry reached the observer */
} TAS_LIFECYCLE_EVIDENCE;

/* Whether the instance belongs to the adapter. Layout {interface GUID}-{filter GUID}-0000, see TC-MOCK-014 for its evidence */
static __inline BOOL tas_name_is_adapter(const WCHAR *name, size_t name_chars, const WCHAR *adapter_guid)
{
    size_t guid_chars = wcslen(adapter_guid);

    return name_chars > guid_chars && _wcsnicmp(name, adapter_guid, guid_chars) == 0 && name[guid_chars] == L'-';
}

/*
 * Finds the one record of IOCTL_FILTER_ENUERATE_ALL_INSTANCES that names the adapter: USHORT byte length, then the name
 * in UTF-16 without a terminator, repeated. filterFindFilterModule stops at the first instance whose name is a prefix of
 * the request, so another record that is a prefix of the result would be restarted in its place.
 */
static __inline TAS_INSTANCE_RESULT tas_instance_find(const BYTE *list, DWORD list_bytes, const WCHAR *adapter_guid,
                                                      const WCHAR **name, USHORT *name_bytes)
{
    const BYTE *found = NULL;
    USHORT found_bytes = 0;
    DWORD at;

    for (at = 0; at < list_bytes;) {
        USHORT bytes;

        if (list_bytes - at < sizeof(USHORT)) {
            return TAS_INSTANCE_MALFORMED;
        }
        memcpy(&bytes, list + at, sizeof(bytes));
        at += sizeof(USHORT);
        if ((bytes % sizeof(WCHAR)) != 0 || bytes > list_bytes - at) {
            return TAS_INSTANCE_MALFORMED;
        }
        if (tas_name_is_adapter((const WCHAR *)(list + at), bytes / sizeof(WCHAR), adapter_guid)) {
            if (found != NULL) {
                return TAS_INSTANCE_AMBIGUOUS;
            }
            found = list + at;
            found_bytes = bytes;
        }
        at += bytes;
    }
    if (found == NULL) {
        return TAS_INSTANCE_NONE;
    }
    for (at = 0; at < list_bytes;) {
        USHORT bytes;

        memcpy(&bytes, list + at, sizeof(bytes));
        at += sizeof(USHORT);
        if (list + at != found && bytes <= found_bytes && memcmp(list + at, found, bytes) == 0) {
            return TAS_INSTANCE_AMBIGUOUS;
        }
        at += bytes;
    }
    *name = (const WCHAR *)found;
    *name_bytes = found_bytes;
    return TAS_INSTANCE_FOUND;
}

static __inline TAS_LIFECYCLE_OUTCOME tas_lifecycle_outcome(const TAS_LIFECYCLE_EVIDENCE *e)
{
    if (e->detach_delta != 0 || e->attach_delta != 0 || e->pause_delta < 0 || e->restart_delta < 0 || e->generation_delta < 0) {
        return TAS_LIFECYCLE_CONTEXT_REPLACED;
    }
    if (e->pause_delta == 0 && e->restart_delta == 0 && e->generation_delta == 0) {
        return TAS_LIFECYCLE_NOT_TRIGGERED;
    }
    if (e->pause_delta != 1 || e->generation_delta != 1 || e->restart_delta > 1) {
        return TAS_LIFECYCLE_OVERLAPPED;
    }
    if (e->restart_delta == 0) {
        return TAS_LIFECYCLE_PAUSE_ONLY;
    }
    return e->traffic_shown ? TAS_LIFECYCLE_RESUMED : TAS_LIFECYCLE_RESTART_ENTERED;
}

static __inline void tas_lifecycle_evidence(const AVB_DRIVER_STATISTICS *before, const AVB_DRIVER_STATISTICS *after, BOOL traffic_shown,
                                            TAS_LIFECYCLE_EVIDENCE *e)
{
    e->pause_delta = (LONGLONG)(after->FilterPauseCount - before->FilterPauseCount);
    e->restart_delta = (LONGLONG)(after->FilterRestartCount - before->FilterRestartCount);
    e->generation_delta = (LONGLONG)(after->PauseRestartGeneration - before->PauseRestartGeneration);
    e->detach_delta = (LONGLONG)(after->FilterDetachCount - before->FilterDetachCount);
    e->attach_delta = (LONGLONG)(after->FilterAttachCount - before->FilterAttachCount);
    e->traffic_shown = traffic_shown;
}

static __inline const char *tas_lifecycle_name(TAS_LIFECYCLE_OUTCOME o)
{
    switch (o) {
    case TAS_LIFECYCLE_NOT_TRIGGERED:    return "NOT_TRIGGERED";
    case TAS_LIFECYCLE_PAUSE_ONLY:       return "PAUSE_ONLY";
    case TAS_LIFECYCLE_RESTART_ENTERED:  return "RESTART_ENTERED_COMPLETION_NOT_SHOWN";
    case TAS_LIFECYCLE_RESUMED:          return "RESUMED_BY_TRAFFIC";
    case TAS_LIFECYCLE_CONTEXT_REPLACED: return "CONTEXT_REPLACED";
    default:                             return "OVERLAPPED";
    }
}

static __inline TAS_PHC_FACT tas_phc_fact(BOOL before_ok, BOOL after_ok, ULONGLONG before, ULONGLONG after)
{
    if (!before_ok || !after_ok) {
        return TAS_PHC_UNMEASURED;
    }
    return after <= before ? TAS_PHC_DISCONTINUOUS : TAS_PHC_NOT_JUDGED;
}

static __inline const char *tas_phc_fact_name(TAS_PHC_FACT f)
{
    switch (f) {
    case TAS_PHC_DISCONTINUOUS: return "DISCONTINUOUS";
    case TAS_PHC_NOT_JUDGED:    return "NOT_JUDGED_NO_TOLERANCE";
    default:                    return "UNMEASURED";
    }
}

/* A register the family does not define is not compared. One that could not be read is unmeasured, never a difference */
static __inline void tas_snapshot_compare(const AVB_HW_SNAPSHOT_REQUEST *before, const AVB_HW_SNAPSHOT_REQUEST *after,
                                          unsigned *compared, unsigned *changed, unsigned *unreadable)
{
    unsigned i;

    *compared = *changed = *unreadable = 0;
    for (i = 0; i < AVB_HWREG_COUNT; i++) {
        unsigned was = i < before->count ? before->entries[i].state : AVB_HWREG_STATE_READ_FAILED;
        unsigned now = i < after->count ? after->entries[i].state : AVB_HWREG_STATE_READ_FAILED;

        if (was == AVB_HWREG_STATE_NOT_IN_SSOT || was == AVB_HWREG_STATE_NO_HAL) {
            if (now != was) {
                (*unreadable)++;
            }
        } else if (was != AVB_HWREG_STATE_VALID || now != AVB_HWREG_STATE_VALID) {
            (*unreadable)++;
        } else {
            (*compared)++;
            if (before->entries[i].value != after->entries[i].value) {
                (*changed)++;
            }
        }
    }
}
