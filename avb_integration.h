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

// Hardware context for real MMIO access
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
} AVB_DEVICE_CONTEXT, *PAVB_DEVICE_CONTEXT;

// Function prototypes
NTSTATUS AvbInitializeDevice(PMS_FILTER FilterModule, PAVB_DEVICE_CONTEXT *AvbContext);
VOID AvbCleanupDevice(PAVB_DEVICE_CONTEXT AvbContext);
NTSTATUS AvbHandleDeviceIoControl(PAVB_DEVICE_CONTEXT AvbContext, PIRP Irp);

// BAR0 hardware resource discovery functions (NEW - Microsoft NDIS patterns)
NTSTATUS AvbDiscoverIntelControllerResources(PMS_FILTER FilterModule, PPHYSICAL_ADDRESS Bar0Address, PULONG Bar0Length);
NTSTATUS AvbInitializeDeviceWithBar0Discovery(PMS_FILTER FilterModule, PAVB_DEVICE_CONTEXT *AvbContext);
NTSTATUS AvbDiscoverIntelControllerResourcesAlternative(PMS_FILTER FilterModule, PPHYSICAL_ADDRESS Bar0Address, PULONG Bar0Length);

// Real hardware access functions (REPLACES simulation)
NTSTATUS AvbMapIntelControllerMemory(PAVB_DEVICE_CONTEXT AvbContext, PHYSICAL_ADDRESS PhysicalAddress, ULONG Length);
VOID AvbUnmapIntelControllerMemory(PAVB_DEVICE_CONTEXT AvbContext);
int AvbMmioReadReal(device_t *dev, ULONG offset, ULONG *value);
int AvbMmioWriteReal(device_t *dev, ULONG offset, ULONG value);
int AvbReadTimestampReal(device_t *dev, ULONGLONG *timestamp);
int AvbPciReadConfigReal(device_t *dev, ULONG offset, ULONG *value);
int AvbPciWriteConfigReal(device_t *dev, ULONG offset, ULONG value);

// Hardware access functions for NDIS filter integration (wrapper functions)
NTSTATUS AvbPlatformInit(device_t *dev);
VOID AvbPlatformCleanup(device_t *dev);
int AvbPciReadConfig(device_t *dev, ULONG offset, ULONG *value);
int AvbPciWriteConfig(device_t *dev, ULONG offset, ULONG value);
int AvbMmioRead(device_t *dev, ULONG offset, ULONG *value);
int AvbMmioWrite(device_t *dev, ULONG offset, ULONG value);
int AvbMdioRead(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWrite(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
int AvbReadTimestamp(device_t *dev, ULONGLONG *timestamp);

// MDIO access functions (REAL implementations for different devices)
int AvbMdioReadReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWriteReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
int AvbMdioReadI219DirectReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWriteI219DirectReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);

// I219-specific direct MDIO access (legacy wrapper functions)
int AvbMdioReadI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWriteI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);

// Helper functions
PMS_FILTER AvbFindIntelFilterModule(void);
BOOLEAN AvbIsIntelDevice(USHORT vendor_id, USHORT device_id);
BOOLEAN AvbIsFilterIntelAdapter(PMS_FILTER FilterInstance);
intel_device_type_t AvbGetIntelDeviceType(USHORT device_id);
BOOLEAN AvbIsSupportedIntelController(PMS_FILTER FilterModule, USHORT* OutVendorId, USHORT* OutDeviceId);

#endif // _AVB_INTEGRATION_H_