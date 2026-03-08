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
  typedef INT64   avb_i64;
#else
  #include <stdint.h>
  typedef uint8_t   avb_u8;
  typedef uint16_t  avb_u16;
  typedef uint32_t  avb_u32;
  typedef uint64_t  avb_u64;
  typedef int64_t   avb_i64;
#endif

/* Optional request header carried at the start of IOCTL payloads to convey ABI info */
typedef struct AVB_REQUEST_HEADER {
    avb_u32 abi_version;   /* must match AVB_IOCTL_ABI_VERSION major */
    avb_u32 header_size;   /* sizeof(AVB_REQUEST_HEADER) when present */
} AVB_REQUEST_HEADER, *PAVB_REQUEST_HEADER;

/* Driver version query response (Issue #64, #273)
 * Used by IOCTL_AVB_GET_VERSION (0x9C40A000)
 * No input buffer required.
 * 
 * Version format: Major.Minor.Build.Revision
 *   Major.Minor = API version (manually set when API changes)
 *   Build       = Incrementing build counter (set by CI/CD or build infrastructure)
 *   Revision    = Source control commit count or patch number (set by CI/CD)
 * 
 * Build and Revision default to 0 if not set by build infrastructure.
 * See src/avb_version.h for infrastructure integration.
 */
typedef struct _IOCTL_VERSION {
    avb_u16 Major;     /* API version - breaking changes (manually set) */
    avb_u16 Minor;     /* API version - compatible additions (manually set) */
    avb_u16 Build;     /* Incrementing build counter (default: 0) */
    avb_u16 Revision;  /* Source control commit count or patch number (default: 0) */
} IOCTL_VERSION, *PIOCTL_VERSION;

/* IOCTL macro (METHOD_BUFFERED) */
#ifndef _NDIS_CONTROL_CODE
  #include <winioctl.h>
  #define _NDIS_CONTROL_CODE(Request,Method) \
          CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, (Request), (Method), FILE_ANY_ACCESS)
#endif

/* AVB-specific IOCTLs (must match kernel) */
/* Version query IOCTL - MUST be first (Issue #64, #273) */
#define IOCTL_AVB_GET_VERSION           _NDIS_CONTROL_CODE(0x800, METHOD_BUFFERED)  /* 0x9C40A000 */
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
#define IOCTL_AVB_TS_UNSUBSCRIBE        _NDIS_CONTROL_CODE(46, METHOD_BUFFERED)  /* Cleanup subscription slot */
#define IOCTL_AVB_SETUP_QAV             _NDIS_CONTROL_CODE(35, METHOD_BUFFERED)
#ifdef AVB_DEV_SIMULATION
#define IOCTL_AVB_REG_READ_UBER         _NDIS_CONTROL_CODE(36, METHOD_BUFFERED)
#endif
#define IOCTL_AVB_GET_HW_STATE          _NDIS_CONTROL_CODE(37, METHOD_BUFFERED)
#define IOCTL_AVB_ADJUST_FREQUENCY      _NDIS_CONTROL_CODE(38, METHOD_BUFFERED)
#define IOCTL_AVB_GET_CLOCK_CONFIG      _NDIS_CONTROL_CODE(45, METHOD_BUFFERED)  /* Changed from 39 to 45 - testing if 0x9C blocked */
#define IOCTL_AVB_SET_HW_TIMESTAMPING   _NDIS_CONTROL_CODE(40, METHOD_BUFFERED)
#define IOCTL_AVB_SET_RX_TIMESTAMP      _NDIS_CONTROL_CODE(41, METHOD_BUFFERED)
#define IOCTL_AVB_SET_QUEUE_TIMESTAMP   _NDIS_CONTROL_CODE(42, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TARGET_TIME       _NDIS_CONTROL_CODE(47, METHOD_BUFFERED)  /* Use code 47 (next available after 46) - bypasses Windows interception */
#define IOCTL_AVB_GET_AUX_TIMESTAMP     _NDIS_CONTROL_CODE(44, METHOD_BUFFERED)
#define IOCTL_AVB_PHC_OFFSET_ADJUST     _NDIS_CONTROL_CODE(48, METHOD_BUFFERED)  /* PHC time offset adjustment (nanosecond-precision) */
#define IOCTL_AVB_GET_TX_TIMESTAMP      _NDIS_CONTROL_CODE(49, METHOD_BUFFERED)  /* Get TX timestamp for performance testing */
#define IOCTL_AVB_GET_RX_TIMESTAMP      _NDIS_CONTROL_CODE(50, METHOD_BUFFERED)  /* Get RX timestamp for performance testing */
#define IOCTL_AVB_TEST_SEND_PTP         _NDIS_CONTROL_CODE(51, METHOD_BUFFERED)  /* Test IOCTL: Send PTP packet from kernel (bypasses Npcap bypass issue) */
/* IEEE 802.1AS-2020 §11.3: timestampCorrectionPortDS — per-port ingress/egress latency calibration.
 * The driver adds ingress_latency_ns to every RX hardware timestamp and egress_latency_ns to every
 * TX hardware timestamp so that user-mode gPTP stacks receive boundary-corrected timestamps without
 * needing to know NIC-specific PHY latency values.
 * Both values are signed so that negative latencies (early timestamp capture) are representable.
 * Typical range: ±200 ns.  Persists until the driver is unloaded or a new SET_PORT_LATENCY is issued. */
