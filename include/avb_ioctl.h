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
#ifndef NDEBUG
/* Debug-only IOCTLs - Provide raw register access for diagnostics.
 * These are DISABLED in Release builds for security.
 * Use proper abstractions (ADJUST_FREQUENCY, GET_CLOCK_CONFIG) in production. */
#define IOCTL_AVB_READ_REGISTER         _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_WRITE_REGISTER        _NDIS_CONTROL_CODE(23, METHOD_BUFFERED)
#endif
#define IOCTL_AVB_GET_TIMESTAMP         _NDIS_CONTROL_CODE(24, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TIMESTAMP         _NDIS_CONTROL_CODE(25, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_TAS             _NDIS_CONTROL_CODE(26, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_FP              _NDIS_CONTROL_CODE(27, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_PTM             _NDIS_CONTROL_CODE(28, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_READ             _NDIS_CONTROL_CODE(29, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_WRITE            _NDIS_CONTROL_CODE(30, METHOD_BUFFERED)
#define IOCTL_AVB_ENUM_ADAPTERS         _NDIS_CONTROL_CODE(31, METHOD_BUFFERED)
#define IOCTL_AVB_OPEN_ADAPTER          _NDIS_CONTROL_CODE(32, METHOD_BUFFERED)
#define IOCTL_AVB_TS_SUBSCRIBE          _NDIS_CONTROL_CODE(33, METHOD_BUFFERED)
#define IOCTL_AVB_TS_RING_MAP           _NDIS_CONTROL_CODE(34, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_QAV             _NDIS_CONTROL_CODE(35, METHOD_BUFFERED)
#ifdef AVB_DEV_SIMULATION
#define IOCTL_AVB_REG_READ_UBER         _NDIS_CONTROL_CODE(36, METHOD_BUFFERED)
#endif
#define IOCTL_AVB_GET_HW_STATE          _NDIS_CONTROL_CODE(37, METHOD_BUFFERED)
#define IOCTL_AVB_ADJUST_FREQUENCY      _NDIS_CONTROL_CODE(38, METHOD_BUFFERED)
#define IOCTL_AVB_GET_CLOCK_CONFIG      _NDIS_CONTROL_CODE(39, METHOD_BUFFERED)
#define IOCTL_AVB_SET_HW_TIMESTAMPING   _NDIS_CONTROL_CODE(40, METHOD_BUFFERED)
#define IOCTL_AVB_SET_RX_TIMESTAMP      _NDIS_CONTROL_CODE(41, METHOD_BUFFERED)
#define IOCTL_AVB_SET_QUEUE_TIMESTAMP   _NDIS_CONTROL_CODE(42, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TARGET_TIME       _NDIS_CONTROL_CODE(43, METHOD_BUFFERED)
#define IOCTL_AVB_GET_AUX_TIMESTAMP     _NDIS_CONTROL_CODE(44, METHOD_BUFFERED)

/* Request/response structures (mirror of avb_integration.h) */
#define AVB_DEVICE_INFO_MAX 1024u

