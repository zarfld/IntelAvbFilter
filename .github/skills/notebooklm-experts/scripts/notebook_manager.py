#!/usr/bin/env python3
"""
Notebook Library Management for NotebookLM
Manages a library of NotebookLM notebooks with metadata
Based on the MCP server implementation
"""

import csv
import io
import json
import sys
import argparse
import uuid
import os
import re
from urllib.parse import unquote
from pathlib import Path
from typing import Dict, List, Optional, Any, Tuple
from datetime import datetime


def _extract_id_from_url(url: str) -> Optional[str]:
    """Extract notebook UUID from NotebookLM URL"""
    match = re.search(r'/notebook/([a-f0-9-]+)', url)
    if match:
        return match.group(1)
    return None


def fetch_notebook_metadata(url: str, profile_id=None, headless: bool = True) -> Dict[str, Any]:
    """
    Navigate to a NotebookLM notebook and extract real metadata.

    Returns:
        Dict with 'title' (str or None) and 'sources' (list of source name strings)
    """
    from patchright.sync_api import sync_playwright
    from browser_utils import BrowserFactory
    from auth_manager import AuthManager
    from config import QUERY_INPUT_SELECTORS

    auth = AuthManager(profile_id=profile_id)

    if not auth.is_authenticated():
        print("Warning: Not authenticated. Cannot fetch metadata.")
        return {'title': None, 'sources': []}

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
        # Set viewport so full layout renders (sources panel visible)
        page.set_viewport_size({"width": 1440, "height": 900})

        print("  Fetching notebook metadata...")
        page.goto(url, wait_until="domcontentloaded")
        page.wait_for_timeout(5000)

        # Check accessibility
        is_ready = False
        for sel in QUERY_INPUT_SELECTORS:
            try:
                if page.is_visible(sel):
                    is_ready = True
                    break
            except Exception:
                continue

        if not is_ready:
            print("  Warning: Notebook may not be accessible")

        # Extract title from page title
        raw_title = page.title()
        title = None
        if raw_title:
            if " - NotebookLM" in raw_title:
                title = raw_title.rsplit(" - NotebookLM", 1)[0].strip()
            elif raw_title not in ("NotebookLM", ""):
                title = raw_title.strip()

        # Extract source names from the sources panel
        sources = []

        # Click "Expand source panel" button to load full source list
        expand_btns = [
            'button[aria-label="Mở rộng bảng điều khiển nguồn"]',
            'button[aria-label="Expand source panel"]',
            'button[aria-label*="Nút xem nguồn"]',
            'button[aria-label="Nút xem nguồn"]',
            'button[aria-label*="View sources"]',
        ]
        clicked_expand = False
        for btn_sel in expand_btns:
            try:
                if page.is_visible(btn_sel):
                    page.click(btn_sel)
                    page.wait_for_timeout(3000)
                    clicked_expand = True
                    print(f"  Clicked expand btn: {btn_sel}")
                    break
            except Exception:
                continue
        if not clicked_expand:
            print("  Could not find expand sources button")

        # Extract source names from aria-labels on source checkbox inputs
        # These are populated after the sources panel is expanded
        exclude_labels = {'Chọn tất cả các nguồn', 'Select all sources'}
        try:
            inputs = page.query_selector_all('source-picker input[aria-label]')
            for el in inputs:
                lbl = el.get_attribute('aria-label')
                if lbl and lbl not in exclude_labels and lbl not in sources:
                    sources.append(lbl)
        except Exception:
            pass

        # Fallback selectors if source-picker inputs didn't work
        if not sources:
            source_selectors = [
                'source-picker div[aria-label]',
            ]
            for sel in source_selectors:
                try:
                    elements = page.query_selector_all(sel)
                    if elements:
                        for el in elements:
                            lbl = el.get_attribute('aria-label')
                            if lbl and lbl not in exclude_labels and lbl not in sources:
                                sources.append(lbl)
                        if sources:
                            break
                except Exception:
                    continue

        print(f"  Title: {title or '(not found)'}")
        if sources:
            print(f"  Sources ({len(sources)}): {', '.join(sources[:5])}")
        else:
            print("  Sources: (none extracted)")

        return {'title': title, 'sources': sources}

    except Exception as e:
        print(f"  Error fetching metadata: {e}")
        return {'title': None, 'sources': []}

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


