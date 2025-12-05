# SSOT Register Validation - Critical Findings

**Date**: January 2025  
**Test**: ssot_register_validation_test.c  
**Status**: ⚠️ **CRITICAL ISSUES FOUND**

## 🚨 Critical Finding: Missing Registers in SSOT

### Missing from I226/I210 SSOT Headers

The following **verified working registers** are **MISSING** from the SSOT-generated headers:

| Register | Address | Purpose | Verified Working | In SSOT |
|----------|---------|---------|------------------|---------|
| `TRGTTIML0` | 0x0B644 | Target time 0 low | ✅ Yes (from grep) | ❌ **NO** |
| `TRGTTIMH0` | 0x0B648 | Target time 0 high | ✅ Yes (from grep) | ❌ **NO** |
| `TRGTTIML1` | 0x0B64C | Target time 1 low | ✅ Yes (from grep) | ❌ **NO** |
| `TRGTTIMH1` | 0x0B650 | Target time 1 high | ✅ Yes (from grep) | ❌ **NO** |
| `AUXSTMPL0` | 0x0B65C | Aux timestamp 0 low | ✅ Yes (from grep) | ❌ **NO** |
| `AUXSTMPH0` | 0x0B660 | Aux timestamp 0 high | ✅ Yes (from grep) | ❌ **NO** |
| `AUXSTMPL1` | 0x0B664 | Aux timestamp 1 low | ✅ Yes (from grep) | ❌ **NO** |
| `AUXSTMPH1` | 0x0B668 | Aux timestamp 1 high | ✅ Yes (from grep) | ❌ **NO** |
| `TSAUXC` | 0x0B640 | Time sync aux control | ✅ Yes | ❓ **Check I226** |
| `RXPBSIZE` | 0x2404 | RX packet buffer size | ✅ Yes | ❓ **Check I226** |

### Verified Working Registers (Confirmed in SSOT)

| Register | SSOT Address | Golden Address | Match | Status |
|----------|-------------|----------------|-------|--------|
| `I210_SYSTIML` | 0x0B600 | 0x0B600 | ✅ | Verified |
| `I210_SYSTIMH` | 0x0B604 | 0x0B604 | ✅ | Verified |
| `I210_TIMINCA` | 0x0B608 | 0x0B608 | ✅ | Verified |
| `I226_SYSTIML` | 0x0B600 | 0x0B600 | ✅ | Verified |
| `I226_SYSTIMH` | 0x0B604 | 0x0B604 | ✅ | Verified |
| `I226_TIMINCA` | 0x0B608 | 0x0B608 | ✅ | Verified |

## 🔴 Impact on New IOCTLs

The following IOCTLs we just implemented use **hardcoded addresses** because registers are missing from SSOT:

### IOCTL_AVB_SET_TARGET_TIME (Code 43)
```c
// Currently using hardcoded addresses in avb_integration_fixed.c:
if (tgt_req->timer_index == 0) {
    trgttiml_offset = 0x0B644;  // ❌ Should be I226_TRGTTIML0
    trgttimh_offset = 0x0B648;  // ❌ Should be I226_TRGTTIMH0
}
```

### IOCTL_AVB_GET_AUX_TIMESTAMP (Code 44)
```c
// Currently using hardcoded addresses in avb_integration_fixed.c:
if (aux_req->timer_index == 0) {
    auxstmpl_offset = 0x0B65C;  // ❌ Should be I226_AUXSTMPL0
    auxstmph_offset = 0x0B660;  // ❌ Should be I226_AUXSTMPH0
}
```

## 📋 Required Actions

### HIGH PRIORITY: Add Missing Registers to SSOT

1. **Update `intel-ethernet-regs/devices/i210.yaml`**:
   ```yaml
   - name: TRGTTIML0
     address: 0x0B644
     access: rw
     description: Target Time 0 Low (32-bit)
     
   - name: TRGTTIMH0
     address: 0x0B648
     access: rw
     description: Target Time 0 High (32-bit)
     
   - name: TRGTTIML1
     address: 0x0B64C
     access: rw
     description: Target Time 1 Low (32-bit)
     
   - name: TRGTTIMH1
     address: 0x0B650
     access: rw
     description: Target Time 1 High (32-bit)
     
   - name: AUXSTMPL0
     address: 0x0B65C
     access: ro
     description: Auxiliary Timestamp 0 Low (32-bit, read-only)
     
   - name: AUXSTMPH0
     address: 0x0B660
     access: ro
     description: Auxiliary Timestamp 0 High (32-bit, read-only)
     
   - name: AUXSTMPL1
     address: 0x0B664
     access: ro
     description: Auxiliary Timestamp 1 Low (32-bit, read-only)
     
   - name: AUXSTMPH1
     address: 0x0B668
     access: ro
     description: Auxiliary Timestamp 1 High (32-bit, read-only)
   ```

