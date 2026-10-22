---
name: intelavbfilter-testing
description: >-
  Required for IntelAvbFilter test work: creating or modifying tests, TDD,
  bugfix verification, HIL and CI testing, driver regression testing, test
  runners (Run-Tests-CI / Run-Tests-Elevated / Build-Tests), test failures and
  timeouts, hardware-state cleanup and restoration, adapter capabilities and
  multi-adapter execution, and interpreting test results or claiming RED/GREEN.
  Loads the canonical cross-agent testing contract.
user-invocable: true
disable-model-invocation: false
---

# IntelAvbFilter testing — Claude Code entry point

This entry point holds **no rules of its own**. The single normative testing contract is shared with GitHub Copilot and lives at:

`.github/skills/intelavbfilter-testing/SKILL.md`

Before any testing decision, command or result claim:

1. **Read that file in full now** (Read tool, repository-root path above) and apply it for the rest of the task.
2. Claude Code does not auto-discover skills under `.github/skills/`. When the contract routes to another skill (`run-tests`, `test-driven-development`, `verification-before-completion`, `lifecycle-traceability`, `ssot-and-magic-numbers`, `DbgView`), read `.github/skills/<name>/SKILL.md` directly.
3. For any runner command, read [`run-tests`](../../../.github/skills/run-tests/SKILL.md) and the script's real `param()` block first.
4. Treat `.github/skills/intelavbfilter-testing/reference.md` as historical only.
5. If you cannot read the canonical file, stop and tell Q — do not improvise testing rules from memory.

The root `claude.md` protocol (DOING/EXPECT/RESULT, stop on failure, ask before hardware-mutating or elevated actions) applies on top of the contract.
