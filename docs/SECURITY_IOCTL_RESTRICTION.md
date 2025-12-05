# Security Enhancement: Debug-Only Register Access

## Problem Statement
Raw register access IOCTLs (`IOCTL_AVB_READ_REGISTER` and `IOCTL_AVB_WRITE_REGISTER`) exposed dangerous capabilities in production builds, violating the principle of least privilege and creating security risks.

## Solution Implemented

### 1. **Debug-Only Raw Register Access** ✅
Guarded dangerous IOCTLs with `#ifndef NDEBUG`:

```c
#ifndef NDEBUG
/* Debug-only IOCTLs - Provide raw register access for diagnostics.
 * These are DISABLED in Release builds for security.
 * Use proper abstractions (ADJUST_FREQUENCY, GET_CLOCK_CONFIG) in production. */
#define IOCTL_AVB_READ_REGISTER         _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_WRITE_REGISTER        _NDIS_CONTROL_CODE(23, METHOD_BUFFERED)
#endif
```

**Impact**: Release builds (NDEBUG defined) will NOT expose raw register access.

### 2. **Production IOCTLs Added** ✅

#### IOCTL_AVB_ADJUST_FREQUENCY (0x0008246C)
- **Purpose**: Adjust PTP clock frequency (replaces raw TIMINCA writes)
- **Input**: `AVB_FREQUENCY_REQUEST`
  * `increment_ns`: Clock increment per cycle (e.g., 8 for 8ns @ 125MHz)
  * `increment_frac`: Optional fractional part for fine-tuning
- **Output**: 
  * `current_increment`: Previous TIMINCA value
  * `status`: Operation result

**Usage Example**:
```c
AVB_FREQUENCY_REQUEST freq_req;
freq_req.increment_ns = 8;      // 8ns increment
freq_req.increment_frac = 0;    // No fractional adjustment
DWORD bytesReturned;

DeviceIoControl(hDevice, IOCTL_AVB_ADJUST_FREQUENCY,
                &freq_req, sizeof(freq_req),
                &freq_req, sizeof(freq_req),
                &bytesReturned, NULL);

printf("Previous TIMINCA: 0x%08X\n", freq_req.current_increment);
```

#### IOCTL_AVB_GET_CLOCK_CONFIG (0x00082470)
- **Purpose**: Query complete clock configuration (replaces raw register reads)
- **Output**: `AVB_CLOCK_CONFIG`
  * `systim`: Current SYSTIM counter (64-bit nanoseconds)
  * `timinca`: Current clock increment configuration
  * `tsauxc`: Auxiliary clock control register
  * `clock_rate_mhz`: Base clock rate (125/156/200/250 MHz)

**Usage Example**:
```c
AVB_CLOCK_CONFIG cfg;
DWORD bytesReturned;

DeviceIoControl(hDevice, IOCTL_AVB_GET_CLOCK_CONFIG,
                &cfg, sizeof(cfg),
                &cfg, sizeof(cfg),
                &bytesReturned, NULL);

printf("SYSTIM:  0x%016llX\n", cfg.systim);
printf("TIMINCA: 0x%08X\n", cfg.timinca);
printf("TSAUXC:  0x%08X (bit 31 = %s)\n", 
       cfg.tsauxc, (cfg.tsauxc & 0x80000000) ? "DISABLED" : "ENABLED");
printf("Clock:   %u MHz\n", cfg.clock_rate_mhz);
```

### 3. **Files Modified** ✅

1. **include/avb_ioctl.h**
   - Added `#ifndef NDEBUG` guards around `IOCTL_AVB_READ/WRITE_REGISTER`
   - Added `AVB_FREQUENCY_REQUEST` structure
   - Added `AVB_CLOCK_CONFIG` structure
   - Added `IOCTL_AVB_ADJUST_FREQUENCY` (code 38)
   - Added `IOCTL_AVB_GET_CLOCK_CONFIG` (code 39)

2. **avb_integration_fixed.c**
   - Wrapped existing `IOCTL_AVB_READ/WRITE_REGISTER` handlers with `#ifndef NDEBUG`
   - Implemented `IOCTL_AVB_ADJUST_FREQUENCY` handler
   - Implemented `IOCTL_AVB_GET_CLOCK_CONFIG` handler

3. **device.c**
   - Guarded `IOCTL_AVB_READ/WRITE_REGISTER` case labels with `#ifndef NDEBUG`
   - Added new IOCTLs to routing switch statement

4. **tools/avb_test/ptp_clock_control_production_test.c** (NEW)
   - Production test using only high-level IOCTLs
   - Tests clock configuration query
   - Tests frequency adjustment (5 different values)
   - Tests timestamp setting/retrieval
   - Tests clock stability measurement
   - **Zero raw register access** - fully abstracted

