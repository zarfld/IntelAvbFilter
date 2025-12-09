#!/usr/bin/env python3
"""Spec Parser

Parses requirements and architecture spec markdown files to extract structured
metadata used for generation (tests, trace JSON) and validation.

Input Directories:
  - 02-requirements/** (functional, non-functional, use-cases, user-stories)
  - 03-architecture/** (architecture spec, decisions, views, quality scenarios)

Output:
  - build/spec-index.json : canonical list of items with IDs, type, title, refs

Traceability Model (simplified):
  Stakeholder Need (StR) -> Requirement (REQ-*) -> Design Element (ARC-*, COMP-* )
     -> Test (TEST-*)

Assumptions:
  - Each spec file has YAML front matter with 'id' or lines containing an ID
  - IDs follow taxonomy documented in templates (e.g., REQ-F-001)

"""
from __future__ import annotations
import json, re, sys, hashlib
from pathlib import Path
from typing import Dict, List, Any

ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / 'build'
OUTPUT_FILE = BUILD_DIR / 'spec-index.json'

ID_PATTERN = re.compile(r'^(?P<id>(StR|REQ|ARC|ADR|QA|TEST)-(?:[A-Z]{4}-)?[A-Z0-9][A-Z0-9\-]*)\b')
# Capture full identifiers with optional 4-char category prefix
# Examples: REQ-AUTH-F-001, StR-CORE-001, ADR-INFRA-001, TEST-LOGIN-001

# Pattern to detect markdown heading definitions (## REQ-F-001: Title)
# Enhancement: 2025-12-02 - Distinguish canonical definitions from references
HEADING_DEF_PATTERN = re.compile(
    r'^#+\s+((?:StR|REQ|ARC|ADR|QA|TEST)-(?:[A-Z]{4}-)?[A-Z0-9][A-Z0-9\-]*)',
    re.MULTILINE
)

REF_PATTERN = re.compile(r'\b(?:StR|REQ|ARC|ADR|QA|TEST)-(?:[A-Z]{4}-)?[A-Z0-9][A-Z0-9\-]*\b')

SCAN_DIRS = [
    ROOT / '02-requirements',
    ROOT / '03-architecture',
]

# Additional directories (code/tests) where TEST-* identifiers and inline requirement references
# may appear. We parse these more leniently (no front matter expected) to enrich traceability.
CODE_TEST_DIRS = [
    ROOT / '05-implementation' / 'tests',
    ROOT / 'tests',  # Support root-level test directories
]

FRONT_MATTER_RE = re.compile(r'^---\n(.*?)\n---\n', re.DOTALL)

# Files / paths to ignore (instructional, templates, meta guidance) – we do not want
# placeholder IDs here (e.g. REQ-F-XXX) polluting traceability metrics.
# Enhancement 2025-12-02: Added spec-kit-templates/ and /templates/ to exclude template examples
IGNORE_PATTERNS = [
    '.github/copilot-instructions.md',
    'ADR-template.md',
    'user-story-template.md',
    'architecture-spec.md',  # template root
    'requirements-spec.md',  # template root
    'spec-kit-templates/',   # Exclude all template examples
    '/templates/',           # Exclude any templates folders
]