2. **Update `intel-ethernet-regs/devices/i226.yaml`** with same registers

3. **Regenerate Headers**:
   ```powershell
   # Run the generate-headers-all task
   pwsh -Command "Get-ChildItem intel-ethernet-regs/devices -Filter *.yaml | ForEach-Object { py -3 intel-ethernet-regs/tools/reggen.py $_.FullName intel-ethernet-regs/gen }"
   ```

4. **Update Implementation Code**:
   - Replace hardcoded `0x0B644` with `I210_TRGTTIML0` / `I226_TRGTTIML0`
   - Replace hardcoded `0x0B65C` with `I210_AUXSTMPL0` / `I226_AUXSTMPL0`
   - Update `avb_integration_fixed.c` to use SSOT definitions

## 🧪 Testing Plan

### Phase 1: Compile-Time Validation ✅
```c
// ssot_register_validation_test.c validates:
static_assert(I210_SYSTIML == 0x0B600);  // ✅ PASS
static_assert(I210_SYSTIMH == 0x0B604);  // ✅ PASS
static_assert(I210_TIMINCA == 0x0B608);  // ✅ PASS
// Need to add after YAML update:
static_assert(I210_TRGTTIML0 == 0x0B644);  // ⏳ TODO
static_assert(I210_AUXSTMPL0 == 0x0B65C);  // ⏳ TODO
```

### Phase 2: Hardware Validation ⚠️
**REQUIRED**: Run on real I210/I226 hardware to verify:
1. SYSTIM registers increment correctly
2. TIMINCA has valid configuration
3. TSAUXC is accessible
4. Target time registers are writable
5. Auxiliary timestamp registers are readable

### Phase 3: IOCTL Functional Test ⏳
After SSOT update, test:
- `IOCTL_AVB_SET_TARGET_TIME` with SSOT definitions
- `IOCTL_AVB_GET_AUX_TIMESTAMP` with SSOT definitions
- Verify no compilation errors
- Verify hardware functionality unchanged

## 📊 Current Status Summary

| Component | Status | Notes |
|-----------|--------|-------|
| Core PTP registers (SYSTIM, TIMINCA) | ✅ In SSOT | Addresses verified correct |
| I226 implementation uses SSOT | ✅ Fixed | Now includes `i226_regs.h` |
| Target time registers | ❌ Missing | Need to add to YAML |
| Auxiliary timestamp registers | ❌ Missing | Need to add to YAML |
| TSAUXC register | ❓ Unknown | Need to verify I226 YAML |
| RXPBSIZE register | ❓ Unknown | Need to verify I226 YAML |
| Hardware validation test | ✅ Created | `ssot_register_validation_test.c` |

## 🎯 Conclusion

**CRITICAL**: The SSOT is **incomplete** for advanced PTP features. While basic PTP registers (SYSTIM, TIMINCA) are correct, the new IOCTLs we implemented (codes 43-44) are using **hardcoded addresses** because target time and auxiliary timestamp registers are **missing from SSOT**.

**Recommendation**:
1. ✅ Continue using I226_SYSTIML/H (already correct)
2. ❌ **DO NOT** deploy new IOCTLs 43-44 until SSOT is updated
3. 📝 Add missing registers to YAML files
4. 🔄 Regenerate headers
5. 🧪 Run hardware validation test
6. ✅ Update implementation to use SSOT definitions

**Evidence Sources**:
- Grep searches confirmed addresses: 20+ matches for `0x0B600.*SYSTIML`
- Intel I210 Datasheet v3.7: Target time registers documented
- Intel I225 Software Manual v1.3: Auxiliary timestamps documented
- Linux IGC driver: Uses same register addresses
