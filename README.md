# Intel AVB Filter Driver

A Windows NDIS 6.30 lightweight filter driver that provides AVB (Audio/Video Bridging) and TSN (Time-Sensitive Networking) capabilities for Intel Ethernet controllers.

## ✨ Features

- **Complete Hardware Access**: Real hardware access for Intel I210, I219, I225, and I226 controllers
- **IEEE 1588 Support**: Precision Time Protocol (PTP) timestamping
- **TSN Ready**: Time-Sensitive Networking foundation for advanced features
- **NDIS Integration**: Seamless integration with Windows networking stack
- **User-Mode API**: IOCTL interface for applications

## 🏗️ Architecture

```
┌─────────────────┐
│ User Application│
└─────────┬───────┘
          │ DeviceIoControl
┌─────────▼───────┐
│ NDIS Filter     │
│ Driver          │
└─────────┬───────┘
          │ Platform Ops
┌─────────▼───────┐
│ Intel AVB       │
│ Library         │
└─────────┬───────┘
          │ Hardware Access
┌─────────▼───────┐
│ Intel Ethernet  │
│ Controller      │
└─────────────────┘
```

## 🔧 Quick Start

### Prerequisites
- Windows Driver Kit (WDK)
- Visual Studio 2019/2022
- Intel Ethernet Controller (I210, I219, I225, or I226)

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

## 📋 Status

### ✅ Completed
- **NDIS Filter Driver**: Full lightweight filter implementation
- **Hardware Access**: Complete PCI, MMIO, MDIO, and timestamp access
- **Intel AVB Integration**: Library fully integrated with real hardware access
- **Device Support**: I210, I219, I225, I226 controllers supported
- **Error Handling**: Comprehensive error reporting and debug output

### 🚀 Advanced Features (Available for Implementation)
- Time-Aware Shaper (TAS)
- Frame Preemption (FP) 
- PCIe Precision Time Measurement (PTM)
- Advanced TSN traffic shaping

## 🎯 Hardware Support

| Controller | Status | Features |
|------------|--------|----------|
| Intel I210 | ✅ Full | IEEE 1588, Basic TSN |
| Intel I219 | ✅ Full | IEEE 1588, MDIO |
| Intel I225 | ✅ Full | IEEE 1588, Advanced TSN |
| Intel I226 | ✅ Full | IEEE 1588, Advanced TSN |

## 📚 Documentation

- [`README_AVB_Integration.md`](README_AVB_Integration.md) - Detailed integration guide
- [`TODO.md`](TODO.md) - Development progress and roadmap
- [`external/intel_avb/README.md`](external/intel_avb/README.md) - Intel AVB library docs

## 🧪 Testing

Build and run the test application:
```cmd
nmake -f avb_test.mak
avb_test.exe
```

## 🔍 Debug Output

Enable debug tracing in DebugView or kernel debugger:
- IOCTL operation tracking
- Hardware access tracing
- Error reporting with specific failure details
- Device state monitoring

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Follow existing code style
4. Add appropriate debug output
5. Update documentation
6. Submit a pull request

## 📄 License

This project incorporates the Intel AVB library and follows its licensing terms. See [`LICENSE.txt`](LICENSE.txt) for details.

## 🙏 Acknowledgments

- Intel Corporation for the AVB library foundation
- Microsoft for the NDIS framework documentation
- The TSN and AVB community for specifications and guidance
