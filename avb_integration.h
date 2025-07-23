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

// Include Intel AVB library headers
#include "external\intel_avb\lib\intel.h"
#include "external\intel_avb\lib\intel_windows.h"

// AVB-specific IOCTLs
#define IOCTL_AVB_INIT_DEVICE           _NDIS_CONTROL_CODE(20, METHOD_BUFFERED)
#define IOCTL_AVB_GET_DEVICE_INFO       _NDIS_CONTROL_CODE(21, METHOD_BUFFERED)
#define IOCTL_AVB_READ_REGISTER         _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_WRITE_REGISTER        _NDIS_CONTROL_CODE(23, METHOD_BUFFERED)
#define IOCTL_AVB_GET_TIMESTAMP         _NDIS_CONTROL_CODE(24, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TIMESTAMP         _NDIS_CONTROL_CODE(25, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_TAS             _NDIS_CONTROL_CODE(26, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_FP              _NDIS_CONTROL_CODE(27, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_PTM             _NDIS_CONTROL_CODE(28, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_READ             _NDIS_CONTROL_CODE(29, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_WRITE            _NDIS_CONTROL_CODE(30, METHOD_BUFFERED)

// Maximum device info buffer size
#define MAX_AVB_DEVICE_INFO_SIZE        1024

// NDIS PHY access structures (define if not available)
#ifndef NDIS_REQUEST_PHY_READ_DEFINED
typedef struct _NDIS_REQUEST_PHY_READ {
    ULONG PhyAddress;
    ULONG RegisterAddress;
    ULONG DeviceAddress;
    USHORT Value;
} NDIS_REQUEST_PHY_READ, *PNDIS_REQUEST_PHY_READ;

typedef struct _NDIS_REQUEST_PHY_WRITE {
    ULONG PhyAddress;
    ULONG RegisterAddress;
    ULONG DeviceAddress;
    USHORT Value;
} NDIS_REQUEST_PHY_WRITE, *PNDIS_REQUEST_PHY_WRITE;

// Define PHY OIDs if not available
#define OID_GEN_PHY_READ                0x00020201
#define OID_GEN_PHY_WRITE               0x00020202

#define NDIS_REQUEST_PHY_READ_DEFINED
#endif

// AVB device context structure
typedef struct _AVB_DEVICE_CONTEXT {
    device_t intel_device;
    BOOLEAN initialized;
    PDEVICE_OBJECT filter_device;
    PMS_FILTER filter_instance;
    BOOLEAN hw_access_enabled;
    NDIS_HANDLE miniport_handle;
} AVB_DEVICE_CONTEXT, *PAVB_DEVICE_CONTEXT;

// IOCTL data structures
typedef struct _AVB_DEVICE_INFO_REQUEST {
    CHAR device_info[MAX_AVB_DEVICE_INFO_SIZE];
    ULONG buffer_size;
    NDIS_STATUS status;
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

typedef struct _AVB_REGISTER_REQUEST {
    UINT32 offset;
    UINT32 value;
    NDIS_STATUS status;
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

typedef struct _AVB_TIMESTAMP_REQUEST {
    UINT64 timestamp;
    clockid_t clock_id;
    NDIS_STATUS status;
} AVB_TIMESTAMP_REQUEST, *PAVB_TIMESTAMP_REQUEST;

typedef struct _AVB_TAS_REQUEST {
    struct tsn_tas_config config;
    NDIS_STATUS status;
} AVB_TAS_REQUEST, *PAVB_TAS_REQUEST;

typedef struct _AVB_FP_REQUEST {
    struct tsn_fp_config config;
    NDIS_STATUS status;
} AVB_FP_REQUEST, *PAVB_FP_REQUEST;

typedef struct _AVB_PTM_REQUEST {
    struct ptm_config config;
    NDIS_STATUS status;
} AVB_PTM_REQUEST, *PAVB_PTM_REQUEST;

typedef struct _AVB_MDIO_REQUEST {
    UINT32 page;
    UINT32 reg;
    UINT16 value;
    NDIS_STATUS status;
} AVB_MDIO_REQUEST, *PAVB_MDIO_REQUEST;

// Function prototypes
NTSTATUS AvbInitializeDevice(PMS_FILTER FilterModule, PAVB_DEVICE_CONTEXT *AvbContext);
VOID AvbCleanupDevice(PAVB_DEVICE_CONTEXT AvbContext);
NTSTATUS AvbHandleDeviceIoControl(PAVB_DEVICE_CONTEXT AvbContext, PIRP Irp);

// Hardware access functions for NDIS filter integration
NTSTATUS AvbPlatformInit(device_t *dev);
VOID AvbPlatformCleanup(device_t *dev);
int AvbPciReadConfig(device_t *dev, DWORD offset, DWORD *value);
int AvbPciWriteConfig(device_t *dev, DWORD offset, DWORD value);
int AvbMmioRead(device_t *dev, uint32_t offset, uint32_t *value);
int AvbMmioWrite(device_t *dev, uint32_t offset, uint32_t value);
int AvbMdioRead(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value);
int AvbMdioWrite(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value);
int AvbReadTimestamp(device_t *dev, uint64_t *timestamp);

// I219-specific direct MDIO access
int AvbMdioReadI219Direct(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value);
int AvbMdioWriteI219Direct(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value);

// Helper functions
PMS_FILTER AvbFindIntelFilterModule(void);
BOOLEAN AvbIsIntelDevice(UINT16 vendor_id, UINT16 device_id);
BOOLEAN AvbIsFilterIntelAdapter(PMS_FILTER FilterInstance);
intel_device_type_t AvbGetIntelDeviceType(UINT16 device_id);

#endif // _AVB_INTEGRATION_H_
