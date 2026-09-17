---
name: ci-triage
description: Use when CI failures arrive as repeated run URLs, when multiple jobs fail across successive runs, or when uncertain whether lint, traceability, and test/coverage categories are all fixed before pushing.
---

# CI Failure Triage for One-Push Recovery

## Overview

Repeated loop pattern:

1. Open one CI run
2. Fix one failing job
3. Push
4. New run fails in a different job

This skill replaces that loop with one pass: collect all failure evidence, group by category, fix all categories in one batch, then verify locally before push.

## When to Use

- The human shares one or more GitHub Actions run URLs and asks for fixes
- Failures rotate between Code Quality, Traceability Coverage, and Unit Test/Coverage jobs
- A prior fix passed one job but the next run failed a different category
- You are unsure whether all CI failure categories are covered locally

**Not for:** deep single-bug root-cause work without CI evidence. Use systematic-debugging for that.

## The Iron Law

```
DO NOT PUSH AFTER FIXING ONLY ONE JOB CATEGORY.

Collect all failed jobs for the target run(s), categorize them,
and clear every failing category in one local batch before push.
```

## One-Pass Workflow

### 1. Collect complete CI evidence first

**Preferred — GitHub MCP (if available with actions support):**

The GitHub MCP tools in this repo's VS Code environment expose `pull_request_read → get_check_runs`, which returns check run name, status, and conclusion for the **current head commit** of a given PR. Use this when you have a PR number.

```
mcp: pull_request_read(method=get_check_runs, pullNumber=NNN)
→ returns: [{name, status, conclusion, html_url, started_at, completed_at}]
```

This covers the category-detection step for lint/traceability/test jobs visible as check runs.

**Hard Limitation — Stop here if blocked:** `get_check_runs` only covers the PR's **current HEAD commit**. If you have a run URL (e.g. `/actions/runs/35227464543`) that is NOT the HEAD of an open PR, the GitHub MCP tools in this environment cannot retrieve its jobs or logs — `GET /actions/runs/{id}/jobs` is not exposed. Options: (a) get the PR number whose HEAD failed and use `get_check_runs`; (b) use gh CLI fallback below; (c) install the full `github/github-mcp-server` which exposes `list_workflow_run_jobs`. For those, fall back to gh CLI:

```powershell
# Summary with per-job outcomes (gh CLI fallback)
gh run view <run-id-or-url> --log-failed

# Machine-readable job list for categorization
gh run view <run-id-or-url> --json jobs,name,headBranch,headSha,workflowName

# Optional: download artifacts for coverage or reports
gh run download <run-id-or-url> -D .\artifacts\ci-run-<id>
```

If the thread includes multiple recent runs, collect each run before planning fixes. Do not assume the latest run contains all failure categories seen in the loop.

### 2. Categorize failures by job family

Group failing jobs into these categories:

- Lint/Code Quality
  - Static checks, formatting, style, compile-time quality gates
- Traceability Coverage
  - Requirement/issue-link validation, traceability matrix checks, SSOT policy checks
- Unit Test/Coverage
  - Test failures, failing assertions, coverage threshold failures

Build one categorized checklist before editing code.

### 3. Build one combined fix plan

For each failing category, define concrete file edits and local verification commands.

Rules:

- No single-category push
- No "fix-now, triage-later"
- If the same category failed in prior run(s), include those signatures in the same batch

### 4. Implement all category fixes in one branch state

Apply all needed changes before running final verification. Keep changes small and traceable, but do not split into separate push attempts per category.

### 5. Verify locally across all affected categories

Run local checks that map to each failed category:

- Lint/Code Quality: equivalent local lint/static/build command(s)
- Traceability Coverage: repository traceability/standards validation command(s)
- Unit Test/Coverage: repository test command(s) for affected suite(s)

Use repository-approved runners and scripts. For test execution details, follow run-tests.

### 6. Push only after all affected categories are green

Report evidence by category (command + result) before claiming CI-ready status.

## IntelAvbFilter-Specific Command Patterns

**MCP — check run status for a PR (preferred when PR number is known):**

```
pull_request_read(method=get_check_runs, owner=zarfld, repo=IntelAvbFilter, pullNumber=NNN)
```

Returns: `name`, `status`, `conclusion`, `html_url` for each check — sufficient to categorize jobs without gh CLI.

**MCP — commit-level combined status:**

```
pull_request_read(method=get_status, owner=zarfld, repo=IntelAvbFilter, pullNumber=NNN)
```

**gh CLI — when run ID is known and MCP log access is unavailable:**

```powershell
gh run view <run-id-or-url> --log-failed
gh run view <run-id-or-url> --json jobs
```

**Local test runners:**

```powershell
# CI/unit-style (no hardware required)
.\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite Unit

# Hardware/interactive
.\tools\test\Run-Tests-Elevated.ps1 -TestName <test>.exe
```

For traceability and SSOT checks, run the same scripts invoked by the failing CI job definitions instead of guessing substitutes.

## Anti-Loop Safeguards

Stop and re-triage if any of these appear:

- "Let us fix this one job first and push"
- "Lint passed locally, so CI should be fine"
- "We can check traceability after the next run"
- "Tests probably pass because build passed"

Any of the above means you are re-entering the multi-round CI loop.

## Pre-Push Gate

Before pushing, confirm all items:

- [ ] Failing jobs collected from the target run(s)
- [ ] Every failure mapped to one of: lint, traceability, test/coverage
- [ ] Fixes implemented for every failing category
- [ ] Local verification run for every failing category
- [ ] Evidence recorded per category (command + pass/fail)

If one category is not locally reproducible, document why and what evidence supports proceeding.

## Use With

- run-tests: correct test entry points and log interpretation
- systematic-debugging: root-cause-first investigation for non-obvious failures
- verification-before-completion: evidence before success claims

## Common Mistakes

| Mistake | Consequence | Correct behavior |
|---|---|---|
| Fix only the first failing job in a run | Next CI run fails in another category | Collect all failing jobs first, then batch-fix |
| Treat "latest run" as full history | Miss recurring failures from prior runs | Include current + relevant previous runs in triage |
| Verify only tests or only lint | False CI readiness claims | Verify every affected category |
| Push without category evidence | Rework and trust loss | Report per-category command evidence before push |

## Quick Triage Skeleton

```text
1) Collect run evidence
   - gh run view ... --log-failed
   - gh run view ... --json jobs
   - gh run download ... (if artifacts are needed)

2) Build category map
   - Lint:
   - Traceability:
   - Test/Coverage:

3) Implement all fixes in one batch

4) Verify by category locally

5) Push once
```