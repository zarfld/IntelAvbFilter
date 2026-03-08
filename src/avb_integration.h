/*++

Module Name:

    avb_integration.h

Abstract:

    Header file for AVB integration with Intel filter driver
    Provides hardware access bridge between NDIS filter and Intel AVB library

--*/

#ifndef _AVB_INTEGRATION_H_
#define _AVB_INTEGRATION_H_

#include "precomp.h"
/* Share IOCTL ABI (codes and request structs) with user-mode */
#include "include/avb_ioctl.h"

// Intel constants
#define INTEL_VENDOR_ID         0x8086

/* ------------------------------------------------------------------------- */
/* Intel hardware context for MMIO mapping */
typedef struct _INTEL_HARDWARE_CONTEXT {
    PHYSICAL_ADDRESS physical_address;  // BAR0 physical address
    PUCHAR mmio_base;                  // Mapped virtual address for register access
    ULONG mmio_length;                 // Size of mapped region
    BOOLEAN mapped;                    // TRUE if successfully mapped
} INTEL_HARDWARE_CONTEXT, *PINTEL_HARDWARE_CONTEXT;

/* ------------------------------------------------------------------------- */
/* Hardware lifecycle state machine (no fabrication) */
typedef enum _AVB_HW_STATE {
    AVB_HW_UNBOUND = 0,      /* Filter not yet attached to supported Intel miniport */
    AVB_HW_BOUND,            /* Filter attached to supported Intel adapter (no BAR/MMIO yet) */
    AVB_HW_BAR_MAPPED,       /* BAR0 resources discovered + MMIO mapped + basic register access validated */
    AVB_HW_PTP_READY         /* PTP clock verified incrementing & timestamp capture enabled */
} AVB_HW_STATE;

/* Utility: readable name for state (for debug prints) */
__forceinline const char* AvbHwStateName(LONG s) {
    switch ((AVB_HW_STATE)s) {
    case AVB_HW_UNBOUND: return "UNBOUND"; 
    case AVB_HW_BOUND: return "BOUND"; 
    case AVB_HW_BAR_MAPPED: return "BAR_MAPPED"; 
    case AVB_HW_PTP_READY: return "PTP_READY"; 
    default: return "?"; }
}

/*
 * Thread-safe hw_state accessors.
 *
 * AVB_READ_HW_STATE(ctx):   volatile 32-bit load — sufficient for reads at any IRQL on x86/x64.
 * AVB_SET_HW_STATE(ctx, s): InterlockedExchange — full memory fence, safe from any IRQL.
 */
#define AVB_READ_HW_STATE(ctx)    ((AVB_HW_STATE)((ctx)->hw_state))
#define AVB_SET_HW_STATE(ctx, s)  ((void)InterlockedExchange(&(ctx)->hw_state, (LONG)(s)))

/* Forward decl */
typedef struct _INTEL_HARDWARE_CONTEXT INTEL_HARDWARE_CONTEXT, *PINTEL_HARDWARE_CONTEXT;

/* Timestamp Event Subscription Management (Issue #13)
 * Supports up to 32 concurrent subscriptions per adapter
 * NOTE: Increased from 8 to support full test suite execution
 *       (19 tests × 5 adapters with no implicit cleanup until handle close)
 */
#define MAX_TS_SUBSCRIPTIONS 32

typedef struct _TS_SUBSCRIPTION {
    avb_u32 ring_id;                      // Subscription ID (1-based, 0=unused)
    avb_u32 event_mask;                   // TS_EVENT_* bitmask
    avb_u16 vlan_filter;                  // VLAN ID filter (INTEL_MASK_16BIT=no filter)
    avb_u8  pcp_filter;                   // PCP filter (0xFF=no filter)
    avb_u8  active;                       // 1=active, 0=unused slot
    PVOID ring_buffer;                    // NonPagedPool allocation (header + events)
    ULONG ring_count;                     // Number of event slots (power of 2)
    PMDL  ring_mdl;                       // MDL for user-space mapping
    PVOID user_va;                        // User virtual address (after mapping)
    volatile LONG sequence_num;           // Next event sequence number
    PFILE_OBJECT file_object;             // Owning handle (for cleanup on close)
} TS_SUBSCRIPTION;

