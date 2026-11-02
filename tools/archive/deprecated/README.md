# tools/archive/deprecated

Scripts that have been retired and replaced by the canonical tool set in `tools/`.

## Why These Are Kept

Retained for audit/history purposes. Do **not** use these scripts in any workflow.
All active tooling lives under:

- `tools/build/` — build scripts
- `tools/setup/` — install/uninstall scripts
- `tools/test/` — test runner scripts
- `tools/development/` — dev utilities

## Structure

| Sub-folder | Contents |
|---|---|
| `development/` | Retired development helper scripts |
| `setup/` | Retired install/uninstall scripts |
| `test/` | Retired test runner scripts |

Scripts at the root of this folder are one-off ad-hoc scripts that did not fit a category.

## Policy

New scripts must **never** be placed in this directory unless they are immediately
being retired. If a script belongs here, open a PR with the rationale.

## Retired 2026-10-29 (REQ-NF-SCRIPTS-001, TC-SCRIPTS-001: canonical budget < 31 kept, not raised)

| Script | Former location | Evidence for retirement |
|---|---|---|
| `Fix-SSOT-Violations.ps1` | `tools/build/` | One-off migration of 2026-03-22; no caller outside `docs/archive`; not runnable as written (`param()` follows the first statement) |
| `Import-VisualStudioVars.ps1` | `tools/build/` | No caller in workflows, scripts or current docs (references only in `docs/archive/**`) |
| `development/restart-service-elevated.ps1` | `tools/development/` | No caller anywhere; stale one-off ("Build 76 now active", interactive `Read-Host`) |

`tools/test/Lib-AvbLifecycle.ps1` was not retired: it is a dot-sourced library and moved to `tools/lib/`.
