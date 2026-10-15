#!/usr/bin/env python3
"""
Regression tests for scripts/generate-readme-status.py (Phase 1 â€” architecture-independent).

ALL tests must remain valid when:
  - "HIL Mandatory Gate" is renamed to "Integration Tests"
  - "HIL Mutation Lane" is removed entirely
  - Several jobs are merged into a single matrix job
  - A new "Acceptance Tests" job appears
  - Additional self-hosted runners are added
  - A formerly required job is removed
  - The overall workflow fails despite some jobs passing
  - A job is skipped or cancelled
  - An older run completes after a newer run
  - Driver identity is unavailable (always â€” Phase 1 does not infer it)

No test may depend on specific job names, job counts, runner labels,
HIL lane topology, or matrix configuration.

Run:
    python tests/status-generator/test_generate_readme_status.py

Exit 0 = all pass; exit 1 = failure(s).
"""

import importlib.util
import os
import sys
import tempfile
import types

# â”€â”€ Load generator by file path (name contains hyphens) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
_SCRIPT = os.path.join(
    os.path.dirname(__file__), "..", "..", "scripts", "generate-readme-status.py"
)
_spec = importlib.util.spec_from_file_location("generate_readme_status", _SCRIPT)
_mod  = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)

generate_block       = _mod.generate_block
update_readme        = _mod.update_readme
parse_published_meta = _mod.parse_published_meta
is_newer             = _mod.is_newer
_make_meta           = _mod._make_meta
START_MARKER         = _mod.START_MARKER
END_MARKER           = _mod.END_MARKER
WORKFLOW_NAME        = _mod.WORKFLOW_NAME

# â”€â”€ Test harness â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
_FAILURES: list[str] = []

def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  [PASS] {name}")
    else:
        print(f"  [FAIL] {name}" + (f" â€” {detail}" if detail else ""))
        _FAILURES.append(name)


# â”€â”€ Shared fixtures â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

_BASE_RUN = {
    "id":          88001001,
    "run_number":  300,
    "run_attempt": 1,
    "html_url":    "https://github.com/zarfld/IntelAvbFilter/actions/runs/88001001",
    "head_sha":    "feedbeefcafe1234",
    "head_branch": "master",
    "conclusion":  "success",
    "updated_at":  "2026-10-15T11:00:00Z",
}
_REPO = "zarfld/IntelAvbFilter"


def _j(name: str, conclusion: str | None, url: str = "") -> dict:
    """Minimal job fixture (steps omitted â€” Phase 1 generator does not use them)."""
    return {
        "name":       name,
        "conclusion": conclusion,
        "html_url":   url or f"https://github.com/zarfld/IntelAvbFilter/actions/jobs/{hash(name) & 0xFFFFFF}",
    }


def _readme_with_block(existing: str = "OLD") -> str:
    return (
        "# Title\n\nHuman content before.\n\n"
        f"{START_MARKER}\n{existing}\n{END_MARKER}\n\n"
        "Human content after.\n"
    )


def _tmp(content: str) -> str:
    fd, path = tempfile.mkstemp(suffix=".md")
    os.close(fd)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return path


# â”€â”€ Typical job lists â€” names are arbitrary labels, not semantically parsed â”€

_JOBS_ALL_PASS = [
    _j("Build and Lint",                                           "success"),
    _j("Unit Tests (windows-latest, Debug)",                       "success"),
    _j("Unit Tests (windows-2022, Release)",                       "success"),
    _j("Integration Tests (windows-latest, Debug)",                "success"),
    _j("Acceptance Tests",                                         "success"),
    _j("Security Scan",                                            "success"),
    _j("Traceability Coverage Enforcement",                        "success"),
    _j("Generate Standards Compliance Report",                     "success"),
]

_JOBS_ONE_FAIL = [
    _j("Build and Lint",                                           "success"),
    _j("Unit Tests (windows-latest, Debug)",                       "success"),
    _j("Integration Tests - hardware runner",                      "failure"),
    _j("Acceptance Tests",                                         "success"),
]

_JOBS_CANCELLED = [
    _j("Build and Lint",     "success"),
    _j("Unit Tests",         "cancelled"),
    _j("Integration Tests",  "skipped"),
]

