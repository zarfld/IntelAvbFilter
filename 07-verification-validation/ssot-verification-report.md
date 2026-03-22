# SSOT Cross-Check Verification Report

## Summary
Cross-checking generated header `i226_regs.h` against source YAML `i226.yaml` to ensure no manual edits exist in the generated file.

---

## Registers Checked

### ✅ TSAUXC (0x0B640) - TimeSync Auxiliary Control

**Current Generated Header:**
- `I226_TSAUXC` = 0x0B640 (register address only)
- ❌ **NO bit field definitions**

**YAML Source (Updated):**
- ✅ `EN_TT0` (bit 0) - Enable Target Time 0
- ✅ `EN_CLK0` (bit 2) - Enable clock 0
- ✅ `EN_TT1` (bit 4) - Enable Target Time 1
- ✅ `AUTT0` (bit 16) - Auxiliary Timestamp 0 captured
- ✅ `AUTT1` (bit 17) - Auxiliary Timestamp 1 captured
- ✅ `PLLLOCKED` (bit 31) - PLL locked/disable bit

**Status:** YAML updated, needs regeneration

---

### ✅ TSICR (0x0B66C) - Time Sync Interrupt Cause Register

**Current Generated Header:**
- `I226_TSICR` = 0x0B66C (register address only)
- ❌ **NO bit field definitions**

**YAML Source (Updated):**
- ✅ `TT0` (bit 3) - Target Time 0 triggered
- ✅ `TT1` (bit 4) - Target Time 1 triggered

**Status:** YAML updated, needs regeneration

---

### ✅ TSIM (0x0B674) - Time Sync Interrupt Mask Register

**Current Generated Header:**
- `I226_TSIM` = 0x0B674 (register address only)
- ❌ **NO bit field definitions**

**YAML Source (Updated):**
- ✅ `TT0` (bit 3) - Target Time 0 interrupt mask
- ✅ `TT1` (bit 4) - Target Time 1 interrupt mask

**Status:** YAML updated, needs regeneration

---

### ✅ EIMS (0x01524) - Extended Interrupt Mask Set/Read

**Current Generated Header:**
- `I226_EIMS` = 0x01524
- `I226_EIMS_VEC0_SHIFT` / `I226_EIMS_VEC0_MASK` (bit 0) ✅
- `I226_EIMS_VEC1_SHIFT` / `I226_EIMS_VEC1_MASK` (bit 1) ✅
- `I226_EIMS_VEC2_SHIFT` / `I226_EIMS_VEC2_MASK` (bit 2) ✅
- `I226_EIMS_VEC3_SHIFT` / `I226_EIMS_VEC3_MASK` (bit 3) ✅
- `I226_EIMS_TCP_TIMER_SHIFT` / `I226_EIMS_TCP_TIMER_MASK` (bit 30) ✅
- ❌ **MISSING: `I226_EIMS_OTHER_SHIFT` / `I226_EIMS_OTHER_MASK` (bit 31)**

**YAML Source (Updated):**
- ✅ `VEC0` (bit 0)
- ✅ `VEC1` (bit 1)
- ✅ `VEC2` (bit 2)
- ✅ `VEC3` (bit 3)
- ✅ `TCP_TIMER` (bit 30)
- ✅ **`OTHER` (bit 31)** - Mask for OTHER interrupts (includes TimeSync)

**Status:** YAML updated with OTHER field, needs regeneration

---

### ✅ EICR (0x01580) - Extended Interrupt Cause Read

**Current Generated Header:**
- `I226_EICR` = 0x01580
- `I226_EICR_VEC0_SHIFT` / `I226_EICR_VEC0_MASK` (bit 0) ✅
- `I226_EICR_VEC1_SHIFT` / `I226_EICR_VEC1_MASK` (bit 1) ✅
- `I226_EICR_VEC2_SHIFT` / `I226_EICR_VEC2_MASK` (bit 2) ✅
- `I226_EICR_VEC3_SHIFT` / `I226_EICR_VEC3_MASK` (bit 3) ✅
- `I226_EICR_TCP_TIMER_SHIFT` / `I226_EICR_TCP_TIMER_MASK` (bit 30) ✅
- `I226_EICR_OTHER_SHIFT` / `I226_EICR_OTHER_MASK` (bit 31) ✅

