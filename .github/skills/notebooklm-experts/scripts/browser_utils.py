"""
Browser Utilities for NotebookLM Skill
Handles browser launching, stealth features, and common interactions
"""

import json
import time
import random
from pathlib import Path
from typing import Optional, List

from patchright.sync_api import Playwright, BrowserContext, Page
from config import BROWSER_ARGS, USER_AGENT


class BrowserFactory:
    """Factory for creating configured browser contexts"""

    @staticmethod
    def launch_persistent_context(
        playwright: Playwright,
        headless: bool = True,
        user_data_dir: Optional[str] = None,
        state_file: Optional[Path] = None,
    ) -> BrowserContext:
        """
        Launch a persistent browser context with anti-detection features
        and cookie workaround.

        Args:
            playwright: Playwright instance
            headless: Run headless
            user_data_dir: Chrome profile directory. If None, resolves from active profile.
            state_file: Path to state.json for cookie injection. If None, resolves from active profile.
        """
        if user_data_dir is None or state_file is None:
            from profile_manager import ProfileManager
            pm = ProfileManager()
            paths = pm.get_active_paths()
            if user_data_dir is None:
                user_data_dir = str(paths["browser_profile_dir"])
            if state_file is None:
                state_file = paths["state_file"]

        # Launch persistent context
        context = playwright.chromium.launch_persistent_context(
            user_data_dir=user_data_dir,
            channel="chrome",  # Use real Chrome
            headless=headless,
            no_viewport=True,
            ignore_default_args=["--enable-automation"],
            user_agent=USER_AGENT,
            args=BROWSER_ARGS
        )

        # Cookie Workaround for Playwright bug #36139
        # Session cookies (expires=-1) don't persist in user_data_dir automatically
        BrowserFactory._inject_cookies(context, state_file)

        return context

    @staticmethod
    def _inject_cookies(context: BrowserContext, state_file: Path):
        """Inject cookies from state.json if available"""
        if state_file.exists():
            try:
                with open(state_file, 'r') as f:
                    state = json.load(f)
                    if 'cookies' in state and len(state['cookies']) > 0:
                        context.add_cookies(state['cookies'])
            except Exception as e:
                print(f"  Warning: Could not load state.json: {e}")


class StealthUtils:
    """Human-like interaction utilities"""

    @staticmethod
    def random_delay(min_ms: int = 100, max_ms: int = 500):
        """Add random delay"""
        time.sleep(random.uniform(min_ms / 1000, max_ms / 1000))

    @staticmethod
    def human_type(page: Page, selector: str, text: str, wpm_min: int = 320, wpm_max: int = 480):
        """Type with human-like speed"""
        element = page.query_selector(selector)
        if not element:
            # Try waiting if not immediately found
            try:
                element = page.wait_for_selector(selector, timeout=2000)
            except:
                pass

        if not element:
            print(f"Warning: Element not found for typing: {selector}")
            return

        # Click to focus
        element.click()

        # Type
        for char in text:
            element.type(char, delay=random.uniform(25, 75))
            if random.random() < 0.05:
                time.sleep(random.uniform(0.15, 0.4))

    @staticmethod
    def realistic_click(page: Page, selector: str):
        """Click with realistic movement"""
        element = page.query_selector(selector)
        if not element:
            return

        # Optional: Move mouse to element (simplified)
        box = element.bounding_box()
        if box:
            x = box['x'] + box['width'] / 2
            y = box['y'] + box['height'] / 2
            page.mouse.move(x, y, steps=5)

        StealthUtils.random_delay(100, 300)
        element.click()
        StealthUtils.random_delay(100, 300)


# ------------------------------------------------------------------ #
# M3 — Add Web Source                                                  #
# ------------------------------------------------------------------ #

def add_source_web(
    notebook_url: str,
    source_url: str,
    profile_id: Optional[str] = None,
    headless: bool = True,
) -> bool:
    """Add a web URL (or YouTube URL) as a source to a NotebookLM notebook.

    Args:
        notebook_url: Full URL of the target notebook
        source_url: Web URL or YouTube URL to add as a source
        profile_id: Profile to use; None = active profile
        headless: Run browser headlessly

    Returns:
        True on success, False on failure
    """
    from patchright.sync_api import sync_playwright
    from auth_manager import AuthManager

    auth = AuthManager(profile_id=profile_id)
    if not auth.is_authenticated():
        print("Error: Not authenticated. Run auth_manager.py setup first.")
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
        page.set_viewport_size({"width": 1440, "height": 900})

        print("  Navigating to notebook...")
        page.goto(notebook_url, wait_until="domcontentloaded")
        page.wait_for_timeout(5000)

        # Open the add-sources dialog
        add_btn_sel = 'button[aria-label="Thêm nguồn"]'
        page.wait_for_selector(add_btn_sel, timeout=15000)
        page.click(add_btn_sel)
        page.wait_for_timeout(1500)
        print("  Opened add-source dialog")

        # Click the "Trang web" (Website) option inside the dialog
        web_btn = page.query_selector('button:has-text("Trang web")')
        if not web_btn:
            print("  Error: 'Trang web' button not found in dialog")
            return False
        web_btn.click()
        page.wait_for_timeout(1500)
        print("  Clicked 'Trang web'")

        # Fill URL into the query box textarea
        url_input_selectors = [
            'textarea[aria-label="Hộp truy vấn"]',
            'textarea.query-box-input',
        ]
        filled = False
        for sel in url_input_selectors:
            try:
                if page.is_visible(sel):
                    page.fill(sel, source_url)
                    filled = True
                    print(f"  Filled URL via: {sel}")
                    break
            except Exception:
                continue

        if not filled:
            print("  Error: URL input textarea not found")
            return False

        page.keyboard.press("Enter")
        page.wait_for_timeout(3000)
        print(f"  Source submitted: {source_url}")
        return True

    except Exception as e:
        print(f"  Error adding source: {e}")
        return False

    finally:
        if context:
            try:
                context.close()
            except Exception:
                pass
        if playwright:
            try:
                playwright.stop()
            except Exception:
                pass
