#!/usr/bin/env python3
"""
Notebook Link Validator for NotebookLM
Checks if notebook links in the library are active and accessible.
"""

import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Any

from patchright.sync_api import sync_playwright, Page

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from config import (
    QUERY_INPUT_SELECTORS, 
    LOGIN_TIMEOUT_MINUTES,
    USER_AGENT
)
from browser_utils import BrowserFactory
from auth_manager import AuthManager
from notebook_manager import NotebookLibrary


class NotebookValidator:
    """Validates notebook links against NotebookLM"""

    def __init__(self, profile_id=None):
        """Initialize validator"""
        self.library = NotebookLibrary(profile_id=profile_id)
        self.auth = AuthManager(profile_id=profile_id)
        self.results = {}

    def validate_all(self):
        """Validate all notebooks in the library"""
        notebooks = self.library.notebooks
        if not notebooks:
            print("No notebooks in library to validate.")
            return

        print(f"Found {len(notebooks)} notebooks. Starting validation...")
        
        # Check authentication first
        if not self.auth.is_authenticated():
            print("Warning: valid authentication not found. Validation may fail.")
            print("Run 'python scripts/auth_manager.py' to login first.")
            # Continue anyway as some notebooks might be public (though rare for NotebookLM)
            # Actually, NotebookLM usually requires login even for shared ones.
            response = input("Continue without verified auth? (y/n): ")
            if response.lower() != 'y':
                return

        with sync_playwright() as p:
            # Launch browser with profile-specific paths
            context = BrowserFactory.launch_persistent_context(
                p,
                headless=True,
                user_data_dir=str(self.auth.browser_profile_dir),
                state_file=self.auth.state_file,
            )
            page = context.new_page()
            
            # Check if logged in by visiting home
            print("Verifying session...")
            page.goto("https://notebooklm.google.com/")
            time.sleep(3)
            
            if "Sign in" in page.title() or "accounts.google.com" in page.url:
                print("Error: Not logged in. Please run authentication script.")
                context.close()
                return

            try:
                for notebook_id, notebook in notebooks.items():
                    self._check_notebook(page, notebook_id, notebook)
            finally:
                context.close()
        
        self._print_report()
        self._update_library()

    def _check_notebook(self, page: Page, notebook_id: str, notebook: Dict[str, Any]):
        """Check a single notebook"""
        url = notebook.get('url')
        name = notebook.get('name', 'Unknown')
        
        if not url:
            print(f"Skipping {name} ({notebook_id}): No URL")
            self.results[notebook_id] = {'status': 'error', 'reason': 'No URL'}
            return

        print(f"Checking: {name} ({url})...", end='', flush=True)
        
        try:
            response = page.goto(url, wait_until="domcontentloaded")
            
            # Wait a bit for redirects or JS loading
            page.wait_for_timeout(5000)
            
            # Check for common error indicators
            current_url = page.url
            content = page.content()
            
            is_active = False
            reason = "Unknown"
            
            # Case 1: Success - Input box is present
            for selector in QUERY_INPUT_SELECTORS:
                if page.is_visible(selector):
                    is_active = True
                    reason = "Accessible"
                    break

            # Extract real title from page when accessible
            detected_title = None
            if is_active:
                raw_title = page.title()
                if raw_title and " - NotebookLM" in raw_title:
                    detected_title = raw_title.rsplit(" - NotebookLM", 1)[0].strip()
            
            if not is_active:
                # Case 2: Redirected to home (common when notebook doesn't exist or no access)
                if current_url == "https://notebooklm.google.com/":
                    is_active = False
                    reason = "Redirected to home (Not Found/No Access)"
                # Case 3: Explicit 404 or Error text (needs actual text from UI, guessing common patterns)
                elif "Notebook not found" in content or "You need access" in content:
                    is_active = False
                    reason = "Access Denied / Not Found"
                # Case 4: 404 HTTP status (unlikely with SPA, but possible)
                elif response and response.status == 404:
                    is_active = False
                    reason = "HTTP 404"
                else:
                    # Fallback success check: Look for title or other unique elements
                    if "NotebookLM" in page.title():
                        # Maybe it loaded but input box isn't visible?
                        # Assume failure if input not found for now to be safe
                        is_active = False
                        reason = f"Loaded but input missing. Title: {page.title()}"
            
            status = 'active' if is_active else 'inactive'
            print(f" [{status.upper()}] - {reason}")
            
            self.results[notebook_id] = {
                'status': status, 
                'reason': reason,
                'checked_at': datetime.now().isoformat(),
                'detected_title': detected_title,
            }
            
        except Exception as e:
            print(f" [ERROR] - {e}")
            self.results[notebook_id] = {'status': 'error', 'reason': str(e)}

    def _print_report(self):
        """Print validation report"""
        print("\n" + "="*50)
        print("NOTEBOOK VALIDATION REPORT")
        print("="*50)
        
        active_count = sum(1 for r in self.results.values() if r.get('status') == 'active')
        total_count = len(self.results)
        
        print(f"Total Notebooks: {total_count}")
        print(f"Active: {active_count}")
        print(f"Issues: {total_count - active_count}")
        print("-" * 50)
        
        for notebook_id, result in self.results.items():
            status = result['status']
            if status != 'active':
                notebook = self.library.notebooks.get(notebook_id, {})
                name = notebook.get('name', notebook_id)
                print(f"[{status.upper()}] {name}: {result['reason']}")

    def _update_library(self):
        """Update library with validation status and detected titles"""
        print("\nUpdating library metadata...")
        for notebook_id, result in self.results.items():
            if notebook_id in self.library.notebooks:
                self.library.notebooks[notebook_id]['last_check_status'] = result['status']
                self.library.notebooks[notebook_id]['last_check_reason'] = result['reason']
                self.library.notebooks[notebook_id]['last_checked'] = datetime.now().isoformat()

                # Sync detected title if available and different
                detected = result.get('detected_title')
                if detected and detected != self.library.notebooks[notebook_id].get('name'):
                    old_name = self.library.notebooks[notebook_id]['name']
                    self.library.notebooks[notebook_id]['name'] = detected
                    print(f"  Updated title: '{old_name}' → '{detected}'")
        
        self.library._save_library()
        print("Library updated.")


if __name__ == "__main__":
    import argparse
    from datetime import datetime

    parser = argparse.ArgumentParser(description='Validate notebook links')
    parser.add_argument('--profile', help='Profile to use (default: active)')
    args = parser.parse_args()

    validator = NotebookValidator(profile_id=getattr(args, 'profile', None))
    validator.validate_all()