#define IOCTL_AVB_SET_PORT_LATENCY      _NDIS_CONTROL_CODE(52, METHOD_BUFFERED)

/* Driver statistics query — implements #270 (TEST-STATISTICS-001) */
/* Function 0x808 → 0x9C40A020 (matches test hardcoded value) */
#define IOCTL_AVB_GET_STATISTICS        _NDIS_CONTROL_CODE(0x808, METHOD_BUFFERED)  /* 0x9C40A020 */
/* Function 0x80A → 0x9C40A028 (optional reset) */
#define IOCTL_AVB_RESET_STATISTICS      _NDIS_CONTROL_CODE(0x80A, METHOD_BUFFERED)  /* 0x9C40A028 */

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

/* PHC time offset adjustment (nanosecond-precision time correction) */
typedef struct AVB_OFFSET_REQUEST {
    INT64   offset_ns;  /* in: Offset in nanoseconds (positive or negative) */
    avb_u32 status;     /* out: NDIS_STATUS value */
} AVB_OFFSET_REQUEST, *PAVB_OFFSET_REQUEST;

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

/* TX timestamp retrieval (TXSTMPL/H registers)
 * Read TX timestamp from hardware FIFO.
 * Implements: Issue #35 (REQ-F-IOCTL-TS-001) - TX timestamp retrieval
 * 
 * Hardware behavior:
 * - When packet with 2STEP_1588 flag is transmitted, hardware latches SYSTIM into TXSTMPL/H
 * - Bit 31 of TXSTMPH indicates valid timestamp in FIFO
 * - Reading TXSTMPL advances FIFO to next entry
 * - FIFO depth: typically 4-8 entries (device-specific)
 * 
 * Critical: Must read TXSTMPL before TXSTMPH to unlock registers for next capture
 */