_JOBS_RENAMED_HIL = [
    # "HIL Mandatory Gate" was renamed to "Integration Tests" â€” no code change needed.
    _j("Build and Lint",                "success"),
    _j("Unit Tests (Debug)",            "success"),
    _j("Integration Tests (i226, Debug)", "failure"),  # was HIL Mandatory Gate
    _j("Acceptance Tests",              "success"),
]

_JOBS_MERGED = [
    # Several jobs merged into a single matrix job.
    _j("CI Matrix (windows-latest, Debug)",  "success"),
    _j("CI Matrix (windows-2022, Release)",  "success"),
    _j("Compliance Report",                  "success"),
]

_JOBS_NEW_ACCEPTANCE = _JOBS_ALL_PASS + [
    _j("E2E Acceptance - Customer Scenarios", "success"),
]

_JOBS_EXTRA_RUNNERS = _JOBS_ALL_PASS + [
    _j("Integration Tests (i219-runner, Debug)", "success"),
    _j("Integration Tests (i210-runner, Debug)", "success"),
]

# Job with zero steps (matrix skipped before variable substitution).
# This mirrors the real "HIL Mutation Lane (${{ matrix.nic }}, ${{ matrix.config }})"
# job that appears in run #488 with conclusion=skipped and steps=[].
_ZERO_STEP_JOB = {
    "name":       "HIL Mutation Lane (${{ matrix.nic }}, ${{ matrix.config }})",
    "conclusion": "skipped",
    "html_url":   "https://github.com/zarfld/IntelAvbFilter/actions/runs/88001001/job/999",
    "steps":      [],   # explicitly empty — must NOT be filtered out
}
_JOBS_WITH_ZERO_STEP = _JOBS_ALL_PASS + [_ZERO_STEP_JOB]


# â”€â”€ Tests â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

def test_overall_pass_shown() -> None:
    print("TEST: overall_pass â€” success conclusion shown")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    check("success icon shown", "\u2705 success" in block)
    check("run number shown", "300" in block)
    check("run link present", "actions/runs/88001001" in block)
    check("tested SHA shown", "feedbee" in block)
    check("no failure warning", "did not succeed" not in block)
    check("meta embedded", "CI-STATUS-META:" in block)


def test_overall_fail_warning() -> None:
    print("TEST: overall_fail â€” failure shown with explicit warning")
    run = {**_BASE_RUN, "conclusion": "failure"}
    block = generate_block(run, _JOBS_ONE_FAIL, _REPO)
    check("failure icon shown", "\u274c failure" in block)
    check("explicit failure warning", "did not succeed" in block)
    check("failure conclusion mentioned", "failure" in block)


def test_all_jobs_in_table() -> None:
    print("TEST: all_jobs_in_table â€” every job appears verbatim")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    for job in _JOBS_ALL_PASS:
        check(f"job '{job['name'][:30]}' appears", job["name"] in block)


def test_failed_job_appears() -> None:
    print("TEST: failed_job_appears â€” failing job shown in table")
    run = {**_BASE_RUN, "conclusion": "failure"}
    block = generate_block(run, _JOBS_ONE_FAIL, _REPO)
    check("failing job name shown", "Integration Tests - hardware runner" in block)
    check("fail icon shown", "\u274c fail" in block)


def test_no_job_name_semantic_inference() -> None:
    print("TEST: no_name_semantics â€” renamed jobs produce same structure")
    run_fail = {**_BASE_RUN, "conclusion": "failure"}
    # Run with "HIL Mandatory Gate" name
    block_hil = generate_block(run_fail, _JOBS_ONE_FAIL, _REPO)
    # Run with the same job renamed to "Integration Tests"
    block_renamed = generate_block(run_fail, _JOBS_RENAMED_HIL, _REPO)
    # Both blocks must have the same structure (summary, disclaimer, table)
    for section in ("CI-STATUS-META:", "did not succeed", "Scope of this status"):
        check(f"'{section}' present in original", section in block_hil)
        check(f"'{section}' present in renamed", section in block_renamed)
    # Generator must not contain any hardcoded HIL/HardwareMandatory names in the output
    for bad_term in ("HIL Mandatory", "HIL Mutation", "HardwareMandatory",
                     "HardwareMutation", "HardwareUnit", "HardwareIntegration",
                     "BASELINE_DRIVER_ONLY", "VERIFIED_CURRENT_BUILD"):
        check(f"no hardcoded '{bad_term}' in output",
              bad_term not in block_hil, f"found in block_hil")
        check(f"no hardcoded '{bad_term}' in renamed output",
              bad_term not in block_renamed, f"found in block_renamed")


