#pragma once

/*
 * Shared IOCTL ABI for Intel AVB Filter Driver
 * Used by kernel-mode driver and user-mode clients (AVBServices, tests)
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ABI versioning for coordination across components */
#define AVB_IOCTL_ABI_VERSION 0x00010000u

/* Basic fixed-width types for UM/KM */
#if defined(_KERNEL_MODE)
  #include <ntdef.h>
  typedef UINT8   avb_u8;
  typedef UINT16  avb_u16;
  typedef UINT32  avb_u32;
  typedef UINT64  avb_u64;
#else
  #include <stdint.h>
  typedef uint8_t  avb_u8;
  typedef uint16_t avb_u16;
  typedef uint32_t avb_u32;
  typedef uint64_t avb_u64;
#endif

/* IOCTL macro (METHOD_BUFFERED) */
#ifndef _NDIS_CONTROL_CODE
  #include <winioctl.h>
  #define _NDIS_CONTROL_CODE(Request,Method) \
          CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, (Request), (Method), FILE_ANY_ACCESS)
#endif

/* AVB-specific IOCTLs (must match kernel) */
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

/* Request/response structures (mirror of avb_integration.h) */
#define AVB_DEVICE_INFO_MAX 1024u

typedef struct AVB_DEVICE_INFO_REQUEST {
    char    device_info[AVB_DEVICE_INFO_MAX];
    avb_u32 buffer_size;  /* in/out: size of device_info used */
    avb_u32 status;       /* NDIS_STATUS value */
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

typedef struct AVB_REGISTER_REQUEST {
    avb_u32 offset;  /* MMIO offset */
    avb_u32 value;   /* in for WRITE, out for READ */
    avb_u32 status;  /* NDIS_STATUS value */
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

typedef struct AVB_TIMESTAMP_REQUEST {
    avb_u64 timestamp; /* in for SET, out for GET */
    avb_u32 clock_id;  /* optional; 0 default */
    avb_u32 status;    /* NDIS_STATUS value */
} AVB_TIMESTAMP_REQUEST, *PAVB_TIMESTAMP_REQUEST;

/* TSN/FP/PTM config structures */
typedef struct tsn_tas_config {
    avb_u64 base_time_s;
    avb_u32 base_time_ns;
    avb_u32 cycle_time_s;
    avb_u32 cycle_time_ns;
    avb_u8  gate_states[8];
    avb_u32 gate_durations[8];
} tsn_tas_config;

typedef struct tsn_fp_config {
    avb_u8  preemptable_queues;
    /* padding to 4-byte alignment occurs naturally */
    avb_u32 min_fragment_size;
    avb_u8  verify_disable;
    /* implicit padding to 12 bytes total under default packing */
} tsn_fp_config;

typedef struct ptm_config {
    avb_u8  enabled;
    avb_u32 clock_granularity;
    /* implicit padding to 8 bytes total under default packing */
} ptm_config;

typedef struct AVB_TAS_REQUEST {
    tsn_tas_config config;
    avb_u32        status; /* NDIS_STATUS value */
} AVB_TAS_REQUEST, *PAVB_TAS_REQUEST;

typedef struct AVB_FP_REQUEST {
    tsn_fp_config  config;
    avb_u32        status; /* NDIS_STATUS value */
} AVB_FP_REQUEST, *PAVB_FP_REQUEST;

typedef struct AVB_PTM_REQUEST {
    ptm_config     config;
    avb_u32        status; /* NDIS_STATUS value */
} AVB_PTM_REQUEST, *PAVB_PTM_REQUEST;

typedef struct AVB_MDIO_REQUEST {
    avb_u32 page;
    avb_u32 reg;
    avb_u16 value;   /* out for READ, in for WRITE */
    /* natural padding to 4-byte alignment before status */
    avb_u32 status;  /* NDIS_STATUS value */
} AVB_MDIO_REQUEST, *PAVB_MDIO_REQUEST;

#ifdef __cplusplus
}
#endif