// AVB device context structure
/* Pre-allocated NBL ring for fast SEND_PTP path.
 * Each slot owns its own packet buffer, MDL, NET_BUFFER, and NET_BUFFER_LIST.
 * Eliminates per-call NdisAllocateNetBuffer/NdisAllocateNetBufferList overhead (~10µs).
 * in_use: 0=free, 1=acquired by IOCTL_AVB_TEST_SEND_PTP handler,
 *         cleared by FilterSendNetBufferListsComplete when the NBL completes.
 * Ring size of 8 supports bursts of up to 8 in-flight test packets simultaneously.
 * Implements: Issue #35 (REQ-F-IOCTL-TS-001) - throughput optimization.   */
#define AVB_TEST_NBL_RING_SIZE  32   /* large enough to absorb ~100µs NIC completion jitter at 150K ops/sec */
typedef struct _AVB_TEST_NBL_SLOT {
    PNET_BUFFER_LIST nbl;     /* pre-allocated NBL, never freed until context teardown */
    PNET_BUFFER      nb;      /* pre-allocated NB, linked to this slot's MDL */
    PVOID            buffer;  /* per-slot packet buffer (TEST_PACKET_SIZE bytes, NonPaged) */
    PMDL             mdl;     /* per-slot MDL describing buffer */
    volatile LONG    in_use;  /* 0=free, 1=in-flight; CAS'd on acquire, Exchange'd on complete */
    LONG             _pad;    /* ensure 8-byte alignment */
} AVB_TEST_NBL_SLOT, *PAVB_TEST_NBL_SLOT;

