# Intel AVB Filter Driver

A Windows NDIS 6.30 lightweight filter driver that provides AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet controllers.

## ⚠️ **CURRENT PROJECT STATUS - JANUARY 2025**

**This project is in active development with working architecture but simulated hardware access.**

### What Actually Works ✅
- **NDIS Filter Driver**: Complete lightweight filter implementation
- **Device Detection**: Successfully identifies and attaches to Intel I210/I217/I219/I225/I226 controllers
- **IOCTL Interface**: Full user-mode API with comprehensive command set
- **Intel AVB Integration**: Complete abstraction layer with platform operations
- **TSN Framework**: Advanced Time-Aware Shaper and Frame Preemption logic
- **Build System**: Successfully compiles for x64/ARM64 Windows

### What Needs Work ⚠️
- **Hardware Access**: Currently uses simulated/stubbed hardware responses
- **Real MMIO Operations**: Memory-mapped I/O needs actual hardware mapping
- **Production Testing**: Cannot be tested on real hardware without MMIO implementation
- **I217 Support**: Missing from device identification (exists in code but not exposed)

## 🏗️ Architecture

```
┌─────────────────┐
│ User Application│
└─────────┬───────┘
          │ DeviceIoControl
┌─────────▼───────┐    ✅ WORKING: IOCTL interface complete
│ NDIS Filter     │    ✅ WORKING: Filter driver functional
│ Driver          │
└─────────┬───────┘
          │ Platform Ops
┌─────────▼───────┐    ⚠️  SIMULATED: Hardware access layer
│ Intel AVB       │    ⚠️  STUBBED: Register operations
│ Library         │
└─────────┬───────┘
          │ Hardware Access
┌─────────▼───────┐    ❌ NOT CONNECTED: Real hardware I/O
│ Intel Ethernet  │
│ Controller      │
└─────────────────┘
```

## 🔧 Quick Start

### Prerequisites
- Windows Driver Kit (WDK)
- Visual Studio 2019/2022
- Intel Ethernet Controller (I210, I217, I219, I225, or I226)

### Build
```cmd
git clone --recursive https://github.com/zarfld/intel_avb.git
cd intel_avb
# Open IntelAvbFilter.sln in Visual Studio and build
```

### Install
```cmd
# Copy driver files to temp directory
pnputil /add-driver IntelAvbFilter.inf /install
```

**Note**: Driver installs and loads successfully but hardware operations are simulated.

## 📋 Device Support Status

| Controller | Device Detection | API Implementation | Hardware Access | TSN Features |
|------------|------------------|-------------------|----------------|--------------|
| Intel I210 | ✅ Working | ✅ Complete | ⚠️ Simulated | ⚠️ Simulated |
| Intel I217 | ❌ Missing in device list | ✅ Code exists | ⚠️ Simulated | ❌ Not supported |
| Intel I219 | ✅ Working | ✅ Complete | ⚠️ Simulated | ⚠️ Basic only |
| Intel I225 | ✅ Working | ✅ Complete | ⚠️ Simulated | ⚠️ Simulated |
| Intel I226 | ✅ Working | ✅ Complete | ⚠️ Simulated | ⚠️ Simulated |

### Status Legend
- ✅ **Working**: Implemented and functional
- ⚠️ **Simulated**: Logic complete but uses fake hardware responses
- ❌ **Missing**: Not implemented or not exposed

## 🚧 Implementation Details

### What's Implemented (Simulated Hardware)
```c
// Real NDIS integration with simulated register patterns
int AvbMmioReadReal(device_t *dev, ULONG offset, ULONG *value) {
    // Returns realistic values based on Intel specifications
    // BUT: Not reading from actual hardware registers
    switch (offset) {
        case 0x0B600: // IEEE 1588 timestamp - simulated
            *value = simulated_timestamp_low();
            break;
        // ... more registers
    }
}
```

### Missing Real Hardware Integration
- Direct memory mapping to Intel controller registers
- Actual PCI configuration space access
- Real IEEE 1588 timestamp register reads
- Physical MDIO bus operations
- Hardware TSN register programming

## 📚 Documentation

- [`README_AVB_Integration.md`](README_AVB_Integration.md) - Integration architecture
- [`TODO.md`](TODO.md) - Honest development status and next steps  
- [`external/intel_avb/README.md`](external/intel_avb/README.md) - Intel library status
- [`external/intel_avb/spec/README.md`](external/intel_avb/spec/README.md) - Hardware specifications

## 🧪 Current Testing Status

### What Can Be Tested Now ✅
```cmd
# Build and run test application  
nmake -f avb_test.mak
avb_test.exe

# Results: Device detection works, simulated responses returned
```

### What Cannot Be Tested ❌
- Real hardware register access
- Actual IEEE 1588 timestamping  
- Hardware-based TSN features
- Production timing accuracy

## 🔍 Debug Output

Enable debug tracing shows realistic simulation:
```
[TRACE] Device I219-LM detected successfully
[INFO]  PCI Config: VID=0x8086, DID=0x15B7 (simulated)
[INFO]  MMIO Read: offset=0x0B600, value=0x12340001 (Intel spec pattern)
[WARN]  Hardware access is simulated - not production ready
```

## 🎯 Next Steps for Production

### Phase 1: Real Hardware Access (High Priority)
1. **MMIO Mapping**: Implement actual memory-mapped I/O to Intel registers
2. **PCI Access**: Real configuration space access (beyond device detection)
3. **Hardware Validation**: Test on actual Intel controllers

### Phase 2: Production Features
1. **I217 Integration**: Add missing I217 to device identification
2. **Performance Optimization**: Remove simulation overhead
3. **Hardware Testing**: Validate timing accuracy on real hardware

### Phase 3: Advanced Features  
1. **TSN Validation**: Test Time-Aware Shaper on I225/I226 hardware
2. **Multi-device Sync**: Cross-controller synchronization
3. **Production Deployment**: Documentation and installation guides

## 🤝 Contributing

**Current Focus**: Real hardware access implementation

1. Fork the repository
2. Focus on removing simulation/stub code
3. Test on real Intel hardware if available
4. Update documentation with actual results
5. Submit pull request with hardware validation

## ⚠️ Important Notes

- **Not Production Ready**: Current implementation uses simulated hardware
- **Driver Loads Successfully**: NDIS filter works but hardware access is fake
- **Architecture is Sound**: Framework ready for real hardware implementation
- **Comprehensive Specs Available**: Intel datasheets provide all register details

## 📄 License

This project incorporates the Intel AVB library and follows its licensing terms. See [`LICENSE.txt`](LICENSE.txt) for details.

## 🙏 Acknowledgments

- Intel Corporation for the AVB library foundation and comprehensive hardware specifications
- Microsoft for the NDIS framework documentation
- The TSN and AVB community for specifications and guidance

---

**Last Updated**: January 2025  
**Status**: Architecture Complete - Hardware Access Needed  
**Next Milestone**: Real MMIO Implementation
