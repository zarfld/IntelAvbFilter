---
name: ssot-and-magic-numbers
description: "Use when writing or reviewing C code in src/ or devices/ that touches registers, IOCTLs, or hardware constants — raw hex register offsets (e.g. 0x0B644), duplicate IOCTL defines, or wrong include paths are violations. Also use before running or interpreting TEST-SSOT-001/003/004 or TEST-REGS-002 failures."
---

# SSOT and Magic Numbers

## Overview

This repo has **two SSOT rules that must never be broken**:
1. **IOCTL SSOT**: `include/avb_ioctl.h` is the *only* place IOCTL codes are defined.  
2. **Register SSOT**: `intel-ethernet-regs/gen/<device>_regs.h` is the *only* place register offsets and bit-field constants may originate.

Raw hex register literals in `src/` or `devices/` are magic-number violations caught by CI (`test_magic_numbers.c`, `Test-REGS-002-MagicNumberDetection.ps1`).

---

## When to Use

- You're adding register access code in `devices/intel_*_impl.c`
- You're adding an include for `avb_ioctl.h` anywhere in the repo
- You see a raw hex literal like `0x0B644` and wonder if it should be a named constant
- A CI test named **TEST-SSOT-001**, **TEST-SSOT-003**, **TEST-SSOT-004**, or **TEST-REGS-002** is failing
- Code review catches something like `uint32_t reg = 0x0B600;`
- You're adding a new IOCTL code or NIC register definition

**NOT for:** general coding style, NDIS callback logic, test output parsing.

---

## Rule 1 — IOCTL Single Source of Truth

### The Rule

```c
// ✅ CORRECT — SSOT path (always)
#include "include/avb_ioctl.h"         // from repo root
#include "../../include/avb_ioctl.h"   // from tools/avb_test/ or tests/taef/

// ❌ WRONG — legacy path (never)
#include "external/intel_avb/include/avb_ioctl.h"
```

```c
// ❌ WRONG — defining IOCTL codes outside SSOT header
#define IOCTL_AVB_MY_NEW_OP  CTL_CODE(...)   // in src/, devices/, tools/

// ✅ CORRECT — add to include/avb_ioctl.h ONLY
```

### What the CI Checks (GitHub Issue traceability)

| Test | Script | Verifies |
|------|--------|----------|
| TEST-SSOT-001 | `tests/verification/ssot/Test-SSOT-001-NoDuplicates.ps1` | No `#define IOCTL_AVB_*` outside `include/avb_ioctl.h` |
| TEST-SSOT-003 | `tests/verification/ssot/Test-SSOT-003-IncludePattern.ps1` | Files using IOCTLs include SSOT header, not legacy path |
| TEST-SSOT-004 | `tests/verification/ssot/Test-SSOT-004-Completeness.ps1` | Core IOCTLs present in SSOT header |

Run all at once: `tests/verification/ssot/Run-All-SSOT-Tests.ps1`

---

## Rule 2 — Register SSOT (No Magic Numbers)

### The Rule

Register offsets and bit-field constants for Intel NICs live in `intel-ethernet-regs/gen/`.  
**Never write raw hex register offsets in `src/` or `devices/`.**

```c
// ❌ WRONG — magic number
uint32_t systiml = intel_read_reg(dev, 0x0B600);
uint32_t tsauxc  = intel_read_reg(dev, 0x0B640);
intel_write_reg(dev, 0x0B644, target_ns & 0xFFFFFFFF);

// ✅ CORRECT — named constant from SSOT header
#include "intel-ethernet-regs/gen/i226_regs.h"

uint32_t systiml = intel_read_reg(dev, I226_SYSTIML);
uint32_t tsauxc  = intel_read_reg(dev, I226_TSAUXC);
intel_write_reg(dev, I226_TRGTTIML0, target_ns & 0xFFFFFFFF);
```

### Device → Header → Prefix Quick Reference

| NIC Family | SSOT Header | Constant Prefix |
|------------|-------------|-----------------|
| I210 | `intel-ethernet-regs/gen/i210_regs.h` | `I210_` |
| I211 | `intel-ethernet-regs/gen/i211_regs.h` | `I211_` |
| I217 | `intel-ethernet-regs/gen/i217_regs.h` | `I217_` |
| I219 | `intel-ethernet-regs/gen/i219_regs.h` | `I219_` |
| I225 | `intel-ethernet-regs/gen/i225_regs.h` | `I225_` |
| I226 | `intel-ethernet-regs/gen/i226_regs.h` | `I226_` |