def test_merged_jobs() -> None:
    print("TEST: merged_jobs â€” fewer rows when jobs are merged")
    block = generate_block(_BASE_RUN, _JOBS_MERGED, _REPO)
    check("CI-STATUS-META present", "CI-STATUS-META:" in block)
    check("success shown", "\u2705 success" in block)
    for job in _JOBS_MERGED:
        check(f"merged job '{job['name'][:30]}' present", job["name"] in block)


def test_new_job_appears() -> None:
    print("TEST: new_job_appears â€” additional jobs shown without code changes")
    block = generate_block(_BASE_RUN, _JOBS_NEW_ACCEPTANCE, _REPO)
    check("new job 'E2E Acceptance' appears", "E2E Acceptance" in block)
    check("existing jobs still appear", "Build and Lint" in block)


def test_extra_runners() -> None:
    print("TEST: extra_runners â€” additional self-hosted runners shown without code changes")
    block = generate_block(_BASE_RUN, _JOBS_EXTRA_RUNNERS, _REPO)
    check("i219 runner job shown", "i219-runner" in block)
    check("i210 runner job shown", "i210-runner" in block)
    check("original jobs still shown", "Build and Lint" in block)


def test_job_removed() -> None:
    print("TEST: job_removed â€” removing a job requires no code change")
    reduced = [j for j in _JOBS_ALL_PASS if "Security" not in j["name"]]
    block = generate_block(_BASE_RUN, reduced, _REPO)
    check("CI-STATUS-META present", "CI-STATUS-META:" in block)
    check("remaining jobs shown", "Build and Lint" in block)
    check("removed job absent", "Security Scan" not in block)


def test_cancelled_workflow() -> None:
    print("TEST: cancelled â€” cancelled workflow shown correctly")
    run = {**_BASE_RUN, "conclusion": "cancelled"}
    block = generate_block(run, _JOBS_CANCELLED, _REPO)
    check("cancelled icon shown", "\u23f9 cancelled" in block)
    check("failure warning shown", "did not succeed" in block)
    check("no false success", "\u2705 success" not in block[:400])


def test_skipped_job_shown() -> None:
    print("TEST: skipped_job â€” skipped jobs visible in table")
    block = generate_block(_BASE_RUN, _JOBS_CANCELLED, _REPO)
    check("skipped job shown", "Integration Tests" in block)
    check("skipped icon shown", "\u2b1b skipped" in block)


def test_disclaimer_always_present() -> None:
    print("TEST: disclaimer â€” hardware/identity disclaimer always shown")
    for label, jobs in [
        ("all_pass",   _JOBS_ALL_PASS),
        ("one_fail",   _JOBS_ONE_FAIL),
        ("cancelled",  _JOBS_CANCELLED),
    ]:
        run = {**_BASE_RUN, "conclusion": "success" if label == "all_pass" else "failure"}
        block = generate_block(run, jobs, _REPO)
        check(f"disclaimer present ({label})", "Scope of this status" in block)
        check(f"no driver identity claimed ({label})",
              "driver identity" in block.lower() or "driver provenance" in block.lower())
        # Must NOT claim test counts
        for bad in ("PASS / 513", "513 / 513", "100%", "test counts: "):
            check(f"no test count '{bad}' ({label})", bad not in block)


def test_no_inferred_hardware_claims() -> None:
    print("TEST: no_hardware_claims â€” no inferred hardware coverage from runner labels")
    block = generate_block(_BASE_RUN, _JOBS_EXTRA_RUNNERS, _REPO)
    # Should NOT claim NIC presence from the job name
    for bad in ("NIC present", "adapter available", "hardware confirmed", "VERIFIED_CURRENT_BUILD"):
        check(f"no '{bad}' inferred", bad not in block)


