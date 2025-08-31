# Intel AVB Filter Driver - TODO

## ??? **Current Development Phase: Hardware Validation**

The driver is **build-complete** and ready for hardware testing. All major implementation work is finished.

---

## ?? **Immediate Next Steps**

### **Hardware Deployment Testing**
- [ ] Test on system with Intel I210 controller
- [ ] Test on system with Intel I219 controller  
- [ ] Test on system with Intel I225 controller
- [ ] Test on system with Intel I226 controller
- [ ] Verify device node creation (`\\.\IntelAvbFilter`)

### **I210 PTP Validation**
- [ ] Verify SYSTIM registers increment properly
- [ ] Test clock initialization sequence
- [ ] Validate timestamp accuracy (compare with reference)
- [ ] Test PTP state transitions (BOUND ? BAR_MAPPED ? PTP_READY)

### **Multi-Adapter Scenarios**
- [ ] Test systems with multiple Intel controllers
- [ ] Verify context switching between adapters
- [ ] Test concurrent access to different controllers

---

## ?? **Potential Hardware-Specific Fixes**

These may be needed after hardware testing:

### **I210 Clock Initialization**
```c
// May need adjustment based on hardware behavior
KeStallExecutionProcessor(200000); // Current: 200ms, may need tuning
```

### **Register Access Timing**
```c
// May need hardware-specific delays
// Different Intel controllers may have different MMIO timing requirements
```

### **BAR0 Discovery Edge Cases**
```c
// NDIS resource enumeration may behave differently on various systems
// Alternative discovery methods may be needed
```

---

## ?? **Explicitly NOT Planned**

### **Simulation/Fake Modes**
- Will NOT add hardware simulation
- Will NOT add fake timestamp generation  
- Will NOT add stub implementations
- **Reason**: No false advertising, real hardware only

### **Legacy Controller Support**
- Will NOT support Intel 82574L (lacks AVB/TSN features)
- Will NOT support non-Intel controllers
- **Reason**: Driver is Intel AVB/TSN specific

### **Alternative Architectures**
- Will NOT implement as standalone miniport driver
- Will NOT implement as user-mode library
- **Reason**: NDIS filter is the correct architecture for this use case

---

## ?? **Success Metrics**

### **Phase 1: Basic Hardware Validation**
- [ ] Device node creation on real hardware
- [ ] Successful IOCTL communication
- [ ] Basic register read/write operations

### **Phase 2: PTP Functionality**  
- [ ] I210 SYSTIM clock running and incrementing
- [ ] Timestamp capture working
- [ ] IEEE 1588 basic compliance

### **Phase 3: Advanced Features**
- [ ] TSN Time-Aware Shaper (I225/I226)
- [ ] Frame Preemption (I225/I226)
- [ ] Multi-adapter context switching

### **Phase 4: Production Readiness**
- [ ] Performance validation
- [ ] Stability testing
- [ ] Production driver signing

---

## ?? **Hardware Testing Requirements**

To complete validation, need access to:
- Intel I210 Gigabit controller
- Intel I219 Gigabit controller  
- Intel I225 2.5G controller
- Intel I226 2.5G controller

**Current Status**: Implementation complete, waiting for hardware access.

---

**Last Updated**: January 2025  
**Next Milestone**: Hardware deployment testing