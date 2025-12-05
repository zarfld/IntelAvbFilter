# Intel AVB Filter Driver - Changelog

## Version 1.2 - January 2025

### ✅ New Feature: Complete RX Packet Timestamping API

**Added 4 new production IOCTLs** for comprehensive RX packet timestamping support:

1. **IOCTL_AVB_SET_RX_TIMESTAMP** (Code 41)
   - Controls RXPBSIZE.CFG_TS_EN bit (register 0x2404)
   - Enables 16-byte timestamp allocation in RX packet buffer
   - Warns when port software reset required

2. **IOCTL_AVB_SET_QUEUE_TIMESTAMP** (Code 42)
   - Controls SRRCTL[n].TIMESTAMP bit (per-queue)
   - Enables hardware timestamping for specific receive queues
   - Supports 4 queues (I210/I226) or 2 queues (I211)

3. **IOCTL_AVB_SET_TARGET_TIME** (Code 43)
   - Configures TRGTTIML/H registers (0x0B644-0x0B650)
   - Enables time-triggered interrupts when SYSTIM crosses target
   - Supports 2 independent target time channels

4. **IOCTL_AVB_GET_AUX_TIMESTAMP** (Code 44)
   - Reads AUXSTMP0/1 registers (0x0B65C-0x0B668)
   - Captures timestamps on SDP pin events
   - Supports clearing AUTT flags after reading

**Documentation**:
- Complete RX timestamping guide: `docs/RX_PACKET_TIMESTAMPING.md`
- IOCTL API reference: `docs/IOCTL_API_SUMMARY.md`
- Hardware register reference with bit definitions

**Test Files**:
- `tools/avb_test/rx_timestamping_test.c` - RX packet timestamping tests
- `tools/avb_test/target_time_test.c` - Target time and auxiliary timestamp tests

**Intel Specification Compliance**:
- I210 Datasheet v3.7 Section 7.3.1 (RX Packet Timestamping)
- I225 Software Manual v1.3 Chapter 5 (Time Synchronization)
- Accurate bit positions and register addresses from SSOT headers

**Hardware Support**:
- I210: Basic RX timestamping (1 SYSTIM, 2 target times, 2 aux timestamps)
- I225/I226: Advanced timestamping (4 SYSTIM timers, full feature set)

---

## Version 1.1 - January 2025

### ✅ Major Fix: TSN IOCTL Handlers Implementation

**Issue**: TAS, Frame Preemption, and PTM IOCTLs returned `ERROR_INVALID_FUNCTION` (Error 1)  
**Root Cause**: Missing case handlers in `AvbDeviceControl` IOCTL switch statement  
**Impact**: TSN features completely non-functional from user-mode applications

#### **What Was Fixed**:
1. **Added Missing IOCTL Case Handlers**:
   - `IOCTL_AVB_SETUP_TAS` ? `intel_setup_time_aware_shaper()`
   - `IOCTL_AVB_SETUP_FP` ? `intel_setup_frame_preemption()`
   - `IOCTL_AVB_SETUP_PTM` ? `intel_setup_ptm()`

2. **Intel Library Integration**:
   - Connected handlers to existing Intel library functions
   - Added proper capability validation
   - Implemented device context management

3. **Error Handling & Debugging**:
   - Added comprehensive status reporting
   - Debug trace output for troubleshooting
   - Proper error code propagation

#### **Hardware Validation Results**:

**Before Fix**:
```
? IOCTL_AVB_SETUP_TAS: Not implemented (Error: 1)
? IOCTL_AVB_SETUP_FP: Not implemented (Error: 1)
? IOCTL_AVB_SETUP_PTM: Not implemented (Error: 1)
```

**After Fix** (Verified on I210 + I226 Hardware):
```
? IOCTL_AVB_SETUP_TAS: Handler exists and succeeded (FIX WORKED)
? IOCTL_AVB_SETUP_FP: Handler exists and succeeded (FIX WORKED)
? IOCTL_AVB_SETUP_PTM: Handler exists, returned error 31 (FIX WORKED)
```

#### **Testing Infrastructure**:
- Created `test_tsn_ioctl_handlers.exe` verification tool
- Added to automated test suite (`run_tests.ps1`)
- Comprehensive multi-adapter validation completed

#### **Files Modified**:
- `avb_integration_fixed.c` - Added missing IOCTL case handlers
- `tools/avb_test/test_tsn_ioctl_handlers_um.c` - New validation tool
- `tools/test_tsn_ioctl_handlers.c` - Kernel build placeholder
- `tools/avb_test/tsn_ioctl_test.mak` - Build system integration

#### **Development Methodology**:
This fix was identified through **Hardware-first validation** rather than code analysis alone. The service team's approach of testing with real hardware revealed the gap between implemented functionality and actual IOCTL routing.

**Key Lesson**: Hardware testing is essential - code analysis alone missed this critical implementation gap.

---

## Version 1.0 - December 2024

### Initial Release
- NDIS 6.30 filter driver infrastructure
- Intel AVB library integration
- Multi-adapter support
- Hardware register access
- PTP timestamping
- MDIO PHY management
- Credit-based shaping (QAV)

### Known Issues in v1.0:
- TSN IOCTL handlers returned ERROR_INVALID_FUNCTION (**FIXED in v1.1**)
- I210 PTP clock initialization issues (ongoing)
- I226 TAS/FP hardware activation issues (ongoing)

---

## Development Notes

### Testing Philosophy
- **Hardware-first validation**: All fixes validated on real Intel hardware
- **No simulation**: Production code paths use real hardware access only
- **Comprehensive testing**: Multi-adapter scenarios with I210 + I226 combinations

### Quality Standards
- All commits must compile successfully
- Hardware validation required for significant changes
- Proper error handling and debug output
- SSOT (Single Source of Truth) register definitions

### Next Milestones
1. **I210 PTP Clock Fix**: Resolve clock initialization issues
2. **I226 Hardware TSN Activation**: Research Intel-specific requirements
3. **Production Hardening**: Performance optimization and edge case handling