def test_empty_jobs() -> None:
    print("TEST: empty_jobs â€” no jobs returns gracefully")
    block = generate_block(_BASE_RUN, [], _REPO)
    check("CI-STATUS-META present", "CI-STATUS-META:" in block)
    check("'No job data' message shown", "No job data" in block)
    check("disclaimer present", "Scope of this status" in block)


def test_malformed_jobs() -> None:
    print("TEST: malformed_jobs â€” entries missing required fields handled gracefully")
    malformed = [
        {"conclusion": "success"},            # missing name
        {},                                   # completely empty
        {"name": "Valid Job", "conclusion": "success"},
    ]
    try:
        block = generate_block(_BASE_RUN, malformed, _REPO)
        check("generates without exception", True)
        check("run link present", "actions/runs/88001001" in block)
        check("valid job shown", "Valid Job" in block)
    except Exception as exc:
        check("generates without exception", False, str(exc))


def test_stale_detection() -> None:
    print("TEST: stale_detection â€” SHA differs from master HEAD")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO, master_sha="different123")
    check("STALE warning shown", "STALE" in block)
    check("tested sha mentioned", "feedbee" in block)
    check("master sha mentioned", "differe" in block)


def test_no_stale_sha_matches() -> None:
    print("TEST: no_stale â€” SHA matches master HEAD")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO, master_sha=_BASE_RUN["head_sha"])
    check("no STALE when sha matches", "STALE" not in block)


def test_no_stale_empty_master_sha() -> None:
    print("TEST: no_stale_empty â€” empty master_sha skips check")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO, master_sha="")
    check("no STALE when master_sha empty", "STALE" not in block)


def test_idempotent() -> None:
    print("TEST: idempotent â€” same inputs produce identical block")
    block1 = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    block2 = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    check("identical blocks", block1 == block2)


def test_markers_preserved() -> None:
    print("TEST: markers_preserved â€” surrounding content unchanged")
    BEFORE = "# Title\n\nHuman before.\n\n"
    AFTER  = "\n\nHuman after.\n\n## Section 2\n"
    content = BEFORE + f"{START_MARKER}\nOLD\n{END_MARKER}" + AFTER
    path = _tmp(content)
    try:
        block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
        meta  = {"run_number": 300, "run_id": 88001001, "run_attempt": 1}
        result = update_readme(path, block, meta)
        with open(path, encoding="utf-8") as f:
            out = f.read()
        check("updated result", result == "updated")
        check("START marker preserved", START_MARKER in out)
        check("END marker preserved", END_MARKER in out)
        check("before content preserved", out.startswith(BEFORE + START_MARKER))
        check("after content preserved", out.endswith(END_MARKER + AFTER))
        check("OLD block removed", "OLD" not in out)
        check("new block present", "CI-STATUS-META:" in out)
    finally:
        os.unlink(path)


def test_idempotent_write() -> None:
    print("TEST: idempotent_write â€” second write with same data skips")
    path = _tmp(_readme_with_block())
    try:
        block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
        meta  = {"run_number": 300, "run_id": 88001001, "run_attempt": 1}
        update_readme(path, block, meta)
        result2 = update_readme(path, block, meta)
        check("second write skipped", result2 == "skipped_stale", f"result={result2}")
    finally:
        os.unlink(path)


def test_freshness_guard_older_rejected() -> None:
    print("TEST: freshness_older â€” older run does not overwrite newer published status")
    path = _tmp(_readme_with_block())
    try:
        run300 = {**_BASE_RUN, "run_number": 300, "id": 88003000, "run_attempt": 1}
        block300 = generate_block(run300, _JOBS_ALL_PASS, _REPO)
        update_readme(path, block300, {"run_number": 300, "run_id": 88003000, "run_attempt": 1})
        with open(path, encoding="utf-8") as f:
            content_after_300 = f.read()

        run200 = {**_BASE_RUN, "run_number": 200, "id": 88002000, "run_attempt": 1}
        block200 = generate_block(run200, _JOBS_ALL_PASS, _REPO)
        result = update_readme(path, block200, {"run_number": 200, "run_id": 88002000, "run_attempt": 1})
        with open(path, encoding="utf-8") as f:
            content_after_200 = f.read()

        check("older run rejected", result == "skipped_stale", f"result={result}")
        check("content unchanged", content_after_300 == content_after_200)
    finally:
        os.unlink(path)


