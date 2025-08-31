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
__forceinline const char* AvbHwStateName(AVB_HW_STATE s) {
    switch (s) {
    case AVB_HW_UNBOUND: return "UNBOUND"; 
    case AVB_HW_BOUND: return "BOUND"; 
    case AVB_HW_BAR_MAPPED: return "BAR_MAPPED"; 
    case AVB_HW_PTP_READY: return "PTP_READY"; 
    default: return "?"; }
}

/* Forward decl */
typedef struct _INTEL_HARDWARE_CONTEXT INTEL_HARDWARE_CONTEXT, *PINTEL_HARDWARE_CONTEXT;

// AVB device context structure
typedef struct _AVB_DEVICE_CONTEXT {
    device_t intel_device;
    BOOLEAN initialized;
    PDEVICE_OBJECT filter_device;
    PMS_FILTER filter_instance;
    BOOLEAN hw_access_enabled;
    NDIS_HANDLE miniport_handle;
    PINTEL_HARDWARE_CONTEXT hardware_context;  // Real hardware access context

    // Hardware lifecycle state
    AVB_HW_STATE hw_state;

    // ABI and capabilities tracking
    ULONG last_seen_abi_version;

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
} AVB_DEVICE_CONTEXT, *PAVB_DEVICE_CONTEXT;

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

/**
 * @brief Handle an incoming DeviceIoControl IRP targeting the AVB filter device.
 * @param AvbContext Device context.
 * @param Irp Pointer to IRP from I/O manager.
 * @return NTSTATUS from processing.
 */
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

#endif // _AVB_INTEGRATION_H_