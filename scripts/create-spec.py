#!/usr/bin/env python3
"""Interactive Spec Creation Wizard

This tool ensures all new specification files:
- Follow the correct template structure
- Have valid YAML front matter
- Use correct ID numbering (auto-incremented)
- Pass schema validation before creation
- Are created in the correct phase directory

Usage:
  python scripts/create-spec.py requirements
  python scripts/create-spec.py architecture
  python scripts/create-spec.py --interactive

"""
from __future__ import annotations
import sys
import json
import re
from pathlib import Path
from datetime import date
from typing import Optional, Dict, Any
import shutil

try:
    import yaml
    import jsonschema
except ImportError:
    print("❌ Missing dependencies. Install with: pip install pyyaml jsonschema", file=sys.stderr)
    sys.exit(2)

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = ROOT / 'spec-kit-templates' / 'schemas'
TEMPLATE_DIR = ROOT / 'spec-kit-templates'

SPEC_TYPES = {
    'requirements': {
        'phase': '02-requirements',
        'subdir': 'functional',  # default subdirectory
        'template': 'requirements-spec.md',
        'schema': 'requirements-spec.schema.json',
        'id_prefix': 'REQ-F-',
        'standard': 'ISO/IEC/IEEE 29148:2018',
    },
    'architecture': {
        'phase': '03-architecture',
        'subdir': 'decisions',
        'template': 'architecture-spec.md',
        'schema': 'architecture-spec.schema.json',
        'id_prefix': 'ADR-',
        'standard': 'ISO/IEC/IEEE 42010:2011',
    }
}

# ID pattern matching (supports optional categories)
ID_PATTERNS = {
    'requirements': re.compile(r'REQ-(?P<category>[A-Z]{4}-)?(?P<type>F|NF)-(?P<num>\d{3})'),
    'stakeholder': re.compile(r'StR-(?P<category>[A-Z]{4}-)?(?P<num>\d{3})'),
    'architecture': re.compile(r'ADR-(?P<category>[A-Z]{4}-)?(?P<num>\d{3})'),
}


def get_next_id_number(phase_dir: Path, id_prefix: str, category: Optional[str] = None) -> int:
    """Find the next available ID number in the phase directory."""
    max_num = 0
    
    # Build the pattern to search for
    if category:
        pattern = re.compile(rf'{id_prefix}{category}-(\d{{3}})')
    else:
        pattern = re.compile(rf'{id_prefix}(\d{{3}})')
    
    # Scan all markdown files in phase directory
    for md_file in phase_dir.rglob('*.md'):
        if md_file.name.startswith('README') or 'template' in md_file.name.lower():
            continue
        
        try:
            content = md_file.read_text(encoding='utf-8')
            for match in pattern.finditer(content):
                num = int(match.group(1))
                max_num = max(max_num, num)
        except Exception:
            continue
    
    return max_num + 1


def validate_against_schema(metadata: Dict[str, Any], spec_type: str) -> tuple[bool, list[str]]:
    """Validate metadata against JSON schema."""
    schema_path = SCHEMA_DIR / SPEC_TYPES[spec_type]['schema']
    
    if not schema_path.exists():
        return False, [f"Schema file not found: {schema_path}"]
    
    try:
        schema = json.loads(schema_path.read_text(encoding='utf-8'))
        validator = jsonschema.Draft7Validator(schema)
        errors = list(validator.iter_errors(metadata))
        
        if errors:
            return False, [f"{'/'.join(map(str, err.path)) or '<root>'}: {err.message}" for err in errors]
        
        return True, []
    
    except Exception as e:
        return False, [f"Schema validation error: {e}"]


def prompt_input(prompt: str, default: Optional[str] = None, required: bool = True) -> str:
    """Prompt user for input with optional default."""
    if default:
        prompt = f"{prompt} [{default}]"
    
    prompt += ": "
    
    while True:
        value = input(prompt).strip()
        
        if not value and default:
            return default
        
        if not value and required:
            print("  ⚠️  This field is required. Please enter a value.")
            continue
        
        return value


def prompt_choice(prompt: str, choices: list[str], default: Optional[str] = None) -> str:
    """Prompt user to choose from a list."""
    print(f"\n{prompt}")
    for i, choice in enumerate(choices, 1):
        marker = " (default)" if choice == default else ""
        print(f"  {i}. {choice}{marker}")
    
    while True:
        value = input("Enter choice number: ").strip()
        
        if not value and default:
            return default
        
        try:
            idx = int(value) - 1
            if 0 <= idx < len(choices):
                return choices[idx]
        except ValueError:
            pass
        
        print(f"  ⚠️  Please enter a number between 1 and {len(choices)}")


def prompt_yes_no(prompt: str, default: bool = False) -> bool:
    """Prompt for yes/no answer."""
    default_str = "Y/n" if default else "y/N"
    response = input(f"{prompt} [{default_str}]: ").strip().lower()
    
    if not response:
        return default
    
    return response in ('y', 'yes')