typedef struct _AVB_DEVICE_CONTEXT {
    device_t intel_device;
    BOOLEAN initialized;
    PDEVICE_OBJECT filter_device;
    PMS_FILTER filter_instance;
    BOOLEAN hw_access_enabled;
    NDIS_HANDLE miniport_handle;
    PINTEL_HARDWARE_CONTEXT hardware_context;  // Real hardware access context

    // Hardware lifecycle state
    // Accessed from both PASSIVE_LEVEL (IOCTL) and DISPATCH_LEVEL (DPC).
    // All writes use InterlockedExchange for a full memory barrier.
    // All reads see the volatile field directly (x86/x64 aligned 32-bit load is atomic).
    volatile LONG hw_state;

    // ABI and capabilities tracking
    ULONG last_seen_abi_version;

    // Timestamp Event Subscriptions (Issue #13)
    TS_SUBSCRIPTION subscriptions[MAX_TS_SUBSCRIPTIONS];  // Subscription table
    NDIS_SPIN_LOCK subscription_lock;                     // Protects subscription table
    volatile LONG next_ring_id;                           // Monotonic ring_id allocator (1, 2, 3, ...)

    // TX Timestamp Polling (Task 6c)
    NDIS_TIMER tx_poll_timer;                             // Periodic timer for TX timestamp FIFO polling
    BOOLEAN tx_poll_active;                               // Timer running flag

    // Target Time DPC Timer (Task 7 - Issue #13)
    KTIMER target_time_timer;                             // Windows kernel timer object
    KDPC target_time_dpc;                                 // Deferred Procedure Call object
    BOOLEAN target_time_poll_active;                      // Flag: Timer is running
    ULONG target_time_poll_interval_ms;                   // Poll interval (default: 10ms)
    volatile LONG target_time_dpc_call_count;             // Diagnostic: DPC invocation counter

    // NDIS Packet Injection Pools (Step 8b - Test IOCTL for TX timestamp validation)
    NDIS_HANDLE nbl_pool_handle;                          // NET_BUFFER_LIST pool for test packets
    NDIS_HANDLE nb_pool_handle;                           // NET_BUFFER pool for test packets
    PVOID test_packet_buffer;                             // Pre-allocated PTP Sync frame (64 bytes, Non-Paged Pool)
    PMDL test_packet_mdl;                                 // MDL describing test_packet_buffer
    NDIS_SPIN_LOCK test_send_lock;                        // Protects test packet send operations
    volatile LONG test_packets_pending;                   // Count of outstanding test NBLs (for completion tracking)

    // Legacy ring (deprecated - kept for compatibility)
    // Timestamp event ring (section-based mapping)
    BOOLEAN ts_ring_allocated;
    ULONG   ts_ring_id;
    PVOID   ts_ring_buffer;     // System-space view base address
    ULONG   ts_ring_length;     // bytes (requested/actual)
    PMDL    ts_ring_mdl;        // reserved for MDL-based mapping option
    ULONGLONG ts_user_cookie;   // echoed back to UM
    HANDLE  ts_ring_section;    // section handle returned to UM
    SIZE_T  ts_ring_view_size;  // mapped system-space view size

    // Qav (Credit-Based Shaper) last request snapshot (placeholder)
    UCHAR   qav_last_tc;
    ULONG   qav_idle_slope;
    ULONG   qav_send_slope;
    ULONG   qav_hi_credit;
    ULONG   qav_lo_credit;

    /* IEEE 802.1AS-2020 §11.3 timestampCorrectionPortDS latency calibration.
     * Set via IOCTL_AVB_SET_PORT_LATENCY.  Both default to 0 (no correction). */
    volatile LONG64 ingress_latency_ns;   /* Added to RX hardware timestamps (signed, ns) */
    volatile LONG64 egress_latency_ns;    /* Added to TX hardware timestamps (signed, ns) */

    /* Adapter's current unicast MAC address — captured at FilterRestart from
     * OID_802_3_CURRENT_ADDRESS.  Used as source MAC bytes [6-11] in injected
     * test packets.  Zero until the first successful FilterRestart. */
    UCHAR source_mac[ETH_LENGTH_OF_ADDRESS];   /* 6 bytes */
    UCHAR source_mac_pad[2];                   /* pad to 8-byte alignment */

    /* NDIS 6.82 TaggedTransmitHw TX timestamp
     * Populated by FilterSendNetBufferListsComplete when igc.sys completes a
     * test packet that had NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT set.
     * Consumed (read-then-zero) by IOCTL_AVB_GET_TX_TIMESTAMP.
     * Zero means "no timestamp available yet".
     * Access protocol: InterlockedExchange64 for both store and load-then-zero.
     * Reinterpreted as ULONG64; LONGLONG declaration satisfies Interlocked ABI. */
    volatile LONGLONG last_ndis_tx_timestamp;

    /* Pre-allocated NBL ring for fast IOCTL_AVB_TEST_SEND_PTP path.
     * Slots are allocated at context creation (AvbCreateMinimalContext) and
     * freed at context teardown.  Each slot is independently recycled on
     * FilterSendNetBufferListsComplete without touching the NDIS pool allocator. */
    AVB_TEST_NBL_SLOT test_nbl_ring[AVB_TEST_NBL_RING_SIZE];

    /*
     * Runtime statistics — queried via IOCTL_AVB_GET_STATISTICS (0x9C40A020).
     * Implements #270 (TEST-STATISTICS-001).
     * Fields are incremented with InterlockedIncrement64 so they are safe
     * from concurrent IOCTL dispatch and NDIS send/receive paths.
     * Layout MUST match AVB_DRIVER_STATISTICS in avb_ioctl.h (104 bytes).
     */
    volatile LONGLONG stats_tx_packets;
    volatile LONGLONG stats_rx_packets;
    volatile LONGLONG stats_tx_bytes;
    volatile LONGLONG stats_rx_bytes;
    volatile LONGLONG stats_phc_query_count;
    volatile LONGLONG stats_phc_adjust_count;
    volatile LONGLONG stats_phc_set_count;
    volatile LONGLONG stats_timestamp_count;
    volatile LONGLONG stats_ioctl_count;
    volatile LONGLONG stats_error_count;
    volatile LONGLONG stats_memory_alloc_failures;
    volatile LONGLONG stats_hardware_faults;
    volatile LONGLONG stats_filter_attach_count;

    // Per-adapter list linkage — protected by g_AvbContextListLock
    struct _AVB_DEVICE_CONTEXT *next_context;
} AVB_DEVICE_CONTEXT, *PAVB_DEVICE_CONTEXT;

/*========================================================================
 * Global context registry
 *
 * g_AvbContext        – the currently *selected* adapter (used by IOCTLs that
 *                       do not carry a per-adapter handle).  Writable only under
 *                       g_AvbContextListLock.
 *
 * g_AvbContextList    – head of a singly-linked list of ALL attached adapter
 *                       contexts (linked via AVB_DEVICE_CONTEXT::next_context).
 *                       Protected by g_AvbContextListLock.
 *
 * g_AvbContextListLock – NDIS spin lock protecting both g_AvbContext and
 *                        g_AvbContextList.  Acquired at DISPATCH_LEVEL.
 *=======================================================================*/
extern PAVB_DEVICE_CONTEXT g_AvbContext;
extern PAVB_DEVICE_CONTEXT g_AvbContextList;
extern NDIS_SPIN_LOCK      g_AvbContextListLock;