All registers are in the `intel-ethernet-regs` submodule (path: `intel-ethernet-regs/`).  
Headers are auto-generated from YAML by `intel-ethernet-regs/tools/reggen.py`.  
**Treat generated headers as read-only** — add new registers to the YAML source, then regenerate.

### What CI Detects as a Magic Number

A hex literal with **4+ hex digits** (`>= 0x1000`) on a **code line** in `src/` or `devices/` is a violation.

**Excluded from checks** (these are always allowed):
- Lines starting with `//` or `/*` (comments)
- Lines starting with `#define` (constant definitions)
- Lines starting with `#include`
- The sentinel `0xFFFFFFFF`

| Test | Script | Verifies |
|------|--------|----------|
| TEST-REGS-002 | `tests/verification/regs/test_magic_numbers.c` | Zero hex literals ≥ 0x1000 in `src/` and `devices/` code lines |
| TEST-REGS-002 (PS) | `tests/verification/regs/Test-REGS-002-MagicNumberDetection.ps1` | Same rule, PowerShell runner |

### Adding a Register That Doesn't Exist Yet

1. Add the register to the YAML device file:  
   `intel-ethernet-regs/devices/<device>.yaml`
2. Regenerate the header:  
   ```powershell
   py -3 intel-ethernet-regs/tools/reggen.py intel-ethernet-regs/devices/<device>.yaml intel-ethernet-regs/gen
   ```
3. Commit both the YAML change and the regenerated `.h`.
4. Use the new named constant in your C code.

---

## Rule 3 — HAL Boundary (src/ must be register-free)

`src/` is the generic NDIS filter integration layer — it **must not** contain register offsets, device IDs, or hardware-specific bit masks. Those belong exclusively in `devices/intel_*_impl.c`.

```c
// ❌ WRONG — register address in src/avb_integration_fixed.c
intel_write_reg(dev, I226_TSAUXC, value);   // constant name is fine, but calling
                                             // write directly here is a HAL violation

// ✅ CORRECT — call through HAL ops
dev->ops->set_auxtime_config(dev, config);
```

See `hardware_abstraction_SRC.md` for the full HAL enforcement rules.

---

## Common Mistakes

| Anti-Pattern | Why It Fails | Fix |
|---|---|---|
| `#include "external/intel_avb/include/avb_ioctl.h"` | Legacy path, may be out of sync | Use `include/avb_ioctl.h` |
| `#define IOCTL_AVB_FOO CTL_CODE(...)` in `src/foo.c` | Violates TEST-SSOT-001; duplicates definition | Move to `include/avb_ioctl.h` |
| `read_reg(dev, 0x0B640)` in `devices/intel_i226_impl.c` | TYPE: magic number, violates TEST-REGS-002 | Replace with `I226_TSAUXC` |
| `read_reg(dev, 0x0B640)` in `src/avb_integration_fixed.c` | Both magic number AND HAL boundary violation | Move to device impl, use ops pointer |
| Editing `intel-ethernet-regs/gen/*.h` directly | Generated file — will be overwritten on next reggen run | Edit the YAML source, regenerate |
| `#define MY_REG_BIT (1 << 3)` in `src/` | Inline bit mask = magic number equivalent | Use the `I226_TSAUXC_*_MASK`/`_SHIFT` macros from the generated header |

---

## Verification Checklist (Before Committing C Code in src/ or devices/)

- [ ] No raw hex literals ≥ 0x1000 on code lines (use named constants)
- [ ] All register constants imported from `intel-ethernet-regs/gen/<device>_regs.h`
- [ ] All IOCTL codes defined only in `include/avb_ioctl.h`
- [ ] All includes of `avb_ioctl.h` use the SSOT path (not `external/`)
- [ ] Register access in `devices/` only — none in `src/`
- [ ] Run `tests/verification/ssot/Run-All-SSOT-Tests.ps1` — all pass
- [ ] Run `tests/verification/regs/Test-REGS-002-MagicNumberDetection.ps1` — 0 violations
