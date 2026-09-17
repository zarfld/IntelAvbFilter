---
name: ci-triage
description: Use when CI fails across multiple jobs or repeated run URLs are being shared, especially when lint, traceability, and test quality failures appear in separate rounds and should be fixed in one local batch before the next push.
---

# CI Failure Triage

## Overview

Repeated "fix one CI failure category, push, wait, repeat" loops waste sessions and fragment context.

**Core principle:** Pull all failing job evidence first, classify by failure category, fix all categories in one batch, then verify locally before pushing.

## When to Use

Use this skill when:
- A CI run URL is provided and one or more jobs failed
- Failures are split across different jobs (for example code quality, traceability, tests)
- A previous push fixed one job but another job now fails
- You want one consolidated fix round instead of 3-5 CI cycles

Do not use this skill when:
- CI is fully green
- Failure is purely infrastructure/transient (runner outage, network interruption)
- The user asked for a single targeted fix only

## Required Inputs

- Run identifier: full URL or numeric run id
- Repository slug
- Local branch with write access

If only URL is available, extract run id from the URL path.

## One-Pass Workflow

### 1. Collect all failed jobs and logs first

```powershell
# Replace values with actual repo/run
$repo = "owner/repo"
$runId = "1234567890"

# Summary with job statuses
gh run view $runId --repo $repo

# JSON for machine-readable triage
gh run view $runId --repo $repo --json jobs,conclusion,headSha,headBranch,url
```

For each failed job, fetch logs before touching code:

```powershell
gh run view $runId --repo $repo --job <job-id> --log
```

If available, download artifacts for deeper diagnostics:

```powershell
gh run download $runId --repo $repo --dir artifacts/ci-$runId
```

### 2. Categorize failures by remediation domain

Group each failing signal into one category:

| Category | Typical Signals | Primary Fix Surface |
|---|---|---|
| Lint / Code Quality | formatting checks, static analysis, style violations | source files, lint config, generated headers as needed |
| Traceability Coverage | missing or malformed requirement/test links, traceability script failures | requirements/docs/issues metadata, traceability docs/scripts |
| Unit Test / Coverage | failing test cases, coverage thresholds not met | test logic, implementation defects, missing tests |

Rules:
- One failure can map to multiple categories if evidence supports it.
- Do not start fixing until all failed jobs are categorized.
- Keep a short triage note mapping: job -> category -> file targets.

### 3. Build a single fix batch

Apply fixes category-by-category locally in one working set:
- Resolve all lint/code-quality issues
- Resolve all traceability coverage issues
- Resolve all unit test/coverage issues

Use these companion skills during implementation:
- `run-tests` for correct local test execution and log interpretation
- `systematic-debugging` before proposing fixes for failing tests
- `verification-before-completion` before claiming completion

### 4. Verify locally before push

Run local equivalents for each category before any push:
- Lint/static checks used by CI
- Traceability validation script(s)
- Relevant unit test suite and coverage check(s)

Minimum gate:
- All locally reproducible CI checks for all failed categories are green
- No unresolved failures remain from the original run categories

### 5. Push once

Push only after all categories pass locally. The goal is one push per CI failure round, not one push per job.

## Triage Template

Use this structure in notes/comments while working:

```text
Run: <url or id>
Failed jobs:
- <job A> -> Lint / Code Quality
- <job B> -> Traceability Coverage
- <job C> -> Unit Test / Coverage

Planned local checks:
- <lint command>
- <traceability command>
- <tests/coverage command>

Status before push:
- Lint: PASS/FAIL
- Traceability: PASS/FAIL
- Tests/Coverage: PASS/FAIL

Decision:
- Push now / Continue fixing
```

## Common Mistakes

| Mistake | Why It Causes CI Loops | Correct Behavior |
|---|---|---|
| Fixing only the first failed job | Subsequent jobs fail in next run | Categorize all failed jobs first |
| Pushing before local verification | Uses CI as a debugger | Run local equivalents for all categories |
| Treating traceability as documentation-only | CI blocks on metadata correctness too | Validate traceability with same rigor as tests |
| Running tests incorrectly | False pass/fail conclusions | Use `run-tests` entry points and read logs |

## Completion Criteria

This skill is correctly applied when:
- All failed jobs from the referenced CI run were inspected
- Failures were categorized into lint, traceability, and test/coverage domains
- Fixes for all affected categories were implemented before pushing
- Local verification for all categories passed
- Exactly one consolidated push was needed for that CI round
