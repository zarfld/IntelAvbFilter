#!/usr/bin/env python3
"""
Authentication Manager for NotebookLM
Handles Google login and browser state persistence with multi-profile support.

Implements hybrid auth approach:
- Persistent browser profile (user_data_dir) for fingerprint consistency
- Manual cookie injection from state.json for session cookies (Playwright bug workaround)
See: https://github.com/microsoft/playwright/issues/36139
"""

import json
import time
import argparse
import shutil
import re
import sys
from pathlib import Path
from typing import Optional, Dict, Any

from patchright.sync_api import sync_playwright, BrowserContext

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

from config import DATA_DIR
from browser_utils import BrowserFactory
from profile_manager import ProfileManager


class AuthManager:
    """
    Manages authentication and browser state for NotebookLM.
    Profile-aware: each profile has its own browser_state and cookies.
    """

    def __init__(self, profile_id: Optional[str] = None):
        """Initialize the authentication manager for a given profile.

        Args:
            profile_id: Explicit profile. None → active profile.
        """
        self.pm = ProfileManager()

        if profile_id:
            self.profile_id = profile_id
        elif self.pm.active_profile:
            self.profile_id = self.pm.active_profile
        else:
            self.profile_id = None

        if self.profile_id:
            paths = self.pm.get_paths(self.profile_id)
            self.state_file = paths["state_file"]
            self.auth_info_file = paths["auth_info_file"]
            self.browser_state_dir = paths["browser_state_dir"]
            self.browser_profile_dir = paths["browser_profile_dir"]
            # Ensure dirs exist
            self.browser_state_dir.mkdir(parents=True, exist_ok=True)
        else:
            self.state_file = None
            self.auth_info_file = None
            self.browser_state_dir = None
            self.browser_profile_dir = None

    def is_authenticated(self) -> bool:
        """Check if valid authentication exists for the current profile"""
        if not self.state_file or not self.state_file.exists():
            return False

        # Check if state file is not too old (7 days)
        age_days = (time.time() - self.state_file.stat().st_mtime) / 86400
        if age_days > 7:
            print(f"Warning: Browser state is {age_days:.1f} days old, may need re-authentication")

        return True

    def get_auth_info(self) -> Dict[str, Any]:
        """Get authentication information for the current profile"""
        info = {
            'profile_id': self.profile_id,
            'authenticated': self.is_authenticated(),
            'state_file': str(self.state_file) if self.state_file else None,
            'state_exists': self.state_file.exists() if self.state_file else False,
        }

        if self.auth_info_file and self.auth_info_file.exists():
            try:
                with open(self.auth_info_file, 'r') as f:
                    saved_info = json.load(f)
                    info.update(saved_info)
            except Exception:
                pass

        if info['state_exists']:
            age_hours = (time.time() - self.state_file.stat().st_mtime) / 3600
            info['state_age_hours'] = age_hours

        return info

    def setup_auth(self, headless: bool = False, timeout_minutes: int = 10) -> bool:
        """
        Perform interactive authentication setup

        Args:
            headless: Run browser in headless mode (False for login)
            timeout_minutes: Maximum time to wait for login

        Returns:
            True if authentication successful
        """
        if not self.profile_id:
            print("Error: No profile set. Create one first with: auth_manager.py setup --name <name>")
            return False

        print(f"Starting authentication setup for profile '{self.profile_id}'...")
        print(f"  Timeout: {timeout_minutes} minutes")

        playwright = None
        context = None

        try:
            playwright = sync_playwright().start()

            # Launch using factory with profile-specific paths
            context = BrowserFactory.launch_persistent_context(
                playwright,
                headless=headless,
                user_data_dir=str(self.browser_profile_dir),
                state_file=self.state_file,
            )

            # Navigate to NotebookLM
            page = context.new_page()
            page.goto("https://notebooklm.google.com", wait_until="domcontentloaded")

            # Check if already authenticated
            if "notebooklm.google.com" in page.url and "accounts.google.com" not in page.url:
                print("  Already authenticated!")
                self._save_browser_state(context)
                return True

            # Wait for manual login
            print("\n  Please log in to your Google account...")
            print(f"  Waiting up to {timeout_minutes} minutes for login...")

            try:
                # Wait for URL to change to NotebookLM (regex ensures it's the actual domain, not a parameter)
                timeout_ms = int(timeout_minutes * 60 * 1000)
                page.wait_for_url(re.compile(r"^https://notebooklm\.google\.com/"), timeout=timeout_ms)

                print(f"  Login successful!")

                # Save authentication state
                self._save_browser_state(context)
                self._save_auth_info()
                return True

            except Exception as e:
                print(f"  Authentication timeout: {e}")
                return False

        except Exception as e:
            print(f"  Error: {e}")
            return False

        finally:
            # Clean up browser resources
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

    def _save_browser_state(self, context: BrowserContext):
        """Save browser state to disk"""
        try:
            # Save storage state (cookies, localStorage)
            context.storage_state(path=str(self.state_file))
            print(f"  Saved browser state to: {self.state_file}")
        except Exception as e:
            print(f"  Failed to save browser state: {e}")
            raise

    def _save_auth_info(self):
        """Save authentication metadata and update profile registry"""
        try:
            now = time.time()
            info = {
                'authenticated_at': now,
                'authenticated_at_iso': time.strftime('%Y-%m-%d %H:%M:%S')
            }
            with open(self.auth_info_file, 'w') as f:
                json.dump(info, f, indent=2)
            # Update profile registry
            self.pm.update_profile(self.profile_id, authenticated_at=now)
        except Exception:
            pass  # Non-critical

    def clear_auth(self) -> bool:
        """
        Clear all authentication data

        Returns:
            True if cleared successfully
        """
        if not self.profile_id:
            print("Error: No profile set.")
            return False

        print(f"Clearing authentication data for profile '{self.profile_id}'...")

        try:
            # Remove browser state
            if self.state_file and self.state_file.exists():
                self.state_file.unlink()
                print("  Removed browser state")

            # Remove auth info
            if self.auth_info_file and self.auth_info_file.exists():
                self.auth_info_file.unlink()
                print("  Removed auth info")

            # Clear entire browser state directory
            if self.browser_state_dir and self.browser_state_dir.exists():
                shutil.rmtree(self.browser_state_dir)
                self.browser_state_dir.mkdir(parents=True, exist_ok=True)
                print("  Cleared browser data")

            return True

        except Exception as e:
            print(f"  Error clearing auth: {e}")
            return False

    def re_auth(self, headless: bool = False, timeout_minutes: int = 10) -> bool:
        """
        Perform re-authentication (clear and setup)

        Args:
            headless: Run browser in headless mode
            timeout_minutes: Login timeout in minutes

        Returns:
            True if successful
        """
        print("Starting re-authentication...")

        # Clear existing auth
        self.clear_auth()

        # Setup new auth
        return self.setup_auth(headless, timeout_minutes)

    def validate_auth(self) -> bool:
        """
        Validate that stored authentication works.
        Uses persistent context to match actual usage pattern.

        Returns:
            True if authentication is valid
        """
        if not self.is_authenticated():
            return False

        print(f"Validating authentication for profile '{self.profile_id}'...")

        playwright = None
        context = None

        try:
            playwright = sync_playwright().start()

            # Launch using factory with profile-specific paths
            context = BrowserFactory.launch_persistent_context(
                playwright,
                headless=True,
                user_data_dir=str(self.browser_profile_dir),
                state_file=self.state_file,
            )

            # Try to access NotebookLM
            page = context.new_page()
            page.goto("https://notebooklm.google.com", wait_until="domcontentloaded", timeout=30000)

            # Check if we can access NotebookLM
            if "notebooklm.google.com" in page.url and "accounts.google.com" not in page.url:
                print("  Authentication is valid")
                self.pm.update_profile(self.profile_id, last_validated=time.time())
                return True
            else:
                print("  Authentication is invalid (redirected to login)")
                return False

        except Exception as e:
            print(f"  Validation failed: {e}")
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


