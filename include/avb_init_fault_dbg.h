/*
 * avb_init_fault_dbg.h - Debug-only adapter-initialization fault mechanism (#412), shared by the driver and its test.
 *
 * Implements: #412 (TEST-INTEGRITY: UT-DEV-LIFECYCLE-002 initialization after failed start)
 * Verifies:   #12 via #313 UT-DEV-LIFECYCLE-002 (tests/ioctl/test_ioctl_dev_lifecycle.c --init-retry-fault)
 *
 * WHAT: a testcase arms a finite list of actions for ONE adapter (by NET_LUID) in one SESSION (a test-chosen, non-zero id).
 * Each adapter-initialization attempt of that adapter that reaches the fault point (AvbPerformBasicInitialization,
 * immediately before its intel_init) consumes the first unconsumed action whose origin matches the attempt's origin. Every
 * attempt on the armed adapter, at the fault point and at the other intel_init sites, is written back as one record of that
 * session. Debug builds only: in a Release build the driver contains none of this (src/avb_init_fault_dbg.c is empty
 * outside DBG).
 *
 * Transport: REG_BINARY values under the driver service key's Parameters subkey
 * (HKLM\SYSTEM\CurrentControlSet\Services\IntelAvbFilter\Parameters), next to DebugLevel (#95):
 *   AVB_INITFAULT_ARM_VALUE                      written and deleted only by the driver, under its own lock: on the test's
 *                                                ARM/DISARM/CLEAR_UNCONSUMED request (IOCTL_AVB_DBG_INITFAULT_CONTROL, SSOT avb_ioctl.h)
 *                                                and per attempt (next_seq, inflight, consumed_by). The test only reads it
 *   AVB_INITFAULT_LOG_PREFIX <session>_<seq>     one record per attempt ("AvbInitFaultLog_%016llX_%02u")
 * Every name starts with AVB_INITFAULT_NAME_PREFIX, so residue is found by enumeration.
 *
 * Session isolation (PR #462 review): an attempt belongs to the session of the arm it entered under. Its record is published
 * only if, at its exit and under the same driver lock, the arm is still present with that session; otherwise the record is
 * discarded. A record's name carries its session, so an old attempt can never publish into a newer session's sequence
 * numbers. inflight counts the session's attempts between entry and exit; the test drains (waits, bounded, for inflight 0),
 * then asks the driver to disarm (the driver deletes the arm and every fault value under the admission lock), then
 * verifies by enumeration that nothing is left.
 *
 * Bounds: at most AVB_INITFAULT_MAX_ACTIONS actions, at most AVB_INITFAULT_MAX_RECORDS records per session, an absolute
 * expiry after which the driver deletes the arm and does nothing, and HOLD at most AVB_INITFAULT_MAX_HOLD_MS. A consumed
 * action never fires again (automatic disarm); once every action is consumed the arm only records.
 *
 * Waiting (HOLD) runs only for the INIT_DEVICE origins and only at PASSIVE_LEVEL, with no driver lock held; any other
 * origin's HOLD is consumed as AVB_INITFAULT_ACT_HOLD_REFUSED without waiting. Registry access needs PASSIVE_LEVEL: an
 * attempt reached at another IRQL is not armed or recorded, but counted (record field nonpassive_skipped).
 */
#ifndef AVB_INIT_FAULT_DBG_H
#define AVB_INIT_FAULT_DBG_H

#define AVB_INITFAULT_VERSION        2u
#define AVB_INITFAULT_MAX_ACTIONS    8u
#define AVB_INITFAULT_MAX_RECORDS    64u
#define AVB_INITFAULT_MAX_HOLD_MS    10000u

#define AVB_INITFAULT_NAME_PREFIX    L"AvbInitFault"
#define AVB_INITFAULT_ARM_VALUE      L"AvbInitFaultArm"
#define AVB_INITFAULT_LOG_PREFIX     L"AvbInitFaultLog_"
#define AVB_INITFAULT_PARAMS_SUBKEY  L"SYSTEM\\CurrentControlSet\\Services\\IntelAvbFilter\\Parameters"   /* test side */

