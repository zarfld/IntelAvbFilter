---
description: "Scoped Git submodule/external-repository instructions; applies only to .gitmodules and the repository's actual submodule paths."
applyTo: ".gitmodules,external/**,intel-ethernet-regs/**"
---

# Submodules and External Repositories

For substantive submodule work, use the `submodule-guidance` skill and consult `.github/skills/submodule-guidance/reference.md`. The reference preserves the complete previous submodule guidance; this file narrows activation so it is no longer injected for unrelated repository files.

## Mandatory boundary rules

- Treat each submodule as an independently versioned repository; distinguish changes to submodule content from changes to the parent repository's recorded submodule commit.
- Preserve reproducible parent-repository state: intentional submodule pointer changes must be explicit and reviewable.
- Do not copy or fork external implementation code into the parent repository merely to avoid proper submodule/version handling.
- Respect the SSOT and ownership boundaries of external/generated sources, especially `intel-ethernet-regs`.
- Verify submodule initialization/update expectations and CI behavior when changing `.gitmodules`, submodule paths, URLs, or pinned commits.
- Do not claim a parent-repository fix is complete if required submodule changes exist only in an uncommitted local submodule worktree.

Current repository-wide rules and accepted architecture/CI contracts override historical examples if a real conflict exists.