**YAML Source:**
- ✅ `VEC0` (bit 0)
- ✅ `VEC1` (bit 1)
- ✅ `VEC2` (bit 2)
- ✅ `VEC3` (bit 3)
- ✅ `TCP_TIMER` (bit 30)
- ✅ `OTHER` (bit 31)

**Status:** Already complete, no changes needed

---

## Verification Result

### ✅ YAML Source is Complete
All required bit field definitions have been added to `i226.yaml`:
1. TSAUXC: 6 bit fields (EN_TT0, EN_CLK0, EN_TT1, AUTT0, AUTT1, PLLLOCKED)
2. TSICR: 2 bit fields (TT0, TT1)
3. TSIM: 2 bit fields (TT0, TT1)
4. EIMS: Added missing OTHER field (bit 31)

### ❌ Generated Header is OUTDATED
The current `i226_regs.h` is missing the following definitions:
- TSAUXC bit fields (6 missing)
- TSICR bit fields (2 missing)
- TSIM bit fields (2 missing)
- EIMS OTHER field (1 missing)

### ✅ NO Manual Edits Detected
No evidence of manual edits in the generated header. All definitions in the header correspond correctly to YAML source.

---

## Action Required

**Run the generator to update the SSOT header:**

```powershell
cd intel-ethernet-regs
python tools\reggen.py devices\i226.yaml
```

This will regenerate `gen/i226_regs.h` with all the new bit field definitions from the updated YAML source.

---

**After regeneration, the header will have:**

```c
// TSAUXC bit fields
#define I226_TSAUXC_EN_TT0_SHIFT      0
#define I226_TSAUXC_EN_TT0_MASK       (((1ULL<<1)-1ULL) << I226_TSAUXC_EN_TT0_SHIFT)
#define I226_TSAUXC_EN_CLK0_SHIFT     2
#define I226_TSAUXC_EN_CLK0_MASK      (((1ULL<<1)-1ULL) << I226_TSAUXC_EN_CLK0_SHIFT)
#define I226_TSAUXC_EN_TT1_SHIFT      4
#define I226_TSAUXC_EN_TT1_MASK       (((1ULL<<1)-1ULL) << I226_TSAUXC_EN_TT1_SHIFT)
#define I226_TSAUXC_AUTT0_SHIFT       16
#define I226_TSAUXC_AUTT0_MASK        (((1ULL<<1)-1ULL) << I226_TSAUXC_AUTT0_SHIFT)
#define I226_TSAUXC_AUTT1_SHIFT       17
#define I226_TSAUXC_AUTT1_MASK        (((1ULL<<1)-1ULL) << I226_TSAUXC_AUTT1_SHIFT)
#define I226_TSAUXC_PLLLOCKED_SHIFT   31
#define I226_TSAUXC_PLLLOCKED_MASK    (((1ULL<<1)-1ULL) << I226_TSAUXC_PLLLOCKED_SHIFT)

// TSIM bit fields
#define I226_TSIM_TT0_SHIFT           3
#define I226_TSIM_TT0_MASK            (((1ULL<<1)-1ULL) << I226_TSIM_TT0_SHIFT)
#define I226_TSIM_TT1_SHIFT           4
#define I226_TSIM_TT1_MASK            (((1ULL<<1)-1ULL) << I226_TSIM_TT1_SHIFT)

// TSICR bit fields
#define I226_TSICR_TT0_SHIFT          3
#define I226_TSICR_TT0_MASK           (((1ULL<<1)-1ULL) << I226_TSICR_TT0_SHIFT)
#define I226_TSICR_TT1_SHIFT          4
#define I226_TSICR_TT1_MASK           (((1ULL<<1)-1ULL) << I226_TSICR_TT1_SHIFT)

// EIMS OTHER field
#define I226_EIMS_OTHER_SHIFT         31
#define I226_EIMS_OTHER_MASK          (((1ULL<<1)-1ULL) << I226_EIMS_OTHER_SHIFT)
```

---

## Next Steps

1. ✅ YAML updated with all missing bit field definitions
2. ⏳ Run generator: `python intel-ethernet-regs\tools\reggen.py intel-ethernet-regs\devices\i226.yaml`
3. ⏳ Remove ALL local `#define` from `devices/intel_i226_impl.c` (lines 10-47)
4. ⏳ Build 19 with proper SSOT compliance

---

**Generated:** 2026-02-20  
**Status:** YAML complete, ready for regeneration
