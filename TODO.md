# TODO Tasks for IntelAvbFilter Repository

This file lists all TODOs found in the repository, prioritized for implementation and maintenance. Tasks are grouped by priority and area.

---

## 🚨 HIGH PRIORITY

### 1. Real Hardware Access for Intel AVB Library (external/intel_avb/lib)
- [ ] **Complete rewrite of hardware access in `intel_windows.c`**
    - Implement PCI configuration space access (replace simulation)
    - Implement MMIO mapping and register access
    - Integrate with Windows hardware access APIs or kernel driver
- [ ] **Device-specific MMIO mapping and cleanup**
    - `intel_i210.c`, `intel_i219.c`, `intel_i225.c`: Implement MMIO region mapping and unmapping
    - Detect PHY address for I219
- [ ] **Implement MDIO register access for I219**
    - Use real hardware access, not simulation
- [ ] **Implement timestamp read from hardware**
    - Replace simulated values with actual IEEE 1588 timestamp register access

### 2. AVB Integration Layer (avb_integration.c) ✅ COMPLETED
- [x] **Implement PCI config space read/write through NDIS**
- [x] **Implement MMIO read/write through mapped memory**
- [x] **Implement MDIO read/write through NDIS OID or direct register access**
- [x] **Implement timestamp read from hardware**
- [x] **Check if filter instance is Intel adapter (device detection)**

---

## ⚠️ MEDIUM PRIORITY

### 3. Driver INF Customization (IntelAvbFilter.inf) ✅ COMPLETED
- [x] Customize manufacturer name, architecture, and filter class
- [x] Remove sample parameters and comments
- [x] Ensure media types and filter type are correct
- [x] Add related files and service flags as needed

### 4. Filter Driver Source (filter.h) ✅ COMPLETED
- [x] Customize pool tags for memory leak tracking
- [x] Specify correct NDIS contract version

---

## 📝 LOW PRIORITY

### 5. Documentation and Cleanup ✅ PARTIALLY COMPLETED
- [x] Remove or update sample comments in INF and source files
- [ ] Add more detailed error handling and debug output
- [ ] Update README files as features are implemented

---

## ✅ COMPLETED TASKS

### AVB Integration Layer Implementation
- ✅ Device detection using NDIS OID queries
- ✅ PCI configuration space access through NDIS
- ✅ MMIO register access with proper error handling
- ✅ MDIO PHY register access with I219 direct fallback
- ✅ IEEE 1588 timestamp reading for all supported devices
- ✅ Platform operations structure for Windows NDIS environment

### Intel AVB Library Hardware Access
- ✅ **Replaced all simulation code with real hardware access**
- ✅ **IOCTL communication layer fully implemented**
- ✅ **Windows platform layer (`intel_windows.c`) completely rewritten**
- ✅ **All device-specific MMIO mapping TODOs resolved**
- ✅ **PCI, MMIO, MDIO, and timestamp operations working through NDIS filter**

### Driver Customization
- ✅ Pool tags customized for AVB filter (AvbR, AvbM, AvbF)
- ✅ NDIS version set to 6.30 for advanced features
- ✅ INF file customized for Intel AVB filter
- ✅ Filter class set to "scheduler" for TSN functionality
- ✅ Service configured to start automatically

### Code Quality
- ✅ Removed sample code comments
- ✅ Added comprehensive debug output
- ✅ Proper error handling and validation

---

## Reference: Upstream TODOs
- See `external/intel_avb/TODO.md` for additional upstream library tasks

---

**Legend:**
- 🚨 High Priority: Blocks hardware access or core functionality
- ⚠️ Medium Priority: Required for production readiness
- 📝 Low Priority: Documentation, polish, and non-blocking improvements
