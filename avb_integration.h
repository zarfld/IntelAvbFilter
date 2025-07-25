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

// Intel constants
#define INTEL_VENDOR_ID         0x8086

// Forward declarations using Windows kernel types
typedef enum {
    INTEL_DEVICE_I210,
    INTEL_DEVICE_I217,
    INTEL_DEVICE_I219,
    INTEL_DEVICE_I225,
    INTEL_DEVICE_I226,
    INTEL_DEVICE_UNKNOWN
} intel_device_type_t;

typedef struct _device_t {
    PVOID private_data;
    PUCHAR hw_addr;             // Hardware MMIO base address
    USHORT pci_vendor_id;
    USHORT pci_device_id;
    USHORT domain;
    UCHAR bus;
    UCHAR dev;
    UCHAR func;
    intel_device_type_t device_type;
    ULONG capabilities;
} device_t;

typedef int clockid_t;

// Hardware context for real MMIO access
typedef struct _INTEL_HARDWARE_CONTEXT INTEL_HARDWARE_CONTEXT, *PINTEL_HARDWARE_CONTEXT;

// TSN Configuration structures using Windows kernel types
struct tsn_tas_config {
    ULONGLONG base_time_s;
    ULONG base_time_ns;
    ULONG cycle_time_s;
    ULONG cycle_time_ns;
    UCHAR gate_states[8];
    ULONG gate_durations[8];
};

struct tsn_fp_config {
    UCHAR preemptable_queues;
    ULONG min_fragment_size;
    UCHAR verify_disable;
};

struct ptm_config {
    UCHAR enabled;
    ULONG clock_granularity;
};

struct timespec {
    LONG tv_sec;
    LONG tv_nsec;
};

// Intel register definitions (from I210/IGB specifications)
#define E1000_SYSTIMR           0x0B6F8  /* System time register Residue (latch) */
#define E1000_SYSTIML           0x0B600  /* System time register Low */
#define E1000_SYSTIMH           0x0B604  /* System time register High */
#define E1000_TIMINCA           0x0B608  /* Increment attributes register */

// I219 register definitions for direct MDIO access
#define I219_REG_MDIO_CTRL      0x12018
#define I219_REG_MDIO_DATA      0x1201C
#define I219_REG_1588_TS_LOW    0x15F84
#define I219_REG_1588_TS_HIGH   0x15F88

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

// IOCTL data structures
typedef struct _AVB_DEVICE_INFO_REQUEST {
    CHAR device_info[MAX_AVB_DEVICE_INFO_SIZE];
    ULONG buffer_size;
    NDIS_STATUS status;
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

typedef struct _AVB_REGISTER_REQUEST {
    ULONG offset;
    ULONG value;
    NDIS_STATUS status;
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

typedef struct _AVB_TIMESTAMP_REQUEST {
    ULONGLONG timestamp;
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
    ULONG page;
    ULONG reg;
    USHORT value;
    NDIS_STATUS status;
} AVB_MDIO_REQUEST, *PAVB_MDIO_REQUEST;

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

#endif // _AVB_INTEGRATION_H_