/* Who started the initialization attempt (the caller of the bring-up) */
#define AVB_INITFAULT_ORIGIN_ATTACH        1u   /* FilterAttach -> AvbInitializeDevice (src/filter.c) */
#define AVB_INITFAULT_ORIGIN_RESTART       2u   /* FilterRestart -> AvbBringUpHardware (src/filter.c) */
#define AVB_INITFAULT_ORIGIN_CREATE_IOCTL  3u   /* lazy context creation for a routed IOCTL (src/device.c) */
#define AVB_INITFAULT_ORIGIN_CREATE_ENUM   4u   /* lazy context creation in ENUM_ADAPTERS (src/device.c) */
#define AVB_INITFAULT_ORIGIN_CREATE_OPEN   5u   /* lazy context creation in OPEN_ADAPTER (src/device.c) */
#define AVB_INITFAULT_ORIGIN_INIT_PRE      6u   /* former INIT_DEVICE pre-dispatch bring-up; removed by #404 part 2, no longer produced (value kept) */
#define AVB_INITFAULT_ORIGIN_INIT_HANDLER  7u   /* INIT_DEVICE handler bring-up, the only INIT_DEVICE initialization since #404 part 2 */
#define AVB_INITFAULT_ORIGIN_OPEN_HANDLER  8u   /* OPEN_ADAPTER handler bring-up / AvbEnsureDeviceReady */

/* Which intel_init call site the attempt is at. Only SITE_BASIC (the fault point) consumes actions */
#define AVB_INITFAULT_SITE_BASIC     1u   /* AvbPerformBasicInitialization, before the first intel_init: the fault point */
#define AVB_INITFAULT_SITE_BRINGUP   2u   /* AvbBringUpHardware, intel_init after a successful basic initialization */
#define AVB_INITFAULT_SITE_ENSURE    3u   /* AvbEnsureDeviceReady, intel_init (OPEN_ADAPTER) */
#define AVB_INITFAULT_SITE_FORCED    4u   /* former INIT_DEVICE forced BAR0 path; removed by #404 part 2, no longer produced (value kept) */

/* Action requested (ARM) and applied (RECORD) */
#define AVB_INITFAULT_ACT_NONE         0u   /* record only: no action matched */
#define AVB_INITFAULT_ACT_OBSERVE      1u   /* consumed, no effect: proves this attempt reached the fault point */
#define AVB_INITFAULT_ACT_FAIL         2u   /* consumed, intel_init is not called and the attempt fails as if it returned non-zero */
#define AVB_INITFAULT_ACT_HOLD         3u   /* consumed, waits hold_ms at the fault point, then intel_init runs */
#define AVB_INITFAULT_ACT_HOLD_REFUSED 4u   /* record only: a HOLD for an origin that may not wait; consumed, no wait */

typedef struct AVB_INITFAULT_ACTION {
    unsigned __int32 origin;       /* AVB_INITFAULT_ORIGIN_* this action applies to */
    unsigned __int32 action;       /* AVB_INITFAULT_ACT_OBSERVE / FAIL / HOLD */
    unsigned __int32 hold_ms;      /* HOLD only, at most AVB_INITFAULT_MAX_HOLD_MS */
    unsigned __int32 consumed_by;  /* 0 = not consumed; else 1 + seq of the record that consumed it (set by the driver) */
} AVB_INITFAULT_ACTION;                                                                                  /* 16 */

typedef struct AVB_INITFAULT_ARM {
    unsigned __int32 version;      /* AVB_INITFAULT_VERSION */
    unsigned __int32 count;        /* actions used, at most AVB_INITFAULT_MAX_ACTIONS */
    unsigned __int64 net_luid;     /* the one target adapter */
    unsigned __int64 expiry_100ns; /* absolute system time (UTC, 100 ns since 1601); later: the driver deletes the arm */
    unsigned __int64 session;      /* test-chosen, non-zero; every record of this arm carries it */
    unsigned __int32 next_seq;     /* next record sequence (driver-owned; the test writes 0) */
    unsigned __int32 inflight;     /* attempts of this session between entry and exit (driver-owned; the test writes 0) */
    AVB_INITFAULT_ACTION actions[AVB_INITFAULT_MAX_ACTIONS];
} AVB_INITFAULT_ARM;                                                                                     /* 168 */

