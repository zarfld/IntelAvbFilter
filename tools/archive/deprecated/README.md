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
