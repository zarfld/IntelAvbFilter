#!/usr/bin/env python3
"""
Cleanup Manager for NotebookLM Skill
Manages cleanup of skill data and browser state
"""

import shutil
import argparse
from pathlib import Path
from typing import Dict, List, Any


class CleanupManager:
    """
    Manages cleanup of NotebookLM skill data.
    Profile-aware: can clean a specific profile or all profiles.
    """

    def __init__(self, profile_id=None):
        """Initialize the cleanup manager.

        Args:
            profile_id: Clean a specific profile. None = clean all data.
        """
        self.skill_dir = Path(__file__).parent.parent
        self.data_dir = self.skill_dir / "data"
        self.profile_id = profile_id

    def get_cleanup_paths(self, preserve_library: bool = False) -> Dict[str, Any]:
        """
        Get paths that would be cleaned up

        Args:
            preserve_library: Keep library.json if True

        Returns:
            Dict with paths and sizes

        Note: .venv is NEVER deleted - it's part of the skill infrastructure
        """
        paths = {
            'browser_state': [],
            'sessions': [],
            'library': [],
            'auth': [],
            'other': []
        }

        total_size = 0

        if self.profile_id:
            # Clean a specific profile
            profiles_dir = self.data_dir / "profiles" / self.profile_id
            if profiles_dir.exists():
                self._scan_profile_dir(profiles_dir, paths, preserve_library)
                total_size = sum(
                    item['size'] for items in paths.values() for item in items
                )
        elif self.data_dir.exists():
            # Clean all profiles
            profiles_dir = self.data_dir / "profiles"
            if profiles_dir.exists():
                for profile_dir in profiles_dir.iterdir():
                    if profile_dir.is_dir():
                        self._scan_profile_dir(profile_dir, paths, preserve_library)

            # Also scan legacy flat layout (pre-migration remnants)
            browser_state_dir = self.data_dir / "browser_state"
            if browser_state_dir.exists():
                for item in browser_state_dir.iterdir():
                    size = self._get_size(item)
                    paths['browser_state'].append({
                        'path': str(item),
                        'size': size,
                        'type': 'dir' if item.is_dir() else 'file'
                    })
                    total_size += size

            # Sessions
            sessions_file = self.data_dir / "sessions.json"
            if sessions_file.exists():
                size = sessions_file.stat().st_size
                paths['sessions'].append({
                    'path': str(sessions_file),
                    'size': size,
                    'type': 'file'
                })
                total_size += size

            # Library (unless preserved)
            if not preserve_library:
                library_file = self.data_dir / "library.json"
                if library_file.exists():
                    size = library_file.stat().st_size
                    paths['library'].append({
                        'path': str(library_file),
                        'size': size,
                        'type': 'file'
                    })
                    total_size += size

            # Auth info
            auth_info = self.data_dir / "auth_info.json"
            if auth_info.exists():
                size = auth_info.stat().st_size
                paths['auth'].append({
                    'path': str(auth_info),
                    'size': size,
                    'type': 'file'
                })
                total_size += size

            # Other files in data dir (but NEVER .venv!)
            for item in self.data_dir.iterdir():
                if item.name not in ['browser_state', 'sessions.json', 'library.json', 'auth_info.json']:
                    size = self._get_size(item)
                    paths['other'].append({
                        'path': str(item),
                        'size': size,
                        'type': 'dir' if item.is_dir() else 'file'
                    })
                    total_size += size

        return {
            'categories': paths,
            'total_size': total_size,
            'total_items': sum(len(items) for items in paths.values())
        }

    def _scan_profile_dir(self, profile_dir, paths, preserve_library):
        """Scan a single profile directory for cleanable items."""
        browser_state_dir = profile_dir / "browser_state"
        if browser_state_dir.exists():
            for item in browser_state_dir.iterdir():
                size = self._get_size(item)
                paths['browser_state'].append({
                    'path': str(item), 'size': size,
                    'type': 'dir' if item.is_dir() else 'file'
                })

        auth_info = profile_dir / "auth_info.json"
        if auth_info.exists():
            size = auth_info.stat().st_size
            paths['auth'].append({'path': str(auth_info), 'size': size, 'type': 'file'})

        if not preserve_library:
            library_file = profile_dir / "library.json"
            if library_file.exists():
                size = library_file.stat().st_size
                paths['library'].append({'path': str(library_file), 'size': size, 'type': 'file'})

    def _get_size(self, path: Path) -> int:
        """Get size of file or directory in bytes"""
        if path.is_file():
            return path.stat().st_size
        elif path.is_dir():
            total = 0
            try:
                for item in path.rglob('*'):
                    if item.is_file():
                        total += item.stat().st_size
            except Exception:
                pass
            return total
        return 0

    def _format_size(self, size: int) -> str:
        """Format size in human-readable form"""
        for unit in ['B', 'KB', 'MB', 'GB']:
            if size < 1024:
                return f"{size:.1f} {unit}"
            size /= 1024
        return f"{size:.1f} TB"

    def perform_cleanup(
        self,
        preserve_library: bool = False,
        dry_run: bool = False
    ) -> Dict[str, Any]:
        """
        Perform the actual cleanup

        Args:
            preserve_library: Keep library.json if True
            dry_run: Preview only, don't delete

        Returns:
            Dict with cleanup results
        """
        cleanup_data = self.get_cleanup_paths(preserve_library)
        deleted_items = []
        failed_items = []
        deleted_size = 0

        if dry_run:
            return {
                'dry_run': True,
                'would_delete': cleanup_data['total_items'],
                'would_free': cleanup_data['total_size']
            }

        # Perform deletion
        for category, items in cleanup_data['categories'].items():
            for item_info in items:
                path = Path(item_info['path'])
                try:
                    if path.exists():
                        if path.is_dir():
                            shutil.rmtree(path)
                        else:
                            path.unlink()
                        deleted_items.append(str(path))
                        deleted_size += item_info['size']
                        print(f"  Deleted: {path.name}")
                except Exception as e:
                    failed_items.append({
                        'path': str(path),
                        'error': str(e)
                    })
                    print(f"  Failed: {path.name} ({e})")

        # Recreate profile dirs if everything was deleted
        if not preserve_library and not failed_items and not self.profile_id:
            profiles_dir = self.data_dir / "profiles"
            profiles_dir.mkdir(parents=True, exist_ok=True)

        return {
            'deleted_items': deleted_items,
            'failed_items': failed_items,
            'deleted_size': deleted_size,
            'deleted_count': len(deleted_items),
            'failed_count': len(failed_items)
        }

    def print_cleanup_preview(self, preserve_library: bool = False):
        """Print a preview of what will be cleaned"""
        data = self.get_cleanup_paths(preserve_library)

        print("\nCleanup Preview")
        print("=" * 60)

        for category, items in data['categories'].items():
            if items:
                print(f"\n{category.replace('_', ' ').title()}:")
                for item in items:
                    path = Path(item['path'])
                    size_str = self._format_size(item['size'])
                    type_icon = "dir" if item['type'] == 'dir' else "file"
                    print(f"  [{type_icon}] {path.name:<30} {size_str:>10}")

        print("\n" + "=" * 60)
        print(f"Total items: {data['total_items']}")
        print(f"Total size: {self._format_size(data['total_size'])}")

        if preserve_library:
            print("\nLibrary will be preserved")

        print("\nThis preview shows what would be deleted.")
        print("Use --confirm to actually perform the cleanup.")


