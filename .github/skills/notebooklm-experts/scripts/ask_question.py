#!/usr/bin/env python3
"""
Simple NotebookLM Question Interface
Based on MCP server implementation - simplified without sessions

Implements hybrid auth approach:
- Persistent browser profile (user_data_dir) for fingerprint consistency
- Manual cookie injection from state.json for session cookies (Playwright bug workaround)
See: https://github.com/microsoft/playwright/issues/36139
"""

import argparse
import sys
import time
import re
from pathlib import Path

from patchright.sync_api import sync_playwright

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from auth_manager import AuthManager
from notebook_manager import NotebookLibrary
from config import QUERY_INPUT_SELECTORS, RESPONSE_SELECTORS
from browser_utils import BrowserFactory, StealthUtils


# Follow-up reminder (adapted from MCP server for stateless operation)
# Since we don't have persistent sessions, we encourage comprehensive questions
FOLLOW_UP_REMINDER = (
    "\n\nEXTREMELY IMPORTANT: Is that ALL you need to know? "
    "You can always ask another question! Think about it carefully: "
    "before you reply to the user, review their original request and this answer. "
    "If anything is still unclear or missing, ask me another comprehensive question "
    "that includes all necessary context (since each question opens a new browser session)."
)


def _extract_notebook_title(raw_title: str) -> str:
    """Normalize NotebookLM browser title to the notebook display name."""
    if not raw_title:
        return ""
    if " - NotebookLM" in raw_title:
        return raw_title.rsplit(" - NotebookLM", 1)[0].strip()
    if raw_title == "NotebookLM":
        return ""
    return raw_title.strip()


def refresh_notebook_name_only(notebook_url: str, headless: bool = True, profile_id: str = None) -> bool:
    """Open a notebook and refresh its stored name without asking a question."""
    auth = AuthManager(profile_id=profile_id)

    if not auth.is_authenticated():
        print("Warning: Not authenticated. Run: auth_manager.py setup")
        return False

    playwright = None
    context = None

    try:
        playwright = sync_playwright().start()
        context = BrowserFactory.launch_persistent_context(
            playwright,
            headless=headless,
            user_data_dir=str(auth.browser_profile_dir),
            state_file=auth.state_file,
        )

        page = context.new_page()
        print("  Opening notebook for name refresh...")
        page.goto(notebook_url, wait_until="domcontentloaded")
        page.wait_for_url(re.compile(r"^https://notebooklm\.google\.com/"), timeout=10000)

        detected_title = ""
        deadline = time.time() + 10
        while time.time() < deadline and not detected_title:
            detected_title = _extract_notebook_title(page.title())
            if detected_title:
                break
            page.wait_for_timeout(500)

        if not detected_title:
            print("  Could not detect notebook title from page")
            return False

        library = NotebookLibrary(profile_id=profile_id)
        changed = library.refresh_notebook_name(
            notebook_url=notebook_url,
            detected_title=detected_title,
        )

        if changed:
            print("  Notebook name refreshed")
        else:
            print("  Notebook name already up to date")
        return True

    except Exception as e:
        print(f"  Error refreshing notebook name: {e}")
        return False

    finally:
        if context:
            try:
                context.close()
            except:
                pass

        if playwright:
            try:
                playwright.stop()
            except:
                pass


