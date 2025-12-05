# SSOT Register Header Regeneration Guide

## ⚠️ Current Technical Debt

**Date**: January 2025  
**Issue**: Register headers (`i210_regs.h`, `i226_regs.h`) were **manually patched** instead of properly regenerated from YAML files.

**Why This Matters**: Violates **Single Source of Truth (SSOT)** principle. The YAML files are authoritative, but headers are now out of sync with the generation process.

## ✅ What's Correct (Source of Truth)

The following YAML files are **correctly updated** and schema-compliant:

### `intel-ethernet-regs/devices/i210.yaml`
Added registers (lines 104-176):
```yaml
- name: TSAUXC        # 0x0B640 - TimeSync Auxiliary Control
- name: TRGTTIML0     # 0x0B644 - Target Time 0 Low
- name: TRGTTIMH0     # 0x0B648 - Target Time 0 High
- name: TRGTTIML1     # 0x0B64C - Target Time 1 Low
- name: TRGTTIMH1     # 0x0B650 - Target Time 1 High
- name: FREQOUT0      # 0x0B654 - Frequency Out 0
- name: FREQOUT1      # 0x0B658 - Frequency Out 1
- name: AUXSTMPL0     # 0x0B65C - Aux Timestamp 0 Low
- name: AUXSTMPH0     # 0x0B660 - Aux Timestamp 0 High
- name: AUXSTMPL1     # 0x0B664 - Aux Timestamp 1 Low
- name: AUXSTMPH1     # 0x0B668 - Aux Timestamp 1 High
- name: TSICR         # 0x0B66C - Time Sync Interrupt Cause
- name: TSIM          # 0x0B674 - Time Sync Interrupt Mask

- name: RXPBSIZE      # 0x2404 - RX Packet Buffer Size
  fields:
    - name: CFG_TS_EN # bit 29 - Enable timestamp in RX buffer
```

### `intel-ethernet-regs/devices/i226.yaml`
Same registers added (lines 175-246) with I226-specific access attributes.

## 🔧 How to Properly Regenerate Headers

### Prerequisites
1. **Install Python 3** (if not installed):
   ```powershell
   winget install Python.Python.3
   # OR download from https://www.python.org/downloads/
   ```

2. **Install PyYAML**:
   ```powershell
   python -m pip install pyyaml
   ```

3. **Verify Installation**:
   ```powershell
   python --version  # Should show Python 3.x
   python -c "import yaml; print('PyYAML OK')"
   ```

### Regeneration Commands

#### Option 1: Regenerate All Devices
```powershell
Get-ChildItem intel-ethernet-regs\devices -Filter *.yaml | ForEach-Object {
    python intel-ethernet-regs\tools\reggen.py $_.FullName intel-ethernet-regs\gen
}
```

#### Option 2: Regenerate Specific Devices
```powershell
# I210 family
python intel-ethernet-regs\tools\reggen.py intel-ethernet-regs\devices\i210.yaml intel-ethernet-regs\gen
python intel-ethernet-regs\tools\reggen.py intel-ethernet-regs\devices\i210_v3_7.yaml intel-ethernet-regs\gen
python intel-ethernet-regs\tools\reggen.py intel-ethernet-regs\devices\i210_cscl_v1_8.yaml intel-ethernet-regs\gen

# I226
python intel-ethernet-regs\tools\reggen.py intel-ethernet-regs\devices\i226.yaml intel-ethernet-regs\gen
```

### Verification After Regeneration

1. **Check Generated Definitions**:
   ```powershell
   Select-String "TRGTTIML0|AUXSTMPL0|RXPBSIZE" intel-ethernet-regs\gen\i210_regs.h
   Select-String "TRGTTIML0|AUXSTMPL0|RXPBSIZE" intel-ethernet-regs\gen\i226_regs.h
   ```

