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

/* Bring in SSOT TSN/PTM types */
/* Relative include path (header lives in include/, external path is sibling) */
#include "../external/intel_avb/lib/intel.h"

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

/* Optional request header carried at the start of IOCTL payloads to convey ABI info */
typedef struct AVB_REQUEST_HEADER {
    avb_u32 abi_version;   /* must match AVB_IOCTL_ABI_VERSION major */
    avb_u32 header_size;   /* sizeof(AVB_REQUEST_HEADER) when present */
} AVB_REQUEST_HEADER, *PAVB_REQUEST_HEADER;

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

/* Recommended additions */
#define IOCTL_AVB_ENUM_ADAPTERS         _NDIS_CONTROL_CODE(31, METHOD_BUFFERED)
#define IOCTL_AVB_OPEN_ADAPTER          _NDIS_CONTROL_CODE(32, METHOD_BUFFERED)
#define IOCTL_AVB_TS_SUBSCRIBE          _NDIS_CONTROL_CODE(33, METHOD_BUFFERED)
#define IOCTL_AVB_TS_RING_MAP           _NDIS_CONTROL_CODE(34, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_QAV             _NDIS_CONTROL_CODE(35, METHOD_BUFFERED)
#ifdef AVB_DEV_SIMULATION
#define IOCTL_AVB_REG_READ_UBER         _NDIS_CONTROL_CODE(36, METHOD_BUFFERED)
#endif

/* Request/response structures (mirror of avb_integration.h) */
#define AVB_DEVICE_INFO_MAX 1024u

typedef struct AVB_DEVICE_INFO_REQUEST {
    char    device_info[AVB_DEVICE_INFO_MAX];
    avb_u32 buffer_size;  /* in/out: size of device_info used */
    avb_u32 status;       /* NDIS_STATUS value */
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

/* Raw register access for diagnostics only. Prefer higher-level IOCTLs in production. */
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

/* Use SSOT config types from external/intel_avb/lib/intel.h; do not redefine here. */

typedef struct AVB_TAS_REQUEST {
    struct tsn_tas_config  config;
    avb_u32                status; /* NDIS_STATUS value */
} AVB_TAS_REQUEST, *PAVB_TAS_REQUEST;

typedef struct AVB_FP_REQUEST {
    struct tsn_fp_config   config;
    avb_u32                status; /* NDIS_STATUS value */
} AVB_FP_REQUEST, *PAVB_FP_REQUEST;

typedef struct AVB_PTM_REQUEST {
    struct ptm_config      config;
    avb_u32                status; /* NDIS_STATUS value */
} AVB_PTM_REQUEST, *PAVB_PTM_REQUEST;

typedef struct AVB_MDIO_REQUEST {
    avb_u32 page;
    avb_u32 reg;
    avb_u16 value;   /* out for READ, in for WRITE */
    /* natural padding to 4-byte alignment before status */
    avb_u32 status;  /* NDIS_STATUS value */
} AVB_MDIO_REQUEST, *PAVB_MDIO_REQUEST;

/* Enumeration and adapter selection */
typedef struct AVB_ENUM_REQUEST {
    avb_u32 index;        /* in: adapter index to query (0..N-1) */
    avb_u32 count;        /* out: total adapter count */
    avb_u16 vendor_id;    /* out */
    avb_u16 device_id;    /* out */
    avb_u32 capabilities; /* out: INTEL_CAP_* bitmask */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_ENUM_REQUEST, *PAVB_ENUM_REQUEST;

typedef struct AVB_OPEN_REQUEST {
    avb_u16 vendor_id;    /* in */
    avb_u16 device_id;    /* in */
    avb_u32 reserved;     /* align */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_OPEN_REQUEST, *PAVB_OPEN_REQUEST;

/* Timestamp event subscription/ring mapping */
typedef struct AVB_TS_SUBSCRIBE_REQUEST {
    avb_u32 types_mask;   /* in: bitmask of event types */
    avb_u16 vlan;         /* in: optional filter */
    avb_u8  pcp;          /* in: optional filter */
    avb_u8  reserved0;    /* align */
    avb_u32 ring_id;      /* in/out: ring identifier */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_TS_SUBSCRIBE_REQUEST, *PAVB_TS_SUBSCRIBE_REQUEST;

typedef struct AVB_TS_RING_MAP_REQUEST {
    avb_u32 ring_id;      /* in */
    avb_u32 length;       /* in/out: requested/actual length in bytes */
    avb_u64 user_cookie;  /* in: opaque UM cookie; KM echoes back */
    avb_u64 shm_token;    /* out: opaque token to map shared buffer (HANDLE value on UM) */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_TS_RING_MAP_REQUEST, *PAVB_TS_RING_MAP_REQUEST;

/* Credit-Based Shaper (Qav) */
typedef struct AVB_QAV_REQUEST {
    avb_u8  tc;           /* traffic class */
    avb_u8  reserved1[3]; /* align */
    avb_u32 idle_slope;   /* bytes/sec */
    avb_u32 send_slope;   /* bytes/sec (two's complement if negative) */
    avb_u32 hi_credit;    /* bytes */
    avb_u32 lo_credit;    /* bytes */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_QAV_REQUEST, *PAVB_QAV_REQUEST;

#ifdef __cplusplus
}
#endif
