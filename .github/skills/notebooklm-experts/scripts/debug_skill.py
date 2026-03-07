#!/usr/bin/env python3
"""
Smoke Test Runner for NotebookLM Skill
Tests each functional layer in sequence.
Each failed layer prints a direct fix hint inline — no need to dig through logs.

Usage:
    .\\run.bat debug_skill.py
    .\\run.bat debug_skill.py --no-browser   # Skip browser launch (faster)
    .\\run.bat debug_skill.py --check-links  # Also validate all notebook URLs
"""

import sys
import time
import argparse
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).parent))

# ANSI colors — supported on Windows Terminal, VS Code, and most CI systems
_GREEN  = "\033[92m"
_YELLOW = "\033[93m"
_RED    = "\033[91m"
_CYAN   = "\033[96m"
_RESET  = "\033[0m"
_BOLD   = "\033[1m"

STATUS_PASS = f"{_GREEN}[PASS]{_RESET}"
STATUS_WARN = f"{_YELLOW}[WARN]{_RESET}"
STATUS_FAIL = f"{_RED}[FAIL]{_RESET}"
STATUS_SKIP = f"{_CYAN}[SKIP]{_RESET}"


class SmokeTestResult:
    """Holds the outcome of a single smoke test check"""

    def __init__(self, layer: str, name: str, status: str, detail: str, hint: str = ""):
        self.layer = layer
        self.name = name
        self.status = status  # "pass" | "warn" | "fail" | "skip"
        self.detail = detail
        self.hint = hint

    def badge(self) -> str:
        return {
            "pass": STATUS_PASS,
            "warn": STATUS_WARN,
            "fail": STATUS_FAIL,
            "skip": STATUS_SKIP,
        }.get(self.status, STATUS_SKIP)