/*========================================================================
 * Device lifecycle & IOCTL handling
 *=======================================================================*/
/**
 * @brief Initialize AVB device context and bind to an Intel miniport filter instance.
 * @param FilterModule Pointer to NDIS filter module instance.
 * @param AvbContext Pointer to receive allocated AVB device context.
 * @return NTSTATUS STATUS_SUCCESS on success or appropriate NTSTATUS error.
 */
NTSTATUS AvbInitializeDevice(
    _In_ PMS_FILTER FilterModule,
    _Outptr_ PAVB_DEVICE_CONTEXT *AvbContext
);

/**
 * @brief Cleanup and free an AVB device context.
 * @param AvbContext Device context returned by AvbInitializeDevice (may be NULL).
 */
VOID AvbCleanupDevice(
    _In_opt_ PAVB_DEVICE_CONTEXT AvbContext
);

/** * @brief Cleanup timestamp subscriptions for a file object (implicit unsubscribe on handle close).
 * @param FileObject File object being closed (from IRP_MJ_CLEANUP).
 * Implements: Issue #13 (REQ-F-TS-SUB-001) Task 5 - Option B
 */
VOID AvbCleanupFileSubscriptions(
    _In_ PFILE_OBJECT FileObject
);

/**
 * @brief Post timestamp event to all matching subscriptions (Task 6c).
 * @param event_type One of TS_EVENT_* constants (RX_TIMESTAMP, TX_TIMESTAMP, etc.)
 * @param timestamp_ns Hardware timestamp in nanoseconds
 * @param vlan_id VLAN ID (0xFFFF if no VLAN tag)
 * @param pcp Priority Code Point (0xFF if not applicable)
 * @param queue RX/TX queue number
 * @param packet_length Packet length in bytes
 * @param trigger_source Timer index or GPIO pin (for target time/aux events)
 * Implements: Issue #13 (REQ-F-TS-SUB-001) Task 6c - Event posting helper
 */
VOID AvbPostTimestampEvent(    _In_ PVOID AvbContextParam,    _In_ avb_u32 event_type,
    _In_ avb_u64 timestamp_ns,
    _In_ avb_u16 vlan_id,
    _In_ avb_u8 pcp,
    _In_ avb_u8 queue,
    _In_ avb_u16 packet_length,
    _In_ avb_u8 trigger_source,
    _In_ avb_i64 correction_field
);

/** * @brief Handle an incoming DeviceIoControl IRP targeting the AVB filter device.
 * @param AvbContext Device context.
 * @param Irp Pointer to IRP from I/O manager.
 * @return NTSTATUS from processing.
 */
/* Fast-path SEND_PTP core — called by both the IRP handler and FastIoDeviceControl.
 * Caller must have validated activeContext != NULL and test_req != NULL / accessible.
 * On success: test_req->timestamp_ns, ->packets_sent, ->status are written. */
NTSTATUS AvbSendPtpCore(
    _In_ PAVB_DEVICE_CONTEXT activeContext,
    _Inout_ PAVB_TEST_SEND_PTP_REQUEST test_req
);

NTSTATUS AvbHandleDeviceIoControl(
    _In_ PAVB_DEVICE_CONTEXT AvbContext,
    _In_ PIRP Irp
);

/*========================================================================
 * BAR0 hardware resource discovery (NDIS patterns)
 *=======================================================================*/
/**
 * @brief Discover Intel controller BAR0 physical address and length from filter module resources.
 * @param FilterModule NDIS filter module.
 * @param Bar0Address Output physical address of BAR0.
 * @param Bar0Length Output length (bytes) of BAR0 region.
 * @return NTSTATUS STATUS_SUCCESS or error.
 */
NTSTATUS AvbDiscoverIntelControllerResources(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
);

/**
 * @brief Convenience initializer that performs BAR0 discovery then device initialization.
 * @param FilterModule NDIS filter module.
 * @param AvbContext Receives created context.
 * @return NTSTATUS.
 */
NTSTATUS AvbInitializeDeviceWithBar0Discovery(
    _In_ PMS_FILTER FilterModule,
    _Outptr_ PAVB_DEVICE_CONTEXT *AvbContext
);

/**
 * @brief Alternative BAR0 discovery path (fallback enumeration strategy).
 * @param FilterModule NDIS filter module.
 * @param Bar0Address Output BAR0 physical address.
 * @param Bar0Length Output BAR0 size.
 * @return NTSTATUS.
 */
