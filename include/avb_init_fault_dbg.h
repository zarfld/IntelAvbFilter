/*
 * avb_init_fault_dbg.h - Debug-only adapter-initialization fault mechanism (#412), shared by the driver and its test.
 *
 * Implements: #412 (TEST-INTEGRITY: UT-DEV-LIFECYCLE-002 initialization after failed start)
 * Verifies:   #12 via #313 UT-DEV-LIFECYCLE-002 (tests/ioctl/test_ioctl_dev_lifecycle.c --init-retry-fault)
 *
 * WHAT: a testcase arms a finite list of actions for ONE adapter (by NET_LUID). Each adapter-initialization attempt of
 * that adapter that reaches the fault point (AvbPerformBasicInitialization, immediately before its intel_init) consumes
 * the first unconsumed action whose origin matches the attempt's origin. Every attempt on the armed adapter, at the
 * fault point and at the other intel_init sites, is written back as one record the test reads. Debug builds only: in a
 * Release build the driver contains none of this (src/avb_init_fault_dbg.c is empty outside DBG).
 *
 * Transport: REG_BINARY values under the driver service key's Parameters subkey
 * (HKLM\SYSTEM\CurrentControlSet\Services\IntelAvbFilter\Parameters), next to DebugLevel (#95):
 *   AVB_INITFAULT_ARM_VALUE        written by the test, updated by the driver (consumption, sequence numbers)
 *   AVB_INITFAULT_LOG_PREFIX + NN  one record per attempt, NN = record sequence 00..AVB_INITFAULT_MAX_RECORDS-1
 * The test owns setup and cleanup: it deletes every value it or the driver wrote. Bounds: at most AVB_INITFAULT_MAX_ACTIONS
 * actions, at most AVB_INITFAULT_MAX_RECORDS records, an absolute expiry after which the driver deletes the arm and does
 * nothing, and HOLD at most AVB_INITFAULT_MAX_HOLD_MS. Automatic disarm: a consumed action never fires again, so once every
 * action is consumed the arm only records.
 *
 * Waiting (HOLD) runs only for the INIT_DEVICE origins and only at PASSIVE_LEVEL, with no driver lock held; any other
 * origin's HOLD is consumed as AVB_INITFAULT_ACT_HOLD_REFUSED without waiting. Registry access needs PASSIVE_LEVEL: an
 * attempt reached at another IRQL is not armed or recorded, but counted (record field nonpassive_skipped).
 */
#ifndef AVB_INIT_FAULT_DBG_H
#define AVB_INIT_FAULT_DBG_H

#define AVB_INITFAULT_VERSION        1u
#define AVB_INITFAULT_MAX_ACTIONS    8u
#define AVB_INITFAULT_MAX_RECORDS    64u
#define AVB_INITFAULT_MAX_HOLD_MS    10000u

#define AVB_INITFAULT_ARM_VALUE      L"AvbInitFaultArm"
#define AVB_INITFAULT_LOG_PREFIX     L"AvbInitFaultLog"
#define AVB_INITFAULT_PARAMS_SUBKEY  L"SYSTEM\\CurrentControlSet\\Services\\IntelAvbFilter\\Parameters"   /* test side */

/* Who started the initialization attempt (the caller of the bring-up) */
#define AVB_INITFAULT_ORIGIN_ATTACH        1u   /* FilterAttach -> AvbInitializeDevice (src/filter.c) */
#define AVB_INITFAULT_ORIGIN_RESTART       2u   /* FilterRestart -> AvbBringUpHardware (src/filter.c) */
#define AVB_INITFAULT_ORIGIN_CREATE_IOCTL  3u   /* lazy context creation for a routed IOCTL (src/device.c) */
#define AVB_INITFAULT_ORIGIN_CREATE_ENUM   4u   /* lazy context creation in ENUM_ADAPTERS (src/device.c) */
#define AVB_INITFAULT_ORIGIN_CREATE_OPEN   5u   /* lazy context creation in OPEN_ADAPTER (src/device.c) */
#define AVB_INITFAULT_ORIGIN_INIT_PRE      6u   /* INIT_DEVICE pre-dispatch bring-up, before the per-instance check */
#define AVB_INITFAULT_ORIGIN_INIT_HANDLER  7u   /* INIT_DEVICE handler bring-up (and its forced BAR0 path) */
#define AVB_INITFAULT_ORIGIN_OPEN_HANDLER  8u   /* OPEN_ADAPTER handler bring-up / AvbEnsureDeviceReady */

/* Which intel_init call site the attempt is at. Only SITE_BASIC (the fault point) consumes actions */
#define AVB_INITFAULT_SITE_BASIC     1u   /* AvbPerformBasicInitialization, before the first intel_init: the fault point */
#define AVB_INITFAULT_SITE_BRINGUP   2u   /* AvbBringUpHardware, intel_init after a successful basic initialization */
#define AVB_INITFAULT_SITE_ENSURE    3u   /* AvbEnsureDeviceReady, intel_init (OPEN_ADAPTER) */
#define AVB_INITFAULT_SITE_FORCED    4u   /* INIT_DEVICE forced BAR0 path, intel_init when no hardware context existed */

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
    unsigned __int32 next_seq;     /* next record sequence (driver-owned; the test writes 0) */
    unsigned __int32 reserved;
    AVB_INITFAULT_ACTION actions[AVB_INITFAULT_MAX_ACTIONS];
} AVB_INITFAULT_ARM;                                                                                     /* 160 */

typedef struct AVB_INITFAULT_RECORD {
    unsigned __int32 version;      /* AVB_INITFAULT_VERSION */
    unsigned __int32 seq;
    unsigned __int64 net_luid;     /* the adapter of the attempt (always the armed target) */
    unsigned __int32 origin;       /* AVB_INITFAULT_ORIGIN_* */
    unsigned __int32 site;         /* AVB_INITFAULT_SITE_* */
    unsigned __int32 applied;      /* AVB_INITFAULT_ACT_* actually applied */
    unsigned __int32 action_index; /* index of the consumed action, 0xFFFFFFFF if none */
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
} AVB_INITFAULT_RECORD;                                                                                  /* 88 */

#endif /* AVB_INIT_FAULT_DBG_H */
