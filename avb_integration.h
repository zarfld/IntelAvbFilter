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

// Forward declarations
typedef enum {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_I210,
    DEVICE_TYPE_I219,
    DEVICE_TYPE_I225,
    DEVICE_TYPE_I226
} intel_device_type_t;

typedef int clockid_t;

// Simple device structure for Intel AVB library compatibility
typedef struct {
    PVOID private_data;
    USHORT pci_vendor_id;
    USHORT pci_device_id;
    intel_device_type_t device_type;
} device_t, *pdevice_t;

// TSN Gate Control Entry
typedef struct {
    UCHAR gate_states;
    ULONG time_interval;
} tsn_gate_entry_t;

// TSN Configuration structures
struct tsn_tas_config {
    ULONGLONG base_time;
    ULONG cycle_time;
    ULONG cycle_extension;
    ULONG num_entries;
    tsn_gate_entry_t entries[16];
};

struct tsn_fp_config {
    UCHAR preemptible_queues;
    UCHAR express_queues;
    UCHAR express_mask;
    UCHAR preemption_enabled;
    USHORT additional_fragment_size;
    ULONG verify_disable_timeout;
    ULONG verify_enable_timeout;
};

struct tsn_ptm_config {
    BOOLEAN enable;
    BOOLEAN root_select;
    ULONG local_clock_granularity;
    ULONG effective_granularity;
    BOOLEAN enabled;
    ULONG timeout_value;
};

// Timespec structure
struct timespec {
    long tv_sec;
    long tv_nsec;
};

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
    struct tsn_ptm_config config;
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

// Hardware access functions for NDIS filter integration
NTSTATUS AvbPlatformInit(device_t *dev);
VOID AvbPlatformCleanup(device_t *dev);
int AvbPciReadConfig(device_t *dev, ULONG offset, PULONG value);
int AvbPciWriteConfig(device_t *dev, ULONG offset, ULONG value);
int AvbMmioRead(device_t *dev, ULONG offset, PULONG value);
int AvbMmioWrite(device_t *dev, ULONG offset, ULONG value);
int AvbMdioRead(device_t *dev, USHORT phy_addr, USHORT reg_addr, PUSHORT value);
int AvbMdioWrite(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
int AvbReadTimestamp(device_t *dev, PULONGLONG timestamp);

// I219-specific direct MDIO access
int AvbMdioReadI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, PUSHORT value);
int AvbMdioWriteI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);

// Stub function declarations for Intel AVB library functions  
int intel_init(device_t *dev);
void intel_detach(device_t *dev);
int intel_get_device_info(device_t *dev, char *buffer, size_t buffer_size);
int intel_read_reg(device_t *dev, ULONG offset, PULONG value);
int intel_write_reg(device_t *dev, ULONG offset, ULONG value);
int intel_gettime(device_t *dev, clockid_t clock_id, PULONGLONG timestamp, struct timespec *tp);
int intel_set_systime(device_t *dev, ULONGLONG timestamp);
int intel_setup_time_aware_shaper(device_t *dev, struct tsn_tas_config *config);
int intel_setup_frame_preemption(device_t *dev, struct tsn_fp_config *config);
int intel_setup_ptm(device_t *dev, struct tsn_ptm_config *config);
int intel_mdio_read(device_t *dev, ULONG page, ULONG reg, PUSHORT value);
int intel_mdio_write(device_t *dev, ULONG page, ULONG reg, USHORT value);

// Helper functions
PMS_FILTER AvbFindIntelFilterModule(void);
BOOLEAN AvbIsIntelDevice(USHORT vendor_id, USHORT device_id);
BOOLEAN AvbIsFilterIntelAdapter(PMS_FILTER FilterInstance);
intel_device_type_t AvbGetIntelDeviceType(USHORT device_id);

#endif // _AVB_INTEGRATION_H_