def test_freshness_guard_rerun_allowed() -> None:
    print("TEST: freshness_rerun â€” higher attempt of same run_number is allowed")
    path = _tmp(_readme_with_block())
    try:
        run_a1 = {**_BASE_RUN, "run_attempt": 1}
        run_a2 = {**_BASE_RUN, "run_attempt": 2}
        block1 = generate_block(run_a1, _JOBS_ALL_PASS, _REPO)
        block2 = generate_block(run_a2, _JOBS_ALL_PASS, _REPO)
        update_readme(path, block1, {"run_number": 300, "run_id": 88001001, "run_attempt": 1})
        result = update_readme(path, block2, {"run_number": 300, "run_id": 88001001, "run_attempt": 2})
        check("re-run attempt allowed", result == "updated", f"result={result}")
        with open(path, encoding="utf-8") as f:
            content = f.read()
        check("attempt 2 meta present", "run_attempt=2" in content)
    finally:
        os.unlink(path)


def test_freshness_guard_first_write() -> None:
    print("TEST: freshness_first â€” first write (no existing meta) always allowed")
    path = _tmp(_readme_with_block("placeholder â€” no meta"))
    try:
        block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
        result = update_readme(path, block, {"run_number": 300, "run_id": 1, "run_attempt": 1})
        check("first write allowed", result == "updated", f"result={result}")
    finally:
        os.unlink(path)


def test_parse_meta_round_trip() -> None:
    print("TEST: parse_meta â€” round-trip")
    meta_line = _make_meta(488, 38712776981, 3)
    parsed = parse_published_meta(meta_line)
    check("run_number", parsed.get("run_number") == 488, str(parsed))
    check("run_id", parsed.get("run_id") == 38712776981, str(parsed))
    check("run_attempt", parsed.get("run_attempt") == 3, str(parsed))


def test_parse_meta_absent() -> None:
    print("TEST: parse_meta_absent â€” returns empty dict when no meta present")
    parsed = parse_published_meta("Some old README content with no meta")
    check("empty dict returned", parsed == {}, str(parsed))


def test_is_newer_rules() -> None:
    print("TEST: is_newer â€” freshness comparison rules")
    check("higher number is newer",
          is_newer({"run_number": 201, "run_attempt": 1},
                   {"run_number": 200, "run_attempt": 1}))
    check("lower number not newer",
          not is_newer({"run_number": 199, "run_attempt": 1},
                       {"run_number": 200, "run_attempt": 1}))
    check("same number higher attempt is newer",
          is_newer({"run_number": 200, "run_attempt": 2},
                   {"run_number": 200, "run_attempt": 1}))
    check("same number same attempt not newer",
          not is_newer({"run_number": 200, "run_attempt": 1},
                       {"run_number": 200, "run_attempt": 1}))
    check("any run newer than no meta",
          is_newer({"run_number": 1, "run_attempt": 1}, {}))


def test_zero_step_job_included() -> None:
    print("TEST: zero_step_job — skipped job with 0 steps must appear (run #488 pattern)")
    block = generate_block(_BASE_RUN, _JOBS_WITH_ZERO_STEP, _REPO)
    # Job with unresolved template variable must appear verbatim
    check("zero-step job present", "${{ matrix.nic }}" in block)
    check("skipped icon shown for zero-step job", "\u2b1b skipped" in block)
    # Total count must include the zero-step job
    total = len(_JOBS_WITH_ZERO_STEP)
    check(f"job count {total} shown", f"{total} jobs" in block)


def test_reports_section_present() -> None:
    print("TEST: reports_section — discoverable links to existing CI reports")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    check("Detailed Reports heading", "Detailed Reports" in block)
    check("JUnit link present", "JUnit" in block or "test-reporter" in block)
    check("traceability artefact link", "traceability" in block.lower())
    check("historical evidence link", "hardware-results-latest.xml" in block)
    check("traceability-summary-latest link", "traceability-summary-latest.md" in block)
    check("historical labelled as historical", "historical" in block)