class NotebookLibrary:
    """Manages a collection of NotebookLM notebooks with metadata"""

    def __init__(self, profile_id: Optional[str] = None):
        """Initialize the notebook library.

        Args:
            profile_id: Profile to load library for. None = active profile.
        """
        from profile_manager import ProfileManager
        pm = ProfileManager()
        if profile_id:
            paths = pm.get_paths(profile_id)
        else:
            paths = pm.get_active_paths()
        self.data_dir = paths["profile_dir"]
        self.data_dir.mkdir(parents=True, exist_ok=True)

        self.library_file = paths["library_file"]
        self.notebooks: Dict[str, Dict[str, Any]] = {}
        self.active_notebook_id: Optional[str] = None

        # Load existing library
        self._load_library()

    def _load_library(self):
        """Load library from disk"""
        if self.library_file.exists():
            try:
                with open(self.library_file, 'r') as f:
                    data = json.load(f)
                    self.notebooks = data.get('notebooks', {})
                    self.active_notebook_id = data.get('active_notebook_id')
                    print(f"Loaded library with {len(self.notebooks)} notebooks")
            except Exception as e:
                print(f"Warning: Error loading library: {e}")
                self.notebooks = {}
                self.active_notebook_id = None
        else:
            self._save_library()

    def _save_library(self):
        """Save library to disk"""
        try:
            data = {
                'notebooks': self.notebooks,
                'active_notebook_id': self.active_notebook_id,
                'updated_at': datetime.now().isoformat()
            }
            with open(self.library_file, 'w') as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"Error saving library: {e}")

    def add_notebook(
        self,
        url: str,
        name: str,
        description: str,
        topics: List[str],
        content_types: Optional[List[str]] = None,
        use_cases: Optional[List[str]] = None,
        tags: Optional[List[str]] = None
    ) -> Dict[str, Any]:
        """
        Add a new notebook to the library

        Args:
            url: NotebookLM notebook URL
            name: Display name for the notebook
            description: What's in this notebook
            topics: Topics covered
            content_types: Types of content (optional)
            use_cases: When to use this notebook (optional)
            tags: Additional tags for organization (optional)

        Returns:
            The created notebook object
        """
        # Use UUID from URL as ID, fallback to slugified name
        notebook_id = _extract_id_from_url(url) or name.lower().replace(' ', '-').replace('_', '-')

        # Check for duplicates
        if notebook_id in self.notebooks:
            raise ValueError(f"Notebook with ID '{notebook_id}' already exists")

        # Create notebook object
        notebook = {
            'id': notebook_id,
            'url': url,
            'name': name,
            'description': description,
            'topics': topics,
            'content_types': content_types or [],
            'use_cases': use_cases or [],
            'tags': tags or [],
            'created_at': datetime.now().isoformat(),
            'updated_at': datetime.now().isoformat(),
            'use_count': 0,
            'last_used': None
        }

        # Add to library
        self.notebooks[notebook_id] = notebook

        # Set as active if it's the first notebook
        if len(self.notebooks) == 1:
            self.active_notebook_id = notebook_id

        self._save_library()

        print(f"Added notebook: {name} ({notebook_id})")
        return notebook

    def remove_notebook(self, notebook_id: str) -> bool:
        """
        Remove a notebook from the library

        Args:
            notebook_id: ID of notebook to remove

        Returns:
            True if removed, False if not found
        """
        if notebook_id in self.notebooks:
            del self.notebooks[notebook_id]

            # Clear active if it was removed
            if self.active_notebook_id == notebook_id:
                self.active_notebook_id = None
                # Set new active if there are other notebooks
                if self.notebooks:
                    self.active_notebook_id = list(self.notebooks.keys())[0]

            self._save_library()
            print(f"Removed notebook: {notebook_id}")
            return True

        print(f"Warning: Notebook not found: {notebook_id}")
        return False

    def update_notebook(
        self,
        notebook_id: str,
        name: Optional[str] = None,
        description: Optional[str] = None,
        topics: Optional[List[str]] = None,
        content_types: Optional[List[str]] = None,
        use_cases: Optional[List[str]] = None,
        tags: Optional[List[str]] = None,
        url: Optional[str] = None
    ) -> Dict[str, Any]:
        """
        Update notebook metadata

        Args:
            notebook_id: ID of notebook to update
            Other args: Fields to update (None = keep existing)

        Returns:
            Updated notebook object
        """
        if notebook_id not in self.notebooks:
            raise ValueError(f"Notebook not found: {notebook_id}")

        notebook = self.notebooks[notebook_id]

        # Update fields if provided
        if name is not None:
            notebook['name'] = name
        if description is not None:
            notebook['description'] = description
        if topics is not None:
            notebook['topics'] = topics
        if content_types is not None:
            notebook['content_types'] = content_types
        if use_cases is not None:
            notebook['use_cases'] = use_cases
        if tags is not None:
            notebook['tags'] = tags
        if url is not None:
            notebook['url'] = url

        notebook['updated_at'] = datetime.now().isoformat()

        self._save_library()
        print(f"Updated notebook: {notebook['name']}")
        return notebook

    def get_notebook(self, notebook_id: str) -> Optional[Dict[str, Any]]:
        """Get a specific notebook by ID"""
        return self.notebooks.get(notebook_id)

    def list_notebooks(self) -> List[Dict[str, Any]]:
        """List all notebooks in the library"""
        return list(self.notebooks.values())

    def search_notebooks(self, query: str) -> List[Dict[str, Any]]:
        """
        Search notebooks by query

        Args:
            query: Search query (searches name, description, topics, tags)

        Returns:
            List of matching notebooks
        """
        query_lower = query.lower()
        results = []

        for notebook in self.notebooks.values():
            # Search in various fields
            searchable = [
                notebook['name'].lower(),
                notebook['description'].lower(),
                ' '.join(notebook['topics']).lower(),
                ' '.join(notebook['tags']).lower(),
                ' '.join(notebook.get('use_cases', [])).lower()
            ]

            if any(query_lower in field for field in searchable):
                results.append(notebook)

        return results

    def select_notebook(self, notebook_id: str) -> Dict[str, Any]:
        """
        Set a notebook as active

        Args:
            notebook_id: ID of notebook to activate

        Returns:
            The activated notebook
        """
        if notebook_id not in self.notebooks:
            raise ValueError(f"Notebook not found: {notebook_id}")

        self.active_notebook_id = notebook_id
        self._save_library()

        notebook = self.notebooks[notebook_id]
        print(f"Activated notebook: {notebook['name']}")
        return notebook

    def get_active_notebook(self) -> Optional[Dict[str, Any]]:
        """Get the currently active notebook"""
        if self.active_notebook_id:
            return self.notebooks.get(self.active_notebook_id)
        return None

    def resolve_notebook_id(
        self,
        notebook_id: Optional[str] = None,
        notebook_url: Optional[str] = None,
    ) -> Optional[str]:
        """Resolve a notebook ID from an explicit ID or URL."""
        if notebook_id and notebook_id in self.notebooks:
            return notebook_id

        if notebook_url:
            # Fast path: NotebookLM URL usually embeds the notebook UUID.
            extracted = _extract_id_from_url(notebook_url)
            if extracted and extracted in self.notebooks:
                return extracted

            # Fallback for non-standard URLs or imported entries.
            for candidate_id, notebook in self.notebooks.items():
                if notebook.get('url') == notebook_url:
                    return candidate_id

        return None

    def refresh_notebook_name(
        self,
        notebook_url: str,
        detected_title: Optional[str],
    ) -> bool:
        """Refresh stored notebook name from the current page title."""
        if not detected_title:
            return False

        notebook_id = self.resolve_notebook_id(notebook_url=notebook_url)
        if not notebook_id:
            return False

        notebook = self.notebooks[notebook_id]
        new_name = detected_title.strip()
        if not new_name or new_name == notebook.get('name'):
            return False

        old_name = notebook.get('name', '')
        notebook['name'] = new_name
        notebook['updated_at'] = datetime.now().isoformat()
        self._save_library()
        print(f"  Refreshed notebook name: '{old_name}' -> '{new_name}'")
        return True

    def increment_use_count(self, notebook_id: str) -> Dict[str, Any]:
        """
        Increment usage counter for a notebook

        Args:
            notebook_id: ID of notebook that was used

        Returns:
            Updated notebook
        """
        if notebook_id not in self.notebooks:
            raise ValueError(f"Notebook not found: {notebook_id}")

        notebook = self.notebooks[notebook_id]
        notebook['use_count'] += 1
        notebook['last_used'] = datetime.now().isoformat()

        self._save_library()
        return notebook

    def get_stats(self) -> Dict[str, Any]:
        """Get library statistics"""
        total_notebooks = len(self.notebooks)
        total_topics = set()
        total_use_count = 0

        for notebook in self.notebooks.values():
            total_topics.update(notebook['topics'])
            total_use_count += notebook['use_count']

        # Find most used
        most_used = None
        if self.notebooks:
            most_used = max(
                self.notebooks.values(),
                key=lambda n: n['use_count']
            )

        return {
            'total_notebooks': total_notebooks,
            'total_topics': len(total_topics),
            'total_use_count': total_use_count,
            'active_notebook': self.get_active_notebook(),
            'most_used_notebook': most_used,
            'library_path': str(self.library_file)
        }

    # ------------------------------------------------------------------ #
    # M1 — Export                                                          #
    # ------------------------------------------------------------------ #

    def export_notebooks(
        self,
        format: str = 'json',
        output_path: Optional[str] = None,
    ) -> Path:
        """Export all notebooks to a file.

        Args:
            format: Output format — 'json' or 'csv'
            output_path: Destination file path. If None, writes to
                         data/profiles/{profile}/exports/notebooks_export_DATE.{ext}

        Returns:
            Path to the written file
        """
        notebooks = list(self.notebooks.values())
        metadata = {
            'active_notebook_id': self.active_notebook_id,
            'total_notebooks': len(notebooks),
            'total_use_count': sum(n.get('use_count', 0) for n in notebooks),
        }

        if format == 'json':
            content = self._export_json(notebooks, metadata)
            ext = 'json'
        elif format == 'csv':
            content = self._export_csv(notebooks)
            ext = 'csv'
        else:
            raise ValueError(f"Unsupported format: {format!r}. Use 'json' or 'csv'")

        if output_path:
            dest = Path(output_path)
        else:
            exports_dir = self.data_dir / 'exports'
            exports_dir.mkdir(parents=True, exist_ok=True)
            date_str = datetime.now().strftime('%Y-%m-%d')
            dest = exports_dir / f'notebooks_export_{date_str}.{ext}'

        dest.parent.mkdir(parents=True, exist_ok=True)
        with open(dest, 'w', encoding='utf-8', newline='') as f:
            f.write(content)

        print(f"Exported {len(notebooks)} notebooks to {dest}")
        return dest

    def _export_json(self, notebooks: List[Dict], metadata: Dict) -> str:
        """Format notebooks as a JSON export payload"""
        payload = {
            'export_version': '1.0',
            'exported_at': datetime.now().isoformat(),
            'exported_by': 'notebooklm-experts/1.0',
            'notebooks': notebooks,
            'metadata': metadata,
        }
        return json.dumps(payload, indent=2, ensure_ascii=False)

    def _export_csv(self, notebooks: List[Dict]) -> str:
        """Format notebooks as CSV with semicolon-delimited list fields"""
        output = io.StringIO()
        fieldnames = [
            'id', 'url', 'name', 'description', 'topics', 'tags',
            'content_types', 'use_cases', 'created_at', 'updated_at',
            'use_count', 'last_used',
        ]
        writer = csv.DictWriter(output, fieldnames=fieldnames, extrasaction='ignore')
        writer.writeheader()
        for nb in notebooks:
            row = dict(nb)
            for field in ('topics', 'tags', 'content_types', 'use_cases'):
                row[field] = ';'.join(nb.get(field) or [])
            writer.writerow(row)
        return output.getvalue()

    # ------------------------------------------------------------------ #
    # M2 — Import                                                          #
    # ------------------------------------------------------------------ #

    def import_notebooks(
        self,
        file_path: str,
        strategy: str = 'merge',
    ) -> Dict[str, Any]:
        """Import notebooks from a JSON or CSV file.

        Args:
            file_path: Path to the import file (.json or .csv)
            strategy: 'merge' = skip existing IDs; 'overwrite' = replace existing

        Returns:
            Dict with keys: imported, skipped, errors, notebooks
        """
        path = Path(file_path)
        if not path.exists():
            raise FileNotFoundError(f"Import file not found: {path}")

        fmt = self._detect_format(path)

        if fmt == 'json':
            with open(path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            if isinstance(data, dict) and 'notebooks' in data:
                raw = data['notebooks']
            elif isinstance(data, list):
                raw = data
            else:
                raise ValueError("JSON must contain an object with 'notebooks' key or a root list")
        elif fmt == 'csv':
            raw = []
            with open(path, 'r', encoding='utf-8', newline='') as f:
                for row in csv.DictReader(f):
                    for field in ('topics', 'tags', 'content_types', 'use_cases'):
                        if field in row:
                            row[field] = [v for v in row[field].split(';') if v]
                    raw.append(dict(row))
        else:
            raise ValueError(f"Unsupported import format: {fmt!r}")

        valid, errors = self._validate_import_notebooks(raw)

        result: Dict[str, Any] = {
            'imported': 0,
            'skipped': 0,
            'errors': errors,
            'notebooks': [],
        }

        for nb in valid:
            nb_id = nb['id']
            if nb_id in self.notebooks:
                if strategy == 'overwrite':
                    nb['updated_at'] = datetime.now().isoformat()
                    self.notebooks[nb_id] = nb
                    result['notebooks'].append({'id': nb_id, 'name': nb.get('name', ''), 'status': 'overwritten'})
                    result['imported'] += 1
                else:  # merge: skip duplicates
                    result['notebooks'].append({'id': nb_id, 'name': nb.get('name', ''), 'status': 'skipped', 'reason': 'already exists'})
                    result['skipped'] += 1
            else:
                nb.setdefault('created_at', datetime.now().isoformat())
                nb.setdefault('updated_at', datetime.now().isoformat())
                nb.setdefault('use_count', 0)
                nb.setdefault('last_used', None)
                self.notebooks[nb_id] = nb
                result['notebooks'].append({'id': nb_id, 'name': nb.get('name', ''), 'status': 'imported'})
                result['imported'] += 1

        if result['imported']:
            self._save_library()

        print(f"Import complete: {result['imported']} imported, {result['skipped']} skipped, {len(errors)} errors")
        return result

    def _detect_format(self, file_path: Path) -> str:
        """Detect file format from extension"""
        suffix = file_path.suffix.lower()
        if suffix == '.json':
            return 'json'
        if suffix == '.csv':
            return 'csv'
        raise ValueError(f"Cannot detect format from extension: {file_path.suffix!r}. Use .json or .csv")

    def _validate_import_notebooks(self, notebooks: List[Dict]) -> Tuple[List[Dict], List[str]]:
        """Validate notebook dicts for required fields.

        Returns:
            Tuple of (valid_list, error_strings)
        """
        valid = []
        errors = []
        seen_ids: set = set()

        for i, nb in enumerate(notebooks):
            nb_errors = []
            for field in ('id', 'url', 'name'):
                if not nb.get(field):
                    nb_errors.append(f"missing required field '{field}'")
            nb_id = nb.get('id')
            if nb_id:
                if nb_id in seen_ids:
                    nb_errors.append(f"duplicate id '{nb_id}' in import file")
                else:
                    seen_ids.add(nb_id)
            if nb_errors:
                errors.append(f"Notebook #{i + 1}: {'; '.join(nb_errors)}")
            else:
                valid.append(nb)

        return valid, errors


def main():
    """Command-line interface for notebook management"""
    parser = argparse.ArgumentParser(description='Manage NotebookLM library')
    parser.add_argument('--profile', help='Profile to use (default: active profile)')

    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Add command
    add_parser = subparsers.add_parser('add', help='Add a notebook')
    add_parser.add_argument('--url', required=True, help='NotebookLM URL')
    add_parser.add_argument('--name', help='Display name (auto-detected if omitted)')
    add_parser.add_argument('--description', help='Description (auto-generated if omitted)')
    add_parser.add_argument('--topics', help='Comma-separated topics (auto-detected from sources if omitted)')
    add_parser.add_argument('--use-cases', help='Comma-separated use cases')
    add_parser.add_argument('--tags', help='Comma-separated tags')
    add_parser.add_argument('--no-fetch', action='store_true', help='Skip auto-fetching metadata from browser')

    # List command
    subparsers.add_parser('list', help='List all notebooks')

    # Search command
    search_parser = subparsers.add_parser('search', help='Search notebooks')
    search_parser.add_argument('--query', required=True, help='Search query')

    # Activate command
    activate_parser = subparsers.add_parser('activate', help='Set active notebook')
    activate_parser.add_argument('--id', required=True, help='Notebook ID')

    # Remove command
    remove_parser = subparsers.add_parser('remove', help='Remove a notebook')
    remove_parser.add_argument('--id', required=True, help='Notebook ID')

    # Stats command
    subparsers.add_parser('stats', help='Show library statistics')

    # Export command
    export_parser = subparsers.add_parser('export', help='Export notebooks to file')
    export_parser.add_argument('--format', choices=['json', 'csv'], default='json',
                               help='Output format (default: json)')
    export_parser.add_argument('--output', help='Output file path (default: auto-generated in exports/)')

    # Import command
    import_parser = subparsers.add_parser('import', help='Import notebooks from file')
    import_parser.add_argument('--file', required=True, help='Path to import file (.json or .csv)')
    import_parser.add_argument('--strategy', choices=['merge', 'overwrite'], default='merge',
                               help='Conflict strategy: merge=skip existing, overwrite=replace (default: merge)')

    # Add-source command
    add_source_parser = subparsers.add_parser('add-source', help='Add a web URL source to a notebook')
    add_source_parser.add_argument('--notebook-url', required=True, help='NotebookLM notebook URL')
    add_source_parser.add_argument('--source-url', required=True, help='Web URL or YouTube URL to add')
    add_source_parser.add_argument('--no-headless', action='store_true', help='Show browser window')

    args = parser.parse_args()

    # Initialize library
    library = NotebookLibrary(profile_id=getattr(args, 'profile', None))

    # Execute command
    if args.command == 'add':
        name = args.name
        description = args.description
        topics_str = args.topics
        # sources_list holds auto-detected sources; avoids comma-split corruption
        sources_list = None

        # Auto-fetch metadata from notebook page when fields are missing
        if not args.no_fetch and (not name or not description or not topics_str):
            print("Auto-detecting metadata from notebook page...")
            meta = fetch_notebook_metadata(
                args.url,
                profile_id=getattr(args, 'profile', None),
            )

            if not name and meta.get('title'):
                name = meta['title']
                print(f"  Using detected name: {name}")

            if not topics_str and meta.get('sources'):
                # Decode percent-encoded filenames (e.g. URL-encoded Vietnamese text)
                sources_list = [unquote(s) for s in meta['sources']]
                print(f"  Using detected topics from sources: {', '.join(sources_list)}")

            if not description:
                src = sources_list or meta.get('sources', [])
                if src:
                    description = f"Notebook with {len(src)} sources: {', '.join(src[:5])}"
                elif name:
                    description = name
                print(f"  Using generated description: {description}")

        # Final fallback: extract ID from URL as name if still missing
        if not name:
            name = _extract_id_from_url(args.url) or 'unnamed-notebook'
            print(f"  Warning: Could not detect name, using: {name}")
        if not description:
            description = name

        # Build topics list: prefer sources_list (preserves commas in names),
        # fall back to comma-split of user-provided topics_str
        if sources_list is not None:
            topics = sources_list
        elif topics_str:
            topics = [t.strip() for t in topics_str.split(',')]
        else:
            topics = []
        use_cases = [u.strip() for u in args.use_cases.split(',')] if args.use_cases else None
        tags = [t.strip() for t in args.tags.split(',')] if args.tags else None

        notebook = library.add_notebook(
            url=args.url,
            name=name,
            description=description,
            topics=topics,
            use_cases=use_cases,
            tags=tags
        )
        print(json.dumps(notebook, indent=2))

    elif args.command == 'list':
        notebooks = library.list_notebooks()
        if notebooks:
            print("\nNotebook Library:")
            for notebook in notebooks:
                active = " [ACTIVE]" if notebook['id'] == library.active_notebook_id else ""
                print(f"\n  {notebook['name']}{active}")
                print(f"     ID: {notebook['id']}")
                print(f"     Topics: {', '.join(notebook['topics'])}")
                print(f"     Uses: {notebook['use_count']}")
        else:
            print("Library is empty. Add notebooks with: notebook_manager.py add")

    elif args.command == 'search':
        results = library.search_notebooks(args.query)
        if results:
            print(f"\nFound {len(results)} notebooks:")
            for notebook in results:
                print(f"\n  {notebook['name']} ({notebook['id']})")
                print(f"     {notebook['description']}")
        else:
            print(f"No notebooks found for: {args.query}")

    elif args.command == 'activate':
        notebook = library.select_notebook(args.id)
        print(f"Now using: {notebook['name']}")

    elif args.command == 'remove':
        if library.remove_notebook(args.id):
            print("Notebook removed from library")

    elif args.command == 'stats':
        stats = library.get_stats()
        print("\nLibrary Statistics:")
        print(f"  Total notebooks: {stats['total_notebooks']}")
        print(f"  Total topics: {stats['total_topics']}")
        print(f"  Total uses: {stats['total_use_count']}")
        if stats['active_notebook']:
            print(f"  Active: {stats['active_notebook']['name']}")
        if stats['most_used_notebook']:
            print(f"  Most used: {stats['most_used_notebook']['name']} ({stats['most_used_notebook']['use_count']} uses)")
        print(f"  Library path: {stats['library_path']}")

    elif args.command == 'export':
        dest = library.export_notebooks(
            format=args.format,
            output_path=getattr(args, 'output', None),
        )
        print(f"Export saved to: {dest}")

    elif args.command == 'import':
        result = library.import_notebooks(
            file_path=args.file,
            strategy=args.strategy,
        )
        print(json.dumps(result, indent=2))

    elif args.command == 'add-source':
        from browser_utils import add_source_web
        success = add_source_web(
            notebook_url=args.notebook_url,
            source_url=args.source_url,
            profile_id=getattr(args, 'profile', None),
            headless=not args.no_headless,
        )
        if success:
            print("Source added successfully")
        else:
            print("Failed to add source")
            sys.exit(1)

    else:
        parser.print_help()


if __name__ == "__main__":
    main()
