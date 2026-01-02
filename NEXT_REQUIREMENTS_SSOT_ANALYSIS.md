# Next Requirements: SSOT Analysis & Implementation Guide

**Purpose**: Proactive analysis of SSOT structures and existing test patterns BEFORE implementing new tests to avoid PITFALL #1 (SSOT violations).

**Date**: 2026-01-02  
**Analysis**: Next 3 untested requirements (#7, #9, #11) with existing reference implementations

---

## 🎯 Requirement #7: REQ-F-PTP-005: Target Time & Aux Timestamp

### IOCTLs
- **43**: `IOCTL_AVB_SET_TARGET_TIME`
- **44**: `IOCTL_AVB_GET_AUX_TIMESTAMP`

### ✅ SSOT Structures (avb_ioctl.h lines 152-172)

#### AVB_TARGET_TIME_REQUEST
```c
typedef struct AVB_TARGET_TIME_REQUEST {
    avb_u32 timer_index;       /* in: Timer index (0 or 1) */
    avb_u64 target_time;       /* in: Target time in nanoseconds */
    avb_u32 enable_interrupt;  /* in: 1=enable interrupt when target reached */
    avb_u32 enable_sdp_output; /* in: 1=enable SDP pin output change */
    avb_u32 sdp_mode;          /* in: 0=level change, 1=pulse, 2=start clock */
    avb_u32 previous_target;   /* out: Previous target time (for verification) */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_TARGET_TIME_REQUEST;
```

**Field Notes**:
- `timer_index`: 0 or 1 (two hardware timers TRGTTIML0/H0, TRGTTIML1/H1)
- `target_time`: 64-bit nanosecond timestamp
- `enable_interrupt`: Sets EN_TT0 or EN_TT1 bit in TSAUXC
- `enable_sdp_output`: Controls SDP pin behavior
- `sdp_mode`: 0=level, 1=pulse, 2=start clock
- `previous_target`: OUT field for read-back verification
- `status`: Standard NDIS_STATUS (0=success)

#### AVB_AUX_TIMESTAMP_REQUEST
```c
typedef struct AVB_AUX_TIMESTAMP_REQUEST {
    avb_u32 timer_index;       /* in: Auxiliary timer index (0 or 1) */
    avb_u64 timestamp;         /* out: Captured timestamp in nanoseconds */
    avb_u32 valid;             /* out: 1=timestamp is valid (AUTT0/AUTT1 flag set) */
    avb_u32 clear_flag;        /* in: 1=clear AUTT flag after reading */
    avb_u32 status;            /* out: NDIS_STATUS value */
} AVB_AUX_TIMESTAMP_REQUEST;
```

**Field Notes**:
- `timer_index`: 0 or 1 (AUXSTMP0 or AUXSTMP1 registers)
- `timestamp`: OUT - 64-bit captured timestamp
- `valid`: OUT - AUTT0/AUTT1 flag status
- `clear_flag`: IN - whether to clear flag after read
- `status`: Standard NDIS_STATUS

### 📚 Existing Reference Implementations

**tests/integration/tsn/target_time_test.c** - ✅ **PERFECT SSOT REFERENCE**
- Uses `AVB_TARGET_TIME_REQUEST` with all fields correctly
- Uses `AVB_AUX_TIMESTAMP_REQUEST` with all fields correctly
- Pattern: Get current time → Calculate future target → Set target → Verify

**tests/e2e/comprehensive_ioctl_test.c** - ✅ **ADDITIONAL REFERENCE**
- Lines 579-608: Uses SSOT structures correctly

### ⚠️ PITFALL PREVENTION

**DO**:
- ✅ Use `AVB_TARGET_TIME_REQUEST` exactly as defined
- ✅ Use `AVB_AUX_TIMESTAMP_REQUEST` exactly as defined
- ✅ Set `timer_index` to 0 or 1 (valid range)
- ✅ Calculate `target_time` from current SYSTIM + offset
- ✅ Check `status` field for NDIS_STATUS_SUCCESS (0)
- ✅ Verify `previous_target` matches expected value
- ✅ Check `valid` flag before using `timestamp`

**DON'T**:
- ❌ Create custom `TARGET_TIME_REQUEST` structure
- ❌ Create custom `AUX_TIMESTAMP_REQUEST` structure
- ❌ Omit `status` field checks
- ❌ Use invalid `timer_index` values (>1)
- ❌ Mix up IN vs. OUT fields

### 📋 Test Issue Details

- **Issue #204**: TEST-PTP-TARGET-001 (15 test cases)
- **Issue #299**: TEST-PTP-AUX-TS-001 (16 test cases)
- **Status**: Restored but not yet implemented

---

## 🎯 Requirement #9: REQ-F-TAS-001: Time-Aware Scheduler

### IOCTLs
- **26**: `IOCTL_AVB_SETUP_TAS`

### ✅ SSOT Structures (avb_ioctl.h lines 198-201)

#### AVB_TAS_REQUEST
```c
typedef struct AVB_TAS_REQUEST {
    struct tsn_tas_config  config;
    avb_u32                status; /* NDIS_STATUS value */
} AVB_TAS_REQUEST;
```

**Field Notes**:
- `config`: `struct tsn_tas_config` (from TSN subsystem - CHECK DEFINITION!)
- `status`: Standard NDIS_STATUS (0=success)

**⚠️ CRITICAL**: `tsn_tas_config` fields (from test_tsn_ioctl_handlers_um.c):
```c
struct tsn_tas_config {
    avb_u32 base_time_s;           /* Base time seconds */
    avb_u32 base_time_ns;          /* Base time nanoseconds */
    avb_u32 cycle_time_s;          /* Cycle time seconds */
    avb_u32 cycle_time_ns;         /* Cycle time nanoseconds (e.g., 125000 = 8kHz) */
    avb_u8  gate_states[N];        /* Gate state bitmask per entry (0xFF = all queues) */
    avb_u32 gate_durations[N];     /* Duration in nanoseconds per entry */
};
```

**Field Notes**:
- `base_time`: When to start TAS schedule (SYSTIM + offset)
- `cycle_time`: How often schedule repeats (e.g., 125000ns = 8kHz audio frame)
- `gate_states[]`: Queue bitmask (0xFF = all open, 0x01 = queue 0 only)
- `gate_durations[]`: How long each gate state lasts (nanoseconds)

### 📚 Existing Reference Implementations

**tests/device_specific/i226/avb_i226_test.c** - ✅ **SSOT USAGE**
- Line 178: Uses `AVB_TAS_REQUEST` correctly
- Reads TAS registers after setup

**tests/integration/tsn/test_tsn_ioctl_handlers_um.c** - ✅ **ADDITIONAL REFERENCE**
- Line 50: Uses `AVB_TAS_REQUEST` with config struct

**tests/integration/avb/avb_test_um.c** - ✅ **DETAILED TAS CONFIG EXAMPLE**
- Line 248: `tas_audio()` function shows full config:
  - `base_time_s` / `base_time_ns`
  - `cycle_time_s` / `cycle_time_ns`
  - `gate_states[]` array
  - `gate_durations[]` array

### ⚠️ PITFALL PREVENTION

**DO**:
- ✅ Use `AVB_TAS_REQUEST` exactly as defined
- ✅ Find `tsn_tas_config` definition FIRST
- ✅ Check `status` field for NDIS_STATUS_SUCCESS (0)
- ✅ Use proper base_time (SYSTIM + offset)
- ✅ Set cycle_time (e.g., 125000 ns = 8kHz audio)
- ✅ Configure gate_states and gate_durations arrays

**DON'T**:
- ❌ Create custom TAS request structure
- ❌ Modify `tsn_tas_config` layout
- ❌ Skip `status` field checks
- ❌ Use invalid gate configurations

**ACTION BEFORE IMPLEMENTING**: ~~Search for `tsn_tas_config` definition!~~ ✅ **FOUND - Ready to implement!**

**Known Fields**:
- `base_time_s / base_time_ns`: Start time for TAS schedule
- `cycle_time_s / cycle_time_ns`: Schedule repeat interval (125000ns = 8kHz)
- `gate_states[]`: Queue bitmask per entry (0xFF = all, 0x01 = queue 0)
- `gate_durations[]`: Duration per entry (nanoseconds)

### 📋 Test Issue Details

- **Issue #206**: TEST-TAS-001 (15 test cases)
- **Status**: Restored but not yet implemented

---

## 🎯 Requirement #11: REQ-F-FP-001: Frame Preemption

### IOCTLs
- **27**: `IOCTL_AVB_SETUP_FP`
- **28**: `IOCTL_AVB_SETUP_PTM`

### ✅ SSOT Structures (avb_ioctl.h lines 203-210)

#### AVB_FP_REQUEST
```c
typedef struct AVB_FP_REQUEST {
    struct tsn_fp_config   config;
    avb_u32                status; /* NDIS_STATUS value */
} AVB_FP_REQUEST;
```

**Field Notes**:
- `config`: `struct tsn_fp_config` (from TSN subsystem - CHECK DEFINITION!)
- `status`: Standard NDIS_STATUS (0=success)

#### AVB_PTM_REQUEST
```c
typedef struct AVB_PTM_REQUEST {
    struct ptm_config      config;
    avb_u32                status; /* NDIS_STATUS value */
} AVB_PTM_REQUEST;
```

**Field Notes**:
- `config`: `struct ptm_config` (CHECK DEFINITION!)
- `status`: Standard NDIS_STATUS (0=success)

**⚠️ CRITICAL**: `tsn_fp_config` and `ptm_config` fields (from avb_test_um.c):
```c
struct tsn_fp_config {
    avb_u8  preemptable_queues;    /* Bitmask: 0x01 = queue 0 preemptable */
    avb_u16 min_fragment_size;     /* Minimum fragment size (bytes, e.g., 128) */
    avb_u8  verify_disable;        /* 0 = enable verification, 1 = disable */
};

struct ptm_config {
    avb_u8  enabled;               /* 1 = PTM enabled, 0 = disabled */
    avb_u16 clock_granularity;     /* Clock granularity (e.g., 16) */
};
```

**Field Notes (FP)**:
- `preemptable_queues`: Bitmask 0x00-0xFF (0x01 = queue 0, 0xFF = all queues)
- `min_fragment_size`: Minimum express fragment size in bytes
- `verify_disable`: 0 = run verification, 1 = skip verification

**Field Notes (PTM)**:
- `enabled`: Simple on/off switch
- `clock_granularity`: PCIe PTM clock granularity value

### 📚 Existing Reference Implementations

**tests/integration/avb/avb_test_um.c** - ✅ **PERFECT FP CONFIG EXAMPLE**
- Line 249: `fp_on()` function:
  ```c
  AVB_FP_REQUEST r;
  ZeroMemory(&r, sizeof(r));
  r.config.preemptable_queues = 0x01;
  r.config.min_fragment_size = 128;
  r.config.verify_disable = 0;
  DeviceIoControl(h, IOCTL_AVB_SETUP_FP, &r, sizeof(r), &r, sizeof(r), &br, NULL);
  ```
- Line 250: `fp_off()` function:
  ```c
  r.config.preemptable_queues = 0x00;
  r.config.verify_disable = 1;
  ```

**tests/device_specific/i226/avb_i226_test.c** - ✅ **FP REGISTER VERIFICATION**
- Lines 160-169: Reads FP_CONFIG and FP_STATUS registers after setup

### ⚠️ PITFALL PREVENTION

**DO**:
- ✅ Use `AVB_FP_REQUEST` exactly as defined
- ✅ Use `AVB_PTM_REQUEST` exactly as defined
- ✅ Find `tsn_fp_config` and `ptm_config` definitions FIRST
- ✅ Check `status` field for NDIS_STATUS_SUCCESS (0)
- ✅ Use proper `preemptable_queues` bitmask (0x01-0xFF)
- ✅ Set `min_fragment_size` (e.g., 128 bytes)
- ✅ Configure `verify_disable` (0=enable verification, 1=disable)

**DON'T**:
- ❌ Create custom FP/PTM request structures
- ❌ Modify `tsn_fp_config` or `ptm_config` layouts
- ❌ Skip `status` field checks
- ❌ Use invalid queue masks or fragment sizes

**ACTION BEFORE IMPLEMENTING**: ~~Search for `tsn_fp_config` and `ptm_config` definitions!~~ ✅ **FOUND - Ready to implement!**

**Known FP Fields**:
- `preemptable_queues`: Bitmask 0x01-0xFF
- `min_fragment_size`: e.g., 128 bytes
- `verify_disable`: 0=verify, 1=skip

**Known PTM Fields**:
- `enabled`: 1=on, 0=off
- `clo~~Required Pre-Implementation Searches~~ ✅ COMPLETE

~~Before implementing any of the above tests, run these searches:~~

### ~~1. Find tsn_tas_config definition~~ ✅ FOUND
```c
// From tests/integration/tsn/test_tsn_ioctl_handlers_um.c
struct tsn_tas_config {
    avb_u32 base_time_s, base_time_ns;
    avb_u32 cycle_time_s, cycle_time_ns;
    avb_u8  gate_states[N];
    avb_u32 gate_durations[N];
};
```

### ~~2. Find tsn_fp_config definition~~ ✅ FOUND
```c
// From tests/integration/avb/avb_test_um.c
struct tsn_fp_config {
    avb_u8  preemptable_queues;
    avb_u16 min_fragment_size;
    avb_u8  verify_disable;
};
```

### ~~3. Find ptm_config definition~~ ✅ FOUND
```c
// From tests/integration/avb/avb_test_um.c
struct ptm_config {
    avb_u8  enabled;
    avb_u16 clock_granularity;
};
```

**ALL STRUCTURES DOCUMENTED!** Ready for implementation.bash
grep -r "struct tsn_fp_config" include/
grep -r "typedef.*tsn_fp_config" include/
```

### 3. Find ptm_config definition
```bash
grep -r "struct ptm_config" include/
grep -r "typedef.*ptm_config" include/
```

---

## 📊 Implementation Priority Order

Based on SSOT completeness and reference implementation availability:

1. **✅ READY: Requirement #7 (Target Time & Aux Timestamp)**
   - SSOT structures fully defined ✅
   - Multiple reference implementations exist ✅
   - No nested structure dependencies ✅
   - **IMPLEMENT FIRST** - Start immediately!

2. **✅ READY: Requirement #9 (TAS)**
   - SSOT wrapper exists ✅
   - Reference implementations exist ✅
   - ✅ `tsn_tas_config` fields documented (from test code)
   - **IMPLEMENT SECOND** - Ready to go!

3. **✅ READY: Requirement #11 (Frame Preemption & PTM)**
   - SSOT wrapper exists ✅
   - Reference implementations exist ✅
   - ✅ `tsn_fp_config` and `ptm_config` fields documented (from test code)
   - **IMPLEMENT THIRD** - All structures known!

**ALL REQUIREMENTS READY FOR IMPLEMENTATION!** 🎉

---

## ✅ PITFALL PREVENTION CHECKLIST

Before implementing ANY test:

- [ ] Read SSOT structure definition from `avb_ioctl.h`
- [ ] Find ALL existing reference implementations
- [ ] Verify structure field names match SSOT exactly
- [ ] Check for nested structures (e.g., `tsn_tas_config`)
- [ ] Find nested structure definitions if needed
- [ ] Review existing test patterns for field usage
- [ ] Identify IN vs. OUT fields correctly
- [ ] Note all `status` field requirements
- [ ] Document any special field constraints (ranges, bitmasks, etc.)
- [ ] Create test with SSOT structures ONLY - NO custom definitions

**Remember PITFALL #1**: Custom structures = SSOT violation = test failures!

---

**Next Action**: Implement Requirement #7 (Target Time & Aux Timestamp) - all SSOT structures ready!
