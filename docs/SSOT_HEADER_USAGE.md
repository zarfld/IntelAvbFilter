# SSOT Header Usage - Intel AVB Filter Driver

## ? **MANDATORY: Use SSOT Header Path**

**CRITICAL ARCHITECTURAL REQUIREMENT**: All code must use the **Single Source of Truth (SSOT)** header:

```c
// ? CORRECT - SSOT path (ALWAYS use this)
#include "external/intel_avb/include/avb_ioctl.h"

// ? WRONG - Legacy/duplicate path (NEVER use this)  
#include "include/avb_ioctl.h"
```

## ?? **File Structure**

```
Intel AVB Filter Driver/
??? external/intel_avb/include/avb_ioctl.h  ? ?? SSOT (Single Source of Truth)
?   ??? Purpose: Authoritative IOCTL definitions
?   ??? Status: ? MAINTAINED and SYNCHRONIZED
?
??? include/avb_ioctl.h                     ? ? LEGACY (should not be used)
    ??? Purpose: Historical duplicate
    ??? Status: ?? MAY BE OUT OF SYNC
```

## ?? **Why SSOT Matters**

According to copilot instructions:
- **"no duplicate or redundant implementations"**  
- **"use centralized, reusable functions instead"**
- **"no implementation based assumptions"**

### **Problems with Wrong Path**:
1. **?? Synchronization Issues**: Two headers can drift apart
2. **?? Build Failures**: Different struct definitions cause link errors  
3. **? Architecture Violation**: Contradicts clean codebase principles

### **Benefits of SSOT Path**:
1. **? Guaranteed Consistency**: Single source, no drift
2. **? Clean Architecture**: Follows established patterns
3. **? Easy Maintenance**: One file to update

## ?? **How to Fix Include Paths**

### **In User-Mode Test Files**:
```c
// From tools/avb_test/ directory:
#include "../../external/intel_avb/include/avb_ioctl.h"

// From tests/taef/ directory:  
#include "../../external/intel_avb/include/avb_ioctl.h"

// From root directory:
#include "external/intel_avb/include/avb_ioctl.h"
```

### **In Makefiles**:
```makefile
CFLAGS = /I../../external/intel_avb/include
```

## ?? **Files Fixed for SSOT Compliance**

### ? **Fixed Files**:
- `tools/avb_test/avb_capability_validation_test_um.c`
- `tools/avb_test/avb_device_separation_test_um.c`  
- `tools/avb_test/avb_i226_test.c`
- `tools/avb_test/avb_i226_advanced_test.c`
- `tools/avb_test/hardware_investigation_tool.c`
- `tests/taef/AvbTestCommon.h`
- `tools/avb_test/avb_capability_validation.mak`
- `tools/avb_test/avb_device_separation_validation.mak`

### ?? **Check Your Code**:
```bash
# Find files using wrong include path:
grep -r "include.*avb_ioctl.h" . | grep -v external/intel_avb
```

## ?? **Build System Integration**

All makefiles and build configurations should use:
```makefile
INCLUDE_PATH = /I$(ROOT)/external/intel_avb/include
```

This ensures:
- ? **Consistent header resolution**
- ? **No path confusion**  
- ? **Architecture compliance**

## ?? **Future Prevention**

1. **Code Review**: Check all new files use SSOT path
2. **Documentation**: Reference this file in PR templates  
3. **Build Validation**: Consider build-time checks for wrong paths
4. **Team Training**: Ensure all developers know SSOT requirement

---

**Remember**: **ALWAYS use `external/intel_avb/include/avb_ioctl.h`** ?

This is a **mandatory architectural requirement** per copilot instructions.