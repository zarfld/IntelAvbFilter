---
description: "CRITICAL architectural requirement: C source and header files must use the authoritative AVB IOCTL Single Source of Truth (SSOT) header."
applyTo: "**/*.c,**/*.h"
---

# SSOT Header Usage - Intel AVB Filter Driver

## MANDATORY: Use the SSOT header path

**CRITICAL ARCHITECTURAL REQUIREMENT**: all affected code must use the **Single Source of Truth (SSOT)** header:

```c
// CORRECT - authoritative SSOT path
#include "include/avb_ioctl.h"

// WRONG - legacy/duplicate path
#include "external/intel_avb/include/avb_ioctl.h"
```

## File structure

```text
Intel AVB Filter Driver/
├── include/avb_ioctl.h
│   ├── Purpose: authoritative IOCTL definitions
│   └── Status: maintained SSOT
└── external/intel_avb/include/avb_ioctl.h
    ├── Purpose: historical duplicate
    └── Status: legacy; may be out of sync and must not be used as the project IOCTL SSOT
```

## Why SSOT matters

This rule implements the repository principles to avoid duplicate/redundant implementations, centralize reusable definitions, and avoid implementation-based assumptions.

Using the legacy path risks:

1. synchronization drift between duplicate headers;
2. build or ABI failures caused by different structure/constant definitions;
3. violation of the repository's one-source-of-truth architecture.

Using the canonical path provides one maintained definition set and a clear review/build contract.

## Include-path guidance

### User-mode test files

```c
// From tools/avb_test/:
#include "../../include/avb_ioctl.h"

// From tests/taef/:
#include "../../include/avb_ioctl.h"

// From the repository root:
#include "include/avb_ioctl.h"
```

### Makefiles / build configuration

Configure the repository `include` directory, not the legacy external duplicate:

```makefile
CFLAGS = /I../../include
```

For root-based build variables:

```makefile
INCLUDE_PATH = /I$(ROOT)/include
```

The exact relative form may vary by build file, but it must resolve to the authoritative repository `include/` directory and must not make `external/intel_avb/include` the source of AVB IOCTL definitions.

## Files historically fixed for SSOT compliance

- `tools/avb_test/avb_capability_validation_test_um.c`
- `tools/avb_test/avb_device_separation_test_um.c`
- `tools/avb_test/avb_i226_test.c`
- `tools/avb_test/avb_i226_advanced_test.c`
- `tools/avb_test/hardware_investigation_tool.c`
- `tests/taef/AvbTestCommon.h`
- `tools/avb_test/avb_capability_validation.mak`
- `tools/avb_test/avb_device_separation_validation.mak`

Treat this list as historical evidence, not as an exhaustive statement of current compliance. Verify the current tree before making claims.

## Check for violations

```bash
# Find direct references to the legacy duplicate path in C/header code:
grep -R --include='*.c' --include='*.h' \
  'external/intel_avb/include/avb_ioctl.h' .
```

A clean result is no production/test C or header file including that legacy path, unless an explicitly documented migration/compatibility case has been approved.

## Future prevention

1. **Code review**: reject new code that treats the legacy duplicate as authoritative.
2. **Build validation**: keep include paths pointed at the repository `include/` SSOT.
3. **Architecture discipline**: when definitions change, update the canonical SSOT rather than creating another copy.
4. **Evidence first**: verify actual include resolution and current repository state before claiming compliance.

---

**Remember**: use the authoritative `include/avb_ioctl.h` definition set. This is a mandatory architectural rule.