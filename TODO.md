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

### 2. AVB Integration Layer (avb_integration.c)
- [ ] **Implement PCI config space read/write through NDIS**
- [ ] **Implement MMIO read/write through mapped memory**
- [ ] **Implement MDIO read/write through NDIS OID or direct register access**
- [ ] **Implement timestamp read from hardware**
- [ ] **Check if filter instance is Intel adapter (device detection)**

---

## ⚠️ MEDIUM PRIORITY

### 3. Driver INF Customization (IntelAvbFilter.inf)
- [ ] Customize manufacturer name, architecture, and filter class
- [ ] Remove sample parameters and comments
- [ ] Ensure media types and filter type are correct
- [ ] Add related files and service flags as needed

### 4. Filter Driver Source (filter.h)
- [ ] Customize pool tags for memory leak tracking
- [ ] Specify correct NDIS contract version

---

## 📝 LOW PRIORITY

### 5. Documentation and Cleanup
- [ ] Remove or update sample comments in INF and source files
- [ ] Add more detailed error handling and debug output
- [ ] Update README files as features are implemented

---

## Reference: Upstream TODOs
- See `external/intel_avb/TODO.md` for additional upstream library tasks

---

**Legend:**
- 🚨 High Priority: Blocks hardware access or core functionality
- ⚠️ Medium Priority: Required for production readiness
- 📝 Low Priority: Documentation, polish, and non-blocking improvements