NTSTATUS AvbDiscoverIntelControllerResourcesAlternative(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
);

/*========================================================================
 * Real hardware memory mapping
 *=======================================================================*/
/**
 * @brief Map Intel controller MMIO register space (BAR0) into system virtual address space.
 * @param AvbContext Device context.
 * @param PhysicalAddress Physical BAR0 base.
 * @param Length Length (bytes) to map.
 * @return NTSTATUS STATUS_SUCCESS if mapped.
 */
NTSTATUS AvbMapIntelControllerMemory(
    _In_ PAVB_DEVICE_CONTEXT AvbContext,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length
);

/**
 * @brief Unmap previously mapped controller MMIO space.
 * @param AvbContext Device context.
 */
VOID AvbUnmapIntelControllerMemory(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
);

/*========================================================================
 * Low-level real hardware accessors
 *=======================================================================*/
/**
 * @brief Read 32-bit MMIO register from real hardware.
 * @param dev Intel library device wrapper.
 * @param offset Byte offset into BAR0.
 * @param value Output register value.
 * @return 0 on success, negative errno style on failure.
 */
int AvbMmioReadReal(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _Out_ ULONG *value
);

/**
 * @brief Write 32-bit MMIO register.
 */
int AvbMmioWriteReal(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _In_ ULONG value
);

/**
 * @brief Read raw IEEE 1588 timestamp registers and compose 64-bit value.
 */
int AvbReadTimestampReal(
    _In_ device_t *dev,
    _Out_ ULONGLONG *timestamp
);

/**
 * @brief Read DWORD from PCI configuration space (filter mediated).
 */
int AvbPciReadConfigReal(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _Out_ ULONG *value
);

/**
 * @brief Attempt to write PCI configuration dword (not supported in filter context).
 */
int AvbPciWriteConfigReal(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _In_ ULONG value
);

/*========================================================================
 * Platform wrapper ops (selected by Intel library)
 *=======================================================================*/
NTSTATUS AvbPlatformInit(
    _In_ device_t *dev
);
VOID AvbPlatformCleanup(
    _In_ device_t *dev
);
int AvbPciReadConfig(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _Out_ ULONG *value
);
int AvbPciWriteConfig(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _In_ ULONG value
);
int AvbMmioRead(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _Out_ ULONG *value
);
int AvbMmioWrite(
    _In_ device_t *dev,
    _In_ ULONG offset,
    _In_ ULONG value
);
int AvbMdioRead(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _Out_ USHORT *value
);
int AvbMdioWrite(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _In_ USHORT value
);
int AvbReadTimestamp(
    _In_ device_t *dev,
    _Out_ ULONGLONG *timestamp
);

/*========================================================================
 * MDIO accessors (real hardware)
 *=======================================================================*/
int AvbMdioReadReal(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _Out_ USHORT *value
);
int AvbMdioWriteReal(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _In_ USHORT value
);
int AvbMdioReadI219DirectReal(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _Out_ USHORT *value
);
int AvbMdioWriteI219DirectReal(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _In_ USHORT value
);

/* Legacy/direct wrappers (to be unified) */
int AvbMdioReadI219Direct(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _Out_ USHORT *value
);
int AvbMdioWriteI219Direct(
    _In_ device_t *dev,
    _In_ USHORT phy_addr,
    _In_ USHORT reg_addr,
    _In_ USHORT value
);

/*========================================================================
 * Helper / discovery utilities
 *=======================================================================*/
PMS_FILTER AvbFindIntelFilterModule(VOID);

BOOLEAN AvbIsIntelDevice(
    _In_ USHORT vendor_id,
    _In_ USHORT device_id
);
BOOLEAN AvbIsFilterIntelAdapter(
    _In_ PMS_FILTER FilterInstance
);
intel_device_type_t AvbGetIntelDeviceType(
    _In_ USHORT device_id
);
BOOLEAN AvbIsSupportedIntelController(
    _In_ PMS_FILTER FilterModule,
    _Out_opt_ USHORT* OutVendorId,
    _Out_opt_ USHORT* OutDeviceId
);

// Enhanced I210 PTP initialization and context management
NTSTATUS AvbI210EnsureSystimRunning(PAVB_DEVICE_CONTEXT context);
BOOLEAN AvbVerifyHardwareContext(PAVB_DEVICE_CONTEXT context);
NTSTATUS AvbForceContextReinitialization(PAVB_DEVICE_CONTEXT context);

#endif // _AVB_INTEGRATION_H_