def main():
    """Command-line interface for authentication management"""
    parser = argparse.ArgumentParser(description='Manage NotebookLM authentication')

    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Setup command
    setup_parser = subparsers.add_parser('setup', help='Setup authentication (creates profile if --name given)')
    setup_parser.add_argument('--name', help='Create a new profile with this name before authenticating')
    setup_parser.add_argument('--profile', help='Authenticate an existing profile')
    setup_parser.add_argument('--headless', action='store_true', help='Run in headless mode')
    setup_parser.add_argument('--timeout', type=float, default=10, help='Login timeout in minutes (default: 10)')

    # Status command
    status_parser = subparsers.add_parser('status', help='Check authentication status')
    status_parser.add_argument('--profile', help='Check a specific profile (default: active)')

    # Validate command
    validate_parser = subparsers.add_parser('validate', help='Validate authentication works')
    validate_parser.add_argument('--profile', help='Validate a specific profile (default: active)')

    # Clear command
    clear_parser = subparsers.add_parser('clear', help='Clear authentication for a profile')
    clear_parser.add_argument('--profile', help='Clear a specific profile (default: active)')

    # Re-auth command
    reauth_parser = subparsers.add_parser('reauth', help='Re-authenticate (clear + setup)')
    reauth_parser.add_argument('--profile', help='Re-authenticate a specific profile (default: active)')
    reauth_parser.add_argument('--timeout', type=float, default=10, help='Login timeout in minutes (default: 10)')

    # List profiles command
    subparsers.add_parser('list', help='List all profiles with status')

    # Set active profile command
    set_active_parser = subparsers.add_parser('set-active', help='Switch active profile')
    set_active_parser.add_argument('--id', required=True, help='Profile ID to activate')

    # Delete profile command
    delete_parser = subparsers.add_parser('delete', help='Delete a profile')
    delete_parser.add_argument('--id', required=True, help='Profile ID to delete')

    args = parser.parse_args()

    if args.command == 'list':
        pm = ProfileManager()
        pm.print_profiles()
        return

    if args.command == 'set-active':
        pm = ProfileManager()
        pm.set_active(args.id)
        return

    if args.command == 'delete':
        pm = ProfileManager()
        pm.delete_profile(args.id)
        return

    if args.command == 'setup':
        profile_id = getattr(args, 'profile', None)
        if args.name:
            pm = ProfileManager()
            entry = pm.create_profile(args.name)
            profile_id = entry["id"]
        auth = AuthManager(profile_id=profile_id)
        if auth.setup_auth(headless=args.headless, timeout_minutes=args.timeout):
            print("\nAuthentication setup complete!")
            print("You can now use ask_question.py to query NotebookLM")
        else:
            print("\nAuthentication setup failed")
            exit(1)

    elif args.command == 'status':
        auth = AuthManager(profile_id=getattr(args, 'profile', None))
        info = auth.get_auth_info()
        print(f"\nAuthentication Status (profile: {info.get('profile_id', 'N/A')}):")
        print(f"  Authenticated: {'Yes' if info['authenticated'] else 'No'}")
        if info.get('state_age_hours'):
            print(f"  State age: {info['state_age_hours']:.1f} hours")
        if info.get('authenticated_at_iso'):
            print(f"  Last auth: {info['authenticated_at_iso']}")
        print(f"  State file: {info['state_file']}")

    elif args.command == 'validate':
        auth = AuthManager(profile_id=getattr(args, 'profile', None))
        if auth.validate_auth():
            print("Authentication is valid and working")
        else:
            print("Authentication is invalid or expired")
            print("Run: auth_manager.py reauth")

    elif args.command == 'clear':
        auth = AuthManager(profile_id=getattr(args, 'profile', None))
        if auth.clear_auth():
            print("Authentication cleared")

    elif args.command == 'reauth':
        auth = AuthManager(profile_id=getattr(args, 'profile', None))
        if auth.re_auth(timeout_minutes=args.timeout):
            print("\nRe-authentication complete!")
        else:
            print("\nRe-authentication failed")
            exit(1)

    else:
        parser.print_help()


if __name__ == "__main__":
    main()