typedef struct AVB_TX_TIMESTAMP_REQUEST {
    avb_u64 timestamp_ns;      /* out: TX timestamp in nanoseconds (from TXSTMPL/H) */
    avb_u32 valid;             /* out: 1=timestamp valid (FIFO had entry), 0=FIFO empty */
    avb_u32 sequence_id;       /* out: Packet sequence ID (if tracking enabled) */
    avb_u32 adapter_index;     /* in: Adapter index (0-based, for multi-adapter systems) */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_TX_TIMESTAMP_REQUEST, *PAVB_TX_TIMESTAMP_REQUEST;

/* Test IOCTL: Send PTP packet from kernel
 * Implements: Step 8 (Kernel-Mode Test Packet Injection)
 * 
 * Purpose: Solves Npcap bypass issue by injecting PTP packets directly
 *          from kernel mode via NdisFSendNetBufferLists, ensuring filter
 *          driver can attach hardware timestamping metadata.
 * 
 * Workflow:
 * 1. User-mode test calls IOCTL_AVB_TEST_SEND_PTP
 * 2. Kernel allocates NET_BUFFER_LIST and crafts PTP Sync frame
 * 3. Calls NdisFSendNetBufferLists → routes through FilterSendNetBufferLists
 * 4. Filter detects EtherType 0x88F7 → attaches NdisHardwareTimestampInfo metadata
 * 5. Miniport driver sets 2STEP_1588 descriptor bit → hardware captures TX timestamp
 * 6. Test retrieves timestamp via IOCTL_AVB_GET_TX_TIMESTAMP (49)
 */
typedef struct AVB_TEST_SEND_PTP_REQUEST {
    avb_u32 adapter_index;     /* in: Adapter index (0-based, for multi-adapter systems) */
    avb_u32 sequence_id;       /* in: PTP sequence ID for packet tracking */
    avb_u32 packets_sent;      /* out: Number of packets successfully queued for transmission */
    avb_u32 status;            /* out: NDIS_STATUS value */
    avb_u64 timestamp_ns;      /* out: Pre-send timestamp (SYSTIM or KeQPC) — zero if send failed */
} AVB_TEST_SEND_PTP_REQUEST, *PAVB_TEST_SEND_PTP_REQUEST;

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
    avb_u32 index;        /* in: adapter instance index (0-based) for multi-adapter */
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

typedef struct AVB_TS_UNSUBSCRIBE_REQUEST {
    avb_u32 ring_id;      /* in: Subscription ID to clean up */
    avb_u32 status;       /* out: NDIS_STATUS */
} AVB_TS_UNSUBSCRIBE_REQUEST, *PAVB_TS_UNSUBSCRIBE_REQUEST;

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

/*==============================================================================
 * Ring Buffer Structures (Issue #13 - Timestamp Event Subscription)
 *============================================================================*/

/* Timestamp event types (bitmask for types_mask) */
#define TS_EVENT_RX_TIMESTAMP      0x00000001  /* RX packet timestamped */
#define TS_EVENT_TX_TIMESTAMP      0x00000002  /* TX packet complete + timestamped */
#define TS_EVENT_TARGET_TIME       0x00000004  /* Target time reached (SDP0) */
#define TS_EVENT_AUX_TIMESTAMP     0x00000008  /* Aux timestamp captured (GPIO) */
#define TS_EVENT_ERROR             0x00000010  /* Timestamp error (overflow, invalid) */

/* Timestamp event entry (written to ring buffer by driver ISR) */
typedef struct AVB_TIMESTAMP_EVENT {
    avb_u64  timestamp_ns;      /* [0-7]   Hardware timestamp (System Time Register in ns) */
    avb_u32  event_type;        /* [8-11]  One of TS_EVENT_* constants */
    avb_u32  sequence_num;      /* [12-15] Monotonically increasing sequence number */
    avb_u16  vlan_id;           /* [16-17] VLAN tag (if present, else INTEL_MASK_16BIT) */
    avb_u8   pcp;               /* [18]    Priority Code Point (802.1Q) */
    avb_u8   queue;             /* [19]    Tx/Rx queue number */
    avb_u16  packet_length;     /* [20-21] Packet length (bytes) */
    avb_u8   trigger_source;    /* [22]    PTP message type / Target time source / GPIO pin */
    avb_u8   reserved[1];       /* [23]    Alignment pad */
    /* IEEE 1588-2019 §9.5 correctionField: signed 64-bit fixed-point, 2^-16 ns units.
     * Positive or NEGATIVE — MUST be declared INT64 (signed), not UINT64.
     * To convert to nanoseconds: correctionField >> 16 (arithmetic right-shift). */
    avb_i64  correction_field;  /* [24-31] PTP correctionField from packet header (0 if N/A) */
} AVB_TIMESTAMP_EVENT, *PAVB_TIMESTAMP_EVENT;

/* Ring buffer header (lock-free producer/consumer) 
 * 
 * Layout in memory:
 *   [AVB_TIMESTAMP_RING_HEADER] (64 bytes)
 *   [AVB_TIMESTAMP_EVENT[0]]
 *   [AVB_TIMESTAMP_EVENT[1]]
 *   ...
 *   [AVB_TIMESTAMP_EVENT[count-1]]
 * 
 * Lock-free protocol:
 *   Producer (Driver ISR):
 *     1. local_prod = producer_index (atomic read)
 *     2. if ((local_prod + 1) & mask) == consumer_index: ring full, drop event
 *     3. events[local_prod & mask] = new_event
 *     4. MemoryBarrier() - ensure event written before index update
 *     5. producer_index++ (atomic write)
 * 
 *   Consumer (User Mode):
 *     1. local_cons = consumer_index (atomic read)
 *     2. if (local_cons == producer_index): ring empty, no events
 *     3. event = events[local_cons & mask]
 *     4. MemoryBarrier() - ensure event read before index update
 *     5. consumer_index++ (atomic write)
 */
typedef struct AVB_TIMESTAMP_RING_HEADER {
    volatile avb_u32 producer_index;  /* Written by driver (ISR context) */
    volatile avb_u32 consumer_index;  /* Written by user (any context) */
    avb_u32 mask;                     /* (count - 1) for wrap-around (count is power of 2) */
    avb_u32 count;                    /* Ring size (power of 2: 64, 128, 256, ..., 4096) */
    volatile avb_u32 overflow_count;  /* Number of events dropped due to full ring */
    avb_u32 event_mask;               /* Copy of subscription event_mask for diagnostics */
    avb_u16 vlan_filter;              /* Copy of VLAN filter (INTEL_MASK_16BIT = no filter) */
    avb_u8  pcp_filter;               /* Copy of PCP filter (0xFF = no filter) */
    avb_u8  reserved0;                /* Align */
    avb_u64 total_events;             /* Total events posted (including dropped) */
    avb_u8  reserved[32];             /* Pad to 64 bytes (cache-line boundary) */
    /* AVB_TIMESTAMP_EVENT events[count]; // Follows immediately in memory */
} AVB_TIMESTAMP_RING_HEADER, *PAVB_TIMESTAMP_RING_HEADER;

/* IEEE 802.1AS-2020 §11.3 timestampCorrectionPortDS latency calibration.
 * Input to IOCTL_AVB_SET_PORT_LATENCY.
 *
 * ingress_latency_ns — time (ns) from physical wire arrival to hardware RX timestamp latch.
 *                      Driver ADDS this to each RX timestamp before posting the event.
 * egress_latency_ns  — time (ns) from hardware TX timestamp latch to physical wire departure.
 *                      Driver ADDS this to each TX timestamp before posting the event.
 *
 * Both fields are signed INT64:
 *   A NIC that latches the timestamp slightly BEFORE the wire event has a NEGATIVE latency.
 *   A NIC that latches the timestamp slightly AFTER  the wire event has a POSITIVE latency.
 *
 * Set both to 0  (default) for no correction.
 * Typical calibrated values: ±10 ns to ±200 ns depending on PHY.
 */
typedef struct AVB_PORT_LATENCY_REQUEST {
    avb_i64 ingress_latency_ns;  /* Added to RX hardware timestamps (signed, nanoseconds) */
    avb_i64 egress_latency_ns;   /* Added to TX hardware timestamps (signed, nanoseconds) */
} AVB_PORT_LATENCY_REQUEST, *PAVB_PORT_LATENCY_REQUEST;

/**
 * Per-adapter runtime statistics returned by IOCTL_AVB_GET_STATISTICS.
 * Implements: #270 (TEST-STATISTICS-001)
 * Verifies:   #67  (REQ-F-STATISTICS-001)
 *
 * Structure size MUST be 13 × 8 = 104 bytes with 8-byte packing.
 * Tests assert sizeof(AVB_DRIVER_STATISTICS) == 104.
 */
#pragma pack(push, 8)
typedef struct _AVB_DRIVER_STATISTICS {
    avb_u64 TxPackets;           /* Transmitted packet count               (offset  0) */
    avb_u64 RxPackets;           /* Received packet count                  (offset  8) */
    avb_u64 TxBytes;             /* Transmitted byte count                 (offset 16) */
    avb_u64 RxBytes;             /* Received byte count                    (offset 24) */
    avb_u64 PhcQueryCount;       /* PHC time query IOCTL calls             (offset 32) */
    avb_u64 PhcAdjustCount;      /* PHC frequency-adjust IOCTL calls       (offset 40) */
    avb_u64 PhcSetCount;         /* PHC set-time IOCTL calls               (offset 48) */
    avb_u64 TimestampCount;      /* Hardware timestamp captures            (offset 56) */
    avb_u64 IoctlCount;          /* Total IOCTL dispatch calls             (offset 64) */
    avb_u64 ErrorCount;          /* IOCTL calls that returned an error     (offset 72) */
    avb_u64 MemoryAllocFailures; /* NdisAllocate* failures                 (offset 80) */
    avb_u64 HardwareFaults;      /* Hardware-fault events                  (offset 88) */
    avb_u64 FilterAttachCount;   /* FilterAttach invocations               (offset 96) */
} AVB_DRIVER_STATISTICS, *PAVB_DRIVER_STATISTICS;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif
