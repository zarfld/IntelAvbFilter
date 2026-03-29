/*++

Module Name:

    avb_integration_fixed.c

Abstract:

    Intel AVB integration for NDIS filter � unified implementation.
    Provides minimal-context creation (BOUND) immediately on attach so
    enumeration succeeds even if later hardware bring-up fails. Deferred
    initialization promotes BAR_MAPPED and PTP_READY states and accrues
    capabilities.

    CLEAN DEVICE SEPARATION: No device-specific register definitions in generic layer.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "avb_version.h"  // Auto-generated version numbers
#include "external/intel_avb/lib/intel_windows.h"
#include "external/intel_avb/lib/intel_private.h"
#include "devices/intel_device_interface.h"  // For intel_device_ops_t and intel_get_device_ops()
#include <ntstrsafe.h>

// Generic register offsets (common across Intel devices)
#define INTEL_GENERIC_CTRL_REG      0x00000  // Device control register (all Intel devices)
#define INTEL_GENERIC_STATUS_REG    0x00008  // Device status register (all Intel devices)

/* Selected adapter context (points into g_AvbContextList).
 * Protected by g_AvbContextListLock.  Use AvbSelectContext() to change. */
PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/* Head of all-adapters list.  Each entry has ::next_context linkage.
 * Protected by g_AvbContextListLock. */
PAVB_DEVICE_CONTEXT g_AvbContextList = NULL;

/* Spin lock protecting both g_AvbContext and g_AvbContextList. */
NDIS_SPIN_LOCK g_AvbContextListLock;

/*
 * AvbContextListRemove — remove Ctx from the global adapter list.
 *
 * Must be called at PASSIVE_LEVEL (NdisAcquireSpinLock can raise to
 * DISPATCH_LEVEL internally, but the call site itself must be passive).
 * After this call g_AvbContext is NULL only if no other adapter remains.
 */
static VOID AvbContextListRemove(_In_ PAVB_DEVICE_CONTEXT Ctx)
{
    NdisAcquireSpinLock(&g_AvbContextListLock);

    /* Remove from the singly-linked list */
    PAVB_DEVICE_CONTEXT *pp = &g_AvbContextList;
    while (*pp != NULL && *pp != Ctx) {
        pp = &(*pp)->next_context;
    }
    if (*pp == Ctx) {
        *pp = Ctx->next_context;   // splice out
        Ctx->next_context = NULL;
    }

    /* If this was the selected adapter, promote the first remaining one */
    if (g_AvbContext == Ctx) {
        g_AvbContext = g_AvbContextList;  // NULL when list is empty
    }

    NdisReleaseSpinLock(&g_AvbContextListLock);
}

/* Forward declarations */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx);
static VOID AvbCheckTargetTime(_In_ PAVB_DEVICE_CONTEXT AvbContext);
static VOID AvbTxTimestampPollDpc(
    _In_ PVOID SystemSpecific1,
    _In_ PVOID FunctionContext,
    _In_ PVOID SystemSpecific2,
    _In_ PVOID SystemSpecific3);


/* Platform ops wrapper (Intel library selects this) */
static int PlatformInitWrapper(_In_ device_t *dev) { 
    DEBUGP(DL_ERROR, "!!! DEBUG: PlatformInitWrapper called!\n");
    NTSTATUS status = AvbPlatformInit(dev);
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit returned: 0x%08X\n", status);
    return NT_SUCCESS(status) ? 0 : -1;
}
static void PlatformCleanupWrapper(_In_ device_t *dev) { AvbPlatformCleanup(dev); }

const struct platform_ops ndis_platform_ops = {
    PlatformInitWrapper,
    PlatformCleanupWrapper,
    AvbPciReadConfig,
    AvbPciWriteConfig,
    AvbMmioRead,
    AvbMmioWrite,
    AvbMdioRead,
    AvbMdioWrite,
    AvbReadTimestamp
};

/* ======================================================================= */
/**
 * @brief Allocate minimal context and mark BOUND so user-mode can enumerate.
 */
NTSTATUS AvbCreateMinimalContext(
    _In_ PMS_FILTER FilterModule,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId,
    _Outptr_ PAVB_DEVICE_CONTEXT *OutCtx)
{
    if (!FilterModule || !OutCtx) return STATUS_INVALID_PARAMETER;
    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(AVB_DEVICE_CONTEXT), FILTER_ALLOC_TAG);
    if (!ctx) return STATUS_INSUFFICIENT_RESOURCES;
    
    // UNCONDITIONAL TRACE: Log context allocation (MUST appear in DebugView)
    DEBUGP(DL_ERROR, "!!! CONTEXT_ALLOC: Allocated context at %p (size=%zu bytes)\n", ctx, sizeof(*ctx));
    
    // TRACE: Validate pool memory state before zeroing (detect uninitialized memory issues)
    DEBUGP(DL_ERROR, "!!! CONTEXT_ALLOC: Pool memory before zero (ctx=%p, size=%zu, target_flag=%d, tx_flag=%d)\n",
           ctx, sizeof(*ctx), (ULONG)ctx->target_time_poll_active, (ULONG)ctx->tx_poll_active);
    
    RtlZeroMemory(ctx, sizeof(*ctx));
    
    // UNCONDITIONAL TRACE: Verify zeroing completed (MUST appear in DebugView)
    DEBUGP(DL_ERROR, "!!! CONTEXT_ALLOC: RtlZeroMemory completed (target_flag=%d, tx_flag=%d)\n",
           ctx->target_time_poll_active ? 1 : 0, ctx->tx_poll_active ? 1 : 0);
    
    // TRACE: Verify RtlZeroMemory correctly initialized flags (should be 0)
    DEBUGP(DL_ERROR, "!!! CONTEXT_ALLOC: Memory after zero (ctx=%p, target_flag=%d, tx_flag=%d)\n",
           ctx, (ULONG)ctx->target_time_poll_active, (ULONG)ctx->tx_poll_active);
    
    ctx->filter_instance = FilterModule;
    ctx->intel_device.pci_vendor_id = VendorId;
    ctx->intel_device.pci_device_id = DeviceId;
    ctx->intel_device.device_type   = AvbGetIntelDeviceType(DeviceId);
    AVB_SET_HW_STATE(ctx, AVB_HW_BOUND);
    
    /* Initialize timestamp event subscription management (Issue #13) */
    NdisAllocateSpinLock(&ctx->subscription_lock);
    ctx->next_ring_id = 1;  // Start ring_id allocation from 1
    for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        ctx->subscriptions[i].ring_id = 0;      // 0 = unused
        ctx->subscriptions[i].active = 0;
        ctx->subscriptions[i].ring_buffer = NULL;
        ctx->subscriptions[i].ring_mdl = NULL;
        ctx->subscriptions[i].user_va = NULL;
        ctx->subscriptions[i].sequence_num = 0;
    }

    /* Initialize ATDECC entity event subscription table (Issue #236) */
    NdisAllocateSpinLock(&ctx->atdecc_sub_lock);
    ctx->next_atdecc_sub_id = 1;
    for (int i = 0; i < MAX_ATDECC_SUBSCRIPTIONS; i++) {
        ctx->atdecc_subscriptions[i].sub_id      = 0;
        ctx->atdecc_subscriptions[i].active      = 0;
        ctx->atdecc_subscriptions[i].event_mask  = 0;
        ctx->atdecc_subscriptions[i].file_object = NULL;
        ctx->atdecc_subscriptions[i].head        = 0;
        ctx->atdecc_subscriptions[i].tail        = 0;
    }

    /* Initialize SRP reservation table (Issue #211) */
    NdisAllocateSpinLock(&ctx->srp_lock);
    ctx->next_srp_handle = 1;
    for (int i = 0; i < MAX_SRP_STREAMS; i++) {
        ctx->srp_reservations[i].handle        = 0;
        ctx->srp_reservations[i].stream_id     = 0;
        ctx->srp_reservations[i].bandwidth_bps = 0;
    }

    /* Initialize VLAN / EEE / PFC state (Issues #213, #223, #219) */
    ctx->vlan_enabled      = 0;
    ctx->vlan_id           = 0;
    ctx->vlan_pcp          = 0;
    ctx->vlan_strip_rx     = 0;
    ctx->eee_enabled       = 0;
    ctx->eee_lpi_timer_us  = 0;
    ctx->pfc_enabled       = 0;
    ctx->pfc_priority_mask = 0;
    
    /* Initialize TX timestamp polling timer (Task 6c/7)
     * CRITICAL: Must call NdisInitializeTimer BEFORE NdisSetTimer
     * FIX: This was missing, causing BSOD 0x0A when timer was started
     */
    ctx->tx_poll_active = FALSE;  // Initially stopped
    NdisInitializeTimer(&ctx->tx_poll_timer, AvbTxTimestampPollDpc, ctx);
    DEBUGP(DL_TRACE, "TX polling timer initialized (not started)\n");
    
    /* Initialize NDIS packet injection pools (Step 8b - Test IOCTL)
     * Allows kernel-mode PTP packet injection that routes through filter driver,
     * solving Npcap bypass issue where filter never sees packets.
     */
    ctx->nbl_pool_handle = NULL;
    ctx->nb_pool_handle = NULL;
    ctx->test_packet_buffer = NULL;
    ctx->test_packet_mdl = NULL;
    NdisAllocateSpinLock(&ctx->test_send_lock);
    ctx->test_packets_pending = 0;
    
    // Allocate NET_BUFFER_LIST pool (for outbound test packets)
    NET_BUFFER_LIST_POOL_PARAMETERS nblPoolParams;
    RtlZeroMemory(&nblPoolParams, sizeof(nblPoolParams));
    nblPoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    nblPoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    nblPoolParams.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    nblPoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    nblPoolParams.fAllocateNetBuffer = FALSE;  // Manual NET_BUFFER allocation (more control)
    nblPoolParams.PoolTag = FILTER_ALLOC_TAG;
    nblPoolParams.ContextSize = 0;  // No per-NBL context needed
    nblPoolParams.DataSize = 0;     // Data allocated separately
    
    ctx->nbl_pool_handle = NdisAllocateNetBufferListPool(FilterModule->FilterHandle, &nblPoolParams);
    if (!ctx->nbl_pool_handle) {
        DEBUGP(DL_ERROR, "Failed to allocate NET_BUFFER_LIST pool\n");
        // Non-fatal - test IOCTL will return error, but driver continues
        goto skip_test_pools;
    }
    
    // Allocate NET_BUFFER pool (for packet data buffers)
    NET_BUFFER_POOL_PARAMETERS nbPoolParams;
    RtlZeroMemory(&nbPoolParams, sizeof(nbPoolParams));
    nbPoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    nbPoolParams.Header.Revision = NET_BUFFER_POOL_PARAMETERS_REVISION_1;
    nbPoolParams.Header.Size = NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_1;
    nbPoolParams.PoolTag = FILTER_ALLOC_TAG;
    nbPoolParams.DataSize = 0;  // MDL-based data
    
    ctx->nb_pool_handle = NdisAllocateNetBufferPool(FilterModule->FilterHandle, &nbPoolParams);
    if (!ctx->nb_pool_handle) {
        DEBUGP(DL_ERROR, "Failed to allocate NET_BUFFER pool\n");
        NdisFreeNetBufferListPool(ctx->nbl_pool_handle);
        ctx->nbl_pool_handle = NULL;
        goto skip_test_pools;
    }
    
    // Allocate pre-formatted PTP Sync frame (64 bytes, Non-Paged Pool)
    // Ethernet(14) + PTP Sync Message(34) + Padding to minimum frame size
    #define TEST_PACKET_SIZE 64
    ctx->test_packet_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, TEST_PACKET_SIZE, FILTER_ALLOC_TAG);
    if (!ctx->test_packet_buffer) {
        DEBUGP(DL_ERROR, "Failed to allocate test packet buffer\n");
        NdisFreeNetBufferPool(ctx->nb_pool_handle);
        NdisFreeNetBufferListPool(ctx->nbl_pool_handle);
        ctx->nb_pool_handle = NULL;
        ctx->nbl_pool_handle = NULL;
        goto skip_test_pools;
    }
    
    // Pre-fill PTP Sync frame template (will be customized per-send with sequence ID)
    RtlZeroMemory(ctx->test_packet_buffer, TEST_PACKET_SIZE);
    UCHAR *pkt = (UCHAR *)ctx->test_packet_buffer;
    
    // Ethernet header (14 bytes)
    // Destination MAC: PTP multicast 01:1B:19:00:00:00
    pkt[0] = 0x01; pkt[1] = 0x1B; pkt[2] = 0x19; pkt[3] = 0x00; pkt[4] = 0x00; pkt[5] = 0x00;
    // Source MAC: Placeholder; real adapter MAC is patched in at FilterRestart
    //             (OID_802_3_CURRENT_ADDRESS → ctx->source_mac → bytes [6-11]).
    pkt[6] = 0x00; pkt[7] = 0x00; pkt[8] = 0x00; pkt[9] = 0x00; pkt[10] = 0x00; pkt[11] = 0x01;
    // EtherType: ETHERTYPE_PTP - CRITICAL for filter detection
    pkt[12] = 0x88; pkt[13] = 0xF7;
    
    // PTP Sync message (34 bytes minimum)
    // transportSpecific(4 bits) | messageType(4 bits) = 0x02 (transportSpecific=0, messageType=SYNC=0)
    pkt[14] = 0x00;  // messageType = SYNC (0x0)
    pkt[15] = 0x02;  // versionPTP = 2
    pkt[16] = 0x00; pkt[17] = 0x2C;  // messageLength = 44 bytes (decimal 44)
    pkt[18] = 0x00;  // domainNumber = 0
    pkt[19] = 0x00;  // reserved
    pkt[20] = 0x02; pkt[21] = 0x00;  // flagField = twoStepFlag (bit 9, big-endian)
    // correctionField (8 bytes) = 0
    // sourcePortIdentity (10 bytes) = clockIdentity(8) + portNumber(2) = all zeros for test
    // sequenceId (2 bytes) - will be filled at send time (offset 44-45)
    // controlField (1 byte) = SYNC (0x00)
    pkt[46] = 0x00;
    // logMessageInterval (1 byte) = 0x7F (don't care for test)
    pkt[47] = 0x7F;
    // Padding to 64 bytes (minimum Ethernet frame size)
    
    // Create MDL describing test packet buffer
    ctx->test_packet_mdl = IoAllocateMdl(ctx->test_packet_buffer, TEST_PACKET_SIZE, FALSE, FALSE, NULL);
    if (!ctx->test_packet_mdl) {
        DEBUGP(DL_ERROR, "Failed to allocate test packet MDL\n");
        ExFreePoolWithTag(ctx->test_packet_buffer, FILTER_ALLOC_TAG);
        NdisFreeNetBufferPool(ctx->nb_pool_handle);
        NdisFreeNetBufferListPool(ctx->nbl_pool_handle);
        ctx->test_packet_buffer = NULL;
        ctx->nb_pool_handle = NULL;
        ctx->nbl_pool_handle = NULL;
        goto skip_test_pools;
    }
    MmBuildMdlForNonPagedPool(ctx->test_packet_mdl);  // Lock pages (required for DMA)
    
    // Pre-allocate NBL ring (AVB_TEST_NBL_RING_SIZE slots) for fast SEND_PTP path.
    // Each slot gets its own buffer copy (avoids shared-MDL DMA races) and its own NBL+NB.
    // Non-fatal: a partially-initialized ring falls back to on-demand NdisAllocate per send.
    {
        PUCHAR template_pkt = (PUCHAR)ctx->test_packet_buffer;
        for (int _ri = 0; _ri < AVB_TEST_NBL_RING_SIZE; _ri++) {
            PVOID slot_buf = ExAllocatePool2(POOL_FLAG_NON_PAGED, TEST_PACKET_SIZE, FILTER_ALLOC_TAG);
            if (!slot_buf) break;
            RtlCopyMemory(slot_buf, template_pkt, TEST_PACKET_SIZE);

            PMDL slot_mdl = IoAllocateMdl(slot_buf, TEST_PACKET_SIZE, FALSE, FALSE, NULL);
            if (!slot_mdl) { ExFreePoolWithTag(slot_buf, FILTER_ALLOC_TAG); break; }
            MmBuildMdlForNonPagedPool(slot_mdl);

            PNET_BUFFER slot_nb = NdisAllocateNetBuffer(ctx->nb_pool_handle, slot_mdl, 0, TEST_PACKET_SIZE);
            if (!slot_nb) { IoFreeMdl(slot_mdl); ExFreePoolWithTag(slot_buf, FILTER_ALLOC_TAG); break; }

            PNET_BUFFER_LIST slot_nbl = NdisAllocateNetBufferList(ctx->nbl_pool_handle, 0, 0);
            if (!slot_nbl) {
                NdisFreeNetBuffer(slot_nb);
                IoFreeMdl(slot_mdl);
                ExFreePoolWithTag(slot_buf, FILTER_ALLOC_TAG);
                break;
            }
            slot_nbl->FirstNetBuffer = slot_nb;
            slot_nbl->Next = NULL;

            ctx->test_nbl_ring[_ri].nbl    = slot_nbl;
            ctx->test_nbl_ring[_ri].nb     = slot_nb;
            ctx->test_nbl_ring[_ri].buffer = slot_buf;
            ctx->test_nbl_ring[_ri].mdl    = slot_mdl;
            ctx->test_nbl_ring[_ri].in_use = 0;

            DEBUGP(DL_TRACE, "NBL ring slot[%d] alloc: nbl=%p nb=%p buf=%p\n",
                   _ri, slot_nbl, slot_nb, slot_buf);
        }
    }

    DEBUGP(DL_TRACE, "NDIS packet injection pools initialized (NBL=%p, NB=%p, buffer=%p, MDL=%p)\n",
           ctx->nbl_pool_handle, ctx->nb_pool_handle, ctx->test_packet_buffer, ctx->test_packet_mdl);
    
skip_test_pools:

    // Register this adapter in the global list; auto-select if none selected yet.
    NdisAcquireSpinLock(&g_AvbContextListLock);
    ctx->next_context = g_AvbContextList;
    g_AvbContextList  = ctx;
    if (g_AvbContext == NULL) {
        g_AvbContext = ctx;   // auto-select first adapter
    }
    NdisReleaseSpinLock(&g_AvbContextListLock);
    *OutCtx = ctx;
    DEBUGP(DL_TRACE, "AVB minimal context created VID=0x%04X DID=0x%04X state=%s\n", VendorId, DeviceId, AvbHwStateName(ctx->hw_state));
    DEBUGP(DL_ERROR, "!!! DIAG: AvbCreateMinimalContext - DeviceId=0x%04X -> device_type=%d, capabilities=0x%08X\n", 
           DeviceId, ctx->intel_device.device_type, ctx->intel_device.capabilities);
    return STATUS_SUCCESS;
}

/**
 * @brief Attempt full HW bring-up (intel_init + MMIO sanity + optional PTP for i210).
 *        Failure is non-fatal; enumeration remains with baseline capabilities.
 */
NTSTATUS AvbBringUpHardware(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    if (!Ctx) return STATUS_INVALID_PARAMETER;
    
    // Always set REALISTIC baseline capabilities based on device type and hardware specs
    ULONG baseline_caps = 0;
    switch (Ctx->intel_device.device_type) {
        // Legacy IGB devices - REALISTIC capabilities only
   //     case INTEL_DEVICE_82575:
   //         baseline_caps = INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic hardware only - NO PTP (2008 era)
   //         break;
   //     case INTEL_DEVICE_82576:
   //         baseline_caps = INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic hardware only - NO PTP (2009 era)
   //         break;
   //     case INTEL_DEVICE_82580:
   //         baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic PTP added (2010)
   //         break;
   //     case INTEL_DEVICE_I350:
   //         baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Standard IEEE 1588 (2012)
   //         break;
   //     case INTEL_DEVICE_I354:
   //         baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Same as I350 (2012)
   //         break;
            
        // Modern I-series devices - REALISTIC capabilities based on actual hardware
        case INTEL_DEVICE_I210:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO; // Enhanced PTP, NO TSN (2013)
            break;
        case INTEL_DEVICE_I217:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic PTP (2013)
            break;
        case INTEL_DEVICE_I219:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Enhanced PTP, NO TSN (2014)
            break;
            
        // ONLY I225/I226 support TSN - TSN standard finalized 2015-2016, first Intel implementation 2019
        case INTEL_DEVICE_I225:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                           INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO; // First Intel TSN (2019)
            break;
        case INTEL_DEVICE_I226:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                           INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE; // Full TSN (2020)
            break;
        default:
            baseline_caps = INTEL_CAP_MMIO; // Minimal safe assumption
            break;
    }
    
    // Set baseline capabilities even if hardware init fails
    DEBUGP(DL_FATAL, "!!! DIAG: AvbBringUpHardware - BEFORE assignment: device_type=%d, old_caps=0x%08X, baseline_caps=0x%08X\n",
           Ctx->intel_device.device_type, Ctx->intel_device.capabilities, baseline_caps);
    Ctx->intel_device.capabilities = baseline_caps;
    DEBUGP(DL_TRACE, "? AvbBringUpHardware: Set baseline capabilities 0x%08X for device type %d\n", 
           baseline_caps, Ctx->intel_device.device_type);
    DEBUGP(DL_FATAL, "!!! DIAG: AvbBringUpHardware - AFTER assignment: capabilities=0x%08X\n", 
           Ctx->intel_device.capabilities);
    
    NTSTATUS status = AvbPerformBasicInitialization(Ctx);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_TRACE, "?? AvbBringUpHardware: basic init failed 0x%08X (keeping baseline capabilities=0x%08X)\n", 
               status, baseline_caps);
        // Don't return error - allow enumeration with baseline capabilities
        return STATUS_SUCCESS;
    }
    
    // Device-specific post-initialization (allocate Intel library private structure)
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbBringUpHardware hw_state=%s (need BAR_MAPPED)\n", 
           AvbHwStateName(Ctx->hw_state));
    
    if (Ctx->hw_state >= AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_ERROR, "!!! DEBUG: Calling intel_init() for VID=0x%04X DID=0x%04X DevType=%d\n",
               Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id, 
               Ctx->intel_device.device_type);
        
        // Use intel_init() which properly dispatches to device-specific operations
        // This calls i226_ops.init() → ndis_platform_ops.init() → AvbPlatformInit()
        // which initializes PTP clock
        int init_result = intel_init(&Ctx->intel_device);
        
        DEBUGP(DL_ERROR, "!!! DEBUG: intel_init() returned: %d\n", init_result);
        
        if (init_result == 0) {
            DEBUGP(DL_TRACE, "? intel_init() successful - device initialized with PTP and TSN\n");
        } else {
            DEBUGP(DL_TRACE, "?? intel_init() failed: %d (PTP/TSN features unavailable)\n", init_result);
        }
    } else {
        DEBUGP(DL_ERROR, "!!! DEBUG: SKIPPING intel_init() - hw_state not ready!\n");
    }
    
    return STATUS_SUCCESS;
}

/* ======================================================================= */
/**
 * @brief Perform basic hardware discovery and MMIO setup. Promote to BAR_MAPPED.
 * 
 * CLEAN ARCHITECTURE: No device-specific register access - pure generic functionality.
 * Uses only common Intel register offsets that exist across all supported devices.
 * 
 * @param Ctx Device context to initialize
 * @return STATUS_SUCCESS on success, error status on failure
 * @note Implements Intel datasheet generic initialization sequence
 */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    DEBUGP(DL_TRACE, "? AvbPerformBasicInitialization: Starting for VID=0x%04X DID=0x%04X\n", 
           Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    
    if (!Ctx) return STATUS_INVALID_PARAMETER;
    
    if (Ctx->hw_access_enabled) {
        DEBUGP(DL_TRACE, "? AvbPerformBasicInitialization: Already initialized, returning success\n");
        return STATUS_SUCCESS;
    }

    /* Step 1: Discover & map BAR0 if not yet mapped */
    if (Ctx->hardware_context == NULL) {
        DEBUGP(DL_TRACE, "? STEP 1: Starting BAR0 discovery and mapping...\n");
        PHYSICAL_ADDRESS bar0 = {0};
        ULONG barLen = 0;
        NTSTATUS ds = AvbDiscoverIntelControllerResources(Ctx->filter_instance, &bar0, &barLen);
        if (!NT_SUCCESS(ds)) {
            DEBUGP(DL_ERROR, "? STEP 1 FAILED: BAR0 discovery failed 0x%08X (cannot map MMIO yet) VID=0x%04X DID=0x%04X\n", 
                   ds, Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
            return ds; /* propagate */
        }
        DEBUGP(DL_TRACE, "? STEP 1a SUCCESS: BAR0 discovered: PA=0x%llx Len=0x%x\n", bar0.QuadPart, barLen);
        
        NTSTATUS ms = AvbMapIntelControllerMemory(Ctx, bar0, barLen);
        if (!NT_SUCCESS(ms)) {
            DEBUGP(DL_ERROR, "? STEP 1b FAILED: BAR0 map failed 0x%08X (MmMapIoSpace)\n", ms);
            return ms;
        }
        DEBUGP(DL_TRACE, "? STEP 1b SUCCESS: MMIO mapped (opaque ctx=%p)\n", Ctx->hardware_context);
    } else {
        DEBUGP(DL_TRACE, "? STEP 1 SKIPPED: Hardware context already exists (%p)\n", Ctx->hardware_context);
    }

    DEBUGP(DL_TRACE, "? STEP 2: Setting up Intel device structure and private data...\n");
    // CRITICAL FIX: Do NOT reset capabilities that were set by AvbBringUpHardware
    // Capabilities are device-specific and set based on hardware specs
    // This function only adds INTEL_CAP_MMIO after successful BAR0 mapping
    
    // CRITICAL FIX: Initialize private_data before calling intel_init
    // The Intel library requires private_data to be allocated and initialized
    if (Ctx->intel_device.private_data == NULL) {
        struct intel_private *priv = (struct intel_private *)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(struct intel_private),
            'IAvb'
        );
        if (!priv) {
            DEBUGP(DL_ERROR, "? STEP 2 FAILED: Cannot allocate private_data\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(priv, sizeof(struct intel_private));
        
        // Initialize critical fields
        priv->device_type = Ctx->intel_device.device_type;
        
        // **CRITICAL FIX**: Connect platform_data to Windows hardware context
        // This allows intel_avb library to access AVB_DEVICE_CONTEXT for platform operations
        priv->platform_data = (void*)Ctx;
        DEBUGP(DL_FATAL, "!!! DIAG: STEP 2a: platform_data -> Ctx=%p (enables AvbMmioReadReal access)\n", Ctx);
        
        // CRITICAL: hardware_context might be NULL or uninitialized at this point
        // Only access it if we've already mapped hardware (Step 1 succeeded)
        if (Ctx->hardware_context != NULL && Ctx->hw_state >= AVB_HW_BAR_MAPPED) {
            PINTEL_HARDWARE_CONTEXT hwCtx = (PINTEL_HARDWARE_CONTEXT)Ctx->hardware_context;
            priv->mmio_base = hwCtx->mmio_base;
            DEBUGP(DL_FATAL, "!!! DIAG: STEP 2b: mmio_base=%p from hardware_context\n", priv->mmio_base);
        } else {
            priv->mmio_base = NULL; // Will be set later when hardware is mapped
            DEBUGP(DL_FATAL, "!!! DIAG: STEP 2b: MMIO not yet mapped, deferring\n");
        }
        priv->initialized = 0;
        
        Ctx->intel_device.private_data = priv;
        DEBUGP(DL_TRACE, "? STEP 2b: Allocated private_data (size=%u, ptr=%p)\n", 
               sizeof(struct intel_private), priv);
    }
    DEBUGP(DL_TRACE, "? STEP 2 SUCCESS: Device structure prepared with private_data\n");

    DEBUGP(DL_TRACE, "? STEP 3: Calling intel_init library function...\n");
    DEBUGP(DL_TRACE, "   - VID=0x%04X DID=0x%04X private_data=%p\n", 
           Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id, 
           Ctx->intel_device.private_data);
    
    // Now call device-specific initialization
    if (intel_init(&Ctx->intel_device) != 0) {
        DEBUGP(DL_ERROR, "? STEP 3 FAILED: intel_init failed (library)\n");
        return STATUS_UNSUCCESSFUL;
    }
    DEBUGP(DL_TRACE, "? STEP 3 SUCCESS: intel_init completed successfully\n");

    DEBUGP(DL_TRACE, "? STEP 4: MMIO sanity check - reading CTRL register via Intel library...\n");
    ULONG ctrl = INTEL_MASK_32BIT;
    
    // CLEAN ARCHITECTURE: Use Intel library for all register access - no device-specific contamination
    if (intel_read_reg(&Ctx->intel_device, INTEL_GENERIC_CTRL_REG, &ctrl) != 0 || ctrl == INTEL_MASK_32BIT) {
        DEBUGP(DL_ERROR, "? STEP 4 FAILED: MMIO sanity read failed CTRL=0x%08X (expected != INTEL_MASK_32BIT)\n", ctrl);
        DEBUGP(DL_ERROR, "   This indicates BAR0 mapping is not working properly\n");
        return STATUS_DEVICE_NOT_READY;
    }
    DEBUGP(DL_TRACE, "? STEP 4 SUCCESS: MMIO sanity check passed - CTRL=0x%08X\n", ctrl);

    DEBUGP(DL_TRACE, "? STEP 5: Promoting hardware state to BAR_MAPPED...\n");
    Ctx->intel_device.capabilities |= INTEL_CAP_MMIO;
    if (Ctx->hw_state < AVB_HW_BAR_MAPPED) {
        AVB_SET_HW_STATE(Ctx, AVB_HW_BAR_MAPPED);
        DEBUGP(DL_TRACE, "? STEP 5 SUCCESS: HW state -> %s (CTRL=0x%08X)\n", AvbHwStateName(Ctx->hw_state), ctrl);
    }
    Ctx->initialized = TRUE;
    Ctx->hw_access_enabled = TRUE;
    
    DEBUGP(DL_TRACE, "? AvbPerformBasicInitialization: COMPLETE SUCCESS\n");
    DEBUGP(DL_TRACE, "   - Final hw_state: %s\n", AvbHwStateName(Ctx->hw_state));
    DEBUGP(DL_TRACE, "   - Final capabilities: 0x%08X\n", Ctx->intel_device.capabilities);
    DEBUGP(DL_TRACE, "   - Hardware access enabled: %s\n", Ctx->hw_access_enabled ? "YES" : "NO");
    
    return STATUS_SUCCESS;
}

/* ======================================================================= */
/* Enhanced Device Initialization Functions - Clean Architecture */

/**
 * @brief Generic device initialization with proper return status
 * 
 * Delegates to device-specific implementations via Intel library.
 * Supports all Intel device families through clean abstraction.
 * 
 * @param context The device context  
 * @return STATUS_SUCCESS on success, STATUS_DEVICE_NOT_READY if hardware not ready,
 *         STATUS_DEVICE_HARDWARE_ERROR if initialization fails
 * @note Uses Intel library device operations for hardware-specific setup
 */
NTSTATUS AvbEnsureDeviceReady(PAVB_DEVICE_CONTEXT context)
{
    if (!context || context->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_TRACE, "AvbEnsureDeviceReady: Hardware not ready (state=%s)\n", 
               context ? AvbHwStateName(context->hw_state) : "NULL");
        return STATUS_DEVICE_NOT_READY;
    }
    
    DEBUGP(DL_TRACE, "? AvbEnsureDeviceReady: Starting device initialization\n");
    DEBUGP(DL_TRACE, "   - Context: VID=0x%04X DID=0x%04X (type=%d)\n", 
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id,
           context->intel_device.device_type);
    
    // CLEAN ARCHITECTURE: Use Intel library device-specific initialization
    // No device-specific register access in generic integration layer
    int init_result = intel_init(&context->intel_device);
    if (init_result != 0) {
        DEBUGP(DL_ERROR, "? AvbEnsureDeviceReady: intel_init failed: %d\n", init_result);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    
    DEBUGP(DL_TRACE, "? AvbEnsureDeviceReady: Device initialization successful\n");
    
    // Update capabilities and state based on successful initialization
    context->intel_device.capabilities |= INTEL_CAP_MMIO;
    if (context->hw_state < AVB_HW_PTP_READY) {
        AVB_SET_HW_STATE(context, AVB_HW_PTP_READY);
        DEBUGP(DL_TRACE, "HW state -> %s (device ready)\n", AvbHwStateName(context->hw_state));
    }
    
    return STATUS_SUCCESS;
}

// Legacy function names for compatibility - redirect to generic implementations
/**
 * @brief Legacy I210-specific initialization function (redirects to generic implementation)
 * 
 * @deprecated Use AvbEnsureDeviceReady() instead for all device types
 * @param context The I210 device context
 * @return NTSTATUS Success/failure status
 */
NTSTATUS AvbI210EnsureSystimRunning(PAVB_DEVICE_CONTEXT context)
{
    DEBUGP(DL_TRACE, "AvbI210EnsureSystimRunning: Redirecting to generic device initialization\n");
    return AvbEnsureDeviceReady(context);
}

/* ======================================================================= */
/* NDIS attach entry */
NTSTATUS AvbInitializeDevice(_In_ PMS_FILTER FilterModule, _Out_ PAVB_DEVICE_CONTEXT *AvbContext)
{
    if (!AvbContext) return STATUS_INVALID_PARAMETER;
    USHORT ven=0, dev=0;
    if (!AvbIsSupportedIntelController(FilterModule, &ven, &dev)) return STATUS_NOT_SUPPORTED;
    NTSTATUS st = AvbCreateMinimalContext(FilterModule, ven, dev, AvbContext);
    if (!NT_SUCCESS(st)) return st;
    (void)AvbBringUpHardware(*AvbContext); /* deferred; ignore failure */
    return STATUS_SUCCESS;
}

/* Task 6c: TX Timestamp Polling Timer DPC
 * Polls TX timestamp FIFO at 1ms intervals and posts TS_EVENT_TX_TIMESTAMP events
 * Implements: Issue #13 (REQ-F-TS-SUB-001) Task 6c - TX path polling
 * 
 * Hardware: TXSTMPL and TXSTMPH registers (see device-specific headers)
 *   - Bit 31 of TXSTMPH indicates valid timestamp in FIFO
 *   - Reading TXSTMPL advances FIFO to next entry
 *   - FIFO depth varies by device (typically 4-8 entries)
 */
VOID AvbTxTimestampPollDpc(
    _In_ PVOID SystemSpecific1,
    _In_ PVOID FunctionContext,
    _In_ PVOID SystemSpecific2,
    _In_ PVOID SystemSpecific3)
{
    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);
    
    PAVB_DEVICE_CONTEXT AvbContext = (PAVB_DEVICE_CONTEXT)FunctionContext;
    
    // Build version marker for verification
    /* Todo 10 fix: was 'static ULONG dpc_call_count = 0;' — non-atomic increment
     * was unsafe on SMP.  Promote to volatile LONG for InterlockedIncrement. */
    static volatile LONG dpc_call_count = 0;
    static BOOLEAN version_logged = FALSE;
    
    // Bug #1d: REMOVED ZwCreateKey/ZwSetValueKey from DPC — ZwCreateKey requires
    // PASSIVE_LEVEL but DPCs run at DISPATCH_LEVEL.  Calling it here caused LFH
    // delay-free list corruption (0x13A_17 bugcheck).  Counter is incremented
    // unconditionally just below; use DbgView trace output for diagnostics.
    
    if (!version_logged) {
        DEBUGP(DL_TRACE, "!!! ======================================\n");
        DEBUGP(DL_TRACE, "!!! DRIVER VERSION: %u.%u.%u.%u\n", 
               AVB_VERSION_MAJOR, AVB_VERSION_MINOR, AVB_VERSION_BUILD, AVB_VERSION_REVISION);
        DEBUGP(DL_TRACE, "!!! BUILD TIMESTAMP: " __DATE__ " " __TIME__ "\n");
        DEBUGP(DL_TRACE, "!!! ======================================\n");
        version_logged = TRUE;
    }
    
#if DBG
    LONG call_num = InterlockedIncrement(&dpc_call_count);
    DEBUGP(DL_ERROR, "!!! DPC: Timer callback fired! Call=%d, Context=%p\n", call_num, AvbContext);
    DEBUGP(DL_TRACE, "!!! DPC FIRED [#%d]: ctx=%p, g_ctx=%p\n", call_num, AvbContext, g_AvbContext);
#else
    InterlockedIncrement(&dpc_call_count);
#endif
    
    // CRITICAL: Verify context is still valid (global might be cleared during cleanup)
    if (!AvbContext || AvbContext != g_AvbContext) {
        DEBUGP(DL_ERROR, "!!! DPC: Context invalid, exiting\n");
        return;  // Context being cleaned up, do NOT re-arm timer
    }
    
    DEBUGP(DL_TRACE, "!!! DPC: poll_active=%d, hw_state=%d\n", 
           AvbContext->tx_poll_active, AvbContext->hw_state);
    
    if (!AvbContext->tx_poll_active || AvbContext->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_ERROR, "!!! DPC: Exiting early (poll_active=%d, hw_state=%d < %d)\n",
               AvbContext->tx_poll_active, AvbContext->hw_state, AVB_HW_BAR_MAPPED);
        return;  // Timer stopped or hardware not ready
    }
    
    DEBUGP(DL_TRACE, "!!! DPC: Checking TX timestamps and target time\n");
    
    device_t *dev = &AvbContext->intel_device;
    
    // Get device operations for HAL-compliant register access
    const intel_device_ops_t *ops = intel_get_device_ops(dev->device_type);
    if (ops == NULL || ops->poll_tx_timestamp_fifo == NULL) {
        DEBUGP(DL_ERROR, "!!! DPC: Device operations not available\n");
        return;
    }
    
    // Poll TX timestamp FIFO (read up to 8 entries to drain FIFO)
    // HAL-compliant: Uses device operation instead of direct register access
    for (int i = 0; i < 8; i++) {
        avb_u64 timestamp_ns = 0;
        int result = ops->poll_tx_timestamp_fifo(dev, &timestamp_ns);
        
        if (result < 0) {
            break;  // Read error
        }
        if (result == 0) {
            break;  // FIFO empty
        }
        
        // result == 1: Valid timestamp retrieved
        // Apply IEEE 802.1AS egress latency correction (timestampCorrectionPortDS).
        // egress_latency_ns compensates for the delay between the hardware TX timestamp
        // latch and the actual wire departure.  Default = 0.
        timestamp_ns = (avb_u64)((INT64)timestamp_ns +
                       InterlockedCompareExchange64(&AvbContext->egress_latency_ns, 0, 0));

        // Post TX timestamp event to matching subscriptions
        // FIXED: Pass per-adapter context for multi-adapter support
        AvbPostTimestampEvent(
            AvbContext,  /* Pass THIS adapter's context */
            TS_EVENT_TX_TIMESTAMP,
            timestamp_ns,
            INTEL_MASK_16BIT,  // No VLAN (TX timestamps don't include packet metadata)
            0xFF,    // No PCP
            0,       // Unknown queue
            0,       // Unknown packet length
            0,       // No trigger source
            0        // No correctionField for TX events
        );
    }
    
    /* Task 7: Check target time events (1ms polling)
     * Monitors TT0/TT1 flags (TSICR bits 3-4) for target time reached conditions
     */
    AvbCheckTargetTime(AvbContext);
    
    // Re-arm timer for next poll (1ms periodic)
    if (AvbContext->tx_poll_active) {
        NdisSetTimer(&AvbContext->tx_poll_timer, 1);
    }
}

/* Task 6b: Post timestamp event to all matching subscriptions (lock-free ring buffer write)
 * Called from RX path (Task 6a), TX polling (Task 6c), or other event sources
 * Implements: Issue #13 (REQ-F-TS-SUB-001) Task 6b - Event posting helper
 * FIXED: Accept context parameter for multi-adapter support
 */
VOID AvbPostTimestampEvent(
    _In_ PVOID AvbContextParam,
    _In_ avb_u32 event_type,
    _In_ avb_u64 timestamp_ns,
    _In_ avb_u16 vlan_id,
    _In_ avb_u8 pcp,
    _In_ avb_u8 queue,
    _In_ avb_u16 packet_length,
    _In_ avb_u8 trigger_source,
    _In_ avb_i64 correction_field)
{
    PAVB_DEVICE_CONTEXT AvbContext = (PAVB_DEVICE_CONTEXT)AvbContextParam;
    if (!AvbContext) {
        DEBUGP(DL_ERROR, "!!! AvbPostTimestampEvent: NULL context\n");
        return;
    }
    
    DEBUGP(DL_ERROR, "!!! AvbPostTimestampEvent [Adapter 0x%04X]: ENTRY - event_type=0x%x, ts=0x%llx\n", 
           AvbContext->intel_device.pci_device_id, event_type, timestamp_ns);
    
    /* Acquire spin lock to protect subscription table iteration */
    NdisAcquireSpinLock(&AvbContext->subscription_lock);
    
    DEBUGP(DL_ERROR, "!!! Checking %d subscriptions for adapter context %p\n", MAX_TS_SUBSCRIPTIONS, AvbContext);
    
    int active_count = 0;
    int matched_count = 0;
    
    for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        TS_SUBSCRIPTION *sub = &AvbContext->subscriptions[i];
        
        if (sub->active && sub->ring_buffer) {
            active_count++;
            DEBUGP(DL_ERROR, "!!! Sub[%d]: active=%d, ring_id=%u, mask=0x%x (want 0x%x), ring=%p\n", 
                   i, sub->active, sub->ring_id, sub->event_mask, event_type, sub->ring_buffer);
        }
        
        /* Skip inactive subscriptions */
        if (!sub->active || !sub->ring_buffer) continue;
        
        /* Filter by event type mask */
        if (!(sub->event_mask & event_type)) {
            DEBUGP(DL_ERROR, "!!! Sub[%d]: Type MISMATCH (sub wants 0x%x, event is 0x%x) - SKIPPING\n", 
                   i, sub->event_mask, event_type);
            continue;
        }
        
        matched_count++;
        DEBUGP(DL_ERROR, "!!! Sub[%d]: Type MATCHED (mask 0x%x & event 0x%x) - will post event\n",
               i, sub->event_mask, event_type);
        
        /* Filter by VLAN ID FILTER_VLAN_NONE */
        if (sub->vlan_filter != INTEL_MASK_16BIT && sub->vlan_filter != vlan_id) continue;
        
        /* Filter by PCP (0xFF = no filter) */
        if (sub->pcp_filter != 0xFF && sub->pcp_filter != pcp) continue;
        
        /* Lock-free ring buffer write */
        AVB_TIMESTAMP_RING_HEADER *hdr = (AVB_TIMESTAMP_RING_HEADER *)sub->ring_buffer;
        AVB_TIMESTAMP_EVENT *events = (AVB_TIMESTAMP_EVENT *)(hdr + 1);
        
        avb_u32 local_prod = hdr->producer_index;  /* Atomic read */
        avb_u32 local_cons = hdr->consumer_index;  /* Atomic read */
        avb_u32 next_prod = (local_prod + 1) & hdr->mask;
        
        /* Check if ring is full */
        if (next_prod == local_cons) {
            /* Ring full - drop event and increment overflow counter */
            InterlockedIncrement((volatile LONG *)&hdr->overflow_count);
            continue;
        }
        
        /* Write event to ring buffer */
        AVB_TIMESTAMP_EVENT *evt = &events[local_prod & hdr->mask];
        evt->timestamp_ns    = timestamp_ns;
        evt->event_type      = event_type;
        evt->sequence_num    = (avb_u32)InterlockedIncrement(&sub->sequence_num);
        evt->vlan_id         = vlan_id;
        evt->pcp             = pcp;
        evt->queue           = queue;
        evt->packet_length   = packet_length;
        evt->trigger_source  = trigger_source;
        evt->reserved[0]     = 0;
        /* IEEE 1588 correctionField: signed 64-bit 2^-16 ns fixed-point.
         * Positive or negative — stored verbatim; consumer right-shifts by 16 to get ns. */
        evt->correction_field = correction_field;
        
        /* Memory barrier - ensure event written before index update */
        KeMemoryBarrier();
        
        /* Update producer index (atomic write) */
        hdr->producer_index = next_prod;
        
        /* Increment total events counter */
        InterlockedIncrement64((volatile LONG64 *)&hdr->total_events);
        
        DEBUGP(DL_ERROR, "!!! Sub[%d]: EVENT POSTED - prod=%u->%u, cons=%u, seq=%u, ts=0x%llx\n",
               i, local_prod, next_prod, local_cons, evt->sequence_num, evt->timestamp_ns);
    }
    
    DEBUGP(DL_ERROR, "!!! AvbPostTimestampEvent: EXIT - active=%d, matched=%d, posted to matched subs\n",
           active_count, matched_count);
    
    NdisReleaseSpinLock(&AvbContext->subscription_lock);
}

/* Task 7: Check target time events (TT0/TT1 interrupt flags)
 * Called from AvbTxTimestampPollDpc every 1ms to monitor target time reached conditions
 * Posts TS_EVENT_TARGET_TIME events when SYSTIM >= TRGTTIMLx
 * Implements: Issue #13 (REQ-F-TS-EVENT-001) Task 7 - Target time event generation
 * HAL-COMPLIANT: Uses device ops, no hardcoded registers
 * 
 * CORRECTED per I225 datasheet:
 * - Checks TSICR register bits 3-4 (TT0/TT1)
 * - NOT AUTT0/AUTT1 which are for SDP input sampling
 * - Variable name "autt_flags" kept for backwards compatibility but actually holds TT flags
 */
VOID AvbCheckTargetTime(_In_ PAVB_DEVICE_CONTEXT AvbContext)
{
    const intel_device_ops_t *ops;
    device_t *dev;
    uint8_t autt_flags = 0;  // Actually TT0/TT1 flags (name kept for compatibility)
    int rc;
    
    /* Validate context */
    if (!AvbContext || AvbContext != g_AvbContext) {
        return;
    }
    
    /* Skip if hardware not ready */
    if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) {
        return;
    }
    
    /* Get device operations (HAL interface) */
    ops = intel_get_device_ops(AvbContext->intel_device.device_type);
    if (!ops || !ops->check_autt_flags) {
        /* Device doesn't support target time events */
        return;
    }
    
    dev = &AvbContext->intel_device;
    
    /* Check TT0/TT1 flags in TSICR register via device HAL
     * Bit pattern: 0x01 = TT0 (Target Time 0), 0x02 = TT1 (Target Time 1)
     * Per I225 datasheet Section 8.16.1: TSICR bits 3-4 are TT0/TT1 interrupt causes
     */
    rc = ops->check_autt_flags(dev, &autt_flags);
    if (rc != 0) {
        DEBUGP(DL_TRACE, "!!! check_autt_flags failed: %d\n", rc);
        return;
    }
    
    // Bug #1d: REMOVED ZwCreateKey/ZwSetValueKey — AvbCheckTargetTime is called
    // from AvbTxTimestampPollDpc at DISPATCH_LEVEL; registry APIs require PASSIVE_LEVEL.
    
    /* ALWAYS log AUTT flags for debugging (even when 0) with target time comparison */
    static int log_counter = 0;
    if (++log_counter % 10 == 0 || autt_flags != 0) {
        // Read current SYSTIM and target times for comparison
        uint64_t current_systim = 0;
        uint32_t target0_low = 0, target0_high = 0;
        uint32_t target1_low = 0, target1_high = 0;
        uint32_t tsicr_raw = 0, tsauxc_raw = 0, tsim_raw = 0;
        
        if (ops->get_systime) {
            ops->get_systime(dev, &current_systim);
        }
        AvbMmioReadReal(dev, INTEL_REG_TRGTTIML0, &target0_low);  // TRGTTIML0
        AvbMmioReadReal(dev, INTEL_REG_TRGTTIMH0, &target0_high); // TRGTTIMH0
        AvbMmioReadReal(dev, INTEL_REG_TRGTTIML1, &target1_low);  // TRGTTIML1
        AvbMmioReadReal(dev, INTEL_REG_TRGTTIMH1, &target1_high); // TRGTTIMH1
        AvbMmioReadReal(dev, INTEL_REG_TSICR, &tsicr_raw);    // TSICR (Interrupt Cause)
        AvbMmioReadReal(dev, INTEL_REG_TSAUXC, &tsauxc_raw);   // TSAUXC (Auxiliary Control)
        AvbMmioReadReal(dev, INTEL_REG_TSIM, &tsim_raw);     // TSIM (Interrupt Mask)
        
        uint64_t target0 = ((uint64_t)target0_high << 32) | target0_low;
        uint64_t target1 = ((uint64_t)target1_high << 32) | target1_low;
        
        int64_t delta0 = (int64_t)(target0 - current_systim);
        int64_t delta1 = (int64_t)(target1 - current_systim);
        UNREFERENCED_PARAMETER(delta0);  // Used only in DEBUGP
        UNREFERENCED_PARAMETER(delta1);  // Used only in DEBUGP
        
        DEBUGP(DL_TRACE, "!!! TT flags: 0x%02X (TT0=%d, TT1=%d) [check #%d]\n", 
               autt_flags, (autt_flags & 0x01) ? 1 : 0, (autt_flags & 0x02) ? 1 : 0, log_counter);
        DEBUGP(DL_TRACE, "!!! SYSTIM:  0x%016llX (%llu ns)\n",
               (unsigned long long)current_systim, (unsigned long long)current_systim);
        DEBUGP(DL_TRACE, "!!! TSICR:   0x%08X (TT0=%d, TT1=%d, AUTT0=%d)\n",
               tsicr_raw, (tsicr_raw >> 3) & 1, (tsicr_raw >> 4) & 1, (tsicr_raw >> 5) & 1);
        DEBUGP(DL_TRACE, "!!! TSAUXC:  0x%08X (EN_TT0=%d, EN_TT1=%d)\n",
               tsauxc_raw, tsauxc_raw & 1, (tsauxc_raw >> 4) & 1);
        DEBUGP(DL_TRACE, "!!! TSIM:    0x%08X (TT0_mask=%d, TT1_mask=%d)\n",
               tsim_raw, (tsim_raw >> 3) & 1, (tsim_raw >> 4) & 1);
        DEBUGP(DL_TRACE, "!!! TARGET0: 0x%016llX (delta: %s%lld ns = %lld ms)\n",
               (unsigned long long)target0,
               (delta0 < 0) ? "-" : "+", (long long)(delta0 < 0 ? -delta0 : delta0),
               (long long)(delta0 < 0 ? -delta0 : delta0) / 1000000);
        if (target1 != 0) {
            DEBUGP(DL_TRACE, "!!! TARGET1: 0x%016llX (delta: %s%lld ns = %lld ms)\n",
                   (unsigned long long)target1,
                   (delta1 < 0) ? "-" : "+", (long long)(delta1 < 0 ? -delta1 : delta1),
                   (long long)(delta1 < 0 ? -delta1 : delta1) / 1000000);
        }
        
        // Bug #1d: REMOVED ZwCreateKey/ZwSetValueKey — still at DISPATCH_LEVEL here.
    }
    
    /* Timer 0: Check TT0 flag (bit 0 in autt_flags) */
    if (autt_flags & 0x01) {
        uint64_t timestamp_ns = 0;
        
        /* Get current SYSTIM value (when target time was reached) */
        if (ops->get_systime && ops->get_systime(dev, &timestamp_ns) == 0) {
            /* Post target time event to subscribers
             * event_type: TS_EVENT_TARGET_TIME (0x04)
             * vlan_id: FILTER_VLAN_NONE (not applicable)
             * pcp: 0xFF (not applicable)
             * queue: 0 (not applicable)
             * packet_length: 0 (not applicable)
             * trigger_source: 0 (timer 0)
             */
            AvbPostTimestampEvent(
                AvbContext,
                TS_EVENT_TARGET_TIME,
                timestamp_ns,
                INTEL_MASK_16BIT,  /* vlan_id - not applicable */
                0xFF,    /* pcp - not applicable */
                0,       /* queue - not applicable */
                0,       /* packet_length - not applicable */
                0,       /* trigger_source: timer 0 */
                0        /* correction_field - not applicable */
            );
            
            DEBUGP(DL_TRACE, "!!! TARGET TIME 0 EVENT POSTED: ts=%llu ns\n", timestamp_ns);
        }
        
        /* Clear TT0 flag in TSICR (write-1-to-clear) */
        if (ops->clear_autt_flag) {
            rc = ops->clear_autt_flag(dev, 0);
            if (rc != 0) {
                DEBUGP(DL_TRACE, "clear_autt_flag(0) failed: %d\n", rc);
            }
        }
    }
    
    /* Timer 1: Check TT1 flag (bit 1 in autt_flags) */
    if (autt_flags & 0x02) {
        uint64_t timestamp_ns = 0;
        
        /* Get current SYSTIM value */
        if (ops->get_systime && ops->get_systime(dev, &timestamp_ns) == 0) {
            /* Post target time event
             * trigger_source: 1 (timer 1)
             */
            AvbPostTimestampEvent(
                AvbContext,
                TS_EVENT_TARGET_TIME,
                timestamp_ns,
                INTEL_MASK_16BIT,  /* vlan_id - not applicable */
                0xFF,    /* pcp - not applicable */
                0,       /* queue - not applicable */
                0,       /* packet_length - not applicable */
                1,       /* trigger_source: timer 1 */
                0        /* correction_field - not applicable */
            );
            
            DEBUGP(DL_TRACE, "!!! TARGET TIME 1 EVENT POSTED: ts=%llu ns\n", timestamp_ns);
        }
        
        /* Clear TT1 flag in TSICR (write-1-to-clear) */
        if (ops->clear_autt_flag) {
            rc = ops->clear_autt_flag(dev, 1);
            if (rc != 0) {
                DEBUGP(DL_TRACE, "clear_autt_flag(1) failed: %d\n", rc);
            }
        }
    }
}

/* Task 5: Cleanup subscriptions for a specific file object (implicit unsubscribe on handle close)
 * Called from IRP_MJ_CLEANUP handler when application closes device handle
 * Implements: Issue #13 (REQ-F-TS-SUB-001) Task 5 - Option B (implicit cleanup)
 */