### 4. **Migration Path for Existing Tests** ⚠️

Existing test files need updates:

#### Old Pattern (Debug-only):
```c
#define REG_TIMINCA 0x0B608  // ❌ Hardcoded, debug-only

AVB_REGISTER_REQUEST req;
req.offset = REG_TIMINCA;
req.value = 0x08000000;  // 8ns increment
DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, ...);  // Only in Debug builds
```

#### New Pattern (Production):
```c
AVB_FREQUENCY_REQUEST freq_req;  // ✅ Abstracted, always available
freq_req.increment_ns = 8;
freq_req.increment_frac = 0;
DeviceIoControl(h, IOCTL_AVB_ADJUST_FREQUENCY, ...);  // Works in Release
```

Files needing migration:
- `tools/avb_test/tsauxc_toggle_test.c`
- `tools/avb_test/ptp_clock_control_test.c`

### 5. **Build Configuration** ✅

#### Debug Build (Default)
- `NDEBUG` is **NOT** defined
- Raw register IOCTLs **AVAILABLE** for diagnostics
- Test code can use `IOCTL_AVB_READ/WRITE_REGISTER`

#### Release Build
- `NDEBUG` **IS** defined (automatically by compiler in Release mode)
- Raw register IOCTLs **REMOVED** for security
- Only production IOCTLs available
- Test code **MUST** use `IOCTL_AVB_ADJUST_FREQUENCY` and `IOCTL_AVB_GET_CLOCK_CONFIG`

**Compiler Behavior**:
```bash
# Debug build (NDEBUG not defined)
cl /W4 /Zi ...  # Raw register IOCTLs included

# Release build (NDEBUG defined)
cl /W4 /O2 /DNDEBUG ...  # Raw register IOCTLs excluded
```

### 6. **Security Benefits** 🔒

✅ **Principle of Least Privilege**: Production code cannot bypass abstractions  
✅ **Attack Surface Reduction**: Release builds expose minimal IOCTL surface  
✅ **Maintenance**: Clear separation between debug diagnostics and production API  
✅ **Documentation**: Code self-documents which IOCTLs are production-grade  
✅ **Testing**: Production tests validate actual deployment configuration  

### 7. **Backward Compatibility** ⚠️

**Debug builds**: Fully backward compatible - all existing test code works  
**Release builds**: Breaking change - tests using raw register access will fail to compile

**Migration Required For**:
- Diagnostic tools that use `IOCTL_AVB_READ/WRITE_REGISTER`
- Test harnesses that hardcode register addresses
- Any user-mode code accessing TIMINCA/TSAUXC directly

**Migration Strategy**:
1. Use `IOCTL_AVB_GET_CLOCK_CONFIG` instead of reading SYSTIM/TIMINCA/TSAUXC
2. Use `IOCTL_AVB_ADJUST_FREQUENCY` instead of writing TIMINCA
3. Use `IOCTL_AVB_GET/SET_TIMESTAMP` for SYSTIM access (already existed)
4. For genuine debugging needs, compile with Debug configuration

## Testing

### Compile Production Test
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /O2 /DNDEBUG /Zi /I include /I external/intel_avb/lib /I intel-ethernet-regs/gen tools/avb_test/ptp_clock_control_production_test.c /Fe:ptp_clock_control_production_test.exe'
```

### Run Production Test
```powershell
.\ptp_clock_control_production_test.exe
```

**Expected Output**:
- Test 1: ✓ Clock configuration query (SYSTIM, TIMINCA, TSAUXC)
- Test 2: ✓ Frequency adjustment (5 different values)
- Test 3: ✓ Timestamp setting and retrieval
- Test 4: ✓ Clock stability measurement

## Next Steps

1. ✅ **Compile driver in Release mode** to verify no build errors
2. ✅ **Run production test** to validate new IOCTLs
3. ⏳ **Migrate existing tests** to use production IOCTLs
4. ⏳ **Update documentation** (avb_ioctl.md) with new IOCTLs
5. ⏳ **Verify Release build** removes raw register access

## Architecture Compliance ✅

This change aligns with project principles:

✅ **Hardware-first policy**: Production IOCTLs still access real hardware  
✅ **Single Source of Truth**: Register offsets remain in SSOT (used by driver internally)  
✅ **No simulations**: All paths use real hardware access  
✅ **Security**: Debug-only features properly guarded  
✅ **Clean abstractions**: User-mode code uses proper API, not raw registers  

## References

- Intel I210 Datasheet Section 8.12.25 (SYSTIM registers)
- Intel I210 Datasheet Section 8.12.27 (TIMINCA)
- Intel I210 Datasheet Section 8.12.36 (TSAUXC)
- IEEE 1588-2008 (PTP clock requirements)
