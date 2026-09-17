---
description: "Test-specific traceability and evidence rules for test source/specification files; routes to the preserved detailed testing instruction set on demand."
applyTo: "**/tests/**,**/*.test.js,**/*.test.ts,**/*.test.py,**/*.spec.js,**/*.spec.ts,**/*.spec.py",**/*test*.c,**/tests/**/*.c"
---

# Test-Specific Instructions

When creating, editing, reviewing, or interpreting tests matched by this instruction, **consult and apply the complete preserved guidance in** `.github/docs/TESTS-INSTRUCTIONS-REFERENCE.md` before making substantive traceability or lifecycle decisions.

Moving the detailed guidance out of this auto-applied file is a context-routing optimization only. Its testing and traceability rules remain intentional when relevant.

## Mandatory current contract

- GitHub Issues are the lifecycle SSOT for requirements and TEST artifacts.
- A TEST that demonstrates a requirement relationship uses the repository's current `Verifies: #N` contract; do not substitute `Traces to` for TEST → requirement verification.
- `Traces to` remains a parent/lineage relationship where defined by the current templates/validator.
- Test evidence must be reproducible and must not convert real failures or unmet prerequisites into false success.
- Keep test IDs, requirement links, implementation evidence, and current issue/template syntax consistent with CI validators.
- For hardware-dependent tests, apply the repository's multi-adapter and PASS/FAIL/SKIP semantics from the `intelavbfilter-testing` skill.

If preserved examples conflict with current `.github/ISSUE_TEMPLATE/07-test-case.yml`, current CI validators, or `lifecycle-traceability`, treat the current executable contract as authoritative and report the conflict.
