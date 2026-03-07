#!/usr/bin/env python3
"""
Profile Manager for NotebookLM Skill
Manages multiple Google account profiles with isolated browser state and libraries.
"""

import json
import shutil
import time
from pathlib import Path
from typing import Dict, List, Optional, Any

from config import DATA_DIR

PROFILES_DIR = DATA_DIR / "profiles"
PROFILES_FILE = DATA_DIR / "profiles.json"

# Auth expiry threshold in days
AUTH_EXPIRY_DAYS = 7
AUTH_WARNING_DAYS = 5


def _migrate_legacy_layout():
    """
    One-time migration: move old flat data/ layout into data/profiles/default/.
    Safe to call multiple times — no-ops if already migrated or no legacy data.
    """
    legacy_browser_state = DATA_DIR / "browser_state"
    legacy_auth_info = DATA_DIR / "auth_info.json"
    legacy_library = DATA_DIR / "library.json"

    if PROFILES_DIR.exists():
        return  # Already migrated

    has_legacy = legacy_browser_state.exists() or legacy_auth_info.exists() or legacy_library.exists()
    if not has_legacy:
        return  # Fresh install, nothing to migrate

    print("Migrating legacy data layout to multi-profile structure...")
    default_dir = PROFILES_DIR / "default"
    default_dir.mkdir(parents=True, exist_ok=True)

    if legacy_browser_state.exists():
        shutil.move(str(legacy_browser_state), str(default_dir / "browser_state"))

    if legacy_auth_info.exists():
        shutil.move(str(legacy_auth_info), str(default_dir / "auth_info.json"))

    if legacy_library.exists():
        shutil.move(str(legacy_library), str(default_dir / "library.json"))

    # Build initial profiles.json from migrated auth_info
    profile_entry = {
        "id": "default",
        "name": "Default",
        "email": None,
        "created_at": time.time(),
        "authenticated_at": None,
        "last_validated": None,
    }

    migrated_auth = default_dir / "auth_info.json"
    if migrated_auth.exists():
        try:
            with open(migrated_auth, "r") as f:
                info = json.load(f)
            profile_entry["authenticated_at"] = info.get("authenticated_at")
        except Exception:
            pass

    registry = {"active_profile": "default", "profiles": [profile_entry]}
    with open(PROFILES_FILE, "w") as f:
        json.dump(registry, f, indent=2)

    print("  Migration complete → data/profiles/default/")


