/*
 * avb_init_fault_dbg.c - Debug-only adapter-initialization fault mechanism (#412).
 *
 * Implements: #412 (bounded, target-specific, test-owned initialization fault for UT-DEV-LIFECYCLE-002)
 * Layout and semantics: include/avb_init_fault_dbg.h. Release builds compile nothing here (the inline stubs in
 * src/avb_integration.h replace the two entry points), so a Release driver has no arm, no registry access and no wait.
 *
 * Where it runs (source review, branch test/412-init-retry-fault-injection from e0d7c03): every caller of the bring-up
 * releases FilterListLock before calling it, and the bring-up path holds no driver spin lock. The IOCTL path holds only the
 * per-context IO remove lock (not a spin lock). The registry and the HOLD are therefore used only when this code itself sees
 * PASSIVE_LEVEL; at any other IRQL the attempt is neither armed nor recorded, only counted (nonpassive_skipped). The
 * mechanism's own lock (an event, PASSIVE_LEVEL) covers the arm read-modify-write at entry and the session check, record
 * write and arm update at exit; it is released before any HOLD, so a held attempt never blocks another attempt here.
 *
 * Session isolation (PR #462 review): the record of an attempt is published only while the arm it entered under is still
 * present with the same session, checked and written under the lock at exit. A disarmed or re-armed session therefore
 * never receives an old attempt's record, and record names carry the session.
 */
#include "precomp.h"
#include <ntstrsafe.h>

/* Layout shared with the test (include/avb_init_fault_dbg.h); checked in every configuration */
C_ASSERT(sizeof(AVB_INITFAULT_ACTION) == 16);
C_ASSERT(sizeof(AVB_INITFAULT_ARM) == 168);
C_ASSERT(sizeof(AVB_INITFAULT_RECORD) == 96);
C_ASSERT(sizeof(AVB_INITFAULT_CONTROL) == 200);

#if DBG

static WCHAR          g_IfPathBuf[512];
static UNICODE_STRING g_IfPath;
static BOOLEAN        g_IfReady;
static KEVENT         g_IfLock;          /* SynchronizationEvent, signalled = free */
static volatile LONG  g_IfNonPassive;

VOID AvbInitFaultSetRegistryPath(_In_ PUNICODE_STRING RegistryPath)
{
    static const WCHAR suffix[] = L"\\Parameters";
    const USHORT suffixBytes = (USHORT)(sizeof(suffix) - sizeof(WCHAR));

    KeInitializeEvent(&g_IfLock, SynchronizationEvent, TRUE);
    if (RegistryPath == NULL || RegistryPath->Buffer == NULL ||
        (ULONG)RegistryPath->Length + suffixBytes + sizeof(WCHAR) > sizeof(g_IfPathBuf)) {
        DEBUGP(DL_ERROR, "!!! [#412] init fault mechanism inert: registry path unusable\n");
        return;
    }
    RtlCopyMemory(g_IfPathBuf, RegistryPath->Buffer, RegistryPath->Length);
    RtlCopyMemory((PUCHAR)g_IfPathBuf + RegistryPath->Length, suffix, sizeof(suffix));
    g_IfPath.Buffer = g_IfPathBuf;
    g_IfPath.Length = (USHORT)(RegistryPath->Length + suffixBytes);
    g_IfPath.MaximumLength = (USHORT)sizeof(g_IfPathBuf);
    g_IfReady = TRUE;
}

static NTSTATUS IfOpenKey(_Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &g_IfPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    return ZwOpenKey(Key, KEY_QUERY_VALUE | KEY_SET_VALUE, &oa);
}

/** The arm, exactly as the test wrote it (size, type, version and a non-zero session checked); FALSE when absent or malformed */
static BOOLEAN IfReadArm(_In_ HANDLE Key, _Out_ AVB_INITFAULT_ARM *Arm)
{
    ULONG buf[(sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(AVB_INITFAULT_ARM) + sizeof(ULONG) - 1) / sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION p = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
    UNICODE_STRING name = RTL_CONSTANT_STRING(AVB_INITFAULT_ARM_VALUE);
    ULONG len = 0;

    RtlZeroMemory(Arm, sizeof(*Arm));
    if (!NT_SUCCESS(ZwQueryValueKey(Key, &name, KeyValuePartialInformation, p, sizeof(buf), &len)) || p->Type != REG_BINARY ||
        p->DataLength != sizeof(*Arm)) {
        return FALSE;
    }
    RtlCopyMemory(Arm, p->Data, sizeof(*Arm));
    return Arm->version == AVB_INITFAULT_VERSION && Arm->count <= AVB_INITFAULT_MAX_ACTIONS && Arm->session != 0;
}

static NTSTATUS IfWriteArm(_In_ HANDLE Key, _In_ AVB_INITFAULT_ARM *Arm)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(AVB_INITFAULT_ARM_VALUE);
    return ZwSetValueKey(Key, &name, 0, REG_BINARY, Arm, sizeof(*Arm));
}

