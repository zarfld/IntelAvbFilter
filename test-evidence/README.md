# test-evidence/

This directory contains **committed hardware test results** in JUnit XML format.

Hardware tests require a machine with:
- A supported Intel NIC (I210, I219, I226) installed and accessible
- The `IntelAvbFilter` kernel driver built and installed
- Device node `\\.\IntelAvbFilter` accessible

These conditions **cannot be met on GitHub Actions CI runners**, so test results
are generated locally and committed here as evidence artifacts.

## Files

| File | Description |
|------|-------------|
| `hardware-results-latest.xml` | Most recent hardware test run (symlink copy) |
| `hardware-results-YYYYMMDD.xml` | Dated hardware test run results |

## Workflow

```powershell
# 1. On a machine with Intel NIC + driver installed:
.\tools\test\Run-Tests-Elevated.ps1 -Configuration Debug -Full

# 2. Convert log files to JUnit XML:
.\tools\test\Convert-Logs-To-JUnit.ps1

# 3. Commit the evidence:
git add test-evidence/
git commit -m "chore(evidence): update hardware test results (YYYYMMDD)"
git push
```

CI will then surface these results via `dorny/test-reporter` in the
**Hardware Test Evidence** job in the Actions workflow.

## Traceability

- Implements: #265 (TEST-COVERAGE-001)
- Implements: #244 (TEST-BUILD-CI-001)
- Traces to:  #98 (REQ-NF-BUILD-CI-001)