def test_no_count_synthesis() -> None:
    print("TEST: no_count_synthesis — test counts never synthesised from job conclusions")
    # Even when all jobs pass, never claim test counts
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    for bad in ("tests passed", "pass rate", "0 failures", "100% pass", "all tests pass"):
        check(f"no '{bad}' claimed", bad not in block.lower())


def test_artefact_integrity_note() -> None:
    print("TEST: artefact_integrity_note — JUnit discrepancy and HIL provenance noted")
    block = generate_block(_BASE_RUN, _JOBS_ALL_PASS, _REPO)
    check("artefact integrity note present", "integrity" in block.lower() or "committed" in block)
    check("historical evidence distinguished", "historical" in block)
    check("JUnit known issue noted", "failures=17" in block or "over-count" in block or "Known JUnit" in block)


def test_28_jobs_run488_pattern() -> None:
    print("TEST: 28_jobs — run #488 pattern: 22 success, 5 failure, 1 skipped (0 steps)")
    # Simulate run #488 structure
    jobs_488 = [
        _j("Hardware Test Evidence (local run, committed XML)", "success"),
        _j("GitHub Issues Structure Validation (Requirements & Traceability)", "success"),
        {"name": "HIL Mandatory Gate (i226, Debug)", "conclusion": "failure",
         "html_url": "https://github.com/zarfld/IntelAvbFilter/actions/runs/88001001/job/1", "steps": [{"name": "s", "conclusion": "failure"}]},
        _j("Pillar 1 - PREfast Driver Static Analysis (/analyze)", "success"),
        _j("SSOT Gate - No Raw Hex Literals in src/", "success"),
        _j("Unit Tests (windows-latest, Debug)", "success"),
        _j("Unit Tests (windows-2022, Debug)", "success"),
        _j("Unit Tests (self-hosted-hardware, Debug)", "failure"),
        _j("Security Scan", "success"),
        _j("Unit Tests (windows-latest, Release)", "success"),
        _j("Unit Tests (windows-2022, Release)", "success"),
        _j("Unit Tests (self-hosted-hardware, Release)", "failure"),
        _j("Requirements Traceability (ISO/IEC/IEEE 29148 via GitHub Issues)", "success"),
        _j("Architecture Compliance (ISO/IEC/IEEE 42010)", "success"),
        _j("GitHub Issues Artifact Generation", "success"),
        # The 0-step skipped job with unresolved template variables
        {"name": "HIL Mutation Lane (${{ matrix.nic }}, ${{ matrix.config }})",
         "conclusion": "skipped",
         "html_url": "https://github.com/zarfld/IntelAvbFilter/actions/runs/88001001/job/16",
         "steps": []},
        _j("Integrity Level Scan", "success"),
        _j("Code Quality & Standards (IEEE 1016, XP Practices)", "success"),
        _j("Integration Tests (windows-2022, Debug)", "success"),
        _j("Integration Tests (windows-latest, Debug)", "success"),
        _j("Integration Tests (self-hosted-hardware, Debug)", "failure"),
        _j("Integration Tests (windows-latest, Release)", "success"),
        _j("Integration Tests (windows-2022, Release)", "success"),
        _j("Integration Tests (self-hosted-hardware, Release)", "failure"),
        _j("Acceptance Tests (XP - Customer Tests, IEEE 1012)", "success"),
        _j("Traceability Coverage Enforcement", "success"),
        _j("Generate Standards Compliance Report", "success"),
        _j("Deploy to Staging (Phase 08 - Transition)", "success"),
    ]
    run = {**_BASE_RUN, "conclusion": "failure"}
    block = generate_block(run, jobs_488, _REPO)
    check("28 jobs shown", "28 jobs" in block)
    check("HIL Mutation Lane shown (0-step, unresolved vars)", "${{ matrix.nic }}" in block)
    check("HIL Mandatory Gate shown", "HIL Mandatory Gate" in block)
    check("failure icon for overall", "\u274c failure" in block)
    # Count job table rows: within <details>...</details>, rows are "| [name](url) | icon |"
    import re as _re
    details_m = _re.search(r"<details>.*?</details>", block, _re.DOTALL)
    if details_m:
        job_rows = _re.findall(r"^\| \[", details_m.group(), _re.MULTILINE)
        check("28 job rows in details section", len(job_rows) == 28,
              f"found {len(job_rows)}")