def is_ignored(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    # quick contains match for any ignore fragment
    return any(p in rel for p in IGNORE_PATTERNS)


def extract_front_matter(text: str) -> Dict[str, Any]:
    m = FRONT_MATTER_RE.match(text)
    if not m:
        return {}
    block = m.group(1)
    data: Dict[str, Any] = {}
    for line in block.splitlines():
        if ':' in line:
            k, v = line.split(':', 1)
            data[k.strip()] = v.strip()
    return data


def parse_file(path: Path) -> List[Dict[str, Any]]:
    """Parse a markdown file and extract all spec IDs with source type classification.
    
    Enhancement 2025-12-02: Distinguish definitions (canonical) from references (citations)
    - Definitions: YAML front matter 'id:', markdown headings (## REQ-F-001)
    - References: All other occurrences (tables, traceability sections, inline mentions)
    """
    text = path.read_text(encoding='utf-8', errors='ignore')
    fm = extract_front_matter(text)
    items: List[Dict[str, Any]] = []
    definitions_found = set()  # Track canonical definitions
    
    # Extract from front matter 'id:' field (canonical definition)
    primary_id = fm.get('id')
    if primary_id:
        items.append(build_item(primary_id, fm.get('title') or path.stem, path, text, 'definition'))
        definitions_found.add(primary_id)
    
    # Extract from markdown headings (## REQ-F-001: canonical definitions)
    for match in HEADING_DEF_PATTERN.finditer(text):
        id_ = match.group(1)
        if id_ not in definitions_found:
            # Extract heading title
            line_start = text.rfind('\n', 0, match.start()) + 1
            line_end = text.find('\n', match.end())
            line = text[line_start:line_end] if line_end != -1 else text[line_start:]
            remainder = line.split(id_, 1)[1].strip(' -:,') if id_ in line else path.stem
            title = remainder or path.stem
            items.append(build_item(id_, title, path, text, 'definition'))
            definitions_found.add(id_)
    
    # Scan body for additional ID references (not already classified as definitions)
    for raw_line in text.splitlines():
        line = raw_line.strip('# ').strip()
        m = ID_PATTERN.match(line)
        if not m:
            continue
        # Extract all valid IDs present in the line (comma separated etc.)
        ids_in_line = [tok for tok in REF_PATTERN.findall(line)]
        for idx, id_ in enumerate(ids_in_line):
            if id_ in definitions_found:
                continue  # Skip: already captured as definition
            if any(i['id'] == id_ for i in items):
                continue
            # Title: remainder of line after this id if first, else just path stem
            if idx == 0:
                remainder = line.split(id_, 1)[1].strip(' -:,')
                title = remainder or path.stem
            else:
                title = path.stem
            items.append(build_item(id_, title, path, text, 'reference'))
    return items


def build_item(id_: str, title: str, path: Path, full_text: str, source_type: str = 'reference') -> Dict[str, Any]:
    """Build spec item with source type classification.
    
    Args:
        source_type: 'definition' (canonical) or 'reference' (citation)
    
    Enhancement: 2025-12-02 - Distinguish definitions from references
    """
    refs = sorted({r for r in REF_PATTERN.findall(full_text) if r != id_})
    sha = hashlib.sha1(full_text.encode('utf-8')).hexdigest()[:8]
    return {
        'id': id_,
        'title': title,
        'path': str(path.relative_to(ROOT)),
        'sourceType': source_type,
        'references': refs,
        'hash': sha,
    }


def main() -> int:
    BUILD_DIR.mkdir(exist_ok=True)
    all_items: List[Dict[str, Any]] = []
    for base in SCAN_DIRS:
        if not base.exists():
            continue
        for path in base.rglob('*.md'):
            if path.name.startswith('README'):
                continue
            if is_ignored(path):
                continue
            try:
                all_items.extend(parse_file(path))
            except Exception as e:
                print(f"WARN: failed to parse {path}: {e}", file=sys.stderr)

    # Parse test source files for TEST-* identifiers and requirement references.
    test_id_pattern = re.compile(r'\b(TEST-[A-Z0-9\-]+)\b')
    req_ref_pattern = re.compile(r'\b(REQ-[A-Z0-9\-]+)\b')
    for tdir in CODE_TEST_DIRS:
        if not tdir.exists():
            continue
        for ext in ('*.cpp','*.cc','*.c','*.hpp','*.h','*.py'):
            for src in tdir.rglob(ext):
                try:
                    text = src.read_text(encoding='utf-8', errors='ignore')
                except Exception as e:
                    print(f"WARN: failed to read test file {src}: {e}", file=sys.stderr)
                    continue
                test_ids = sorted(set(test_id_pattern.findall(text)))
                if not test_ids:
                    continue
                req_refs = sorted({r for r in req_ref_pattern.findall(text)})
                for tid in test_ids:
                    all_items.append({
                        'id': tid,
                        'title': src.stem,
                        'path': str(src.relative_to(ROOT)),
                        'sourceType': 'definition',  # Test IDs in source code are definitions
                        'references': req_refs,
                        'hash': hashlib.sha1((tid+text).encode('utf-8')).hexdigest()[:8],
                    })
    # De-duplicate by ID keeping first occurrence
    # Enhancement 2025-12-02: Only flag multiple *definitions* as duplicates (references are expected)
    seen = {}
    dedup: List[Dict[str, Any]] = []
    duplicate_definition_ids: Dict[str, int] = {}
    
    for item in all_items:
        iid = item['id']
        source_type = item.get('sourceType', 'reference')
        
        if iid in seen:
            # Only flag if both occurrences are definitions
            if source_type == 'definition' and seen[iid] == 'definition':
                duplicate_definition_ids[iid] = duplicate_definition_ids.get(iid, 1) + 1
            continue
        seen[iid] = source_type
        dedup.append(item)
    
    if duplicate_definition_ids:
        print("⚠️ Duplicate definition(s) detected (keeping first):", file=sys.stderr)
        for k, count in duplicate_definition_ids.items():
            print(f"  - {k} (definitions: {count+1})", file=sys.stderr)
    
    OUTPUT_FILE.write_text(
        json.dumps({
            'items': dedup,
            'duplicateDefinitionIds': list(duplicate_definition_ids.keys()),
            'ignoredPatterns': IGNORE_PATTERNS
        }, indent=2),
        encoding='utf-8'
    )
    print(f"Wrote {OUTPUT_FILE} with {len(dedup)} unique items (duplicate definitions: {len(duplicate_definition_ids)})")
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