class SmokeTester:
    """
    Runs smoke tests across 5 layers in dependency order.
    A critical FAIL in Layer 4 (browser) blocks Layer 5 (links) automatically.
    """

    def __init__(self, run_browser: bool = True, check_links: bool = False):
        self.run_browser = run_browser
        self.check_links = check_links
        self.results: List[SmokeTestResult] = []
        self._browser_failed = False

    # ── Layer 1: Environment ──────────────────────────────────────────────

    def _check_venv(self) -> SmokeTestResult:
        venv_dir = Path(__file__).parent.parent / ".venv"
        if venv_dir.exists():
            return SmokeTestResult("env", "Virtual environment (.venv)", "pass", str(venv_dir))
        return SmokeTestResult(
            "env", "Virtual environment (.venv)", "fail",
            f"Not found at: {venv_dir}",
            hint="Run:  python install.py"
        )

    def _check_patchright(self) -> SmokeTestResult:
        try:
            import patchright  # noqa: F401
            from patchright.sync_api import sync_playwright  # noqa: F401
            return SmokeTestResult("env", "patchright importable", "pass", "")
        except ImportError as e:
            return SmokeTestResult(
                "env", "patchright importable", "fail", str(e),
                hint="Run:  python install.py"
            )

    def _check_requirements_file(self) -> SmokeTestResult:
        req = Path(__file__).parent.parent / "requirements.txt"
        if req.exists():
            return SmokeTestResult("env", "requirements.txt present", "pass", str(req))
        return SmokeTestResult(
            "env", "requirements.txt present", "warn",
            "File missing — skill may still work if packages are manually installed",
            hint="Restore requirements.txt from source control."
        )

    # ── Layer 2: Auth ─────────────────────────────────────────────────────

    def _check_auth_state(self) -> SmokeTestResult:
        try:
            from auth_manager import AuthManager
            mgr = AuthManager()

            if not mgr.state_file or not mgr.state_file.exists():
                return SmokeTestResult(
                    "auth", "Auth state file (state.json)", "fail",
                    "state.json not found — never authenticated",
                    hint="Run:  .\\run.bat auth_manager.py setup  (browser opens for Google login)"
                )

            age_days = (time.time() - mgr.state_file.stat().st_mtime) / 86400
            if age_days > 7:
                return SmokeTestResult(
                    "auth", "Auth state freshness", "warn",
                    f"state.json is {age_days:.1f} days old — session may be expired",
                    hint="Run:  .\\run.bat auth_manager.py reauth"
                )

            return SmokeTestResult(
                "auth", "Auth state (state.json)", "pass",
                f"Age: {age_days * 24:.1f}h"
            )
        except Exception as e:
            return SmokeTestResult(
                "auth", "Auth state check", "fail", str(e),
                hint="Run:  .\\run.bat auth_manager.py status"
            )

    # ── Layer 3: Library ──────────────────────────────────────────────────

    def _check_library(self) -> SmokeTestResult:
        try:
            from notebook_manager import NotebookLibrary
            lib = NotebookLibrary()
            count = len(lib.notebooks)

            if count == 0:
                return SmokeTestResult(
                    "library", "Notebook library", "warn",
                    "Library is empty — no notebooks registered",
                    hint=(
                        "Run:  .\\run.bat notebook_manager.py add \\\n"
                        "           --url \"https://notebooklm.google.com/notebook/...\" \\\n"
                        "           --name \"My Docs\" --description \"...\" --topics \"topic1,topic2\""
                    )
                )
            active = lib.active_notebook_id or "(none set)"
            return SmokeTestResult(
                "library", "Notebook library", "pass",
                f"{count} notebook(s) — active: {active}"
            )
        except Exception as e:
            return SmokeTestResult(
                "library", "Notebook library load", "fail", str(e),
                hint="library.json may be corrupt. Run:  .\\run.bat cleanup_manager.py --preserve-library"
            )

    def _check_active_notebook(self) -> SmokeTestResult:
        try:
            from notebook_manager import NotebookLibrary
            lib = NotebookLibrary()

            if not lib.active_notebook_id:
                return SmokeTestResult(
                    "library", "Active notebook set", "warn",
                    "No active notebook — queries require explicit --notebook-id",
                    hint="Run:  .\\run.bat notebook_manager.py activate --id <id>"
                )

            nb = lib.get_notebook(lib.active_notebook_id)
            if nb and not nb.get("url"):
                return SmokeTestResult(
                    "library", "Active notebook URL", "fail",
                    f"Notebook '{lib.active_notebook_id}' has no URL set",
                    hint="Run:  .\\run.bat notebook_manager.py update --id <id> --url <url>"
                )

            return SmokeTestResult(
                "library", "Active notebook set", "pass",
                f"id={lib.active_notebook_id}"
            )
        except Exception as e:
            return SmokeTestResult("library", "Active notebook check", "fail", str(e))

    # ── Layer 4: Browser ──────────────────────────────────────────────────

    def _check_browser_launch(self) -> SmokeTestResult:
        if not self.run_browser:
            return SmokeTestResult(
                "browser", "Browser launch", "skip",
                "Skipped via --no-browser"
            )
        try:
            from patchright.sync_api import sync_playwright
            from browser_utils import BrowserFactory

            with sync_playwright() as p:
                context = BrowserFactory.launch_persistent_context(p, headless=True)
                page = context.new_page()
                page.goto("https://notebooklm.google.com/", wait_until="domcontentloaded")
                title = page.title()
                url_after = page.url
                context.close()

            if "accounts.google.com" in url_after:
                return SmokeTestResult(
                    "browser", "NotebookLM home reachable", "fail",
                    f"Redirected to Google login (session expired). URL: {url_after}",
                    hint="Run:  .\\run.bat auth_manager.py reauth"
                )

            return SmokeTestResult(
                "browser", "Browser launch + NotebookLM home", "pass",
                f"Title: {title!r}"
            )
        except Exception as e:
            self._browser_failed = True
            return SmokeTestResult(
                "browser", "Browser launch", "fail", str(e),
                hint="Chrome may not be installed. Run:  python install.py"
            )

    # ── Layer 5: Notebook Links ───────────────────────────────────────────

    def _check_notebook_links(self) -> List[SmokeTestResult]:
        if not self.check_links:
            return [SmokeTestResult(
                "links", "Notebook link validation", "skip",
                "Pass --check-links to enable this layer"
            )]

        if self._browser_failed:
            return [SmokeTestResult(
                "links", "Notebook link validation", "skip",
                "Skipped — browser layer failed"
            )]

        try:
            from check_notebooks import NotebookValidator
            from notebook_manager import NotebookLibrary

            lib = NotebookLibrary()
            if not lib.notebooks:
                return [SmokeTestResult(
                    "links", "Notebook links", "skip",
                    "No notebooks in library"
                )]

            validator = NotebookValidator()
            validator.validate_all()

            results = []
            for nb_id, result in validator.results.items():
                nb = lib.notebooks.get(nb_id, {})
                name = nb.get("name", nb_id)
                status = result["status"]
                hint = (
                    f"Update URL:  .\\run.bat notebook_manager.py update --id {nb_id} --url <new-url>"
                    if status != "active" else ""
                )
                results.append(SmokeTestResult(
                    "links", f"Link: {name}", status,
                    result.get("reason", ""), hint=hint
                ))
            return results

        except Exception as e:
            return [SmokeTestResult(
                "links", "Notebook link check", "fail", str(e),
                hint="Run individually:  .\\run.bat check_notebooks.py"
            )]

    # ── Runner ────────────────────────────────────────────────────────────

    def run(self):
        print(f"\n{_BOLD}{'=' * 56}")
        print("  NotebookLM Skill — Smoke Test Runner")
        print(f"{'=' * 56}{_RESET}\n")

        layer_groups = [
            ("Layer 1: Environment", [
                self._check_venv,
                self._check_patchright,
                self._check_requirements_file,
            ]),
            ("Layer 2: Auth", [
                self._check_auth_state,
            ]),
            ("Layer 3: Library", [
                self._check_library,
                self._check_active_notebook,
            ]),
            ("Layer 4: Browser", [
                self._check_browser_launch,
            ]),
        ]

        for layer_name, checks in layer_groups:
            print(f"{_BOLD}{_CYAN}{layer_name}{_RESET}")
            for fn in checks:
                result = fn()
                self.results.append(result)
                self._print_result(result)
            print()

        print(f"{_BOLD}{_CYAN}Layer 5: Notebook Links{_RESET}")
        for result in self._check_notebook_links():
            self.results.append(result)
            self._print_result(result)
        print()

        self._print_summary()

    def _print_result(self, r: SmokeTestResult):
        detail_str = f"  {_CYAN}{r.detail}{_RESET}" if r.detail else ""
        print(f"  {r.badge()}  {r.name}{detail_str}")
        if r.hint and r.status in ("fail", "warn"):
            for line in r.hint.split("\n"):
                print(f"         {_YELLOW}→ {line}{_RESET}")

    def _print_summary(self):
        passed  = sum(1 for r in self.results if r.status == "pass")
        warned  = sum(1 for r in self.results if r.status == "warn")
        failed  = sum(1 for r in self.results if r.status == "fail")
        skipped = sum(1 for r in self.results if r.status == "skip")
        total   = len(self.results)

        print(f"{_BOLD}{'=' * 56}")
        print("  SUMMARY")
        print(f"{'=' * 56}{_RESET}")
        print(
            f"  {STATUS_PASS} {passed}  "
            f"{STATUS_WARN} {warned}  "
            f"{STATUS_FAIL} {failed}  "
            f"{STATUS_SKIP} {skipped}  "
            f"of {total} checks"
        )

        if failed > 0:
            print(f"\n  {_RED}Skill has critical issues — fix FAIL items above.{_RESET}")
            print(f"  See: references/debugging.md for per-layer recovery steps.")
        elif warned > 0:
            print(f"\n  {_YELLOW}Skill is functional but has warnings.{_RESET}")
            print(f"  See: references/debugging.md for details.")
        else:
            print(f"\n  {_GREEN}All checks passed — skill is fully healthy.{_RESET}")
        print()


def main():
    parser = argparse.ArgumentParser(
        description="Smoke test runner for NotebookLM skill"
    )
    parser.add_argument(
        "--no-browser",
        action="store_true",
        help="Skip browser launch checks (faster, no Chrome needed)"
    )
    parser.add_argument(
        "--check-links",
        action="store_true",
        help="Also validate each notebook URL against NotebookLM (slower)"
    )
    args = parser.parse_args()

    tester = SmokeTester(
        run_browser=not args.no_browser,
        check_links=args.check_links
    )
    tester.run()

    # Non-zero exit on FAIL — safe to use in CI pipelines
    sys.exit(1 if any(r.status == "fail" for r in tester.results) else 0)


if __name__ == "__main__":
    main()