/* AVB_INITFAULT_RECORD.action_index of an attempt that consumed no armed action (no action index can have this value) */
#define AVB_INITFAULT_ACTION_INDEX_NONE    0xFFFFFFFFu

typedef struct AVB_INITFAULT_RECORD {
    unsigned __int32 version;      /* AVB_INITFAULT_VERSION */
    unsigned __int32 seq;
    unsigned __int64 net_luid;     /* the adapter of the attempt (always the armed target) */
    unsigned __int64 session;      /* the session of the arm the attempt entered under */
    unsigned __int32 origin;       /* AVB_INITFAULT_ORIGIN_* */
    unsigned __int32 site;         /* AVB_INITFAULT_SITE_* */
    unsigned __int32 applied;      /* AVB_INITFAULT_ACT_* actually applied */
    unsigned __int32 action_index; /* index of the consumed action, AVB_INITFAULT_ACTION_INDEX_NONE if none */
    unsigned __int32 irql;         /* IRQL at the site */
    unsigned __int32 hw_state_before;
    unsigned __int32 hw_state_after;
    __int32          init_result;  /* intel_init return value, or -1 when ACT_FAIL replaced the call */
    unsigned __int32 hold_ms;      /* waited */
    unsigned __int32 thread_id;    /* low 32 bits of the kernel thread id */
    __int64          qpc_enter;    /* KeQueryPerformanceCounter: comparable with QueryPerformanceCounter */
    __int64          qpc_hold_end; /* after the HOLD, else = qpc_enter */
    __int64          qpc_exit;     /* after intel_init returned (or was replaced) */
    unsigned __int32 nonpassive_skipped;  /* attempts skipped because they were not at PASSIVE_LEVEL, since load */
    unsigned __int32 reserved;
} AVB_INITFAULT_RECORD;                                                                                  /* 96 */

/*
 * Control (PR #462 review round 3): the test never writes or deletes a fault value itself. Arming, disarming and clearing
 * go through this Debug-only IOCTL, which the driver performs under the same lock that admits an attempt (entry) and
 * publishes its record (exit). Admission and disarm/rearm therefore share one driver-owned synchronization boundary: after
 * DISARM returns, no attempt can write the arm or a record until the next ARM, because both writes require the arm to be
 * present under that lock. The registry stays readable by the test (arm, records, residue enumeration). The control handler does no
 * waiting for in-flight attempts; the executable performs that bounded drain by reading inflight before it disarms
 * (the lock itself is taken with KeWaitForSingleObject at PASSIVE_LEVEL, outside any spin lock or hardware access).
 * The IOCTL code is defined in the SSOT header include/avb_ioctl.h (IOCTL_AVB_DBG_INITFAULT_CONTROL, 0x0017A3C0): a Debug
 * (DBG) driver implements it; a Release driver does not and rejects it with STATUS_INVALID_DEVICE_REQUEST.
 */

#define AVB_INITFAULT_OP_ARM               1u   /* delete every fault value, then write this arm with next_seq, inflight, consumed_by 0 */
#define AVB_INITFAULT_OP_DISARM            2u   /* delete the arm and every fault value */
#define AVB_INITFAULT_OP_CLEAR_UNCONSUMED  3u   /* mark the session's unconsumed actions consumed_by = AVB_INITFAULT_CLEARED */
#define AVB_INITFAULT_CLEARED              0xFFFFFFFFu

typedef struct AVB_INITFAULT_CONTROL {
    unsigned __int32 version;            /* AVB_INITFAULT_VERSION */
    unsigned __int32 op;                 /* AVB_INITFAULT_OP_* */
    unsigned __int64 session;            /* CLEAR_UNCONSUMED: the armed session it applies to */
    AVB_INITFAULT_ARM arm;               /* ARM: the arm to write (session non-zero, target, actions, expiry in the future) */
    unsigned __int32 inflight_at_op;     /* out: the armed session's inflight when the operation ran (0 if no arm) */
    unsigned __int32 values_deleted;     /* out: fault values deleted (ARM, DISARM) */
    unsigned __int32 actions_cleared;    /* out: CLEAR_UNCONSUMED */
    unsigned __int32 reserved;
} AVB_INITFAULT_CONTROL;                                                                                 /* 200 */

#endif /* AVB_INIT_FAULT_DBG_H */