def ask_notebooklm(question: str, notebook_url: str, headless: bool = True, profile_id: str = None) -> str:
    """
    Ask a question to NotebookLM

    Args:
        question: Question to ask
        notebook_url: NotebookLM notebook URL
        headless: Run browser in headless mode
        profile_id: Profile to use (None = active)

    Returns:
        Answer text from NotebookLM
    """
    auth = AuthManager(profile_id=profile_id)

    if not auth.is_authenticated():
        print("Warning: Not authenticated. Run: auth_manager.py setup")
        return None

    print(f"Asking: {question}")
    print(f"Notebook: {notebook_url}")
    if auth.profile_id:
        print(f"Profile: {auth.profile_id}")

    playwright = None
    context = None

    try:
        # Start playwright
        playwright = sync_playwright().start()

        # Launch persistent browser context using factory with profile paths
        context = BrowserFactory.launch_persistent_context(
            playwright,
            headless=headless,
            user_data_dir=str(auth.browser_profile_dir),
            state_file=auth.state_file,
        )

        # Navigate to notebook
        page = context.new_page()
        print("  Opening notebook...")
        page.goto(notebook_url, wait_until="domcontentloaded")

        # Wait for NotebookLM
        page.wait_for_url(re.compile(r"^https://notebooklm\.google\.com/"), timeout=10000)

        # Refresh notebook name in library if page title changed.
        try:
            detected_title = _extract_notebook_title(page.title())

            if detected_title:
                library = NotebookLibrary(profile_id=profile_id)
                library.refresh_notebook_name(
                    notebook_url=notebook_url,
                    detected_title=detected_title,
                )
        except Exception as refresh_err:
            print(f"  Warning: Could not refresh notebook name: {refresh_err}")

        # Wait for query input (MCP approach)
        print("  Waiting for query input...")
        query_element = None

        for selector in QUERY_INPUT_SELECTORS:
            try:
                query_element = page.wait_for_selector(
                    selector,
                    timeout=10000,
                    state="visible"  # Only check visibility, not disabled!
                )
                if query_element:
                    print(f"  Found input: {selector}")
                    break
            except:
                continue

        if not query_element:
            print("  Could not find query input")
            return None

        # Type question (human-like, fast)
        print("  Typing question...")

        # Use primary selector for typing
        input_selector = QUERY_INPUT_SELECTORS[0]
        StealthUtils.human_type(page, input_selector, question)

        # Submit
        print("  Submitting...")
        page.keyboard.press("Enter")

        # Small pause
        StealthUtils.random_delay(500, 1500)

        # Wait for response (MCP approach: poll for stable text)
        print("  Waiting for answer...")

        answer = None
        stable_count = 0
        last_text = None
        deadline = time.time() + 120  # 2 minutes timeout

        while time.time() < deadline:
            # Check if NotebookLM is still thinking (most reliable indicator)
            try:
                thinking_element = page.query_selector('div.thinking-message')
                if thinking_element and thinking_element.is_visible():
                    time.sleep(1)
                    continue
            except:
                pass

            # Try to find response with MCP selectors
            for selector in RESPONSE_SELECTORS:
                try:
                    elements = page.query_selector_all(selector)
                    if elements:
                        # Get last (newest) response
                        latest = elements[-1]
                        text = latest.inner_text().strip()

                        if text:
                            if text == last_text:
                                stable_count += 1
                                if stable_count >= 3:  # Stable for 3 polls
                                    answer = text
                                    break
                            else:
                                stable_count = 0
                                last_text = text
                except:
                    continue

            if answer:
                break

            time.sleep(1)

        if not answer:
            print("  Timeout waiting for answer")
            return None

        print("  Got answer!")
        # Add follow-up reminder to encourage Copilot to ask more questions
        return answer + FOLLOW_UP_REMINDER

    except Exception as e:
        print(f"  Error: {e}")
        import traceback
        traceback.print_exc()
        return None

    finally:
        # Always clean up
        if context:
            try:
                context.close()
            except:
                pass

        if playwright:
            try:
                playwright.stop()
            except:
                pass


def main():
    parser = argparse.ArgumentParser(description='Ask NotebookLM a question')

    parser.add_argument('--question', help='Question to ask')
    parser.add_argument('--notebook-url', help='NotebookLM notebook URL')
    parser.add_argument('--notebook-id', help='Notebook ID from library')
    parser.add_argument('--profile', help='Profile to use (default: active profile)')
    parser.add_argument('--show-browser', action='store_true', help='Show browser')
    parser.add_argument(
        '--refresh-name-only',
        action='store_true',
        help='Open notebook and refresh stored notebook name without sending a question',
    )

    args = parser.parse_args()

    # Resolve notebook URL
    notebook_url = args.notebook_url

    profile_id = getattr(args, 'profile', None)

    if not notebook_url and args.notebook_id:
        library = NotebookLibrary(profile_id=profile_id)
        notebook = library.get_notebook(args.notebook_id)
        if notebook:
            notebook_url = notebook['url']
        else:
            print(f"Notebook '{args.notebook_id}' not found")
            return 1

    if not notebook_url:
        # Check for active notebook first
        library = NotebookLibrary(profile_id=profile_id)
        active = library.get_active_notebook()
        if active:
            notebook_url = active['url']
            print(f"Using active notebook: {active['name']}")
        else:
            # Show available notebooks
            notebooks = library.list_notebooks()
            if notebooks:
                print("\nAvailable notebooks:")
                for nb in notebooks:
                    mark = " [ACTIVE]" if nb.get('id') == library.active_notebook_id else ""
                    print(f"  {nb['id']}: {nb['name']}{mark}")
                print("\nSpecify with --notebook-id or set active:")
                print("python scripts/run.py notebook_manager.py activate --id ID")
            else:
                print("No notebooks in library. Add one first:")
                print("python scripts/run.py notebook_manager.py add --url URL --name NAME --description DESC --topics TOPICS")
            return 1

    if args.refresh_name_only:
        ok = refresh_notebook_name_only(
            notebook_url=notebook_url,
            headless=not args.show_browser,
            profile_id=profile_id,
        )
        return 0 if ok else 1

    if not args.question:
        print("Error: --question is required unless --refresh-name-only is used")
        return 1

    # Ask the question
    answer = ask_notebooklm(
        question=args.question,
        notebook_url=notebook_url,
        headless=not args.show_browser,
        profile_id=profile_id,
    )

    if answer:
        print("\n" + "=" * 60)
        print(f"Question: {args.question}")
        print("=" * 60)
        print()
        print(answer)
        print()
        print("=" * 60)
        return 0
    else:
        print("\nFailed to get answer")
        return 1


if __name__ == "__main__":
    sys.exit(main())