class ProfileManager:
    """Manages the profile registry and per-profile path resolution."""

    def __init__(self):
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        _migrate_legacy_layout()
        PROFILES_DIR.mkdir(parents=True, exist_ok=True)
        self._load()

    def _load(self):
        if PROFILES_FILE.exists():
            with open(PROFILES_FILE, "r") as f:
                data = json.load(f)
            self.active_profile: Optional[str] = data.get("active_profile")
            self.profiles: List[Dict[str, Any]] = data.get("profiles", [])
        else:
            self.active_profile = None
            self.profiles = []

    def _save(self):
        data = {"active_profile": self.active_profile, "profiles": self.profiles}
        with open(PROFILES_FILE, "w") as f:
            json.dump(data, f, indent=2)

    # ── Path resolution ───────────────────────────────────────────────────

    def get_profile_dir(self, profile_id: str) -> Path:
        return PROFILES_DIR / profile_id

    def get_paths(self, profile_id: str) -> Dict[str, Path]:
        base = self.get_profile_dir(profile_id)
        browser_state = base / "browser_state"
        return {
            "profile_dir": base,
            "browser_state_dir": browser_state,
            "browser_profile_dir": browser_state / "browser_profile",
            "state_file": browser_state / "state.json",
            "auth_info_file": base / "auth_info.json",
            "library_file": base / "library.json",
        }

    def get_active_paths(self) -> Dict[str, Path]:
        if not self.active_profile:
            raise RuntimeError("No active profile. Run: auth_manager.py setup --name <name>")
        return self.get_paths(self.active_profile)

    # ── CRUD ──────────────────────────────────────────────────────────────

    def _find(self, profile_id: str) -> Optional[Dict[str, Any]]:
        for p in self.profiles:
            if p["id"] == profile_id:
                return p
        return None

    def create_profile(self, name: str) -> Dict[str, Any]:
        profile_id = name.lower().replace(" ", "-").replace("_", "-")
        if self._find(profile_id):
            raise ValueError(f"Profile '{profile_id}' already exists")

        paths = self.get_paths(profile_id)
        paths["browser_state_dir"].mkdir(parents=True, exist_ok=True)

        entry = {
            "id": profile_id,
            "name": name,
            "email": None,
            "created_at": time.time(),
            "authenticated_at": None,
            "last_validated": None,
        }
        self.profiles.append(entry)

        if self.active_profile is None:
            self.active_profile = profile_id

        self._save()
        print(f"Created profile: {name} ({profile_id})")
        return entry

    def delete_profile(self, profile_id: str) -> bool:
        entry = self._find(profile_id)
        if not entry:
            print(f"Profile not found: {profile_id}")
            return False

        profile_dir = self.get_profile_dir(profile_id)
        if profile_dir.exists():
            shutil.rmtree(profile_dir)

        self.profiles = [p for p in self.profiles if p["id"] != profile_id]

        if self.active_profile == profile_id:
            self.active_profile = self.profiles[0]["id"] if self.profiles else None

        self._save()
        print(f"Deleted profile: {profile_id}")
        return True

    def set_active(self, profile_id: str):
        if not self._find(profile_id):
            raise ValueError(f"Profile not found: {profile_id}")
        self.active_profile = profile_id
        self._save()
        print(f"Active profile set to: {profile_id}")

    def update_profile(self, profile_id: str, **fields):
        entry = self._find(profile_id)
        if not entry:
            raise ValueError(f"Profile not found: {profile_id}")
        for k, v in fields.items():
            if k in entry:
                entry[k] = v
        self._save()

    # ── Status helpers ────────────────────────────────────────────────────

    @staticmethod
    def compute_status(profile: Dict[str, Any]) -> str:
        auth_at = profile.get("authenticated_at")
        if not auth_at:
            return "NOT_AUTHENTICATED"
        age_days = (time.time() - auth_at) / 86400
        if age_days > AUTH_EXPIRY_DAYS:
            return "EXPIRED"
        if age_days > AUTH_WARNING_DAYS:
            return "EXPIRING_SOON"
        return "VALID"

    def list_profiles(self) -> List[Dict[str, Any]]:
        result = []
        for p in self.profiles:
            info = dict(p)
            info["status"] = self.compute_status(p)
            info["is_active"] = p["id"] == self.active_profile

            auth_at = p.get("authenticated_at")
            if auth_at:
                age = (time.time() - auth_at) / 86400
                info["auth_age_days"] = round(age, 1)
                info["expires_in_days"] = round(AUTH_EXPIRY_DAYS - age, 1)
            else:
                info["auth_age_days"] = None
                info["expires_in_days"] = None

            result.append(info)
        return result

    def print_profiles(self):
        profiles = self.list_profiles()
        if not profiles:
            print("No profiles. Create one with: auth_manager.py setup --name <name>")
            return

        print(f"\nProfiles ({len(profiles)}):")
        for i, p in enumerate(profiles, 1):
            active_tag = " [ACTIVE]" if p["is_active"] else ""
            print(f"\n  {i}. {p['id']}{active_tag}")
            print(f"     Name: {p['name']}")
            if p.get("email"):
                print(f"     Email: {p['email']}")
            print(f"     Status: {p['status']}")
            if p["auth_age_days"] is not None:
                if p["expires_in_days"] > 0:
                    print(f"     Auth age: {p['auth_age_days']} days | Expires in: {p['expires_in_days']} days")
                else:
                    print(f"     Auth age: {p['auth_age_days']} days | EXPIRED {abs(p['expires_in_days'])} days ago")
            if p["status"] in ("EXPIRED", "NOT_AUTHENTICATED"):
                print(f"     Action: Run 'auth_manager.py reauth --profile {p['id']}'")


def main():
    """Command-line interface for profile management."""
    import argparse
    import sys

    parser = argparse.ArgumentParser(description='Manage NotebookLM profiles')
    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # list command
    subparsers.add_parser('list', help='List all profiles')

    # create command
    create_parser = subparsers.add_parser('create', help='Create a new profile')
    create_parser.add_argument('--name', required=True, help='Profile display name')

    # set-active command
    active_parser = subparsers.add_parser('set-active', help='Set active profile')
    active_parser.add_argument('--id', required=True, help='Profile ID')

    # delete command
    delete_parser = subparsers.add_parser('delete', help='Delete a profile')
    delete_parser.add_argument('--id', required=True, help='Profile ID')

    args = parser.parse_args()
    pm = ProfileManager()

    if args.command == 'list':
        pm.print_profiles()

    elif args.command == 'create':
        entry = pm.create_profile(args.name)
        print(f"Profile created: {entry['id']}")
        print(f"Next steps:")
        print(f"  1. Authenticate this profile:")
        print(f"     python scripts/run.py auth_manager.py setup --profile {entry['id']}")
        print(f"  2. Or set it as active and authenticate later:")
        print(f"     python scripts/run.py profile_manager.py set-active --id {entry['id']}")

    elif args.command == 'set-active':
        pm.set_active(args.id)
        profile = pm._find(args.id)
        if profile:
            print(f"Active profile: {profile['name']} ({args.id})")

    elif args.command == 'delete':
        if pm.delete_profile(args.id):
            print(f"Profile deleted: {args.id}")
        else:
            sys.exit(1)

    else:
        parser.print_help()


if __name__ == "__main__":
    main()