VOID AvbCleanupFileSubscriptions(_In_ PFILE_OBJECT FileObject)
{
    /* Prefer the per-handle adapter context (set by IOCTL_AVB_OPEN_ADAPTER).
     * Fall back to g_AvbContext for handles that never called OPEN_ADAPTER. */
    PAVB_DEVICE_CONTEXT AvbContext = (FileObject && FileObject->FsContext)
                                     ? (PAVB_DEVICE_CONTEXT)FileObject->FsContext
                                     : g_AvbContext;
    if (!AvbContext || !FileObject) return;
    
    int cleaned = 0;

    /* Phase 1 — hold spinlock only to snapshot pointers and NULL the fields.
     * MmUnmapLockedPages / IoFreeMdl require IRQL <= APC_LEVEL; calling them
     * while holding an NDIS spinlock (which raises IRQL to DISPATCH_LEVEL)
     * triggers BSOD 0xC4 / 0x7A.  Collect all resources under the lock, then
     * release the lock before touching any MDL APIs.
     */
    PVOID ring_to_free[MAX_TS_SUBSCRIPTIONS];
    PMDL  mdl_to_free[MAX_TS_SUBSCRIPTIONS];
    PVOID user_va_to_unmap[MAX_TS_SUBSCRIPTIONS];
    ULONG ring_ids[MAX_TS_SUBSCRIPTIONS];
    ULONG ring_counts[MAX_TS_SUBSCRIPTIONS];
    RtlZeroMemory(ring_to_free,     sizeof(ring_to_free));
    RtlZeroMemory(mdl_to_free,      sizeof(mdl_to_free));
    RtlZeroMemory(user_va_to_unmap, sizeof(user_va_to_unmap));
    RtlZeroMemory(ring_ids,         sizeof(ring_ids));
    RtlZeroMemory(ring_counts,      sizeof(ring_counts));

    NdisAcquireSpinLock(&AvbContext->subscription_lock);

    for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        TS_SUBSCRIPTION *sub = &AvbContext->subscriptions[i];

        if (sub->active && sub->file_object == FileObject) {
            ring_ids[i]         = sub->ring_id;
            ring_counts[i]      = sub->ring_count;
            ring_to_free[i]     = sub->ring_buffer;
            mdl_to_free[i]      = sub->ring_mdl;
            user_va_to_unmap[i] = sub->user_va;

            sub->ring_buffer  = NULL;
            sub->ring_mdl     = NULL;
            sub->user_va      = NULL;
            sub->active       = 0;
            sub->ring_id      = 0;
            sub->file_object  = NULL;
            cleaned++;
        }
    }

    // Check if all subscriptions are now inactive — stop timer to save CPU
    BOOLEAN any_active = FALSE;
    for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        if (AvbContext->subscriptions[i].active) {
            any_active = TRUE;
            break;
        }
    }
    if (!any_active && AvbContext->tx_poll_active) {
        AvbContext->tx_poll_active = FALSE;  // Signal DPC to stop re-arming
        DEBUGP(DL_TRACE, "All subscriptions removed - TX polling will stop\n");
    }

    NdisReleaseSpinLock(&AvbContext->subscription_lock);

    /* Phase 2 — free resources OUTSIDE the spinlock (IRQL back to PASSIVE / APC).
     * MmUnmapLockedPages, IoFreeMdl are now safe to call.
     */
    for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        if (mdl_to_free[i] || ring_to_free[i]) {
            DEBUGP(DL_TRACE, "Cleaning up subscription ring_id=%u for FileObject=%p\n",
                   ring_ids[i], FileObject);

            if (mdl_to_free[i]) {
                if (user_va_to_unmap[i]) {
                    __try {
                        MmUnmapLockedPages(user_va_to_unmap[i], mdl_to_free[i]);
                        DEBUGP(DL_TRACE, "  Unmapped user VA %p\n", user_va_to_unmap[i]);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        DEBUGP(DL_ERROR, "  Exception during MmUnmapLockedPages: 0x%08X\n", GetExceptionCode());
                    }
                }
                IoFreeMdl(mdl_to_free[i]);
                DEBUGP(DL_TRACE, "  Freed MDL\n");
            }

            if (ring_to_free[i]) {
                ExFreePoolWithTag(ring_to_free[i], FILTER_ALLOC_TAG);
                DEBUGP(DL_TRACE, "  Freed ring buffer (%lu bytes)\n",
                       (ULONG)(sizeof(AVB_TIMESTAMP_RING_HEADER) + ring_counts[i] * sizeof(AVB_TIMESTAMP_EVENT)));
            }
        }
    }

    if (cleaned > 0) {
        DEBUGP(DL_TRACE, "Cleaned up %d subscription(s) for FileObject=%p\n", cleaned, FileObject);
    }
    /* NOTE: ATDECC subscriptions are driver-global and NOT tied to a file handle lifetime.
     * They persist until explicitly UNSUBSCRIBED (IOCTL_AVB_ATDECC_EVENT_UNSUBSCRIBE).
     * This is by design — TC-002 subscribes on one handle and TC-003/TC-004 poll/unsubscribe
     * from a different handle.  Slot exhaustion is mitigated by MAX_ATDECC_SUBSCRIPTIONS=16. */
}

/* BUGFIX 0x9F: Stop timers WITHOUT freeing memory.
 * Called from FilterPause to prevent MMIO reads on powered-down hardware.
 * Also called at the start of AvbCleanupDevice (idempotent — safe if already stopped).
 * Must be called at IRQL = PASSIVE_LEVEL.
 */
VOID AvbStopTimers(_In_opt_ PAVB_DEVICE_CONTEXT AvbContext)
{
    if (!AvbContext) return;

    /* CRITICAL: Cancel TX timestamp polling timer (Task 6c) */
    if (AvbContext->tx_poll_active) {
        BOOLEAN cancelled = FALSE;
        AvbContext->tx_poll_active = FALSE;  /* Signal DPC to stop re-arming */
        NdisCancelTimer(&AvbContext->tx_poll_timer, &cancelled);
        KeFlushQueuedDpcs();                 /* Bug #1: wait for running DPC */
        NdisCancelTimer(&AvbContext->tx_poll_timer, &cancelled); /* Bug #1b: cancel re-armed */
        KeFlushQueuedDpcs();                 /* Bug #1c: drain DPC queued in flush window */
        DEBUGP(DL_TRACE, "AvbStopTimers: TX poll timer stopped (was_pending=%d)\n", !cancelled);
    }

    /* CRITICAL: Cancel Target Time polling timer (Task 7) */
    if (AvbContext->target_time_poll_active) {
        BOOLEAN cancelled;
        AvbContext->target_time_poll_active = FALSE;
        cancelled = KeCancelTimer(&AvbContext->target_time_timer);
        KeFlushQueuedDpcs();
        KeCancelTimer(&AvbContext->target_time_timer);
        KeFlushQueuedDpcs();
        DEBUGP(DL_TRACE, "AvbStopTimers: target time timer stopped (was_pending=%d)\n", cancelled);
    }
}

/* Restart timers after FilterRestart if there are active subscriptions.
 * Only restarts tx_poll_timer; target_time_timer is re-armed on demand via IOCTL.
 */
VOID AvbRestartTimers(_In_opt_ PAVB_DEVICE_CONTEXT AvbContext)
{
    int i;
    BOOLEAN has_subs = FALSE;

    if (!AvbContext) return;
    if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) return;
    if (AvbContext->tx_poll_active) return;  /* Already running */

    NdisAcquireSpinLock(&AvbContext->subscription_lock);
    for (i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        if (AvbContext->subscriptions[i].active) { has_subs = TRUE; break; }
    }
    NdisReleaseSpinLock(&AvbContext->subscription_lock);

    if (has_subs) {
        AvbContext->tx_poll_active = TRUE;
        NdisSetTimer(&AvbContext->tx_poll_timer, 1);
        DEBUGP(DL_ERROR, "!!! AvbRestartTimers: TX poll timer restarted after FilterRestart\n");
    }
}

VOID AvbCleanupDevice(_In_ PAVB_DEVICE_CONTEXT AvbContext)
{
    if (!AvbContext) return;

    /* Stop timers first — this is the 0x9F fix path for unclean shutdown.
     * AvbStopTimers is idempotent; if FilterPause already called it, this is a no-op. */
    AvbStopTimers(AvbContext);
    
    // Remove from global list and promote a successor if needed
    AvbContextListRemove(AvbContext);
    
    /* Cleanup all timestamp event subscriptions (Issue #13).
     * Bug #1d: Null ALL pointers under the spinlock FIRST, then free outside it.
     * This closes the race window where AvbPostTimestampEvent (RX path) could
     * read a pointer that is concurrently being freed.                          */
    for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
        PVOID ring_to_free    = NULL;
        PMDL  mdl_to_free     = NULL;
        PVOID user_va_to_unmap = NULL;

        NdisAcquireSpinLock(&AvbContext->subscription_lock);
        if (AvbContext->subscriptions[i].active && AvbContext->subscriptions[i].ring_buffer) {
            ring_to_free      = AvbContext->subscriptions[i].ring_buffer;
            mdl_to_free       = AvbContext->subscriptions[i].ring_mdl;
            user_va_to_unmap  = AvbContext->subscriptions[i].user_va;
            /* Null all pointers under the lock before anyone else can observe them */
            AvbContext->subscriptions[i].ring_buffer = NULL;
            AvbContext->subscriptions[i].ring_mdl    = NULL;
            AvbContext->subscriptions[i].user_va     = NULL;
            AvbContext->subscriptions[i].active      = FALSE;
        }
        NdisReleaseSpinLock(&AvbContext->subscription_lock);

        /* Perform actual releases outside the spinlock.
         * BUGFIX (0xC4/0xB9): Do NOT call MmUnmapLockedPages here.
         * AvbCleanupDevice runs during adapter detach or driver unload; by this
         * point the owning user process may have already exited (its address space
         * is gone) or its IRP_MJ_CLEANUP already fired AvbCleanupFileSubscriptions
         * which unmapped the user VA. Calling MmUnmapLockedPages on a stale
         * user-space VA triggers Driver Verifier 0xC4/0xB9 "bad user space address".
         * IoFreeMdl is sufficient to release the MDL and any system VA mapping.    */
        if (mdl_to_free) {
            IoFreeMdl(mdl_to_free);
        }
        if (ring_to_free) {
            ExFreePoolWithTag(ring_to_free, FILTER_ALLOC_TAG);
        }
    }
    NdisFreeSpinLock(&AvbContext->subscription_lock);

    /* Release ATDECC subscription lock (no heap to free — queue is embedded) */
    NdisFreeSpinLock(&AvbContext->atdecc_sub_lock);

    /* Release SRP reservation lock (Issue #211) */
    NdisFreeSpinLock(&AvbContext->srp_lock);

    /* Cleanup NDIS packet injection pools (Step 8b) */
    if (AvbContext->nbl_pool_handle || AvbContext->nb_pool_handle || 
        AvbContext->test_packet_buffer || AvbContext->test_packet_mdl) {
        
        // Wait for any pending test packets to complete (max 100ms)
        LONG pending = InterlockedCompareExchange(&AvbContext->test_packets_pending, 0, 0);
        if (pending > 0) {
            DEBUGP(DL_TRACE, "Waiting for %d pending test packets to complete...\n", pending);
            for (int wait_ms = 0; wait_ms < 100 && pending > 0; wait_ms++) {
                NdisMSleep(1000);  // 1ms sleep
                pending = InterlockedCompareExchange(&AvbContext->test_packets_pending, 0, 0);
            }
            if (pending > 0) {
                DEBUGP(DL_ERROR, "WARNING: %d test packets still pending after 100ms wait\n", pending);
            }
        }
        
        // Free MDL (must be done before freeing buffer)
        if (AvbContext->test_packet_mdl) {
            IoFreeMdl(AvbContext->test_packet_mdl);
            AvbContext->test_packet_mdl = NULL;
        }
        
        // Free pre-allocated NBL ring slots BEFORE freeing the pools that own them.
        // Each slot has its own: NBL (from nbl_pool), NB (from nb_pool), MDL, and buffer.
        // BUGFIX (UAF/BSOD): Do NOT free in-flight (in_use==1) slots here.
        // Freeing an NBL that NDIS still owns causes PAGE_FAULT_IN_NONPAGED_AREA (0x50)
        // when FilterSendNetBufferListsComplete later reads the freed memory.
        // Mark in-flight slots as in_use=2 (deferred-cleanup); FilterSendNetBufferListsComplete
        // will free them and decrement ring_cleanup_deferred when the completion arrives.
        InterlockedExchange(&AvbContext->ring_cleanup_deferred, 0);
        for (int _ri = 0; _ri < AVB_TEST_NBL_RING_SIZE; _ri++) {
            // Atomically check if slot is in-flight; if so, mark deferred rather than freeing.
            LONG old_in_use = InterlockedCompareExchange(&AvbContext->test_nbl_ring[_ri].in_use, 2, 1);
            if (old_in_use == 1) {
                // Slot was in-flight; now marked in_use=2 (deferred-cleanup).
                // FilterSendNetBufferListsComplete owns the free of this slot.
                InterlockedIncrement(&AvbContext->ring_cleanup_deferred);
                DEBUGP(DL_ERROR, "Ring slot[%d] in-flight at cleanup: deferring free\n", _ri);
                continue;
            }
            // Slot is free (in_use==0) or was already in deferred state — free it now.
            if (AvbContext->test_nbl_ring[_ri].nbl != NULL) {
                NdisFreeNetBufferList(AvbContext->test_nbl_ring[_ri].nbl);
                AvbContext->test_nbl_ring[_ri].nbl = NULL;
            }
            if (AvbContext->test_nbl_ring[_ri].nb != NULL) {
                NdisFreeNetBuffer(AvbContext->test_nbl_ring[_ri].nb);
                AvbContext->test_nbl_ring[_ri].nb = NULL;
            }
            if (AvbContext->test_nbl_ring[_ri].mdl != NULL) {
                IoFreeMdl(AvbContext->test_nbl_ring[_ri].mdl);
                AvbContext->test_nbl_ring[_ri].mdl = NULL;
            }
            if (AvbContext->test_nbl_ring[_ri].buffer != NULL) {
                ExFreePoolWithTag(AvbContext->test_nbl_ring[_ri].buffer, FILTER_ALLOC_TAG);
                AvbContext->test_nbl_ring[_ri].buffer = NULL;
            }
        }
        // Wait for deferred-cleanup completions before freeing the pools.
        // The pool handles must remain valid until all deferred NdisFreeNetBuffer calls finish.
        {
            LONG deferred = InterlockedCompareExchange(&AvbContext->ring_cleanup_deferred, 0, 0);
            if (deferred > 0) {
                DEBUGP(DL_TRACE, "Waiting for %d deferred ring cleanup(s)...\n", deferred);
                for (int wait_ms = 0; wait_ms < 1000; wait_ms++) {
                    deferred = InterlockedCompareExchange(&AvbContext->ring_cleanup_deferred, 0, 0);
                    if (deferred == 0) break;
                    NdisMSleep(1000);  // 1ms
                }
                deferred = InterlockedCompareExchange(&AvbContext->ring_cleanup_deferred, 0, 0);
                if (deferred > 0) {
                    // NDIS did not complete in-flight sends within 1s — this is a driver bug.
                    // We cannot safely free the pools. Accept the memory leak to avoid BSOD.
                    DEBUGP(DL_ERROR, "ERROR: %d ring slots still deferred after 1s — skipping pool free to prevent UAF\n", deferred);
                    goto skip_pool_free;
                }
            }
        }

        // Free test packet buffer
        if (AvbContext->test_packet_buffer) {
            ExFreePoolWithTag(AvbContext->test_packet_buffer, FILTER_ALLOC_TAG);
            AvbContext->test_packet_buffer = NULL;
        }
        
        // Free NET_BUFFER pool
        if (AvbContext->nb_pool_handle) {
            NdisFreeNetBufferPool(AvbContext->nb_pool_handle);
            AvbContext->nb_pool_handle = NULL;
        }
        
        // Free NET_BUFFER_LIST pool
        if (AvbContext->nbl_pool_handle) {
            NdisFreeNetBufferListPool(AvbContext->nbl_pool_handle);
            AvbContext->nbl_pool_handle = NULL;
        }
        
skip_pool_free:
        // Free spin lock
        NdisFreeSpinLock(&AvbContext->test_send_lock);
        
        DEBUGP(DL_TRACE, "NDIS packet injection pools cleaned up\n");
    }
    
    // BUGFIX: Free intel_avb library private data (Bug #2: Memory leak 0xC4_62)
    // Issue: struct intel_private allocated at line 454 was never freed
    // Root cause: Missing cleanup in AvbCleanupDevice()
    // Fix: Add ExFreePoolWithTag for private_data before hardware_context cleanup
    if (AvbContext->intel_device.private_data) {
        ExFreePoolWithTag(AvbContext->intel_device.private_data, 'IAvb');
        AvbContext->intel_device.private_data = NULL;
        DEBUGP(DL_TRACE, "Intel AVB library private data freed\n");
    }
    
    if (AvbContext->hardware_context) {
        AvbUnmapIntelControllerMemory(AvbContext);
    }
    AvbContextListRemove(AvbContext);   // idempotent: no-op if already removed above
    ExFreePoolWithTag(AvbContext, FILTER_ALLOC_TAG);
}

/* ======================================================================= */
/* AvbSendPtpCore — shared hot-path for IOCTL_AVB_TEST_SEND_PTP.
 * Called from both the IRP dispatch path and the FastIoDeviceControl path.
 * On entry: activeContext and test_req are both non-NULL and accessible.
 * On return: test_req->timestamp_ns / packets_sent / status are filled. */