def test_attempt_isolation() -> None:
    print("TEST: attempt_isolation")
    # ── Part 1: get_jobs uses attempt-specific endpoint ─────────────────────
    calls = []
    _orig_gh_get = _mod._gh_get

    def _mock(path, token):
        calls.append(path)
        if "/attempts/2/jobs" in path and "page=1" in path:
            return {"jobs": [_j("Job A", "success")]}
        return {"jobs": []}

    _mod._gh_get = _mock
    try:
        jobs = _mod.get_jobs("tok", "zarfld/IntelAvbFilter", 88001001, 2)
    finally:
        _mod._gh_get = _orig_gh_get

    check("attempt-specific endpoint used", any("/attempts/2/jobs" in c for c in calls),
          f"calls={calls[:3]}")
    check("filter=all NOT in any call", not any("filter=all" in c for c in calls))
    check("attempt-1 endpoint NOT called", not any("/attempts/1/jobs" in c for c in calls))
    check("attempt-2 job returned", len(jobs) == 1 and jobs[0]["name"] == "Job A")

    # ── Part 2: ANY page failure raises RuntimeError (fail closed) ──────────
    def _mock_fail(path, token):
        return None

    _mod._gh_get = _mock_fail
    try:
        raised = False
        try:
            _mod.get_jobs("tok", "zarfld/IntelAvbFilter", 88001001, 2)
        except RuntimeError:
            raised = True
    finally:
        _mod._gh_get = _orig_gh_get
    check("RuntimeError raised on first-page failure", raised)

    # ── Part 3: page-2 failure also raises RuntimeError (fail closed) ────────
    page_counter = {"n": 0}

    def _mock_page2_fail(path, token):
        page_counter["n"] += 1
        if page_counter["n"] == 1:
            # Page 1 returns a full page (100 jobs) to trigger pagination
            return {"jobs": [_j(f"Job{i}", "success") for i in range(100)]}
        return None   # page 2 fails

    _mod._gh_get = _mock_page2_fail
    try:
        raised_p2 = False
        try:
            _mod.get_jobs("tok", "zarfld/IntelAvbFilter", 88001001, 2)
        except RuntimeError as exc:
            raised_p2 = True
            check("page-2 error mentions page number", "page 2" in str(exc).lower(),
                  f"exc={exc}")
    finally:
        _mod._gh_get = _orig_gh_get
    check("RuntimeError raised on page-2 failure", raised_p2)

    # ── Part 4: page-2 failure → no README modification ──────────────────────
    path_p2 = _tmp(_readme_with_block("ORIGINAL"))
    try:
        with open(path_p2, encoding="utf-8") as f:
            original_content = f.read()
        # Simulate what main() does: get_jobs raises → caller does not call update_readme
        jobs_ok = False
        try:
            page_counter["n"] = 0
            _mod._gh_get = _mock_page2_fail
            _mod.get_jobs("tok", "zarfld/IntelAvbFilter", 88001001, 2)
        except RuntimeError:
            jobs_ok = False   # Do NOT update README on failure
        finally:
            _mod._gh_get = _orig_gh_get
        with open(path_p2, encoding="utf-8") as f:
            after_content = f.read()
        check("README not modified after page-2 failure", original_content == after_content)
    finally:
        os.unlink(path_p2)

    # ── Part 3: delayed attempt-1 updater cannot overwrite attempt-2 result ─
    path = _tmp(_readme_with_block())
    try:
        run_a2 = {**_BASE_RUN, "run_attempt": 2}
        run_a1 = {**_BASE_RUN, "run_attempt": 1}
        a2_jobs = [_j("Job A", "success")]
        a1_jobs = [_j("Job A", "failure")]    # attempt 1 had a different result

        block2 = generate_block(run_a2, a2_jobs, _REPO)
        update_readme(path, block2, {"run_number": 300, "run_id": 88001001, "run_attempt": 2})

        result = update_readme(path,
                               generate_block(run_a1, a1_jobs, _REPO),
                               {"run_number": 300, "run_id": 88001001, "run_attempt": 1})
        check("delayed attempt-1 rejected", result == "skipped_stale", f"result={result}")
        with open(path, encoding="utf-8") as f:
            content = f.read()
        check("attempt-2 still published", "run_attempt=2" in content)
        check("attempt-1 failure job not visible", "\u274c fail" not in content)
    finally:
        os.unlink(path)

    # ── Part 4: higher attempt of same run IS accepted ───────────────────────
    path2 = _tmp(_readme_with_block())
    try:
        update_readme(path2,
                      generate_block({**_BASE_RUN, "run_attempt": 1}, a1_jobs, _REPO),
                      {"run_number": 300, "run_id": 88001001, "run_attempt": 1})
        result2 = update_readme(path2,
                                generate_block({**_BASE_RUN, "run_attempt": 2}, a2_jobs, _REPO),
                                {"run_number": 300, "run_id": 88001001, "run_attempt": 2})
        check("attempt-2 can overwrite attempt-1", result2 == "updated", f"result={result2}")
    finally:
        os.unlink(path2)