static VOID IfLock(VOID)
{
    KeEnterCriticalRegion();
    (void)KeWaitForSingleObject(&g_IfLock, Executive, KernelMode, FALSE, NULL);
}

static VOID IfUnlock(VOID)
{
    (void)KeSetEvent(&g_IfLock, 0, FALSE);
    KeLeaveCriticalRegion();
}

static BOOLEAN IfMayHold(ULONG Origin)
{
    return Origin == AVB_INITFAULT_ORIGIN_INIT_PRE || Origin == AVB_INITFAULT_ORIGIN_INIT_HANDLER;
}

ULONG AvbInitFaultEnter(_In_ PAVB_DEVICE_CONTEXT Ctx, _In_ ULONG Origin, _In_ ULONG Site, _Out_ PAVB_INITFAULT_CALL Call)
{
    UNICODE_STRING armName = RTL_CONSTANT_STRING(AVB_INITFAULT_ARM_VALUE);
    AVB_INITFAULT_ARM arm;
    LARGE_INTEGER now;
    HANDLE key = NULL;
    ULONG holdMs = 0, i;
    AVB_INITFAULT_RECORD *rec = &Call->rec;

    RtlZeroMemory(Call, sizeof(*Call));
    if (!g_IfReady || Ctx == NULL || Ctx->filter_instance == NULL) {
        return AVB_INITFAULT_ACT_NONE;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        InterlockedIncrement(&g_IfNonPassive);
        return AVB_INITFAULT_ACT_NONE;
    }

    IfLock();
    if (NT_SUCCESS(IfOpenKey(&key))) {
        if (IfReadArm(key, &arm)) {
            KeQuerySystemTime(&now);
            if ((unsigned __int64)now.QuadPart > arm.expiry_100ns) {
                (void)ZwDeleteValueKey(key, &armName);   /* bounded in time: an expired arm is removed and never acts */
                DEBUGP(DL_ERROR, "!!! [#412] init fault arm expired: deleted, no action\n");
            } else if (arm.net_luid == Ctx->filter_instance->MiniportNetLuid.Value && arm.next_seq < AVB_INITFAULT_MAX_RECORDS) {
                rec->version = AVB_INITFAULT_VERSION;
                rec->seq = arm.next_seq;
                rec->net_luid = arm.net_luid;
                rec->session = arm.session;
                rec->origin = Origin;
                rec->site = Site;
                rec->applied = AVB_INITFAULT_ACT_NONE;
                rec->action_index = AVB_INITFAULT_ACTION_INDEX_NONE;
                rec->irql = (unsigned __int32)KeGetCurrentIrql();
                rec->hw_state_before = (unsigned __int32)Ctx->hw_state;
                rec->thread_id = (unsigned __int32)(ULONG_PTR)PsGetCurrentThreadId();
                rec->qpc_enter = KeQueryPerformanceCounter(NULL).QuadPart;
                arm.next_seq++;
                arm.inflight++;
                if (Site == AVB_INITFAULT_SITE_BASIC) {   /* the fault point is the only consuming site */
                    for (i = 0; i < arm.count; i++) {
                        AVB_INITFAULT_ACTION *a = &arm.actions[i];
                        if (a->consumed_by != 0 || a->origin != Origin ||
                            (a->action != AVB_INITFAULT_ACT_OBSERVE && a->action != AVB_INITFAULT_ACT_FAIL && a->action != AVB_INITFAULT_ACT_HOLD)) {
                            continue;
                        }
                        a->consumed_by = rec->seq + 1u;
                        rec->action_index = i;
                        rec->applied = a->action;
                        if (a->action == AVB_INITFAULT_ACT_HOLD) {
                            if (IfMayHold(Origin)) {
                                holdMs = (a->hold_ms > AVB_INITFAULT_MAX_HOLD_MS) ? AVB_INITFAULT_MAX_HOLD_MS : a->hold_ms;
                            } else {
                                rec->applied = AVB_INITFAULT_ACT_HOLD_REFUSED;
                            }
                        }
                        break;
                    }
                }
                /* The consumption, the sequence number and inflight must be readable by the test before the action takes
                 * effect; if the write fails the attempt is neither acted on nor recorded */
                if (NT_SUCCESS(IfWriteArm(key, &arm))) {
                    Call->active = TRUE;
                } else {
                    holdMs = 0;
                    DEBUGP(DL_ERROR, "!!! [#412] init fault arm update failed: attempt not acted on\n");
                }
            }
        }
        ZwClose(key);
    }
    IfUnlock();

    if (!Call->active) {
        return AVB_INITFAULT_ACT_NONE;
    }
    if (holdMs != 0) {   /* PASSIVE_LEVEL (checked above), no driver lock held, before any device access of this attempt */
        LARGE_INTEGER delay;
        delay.QuadPart = -(LONGLONG)holdMs * 10000;
        (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
        rec->hold_ms = holdMs;
    }
    rec->qpc_hold_end = KeQueryPerformanceCounter(NULL).QuadPart;
    rec->nonpassive_skipped = (unsigned __int32)g_IfNonPassive;
    DEBUGP(DL_ERROR, "!!! [#412] init attempt session=0x%016llX seq=%u luid=0x%016llX origin=%u site=%u applied=%u action=%d hold=%u ms\n",
           rec->session, rec->seq, rec->net_luid, rec->origin, rec->site, rec->applied, (int)rec->action_index, rec->hold_ms);
    return rec->applied;
}

/** Deletes the arm and every value named AVB_INITFAULT_NAME_PREFIX* (caller holds the lock). Bounded: at most a fixed number of
 *  enumeration steps; *Inflight receives the deleted arm's inflight */
static NTSTATUS IfDeleteAll(_In_ HANDLE Key, _Out_ ULONG *Deleted, _Out_ ULONG *Inflight)
{
    ULONG buf[(sizeof(KEY_VALUE_BASIC_INFORMATION) + 128 * sizeof(WCHAR)) / sizeof(ULONG) + 1];
    PKEY_VALUE_BASIC_INFORMATION v = (PKEY_VALUE_BASIC_INFORMATION)buf;
    const ULONG plen = (ULONG)(sizeof(AVB_INITFAULT_NAME_PREFIX) - sizeof(WCHAR));
    AVB_INITFAULT_ARM arm;
    ULONG idx = 0, steps, len;
    NTSTATUS st = STATUS_SUCCESS;

    *Deleted = 0;
    *Inflight = IfReadArm(Key, &arm) ? arm.inflight : 0;
    for (steps = 0; steps < 4096; steps++) {
        UNICODE_STRING name;
        st = ZwEnumerateValueKey(Key, idx, KeyValueBasicInformation, v, sizeof(buf), &len);
        if (st == STATUS_NO_MORE_ENTRIES) {
            return STATUS_SUCCESS;
        }
        if (st == STATUS_BUFFER_OVERFLOW || st == STATUS_BUFFER_TOO_SMALL) {   /* a longer name is not one of ours */
            idx++;
            continue;
        }
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (v->NameLength >= plen && RtlCompareMemory(v->Name, AVB_INITFAULT_NAME_PREFIX, plen) == plen) {
            name.Buffer = v->Name;
            name.Length = name.MaximumLength = (USHORT)v->NameLength;
            st = ZwDeleteValueKey(Key, &name);
            if (!NT_SUCCESS(st)) {
                return st;
            }
            (*Deleted)++;   /* the next value moved to idx: do not advance */
        } else {
            idx++;
        }
    }
    return STATUS_UNSUCCESSFUL;   /* the bound was reached */
}

NTSTATUS AvbInitFaultControl(_Inout_ PVOID Buffer, _In_ ULONG InLen, _In_ ULONG OutLen, _Out_ ULONG *Info)
{
    AVB_INITFAULT_CONTROL *c = (AVB_INITFAULT_CONTROL *)Buffer;
    OBJECT_ATTRIBUTES oa;
    HANDLE key = NULL;
    LARGE_INTEGER now;
    ULONG deleted = 0, inflight = 0, i, disp = 0;
    NTSTATUS st;

    *Info = 0;
    if (!g_IfReady) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Buffer == NULL || InLen < sizeof(*c) || OutLen < sizeof(*c) || c->version != AVB_INITFAULT_VERSION) {
        return STATUS_INVALID_PARAMETER;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (c->op == AVB_INITFAULT_OP_ARM) {
        KeQuerySystemTime(&now);
        if (c->arm.version != AVB_INITFAULT_VERSION || c->arm.count > AVB_INITFAULT_MAX_ACTIONS || c->arm.session == 0 ||
            c->arm.net_luid == 0 || c->arm.expiry_100ns <= (unsigned __int64)now.QuadPart) {
            return STATUS_INVALID_PARAMETER;
        }
    } else if (c->op != AVB_INITFAULT_OP_DISARM && c->op != AVB_INITFAULT_OP_CLEAR_UNCONSUMED) {
        return STATUS_INVALID_PARAMETER;
    }
    c->inflight_at_op = c->values_deleted = c->actions_cleared = 0;

    IfLock();   /* the admission lock: no attempt enters or publishes while this runs */
    InitializeObjectAttributes(&oa, &g_IfPath, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    st = ZwCreateKey(&key, KEY_QUERY_VALUE | KEY_SET_VALUE, &oa, 0, NULL, REG_OPTION_NON_VOLATILE, &disp);
    if (NT_SUCCESS(st)) {
        if (c->op == AVB_INITFAULT_OP_CLEAR_UNCONSUMED) {
            AVB_INITFAULT_ARM arm;
            if (!IfReadArm(key, &arm) || arm.session != c->session) {
                st = STATUS_NOT_FOUND;
            } else {
                for (i = 0; i < arm.count; i++) {
                    if (arm.actions[i].consumed_by == 0) {
                        arm.actions[i].consumed_by = AVB_INITFAULT_CLEARED;
                        c->actions_cleared++;
                    }
                }
                c->inflight_at_op = arm.inflight;
                st = IfWriteArm(key, &arm);
            }
        } else {
            st = IfDeleteAll(key, &deleted, &inflight);
            c->values_deleted = deleted;
            c->inflight_at_op = inflight;
            if (NT_SUCCESS(st) && c->op == AVB_INITFAULT_OP_ARM) {
                c->arm.next_seq = 0;
                c->arm.inflight = 0;
                for (i = 0; i < AVB_INITFAULT_MAX_ACTIONS; i++) {
                    c->arm.actions[i].consumed_by = 0;
                }
                st = IfWriteArm(key, &c->arm);
            }
        }
        ZwClose(key);
    }
    IfUnlock();

    DEBUGP(DL_ERROR, "!!! [#412] init fault control op=%u session=0x%016llX status=0x%08X inflight=%u deleted=%u cleared=%u\n",
           c->op, c->op == AVB_INITFAULT_OP_ARM ? c->arm.session : c->session, st, c->inflight_at_op, c->values_deleted,
           c->actions_cleared);
    if (NT_SUCCESS(st)) {
        *Info = sizeof(*c);
    }
    return st;
}

VOID AvbInitFaultExit(_In_ PAVB_DEVICE_CONTEXT Ctx, _Inout_ PAVB_INITFAULT_CALL Call, _In_ int InitResult)
{
    WCHAR nameBuf[48];
    UNICODE_STRING name;
    HANDLE key = NULL;
    AVB_INITFAULT_ARM arm;
    AVB_INITFAULT_RECORD *rec = &Call->rec;
    BOOLEAN published = FALSE;

    if (!Call->active) {
        return;
    }
    Call->active = FALSE;
    rec->init_result = InitResult;
    rec->hw_state_after = (unsigned __int32)Ctx->hw_state;
    rec->qpc_exit = KeQueryPerformanceCounter(NULL).QuadPart;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        InterlockedIncrement(&g_IfNonPassive);
        return;
    }
    if (!NT_SUCCESS(RtlStringCbPrintfW(nameBuf, sizeof(nameBuf), L"%ls%016llX_%02u", AVB_INITFAULT_LOG_PREFIX, rec->session, rec->seq))) {
        return;
    }
    RtlInitUnicodeString(&name, nameBuf);

    IfLock();
    if (NT_SUCCESS(IfOpenKey(&key))) {
        /* Publish only into the session this attempt entered under, while it is still armed (checked under the lock) */
        if (IfReadArm(key, &arm) && arm.session == rec->session && arm.net_luid == rec->net_luid) {
            if (NT_SUCCESS(ZwSetValueKey(key, &name, 0, REG_BINARY, rec, sizeof(*rec)))) {
                published = TRUE;
            } else {
                DEBUGP(DL_ERROR, "!!! [#412] init attempt seq=%u: record write failed\n", rec->seq);
            }
            if (arm.inflight != 0) {
                arm.inflight--;
            }
            if (!NT_SUCCESS(IfWriteArm(key, &arm))) {
                DEBUGP(DL_ERROR, "!!! [#412] init attempt seq=%u: inflight update failed\n", rec->seq);
            }
        }
        ZwClose(key);
    }
    IfUnlock();

    DEBUGP(DL_ERROR, "!!! [#412] init attempt session=0x%016llX seq=%u result=%d hw_state %u -> %u %s\n", rec->session, rec->seq,
           InitResult, rec->hw_state_before, rec->hw_state_after, published ? "recorded" : "NOT recorded (session disarmed or changed)");
}

#endif /* DBG */