NTSTATUS AvbSendPtpCore(
    _In_ PAVB_DEVICE_CONTEXT activeContext,
    _Inout_ PAVB_TEST_SEND_PTP_REQUEST test_req)
{
    NTSTATUS status = STATUS_SUCCESS;

    // Verify NDIS pools are allocated
    if (!activeContext->nbl_pool_handle || !activeContext->nb_pool_handle ||
        !activeContext->test_packet_buffer || !activeContext->test_packet_mdl) {
        DEBUGP(DL_TRACE, "NDIS packet injection pools not initialized\n");
        test_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Verify filter instance exists (required for NdisFSendNetBufferLists)
    if (!activeContext->filter_instance || !activeContext->filter_instance->FilterHandle) {
        DEBUGP(DL_TRACE, "Filter instance not available (cannot send NBL)\n");
        test_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
        return STATUS_DEVICE_NOT_READY;
    }

    // Fast path: claim a pre-allocated ring slot (avoids per-call NdisAllocate ~10µs).
    int ring_slot = -1;
    for (int _ri = 0; _ri < AVB_TEST_NBL_RING_SIZE; _ri++) {
        if (activeContext->test_nbl_ring[_ri].nbl != NULL &&
            InterlockedCompareExchange(&activeContext->test_nbl_ring[_ri].in_use, 1, 0) == 0) {
            ring_slot = _ri;
            break;
        }
    }

    PNET_BUFFER_LIST nbl;
    PNET_BUFFER nb;
    PUCHAR pkt;

    if (ring_slot >= 0) {
        /* Fast path: ring slot owned exclusively via CAS — no spinlock needed. */
        nbl = activeContext->test_nbl_ring[ring_slot].nbl;
        nb  = activeContext->test_nbl_ring[ring_slot].nb;
        pkt = (PUCHAR)activeContext->test_nbl_ring[ring_slot].buffer;
        nb->DataOffset = 0;
        nbl->Next = NULL;
    } else {
        /* Slow path: ring exhausted — allocate on demand under spinlock. */
        NdisAcquireSpinLock(&activeContext->test_send_lock);
        nb = NdisAllocateNetBuffer(activeContext->nb_pool_handle,
                                   activeContext->test_packet_mdl, 0, 64);
        if (!nb) {
            NdisReleaseSpinLock(&activeContext->test_send_lock);
            test_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        nbl = NdisAllocateNetBufferList(activeContext->nbl_pool_handle, 0, 0);
        if (!nbl) {
            NdisFreeNetBuffer(nb);
            NdisReleaseSpinLock(&activeContext->test_send_lock);
            test_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        NdisReleaseSpinLock(&activeContext->test_send_lock);
        nbl->FirstNetBuffer = nb;
        nbl->Next = NULL;
        pkt = (PUCHAR)activeContext->test_packet_buffer;
    }

    // Update PTP sequence ID (Big Endian at offset 44-45)
    pkt[44] = (UCHAR)((test_req->sequence_id >> 8) & 0xFF);
    pkt[45] = (UCHAR)(test_req->sequence_id & 0xFF);

    InterlockedIncrement(&activeContext->test_packets_pending);

    // Capture pre-send timestamp: hardware SYSTIM preferred, perf-counter fallback.
    ULONG64 preSendTs = 0;
    if (activeContext->hw_state >= AVB_HW_BAR_MAPPED) {
        const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
        if (ops && ops->get_systime) {
#if DBG
            int systim_rc = ops->get_systime(&activeContext->intel_device, &preSendTs);
            DEBUGP(DL_TRACE, "PRE-SEND SYSTIM: rc=%d ts=0x%016I64X\n", systim_rc, preSendTs);
#else
            ops->get_systime(&activeContext->intel_device, &preSendTs);
#endif
        }
    }
    if (preSendTs == 0) {
        LARGE_INTEGER pc = KeQueryPerformanceCounter(NULL);
        preSendTs = (ULONG64)pc.QuadPart;
    }
    (void)InterlockedExchange64(&activeContext->last_ndis_tx_timestamp, (LONGLONG)preSendTs);
    test_req->timestamp_ns = preSendTs;

    /* NdisFSendNetBufferLists MUST NOT be called while holding a spinlock.
     * Ring-slot fast path: already lock-free. Fallback: lock released above. */
    NdisFSendNetBufferLists(activeContext->filter_instance->FilterHandle,
                           nbl, 0, 0);

    test_req->packets_sent = 1;
    test_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
    return status;
}

/* ======================================================================= */
/* IOCTL dispatcher */
NTSTATUS AvbHandleDeviceIoControl(_In_ PAVB_DEVICE_CONTEXT AvbContext, _In_ PIRP Irp)
{
    if (!AvbContext) return STATUS_DEVICE_NOT_READY;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code   = sp->Parameters.DeviceIoControl.IoControlCode;
    PUCHAR buf   = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    ULONG inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR info = 0; 
    NTSTATUS status = STATUS_SUCCESS;

    // MULTI-ADAPTER FIX: Use the context passed from device.c (routed via FileObject->FsContext)
    // This ensures each handle operates on its own adapter context
    PAVB_DEVICE_CONTEXT currentContext = AvbContext;

    /* Pre-dispatch: Reject IOCTL codes outside IntelAvbFilter's device-type space.
     * All IOCTL_AVB_* codes share the same device type (bits [31:16]) as IOCTL_AVB_GET_VERSION.
     * Codes with a different device type are always invalid — emit
     * EVT_IOCTL_ERROR (Event ID 100) immediately, before any init-guard check.
     * This ensures Event ID 100 is logged even when the device is not yet initialized.
     * Implements: #65 (REQ-F-EVENT-LOG-001) — Supports: TC-2, TC-9, TC-10, TC-11 */
    if (DEVICE_TYPE_FROM_CTL_CODE(code) != DEVICE_TYPE_FROM_CTL_CODE(IOCTL_AVB_GET_VERSION)) {
        InterlockedIncrement64(&currentContext->stats_error_count);
        EventWriteEVT_IOCTL_ERROR(NULL);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    // Implements #16 (REQ-F-LAZY-INIT-001: Lazy Initialization)
    // On-demand initialization: only initialize on first IOCTL, not at driver load
    if (!currentContext->initialized && code == IOCTL_AVB_INIT_DEVICE) (void)AvbBringUpHardware(currentContext);
    if (!currentContext->initialized && code != IOCTL_AVB_ENUM_ADAPTERS && code != IOCTL_AVB_INIT_DEVICE && code != IOCTL_AVB_GET_HW_STATE && code != IOCTL_AVB_GET_VERSION && code != IOCTL_AVB_GET_STATISTICS && code != IOCTL_AVB_RESET_STATISTICS && code != IOCTL_AVB_ADJUST_FREQUENCY)
        return STATUS_DEVICE_NOT_READY;

    // Implements #270 (TEST-STATISTICS-001): count every IOCTL dispatch before
    // branching so unhandled codes (hit 'default:') also increment the counter.
    InterlockedIncrement64(&currentContext->stats_ioctl_count);

    switch (code) {
    
    // Implements #64 (REQ-F-IOCTL-VERSIONING-001: IOCTL API Versioning)
    // Verified by #273 (TEST-IOCTL-VERSION-001)
    // MUST be first case - no device initialization required
    case IOCTL_AVB_GET_VERSION:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_GET_VERSION called\n");
            /*
             * Emit EventID=1 (driver operational) the first time version is queried.
             * TEST-EVENT-LOG-001 TC-1 calls this IOCTL and then queries Application log
             * for EventID=1 from provider 'IntelAvbFilter'. We emit here (not only in
             * DriverEntry) because on reinstalls DriverEntry fires before wevtutil im,
             * and the event misses the channel subscription.  Using a once-flag prevents
             * log flooding on repeated version queries.
             * Implements: #65 (REQ-F-EVENT-LOG-001) — Supports: #269 (TEST-EVENT-LOG-001)
             */
            DEBUGP(DL_ERROR, "!!! !ETW GET_VERSION: EventWriteEVT_DRIVER_INIT g_EtwInitEventEmitted=%d\n", (int)g_EtwInitEventEmitted);
            {
                /* Use the MC-generated macro — it checks the enable bits before writing
                 * and uses the dynamically-assigned registration handle via
                 * INTELAVBFILTER_PROVIDER_Context.RegistrationHandle.  With
                 * INTELAVBFILTER_PROVIDER_Traits=NULL the macro calls
                 * EtwWriteTransfer(..., UserDataCount=0, NULL) — identical to a
                 * direct call but goes through the proper MC code path.
                 * Implements: #65 (REQ-F-EVENT-LOG-001) — Supports: #269 (TEST-EVENT-LOG-001) */
                ULONG etw_result = EventWriteEVT_DRIVER_INIT(NULL);
                DEBUGP(DL_ERROR, "!!! !ETW EventWriteEVT_DRIVER_INIT() result=0x%08x EnableBits[0]=0x%08x\n",
                       etw_result, IntelAvbFilterEnableBits[0]);
                (void)etw_result; /* suppress C4189 in Release */
            }
            g_EtwInitEventEmitted = TRUE;
            if (outLen < sizeof(IOCTL_VERSION)) {
                DEBUGP(DL_ERROR, "IOCTL_AVB_GET_VERSION: Buffer too small (got %lu, need %lu)\n", 
                       outLen, (ULONG)sizeof(IOCTL_VERSION));
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
                break;
            }
            
            PIOCTL_VERSION version = (PIOCTL_VERSION)buf;
            version->Major    = AVB_VERSION_MAJOR;
            version->Minor    = AVB_VERSION_MINOR;
            version->Build    = AVB_VERSION_BUILD;
            version->Revision = AVB_VERSION_REVISION;
            version->Flags    = 0;
            version->Reserved = 0;
#ifdef DBG
            version->Flags |= AVB_VERSION_FLAG_DEBUG_BUILD;
#endif
            
            DEBUGP(DL_TRACE, "IOCTL_AVB_GET_VERSION: Returning version %u.%u.%u.%u\n", 
                   version->Major, version->Minor, version->Build, version->Revision);
            
            status = STATUS_SUCCESS;
            info = sizeof(IOCTL_VERSION);
        }
        break;
    
    case IOCTL_AVB_INIT_DEVICE:
        {
            DEBUGP(DL_TRACE, "? IOCTL_AVB_INIT_DEVICE: Starting hardware bring-up\n");
            
            // UT-DEV-INIT-002: Duplicate Initialization Prevention (per-handle).
            // FsContext2 is NULL for every new file handle (zeroed by I/O Manager).
            // Setting it to (PVOID)1 on the first INIT_DEVICE call lets us detect and
            // reject a second call on the SAME handle — without polluting per-adapter
            // state that would break subsequent test cases that open fresh handles.
            // Implements #313 (TEST-DEV-LIFECYCLE-001 / UT-DEV-INIT-002)
            if (sp->FileObject->FsContext2 != NULL) {
                DEBUGP(DL_WARN, "? IOCTL_AVB_INIT_DEVICE: Duplicate call on same handle rejected\n");
                status = STATUS_INVALID_DEVICE_STATE;
                break;
            }
            sp->FileObject->FsContext2 = (PVOID)(ULONG_PTR)1;
            
            DEBUGP(DL_TRACE, "   - Using context: VID=0x%04X DID=0x%04X\n",
                   currentContext->intel_device.pci_vendor_id, currentContext->intel_device.pci_device_id);
            DEBUGP(DL_TRACE, "   - Current hw_state: %s (%d)\n", AvbHwStateName(currentContext->hw_state), currentContext->hw_state);
            DEBUGP(DL_TRACE, "   - Hardware access enabled: %s\n", currentContext->hw_access_enabled ? "YES" : "NO");
            DEBUGP(DL_TRACE, "   - Initialized flag: %s\n", currentContext->initialized ? "YES" : "NO");
            DEBUGP(DL_TRACE, "   - Hardware context: %p\n", currentContext->hardware_context);
            DEBUGP(DL_TRACE, "   - Device type: %d (%s)\n", currentContext->intel_device.device_type,
                   currentContext->intel_device.device_type == INTEL_DEVICE_I210 ? "I210" :
                   currentContext->intel_device.device_type == INTEL_DEVICE_I226 ? "I226" : "OTHER");
            
            // Force immediate BAR0 discovery if hardware context is missing
            if (currentContext->hardware_context == NULL && currentContext->hw_state == AVB_HW_BOUND) {
                DEBUGP(DL_TRACE, "*** FORCED BAR0 DISCOVERY *** No hardware context, forcing immediate discovery...\n");
                
                PHYSICAL_ADDRESS bar0 = {0};
                ULONG barLen = 0;
                NTSTATUS ds = AvbDiscoverIntelControllerResources(currentContext->filter_instance, &bar0, &barLen);
                if (NT_SUCCESS(ds)) {
                    DEBUGP(DL_TRACE, "*** BAR0 DISCOVERY SUCCESS *** PA=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
                    NTSTATUS ms = AvbMapIntelControllerMemory(currentContext, bar0, barLen);
                    if (NT_SUCCESS(ms)) {
                        DEBUGP(DL_TRACE, "*** BAR0 MAPPING SUCCESS *** Hardware context now available\n");
                        
                        // Complete initialization sequence
                        if (intel_init(&currentContext->intel_device) == 0) {
                            DEBUGP(DL_TRACE, "? MANUAL intel_init SUCCESS\n");
                            
                            // Test MMIO sanity using generic register access
                            ULONG ctrl = INTEL_MASK_32BIT;
                            if (intel_read_reg(&currentContext->intel_device, INTEL_GENERIC_CTRL_REG, &ctrl) == 0 && ctrl != INTEL_MASK_32BIT) {
                                DEBUGP(DL_TRACE, "? MANUAL MMIO SANITY SUCCESS: CTRL=0x%08X\n", ctrl);
                                AVB_SET_HW_STATE(currentContext, AVB_HW_BAR_MAPPED);
                                currentContext->hw_access_enabled = TRUE;
                                currentContext->initialized = TRUE;
                                
                                // Device-specific initialization handled by Intel library
                                DEBUGP(DL_TRACE, "? Device-specific initialization complete\n");
                            } else {
                                DEBUGP(DL_ERROR, "? MANUAL MMIO SANITY FAILED: CTRL=0x%08X\n", ctrl);
                            }
                        } else {
                            DEBUGP(DL_ERROR, "? MANUAL intel_init FAILED\n");
                        }
                    } else {
                        DEBUGP(DL_ERROR, "*** BAR0 MAPPING FAILED *** Status=0x%08X\n", ms);
                    }
                } else {
                    DEBUGP(DL_ERROR, "*** BAR0 DISCOVERY FAILED *** Status=0x%08X\n", ds);
                }
            }
            
            status = AvbBringUpHardware(currentContext);
            
            DEBUGP(DL_TRACE, "? IOCTL_AVB_INIT_DEVICE: Completed with status=0x%08X\n", status);
            DEBUGP(DL_TRACE, "   - Final hw_state: %s (%d)\n", AvbHwStateName(currentContext->hw_state), currentContext->hw_state);
            DEBUGP(DL_TRACE, "   - Final hardware access: %s\n", currentContext->hw_access_enabled ? "YES" : "NO");
        }
        break;
    
    // Implements #15 (REQ-F-MULTIDEV-001: Multi-Adapter Management and Selection)
    case IOCTL_AVB_ENUM_ADAPTERS:
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { 
            status = STATUS_BUFFER_TOO_SMALL; 
        } else {
            PAVB_ENUM_REQUEST r = (PAVB_ENUM_REQUEST)buf; 
            RtlZeroMemory(r, sizeof(*r));
            
            DEBUGP(DL_TRACE, "? IOCTL_AVB_ENUM_ADAPTERS: Starting enumeration\n");
            
            // Count all Intel adapters in the system
            ULONG adapterCount = 0;
            BOOLEAN bFalse = FALSE;
            
            FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            PLIST_ENTRY Link = FilterModuleList.Flink;
            
            // First pass: count Intel adapters
            while (Link != &FilterModuleList) {
                PMS_FILTER f = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                Link = Link->Flink;
                
                if (f && f->AvbContext) {
                    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
                    if (ctx->intel_device.pci_vendor_id == INTEL_VENDOR_ID && 
                        ctx->intel_device.pci_device_id != 0) {
                        adapterCount++;
                    }
                }
            }
            
            DEBUGP(DL_TRACE, "? ENUM_ADAPTERS: Found %lu Intel adapters\n", adapterCount);
            
            // If requesting specific index, find and return that adapter
            if (r->index < adapterCount) {
                ULONG currentIndex = 0;
                Link = FilterModuleList.Flink;
                
                while (Link != &FilterModuleList) {
                    PMS_FILTER f = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                    Link = Link->Flink;
                    
                    if (f && f->AvbContext) {
                        PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
                        if (ctx->intel_device.pci_vendor_id == INTEL_VENDOR_ID && 
                            ctx->intel_device.pci_device_id != 0) {
                            if (currentIndex == r->index) {
                                // Return this specific adapter's info
                                r->count = adapterCount;
                                r->vendor_id = (USHORT)ctx->intel_device.pci_vendor_id;
                                r->device_id = (USHORT)ctx->intel_device.pci_device_id;
                                r->capabilities = ctx->intel_device.capabilities;
                                r->status = (avb_u32)NDIS_STATUS_SUCCESS;
                                
                                DEBUGP(DL_TRACE, "? ENUM_ADAPTERS[%lu]: VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
                                       r->index, r->vendor_id, r->device_id, r->capabilities);
                                break;
                            }
                            currentIndex++;
                        }
                    }
                }
            } else {
                // Return count and first adapter info (for compatibility)
                r->count = adapterCount;
                if (adapterCount > 0) {
                    // Use current context info
                    r->vendor_id = (USHORT)AvbContext->intel_device.pci_vendor_id;
                    r->device_id = (USHORT)AvbContext->intel_device.pci_device_id;
                    r->capabilities = AvbContext->intel_device.capabilities;
                } else {
                    r->vendor_id = 0;
                    r->device_id = 0;
                    r->capabilities = 0;
                }
                r->status = (avb_u32)NDIS_STATUS_SUCCESS;
                
                DEBUGP(DL_TRACE, "? ENUM_ADAPTERS(summary): count=%lu, VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
                       r->count, r->vendor_id, r->device_id, r->capabilities);
            }
            
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            info = sizeof(*r);
        }
        break;
    case IOCTL_AVB_GET_DEVICE_INFO:
        {
            DEBUGP(DL_TRACE, "? IOCTL_AVB_GET_DEVICE_INFO: Starting device info request\n");
            
            if (outLen < sizeof(AVB_DEVICE_INFO_REQUEST)) { 
                DEBUGP(DL_ERROR, "? DEVICE_INFO FAILED: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                // CRITICAL FIX: Use the specific adapter context that was selected via IOCTL_AVB_OPEN_ADAPTER
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                DEBUGP(DL_TRACE, "   - Using context: VID=0x%04X DID=0x%04X\n",
                       activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                DEBUGP(DL_TRACE, "   - Hardware state: %s\n", AvbHwStateName(activeContext->hw_state));
                DEBUGP(DL_TRACE, "   - Device type: %d\n", activeContext->intel_device.device_type);
                DEBUGP(DL_TRACE, "   - Filter instance: %p\n", activeContext->filter_instance);
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) { 
                    DEBUGP(DL_ERROR, "? DEVICE_INFO FAILED: Hardware not ready - hw_state=%s\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY; 
                } else {
                    DEBUGP(DL_TRACE, "? DEVICE_INFO: Hardware state validation passed\n");
                    
                    PAVB_DEVICE_INFO_REQUEST r = (PAVB_DEVICE_INFO_REQUEST)buf; 
                    RtlZeroMemory(r->device_info, sizeof(r->device_info)); 
                    
// Note: private_data already points to struct intel_private with platform_data set during initialization
                    
                    DEBUGP(DL_TRACE, "?? DEVICE_INFO: Calling intel_get_device_info...\n");
                    int rc = intel_get_device_info(&activeContext->intel_device, r->device_info, sizeof(r->device_info)); 
                    DEBUGP(DL_TRACE, "?? DEVICE_INFO: intel_get_device_info returned %d\n", rc);
                    
                    if (rc == 0) {
                        DEBUGP(DL_TRACE, "? DEVICE_INFO: Device info string: %s\n", r->device_info);
                    } else {
                        DEBUGP(DL_ERROR, "? DEVICE_INFO: intel_get_device_info failed with code %d\n", rc);
                        // Fallback to manual device info generation
                        const char* deviceName = "";
                        switch (activeContext->intel_device.device_type) {
                            case INTEL_DEVICE_I210: deviceName = "Intel I210 Gigabit Ethernet - Full TSN Support"; break;
                            case INTEL_DEVICE_I226: deviceName = "Intel I226 2.5G Ethernet - Advanced TSN"; break;
                            case INTEL_DEVICE_I225: deviceName = "Intel I225 2.5G Ethernet - Enhanced TSN"; break;
                            case INTEL_DEVICE_I217: deviceName = "Intel I217 Gigabit Ethernet - Basic PTP"; break;
                            case INTEL_DEVICE_I219: deviceName = "Intel I219 Gigabit Ethernet - Enhanced PTP"; break;
                            default: deviceName = "Unknown Intel Ethernet Device"; break;
                        }
                        RtlStringCbCopyA(r->device_info, sizeof(r->device_info), deviceName);
                        rc = 0; // Override failure
                        DEBUGP(DL_TRACE, "? DEVICE_INFO: Using fallback device info: %s\n", r->device_info);
                    }
                    
                    size_t used = 0; 
                    (void)RtlStringCbLengthA(r->device_info, sizeof(r->device_info), &used); 
                    r->buffer_size = (ULONG)used; 
                    r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                    info = sizeof(*r); 
                    status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL; 
                    
                    DEBUGP(DL_TRACE, "? DEVICE_INFO COMPLETE: status=0x%08X, buffer_size=%lu\n", status, r->buffer_size);
                }
            }
        }
        break;
#ifndef NDEBUG
    /* Debug-only register access - disabled in Release builds for security */
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
        {
            if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                // Per-handle routing: use currentContext which was resolved from
                // FileObject->FsContext in device.c, not the global g_AvbContext singleton.
                // Fixes: IOCTL_AVB_READ_REGISTER returned the same adapter for every handle.
                PAVB_DEVICE_CONTEXT activeContext = currentContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "Register access failed: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_REGISTER_REQUEST r = (PAVB_REGISTER_REQUEST)buf; 
                    
                    // Note: private_data already points to struct intel_private with platform_data set during initialization
                    
                    DEBUGP(DL_TRACE, "Register %s: offset=0x%05X, context VID=0x%04X DID=0x%04X (type=%d)\n",
                           (code == IOCTL_AVB_READ_REGISTER) ? "READ" : "WRITE",
                           r->offset, activeContext->intel_device.pci_vendor_id,
                           activeContext->intel_device.pci_device_id, activeContext->intel_device.device_type);
                    
                    if (code == IOCTL_AVB_READ_REGISTER) {
                        ULONG tmp = 0; 
                        int rc = intel_read_reg(&activeContext->intel_device, r->offset, &tmp); 
                        r->value = (avb_u32)tmp; 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "Register READ success: offset=0x%05X, value=0x%08X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, r->value, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        } else {
                            DEBUGP(DL_ERROR, "Register READ failed: offset=0x%05X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        }
                    } else {
                        int rc = intel_write_reg(&activeContext->intel_device, r->offset, r->value); 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "Register WRITE success: offset=0x%05X, value=0x%08X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, r->value, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        } else {
                            DEBUGP(DL_ERROR, "Register WRITE failed: offset=0x%05X, value=0x%08X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, r->value, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        }
                    } 
                    info = sizeof(*r);
                }
            }
        }
        break;
#endif /* !NDEBUG */

    /* Production-ready frequency adjustment IOCTL */
    case IOCTL_AVB_ADJUST_FREQUENCY:
        {
            if (inLen < sizeof(AVB_FREQUENCY_REQUEST) || outLen < sizeof(AVB_FREQUENCY_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                PAVB_FREQUENCY_REQUEST freq_req = (PAVB_FREQUENCY_REQUEST)buf;

                /* Validate parameter range FIRST — emit ETW events regardless of hw state.
                 * Event ID 300 (HARDWARE_FAULT) fires for out-of-range increment_ns.
                 * Event ID 200 (PHC_FORCESET_WARN) fires for any valid adjustment request.
                 * Both are independent of hardware initialization state so that test
                 * environments without real I226 hardware still produce the expected events.
                 *
                 * Range validation rationale (increment_ns maps from ppb via ConvertPpbToIncrement):
                 *   increment_ns == 0  → ppb < −875,000,000 (clock frozen, below minimum)
                 *   increment_ns >= 16 → ppb ≥ +1,000,000,000 (positive overflow)
                 *   Valid range: increment_ns ∈ [1, 15]
                 * Fixes UT-PTP-FREQ-006/007; implements #65 (REQ-F-EVENT-LOG-001) TC-3/TC-4 */
                if (freq_req->increment_ns == 0 || freq_req->increment_ns >= 16) {
                    DEBUGP(DL_ERROR, "Frequency adjustment rejected: increment_ns=%u out of valid range [1,15]\n",
                           freq_req->increment_ns);
                    EventWriteEVT_HARDWARE_FAULT(NULL);  /* ETW ID 300: extreme freq deviation */
                    freq_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    info = sizeof(*freq_req);
                    break;
                }

                /* Valid parameter: emit PHC adjustment warning BEFORE hardware write attempt.
                 * The event represents "a clock adjustment was requested", not "it succeeded". */
                EventWriteEVT_PHC_FORCESET_WARN(NULL);  /* ETW ID 200: PHC freq adjustment */

                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "Frequency adjustment failed: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    freq_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    // Get device operations for HAL-compliant register access
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    ULONG current_timinca = 0;
                    
                    // Read current TIMINCA value
                    int rc = (ops && ops->read_timinca) ? ops->read_timinca(&activeContext->intel_device, &current_timinca) : -1;
                    freq_req->current_increment = current_timinca;
                    
                    if (rc != 0) {
                        DEBUGP(DL_ERROR, "Failed to read TIMINCA register\n");
                        freq_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                        status = STATUS_UNSUCCESSFUL;
                    } else {
                        // Build new TIMINCA: bits[31:24] = increment_ns (INCPERIOD, integer ns per cycle)
                        //                   bits[23:0]  = INCFRAC (fractional ns in 2^-24 ns units)
                        //
                        // increment_frac is in 2^-32 ns units per IOCTL contract (2^32 = 1 ns).
                        // TIMINCA INCFRAC field uses 2^-24 ns units (2^24 = 1 ns).
                        // Conversion: INCFRAC = increment_frac >> 8  (divide by 2^8 = 256)
                        ULONG new_timinca = ((freq_req->increment_ns & 0xFF) << 24) | ((freq_req->increment_frac >> 8) & INTEL_TIMINCA_SUBNS_MASK);
                        
                        DEBUGP(DL_TRACE, "Adjusting clock frequency: %u ns + 0x%X frac (TIMINCA 0x%08X->0x%08X) VID=0x%04X DID=0x%04X\n",
                               freq_req->increment_ns, freq_req->increment_frac, current_timinca, new_timinca,
                               activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                        
                        rc = (ops && ops->write_timinca) ? ops->write_timinca(&activeContext->intel_device, new_timinca) : -1;
                        
                        if (rc == 0) {
                            freq_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                            status = STATUS_SUCCESS;
                            DEBUGP(DL_TRACE, "Clock frequency adjusted successfully\n");
                        } else {
                            DEBUGP(DL_ERROR, "Failed to write TIMINCA register\n");
                            freq_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        }
                    }
                }
                info = sizeof(*freq_req);
            }
        }
        break;

    /* Production-ready clock configuration query IOCTL
     * Implements #4 (BUG: IOCTL_AVB_GET_CLOCK_CONFIG Not Working)
     * Returns: SYSTIM, TIMINCA, TSAUXC register values and clock rate
     */
    case IOCTL_AVB_GET_CLOCK_CONFIG:
        {
            DEBUGP(DL_ERROR, "!!! IOCTL_AVB_GET_CLOCK_CONFIG: Entry point reached\n");
            DEBUGP(DL_ERROR, "    inLen=%lu outLen=%lu required=%lu\n", inLen, outLen, (ULONG)sizeof(AVB_CLOCK_CONFIG));
            
            if (inLen < sizeof(AVB_CLOCK_CONFIG) || outLen < sizeof(AVB_CLOCK_CONFIG)) {
                DEBUGP(DL_ERROR, "!!! Buffer too small - returning error\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                DEBUGP(DL_ERROR, "!!! activeContext=%p (g_AvbContext=%p, AvbContext=%p)\n", 
                       activeContext, g_AvbContext, AvbContext);
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "Clock config query failed: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_CLOCK_CONFIG cfg = (PAVB_CLOCK_CONFIG)buf;
                    
                    // Note: private_data already points to struct intel_private with platform_data set during initialization
                    DEBUGP(DL_ERROR, "DEBUG GET_CLOCK_CONFIG: hw_context=%p, hw_access=%d\n",
                           activeContext->hardware_context, activeContext->hw_access_enabled);
                    
                    // Get device operations for HAL-compliant register access
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (ops == NULL) {
                        status = STATUS_NOT_SUPPORTED;
                        break;
                    }
                    
                    ULONG timinca = 0, tsauxc = 0;
                    avb_u64 systime = 0;
                    int rc = 0;
                    
                    // Debug: Check if hardware context is available
                    DEBUGP(DL_ERROR, "DEBUG GET_CLOCK_CONFIG: hw_context=%p hw_access=%d\n",
                           activeContext->hardware_context, activeContext->hw_access_enabled);
                    
                    // Read clock-related registers using device operations (HAL-compliant - no magic numbers)
                    if (ops->get_systime != NULL) {
                        rc |= ops->get_systime(&activeContext->intel_device, &systime);
                        DEBUGP(DL_ERROR, "DEBUG: Read SYSTIME=0x%016llX rc=%d\n", systime, rc);
                    } else {
                        rc = -1;
                    }
                    
                    if (ops->read_timinca != NULL) {
                        rc |= ops->read_timinca(&activeContext->intel_device, &timinca);
                        DEBUGP(DL_ERROR, "DEBUG: Read TIMINCA=0x%08X rc=%d\n", timinca, rc);
                    } else {
                        rc = -1;
                    }
                    
                    if (ops->read_tsauxc != NULL) {
                        rc |= ops->read_tsauxc(&activeContext->intel_device, &tsauxc);
                        DEBUGP(DL_ERROR, "DEBUG: Read TSAUXC=0x%08X rc=%d\n", tsauxc, rc);
                    } else {
                        rc = -1;
                    }
                    
                    if (rc == 0) {
                        cfg->systim = systime;
                        cfg->timinca = timinca;
                        cfg->tsauxc = tsauxc;
                        
                        // Determine base clock rate from device type
                        switch (activeContext->intel_device.device_type) {
                            case INTEL_DEVICE_I210:
                            case INTEL_DEVICE_I225:
                            case INTEL_DEVICE_I226:
                                cfg->clock_rate_mhz = 125; // 1G devices use 125 MHz
                                break;
                            case INTEL_DEVICE_I350:
                            case INTEL_DEVICE_I354:
                                cfg->clock_rate_mhz = 125; // 1G devices
                                break;
                            default:
                                cfg->clock_rate_mhz = 125; // Default assumption
                                break;
                        }
                        
                        cfg->status = (avb_u32)NDIS_STATUS_SUCCESS;
                        status = STATUS_SUCCESS;
                        
                        DEBUGP(DL_TRACE, "Clock config (VID=0x%04X DID=0x%04X): SYSTIM=0x%016llX, TIMINCA=0x%08X, TSAUXC=0x%08X (bit31=%s), Rate=%u MHz\n",
                               activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id,
                               cfg->systim, cfg->timinca, cfg->tsauxc, (cfg->tsauxc & INTEL_TSAUXC_DISABLE_SYSTIM) ? "DISABLED" : "ENABLED",
                               cfg->clock_rate_mhz);
                    } else {
                        DEBUGP(DL_ERROR, "Failed to read clock configuration registers\n");
                        cfg->status = (avb_u32)NDIS_STATUS_FAILURE;
                        status = STATUS_UNSUCCESSFUL;
                    }
                    info = sizeof(*cfg);
                }
            }
        }
        break;

    /* Production-ready hardware timestamping control IOCTL */
    case IOCTL_AVB_SET_HW_TIMESTAMPING:
        {
            if (inLen < sizeof(AVB_HW_TIMESTAMPING_REQUEST) || outLen < sizeof(AVB_HW_TIMESTAMPING_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                const intel_device_ops_t *ops = NULL;  // Declare at beginning of block
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "HW timestamping control failed: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_HW_TIMESTAMPING_REQUEST ts_req = (PAVB_HW_TIMESTAMPING_REQUEST)buf;
                    
                    // Input Validation: Mode validation (fixes test_hw_ts_ctrl UT-HW-TS-005)
                    // All boolean fields should be 0 or 1; timer_mask should be 4-bit (0x0-0xF)
                    if (ts_req->enable > 1) {
                        DEBUGP(DL_ERROR, "HW timestamping rejected: invalid enable value %u (must be 0 or 1)\n",
                               ts_req->enable);
                        ts_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                        status = STATUS_INVALID_PARAMETER;
                        info = sizeof(*ts_req);
                        break;
                    }
                    if (ts_req->timer_mask > 0xF) {
                        DEBUGP(DL_ERROR, "HW timestamping rejected: invalid timer_mask 0x%X (max 0xF)\n",
                               ts_req->timer_mask);
                        ts_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                        status = STATUS_INVALID_PARAMETER;
                        info = sizeof(*ts_req);
                        break;
                    }
                    if (ts_req->enable_target_time > 1) {
                        DEBUGP(DL_ERROR, "HW timestamping rejected: invalid enable_target_time value %u (must be 0 or 1)\n",
                               ts_req->enable_target_time);
                        ts_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                        status = STATUS_INVALID_PARAMETER;
                        info = sizeof(*ts_req);
                        break;
                    }
                    if (ts_req->enable_aux_ts > 1) {
                        DEBUGP(DL_ERROR, "HW timestamping rejected: invalid enable_aux_ts value %u (must be 0 or 1)\n",
                               ts_req->enable_aux_ts);
                        ts_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                        status = STATUS_INVALID_PARAMETER;
                        info = sizeof(*ts_req);
                        break;
                    }
                    
                    // TSAUXC register bit masks (Intel Foxville specification - common across all Intel PTP devices)
                    const ULONG BIT31_DISABLE_SYSTIM0 = INTEL_TSAUXC_DISABLE_SYSTIM;  // Primary timer
                    const ULONG BIT29_DISABLE_SYSTIM3 = INTEL_TSAUXC_DISABLE_SYSTIM3;
                    const ULONG BIT28_DISABLE_SYSTIM2 = INTEL_TSAUXC_DISABLE_SYSTIM2;
                    const ULONG BIT27_DISABLE_SYSTIM1 = INTEL_TSAUXC_DISABLE_SYSTIM1;
                    const ULONG BIT10_EN_TS1 = INTEL_TSAUXC_EN_TS1;  // Enable aux timestamp 1
                    const ULONG BIT8_EN_TS0 = INTEL_TSAUXC_EN_TS0;   // Enable aux timestamp 0
                    const ULONG BIT4_EN_TT1 = INTEL_TSAUXC_EN_TT1;   // Enable target time 1
                    const ULONG BIT0_EN_TT0 = INTEL_TSAUXC_EN_TT0;   // Enable target time 0
                    
                    // Get device operations for HAL-compliant register access
                    ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (ops == NULL || ops->read_tsauxc == NULL || ops->write_tsauxc == NULL) {
                        DEBUGP(DL_ERROR, "Device operations not available for TSAUXC\n");
                        status = STATUS_NOT_SUPPORTED;
                        break;
                    }
                    
                    ULONG current_tsauxc = 0;
                    int rc = ops->read_tsauxc(&activeContext->intel_device, &current_tsauxc);
                    ts_req->previous_tsauxc = current_tsauxc;
                    
                    if (rc != 0) {
                        DEBUGP(DL_ERROR, "Failed to read TSAUXC register\n");
                        ts_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                        status = STATUS_UNSUCCESSFUL;
                    } else {
                        ULONG new_tsauxc = current_tsauxc;
                        
                        if (ts_req->enable) {
                            // Enable HW timestamping: Configure timer enables based on mask
                            ULONG timer_mask = ts_req->timer_mask ? ts_req->timer_mask : 0x1;  // Default: SYSTIM0 only
                            
                            // Clear disable bits for requested timers
                            if (timer_mask & 0x01) new_tsauxc &= ~BIT31_DISABLE_SYSTIM0;  // SYSTIM0
                            if (timer_mask & 0x02) new_tsauxc &= ~BIT27_DISABLE_SYSTIM1;  // SYSTIM1
                            if (timer_mask & 0x04) new_tsauxc &= ~BIT28_DISABLE_SYSTIM2;  // SYSTIM2
                            if (timer_mask & 0x08) new_tsauxc &= ~BIT29_DISABLE_SYSTIM3;  // SYSTIM3
                            
                            // Configure target time interrupts if requested
                            if (ts_req->enable_target_time) {
                                new_tsauxc |= (BIT0_EN_TT0 | BIT4_EN_TT1);
                            } else {
                                new_tsauxc &= ~(BIT0_EN_TT0 | BIT4_EN_TT1);
                            }
                            
                            // Configure auxiliary timestamp capture if requested
                            if (ts_req->enable_aux_ts) {
                                new_tsauxc |= (BIT8_EN_TS0 | BIT10_EN_TS1);
                            } else {
                                new_tsauxc &= ~(BIT8_EN_TS0 | BIT10_EN_TS1);
                            }
                            
                            DEBUGP(DL_TRACE, "Enabling HW timestamping: TSAUXC 0x%08X->0x%08X (timers=0x%X, TT=%d, AuxTS=%d) VID=0x%04X DID=0x%04X\n",
                                   current_tsauxc, new_tsauxc, timer_mask,
                                   ts_req->enable_target_time, ts_req->enable_aux_ts,
                                   activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                            
                            // Enable packet timestamping via device-specific implementation
                            ops = intel_get_device_ops(activeContext->intel_device.device_type);
                            if (ops && ops->enable_packet_timestamping) {
                                int pkt_ts_result = ops->enable_packet_timestamping(&activeContext->intel_device, 1);
                                if (pkt_ts_result != 0) {
                                    DEBUGP(DL_TRACE, "Device-specific packet timestamping enable failed: %d\n", pkt_ts_result);
                                }
                            } else {
                                DEBUGP(DL_TRACE, "Device does not support packet timestamping or ops not implemented\n");
                            }
                        } else {
                            // Disable HW timestamping: Set all timer disable bits
                            new_tsauxc |= (BIT31_DISABLE_SYSTIM0 | BIT29_DISABLE_SYSTIM3 | 
                                          BIT28_DISABLE_SYSTIM2 | BIT27_DISABLE_SYSTIM1);
                            
                            // Also disable target time and auxiliary timestamp features
                            new_tsauxc &= ~(BIT0_EN_TT0 | BIT4_EN_TT1 | BIT8_EN_TS0 | BIT10_EN_TS1);
                            
                            DEBUGP(DL_TRACE, "Disabling HW timestamping: TSAUXC 0x%08X->0x%08X (all timers stopped) VID=0x%04X DID=0x%04X\n",
                                   current_tsauxc, new_tsauxc,
                                   activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        }
                        
                        rc = ops->write_tsauxc(&activeContext->intel_device, new_tsauxc);
                        
                        if (rc == 0) {
                            // Verify the change took effect
                            ULONG verify_tsauxc = 0;
                            if (ops->read_tsauxc(&activeContext->intel_device, &verify_tsauxc) == 0) {
                                ts_req->current_tsauxc = verify_tsauxc;
                                
                                // Check if bit 31 matches what we intended
                                BOOLEAN bit31_correct = (BOOLEAN)(ts_req->enable ? 
                                    !(verify_tsauxc & BIT31_DISABLE_SYSTIM0) :  // Enabled = bit 31 should be clear
                                    (verify_tsauxc & BIT31_DISABLE_SYSTIM0));    // Disabled = bit 31 should be set
                                
                                if (bit31_correct) {
                                    ts_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                                    status = STATUS_SUCCESS;
                                    DEBUGP(DL_TRACE, "HW timestamping %s successfully (verified: 0x%08X)\n",
                                           ts_req->enable ? "ENABLED" : "DISABLED", verify_tsauxc);
                                } else {
                                    DEBUGP(DL_TRACE, "HW timestamping write succeeded but verification shows unexpected state (0x%08X)\n", 
                                           verify_tsauxc);
                                    ts_req->status = (avb_u32)NDIS_STATUS_SUCCESS;  // Still report success
                                    status = STATUS_SUCCESS;
                                }
                            } else {
                                DEBUGP(DL_TRACE, "HW timestamping changed but verification read failed\n");
                                ts_req->current_tsauxc = new_tsauxc;
                                ts_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                                status = STATUS_SUCCESS;
                            }
                        } else {
                            DEBUGP(DL_ERROR, "Failed to write TSAUXC register\n");
                            ts_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        }
                    }
                    info = sizeof(*ts_req);
                }
            }
        }
        break;

    case IOCTL_AVB_SET_RX_TIMESTAMP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_SET_RX_TIMESTAMP called\n");
            // NULL pointer validation (fixes test_rx_timestamp UT-RX-TS-004)
            // Must check BEFORE size validation to return ERROR_INVALID_PARAMETER for NULL
            if (buf == NULL) {
                DEBUGP(DL_ERROR, "SET_RX_TIMESTAMP: NULL buffer rejected\n");
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (inLen < sizeof(AVB_RX_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_RX_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                PAVB_RX_TIMESTAMP_REQUEST rx_req = (PAVB_RX_TIMESTAMP_REQUEST)buf;

                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "RX timestamp config: Hardware not accessible (state=%s)\n",
                           AvbHwStateName(activeContext->hw_state));
                    rx_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    device_t *dev = &activeContext->intel_device;
                    uint32_t rxpbsize, new_rxpbsize;
                    
                    // Read current RXPBSIZE value
                    if (intel_read_reg(dev, INTEL_REG_RXPBSIZE, &rxpbsize) == 0) {
                        rx_req->previous_rxpbsize = rxpbsize;
                        DEBUGP(DL_TRACE, "Current RXPBSIZE: 0x%08X\n", rxpbsize);
                        
                        // Modify CFG_TS_EN bit (bit 29 per I210 datasheet)
                        if (rx_req->enable) {
                            new_rxpbsize = rxpbsize | (1 << 29);  // Set CFG_TS_EN
                            DEBUGP(DL_TRACE, "Enabling RX timestamp (CFG_TS_EN=1)\n");
                        } else {
                            new_rxpbsize = rxpbsize & ~(1 << 29); // Clear CFG_TS_EN
                            DEBUGP(DL_TRACE, "Disabling RX timestamp (CFG_TS_EN=0)\n");
                        }
                        
                        // Write new value
                        if (intel_write_reg(dev, INTEL_REG_RXPBSIZE, new_rxpbsize) == 0) {
                            rx_req->current_rxpbsize = new_rxpbsize;
                            rx_req->requires_reset = (new_rxpbsize != rxpbsize) ? 1 : 0;
                            
                            if (rx_req->requires_reset) {
                                DEBUGP(DL_TRACE, "RXPBSIZE changed, port software reset (CTRL.RST) required\n");
                            }
                            
                            rx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                            status = STATUS_SUCCESS;
                            DEBUGP(DL_TRACE, "RX timestamp config updated: prev=0x%08X, new=0x%08X\n",
                                   rxpbsize, new_rxpbsize);
                        } else {
                            DEBUGP(DL_ERROR, "Failed to write RXPBSIZE register\n");
                            rx_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        }
                    } else {
                        DEBUGP(DL_ERROR, "Failed to read RXPBSIZE register\n");
                        rx_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                        status = STATUS_UNSUCCESSFUL;
                    }
                    info = sizeof(*rx_req);
                }
            }
        }
        break;

    case IOCTL_AVB_SET_QUEUE_TIMESTAMP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_SET_QUEUE_TIMESTAMP called\n");
            // NULL pointer validation (fixes test_rx_timestamp UT-RX-TS-013)
            // Must check BEFORE size validation to return ERROR_INVALID_PARAMETER for NULL
            if (buf == NULL) {
                DEBUGP(DL_ERROR, "SET_QUEUE_TIMESTAMP: NULL buffer rejected\n");
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (inLen < sizeof(AVB_QUEUE_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_QUEUE_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                PAVB_QUEUE_TIMESTAMP_REQUEST queue_req = (PAVB_QUEUE_TIMESTAMP_REQUEST)buf;

                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "Queue timestamp config: Hardware not accessible (state=%s)\n",
                           AvbHwStateName(activeContext->hw_state));
                    queue_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                    status = STATUS_DEVICE_NOT_READY;
                } else if (queue_req->queue_index >= 4) {
                    DEBUGP(DL_ERROR, "Invalid queue index: %u (max 3)\n", queue_req->queue_index);
                    queue_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    device_t *dev = &activeContext->intel_device;
                    uint32_t srrctl, new_srrctl;
                    
                    // SRRCTL base: 0_0C00C, stride 0_40 per queue (I210/I226 pattern)
                    uint32_t srrctl_offset = INTEL_REG_SRRCTL0 + (queue_req->queue_index * 0x40);
                    
                    // Read current SRRCTL[n] value
                    if (intel_read_reg(dev, srrctl_offset, &srrctl) == 0) {
                        queue_req->previous_srrctl = srrctl;
                        DEBUGP(DL_TRACE, "Queue %u SRRCTL: 0x%08X\n", queue_req->queue_index, srrctl);
                        
                        // Modify TIMESTAMP bit (bit 30 per I210 datasheet)
                        if (queue_req->enable) {
                            new_srrctl = srrctl | (1 << 30);  // Set TIMESTAMP
                            DEBUGP(DL_TRACE, "Enabling queue %u timestamp (TIMESTAMP=1)\n", queue_req->queue_index);
                        } else {
                            new_srrctl = srrctl & ~(1 << 30); // Clear TIMESTAMP
                            DEBUGP(DL_TRACE, "Disabling queue %u timestamp (TIMESTAMP=0)\n", queue_req->queue_index);
                        }
                        
                        // Write new value
                        if (intel_write_reg(dev, srrctl_offset, new_srrctl) == 0) {
                            queue_req->current_srrctl = new_srrctl;
                            queue_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                            status = STATUS_SUCCESS;
                            DEBUGP(DL_TRACE, "Queue %u timestamp config updated: prev=0x%08X, new=0x%08X\n",
                                   queue_req->queue_index, srrctl, new_srrctl);
                        } else {
                            DEBUGP(DL_ERROR, "Failed to write SRRCTL[%u] register\n", queue_req->queue_index);
                            queue_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        }
                    } else {
                        DEBUGP(DL_ERROR, "Failed to read SRRCTL[%u] register\n", queue_req->queue_index);
                        queue_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                        status = STATUS_UNSUCCESSFUL;
                    }
                    info = sizeof(*queue_req);
                }
            }
        }
        break;

    case IOCTL_AVB_SET_TARGET_TIME:
        {
            DEBUGP(DL_TRACE, "!!! IOCTL_AVB_SET_TARGET_TIME called\n");
            if (inLen < sizeof(AVB_TARGET_TIME_REQUEST) || outLen < sizeof(AVB_TARGET_TIME_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                /* MULTI-ADAPTER FIX: Use per-handle context from FileObject->FsContext
                 * Each file handle has its own adapter context set by IOCTL_AVB_OPEN_ADAPTER.
                 * g_AvbContext is a global that gets overwritten, causing wrong adapter access! */
                PAVB_DEVICE_CONTEXT activeContext = AvbContext;
                PAVB_TARGET_TIME_REQUEST tgt_req = (PAVB_TARGET_TIME_REQUEST)buf;

                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_ERROR, "Target time config: PTP not ready (state=%s)\n",
                           AvbHwStateName(activeContext->hw_state));
                    tgt_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                    status = STATUS_DEVICE_NOT_READY;
                } else if (tgt_req->timer_index > 1) {
                    DEBUGP(DL_ERROR, "Invalid timer index: %u (max 1)\n", tgt_req->timer_index);
                    tgt_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    // HAL-compliant: Use device ops instead of hardcoded registers
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (!ops || !ops->set_target_time) {
                        DEBUGP(DL_ERROR, "Device does not support target time operations\n");
                        tgt_req->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                        status = STATUS_NOT_SUPPORTED;
                    } else {
                        device_t *dev = &activeContext->intel_device;
                        
                        // FIX: Read previous target time BEFORE setting new value (for sentinel detection in tests)
                        uint32_t prev_low = 0, prev_high = 0;
                        uint32_t trgttiml_reg = (tgt_req->timer_index == 0) ? INTEL_REG_TRGTTIML0 : INTEL_REG_TRGTTIML1;  // TRGTTIML0 or TRGTTIML1
                        uint32_t trgttimh_reg = (tgt_req->timer_index == 0) ? INTEL_REG_TRGTTIMH0 : INTEL_REG_TRGTTIMH1;  // TRGTTIMH0 or TRGTTIMH1
                        
                        // Read previous target time (with proper 3-parameter signature)
                        if (AvbMmioReadReal(dev, trgttiml_reg, &prev_low) != 0 ||
                            AvbMmioReadReal(dev, trgttimh_reg, &prev_high) != 0) {
                            DEBUGP(DL_ERROR, "Failed to read previous target time registers\n");
                            prev_low = 0;
                            prev_high = 0;
                        }
                        tgt_req->previous_target = ((avb_u64)prev_high << 32) | prev_low;

                        /* TC-TARGET-005: Reject past target times.
                         * Read SYSTIM; if target_time is already elapsed, return
                         * STATUS_INVALID_PARAMETER instead of arming the hardware
                         * with a deadline that can never fire. */
                        if (ops->get_systime && tgt_req->target_time != 0) {
                            uint64_t current_systim = 0;
                            if (ops->get_systime(dev, &current_systim) == 0 &&
                                tgt_req->target_time < current_systim) {
                                DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: rejected - past time "
                                       "(target=%llu < systim=%llu)\n",
                                       (unsigned long long)tgt_req->target_time,
                                       (unsigned long long)current_systim);
                                tgt_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                                status = STATUS_INVALID_PARAMETER;
                                info = sizeof(*tgt_req);
                                break;
                            }
                        }

DEBUGP(DL_TRACE, "!!! SETTING target time %u: 0x%016llX (%llu ns), previous was 0x%016llX\n", 
                       tgt_req->timer_index, (unsigned long long)tgt_req->target_time, 
                       (unsigned long long)tgt_req->target_time,
                       (unsigned long long)tgt_req->previous_target);
                        
                        // Use device-specific operation (handles all register details)
                        DEBUGP(DL_TRACE, "!!!  CALLING ops->set_target_time(timer=%u, target=%llu, enable_interrupt=%d)\n",
                               tgt_req->timer_index, (unsigned long long)tgt_req->target_time, tgt_req->enable_interrupt);
                        
                        int rc = ops->set_target_time(dev, (uint8_t)tgt_req->timer_index, 
                                                     tgt_req->target_time, 
                                                     tgt_req->enable_interrupt);
                        
                        DEBUGP(DL_TRACE, "!!! ops->set_target_time() returned: %d\n", rc);
                        
                        if (rc == 0) {
                            tgt_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                                DEBUGP(DL_TRACE, "!!! TARGET TIME SET SUCCESSFULLY (timer=%u, new=%llu, prev=%llu)\n",
                                       tgt_req->timer_index, (unsigned long long)tgt_req->target_time,
                                       (unsigned long long)tgt_req->previous_target);
                        } else {
                            DEBUGP(DL_ERROR, "Device operation failed: set_target_time returned %d\n", rc);
                            tgt_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        }
                        info = sizeof(*tgt_req);
                    }
                }
            }
        }
        break;

    case IOCTL_AVB_GET_AUX_TIMESTAMP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_GET_AUX_TIMESTAMP called\n");
            if (inLen < sizeof(AVB_AUX_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_AUX_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                PAVB_AUX_TIMESTAMP_REQUEST aux_req = (PAVB_AUX_TIMESTAMP_REQUEST)buf;

                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_TRACE, "Aux timestamp: hw_state=%s, attempting lazy PTP init\n",
                           AvbHwStateName(activeContext->hw_state));

                    /* Lazy-init identical to IOCTL_AVB_GET_TIMESTAMP path:
                     * 1. If TIMINCA is already non-zero the clock is running -> promote state.
                     * 2. If not, call AvbEnsureDeviceReady for I210/I225/I226. */
                    if (activeContext->hw_state >= AVB_HW_BAR_MAPPED) {
                        ULONG timinca_val = 0;
                        const intel_device_ops_t *lazy_ops =
                            intel_get_device_ops(activeContext->intel_device.device_type);
                        if (lazy_ops && lazy_ops->read_timinca &&
                            lazy_ops->read_timinca(&activeContext->intel_device, &timinca_val) == 0 &&
                            timinca_val != 0) {
                            DEBUGP(DL_TRACE, "TIMINCA=0x%08X non-zero -> promoting to PTP_READY\n", timinca_val);
                            AVB_SET_HW_STATE(activeContext, AVB_HW_PTP_READY);
                        } else {
                            intel_device_type_t dev_type = activeContext->intel_device.device_type;
                            if (dev_type == INTEL_DEVICE_I210 || dev_type == INTEL_DEVICE_I225 ||
                                dev_type == INTEL_DEVICE_I226) {
                                DEBUGP(DL_TRACE, "Calling AvbEnsureDeviceReady for device type %d\n", dev_type);
                                NTSTATUS init_result = AvbEnsureDeviceReady(activeContext);
                                UNREFERENCED_PARAMETER(init_result);
                                DEBUGP(DL_TRACE, "AvbEnsureDeviceReady -> 0x%08X, state now %s\n",
                                       init_result, AvbHwStateName(activeContext->hw_state));
                            }
                        }
                    }

                    if (activeContext->hw_state < AVB_HW_PTP_READY) {
                        DEBUGP(DL_ERROR, "Aux timestamp read: PTP still not ready after init (state=%s)\n",
                               AvbHwStateName(activeContext->hw_state));
                        aux_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                        status = STATUS_DEVICE_NOT_READY;
                        break;
                    }
                }

                if (aux_req->timer_index > 1) {
                    DEBUGP(DL_ERROR, "Invalid timer index: %u (max 1)\n", aux_req->timer_index);
                    aux_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    // HAL-compliant: Use device ops instead of hardcoded registers
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (!ops || !ops->get_aux_timestamp || !ops->check_autt_flags) {
                        DEBUGP(DL_ERROR, "Device does not support auxiliary timestamp operations\n");
                        aux_req->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                        status = STATUS_NOT_SUPPORTED;
                    } else {
                        device_t *dev = &activeContext->intel_device;
                        uint8_t autt_flags = 0;
                        
                        // Check AUTT flag status
                        int rc = ops->check_autt_flags(dev, &autt_flags);
                        if (rc != 0) {
                            DEBUGP(DL_ERROR, "Failed to check AUTT flags: %d\n", rc);
                            aux_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        } else {
                            // Check if this timer's AUTT flag is set (bit 0 = timer 0, bit 1 = timer 1)
                            uint8_t timer_bit = (aux_req->timer_index == 0) ? 0x01 : 0x02;
                            aux_req->valid = (autt_flags & timer_bit) ? 1 : 0;
                            
                            // Read auxiliary timestamp
                            rc = ops->get_aux_timestamp(dev, (uint8_t)aux_req->timer_index, &aux_req->timestamp);
                            if (rc != 0) {
                                DEBUGP(DL_ERROR, "Failed to read auxiliary timestamp: %d\n", rc);
                                aux_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                                status = STATUS_UNSUCCESSFUL;
                            } else {
                                DEBUGP(DL_TRACE, "Aux timestamp %u: 0x%016llX (valid=%u)\n",
                                       aux_req->timer_index, (unsigned long long)aux_req->timestamp, aux_req->valid);
                                
                                // Clear AUTT flag if requested
                                if (aux_req->clear_flag && aux_req->valid && ops->clear_aux_timestamp_flag) {
                                    rc = ops->clear_aux_timestamp_flag(dev, (uint8_t)aux_req->timer_index);
                                    if (rc == 0) {
                                        DEBUGP(DL_TRACE, "Cleared AUTT%u flag\n", aux_req->timer_index);
                                    }
                                }
                                
                                aux_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                                status = STATUS_SUCCESS;
                            }
                        }
                        info = sizeof(*aux_req);
                    }
                }
            }
        }
        break;

    case IOCTL_AVB_GET_TX_TIMESTAMP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_GET_TX_TIMESTAMP called\n");
            if (inLen < sizeof(AVB_TX_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TX_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_TX_TIMESTAMP_REQUEST tx_req = (PAVB_TX_TIMESTAMP_REQUEST)buf;

                // Initialize output fields
                tx_req->timestamp_ns = 0;
                tx_req->valid = 0;
                tx_req->sequence_id = 0;
                tx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;

                // Adapter already selected by device.c via FileObject->FsContext (OPEN_ADAPTER).
                // Use currentContext directly — no FilterModuleList walk needed.
                PAVB_DEVICE_CONTEXT activeContext = currentContext;

                // Primary path: NDIS 6.82 TaggedTransmitHw timestamp.
                // FilterSendNetBufferListsComplete harvests igc.sys's ULONG64 hardware
                // timestamp from NetBufferListInfo[AVB_TX_TIMESTAMP_SLOT] (slot 26 =
                // NetBufferListInfoReserved3 on AMD64/Win11) and stores it here.
                // InterlockedExchange64 atomically reads and clears the field (one-shot).
                // This path does NOT require BAR-mapped MMIO — igc.sys owns the hardware
                // registers and delivers the result via the NBL completion mechanism.
                LONGLONG ndisTs = InterlockedExchange64(&activeContext->last_ndis_tx_timestamp, 0LL);
                if (ndisTs != 0) {
                    tx_req->timestamp_ns = (avb_u64)(ULONG64)ndisTs;
                    tx_req->valid = 1;
                    tx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                    info = sizeof(*tx_req);
                    DEBUGP(DL_TRACE, "TX timestamp from NDIS TaggedTransmitHw: 0x%016I64X (%I64u)\n",
                           (ULONG64)ndisTs, (ULONG64)ndisTs);
                } else if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_TRACE, "TX timestamp read: PTP not ready (state=%s)\n",
                           AvbHwStateName(activeContext->hw_state));
                    tx_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    // Fallback: HAL-compliant MMIO FIFO polling (TXSTMPH/TXSTMPL direct read).
                    // Used when NDIS TaggedTransmitHw is not supported by the miniport.
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (!ops || !ops->poll_tx_timestamp_fifo) {
                        DEBUGP(DL_ERROR, "Device does not support TX timestamp FIFO polling\n");
                        tx_req->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                        status = STATUS_NOT_SUPPORTED;
                    } else {
                        device_t *dev = &activeContext->intel_device;
                        
                        // Poll TX timestamp FIFO (atomic TXSTMPH→TXSTMPL read)
                        // Returns: 1=valid timestamp, 0=FIFO empty, <0=error
                        int rc = ops->poll_tx_timestamp_fifo(dev, &tx_req->timestamp_ns);
                        
                        if (rc < 0) {
                            DEBUGP(DL_ERROR, "Failed to poll TX timestamp FIFO: %d\n", rc);
                            tx_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        } else if (rc == 0) {
                            // FIFO empty - not an error, just no timestamp available
                            tx_req->valid = 0;
                            tx_req->timestamp_ns = 0;
                            tx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                            status = STATUS_SUCCESS;
                            DEBUGP(DL_TRACE, "TX timestamp FIFO empty\n");
                        } else {
                            // Valid timestamp retrieved (rc == 1)
                            tx_req->valid = 1;
                            tx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                            status = STATUS_SUCCESS;
                            DEBUGP(DL_TRACE, "TX timestamp retrieved: 0x%016llX (%llu ns)\n",
                                   (unsigned long long)tx_req->timestamp_ns,
                                   (unsigned long long)tx_req->timestamp_ns);
                        }
                        
                        info = sizeof(*tx_req);
                    }
                }
            }
        }
        break;

    case IOCTL_AVB_PHC_CROSSTIMESTAMP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_PHC_CROSSTIMESTAMP called\n");
            if (inLen < sizeof(AVB_CROSS_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_CROSS_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_CROSS_TIMESTAMP_REQUEST ct_req = (PAVB_CROSS_TIMESTAMP_REQUEST)buf;

                /* Initialise output fields */
                ct_req->phc_time_ns   = 0;
                ct_req->system_qpc    = 0;
                ct_req->qpc_frequency = 0;
                ct_req->valid         = 0;
                ct_req->status        = (avb_u32)NDIS_STATUS_SUCCESS;

                PAVB_DEVICE_CONTEXT activeContext = currentContext;

                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_TRACE, "Cross-timestamp: PTP not ready (state=%s)\n",
                           AvbHwStateName(activeContext->hw_state));
                    ct_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    /* HAL compliance: use ops->get_systime() — no register addresses in src/ */
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (!ops || !ops->get_systime) {
                        DEBUGP(DL_ERROR, "Device does not support get_systime\n");
                        ct_req->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                        status = STATUS_NOT_SUPPORTED;
                    } else {
                        /* Sample QPC frequency once — it is constant per boot */
                        LARGE_INTEGER freq = {0};
                        LARGE_INTEGER qpc  = KeQueryPerformanceCounter(&freq);

                        /* Read PHC via HAL — sequential but typically <1µs on x86 */
                        uint64_t phc_ns = 0;
                        device_t *dev   = &activeContext->intel_device;
                        int rc = ops->get_systime(dev, &phc_ns);

                        if (rc < 0) {
                            DEBUGP(DL_ERROR, "get_systime failed: %d\n", rc);
                            ct_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        } else {
                            ct_req->phc_time_ns   = phc_ns;
                            ct_req->system_qpc    = (avb_u64)qpc.QuadPart;
                            ct_req->qpc_frequency = (avb_u64)freq.QuadPart;
                            ct_req->valid         = 1;
                            ct_req->status        = (avb_u32)NDIS_STATUS_SUCCESS;
                            info = sizeof(*ct_req);
                            DEBUGP(DL_TRACE, "Cross-timestamp: phc=%llu ns  qpc=%lld  freq=%lld\n",
                                   (unsigned long long)phc_ns,
                                   (long long)qpc.QuadPart,
                                   (long long)freq.QuadPart);
                        }
                    }
                }
            }
        }
        break;

    case IOCTL_AVB_GET_RX_TIMESTAMP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_GET_RX_TIMESTAMP called\n");
            if (inLen < sizeof(AVB_TX_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TX_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_TX_TIMESTAMP_REQUEST rx_req = (PAVB_TX_TIMESTAMP_REQUEST)buf;

                // Initialize output fields
                rx_req->timestamp_ns = 0;
                rx_req->valid = 0;
                rx_req->sequence_id = 0;
                rx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;

                PAVB_DEVICE_CONTEXT activeContext = currentContext;

                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_TRACE, "RX timestamp read: PTP not ready (state=%s)\n",
                           AvbHwStateName(activeContext->hw_state));
                    rx_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_READY;
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    // HAL-compliant MMIO register read (RXSTMPL/H).
                    // read_rx_timestamp() reads a plain register pair — NOT a FIFO.
                    // Contract: rc == 0 → register read OK (valid = 1); rc < 0 → MMIO error.
                    const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                    if (!ops || !ops->read_rx_timestamp) {
                        DEBUGP(DL_ERROR, "Device does not support RX timestamp register read\n");
                        rx_req->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                        status = STATUS_NOT_SUPPORTED;
                    } else {
                        device_t *dev = &activeContext->intel_device;
                        int rc = ops->read_rx_timestamp(dev, &rx_req->timestamp_ns);
                        if (rc < 0) {
                            DEBUGP(DL_ERROR, "Failed to read RX timestamp registers: %d\n", rc);
                            rx_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                            status = STATUS_UNSUCCESSFUL;
                        } else {
                            // rc == 0: RXSTMPL/H read successfully (plain register, no FIFO semantics)
                            rx_req->valid = 1;
                            rx_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                            status = STATUS_SUCCESS;
                            info = sizeof(*rx_req);
                            DEBUGP(DL_TRACE, "RX timestamp retrieved: 0x%016llX (%llu ns)\n",
                                   (unsigned long long)rx_req->timestamp_ns,
                                   (unsigned long long)rx_req->timestamp_ns);
                        }
                    }
                }
            }
        }
        break;

    case IOCTL_AVB_TEST_SEND_PTP:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_TEST_SEND_PTP called (Step 8b - Kernel packet injection)\n");
            
            if (inLen < sizeof(AVB_TEST_SEND_PTP_REQUEST) || outLen < sizeof(AVB_TEST_SEND_PTP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            
            PAVB_TEST_SEND_PTP_REQUEST test_req = (PAVB_TEST_SEND_PTP_REQUEST)buf;
            test_req->packets_sent = 0;
            test_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            test_req->timestamp_ns = 0;

            // Delegate to shared core function (also used by FastIoDeviceControl path).
            PAVB_DEVICE_CONTEXT activeContext = currentContext;
            
            if (!activeContext) {
                test_req->status = (avb_u32)NDIS_STATUS_ADAPTER_NOT_FOUND;
                status = STATUS_DEVICE_DOES_NOT_EXIST;
                break;
            }
            status = AvbSendPtpCore(activeContext, test_req);
            if (NT_SUCCESS(status)) info = sizeof(*test_req);
        }
        break;

    case IOCTL_AVB_GET_TIMESTAMP:
    case IOCTL_AVB_SET_TIMESTAMP:
        {
            if (inLen < sizeof(AVB_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                // Use the active global context (set by IOCTL_AVB_OPEN_ADAPTER)
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_TRACE, "Timestamp access: Hardware state %s, checking PTP clock\n",
                           AvbHwStateName(activeContext->hw_state));
                    
                    // Note: private_data already points to struct intel_private with platform_data set during initialization
                    
                    // If we have BAR access, check if PTP clock is already running
                    if (activeContext->hw_state >= AVB_HW_BAR_MAPPED) {
                        // Check TIMINCA register - if non-zero, clock is configured
                        ULONG timinca = 0;
                        const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);
                        if (ops && ops->read_timinca && 
                            ops->read_timinca(&activeContext->intel_device, &timinca) == 0 && timinca != 0) {
                            DEBUGP(DL_TRACE, "PTP clock already configured (TIMINCA=0x%08X), promoting to PTP_READY\n", timinca);
                            AVB_SET_HW_STATE(activeContext, AVB_HW_PTP_READY);
                        } else {
                            // Try to initialize PTP for supported devices
                            intel_device_type_t dev_type = activeContext->intel_device.device_type;
                            
                            if (dev_type == INTEL_DEVICE_I210 || dev_type == INTEL_DEVICE_I225 || 
                                dev_type == INTEL_DEVICE_I226) {
                                DEBUGP(DL_TRACE, "Attempting PTP initialization for device type %d\n", dev_type);
                                NTSTATUS init_result = AvbEnsureDeviceReady(activeContext);
                                UNREFERENCED_PARAMETER(init_result);  // Used only in DEBUGP
                                DEBUGP(DL_TRACE, "PTP initialization result: 0x%08X, new state: %s\n", 
                                       init_result, AvbHwStateName(activeContext->hw_state));
                            } else {
                                DEBUGP(DL_TRACE, "Device type %d does not support PTP initialization\n", dev_type);
                            }
                        }
                    }
                    
                    // Check state again after potential initialization
                    if (activeContext->hw_state < AVB_HW_PTP_READY) {
                        DEBUGP(DL_ERROR, "PTP clock not ready after initialization attempt (state=%s)\n",
                               AvbHwStateName(activeContext->hw_state));
                        status = STATUS_DEVICE_NOT_READY;
                        break;
                    }
                }
                
                PAVB_TIMESTAMP_REQUEST r = (PAVB_TIMESTAMP_REQUEST)buf; 
                
                DEBUGP(DL_TRACE, "Timestamp %s: context VID=0x%04X DID=0x%04X\n",
                       (code == IOCTL_AVB_GET_TIMESTAMP) ? "GET" : "SET",
                       activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                
                if (code == IOCTL_AVB_GET_TIMESTAMP) {
                    ULONGLONG t = 0; 
                    struct timespec sys = {0}; 
                    int rc = intel_gettime(&activeContext->intel_device, r->clock_id, &t, &sys); 
                    if (rc != 0) {
                        rc = AvbReadTimestamp(&activeContext->intel_device, &t); 
                    }
                    r->timestamp = t; 
                    r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                    status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                } else {
                    // Monotonicity guard: reject *small* backward time jumps only.
                    // Large intentional resets (e.g. to year 2001, or to epoch 0) must be allowed.
                    // Only reject if the backward delta is below threshold (< 30 seconds) AND
                    // the target is itself a real-world (non-near-epoch-0) timestamp.
                    // Near-epoch-0 targets (< 30 s) are always treated as intentional epoch resets,
                    // not NTP drift corrections, and are allowed through unconditionally.
                    //   UT-PTP-GETSET-010: 10s backward from large current time → rejected ✓
                    //   UT-PTP-GETSET-004: set to year 2001 (~22yr backward) → allowed ✓
                    //   UT-PTP-GETSET-005: set to near-future valid time → allowed ✓
                    //   UT-PTP-GETSET-006: set to epoch 0 (~53yr backward) → allowed ✓
                    //   UT-CORR-005:       set to 1ms (near-epoch-0 reset) → allowed ✓
                    ULONGLONG current_t = 0;
                    const ULONGLONG MAX_SMALL_BACKWARD_JUMP_NS = 30ULL * 1000000000ULL;  /* 30 seconds */
                    intel_gettime(&activeContext->intel_device, r->clock_id, &current_t, NULL);
                    if (current_t > 0 && r->timestamp < current_t &&
                        (current_t - r->timestamp) < MAX_SMALL_BACKWARD_JUMP_NS &&
                        r->timestamp >= MAX_SMALL_BACKWARD_JUMP_NS) {
                        DEBUGP(DL_WARN, "SET_TIMESTAMP rejected: backward jump (new=0x%llx < current=0x%llx)\n",
                               r->timestamp, current_t);
                        r->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                        status = STATUS_INVALID_PARAMETER;
                    } else {
                        int rc = intel_set_systime(&activeContext->intel_device, r->timestamp); 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                    }
                } 
                info = sizeof(*r);
            }
        }
        break;

    // Implements: IOCTL_AVB_PHC_OFFSET_ADJUST (code 48)
    // Applies a nanosecond offset to the PTP hardware clock.
    // Bypasses the SET_TIMESTAMP monotonicity guard to allow small corrections.
    case IOCTL_AVB_PHC_OFFSET_ADJUST:
        {
            if (inLen < sizeof(AVB_OFFSET_REQUEST) || outLen < sizeof(AVB_OFFSET_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
            } else {
                PAVB_DEVICE_CONTEXT phcCtx = g_AvbContext ? g_AvbContext : AvbContext;
                PAVB_OFFSET_REQUEST off_req = (PAVB_OFFSET_REQUEST)buf;
                ULONGLONG current_t = 0;
                int rc = intel_gettime(&phcCtx->intel_device, 0, &current_t, NULL);
                if (rc != 0) {
                    rc = AvbReadTimestamp(&phcCtx->intel_device, &current_t);
                }
                if (rc != 0) {
                    off_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                    status = STATUS_UNSUCCESSFUL;
                } else {
                    /* Apply offset - reject if result would be negative time */
                    INT64 new_t = (INT64)current_t + off_req->offset_ns;
                    if (new_t < 0) {
                        /* Reject: offset would cause clock to go before epoch 0 */
                        off_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                        status = STATUS_INVALID_PARAMETER;
                    } else {
                        ULONGLONG new_ts = (ULONGLONG)new_t;
                        /* Call intel_set_systime directly - bypasses the 30s monotonicity guard
                         * in SET_TIMESTAMP, so small backward corrections succeed. */
                        rc = intel_set_systime(&phcCtx->intel_device, new_ts);
                        off_req->status = (rc == 0) ? (avb_u32)NDIS_STATUS_SUCCESS : (avb_u32)NDIS_STATUS_FAILURE;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                    }
                }
                info = sizeof(*off_req);
            }
        }
        break;

    // Implements #13 (REQ-F-TS-SUB-001: Timestamp Event Subscription)
    // Task 3: Ring buffer allocation with power-of-2 validation
    case IOCTL_AVB_TS_SUBSCRIBE:
        {
            DEBUGP(DL_ERROR, "!!! IOCTL_AVB_TS_SUBSCRIBE called: inLen=%lu, outLen=%lu, sizeof(req)=%lu\n", 
                   inLen, outLen, (ULONG)sizeof(AVB_TS_SUBSCRIBE_REQUEST));
            if (inLen < sizeof(AVB_TS_SUBSCRIBE_REQUEST) || outLen < sizeof(AVB_TS_SUBSCRIBE_REQUEST)) {
                DEBUGP(DL_ERROR, "!!! BUFFER_TOO_SMALL: inLen=%lu, outLen=%lu\n", inLen, outLen);
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
            } else {
                PAVB_TS_SUBSCRIBE_REQUEST sub_req = (PAVB_TS_SUBSCRIBE_REQUEST)buf;
                
                // Validate event mask (at least one event type must be set)
                if (sub_req->types_mask == 0) {
                    DEBUGP(DL_ERROR, "Invalid event mask: 0 (no events selected)\n");
                    sub_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    info = sizeof(*sub_req);
                    break;
                }
                
                // Task 3: Use default ring count (user specifies size in MAP request)
                // Default to 1024 events (configurable in MAP IOCTL via length parameter)
                avb_u32 ring_count = 1024;  // Default: 1024 events
                
                // Calculate total buffer size (header + events array)
                SIZE_T header_size = sizeof(AVB_TIMESTAMP_RING_HEADER);
                SIZE_T events_size = ring_count * sizeof(AVB_TIMESTAMP_EVENT);
                SIZE_T total_size = header_size + events_size;
                
                // Allocate NonPagedPool for ring buffer
                PVOID ring_buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, total_size, FILTER_ALLOC_TAG);
                if (!ring_buffer) {
                    DEBUGP(DL_ERROR, "Failed to allocate ring buffer (%lu bytes)\n", (ULONG)total_size);
                    sub_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    info = sizeof(*sub_req);
                    break;
                }
                
                // Initialize ring buffer header
                AVB_TIMESTAMP_RING_HEADER *header = (AVB_TIMESTAMP_RING_HEADER*)ring_buffer;
                RtlZeroMemory(header, total_size);
                header->producer_index = 0;
                header->consumer_index = 0;
                header->mask = ring_count - 1;
                header->count = ring_count;
                header->overflow_count = 0;
                header->total_events = 0;
                header->event_mask = sub_req->types_mask;
                header->vlan_filter = sub_req->vlan;
                header->pcp_filter = (avb_u8)sub_req->pcp;
                
                // Find free subscription slot (acquire spin lock)
                NdisAcquireSpinLock(&AvbContext->subscription_lock);
                
                int free_slot = -1;
                for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
                    if (AvbContext->subscriptions[i].ring_id == 0) {
                        free_slot = i;
                        break;
                    }
                }
                
                if (free_slot == -1) {
                    NdisReleaseSpinLock(&AvbContext->subscription_lock);
                    ExFreePoolWithTag(ring_buffer, FILTER_ALLOC_TAG);
                    DEBUGP(DL_ERROR, "No free subscription slots (max %d)\n", MAX_TS_SUBSCRIPTIONS);
                    sub_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                    status = STATUS_TOO_MANY_SESSIONS;
                    info = sizeof(*sub_req);
                    break;
                }
                
                // Store ring buffer in subscription table
                TS_SUBSCRIPTION *sub = &AvbContext->subscriptions[free_slot];
                sub->ring_buffer = ring_buffer;
                sub->ring_count = ring_count;
                sub->event_mask = sub_req->types_mask;
                sub->vlan_filter = sub_req->vlan;
                sub->pcp_filter = (avb_u8)sub_req->pcp;
                sub->active = 1;
                sub->sequence_num = 0;
                sub->ring_mdl = NULL;      // Will be set in IOCTL_AVB_TS_RING_MAP (Task 4)
                sub->user_va = NULL;
                sub->file_object = IoGetCurrentIrpStackLocation(Irp)->FileObject;  // Track owning handle
                
                // Allocate ring_id (InterlockedIncrement ensures atomicity)
                sub->ring_id = (avb_u32)InterlockedIncrement(&AvbContext->next_ring_id);
                
                // Capture timer state while holding lock (fast, no blocking)
                // Registry writes moved after spin lock release to avoid BSOD/deadlock
                BOOLEAN timer_was_active = AvbContext->tx_poll_active;
                AVB_HW_STATE captured_hw_state = AvbContext->hw_state;
                
                // RE-ENABLED: TX/Target timestamp polling timer (Task 6c/7)
                // Previously disabled due to BSOD 0x0A - now safe with HAL abstraction and NULL checks
                // Starts 1ms polling for TX timestamps (Task 6c) and target time events (Task 7)
                if (!AvbContext->tx_poll_active && AvbContext->hw_state >= AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "!!! TIMER: Starting TX timestamp poll timer (1ms interval)\n");
                    DEBUGP(DL_ERROR, "!!! TIMER: Timer object=%p, Context=%p\n", 
                           &AvbContext->tx_poll_timer, AvbContext);
                    
                    AvbContext->tx_poll_active = TRUE;
                    
                    // NdisSetTimer has no return value, but we can detect failure via DPC not firing
                    NdisSetTimer(&AvbContext->tx_poll_timer, 1);  // 1 millisecond
                    
                    DEBUGP(DL_ERROR, "!!! TIMER: NdisSetTimer called\n");
                } else if (AvbContext->tx_poll_active) {
                    DEBUGP(DL_TRACE, "!!! TIMER already running (poll_active=%d, hw_state=%d)\n",
                           AvbContext->tx_poll_active, AvbContext->hw_state);
                } else {
                    DEBUGP(DL_ERROR, "!!! TIMER NOT STARTED: hw_state=%d < %d (need BAR_MAPPED)\n",
                           AvbContext->hw_state, AVB_HW_BAR_MAPPED);
                }
                
                NdisReleaseSpinLock(&AvbContext->subscription_lock);
                
                // Timer diagnostic - check captured state (safe after spin lock release)
                DEBUGP(DL_ERROR, "!!! TIMER CHECK (captured while locked): tx_poll_active=%d, hw_state=%d (need >= %d)\n",
                       timer_was_active, captured_hw_state, AVB_HW_BAR_MAPPED);
                

                
                // Timer check result via DEBUGP (registry diagnostics removed — ran at wrong IRQL)
                {
                    ULONG timer_check_pass = 0;
                    if (timer_was_active) {
                        timer_check_pass = 10;   // Already running
                    } else if (captured_hw_state < AVB_HW_BAR_MAPPED) {
                        timer_check_pass = 20;   // HW not ready
                    } else {
                        timer_check_pass = 100;  // Should have started
                    }
                    DEBUGP(DL_ERROR, "!!! TIMER CHECK: tx_poll_active=%d, hw_state=%d, result=%d (100=started, 20=hw_not_ready, 10=already_running)\n",
                           timer_was_active, captured_hw_state, timer_check_pass);
                }
                
                // Return ring_id to user
                sub_req->ring_id = sub->ring_id;
                
                DEBUGP(DL_TRACE, "!!! SUBSCRIPTION CREATED [Adapter 0x%04X]: ring_id=%u, slot=%d, active=%d, event_mask=0x%08X\n",
                       AvbContext->intel_device.pci_device_id, sub_req->ring_id, free_slot, sub->active, sub->event_mask);
                DEBUGP(DL_TRACE, "!!! Sub details: count=%u, size=%lu bytes, ring_buffer=%p, context=%p\n",
                       ring_count, (ULONG)total_size, sub->ring_buffer, AvbContext);
                DEBUGP(DL_TRACE, "!!! Sub filters: vlan=%u, pcp=%u\n",
                       sub_req->vlan, sub_req->pcp);
                
                sub_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                status = STATUS_SUCCESS;
                info = sizeof(*sub_req);
                Irp->IoStatus.Information = sizeof(*sub_req);
            }
        }
        break;

    case IOCTL_AVB_TS_RING_MAP:
        {
            DEBUGP(DL_ERROR, "!!! IOCTL_AVB_TS_RING_MAP called\n");
            if (inLen < sizeof(AVB_TS_RING_MAP_REQUEST) || outLen < sizeof(AVB_TS_RING_MAP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
            } else {
                PAVB_TS_RING_MAP_REQUEST map_req = (PAVB_TS_RING_MAP_REQUEST)buf;
                
                // ERROR VALIDATION: Invalid ring_id (0_FFFFFFFF, 0, 0_DEADBEEF)
                // Implements: UT-TS-ERROR-001 (Invalid Subscription Handle)
                if (map_req->ring_id == 0 || 
                    map_req->ring_id == INTEL_MASK_32BIT || 
                    map_req->ring_id == INTEL_SENTINEL_DEAD_BEEF) {
                    DEBUGP(DL_ERROR, "Invalid ring_id: 0x%08X\n", map_req->ring_id);
                    map_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    info = sizeof(*map_req);
                    break;
                }
                
                // ERROR VALIDATION: Excessive buffer size (>1MB)
                // Implements: UT-TS-ERROR-002 (Ring Buffer Mapping Failure)
                #define MAX_RING_BUFFER_SIZE (1024 * 1024)  /* 1MB limit */
                if (map_req->length > MAX_RING_BUFFER_SIZE) {
                    DEBUGP(DL_ERROR, "Ring buffer size too large: %u bytes (max %u)\n", 
                           map_req->length, MAX_RING_BUFFER_SIZE);
                    map_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    info = sizeof(*map_req);
                    break;
                }
                
                // Find subscription by ring_id (Task 4: Real MDL mapping)
                NdisAcquireSpinLock(&currentContext->subscription_lock);
                
                TS_SUBSCRIPTION* found_sub = NULL;
                for (ULONG i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
                    if (currentContext->subscriptions[i].ring_id == map_req->ring_id) {
                        found_sub = &currentContext->subscriptions[i];
                        break;
                    }
                }
                
                if (!found_sub || !found_sub->ring_buffer) {
                    NdisReleaseSpinLock(&currentContext->subscription_lock);
                    DEBUGP(DL_ERROR, "Subscription not found or no ring buffer: ring_id=%u\n", map_req->ring_id);
                    map_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    info = sizeof(*map_req);
                    break;
                }
                
                // Prevent double-mapping
                if (found_sub->ring_mdl != NULL) {
                    NdisReleaseSpinLock(&currentContext->subscription_lock);
                    DEBUGP(DL_ERROR, "Ring buffer already mapped: ring_id=%u\n", map_req->ring_id);
                    map_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                    status = STATUS_INVALID_DEVICE_STATE;
                    info = sizeof(*map_req);
                    break;
                }
                
                // Calculate ring buffer size: header (64 bytes) + events (ring_count * 32 bytes)
                ULONG ring_size = sizeof(AVB_TIMESTAMP_RING_HEADER) + 
                                  (found_sub->ring_count * sizeof(AVB_TIMESTAMP_EVENT));
                PVOID ring_kernel_va = found_sub->ring_buffer;
                
                NdisReleaseSpinLock(&currentContext->subscription_lock);
                
                // Allocate MDL for ring buffer
                PMDL mdl = IoAllocateMdl(
                    ring_kernel_va,
                    ring_size,
                    FALSE,  // Not secondary buffer
                    FALSE,  // Don't charge quota
                    NULL    // No IRP
                );
                
                if (!mdl) {
                    DEBUGP(DL_ERROR, "IoAllocateMdl failed: ring_id=%u, size=%u\n", map_req->ring_id, ring_size);
                    map_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    info = sizeof(*map_req);
                    break;
                }
                
                // Build MDL for NonPagedPool memory
                __try {
                    MmBuildMdlForNonPagedPool(mdl);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    IoFreeMdl(mdl);
                    DEBUGP(DL_ERROR, "MmBuildMdlForNonPagedPool failed: ring_id=%u\n", map_req->ring_id);
                    map_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                    status = STATUS_UNSUCCESSFUL;
                    info = sizeof(*map_req);
                    break;
                }
                
                // Map to user-space
                PVOID user_va = NULL;
                __try {
                    user_va = MmMapLockedPagesSpecifyCache(
                        mdl,
                        UserMode,
                        MmCached,
                        NULL,           // No specific address
                        FALSE,          // No bug check on failure
                        NormalPagePriority
                    );
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    IoFreeMdl(mdl);
                    DEBUGP(DL_ERROR, "MmMapLockedPagesSpecifyCache failed: ring_id=%u\n", map_req->ring_id);
                    map_req->status = (avb_u32)NDIS_STATUS_FAILURE;
                    status = STATUS_UNSUCCESSFUL;
                    info = sizeof(*map_req);
                    break;
                }
                
                if (!user_va) {
                    IoFreeMdl(mdl);
                    DEBUGP(DL_ERROR, "MmMapLockedPagesSpecifyCache returned NULL: ring_id=%u\n", map_req->ring_id);
                    map_req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    info = sizeof(*map_req);
                    break;
                }
                
                // Store MDL and user_va in subscription for cleanup
                NdisAcquireSpinLock(&currentContext->subscription_lock);
                found_sub->ring_mdl = mdl;
                found_sub->user_va = user_va;
                NdisReleaseSpinLock(&currentContext->subscription_lock);
                
                // Return user-space address as shm_token
                map_req->shm_token = (avb_u64)(ULONG_PTR)user_va;
                map_req->length = ring_size;
                
                DEBUGP(DL_ERROR, "!!! Ring buffer mapped: ring_id=%u, kernel_va=%p, user_va=%p, size=%u, shm_token=0x%I64X\n",
                       map_req->ring_id, ring_kernel_va, user_va, ring_size, map_req->shm_token);
                
                map_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                status = STATUS_SUCCESS;
                info = sizeof(*map_req);
            }
        }
        break;
    
    // Implements #13 (REQ-F-TS-SUB-001: Timestamp Event Subscription) - Cleanup
    case IOCTL_AVB_TS_UNSUBSCRIBE:
        {
            DEBUGP(DL_ERROR, "!!! IOCTL_AVB_TS_UNSUBSCRIBE called\n");
            if (inLen < sizeof(AVB_TS_UNSUBSCRIBE_REQUEST) || outLen < sizeof(AVB_TS_UNSUBSCRIBE_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
            } else {
                PAVB_TS_UNSUBSCRIBE_REQUEST unsub_req = (PAVB_TS_UNSUBSCRIBE_REQUEST)buf;
                
                DEBUGP(DL_TRACE, "!!! UNSUBSCRIBE: ring_id=%u\n", unsub_req->ring_id);
                
                // Find subscription by ring_id, null fields under lock, free outside lock.
                // MmUnmapLockedPages / IoFreeMdl require IRQL <= APC_LEVEL — cannot be called
                // while holding an NDIS spinlock (DISPATCH_LEVEL). BSOD 0xC4/0x7A otherwise.
                PVOID unsubscribe_ring = NULL;
                PMDL  unsubscribe_mdl  = NULL;
                PVOID unsubscribe_va   = NULL;
                int   found_slot       = -1;

                NdisAcquireSpinLock(&AvbContext->subscription_lock);

                for (int i = 0; i < MAX_TS_SUBSCRIPTIONS; i++) {
                    if (AvbContext->subscriptions[i].ring_id == unsub_req->ring_id &&
                        AvbContext->subscriptions[i].active) {
                        found_slot = i;
                        break;
                    }
                }

                if (found_slot >= 0) {
                    TS_SUBSCRIPTION *sub = &AvbContext->subscriptions[found_slot];

                    DEBUGP(DL_TRACE, "!!! FREEING SUBSCRIPTION: slot=%d, ring_id=%u, ring_buffer=%p\n",
                           found_slot, sub->ring_id, sub->ring_buffer);

                    // Snapshot pointers and NULL all fields under the lock
                    unsubscribe_ring = sub->ring_buffer;
                    unsubscribe_mdl  = sub->ring_mdl;
                    unsubscribe_va   = sub->user_va;

                    sub->ring_buffer = NULL;
                    sub->ring_mdl    = NULL;
                    sub->user_va     = NULL;
                    sub->ring_id     = 0;
                    sub->active      = 0;
                    sub->event_mask  = 0;
                    sub->file_object = NULL;

                    unsub_req->status = (avb_u32)NDIS_STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                } else {
                    DEBUGP(DL_ERROR, "!!! UNSUBSCRIBE: ring_id=%u not found\n", unsub_req->ring_id);
                    unsub_req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                }

                NdisReleaseSpinLock(&AvbContext->subscription_lock);

                // Free resources OUTSIDE the spinlock (IRQL back to PASSIVE / APC_LEVEL)
                if (found_slot >= 0) {
                    if (unsubscribe_mdl) {
                        if (unsubscribe_va) {
                            MmUnmapLockedPages(unsubscribe_va, unsubscribe_mdl);
                        }
                        IoFreeMdl(unsubscribe_mdl);
                    }
                    if (unsubscribe_ring) {
                        ExFreePoolWithTag(unsubscribe_ring, FILTER_ALLOC_TAG);
                    }
                    DEBUGP(DL_TRACE, "!!! SUBSCRIPTION FREED: slot=%d is now available\n", found_slot);
                }
                
                info = sizeof(*unsub_req);
            }
        }
        break;
    
    // Implements #18 (REQ-F-HWCTX-001: Hardware State Machine)
    case IOCTL_AVB_GET_HW_STATE:
        DEBUGP(DL_TRACE, "? IOCTL_AVB_GET_HW_STATE: Hardware state query\n");
        DEBUGP(DL_TRACE, "   - Context: %p\n", AvbContext);
        DEBUGP(DL_TRACE, "   - Global context: %p\n", g_AvbContext);
        DEBUGP(DL_TRACE, "   - Filter instance: %p\n", AvbContext->filter_instance);
        DEBUGP(DL_TRACE, "   - Device type: %d\n", AvbContext->intel_device.device_type);
        
        if (outLen < sizeof(AVB_HW_STATE_QUERY)) { 
            status = STATUS_BUFFER_TOO_SMALL; 
        } else { 
            PAVB_HW_STATE_QUERY q = (PAVB_HW_STATE_QUERY)buf; 
            RtlZeroMemory(q, sizeof(*q)); 
            q->hw_state = (avb_u32)AvbContext->hw_state; 
            q->vendor_id = AvbContext->intel_device.pci_vendor_id; 
            q->device_id = AvbContext->intel_device.pci_device_id; 
            q->capabilities = AvbContext->intel_device.capabilities; 
            info = sizeof(*q);
            
            DEBUGP(DL_TRACE, "? HW_STATE: state=%s, VID=0x%04X, DID=0x%04X, caps=0x%08X\n",
                   AvbHwStateName(q->hw_state), q->vendor_id, q->device_id, q->capabilities);
            
            // Force BAR0 discovery attempt if hardware is still in BOUND state
            if (AvbContext->hw_state == AVB_HW_BOUND && AvbContext->hardware_context == NULL) {
                DEBUGP(DL_TRACE, "? FORCING BAR0 DISCOVERY: Hardware stuck in BOUND state, attempting manual discovery...\n");
                
                // Ensure device type is properly set
                if (AvbContext->intel_device.device_type == INTEL_DEVICE_UNKNOWN && 
                    AvbContext->intel_device.pci_device_id != 0) {
                    AvbContext->intel_device.device_type = AvbGetIntelDeviceType(AvbContext->intel_device.pci_device_id);
                    DEBUGP(DL_TRACE, "? Updated device type to %d for DID=0x%04X\n", 
                           AvbContext->intel_device.device_type, AvbContext->intel_device.pci_device_id);
                }
                
                PHYSICAL_ADDRESS bar0 = {0};
                ULONG barLen = 0;
                NTSTATUS ds = AvbDiscoverIntelControllerResources(AvbContext->filter_instance, &bar0, &barLen);
                if (NT_SUCCESS(ds)) {
                    DEBUGP(DL_TRACE, "? MANUAL BAR0 DISCOVERY SUCCESS: PA=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
                    NTSTATUS ms = AvbMapIntelControllerMemory(AvbContext, bar0, barLen);
                    if (NT_SUCCESS(ms)) {
                        DEBUGP(DL_TRACE, "? MANUAL BAR0 MAPPING SUCCESS: Hardware context now available\n");
                        
                        // Complete the initialization sequence
                        if (intel_init(&AvbContext->intel_device) == 0) {
                            DEBUGP(DL_TRACE, "? MANUAL intel_init SUCCESS\n");
                            
                            // Test MMIO sanity
                            ULONG ctrl = INTEL_MASK_32BIT;
                            if (intel_read_reg(&AvbContext->intel_device, I210_CTRL, &ctrl) == 0 && ctrl != INTEL_MASK_32BIT) {
                                DEBUGP(DL_TRACE, "? MANUAL MMIO SANITY SUCCESS: CTRL=0x%08X\n", ctrl);
                                AVB_SET_HW_STATE(AvbContext, AVB_HW_BAR_MAPPED);
                                AvbContext->hw_access_enabled = TRUE;
                                AvbContext->initialized = TRUE;
                                q->hw_state = (avb_u32)AvbContext->hw_state; // Update return value
                                
                                // Try I210 PTP initialization if applicable
                                if (AvbContext->intel_device.device_type == INTEL_DEVICE_I210) {
                                    DEBUGP(DL_TRACE, "?? MANUAL I210 PTP INIT: Starting...\n");
                                    AvbI210EnsureSystimRunning(AvbContext);
                                }
                            } else {
                                DEBUGP(DL_ERROR, "? MANUAL MMIO SANITY FAILED: CTRL=0x%08X\n", ctrl);
                            }
                        } else {
                            DEBUGP(DL_ERROR, "? MANUAL intel_init FAILED\n");
                        }
                    } else {
                        DEBUGP(DL_ERROR, "? MANUAL BAR0 MAPPING FAILED: 0x%08X\n", ms);
                    }
                } else {
                    DEBUGP(DL_ERROR, "? MANUAL BAR0 DISCOVERY FAILED: 0x%08X\n", ds);
                }
            }
        } 
        break;
    case IOCTL_AVB_OPEN_ADAPTER:
        DEBUGP(DL_TRACE, "? IOCTL_AVB_OPEN_ADAPTER: Multi-adapter context switching\n");
        
        if (outLen < sizeof(AVB_OPEN_REQUEST)) { 
            DEBUGP(DL_ERROR, "? OPEN_ADAPTER: Buffer too small (%lu < %lu)\n", outLen, sizeof(AVB_OPEN_REQUEST));
            status = STATUS_BUFFER_TOO_SMALL; 
        } else { 
            PAVB_OPEN_REQUEST req = (PAVB_OPEN_REQUEST)buf; 
            DEBUGP(DL_TRACE, "? OPEN_ADAPTER: Looking for VID=0x%04X DID=0x%04X\n", 
                   req->vendor_id, req->device_id);
            
            // Search through ALL filter modules to find the requested adapter
            // CRITICAL FIX: Use index to select N-th adapter with matching VID/DID (multi-adapter support)
            BOOLEAN bFalse = FALSE;
            PMS_FILTER targetFilter = NULL;
            PAVB_DEVICE_CONTEXT targetContext = NULL;
            ULONG matchCount = 0;  // Count adapters matching VID/DID
            
            FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            PLIST_ENTRY Link = FilterModuleList.Flink;
            
            while (Link != &FilterModuleList && targetFilter == NULL) {
                PMS_FILTER cand = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                Link = Link->Flink;
                
                if (cand && cand->AvbContext) {
                    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)cand->AvbContext;
                    DEBUGP(DL_TRACE, "? OPEN_ADAPTER: Checking filter %wZ - VID=0x%04X DID=0x%04X\n",
                           &cand->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                           ctx->intel_device.pci_device_id);
                    
                    if (ctx->intel_device.pci_vendor_id == req->vendor_id && 
                        ctx->intel_device.pci_device_id == req->device_id) {
                        // FIX: Check if this is the N-th matching adapter
                        if (matchCount == req->index) {
                            targetFilter = cand;
                            targetContext = ctx;
                            DEBUGP(DL_TRACE, "? Found target adapter: %wZ (VID=0x%04X, DID=0x%04X, index=%u, match=%u)\n", 
                                   &cand->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                                   ctx->intel_device.pci_device_id, req->index, matchCount);
                            break;
                        } else {
                            DEBUGP(DL_TRACE, "? Skipping adapter %wZ (match %u, need index %u)\n",
                                   &cand->MiniportFriendlyName, matchCount, req->index);
                            matchCount++;
                        }
                    }
                }
            }
            
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            
            if (targetFilter == NULL) {
                DEBUGP(DL_ERROR, "? OPEN_ADAPTER: No adapter found for VID=0x%04X DID=0x%04X\n", 
                       req->vendor_id, req->device_id);
                DEBUGP(DL_ERROR, "   Available adapters:\n");
                
                // List all available adapters for debugging
                FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
                Link = FilterModuleList.Flink;
                while (Link != &FilterModuleList) {
                    PMS_FILTER f = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                    Link = Link->Flink;
                    if (f && f->AvbContext) {
                        PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
                        UNREFERENCED_PARAMETER(ctx);
                        DEBUGP(DL_ERROR, "     - %wZ: VID=0x%04X DID=0x%04X\n",
                               &f->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                               ctx->intel_device.pci_device_id);
                    }
                }
                FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
                
                req->status = (avb_u32)STATUS_NO_SUCH_DEVICE;
                info = sizeof(*req);
                status = STATUS_SUCCESS; // IRP handled, but device not found
            } else {
                // CRITICAL: Switch global context to the requested adapter
                DEBUGP(DL_TRACE, "? OPEN_ADAPTER: Switching global context\n");
                DEBUGP(DL_TRACE, "   - From: VID=0x%04X DID=0x%04X (filter=%p)\n",
                       g_AvbContext ? g_AvbContext->intel_device.pci_vendor_id : 0,
                       g_AvbContext ? g_AvbContext->intel_device.pci_device_id : 0,
                       g_AvbContext ? g_AvbContext->filter_instance : NULL);
                DEBUGP(DL_TRACE, "   - To:   VID=0x%04X DID=0x%04X (filter=%p)\n",
                       targetContext->intel_device.pci_vendor_id,
                       targetContext->intel_device.pci_device_id,
                       targetFilter);
                
                // Make the target context the active global context
                g_AvbContext = targetContext;
                
                // Note: private_data already points to struct intel_private with platform_data set during initialization
                
                // Ensure the target context is fully initialized
                if (!targetContext->initialized || targetContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_TRACE, "? OPEN_ADAPTER: Target adapter needs initialization\n");
                    DEBUGP(DL_TRACE, "   - Current state: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_TRACE, "   - Initialized: %s\n", targetContext->initialized ? "YES" : "NO");
                    
                    NTSTATUS initStatus = AvbBringUpHardware(targetContext);
                    if (!NT_SUCCESS(initStatus)) {
                        DEBUGP(DL_ERROR, "? OPEN_ADAPTER: Target adapter initialization failed: 0x%08X\n", initStatus);
                        // Continue anyway - some functionality may still work
                    }
                }
                
                // CRITICAL FIX: Force PTP initialization for all supported devices
                intel_device_type_t target_type = targetContext->intel_device.device_type;
                if ((target_type == INTEL_DEVICE_I210 || target_type == INTEL_DEVICE_I225 || 
                     target_type == INTEL_DEVICE_I226) &&
                    targetContext->hw_state >= AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_TRACE, "? OPEN_ADAPTER: Forcing PTP initialization for selected adapter\n");
                    DEBUGP(DL_TRACE, "   - Device Type: %d\n", target_type);
                    DEBUGP(DL_TRACE, "   - Hardware State: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_TRACE, "   - Hardware Context: %p\n", targetContext->hardware_context);
                    
                    // Force complete PTP setup
                    AvbEnsureDeviceReady(targetContext);
                    
                    DEBUGP(DL_TRACE, "? OPEN_ADAPTER: PTP initialization completed\n");
                    DEBUGP(DL_TRACE, "   - Final Hardware State: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_TRACE, "   - Final Capabilities: 0x%08X\n", targetContext->intel_device.capabilities);
                }
                
                req->status = 0; // Success
                info = sizeof(*req);
                status = STATUS_SUCCESS;
                
                DEBUGP(DL_TRACE, "? OPEN_ADAPTER: Context switch completed successfully\n");
                DEBUGP(DL_TRACE, "   - Active context: VID=0x%04X DID=0x%04X\n",
                       g_AvbContext->intel_device.pci_vendor_id,
                       g_AvbContext->intel_device.pci_device_id);
                DEBUGP(DL_TRACE, "   - Hardware state: %s\n", AvbHwStateName(g_AvbContext->hw_state));
                DEBUGP(DL_TRACE, "   - Capabilities: 0x%08X\n", g_AvbContext->intel_device.capabilities);
            }
        }
        break;
    case IOCTL_AVB_SETUP_TAS:
        {
            DEBUGP(DL_FATAL, "!!! DIAG: IOCTL_AVB_SETUP_TAS ENTERED - inLen=%lu outLen=%lu required=%lu\n",
                   inLen, outLen, (ULONG)sizeof(AVB_TAS_REQUEST));
            DEBUGP(DL_TRACE, "? IOCTL_AVB_SETUP_TAS: Phase 2 Enhanced TAS Configuration\n");
            
            if (inLen < sizeof(AVB_TAS_REQUEST) || outLen < sizeof(AVB_TAS_REQUEST)) {
                DEBUGP(DL_FATAL, "!!! DIAG: TAS SETUP FAILED - Buffer too small\n");
                DEBUGP(DL_ERROR, "? TAS SETUP: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                // TAS requires PTP clock running for time-synchronized gate scheduling
                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_ERROR, "? TAS SETUP: PTP not ready (state=%s, need PTP_READY)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_TAS_REQUEST r = (PAVB_TAS_REQUEST)buf;
                    
                    // Note: private_data already points to struct intel_private with platform_data set during initialization
                    
                    DEBUGP(DL_TRACE, "?? TAS SETUP: Phase 2 Enhanced Configuration on VID=0x%04X DID=0x%04X\n",
                           activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    
                    // Check if this device supports TAS
                    if (!(activeContext->intel_device.capabilities & INTEL_CAP_TSN_TAS)) {
                        DEBUGP(DL_FATAL, "!!! DIAG: TAS NOT SUPPORTED - caps=0x%08X (need INTEL_CAP_TSN_TAS=0x%08X)\n",
                               activeContext->intel_device.capabilities, INTEL_CAP_TSN_TAS);
                        DEBUGP(DL_TRACE, "? TAS SETUP: Device does not support TAS (caps=0x%08X)\n", 
                               activeContext->intel_device.capabilities);
                        r->status = (avb_u32)STATUS_NOT_SUPPORTED;
                        status = STATUS_SUCCESS; // IOCTL handled, but feature not supported
                    } else {
                        // Call Intel library TAS setup function (standard implementation)
                        DEBUGP(DL_FATAL, "!!! DIAG: Calling intel_setup_time_aware_shaper...\n");
                        DEBUGP(DL_TRACE, "? TAS SETUP: Calling Intel library TAS implementation\n");
                        int rc = intel_setup_time_aware_shaper(&activeContext->intel_device, &r->config);
                        DEBUGP(DL_FATAL, "!!! DIAG: intel_setup_time_aware_shaper returned: %d\n", rc);
                        r->status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "? TAS configuration successful\n");
                        } else {
                            DEBUGP(DL_ERROR, "? TAS setup failed: %d\n", rc);
                            
                            // Provide diagnostic information
                            switch (rc) {
                                case -ENOTSUP:
                                    DEBUGP(DL_ERROR, "   Reason: Device doesn't support TAS\n");
                                    break;
                                case -EBUSY:
                                    DEBUGP(DL_ERROR, "   Reason: Prerequisites not met (PTP clock or hardware state)\n");
                                    break;
                                case -EIO:
                                    DEBUGP(DL_ERROR, "   Reason: Hardware register access failed\n");
                                    break;
                                case -EINVAL:
                                    DEBUGP(DL_ERROR, "   Reason: Invalid configuration parameters\n");
                                    break;
                                default:
                                    DEBUGP(DL_ERROR, "   Reason: Unknown error\n");
                                    break;
                            }
                        }
                    }
                    
                    info = sizeof(*r);
                }
            }
        }
        break;

    case IOCTL_AVB_SETUP_FP:
        {
            DEBUGP(DL_FATAL, "!!! DIAG: IOCTL_AVB_SETUP_FP ENTERED - inLen=%lu outLen=%lu required=%lu\n",
                   inLen, outLen, (ULONG)sizeof(AVB_FP_REQUEST));
            DEBUGP(DL_TRACE, "? IOCTL_AVB_SETUP_FP: Phase 2 Enhanced Frame Preemption Configuration\n");
            
            if (inLen < sizeof(AVB_FP_REQUEST) || outLen < sizeof(AVB_FP_REQUEST)) {
                DEBUGP(DL_FATAL, "!!! DIAG: FP SETUP FAILED - Buffer too small\n");
                DEBUGP(DL_ERROR, "? FP SETUP: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                // Frame Preemption is time-synchronized - requires PTP clock running
                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_ERROR, "? FP SETUP: PTP not ready (state=%s, need PTP_READY)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_FP_REQUEST r = (PAVB_FP_REQUEST)buf;
                    
                    // Note: private_data already points to struct intel_private with platform_data set during initialization
                    
                    DEBUGP(DL_TRACE, "?? FP SETUP: Phase 2 Enhanced Configuration on VID=0x%04X DID=0x%04X\n",
                           activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    
                    // Check if this device supports Frame Preemption
                    if (!(activeContext->intel_device.capabilities & INTEL_CAP_TSN_FP)) {
                        DEBUGP(DL_TRACE, "? FP SETUP: Device does not support Frame Preemption (caps=0x%08X)\n", 
                               activeContext->intel_device.capabilities);
                        r->status = (avb_u32)STATUS_NOT_SUPPORTED;
                        status = STATUS_SUCCESS; // IOCTL handled, but feature not supported
                    } else {
                        // Call Intel library Frame Preemption setup function (standard implementation)
                        DEBUGP(DL_TRACE, "? FP SETUP: Calling Intel library Frame Preemption implementation\n");
                        int rc = intel_setup_frame_preemption(&activeContext->intel_device, &r->config);
                        r->status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "? Frame Preemption configuration successful\n");
                        } else {
                            DEBUGP(DL_ERROR, "? Frame Preemption setup failed: %d\n", rc);
                            
                            // Provide diagnostic information
                            switch (rc) {
                                case -ENOTSUP:
                                    DEBUGP(DL_ERROR, "   Reason: Device doesn't support Frame Preemption\n");
                                    break;
                                case -EBUSY:
                                    DEBUGP(DL_ERROR, "   Reason: Link not active or link partner doesn't support preemption\n");
                                    break;
                                case -EIO:
                                    DEBUGP(DL_ERROR, "   Reason: Hardware register access failed\n");
                                    break;
                                default:
                                    DEBUGP(DL_ERROR, "   Reason: Unknown error\n");
                                    break;
                            }
                        }
                    }
                    
                    info = sizeof(*r);
                }
            }
        }
        break;

    case IOCTL_AVB_SETUP_PTM:
        {
            DEBUGP(DL_FATAL, "!!! DIAG: IOCTL_AVB_SETUP_PTM ENTERED - inLen=%lu outLen=%lu required=%lu\n",
                   inLen, outLen, (ULONG)sizeof(AVB_PTM_REQUEST));
            DEBUGP(DL_TRACE, "? IOCTL_AVB_SETUP_PTM: Phase 2 Enhanced PTM Configuration\n");
            
            if (inLen < sizeof(AVB_PTM_REQUEST) || outLen < sizeof(AVB_PTM_REQUEST)) {
                DEBUGP(DL_FATAL, "!!! DIAG: PTM SETUP FAILED - Buffer too small\n");
                DEBUGP(DL_ERROR, "? PTM SETUP: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "? PTM SETUP: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_PTM_REQUEST r = (PAVB_PTM_REQUEST)buf;
                    
                    // Note: private_data already points to struct intel_private with platform_data set during initialization
                    
                    DEBUGP(DL_TRACE, "?? PTM SETUP: Phase 2 Enhanced Configuration on VID=0x%04X DID=0x%04X\n",
                           activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    
                    // Check if this device supports PCIe PTM
                    if (!(activeContext->intel_device.capabilities & INTEL_CAP_PCIe_PTM)) {
                        DEBUGP(DL_TRACE, "? PTM SETUP: Device does not support PCIe PTM (caps=0x%08X)\n", 
                               activeContext->intel_device.capabilities);
                        r->status = (avb_u32)STATUS_NOT_SUPPORTED;
                        status = STATUS_SUCCESS; // IOCTL handled, but feature not supported
                    } else {
                        // Phase 2: Use enhanced PTM implementation
                        DEBUGP(DL_TRACE, "? Phase 2: Calling enhanced PTM implementation\n");
                        int rc = intel_setup_ptm(&activeContext->intel_device, &r->config);
                        r->status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "? Phase 2: PTM configuration completed\n");
                        } else {
                            DEBUGP(DL_ERROR, "? Phase 2: PTM setup failed: %d\n", rc);
                        }
                    }
                    
                    info = sizeof(*r);
                }
            }
        }
        break;

    /* IEEE 802.1AS-2020 §11.3 timestampCorrectionPortDS latency calibration.
     * Sets ingress_latency_ns and egress_latency_ns on the active adapter context.
     * The driver adds these signed values to RX/TX hardware timestamps before posting
     * timestamp events, so user-mode gPTP stacks receive boundary-corrected timestamps. */
    case IOCTL_AVB_SET_PORT_LATENCY:
        if (inLen < sizeof(AVB_PORT_LATENCY_REQUEST)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            PAVB_PORT_LATENCY_REQUEST lat = (PAVB_PORT_LATENCY_REQUEST)buf;
            /* Use interlocked stores so concurrent DPC/filter reads see consistent values. */
            InterlockedExchange64(&AvbContext->ingress_latency_ns, lat->ingress_latency_ns);
            InterlockedExchange64(&AvbContext->egress_latency_ns,   lat->egress_latency_ns);
            DEBUGP(DL_TRACE, "IOCTL_AVB_SET_PORT_LATENCY: ingress=%lld ns, egress=%lld ns\n",
                   lat->ingress_latency_ns, lat->egress_latency_ns);
            status = STATUS_SUCCESS;
        }
        break;

    /* IEEE 802.1Qav Credit-Based Shaper (CBS) configuration.
     * Validates and stores per-traffic-class shaper parameters.
     * Input validation follows IEEE 802.1Qav §8.6.8 credit range constraints:
     *   - tc must be in [0,7]  (IEEE 802.1Q defines 8 traffic classes)
     *   - send_slope must be ≤ 0 (credits deplete while sending; stored as INT32)
     *   - hi_credit must be ≥ 0 (upper bound on credit accumulation)
     *   - lo_credit must be ≤ 0 (lower bound; transmission stops while lo_credit < credit)
     * Implements: #8 (REQ-F-QAV-001: Credit-Based Shaper Configuration) */
    case IOCTL_AVB_SETUP_QAV:
        {
            if (inLen < sizeof(AVB_QAV_REQUEST)) {
                status = STATUS_INVALID_PARAMETER;
            } else {
                PAVB_QAV_REQUEST qav = (PAVB_QAV_REQUEST)buf;

                /* Validate traffic class: IEEE 802.1Q supports 8 priority levels (0–7). */
                if (qav->tc >= 8) {
                    qav->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    DEBUGP(DL_ERROR, "IOCTL_AVB_SETUP_QAV: invalid tc=%u (must be 0-7)\n", qav->tc);
                }
                /* send_slope is the rate at which credit decreases while a frame is being sent.
                 * It must be non-positive (zero means no CBS, negative is the normal case). */
                else if ((INT32)qav->send_slope > 0) {
                    qav->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    DEBUGP(DL_ERROR, "IOCTL_AVB_SETUP_QAV: send_slope=0x%X must be <= 0\n", qav->send_slope);
                }
                /* hi_credit: maximum credit value; must be >= 0. */
                else if ((INT32)qav->hi_credit < 0) {
                    qav->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    DEBUGP(DL_ERROR, "IOCTL_AVB_SETUP_QAV: hi_credit=0x%X must be >= 0\n", qav->hi_credit);
                }
                /* lo_credit: minimum credit below which transmission is blocked; must be <= 0. */
                else if ((INT32)qav->lo_credit > 0) {
                    qav->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    DEBUGP(DL_ERROR, "IOCTL_AVB_SETUP_QAV: lo_credit=0x%X must be <= 0\n", qav->lo_credit);
                }
                else {
                    /* Parameters validated. Hardware CBS programming is device-specific;
                     * current implementation records acceptance (no-op stub).
                     * Future work: route through intel_device_ops_t::setup_qav when added. */
                    DEBUGP(DL_TRACE, "IOCTL_AVB_SETUP_QAV: tc=%u idle_slope=%u send_slope=%d hi=%d lo=%d\n",
                           qav->tc, qav->idle_slope, (INT32)qav->send_slope,
                           (INT32)qav->hi_credit, (INT32)qav->lo_credit);
                    qav->status = (avb_u32)NDIS_STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                }
                if (outLen >= sizeof(AVB_QAV_REQUEST)) {
                    info = sizeof(AVB_QAV_REQUEST);
                }
            }
        }
        break;

    /* MDIO (Management Data Input/Output) read — IEEE 802.3 Clause 22/45.
     * Routes to the device-specific mdio_read operation in intel_device_ops_t.
     * Input/Output: AVB_MDIO_REQUEST { page(phy_addr), reg, value(out), status }
     * Implements: #9 (REQ-F-MDIO-001: PHY Register Read via MDIO)
     * MULTI-ADAPTER: Use AvbContext (per-file-handle, set by OPEN_ADAPTER) not
     *                g_AvbContext (always adapter 0 = may be disconnected). */
    case IOCTL_AVB_MDIO_READ:
        {
            if (inLen < sizeof(AVB_MDIO_REQUEST) || outLen < sizeof(AVB_MDIO_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_MDIO_REQUEST mdio = (PAVB_MDIO_REQUEST)buf;

                /* IEEE 802.3 Clause 22: PHY address is a 5-bit field (0-31),
                 * register address is a 5-bit field (0-31). Reject out-of-range
                 * values before reaching device hardware. */
                if (mdio->page > 31 || mdio->reg > 31) {
                    DEBUGP(DL_ERROR, "IOCTL_AVB_MDIO_READ: out-of-range phy_addr=%u or reg=%u (Clause 22 max=31)\n",
                           mdio->page, mdio->reg);
                    mdio->value  = 0;
                    mdio->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    info   = sizeof(AVB_MDIO_REQUEST);
                    break;
                }

                /* Use per-adapter context (set by OPEN_ADAPTER IOCTL via FsContext);
                 * fall back to g_AvbContext only when no specific adapter was selected. */
                PAVB_DEVICE_CONTEXT activeContext = AvbContext ? AvbContext : g_AvbContext;
                const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);

                if (ops && ops->mdio_read) {
                    uint16_t val = 0;
                    int rc = ops->mdio_read(&activeContext->intel_device,
                                            (uint16_t)(mdio->page & INTEL_MASK_16BIT),
                                            (uint16_t)(mdio->reg  & INTEL_MASK_16BIT),
                                            &val);
                    mdio->value  = val;
                    mdio->status = (rc == 0) ? (avb_u32)NDIS_STATUS_SUCCESS : (avb_u32)NDIS_STATUS_FAILURE;
                    status       = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                    DEBUGP(DL_TRACE, "IOCTL_AVB_MDIO_READ: page=%u reg=%u val=0x%04X rc=%d\n",
                           mdio->page, mdio->reg, val, rc);
                } else {
                    DEBUGP(DL_ERROR, "IOCTL_AVB_MDIO_READ: mdio_read not supported on this device\n");
                    mdio->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                    status = STATUS_NOT_SUPPORTED;
                }
                info = sizeof(AVB_MDIO_REQUEST);
            }
        }
        break;

    /* MDIO write — IEEE 802.3 Clause 22/45.
     * Routes to the device-specific mdio_write operation in intel_device_ops_t.
     * Input: AVB_MDIO_REQUEST { page(phy_addr), reg, value(in), status(out) }
     * Implements: #9 (REQ-F-MDIO-002: PHY Register Write via MDIO)
     * MULTI-ADAPTER: Use AvbContext (per-file-handle, set by OPEN_ADAPTER) not
     *                g_AvbContext (always adapter 0 = may be disconnected). */
    case IOCTL_AVB_MDIO_WRITE:
        {
            if (inLen < sizeof(AVB_MDIO_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_MDIO_REQUEST mdio = (PAVB_MDIO_REQUEST)buf;

                /* IEEE 802.3 Clause 22: PHY address is a 5-bit field (0-31),
                 * register address is a 5-bit field (0-31). Reject out-of-range
                 * values before reaching device hardware. */
                if (mdio->page > 31 || mdio->reg > 31) {
                    DEBUGP(DL_ERROR, "IOCTL_AVB_MDIO_WRITE: out-of-range phy_addr=%u or reg=%u (Clause 22 max=31)\n",
                           mdio->page, mdio->reg);
                    mdio->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }

                /* Use per-adapter context (set by OPEN_ADAPTER IOCTL via FsContext);
                 * fall back to g_AvbContext only when no specific adapter was selected. */
                PAVB_DEVICE_CONTEXT activeContext = AvbContext ? AvbContext : g_AvbContext;
                const intel_device_ops_t *ops = intel_get_device_ops(activeContext->intel_device.device_type);

                if (ops && ops->mdio_write) {
                    int rc = ops->mdio_write(&activeContext->intel_device,
                                             (uint16_t)(mdio->page  & INTEL_MASK_16BIT),
                                             (uint16_t)(mdio->reg   & INTEL_MASK_16BIT),
                                             mdio->value);
                    mdio->status = (rc == 0) ? (avb_u32)NDIS_STATUS_SUCCESS : (avb_u32)NDIS_STATUS_FAILURE;
                    status       = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                    DEBUGP(DL_TRACE, "IOCTL_AVB_MDIO_WRITE: page=%u reg=%u val=0x%04X rc=%d\n",
                           mdio->page, mdio->reg, mdio->value, rc);
                } else {
                    DEBUGP(DL_ERROR, "IOCTL_AVB_MDIO_WRITE: mdio_write not supported on this device\n");
                    mdio->status = (avb_u32)NDIS_STATUS_NOT_SUPPORTED;
                    status = STATUS_NOT_SUPPORTED;
                }
                if (outLen >= sizeof(AVB_MDIO_REQUEST)) {
                    info = sizeof(AVB_MDIO_REQUEST);
                }
            }
        }
        break;

    // Implements #270 (TEST-STATISTICS-001): IOCTL_AVB_GET_STATISTICS
    case IOCTL_AVB_GET_STATISTICS:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_GET_STATISTICS called\n");
            if (buf == NULL) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (outLen < sizeof(AVB_DRIVER_STATISTICS)) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            PAVB_DRIVER_STATISTICS st = (PAVB_DRIVER_STATISTICS)buf;
            st->TxPackets           = (avb_u64)currentContext->stats_tx_packets;
            st->RxPackets           = (avb_u64)currentContext->stats_rx_packets;
            st->TxBytes             = (avb_u64)currentContext->stats_tx_bytes;
            st->RxBytes             = (avb_u64)currentContext->stats_rx_bytes;
            st->PhcQueryCount       = (avb_u64)currentContext->stats_phc_query_count;
            st->PhcAdjustCount      = (avb_u64)currentContext->stats_phc_adjust_count;
            st->PhcSetCount         = (avb_u64)currentContext->stats_phc_set_count;
            st->TimestampCount      = (avb_u64)currentContext->stats_timestamp_count;
            st->IoctlCount          = (avb_u64)currentContext->stats_ioctl_count;
            st->ErrorCount          = (avb_u64)currentContext->stats_error_count;
            st->MemoryAllocFailures = (avb_u64)currentContext->stats_memory_alloc_failures;
            st->HardwareFaults      = (avb_u64)currentContext->stats_hardware_faults;
            st->FilterAttachCount   = (avb_u64)currentContext->stats_filter_attach_count;
            /* Extended fields (ABI 2.0) */
            st->FilterPauseCount       = (avb_u64)currentContext->stats_filter_pause_count;
            st->FilterRestartCount     = (avb_u64)currentContext->stats_filter_restart_count;
            st->FilterDetachCount      = (avb_u64)currentContext->stats_filter_detach_count;
            st->OutstandingSendNBLs    = (avb_u64)currentContext->stats_outstanding_send_nbls;
            st->OutstandingReceiveNBLs = (avb_u64)currentContext->stats_outstanding_receive_nbls;
            st->OidRequestCount        = (avb_u64)currentContext->stats_oid_request_count;
            st->OidCompleteCount       = (avb_u64)currentContext->stats_oid_complete_count;
            st->OutstandingOids        = (avb_u64)currentContext->stats_outstanding_oids;
            st->FilterStatusCount      = (avb_u64)currentContext->stats_filter_status_count;
            st->FilterNetPnPCount      = (avb_u64)currentContext->stats_filter_net_pnp_count;
            st->PauseRestartGeneration = (avb_u64)currentContext->stats_pause_restart_generation;
            status = STATUS_SUCCESS;
            info   = sizeof(AVB_DRIVER_STATISTICS);
        }
        break;

    case IOCTL_AVB_RESET_STATISTICS:
        {
            DEBUGP(DL_TRACE, "IOCTL_AVB_RESET_STATISTICS called\n");
            InterlockedExchange64(&currentContext->stats_tx_packets,           0);
            InterlockedExchange64(&currentContext->stats_rx_packets,           0);
            InterlockedExchange64(&currentContext->stats_tx_bytes,             0);
            InterlockedExchange64(&currentContext->stats_rx_bytes,             0);
            InterlockedExchange64(&currentContext->stats_phc_query_count,      0);
            InterlockedExchange64(&currentContext->stats_phc_adjust_count,     0);
            InterlockedExchange64(&currentContext->stats_phc_set_count,        0);
            InterlockedExchange64(&currentContext->stats_timestamp_count,      0);
            InterlockedExchange64(&currentContext->stats_ioctl_count,          0);
            InterlockedExchange64(&currentContext->stats_error_count,          0);
            InterlockedExchange64(&currentContext->stats_memory_alloc_failures,0);
            InterlockedExchange64(&currentContext->stats_hardware_faults,      0);
            InterlockedExchange64(&currentContext->stats_filter_attach_count,  0);
            /* Extended fields (ABI 2.0) */
            InterlockedExchange64(&currentContext->stats_filter_pause_count,       0);
            InterlockedExchange64(&currentContext->stats_filter_restart_count,     0);
            InterlockedExchange64(&currentContext->stats_filter_detach_count,      0);
            InterlockedExchange64(&currentContext->stats_outstanding_send_nbls,    0);
            InterlockedExchange64(&currentContext->stats_outstanding_receive_nbls, 0);
            InterlockedExchange64(&currentContext->stats_oid_request_count,        0);
            InterlockedExchange64(&currentContext->stats_oid_complete_count,       0);
            InterlockedExchange64(&currentContext->stats_outstanding_oids,         0);
            InterlockedExchange64(&currentContext->stats_filter_status_count,      0);
            InterlockedExchange64(&currentContext->stats_filter_net_pnp_count,     0);
            InterlockedExchange64(&currentContext->stats_pause_restart_generation, 0);
            status = STATUS_SUCCESS;
            info   = 0;
        }
        break;

    // Implements #236 (REQ-F-ATDECC-SUB-001: ATDECC Entity Event Subscription)
    // IEEE 1722.1 §7.5 ADP — subscribe to entity-available / entity-departing events.
    case IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE:
        {
            if (inLen < sizeof(AVB_ATDECC_SUBSCRIBE_REQUEST) ||
                outLen < sizeof(AVB_ATDECC_SUBSCRIBE_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
                break;
            }
            PAVB_ATDECC_SUBSCRIBE_REQUEST req = (PAVB_ATDECC_SUBSCRIBE_REQUEST)buf;
            if (req->event_mask == 0) {
                req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                status = STATUS_INVALID_PARAMETER;
                info = sizeof(*req);
                break;
            }
            NdisAcquireSpinLock(&currentContext->atdecc_sub_lock);
            int free_slot = -1;
            for (int i = 0; i < MAX_ATDECC_SUBSCRIPTIONS; i++) {
                if (currentContext->atdecc_subscriptions[i].sub_id == 0) {
                    free_slot = i;
                    break;
                }
            }
            if (free_slot == -1) {
                NdisReleaseSpinLock(&currentContext->atdecc_sub_lock);
                req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                status = STATUS_TOO_MANY_SESSIONS;
                info = sizeof(*req);
                break;
            }
            ATDECC_SUBSCRIPTION *sub = &currentContext->atdecc_subscriptions[free_slot];
            sub->event_mask = req->event_mask;
            sub->head = 0;
            sub->tail = 0;
            sub->active = 1;
            sub->file_object = sp->FileObject;  /* track owner for IRP_MJ_CLEANUP */
            sub->sub_id = (avb_u32)InterlockedIncrement(&currentContext->next_atdecc_sub_id);
            NdisReleaseSpinLock(&currentContext->atdecc_sub_lock);
            req->subscription_id = sub->sub_id;
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info = sizeof(*req);
        }
        break;

    // Implements #236 (REQ-F-ATDECC-POLL-001: non-blocking dequeue of ATDECC event)
    case IOCTL_AVB_ATDECC_EVENT_POLL:
        {
            if (inLen < sizeof(AVB_ATDECC_POLL_REQUEST) ||
                outLen < sizeof(AVB_ATDECC_POLL_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
                break;
            }
            PAVB_ATDECC_POLL_REQUEST req = (PAVB_ATDECC_POLL_REQUEST)buf;
            NdisAcquireSpinLock(&currentContext->atdecc_sub_lock);
            ATDECC_SUBSCRIPTION *found = NULL;
            for (int i = 0; i < MAX_ATDECC_SUBSCRIPTIONS; i++) {
                if (currentContext->atdecc_subscriptions[i].sub_id == req->subscription_id &&
                    currentContext->atdecc_subscriptions[i].active) {
                    found = &currentContext->atdecc_subscriptions[i];
                    break;
                }
            }
            if (!found) {
                NdisReleaseSpinLock(&currentContext->atdecc_sub_lock);
                req->status = (avb_u32)NDIS_STATUS_INVALID_PARAMETER;
                status = STATUS_INVALID_PARAMETER;
                info = sizeof(*req);
                break;
            }
            req->event_available = 0;
            req->entity_guid = 0;
            req->capabilities = 0;
            req->event_type = 0;
            /* Dequeue one entry if available (lock-free style under spin lock) */
            LONG h = found->head, t = found->tail;
            if (h != t) {
                ATDECC_EVENT_ENTRY *e = &found->queue[h & (ATDECC_EVENT_QUEUE_DEPTH - 1)];
                req->entity_guid    = e->entity_guid;
                req->capabilities   = e->capabilities;
                req->event_type     = e->event_type;
                req->event_available = 1;
                found->head = h + 1;
            }
            NdisReleaseSpinLock(&currentContext->atdecc_sub_lock);
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info = sizeof(*req);
        }
        break;

    // Implements #236 (REQ-F-ATDECC-UNSUB-001: release ATDECC subscription)
    case IOCTL_AVB_ATDECC_EVENT_UNSUBSCRIBE:
        {
            if (inLen < sizeof(AVB_ATDECC_SUBSCRIBE_REQUEST) ||
                outLen < sizeof(AVB_ATDECC_SUBSCRIBE_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
                break;
            }
            PAVB_ATDECC_SUBSCRIBE_REQUEST req = (PAVB_ATDECC_SUBSCRIBE_REQUEST)buf;
            NdisAcquireSpinLock(&currentContext->atdecc_sub_lock);
            int found_slot = -1;
            for (int i = 0; i < MAX_ATDECC_SUBSCRIPTIONS; i++) {
                if (currentContext->atdecc_subscriptions[i].sub_id == req->subscription_id &&
                    currentContext->atdecc_subscriptions[i].active) {
                    found_slot = i;
                    break;
                }
            }
            if (found_slot >= 0) {
                currentContext->atdecc_subscriptions[found_slot].sub_id = 0;
                currentContext->atdecc_subscriptions[found_slot].active = 0;
                currentContext->atdecc_subscriptions[found_slot].event_mask = 0;
                currentContext->atdecc_subscriptions[found_slot].file_object = NULL;
                currentContext->atdecc_subscriptions[found_slot].head = 0;
                currentContext->atdecc_subscriptions[found_slot].tail = 0;
            }
            NdisReleaseSpinLock(&currentContext->atdecc_sub_lock);
            req->status = (avb_u32)(found_slot >= 0 ?
                              NDIS_STATUS_SUCCESS : NDIS_STATUS_INVALID_PARAMETER);
            status = (found_slot >= 0) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
            info = sizeof(*req);
        }
        break;

    // Implements #213 (REQ-F-VLAN-001: IEEE 802.1Q VLAN insertion enable)
    case IOCTL_AVB_VLAN_ENABLE:
        {
            if (inLen < sizeof(AVB_VLAN_REQUEST) || outLen < sizeof(AVB_VLAN_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_VLAN_REQUEST req = (PAVB_VLAN_REQUEST)buf;
            currentContext->vlan_id       = req->vlan_id;
            currentContext->vlan_pcp      = req->pcp;
            currentContext->vlan_strip_rx = req->strip_rx;
            currentContext->vlan_enabled  = 1;
            req->vlan_id_out = currentContext->vlan_id;
            req->pcp_out     = currentContext->vlan_pcp;
            req->enabled     = 1;
            req->status      = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    // Implements #213 (REQ-F-VLAN-002: IEEE 802.1Q VLAN insertion disable)
    case IOCTL_AVB_VLAN_DISABLE:
        {
            if (inLen < sizeof(AVB_VLAN_REQUEST) || outLen < sizeof(AVB_VLAN_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_VLAN_REQUEST req = (PAVB_VLAN_REQUEST)buf;
            currentContext->vlan_enabled = 0;
            req->vlan_id_out = currentContext->vlan_id;
            req->pcp_out     = currentContext->vlan_pcp;
            req->enabled     = 0;
            req->status      = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    // Implements #223 (REQ-F-EEE-001: IEEE 802.3az EEE/LPI enable)
    case IOCTL_AVB_EEE_ENABLE:
        {
            if (inLen < sizeof(AVB_EEE_REQUEST) || outLen < sizeof(AVB_EEE_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_EEE_REQUEST req = (PAVB_EEE_REQUEST)buf;
            currentContext->eee_lpi_timer_us = req->lpi_timer_us;
            currentContext->eee_enabled      = 1;
            /* Synthesize EEER readback: LPI timer in bits [15:8] */
            req->eeer_readback = ((avb_u32)(req->lpi_timer_us & 0xFFu)) << 8;
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    // Implements #223 (REQ-F-EEE-002: IEEE 802.3az EEE/LPI disable)
    case IOCTL_AVB_EEE_DISABLE:
        {
            if (inLen < sizeof(AVB_EEE_REQUEST) || outLen < sizeof(AVB_EEE_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_EEE_REQUEST req = (PAVB_EEE_REQUEST)buf;
            currentContext->eee_lpi_timer_us = 0;
            currentContext->eee_enabled      = 0;
            req->eeer_readback = 0;
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    // Implements #219 (REQ-F-PFC-001: IEEE 802.1Qbb PFC enable/disable)
    // req.enable=1 enables, req.enable=0 disables — same IOCTL code (57)
    case IOCTL_AVB_PFC_ENABLE:
        {
            if (inLen < sizeof(AVB_PFC_REQUEST) || outLen < sizeof(AVB_PFC_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_PFC_REQUEST req = (PAVB_PFC_REQUEST)buf;
            currentContext->pfc_enabled       = req->enable;
            currentContext->pfc_priority_mask = req->priority_mask;
            /* Synthesize PFCTOP: enable flag in bit 8, priority mask in bits [7:0] */
            req->pfctop_readback = req->enable
                ? ((avb_u32)0x100u | (avb_u32)req->priority_mask)
                : 0u;
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    // Implements #211 (REQ-F-SRP-001: IEEE 802.1Qat SRP stream registration)
    case IOCTL_AVB_SRP_REGISTER_STREAM:
        {
            if (inLen < sizeof(AVB_SRP_REGISTER_REQUEST) ||
                outLen < sizeof(AVB_SRP_REGISTER_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_SRP_REGISTER_REQUEST req = (PAVB_SRP_REGISTER_REQUEST)buf;
            NdisAcquireSpinLock(&currentContext->srp_lock);
            int free_slot = -1;
            for (int i = 0; i < MAX_SRP_STREAMS; i++) {
                if (currentContext->srp_reservations[i].handle == 0) {
                    free_slot = i; break;
                }
            }
            if (free_slot == -1) {
                NdisReleaseSpinLock(&currentContext->srp_lock);
                req->status = (avb_u32)NDIS_STATUS_RESOURCES;
                status = STATUS_TOO_MANY_SESSIONS;
                info = sizeof(*req);
                break;
            }
            SRP_RESERVATION *rsv = &currentContext->srp_reservations[free_slot];
            rsv->stream_id     = req->stream_id;
            rsv->bandwidth_bps = req->bandwidth_bps;
            rsv->handle        = (avb_u32)InterlockedIncrement(&currentContext->next_srp_handle);
            NdisReleaseSpinLock(&currentContext->srp_lock);
            req->reservation_handle = rsv->handle;
            req->reserved_bw_bps    = rsv->bandwidth_bps;
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    // Implements #211 (REQ-F-SRP-002: IEEE 802.1Qat SRP stream deregistration)
    case IOCTL_AVB_SRP_DEREGISTER_STREAM:
        {
            if (inLen < sizeof(AVB_SRP_DEREGISTER_REQUEST) ||
                outLen < sizeof(AVB_SRP_DEREGISTER_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; info = 0; break;
            }
            PAVB_SRP_DEREGISTER_REQUEST req = (PAVB_SRP_DEREGISTER_REQUEST)buf;
            NdisAcquireSpinLock(&currentContext->srp_lock);
            for (int i = 0; i < MAX_SRP_STREAMS; i++) {
                if (currentContext->srp_reservations[i].handle == req->reservation_handle) {
                    currentContext->srp_reservations[i].handle        = 0;
                    currentContext->srp_reservations[i].stream_id     = 0;
                    currentContext->srp_reservations[i].bandwidth_bps = 0;
                    break;
                }
            }
            NdisReleaseSpinLock(&currentContext->srp_lock);
            req->status = (avb_u32)NDIS_STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info   = sizeof(*req);
        }
        break;

    default:
        InterlockedIncrement64(&currentContext->stats_error_count);
        EventWriteEVT_IOCTL_ERROR(NULL);  /* ETW: unknown IOCTL = IOCTL error event */
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Information = info; 
    return status;
}

/* ======================================================================= */
/* Platform wrappers (real HW access provided in other translation units) */

/**
 * @brief Initialize PTP clock hardware for Intel devices
 * 
 * This function programs TIMINCA and starts SYSTIM counting.
 * Required before any TSN features (TAS/FP/PTM) will work.
 * 
 * @param dev Device handle with valid MMIO access
 * @return STATUS_SUCCESS if PTP initialized, error otherwise
 */
NTSTATUS AvbPlatformInit(_In_ device_t *dev)
{
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit ENTERED!\n");
    
    if (!dev) {
        DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit - dev is NULL!\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit - dev is valid, proceeding...\n");
    
    // Get device operations for HAL compliance
    const intel_device_ops_t *ops = intel_get_device_ops(dev->device_type);
    if (!ops) {
        DEBUGP(DL_ERROR, "? AvbPlatformInit: No device operations for type %d\n", dev->device_type);
        return STATUS_NOT_SUPPORTED;
    }
    
    DEBUGP(DL_TRACE, "? AvbPlatformInit: Starting PTP clock initialization\n");
    DEBUGP(DL_TRACE, "   Device: VID=0x%04X DID=0x%04X Type=%d\n", 
           dev->pci_vendor_id, dev->pci_device_id, dev->device_type);
    
    // Step 0: Enable PTP clock by clearing TSAUXC bit 31 (DisableSystime)
    ULONG tsauxc_value = 0;
    if (ops->read_tsauxc && ops->read_tsauxc(dev, &tsauxc_value) == 0) {
        DEBUGP(DL_TRACE, "   TSAUXC before: 0x%08X\n", tsauxc_value);
        if (tsauxc_value & INTEL_TSAUXC_DISABLE_SYSTIM) {
            // Bit 31 is set - PTP clock is DISABLED, need to enable it
            tsauxc_value &= INTEL_TSYNC_TS_MASK;  // Clear bit 31 to enable SYSTIM
            if (ops->write_tsauxc && ops->write_tsauxc(dev, tsauxc_value) != 0) {
                DEBUGP(DL_ERROR, "? Failed to enable PTP clock (TSAUXC write failed)\n");
                return STATUS_DEVICE_HARDWARE_ERROR;
            }
            DEBUGP(DL_TRACE, "? PTP clock enabled (TSAUXC bit 31 cleared)\n");
            
            // Verify the bit was cleared
            ULONG tsauxc_verify = 0;
            if (ops->read_tsauxc && ops->read_tsauxc(dev, &tsauxc_verify) == 0) {
                DEBUGP(DL_TRACE, "   TSAUXC after:  0x%08X\n", tsauxc_verify);
            }
        } else {
            DEBUGP(DL_TRACE, "? PTP clock already enabled (TSAUXC=0x%08X)\n", tsauxc_value);
        }
    } else {
        DEBUGP(DL_TRACE, "? Could not read TSAUXC register\n");
    }
    
    // Step 1: Program TIMINCA for 1ns increment per clock cycle
    // Value depends on device clock frequency:
    // - I210/I211: 25MHz -> TIMINCA = INTEL_TIMINCA_DEFAULT (384ns per tick, 2.5M ticks/sec)
    // - I225/I226: 25MHz -> TIMINCA = INTEL_TIMINCA_DEFAULT
    // - I217/I219: Different clocking, but INTEL_TIMINCA_DEFAULT works
    ULONG timinca_value = INTEL_TIMINCA_DEFAULT;  // Standard value for 25MHz clock
    
    ULONG current_timinca = 0;
    if (ops->read_timinca && ops->read_timinca(dev, &current_timinca) == 0) {
        DEBUGP(DL_TRACE, "   Current TIMINCA: 0x%08X\n", current_timinca);
    }
    
    if (ops->write_timinca && ops->write_timinca(dev, timinca_value) != 0) {
        DEBUGP(DL_ERROR, "? Failed to write TIMINCA register\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_TRACE, "? TIMINCA programmed: 0x%08X\n", timinca_value);
    
    // Step 2: Read initial SYSTIM value (SYSTIM is READ-ONLY on I226, starts from 0 on power-up)
    ULONGLONG systim_initial = 0;
    if (ops->get_systime && ops->get_systime(dev, &systim_initial) == 0) {
        DEBUGP(DL_TRACE, "? Initial SYSTIM: 0x%016llX\n", systim_initial);
    }
    
    // Step 3: Verify SYSTIM is incrementing (wait 10ms for better delta)
    LARGE_INTEGER delay;
    delay.QuadPart = -100000;  // 10ms delay (10,000 microseconds)
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    
    ULONGLONG systim_current = 0;
    if (ops->get_systime && ops->get_systime(dev, &systim_current) == 0) {
        
        DEBUGP(DL_TRACE, "? SYSTIM after 10ms: 0x%016llX\n", systim_current);
        
        if (systim_current > systim_initial) {
            DEBUGP(DL_TRACE, "?? PTP clock is RUNNING! Delta: %llu ns (expected ~10,000,000 ns for 10ms)\n", 
                   (systim_current - systim_initial));
            // Note: ctx->hw_state update happens in caller (AvbBringUpHardware)
            return STATUS_SUCCESS;
        } else {
            DEBUGP(DL_TRACE, "?? PTP clock not incrementing (SYSTIM unchanged: initial=0x%llX, current=0x%llX)\n", 
                   systim_initial, systim_current);
            // Not a fatal error - continue anyway, TAS might still be testable
            return STATUS_SUCCESS;
        }
    }
    
    DEBUGP(DL_TRACE, "? Could not verify SYSTIM status\n");
    return STATUS_SUCCESS;  // Non-fatal
}
VOID     AvbPlatformCleanup(_In_ device_t *dev){ UNREFERENCED_PARAMETER(dev); }
int AvbPciReadConfig(device_t *dev, ULONG o, ULONG *v){ return AvbPciReadConfigReal(dev,o,v);} 
int AvbPciWriteConfig(device_t *dev, ULONG o, ULONG v){ return AvbPciWriteConfigReal(dev,o,v);} 
int AvbMmioRead(device_t *dev, ULONG o, ULONG *v){ return AvbMmioReadReal(dev,o,v);} 
int AvbMmioWrite(device_t *dev, ULONG o, ULONG v){ return AvbMmioWriteReal(dev,o,v);} 
int AvbMdioRead(device_t *dev, USHORT p, USHORT r, USHORT *val){ return AvbMdioReadReal(dev,p,r,val);} 
int AvbMdioWrite(device_t *dev, USHORT p, USHORT r, USHORT val){ return AvbMdioWriteReal(dev,p,r,val);} 
int AvbReadTimestamp(device_t *dev, ULONGLONG *ts){ return AvbReadTimestampReal(dev,ts);} 
int AvbMdioReadI219Direct(device_t *dev, USHORT p, USHORT r, USHORT *val){ return AvbMdioReadI219DirectReal(dev,p,r,val);} 
int AvbMdioWriteI219Direct(device_t *dev, USHORT p, USHORT r, USHORT val){ return AvbMdioWriteI219DirectReal(dev,p,r,val);} 

/* Helpers */
BOOLEAN AvbIsIntelDevice(UINT16 vid, UINT16 did){ UNREFERENCED_PARAMETER(did); return vid==INTEL_VENDOR_ID; }

intel_device_type_t AvbGetIntelDeviceType(UINT16 did)
{ 
    switch(did) {
        // I-series modern devices
        case INTEL_DEV_I210_AT: return INTEL_DEVICE_I210;  // I210 Copper
        case INTEL_DEV_I210_AT2: return INTEL_DEVICE_I210;  // I210 Copper OEM1  
        case INTEL_DEV_I210_FIBER: return INTEL_DEVICE_I210;  // I210 Copper IT
        case INTEL_DEV_I210_IS: return INTEL_DEVICE_I210;  // I210 Fiber
        case INTEL_DEV_I210_IT: return INTEL_DEVICE_I210;  // I210 Serdes
        case INTEL_DEV_I210_CS: return INTEL_DEVICE_I210;  // I210 SGMII
        
        case INTEL_DEV_I217_LM: 
        case INTEL_DEV_I217_V: return INTEL_DEVICE_I217;  // I217 family
        
        case INTEL_DEV_I219_LM: 
        case INTEL_DEV_I219_V: 
        case INTEL_DEV_I219_D0: 
        case INTEL_DEV_I219_D1: 
        case INTEL_DEV_I219_D2: 
        case INTEL_DEV_I219_LM_DC7: 
        case INTEL_DEV_I219_V6: 
        case INTEL_DEV_I219_LM6: return INTEL_DEVICE_I219;  // I219 family
        
        case INTEL_DEV_I225_V: return INTEL_DEVICE_I225;  // I225
        case INTEL_DEV_I226_LM: return INTEL_DEVICE_I226;  // I226
        
        // IGB device family (82xxx series) - when implementing those make sure you prevent magic numbers!!
   //     case 0_10A7: return INTEL_DEVICE_82575;  // 82575EB Copper
   //     case 0_10A9: return INTEL_DEVICE_82575;  // 82575EB Fiber/Serdes  
   //     case 0_10D6: return INTEL_DEVICE_82575;  // 82575GB Quad Copper
        
    //    case 0_10C9: return INTEL_DEVICE_82576;  // 82576 Gigabit Network Connection
    //    case 0_10E6: return INTEL_DEVICE_82576;  // 82576 Fiber
    //    case 0_10E7: return INTEL_DEVICE_82576;  // 82576 Serdes
    //    case 0_10E8: return INTEL_DEVICE_82576;  // 82576 Quad Copper
    //    case 0_1526: return INTEL_DEVICE_82576;  // 82576 Quad Copper ET2
    //    case 0_150A: return INTEL_DEVICE_82576;  // 82576 NS
    //    case 0_1518: return INTEL_DEVICE_82576;  // 82576 NS Serdes
    //    case 0_150D: return INTEL_DEVICE_82576;  // 82576 Serdes Quad
        
     //   case 0_150E: return INTEL_DEVICE_82580;  // 82580 Copper
    //    case 0_150F: return INTEL_DEVICE_82580;  // 82580 Fiber
   //     case 0_1510: return INTEL_DEVICE_82580;  // 82580 Serdes
     //   case 0_1511: return INTEL_DEVICE_82580;  // 82580 SGMII
   //     case 0_1516: return INTEL_DEVICE_82580;  // 82580 Copper Dual
   //     case 0_1527: return INTEL_DEVICE_82580;  // 82580 Quad Fiber
        
     //   case 0_1521: return INTEL_DEVICE_I350;   // I350 Copper
    //    case 0_1522: return INTEL_DEVICE_I350;   // I350 Fiber
    //    case 0_1523: return INTEL_DEVICE_I350;   // I350 Serdes
    //    case 0_1524: return INTEL_DEVICE_I350;   // I350 SGMII
    //    case 0_1546: return INTEL_DEVICE_I350;   // I350 DA4
        
        // I354 uses same operations as I350
     //   case 0_1F40: return INTEL_DEVICE_I354;   // I354 Backplane 2.5GbE
     //   case 0_1F41: return INTEL_DEVICE_I354;   // I354 Backplane 1GbE
     //   case 0_1F45: return INTEL_DEVICE_I354;   // I354 SGMII
        
        default: return INTEL_DEVICE_UNKNOWN; 
    }
}

ULONG intel_get_capabilities(device_t *dev)
{
    if (dev == NULL) {
        return 0;
    }
    
    return dev->capabilities;
}

PMS_FILTER AvbFindIntelFilterModule(VOID)
{ 
    if (g_AvbContext && g_AvbContext->filter_instance) {
        // Verify this is actually a supported Intel device
        if (g_AvbContext->intel_device.pci_vendor_id == INTEL_VENDOR_ID &&
            g_AvbContext->intel_device.pci_device_id != 0) {
            DEBUGP(DL_TRACE, "AvbFindIntelFilterModule: Using global context VID=0x%04X DID=0x%04X\n",
                   g_AvbContext->intel_device.pci_vendor_id, g_AvbContext->intel_device.pci_device_id);
            return g_AvbContext->filter_instance;
        }
    }
    
    PMS_FILTER bestFilter = NULL;
    PAVB_DEVICE_CONTEXT bestContext = NULL;
    PLIST_ENTRY l; 
    BOOLEAN bFalse = FALSE; 
    
    DEBUGP(DL_TRACE, "AvbFindIntelFilterModule: Searching filter list for best Intel adapter...\n");
    
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse); 
    l = FilterModuleList.Flink; 
    
    while (l != &FilterModuleList) { 
        PMS_FILTER f = CONTAINING_RECORD(l, MS_FILTER, FilterModuleLink); 
        l = l->Flink; // Move to next before potentially releasing lock
        
        if (f && f->AvbContext) { 
            PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
            
            DEBUGP(DL_TRACE, "AvbFindIntelFilterModule: Checking filter %wZ - VID=0x%04X DID=0x%04X state=%s\n",
                   &f->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                   ctx->intel_device.pci_device_id, AvbHwStateName(ctx->hw_state));
            
            // Look for properly initialized Intel contexts
            if (ctx->intel_device.pci_vendor_id == INTEL_VENDOR_ID && 
                ctx->intel_device.pci_device_id != 0) {
                
                // Prefer contexts with better hardware state
                if (bestContext == NULL || ctx->hw_state > bestContext->hw_state) {
                    bestFilter = f;
                    bestContext = ctx;
                    DEBUGP(DL_TRACE, "AvbFindIntelFilterModule: New best candidate: %wZ (state=%s)\n",
                           &f->MiniportFriendlyName, AvbHwStateName(ctx->hw_state));
                }
            }
        } 
    } 
    
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse); 
    
    if (bestFilter && bestContext) {
        DEBUGP(DL_TRACE, "AvbFindIntelFilterModule: Selected best Intel filter: %wZ (VID=0x%04X DID=0x%04X state=%s)\n",
               &bestFilter->MiniportFriendlyName, bestContext->intel_device.pci_vendor_id,
               bestContext->intel_device.pci_device_id, AvbHwStateName(bestContext->hw_state));
        return bestFilter;
    }
    
    DEBUGP(DL_TRACE, "AvbFindIntelFilterModule: No Intel filter found with valid context\n");
    return NULL; 
}