typedef struct AVB_DEVICE_INFO_REQUEST {
    char    device_info[AVB_DEVICE_INFO_MAX];
    avb_u32 buffer_size;  /* in/out: size of device_info used */
    avb_u32 status;       /* NDIS_STATUS value */
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

#ifndef NDEBUG
/* Raw register access for diagnostics only. Debug builds only.
 * Use IOCTL_AVB_ADJUST_FREQUENCY and IOCTL_AVB_GET_CLOCK_CONFIG in production. */
typedef struct AVB_REGISTER_REQUEST {
    avb_u32 offset;  /* MMIO offset */
    avb_u32 value;   /* in for WRITE, out for READ */
    avb_u32 status;  /* NDIS_STATUS value */
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;
#endif

typedef struct AVB_TIMESTAMP_REQUEST {
    avb_u64 timestamp; /* in for SET, out for GET */
    avb_u32 clock_id;  /* optional; 0 default */
    avb_u32 status;    /* NDIS_STATUS value */
} AVB_TIMESTAMP_REQUEST, *PAVB_TIMESTAMP_REQUEST;

/* Production clock frequency adjustment (replaces raw TIMINCA register access) */
typedef struct AVB_FREQUENCY_REQUEST {
    avb_u32 increment_ns;      /* Clock increment in nanoseconds per cycle (e.g., 8 for 8ns @ 125MHz) */
    avb_u32 increment_frac;    /* Fractional part (2^32 = 1ns, optional fine-tuning) */
    avb_u32 current_increment; /* out: current TIMINCA value before change */
    avb_u32 status;            /* NDIS_STATUS value */
} AVB_FREQUENCY_REQUEST, *PAVB_FREQUENCY_REQUEST;

/* Production clock configuration query (replaces raw register reads) */
typedef struct AVB_CLOCK_CONFIG {
    avb_u64 systim;            /* out: Current SYSTIM counter value */
    avb_u32 timinca;           /* out: Current clock increment configuration */
    avb_u32 tsauxc;            /* out: Auxiliary clock control register */
    avb_u32 clock_rate_mhz;    /* out: Base clock rate (125/156/200/250 MHz) */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_CLOCK_CONFIG, *PAVB_CLOCK_CONFIG;

/* RX packet timestamping enable (RXPBSIZE.CFG_TS_EN bit)
 * Allocates 16 bytes in packet buffer for hardware timestamp.
 * Must be set before enabling per-queue timestamping.
 * Requires port software reset (CTRL.RST) after changing.
 */
typedef struct AVB_RX_TIMESTAMP_REQUEST {
    avb_u32 enable;            /* in: 1=enable 16-byte timestamp in RX buffer, 0=disable */
    avb_u32 previous_rxpbsize; /* out: RXPBSIZE value before change */
    avb_u32 current_rxpbsize;  /* out: RXPBSIZE value after change */
    avb_u32 requires_reset;    /* out: 1=port reset required for change to take effect */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_RX_TIMESTAMP_REQUEST, *PAVB_RX_TIMESTAMP_REQUEST;

/* Per-queue timestamp enable (SRRCTL[n].TIMESTAMP bit)
 * Enables hardware timestamping for specific receive queue.
 * Requires RXPBSIZE.CFG_TS_EN=1 first.
 */
typedef struct AVB_QUEUE_TIMESTAMP_REQUEST {
    avb_u32 queue_index;       /* in: Queue index (0-3 for I210/I226) */
    avb_u32 enable;            /* in: 1=enable timestamping for this queue, 0=disable */
    avb_u32 previous_srrctl;   /* out: SRRCTL[n] value before change */
    avb_u32 current_srrctl;    /* out: SRRCTL[n] value after change */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_QUEUE_TIMESTAMP_REQUEST, *PAVB_QUEUE_TIMESTAMP_REQUEST;

/* Target time configuration (TRGTTIML/H registers)
 * Used for generating time-triggered interrupts or SDP output events.
 * When SYSTIM crosses the target time, interrupt/event is generated.
 */
typedef struct AVB_TARGET_TIME_REQUEST {
    avb_u32 timer_index;       /* in: Timer index (0 or 1) */
    avb_u64 target_time;       /* in: Target time in nanoseconds */
    avb_u32 enable_interrupt;  /* in: 1=enable interrupt when target reached */
    avb_u32 enable_sdp_output; /* in: 1=enable SDP pin output change */
    avb_u32 sdp_mode;          /* in: 0=level change, 1=pulse, 2=start clock */
    avb_u32 previous_target;   /* out: Previous target time (for verification) */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_TARGET_TIME_REQUEST, *PAVB_TARGET_TIME_REQUEST;

/* Auxiliary timestamp query (AUXSTMPL/H registers)
 * Read captured timestamp from auxiliary timestamp registers.
 * Captured when SDP pin event occurs (if EN_TS0/EN_TS1 enabled).
 */
typedef struct AVB_AUX_TIMESTAMP_REQUEST {
    avb_u32 timer_index;       /* in: Auxiliary timer index (0 or 1) */
    avb_u64 timestamp;         /* out: Captured timestamp in nanoseconds */
    avb_u32 valid;             /* out: 1=timestamp is valid (AUTT0/AUTT1 flag set) */
    avb_u32 clear_flag;        /* in: 1=clear AUTT flag after reading */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_AUX_TIMESTAMP_REQUEST, *PAVB_AUX_TIMESTAMP_REQUEST;

/* Hardware timestamping control (TSAUXC register) 
 * Based on Intel Foxville Ethernet Controller specification:
 * - Bit 31: Disable SYSTIM0 (primary timer)
 * - Bit 30: DIS_TS_CLEAR (auto-clear timestamp interrupt flags)
 * - Bit 29: Disable SYSTIM3
 * - Bit 28: Disable SYSTIM2
 * - Bit 27: Disable SYSTIM1
 * - Bits 10-11: AUTT1 (Auxiliary Timestamp 1)
 * - Bit 9: AUTT0 (Auxiliary Timestamp 0)
 * - Bit 8: EN_TS0 (Enable timestamp on SDP to AUXSTMP0)
 * - Bit 10: EN_TS1 (Enable timestamp on SDP to AUXSTMP1)
 * - Bit 4: EN_TT1 (Enable target time 1 interrupt)
 * - Bit 0: EN_TT0 (Enable target time 0 interrupt)
 */
typedef struct AVB_HW_TIMESTAMPING_REQUEST {
    avb_u32 enable;            /* in: 1=enable HW timestamping (clear bit 31), 0=disable (set bit 31) */
    avb_u32 timer_mask;        /* in: Bitmask of timers to enable (bit 0=SYSTIM0, bit 1=SYSTIM1, etc.) Default: 0x1 (SYSTIM0 only) */
    avb_u32 enable_target_time; /* in: 1=enable target time interrupts (EN_TT0/EN_TT1), 0=disable */
    avb_u32 enable_aux_ts;     /* in: 1=enable auxiliary timestamp capture on SDP pins, 0=disable */
    avb_u32 previous_tsauxc;   /* out: TSAUXC value before change */
    avb_u32 current_tsauxc;    /* out: TSAUXC value after change */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_HW_TIMESTAMPING_REQUEST, *PAVB_HW_TIMESTAMPING_REQUEST;

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

typedef struct AVB_QAV_REQUEST {
    avb_u8  tc;           /* traffic class */
    avb_u8  reserved1[3]; /* align */
    avb_u32 idle_slope;   /* bytes/sec */
    avb_u32 send_slope;   /* bytes/sec (two's complement if negative) */
    avb_u32 hi_credit;    /* bytes */
    avb_u32 lo_credit;    /* bytes */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_QAV_REQUEST, *PAVB_QAV_REQUEST;

/* Hardware state query (diagnostics) */
typedef struct AVB_HW_STATE_QUERY {
    avb_u32 hw_state;     /* AVB_HW_STATE enum value */
    avb_u16 vendor_id;
    avb_u16 device_id;
    avb_u32 capabilities; /* current published caps */
    avb_u32 reserved;     /* future use */
} AVB_HW_STATE_QUERY, *PAVB_HW_STATE_QUERY;

#ifdef __cplusplus
}
#endif