2. **Expected Output**:
   ```c
   #define I210_TRGTTIML0  0x0B644
   #define I210_AUXSTMPL0  0x0B65C
   #define I210_RXPBSIZE   0x02404
   #define I226_TRGTTIML0  0x0B644
   #define I226_AUXSTMPL0  0x0B65C
   #define I226_RXPBSIZE   0x02404
   ```

## 📝 Next Steps After Regeneration

1. **Remove Manual Edits** (if regenerated headers differ):
   - Manually edited sections in `i210_regs.h` (lines 49-77)
   - Manually edited sections in `i226_regs.h` (lines 93-137)
   - These will be replaced by properly generated content

2. **Update Implementation Code**:
   ```c
   // Replace in avb_integration_fixed.c:
   0x0B644 → I210_TRGTTIML0 or I226_TRGTTIML0
   0x0B648 → I210_TRGTTIMH0 or I226_TRGTTIMH0
   0x0B64C → I210_TRGTTIML1 or I226_TRGTTIML1
   0x0B650 → I210_TRGTTIMH1 or I226_TRGTTIMH1
   0x0B65C → I210_AUXSTMPL0 or I226_AUXSTMPL0
   0x0B660 → I210_AUXSTMPH0 or I226_AUXSTMPH0
   0x0B664 → I210_AUXSTMPL1 or I226_AUXSTMPL1
   0x0B668 → I210_AUXSTMPH1 or I226_AUXSTMPH1
   0x2404  → I210_RXPBSIZE or I226_RXPBSIZE
   ```

3. **Build and Test**:
   ```powershell
   # Rebuild driver
   msbuild IntelAvbFilter.sln /t:Build /p:Configuration=Debug;Platform=x64

   # Build validation test
   powershell -File tools\vs_compile.ps1 -BuildCmd "cl /nologo /W4 /Zi /I include /I external/intel_avb/lib /I intel-ethernet-regs/gen tools/avb_test/ssot_register_validation_test.c /Fe:ssot_validation.exe"

   # Run on hardware
   .\ssot_validation.exe
   ```

## 🎯 Why SSOT Matters

**Single Source of Truth** means:
- ✅ **YAML files** define register addresses (authoritative)
- ✅ **Headers generated** from YAML (derived, never manually edited)
- ✅ **Code uses headers** (always in sync)

**Manual editing breaks this chain**:
- ❌ Headers diverge from YAML
- ❌ Future regeneration may overwrite manual edits
- ❌ No way to verify correctness except manual inspection

## 📊 Current Status Summary

| Component | Status | Notes |
|-----------|--------|-------|
| i210.yaml | ✅ Updated | All registers added, schema-compliant |
| i226.yaml | ✅ Updated | All registers added, schema-compliant |
| i210_regs.h | ⚠️ Manually patched | Lines 49-77 added manually |
| i226_regs.h | ⚠️ Manually patched | Lines 93-137 added manually |
| Python Environment | ❌ Not working | "Python was not found" error |
| Proper Regeneration | ⏳ Pending | Blocked on Python installation |

## 🔄 Regeneration Task (VS Code)

Add this task to `.vscode/tasks.json` for easy regeneration:

```json
{
    "label": "regenerate-ssot-headers",
    "type": "shell",
    "command": "python",
    "args": [
        "intel-ethernet-regs/tools/reggen.py",
        "${input:yamlFile}",
        "intel-ethernet-regs/gen"
    ],
    "problemMatcher": []
}
```

## ✅ Acceptance Criteria

Before closing this technical debt:
- [ ] Python 3 + PyYAML installed and working
- [ ] Headers regenerated from YAML (not manually edited)
- [ ] `grep "Auto-generated by"` shows correct generation timestamp
- [ ] All TRGTTIML/AUXSTMP/RXPBSIZE definitions present
- [ ] Code compiles without errors
- [ ] Hardware validation test passes on I210/I226

---

**Remember**: Always regenerate headers from YAML. Never manually edit generated `.h` files.