def create_requirements_spec(interactive: bool = True) -> Optional[Path]:
    """Create a new requirements specification."""
    print("\n📋 Creating Requirements Specification")
    print("=" * 50)
    
    config = SPEC_TYPES['requirements']
    phase_dir = ROOT / config['phase']
    
    # Get category (optional)
    category = None
    if interactive:
        use_category = prompt_yes_no("Use category identifier (e.g., AUTH, PAYM)?", default=False)
        if use_category:
            category = prompt_input("Category (4 uppercase letters)", required=True)
            if len(category) != 4 or not category.isupper() or not category.isalpha():
                print("  ⚠️  Category must be exactly 4 uppercase letters")
                return None
    
    # Auto-generate next ID
    next_num = get_next_id_number(phase_dir, config['id_prefix'], category)
    if category:
        next_id = f"{config['id_prefix']}{category}-{next_num:03d}"
    else:
        next_id = f"{config['id_prefix']}{next_num:03d}"
    
    print(f"\n✨ Next available ID: {next_id}")
    
    # Get spec metadata
    title = prompt_input("Specification title", required=True) if interactive else "New Requirement"
    author = prompt_input("Author name", default="System", required=True) if interactive else "System"
    
    # Choose subdirectory
    subdirs = ['functional', 'non-functional', 'use-cases', 'user-stories']
    subdir = prompt_choice("Select subdirectory", subdirs, default='functional') if interactive else 'functional'
    
    # Get stakeholder requirements
    stakeholder_reqs = []
    if interactive:
        print("\nEnter stakeholder requirement IDs (e.g., StR-001)")
        print("Press Enter with empty value when done")
        while True:
            req_id = prompt_input("Stakeholder Requirement ID", required=False)
            if not req_id:
                break
            if not re.match(r'^StR-(?:[A-Z]{4}-)??\d{3}$', req_id):
                print("  ⚠️  Invalid format. Use StR-NNN or StR-XXXX-NNN")
                continue
            stakeholder_reqs.append(req_id)
    
    if not stakeholder_reqs:
        stakeholder_reqs = ['StR-001']  # Default
    
    # Build metadata
    metadata = {
        'title': title,
        'specType': 'requirements',
        'standard': config['standard'],
        'phase': config['phase'],
        'version': '1.0.0',
        'author': author,
        'date': date.today().strftime('%Y-%m-%d'),
        'status': 'draft',
        'traceability': {
            'stakeholderRequirements': stakeholder_reqs
        }
    }
    
    if category:
        metadata['idCategory'] = category
    
    # Validate against schema
    valid, errors = validate_against_schema(metadata, 'requirements')
    
    if not valid:
        print("\n❌ Schema validation failed:")
        for error in errors:
            print(f"  • {error}")
        return None
    
    print("\n✅ Metadata validation passed")
    
    # Load template
    template_path = TEMPLATE_DIR / config['template']
    if not template_path.exists():
        print(f"❌ Template not found: {template_path}")
        return None
    
    template_content = template_path.read_text(encoding='utf-8')
    
    # Replace template placeholders
    # Extract existing front matter and replace it
    fm_match = re.match(r'^---\n(.*?)\n---\n(.*)$', template_content, re.DOTALL)
    if fm_match:
        body = fm_match.group(2)
    else:
        body = template_content
    
    # Create new front matter
    front_matter = yaml.dump(metadata, default_flow_style=False, sort_keys=False)
    
    # Replace placeholders in body
    body = body.replace('[Feature Name]', title)
    body = body.replace('[Your Name]', author)
    body = body.replace('2025-02-15', metadata['date'])
    body = body.replace('REQ-F-001', next_id)
    
    # Combine
    content = f"---\n{front_matter}---\n{body}"
    
    # Determine filename
    filename_base = re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')
    filename = f"{filename_base}.md"
    
    target_dir = phase_dir / subdir
    target_dir.mkdir(parents=True, exist_ok=True)
    target_path = target_dir / filename
    
    # Check if file exists
    if target_path.exists():
        if interactive and not prompt_yes_no(f"File {filename} exists. Overwrite?", default=False):
            print("❌ Cancelled")
            return None
    
    # Write file
    target_path.write_text(content, encoding='utf-8')
    
    print(f"\n✅ Created: {target_path.relative_to(ROOT)}")
    print(f"📝 ID: {next_id}")
    print(f"📋 Status: draft")
    
    return target_path


