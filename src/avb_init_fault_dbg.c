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
 * mechanism's own lock (an event, PASSIVE_LEVEL) covers the arm read-modify-write only and is released before any HOLD,
 * so a held attempt never blocks another attempt in this code.
 */
#include "precomp.h"
#include <ntstrsafe.h>

/* Layout shared with the test (include/avb_init_fault_dbg.h); checked in every configuration */
C_ASSERT(sizeof(AVB_INITFAULT_ACTION) == 16);
C_ASSERT(sizeof(AVB_INITFAULT_ARM) == 160);
C_ASSERT(sizeof(AVB_INITFAULT_RECORD) == 88);

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

/** The arm, exactly as the test wrote it (size, type and version checked); FALSE when absent or malformed */
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
    return Arm->version == AVB_INITFAULT_VERSION && Arm->count <= AVB_INITFAULT_MAX_ACTIONS;
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

    KeEnterCriticalRegion();
    (void)KeWaitForSingleObject(&g_IfLock, Executive, KernelMode, FALSE, NULL);
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
                rec->origin = Origin;
                rec->site = Site;
                rec->applied = AVB_INITFAULT_ACT_NONE;
                rec->action_index = 0xFFFFFFFFu;
                rec->irql = (unsigned __int32)KeGetCurrentIrql();
                rec->hw_state_before = (unsigned __int32)Ctx->hw_state;
                rec->thread_id = (unsigned __int32)(ULONG_PTR)PsGetCurrentThreadId();
                rec->qpc_enter = KeQueryPerformanceCounter(NULL).QuadPart;
                arm.next_seq++;
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
                /* The consumption and the sequence number must be readable by the test before the action takes effect;
                 * if the write fails the attempt is neither acted on nor recorded */
                if (NT_SUCCESS(ZwSetValueKey(key, &armName, 0, REG_BINARY, &arm, sizeof(arm)))) {
                    Call->active = TRUE;
                } else {
                    holdMs = 0;
                    DEBUGP(DL_ERROR, "!!! [#412] init fault arm update failed: attempt not acted on\n");
                }
            }
        }
        ZwClose(key);
    }
    (void)KeSetEvent(&g_IfLock, 0, FALSE);
    KeLeaveCriticalRegion();

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
    DEBUGP(DL_ERROR, "!!! [#412] init attempt seq=%u luid=0x%016llX origin=%u site=%u applied=%u action=%d hold=%u ms\n",
           rec->seq, rec->net_luid, rec->origin, rec->site, rec->applied, (int)rec->action_index, rec->hold_ms);
    return rec->applied;
}

VOID AvbInitFaultExit(_In_ PAVB_DEVICE_CONTEXT Ctx, _Inout_ PAVB_INITFAULT_CALL Call, _In_ int InitResult)
{
    WCHAR nameBuf[32];
    UNICODE_STRING name;
    HANDLE key = NULL;
    AVB_INITFAULT_RECORD *rec = &Call->rec;

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
    if (!NT_SUCCESS(RtlStringCbPrintfW(nameBuf, sizeof(nameBuf), L"%ls%02u", AVB_INITFAULT_LOG_PREFIX, rec->seq))) {
        return;
    }
    RtlInitUnicodeString(&name, nameBuf);
    if (NT_SUCCESS(IfOpenKey(&key))) {   /* one value per sequence number: no read-modify-write, no lock needed */
        if (!NT_SUCCESS(ZwSetValueKey(key, &name, 0, REG_BINARY, rec, sizeof(*rec)))) {
            DEBUGP(DL_ERROR, "!!! [#412] init attempt seq=%u: record write failed\n", rec->seq);
        }
        ZwClose(key);
    }
    DEBUGP(DL_ERROR, "!!! [#412] init attempt seq=%u result=%d hw_state %u -> %u\n", rec->seq, InitResult,
           rec->hw_state_before, rec->hw_state_after);
}

#endif /* DBG */
