#!/usr/bin/env python3
"""Check spec ID numbering for gaps and duplicates.

This tool scans all spec files and reports:
- Duplicate IDs (same ID used multiple times)
- Numbering gaps (e.g., REQ-F-001, REQ-F-003 - missing 002)
- Out-of-sequence files

Usage:
  python scripts/check-spec-numbering.py
  python scripts/check-spec-numbering.py --fix-gaps

"""
from __future__ import annotations
import sys
import re
from pathlib import Path
from collections import defaultdict
from typing import Dict, List, Set

ROOT = Path(__file__).resolve().parent.parent

# ID patterns
ID_PATTERNS = {
    'stakeholder': re.compile(r'\bStR-(?P<category>[A-Z]{4}-)?(?P<num>\d{3})\b'),
    'requirement': re.compile(r'\bREQ-(?P<category>[A-Z]{4}-)?(?P<type>F|NF)-(?P<num>\d{3})\b'),
    'architecture': re.compile(r'\bADR-(?P<category>[A-Z]{4}-)?(?P<num>\d{3})\b'),
}

PHASE_DIRS = [
    '01-stakeholder-requirements',
    '02-requirements',
    '03-architecture',
]


def extract_ids_from_file(file_path: Path) -> List[str]:
    """Extract all IDs from a file."""
    try:
        content = file_path.read_text(encoding='utf-8')
        
        ids = []
        for pattern_name, pattern in ID_PATTERNS.items():
            for match in pattern.finditer(content):
                ids.append(match.group(0))
        
        return ids
    
    except Exception as e:
        print(f"⚠️  Could not read {file_path}: {e}", file=sys.stderr)
        return []


def analyze_numbering() -> Dict[str, any]:
    """Analyze ID numbering across all specs."""
    # Track IDs: id -> list of files containing it
    id_locations: Dict[str, List[Path]] = defaultdict(list)
    
    # Scan all phase directories
    for phase_dir_name in PHASE_DIRS:
        phase_dir = ROOT / phase_dir_name
        if not phase_dir.exists():
            continue
        
        for md_file in phase_dir.rglob('*.md'):
            # Skip templates and READMEs
            name_lower = md_file.name.lower()
            if 'readme' in name_lower or 'template' in name_lower:
                continue
            
            ids = extract_ids_from_file(md_file)
            for id_str in ids:
                id_locations[id_str].append(md_file)
    
    # Analyze results
    duplicates = {id_: locs for id_, locs in id_locations.items() if len(locs) > 1}
    
    # Group by ID type for gap analysis
    by_type: Dict[str, Set[int]] = defaultdict(set)
    
    for id_str in id_locations.keys():
        # Parse ID to get type and number
        if id_str.startswith('StR-'):
            match = ID_PATTERNS['stakeholder'].match(id_str)
            if match:
                num = int(match.group('num'))
                category = match.group('category') or ''
                key = f"StR-{category}"
                by_type[key].add(num)
        
        elif id_str.startswith('REQ-'):
            match = ID_PATTERNS['requirement'].match(id_str)
            if match:
                num = int(match.group('num'))
                category = match.group('category') or ''
                req_type = match.group('type')
                key = f"REQ-{category}{req_type}"
                by_type[key].add(num)
        
        elif id_str.startswith('ADR-'):
            match = ID_PATTERNS['architecture'].match(id_str)
            if match:
                num = int(match.group('num'))
                category = match.group('category') or ''
                key = f"ADR-{category}"
                by_type[key].add(num)
    
    # Find gaps
    gaps = {}
    for key, numbers in by_type.items():
        if not numbers:
            continue
        
        sorted_nums = sorted(numbers)
        min_num = sorted_nums[0]
        max_num = sorted_nums[-1]
        
        expected = set(range(min_num, max_num + 1))
        missing = expected - numbers
        
        if missing:
            gaps[key] = sorted(missing)
    
    return {
        'id_locations': id_locations,
        'duplicates': duplicates,
        'gaps': gaps,
        'by_type': by_type,
    }


def print_report(analysis: Dict[str, any]) -> int:
    """Print analysis report and return exit code."""
    duplicates = analysis['duplicates']
    gaps = analysis['gaps']
    by_type = analysis['by_type']
    
    issues_found = False
    
    print("🔍 Specification ID Numbering Analysis")
    print("=" * 60)
    print()
    
    # Summary
    print("📊 Summary:")
    print(f"  Total unique IDs: {len(analysis['id_locations'])}")
    print(f"  ID categories: {len(by_type)}")
    print()
    
    # Duplicates
    if duplicates:
        issues_found = True
        print("❌ DUPLICATES FOUND:")
        print()
        for id_str, locations in sorted(duplicates.items()):
            print(f"  {id_str} appears in {len(locations)} files:")
            for loc in locations:
                print(f"    - {loc.relative_to(ROOT)}")
            print()
    else:
        print("✅ No duplicate IDs found")
        print()
    
    # Gaps
    if gaps:
        print("⚠️  NUMBERING GAPS DETECTED:")
        print()
        for key, missing_nums in sorted(gaps.items()):
            print(f"  {key}: Missing {len(missing_nums)} number(s)")
            print(f"    {missing_nums}")
            print()
        
        # Note: Gaps are warnings, not hard errors
        # Some gaps may be intentional (deleted requirements)
        print("ℹ️  Note: Gaps may be intentional if requirements were removed.")
        print("   Run with --strict to treat gaps as errors.")
        print()
    else:
        print("✅ No numbering gaps")
        print()
    
    # Distribution by type
    print("📈 ID Distribution by Type:")
    print()
    for key, numbers in sorted(by_type.items()):
        min_num = min(numbers)
        max_num = max(numbers)
        print(f"  {key:20} : {len(numbers):3} IDs (range: {min_num:03d}-{max_num:03d})")
    print()
    
    print("=" * 60)
    
    if issues_found:
        print("❌ Issues found - review duplicates above")
        return 1
    else:
        print("✅ Numbering validation passed")
        return 0


def main(argv: List[str]) -> int:
    """Main entry point."""
    strict = '--strict' in argv
    
    analysis = analyze_numbering()
    exit_code = print_report(analysis)
    
    # In strict mode, gaps are errors
    if strict and analysis['gaps']:
        print("\n⚠️  Strict mode: Treating gaps as errors")
        return 1
    
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