def create_architecture_spec(interactive: bool = True) -> Optional[Path]:
    """Create a new architecture specification (ADR)."""
    print("\n🏗️  Creating Architecture Decision Record (ADR)")
    print("=" * 50)
    
    config = SPEC_TYPES['architecture']
    phase_dir = ROOT / config['phase']
    
    # Get category (optional)
    category = None
    if interactive:
        use_category = prompt_yes_no("Use category identifier (e.g., INFRA, SECUR)?", default=False)
        if use_category:
            category = prompt_input("Category (4 uppercase letters)", required=True)
            if len(category) != 4 or not category.isupper() or not category.isalpha():
                print("  ⚠️  Category must be exactly 4 uppercase letters")
                return None
    
    # Auto-generate next ID
    next_num = get_next_id_number(phase_dir, config['id_prefix'], category)
    if category:
        next_id = f"{config['id_prefix']}{category}-{next_num:03d}"
    else:
        next_id = f"{config['id_prefix']}{next_num:03d}"
    
    print(f"\n✨ Next available ID: {next_id}")
    
    # Get spec metadata
    title = prompt_input("ADR title", required=True) if interactive else "New Architecture Decision"
    author = prompt_input("Author name", default="System", required=True) if interactive else "System"
    
    # Get requirement IDs
    requirement_ids = []
    if interactive:
        print("\nEnter requirement IDs that this ADR addresses (e.g., REQ-F-001)")
        print("Press Enter with empty value when done")
        while True:
            req_id = prompt_input("Requirement ID", required=False)
            if not req_id:
                break
            if not re.match(r'^REQ-(?:[A-Z]{4}-)?(?:F|NF)-\d{3}$', req_id):
                print("  ⚠️  Invalid format. Use REQ-F-NNN or REQ-XXXX-F-NNN")
                continue
            requirement_ids.append(req_id)
    
    if not requirement_ids:
        requirement_ids = ['REQ-F-001']  # Default
    
    # Build metadata
    metadata = {
        'title': title,
        'specType': 'architecture',
        'standard': config['standard'],
        'phase': config['phase'],
        'version': '1.0.0',
        'author': author,
        'date': date.today().strftime('%Y-%m-%d'),
        'status': 'draft',
        'traceability': {
            'requirements': requirement_ids
        }
    }
    
    if category:
        metadata['idCategory'] = category
    
    # Validate against schema
    valid, errors = validate_against_schema(metadata, 'architecture')
    
    if not valid:
        print("\n❌ Schema validation failed:")
        for error in errors:
            print(f"  • {error}")
        return None
    
    print("\n✅ Metadata validation passed")
    
    # Load template
    template_path = TEMPLATE_DIR / config['template']
    if not template_path.exists():
        print(f"❌ Template not found: {template_path}")
        return None
    
    template_content = template_path.read_text(encoding='utf-8')
    
    # Replace template placeholders
    fm_match = re.match(r'^---\n(.*?)\n---\n(.*)$', template_content, re.DOTALL)
    if fm_match:
        body = fm_match.group(2)
    else:
        body = template_content
    
    # Create new front matter
    front_matter = yaml.dump(metadata, default_flow_style=False, sort_keys=False)
    
    # Replace placeholders in body
    body = body.replace('[Decision Title]', title)
    body = body.replace('[Your Name]', author)
    body = body.replace('2025-02-15', metadata['date'])
    body = body.replace('ADR-001', next_id)
    
    # Combine
    content = f"---\n{front_matter}---\n{body}"
    
    # Determine filename (ADR format: ADR-NNN-title.md)
    filename_base = re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')
    filename = f"{next_id}-{filename_base}.md"
    
    target_dir = phase_dir / config['subdir']
    target_dir.mkdir(parents=True, exist_ok=True)
    target_path = target_dir / filename
    
    # Check if file exists
    if target_path.exists():
        if interactive and not prompt_yes_no(f"File {filename} exists. Overwrite?", default=False):
            print("❌ Cancelled")
            return None
    
    # Write file
    target_path.write_text(content, encoding='utf-8')
    
    print(f"\n✅ Created: {target_path.relative_to(ROOT)}")
    print(f"🏗️  ID: {next_id}")
    print(f"📋 Status: draft")
    
    return target_path


def main(argv: list[str]) -> int:
    """Main entry point."""
    if not argv or '--help' in argv or '-h' in argv:
        print(__doc__)
        print("\nAvailable spec types:")
        for spec_type, config in SPEC_TYPES.items():
            print(f"  • {spec_type:15} - {config['standard']}")
        return 0
    
    interactive = '--interactive' in argv or '-i' in argv
    
    if interactive or len(argv) == 0:
        # Interactive mode
        spec_type = prompt_choice(
            "Select specification type:",
            list(SPEC_TYPES.keys()),
            default='requirements'
        )
    else:
        spec_type = argv[0]
    
    if spec_type not in SPEC_TYPES:
        print(f"❌ Unknown spec type: {spec_type}", file=sys.stderr)
        print(f"Available types: {', '.join(SPEC_TYPES.keys())}", file=sys.stderr)
        return 1
    
    # Create spec based on type
    if spec_type == 'requirements':
        result = create_requirements_spec(interactive=interactive or len(argv) == 1)
    elif spec_type == 'architecture':
        result = create_architecture_spec(interactive=interactive or len(argv) == 1)
    else:
        print(f"❌ Spec type '{spec_type}' not yet implemented", file=sys.stderr)
        return 1
    
    if result is None:
        return 1
    
    print("\n" + "=" * 50)
    print("✅ Spec creation complete!")
    print("\nNext steps:")
    print("  1. Review and edit the generated file")
    print("  2. Fill in requirement details and acceptance criteria")
    print("  3. Run validation: python scripts/validate-spec-structure.py")
    print("  4. Commit to version control")
    
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
