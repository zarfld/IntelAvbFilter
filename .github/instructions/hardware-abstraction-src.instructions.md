---
description: "CRITICAL HAL boundary enforcement for generic src/ code; prevents device-specific register logic from leaking into the generic driver layer."
applyTo: "src/**"
---

# Hardware Abstraction Layer — `src/**`

The Hardware Abstraction Layer boundary is mandatory. When creating, editing, reviewing, or refactoring files under `src/**`, **consult and apply the complete preserved HAL guidance in** `.github/docs/HARDWARE-ABSTRACTION-SRC-REFERENCE.md`.

Moving the detailed guidance out of this auto-applied file is a context-routing optimization only. Its architectural rules remain intentional and applicable.

## Mandatory boundary rules

- `src/**` MUST remain adapter-agnostic. Do not add Intel-controller-specific register offsets, masks, device IDs, or controller-specific behavior there.
- Device-specific operations belong behind `devices/intel_device_interface.h` / `intel_device_ops_t` and in `devices/intel_*_impl.c` implementations.
- Generic code must call the HAL/device-ops interface rather than bypassing it with direct per-controller register access.
- If a required operation is missing, extend the shared device interface deliberately and implement the operation in the relevant device implementations; do not special-case one controller inside `src/**`.
- Register definitions must come from the repository's authoritative SSOT sources. Do not introduce unverified magic numbers.
- Preserve multi-device support, testability, and the existing architectural separation when changing generic code.

For device-specific implementation files, also apply `.github/instructions/hardware-abstraction-device-impl.instructions.md`.

Repository-wide rules, current accepted architecture/issues, and CI contracts override preserved historical examples if a real conflict is discovered. Report material conflicts instead of silently bypassing the HAL.
