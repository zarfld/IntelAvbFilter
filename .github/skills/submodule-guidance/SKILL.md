---
name: submodule-guidance
description: "Use when adding, removing, updating, troubleshooting, or reviewing Git submodules/external nested repositories, including .gitmodules, submodule commit pointers, external/*, and intel-ethernet-regs."
user-invocable: true
---

# Submodule Guidance

Use this skill whenever work concerns Git submodules or nested external repositories.

Before substantive changes, consult [`reference.md`](./reference.md). It preserves the complete pre-P0.2 `submodules.instructions.md` guidance byte-for-byte; moving it behind a skill is an activation/context-routing change, not removal of the methodology.

## Working contract

1. Identify whether the requested change belongs in the parent repository, the submodule repository, or both.
2. Inspect `.gitmodules`, the recorded gitlink commit, and relevant CI/build assumptions before changing topology or versions.
3. Make submodule-content changes in the owning repository; make the parent pointer update separately and deliberately.
4. Verify clean/reproducible checkout behavior, including initialization/update steps expected by CI.
5. Preserve external-source, generated-source, licensing, and SSOT boundaries.
6. Report dirty/unpublished submodule state instead of presenting it as a completed parent-repository change.

Current repository-wide instructions and CI contracts take precedence over historical examples if they conflict.