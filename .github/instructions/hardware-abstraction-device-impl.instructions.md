---
description: "Device-specific HAL implementation guidance: implement only evidence-supported controller capabilities and use authoritative register definitions."
applyTo: "devices/intel_*_impl.c"
---

# Device-Specific Hardware Abstraction Guidance

Files matched by this instruction are the device-specific side of the HAL boundary.

- Implement hardware behavior behind the shared `intel_device_ops_t` / device interface; do not leak controller-specific logic back into `src/**`.
- Use authoritative generated register definitions from `intel-ethernet-regs`; do not invent offsets, masks, bit semantics, timing limits, or capabilities.
- Base device-specific behavior on datasheets/specifications, accepted analysis results, and verified hardware evidence. Do not advertise a feature merely because a related controller supports it.
- Report unsupported operations explicitly through the established HAL/driver contract rather than simulating production hardware behavior.
- Preserve controller capability differences. TAS, Frame Preemption, PTM, PTP/PHC, CBS, and related TSN features must be enabled only where current evidence supports them.
- Keep register access, masks, capability checks, and device quirks localized to the relevant device implementation or authoritative generated register source.
- Apply `.github/skills/ssot-and-magic-numbers/SKILL.md` for register/IOCTL SSOT rules and `.github/instructions/SSOT.instructions.md` when IOCTL definitions are involved.

If evidence is missing or contradictory, investigate and document the uncertainty instead of guessing an implementation.