def main():
    """Command-line interface for cleanup management"""
    parser = argparse.ArgumentParser(
        description='Clean up NotebookLM skill data',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Preview what will be deleted
  python cleanup_manager.py

  # Perform cleanup (delete everything)
  python cleanup_manager.py --confirm

  # Cleanup but keep library
  python cleanup_manager.py --confirm --preserve-library

  # Force cleanup without preview
  python cleanup_manager.py --confirm --force
        """
    )

    parser.add_argument(
        '--profile',
        help='Clean only a specific profile (default: clean all data)'
    )

    parser.add_argument(
        '--confirm',
        action='store_true',
        help='Actually perform the cleanup (without this, only preview)'
    )

    parser.add_argument(
        '--preserve-library',
        action='store_true',
        help='Keep the notebook library (library.json)'
    )

    parser.add_argument(
        '--force',
        action='store_true',
        help='Skip confirmation prompt'
    )

    args = parser.parse_args()

    # Initialize manager
    manager = CleanupManager(profile_id=args.profile)

    if args.confirm:
        # Show preview first unless forced
        if not args.force:
            manager.print_cleanup_preview(args.preserve_library)

            print("\nWARNING: This will delete the files shown above!")
            print("   Note: .venv is preserved (part of skill infrastructure)")
            response = input("Are you sure? (yes/no): ")

            if response.lower() != 'yes':
                print("Cleanup cancelled.")
                return

        # Perform cleanup
        print("\nPerforming cleanup...")
        result = manager.perform_cleanup(args.preserve_library, dry_run=False)

        print(f"\nCleanup complete!")
        print(f"  Deleted: {result['deleted_count']} items")
        print(f"  Freed: {manager._format_size(result['deleted_size'])}")

        if result['failed_count'] > 0:
            print(f"  Warning: Failed: {result['failed_count']} items")

    else:
        # Just show preview
        manager.print_cleanup_preview(args.preserve_library)
        print("\nNote: Virtual environment (.venv) is never deleted")
        print("   It's part of the skill infrastructure, not user data")


if __name__ == "__main__":
    main()