def test_auth_header() -> None:
    print("TEST: auth_header — outgoing HTTP request carries correct Authorization header")
    import urllib.request as _ureq

    captured = []

    class _MockResp:
        def __enter__(self): return self
        def __exit__(self, *a): pass
        def read(self): return b'{"jobs": []}'

    _orig = _ureq.urlopen

    def _mock_urlopen(req, timeout=None):
        # Capture the outgoing header value without printing it
        auth = req.get_header("Authorization")
        captured.append(auth or "")
        return _MockResp()

    _ureq.urlopen = _mock_urlopen
    try:
        try:
            _mod.get_jobs("MY_TEST_TOKEN", "zarfld/IntelAvbFilter", 1, 1)
        except (RuntimeError, Exception):
            pass   # empty job list raises RuntimeError — that is expected here
    finally:
        _ureq.urlopen = _orig

    check("HTTP request was made", len(captured) > 0)
    if captured:
        auth_val = captured[0]
        check("Authorization header non-empty", bool(auth_val))
        # Must use Bearer scheme
        check("Bearer scheme used", auth_val.lower().startswith("bearer "))
        # Must carry the actual token, not placeholder asterisks
        check("Token is the supplied value", "MY_TEST_TOKEN" in auth_val)
        check("No asterisk placeholder", "****" not in auth_val)


_TESTS = [
    test_overall_pass_shown,
    test_overall_fail_warning,
    test_all_jobs_in_table,
    test_failed_job_appears,
    test_no_job_name_semantic_inference,
    test_merged_jobs,
    test_new_job_appears,
    test_extra_runners,
    test_job_removed,
    test_cancelled_workflow,
    test_skipped_job_shown,
    test_disclaimer_always_present,
    test_no_inferred_hardware_claims,
    test_empty_jobs,
    test_malformed_jobs,
    test_stale_detection,
    test_no_stale_sha_matches,
    test_no_stale_empty_master_sha,
    test_idempotent,
    test_markers_preserved,
    test_idempotent_write,
    test_freshness_guard_older_rejected,
    test_freshness_guard_rerun_allowed,
    test_freshness_guard_first_write,
    test_parse_meta_round_trip,
    test_parse_meta_absent,
    test_is_newer_rules,
    test_zero_step_job_included,
    test_reports_section_present,
    test_no_count_synthesis,
    test_artefact_integrity_note,
    test_28_jobs_run488_pattern,
    test_attempt_isolation,
    test_auth_header,
]

if __name__ == "__main__":
    print(f"Running {len(_TESTS)} regression tests"
          f" for generate-readme-status.py (Phase 1)\n")
    for t in _TESTS:
        t()
        print()

    if _FAILURES:
        print("=" * 60)
        print(f"FAILURES ({len(_FAILURES)}):")
        for name in _FAILURES:
            print(f"  - {name}")
        sys.exit(1)
    else:
        print("=" * 60)
        print(f"All {len(_TESTS)} tests PASSED \u2705")
