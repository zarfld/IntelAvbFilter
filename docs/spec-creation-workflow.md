# Spec Creation and Validation Workflow

> **⚠️ DEPRECATED**: This document describes the **legacy file-based traceability workflow**.
> 
> **Current Approach**: Use **GitHub Issues** as the single source of truth for requirements, architecture, and tests.
> - See: [GitHub Issues Workflow](improvement_ideas/using_github_issues_as_infrastructure_for_REQ_Tracability.md)
> - See: Phase-specific instructions in `.github/instructions/phase-NN-*.instructions.md`
> 
> This document is kept for historical reference only.

---

This document describes the infrastructure and workflow for creating and validating specification files with automatic compliance checking.

## 🎯 Overview

The template provides comprehensive tooling to ensure all specifications:
- ✅ Have valid YAML front matter
- ✅ Pass JSON schema validation
- ✅ Use correct ID numbering (auto-incremented)
- ✅ Follow IEEE/ISO/IEC standards
- ✅ Maintain traceability

## 🛠️ Tools Available

### 1. **Spec Creation Wizard** (`scripts/create-spec.py`)

Interactive tool that guides you through creating compliant spec files.

**Features**:
- Auto-generates next available ID number
- Prompts for all required metadata
- Validates against JSON schema before creation
- Supports both basic and categorized IDs
- Pre-fills template with your inputs

**Usage**:

```bash
# Interactive mode - prompts for all inputs
python scripts/create-spec.py requirements --interactive

# Quick create with defaults
python scripts/create-spec.py requirements

# Create architecture spec (ADR)
python scripts/create-spec.py architecture
```

**Example Session**:

```
📋 Creating Requirements Specification
==================================================

Use category identifier (e.g., AUTH, PAYM)? [y/N]: y
Category (4 uppercase letters): AUTH

✨ Next available ID: REQ-AUTH-F-001

Specification title: User Authentication API
Author name [System]: Jane Developer

Select subdirectory:
  1. functional (default)
  2. non-functional
  3. use-cases
  4. user-stories
Enter choice number: 1

Enter stakeholder requirement IDs (e.g., StR-001)
Press Enter with empty value when done
Stakeholder Requirement ID: StR-SECUR-001
Stakeholder Requirement ID: 

✅ Metadata validation passed
✅ Created: 02-requirements/functional/user-authentication-api.md
📝 ID: REQ-AUTH-F-001
📋 Status: draft
```

### 2. **Pre-commit Hook** (`scripts/pre-commit-hook.py`)

Validates spec files before they're committed to Git.

**Features**:
- Runs automatically on `git commit`
- Validates only staged spec files
- Provides clear error messages
- Can be bypassed with `--no-verify` if needed

**Setup**:

```bash
# Option 1: Manual installation
cp scripts/pre-commit-hook.py .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit

# Option 2: Using pre-commit framework (recommended)
pip install pre-commit
pre-commit install
```

**What it checks**:
- ✅ YAML front matter present and valid
- ✅ All required fields present (specType, version, author, date, status)
- ✅ Fields match JSON schema constraints
- ✅ Traceability arrays properly formatted
- ✅ ID patterns valid

**Example Output**:

```
🔍 Validating staged specification files...
📋 Found 2 spec file(s) to validate
  Checking: 02-requirements/functional/auth-api.md
    ✅ Valid
  Checking: 03-architecture/decisions/ADR-001-database-choice.md
    ❌ Missing required field: traceability.requirements
============================================================
❌ Validation failed: 1 issue(s) found

Fix the issues above and try committing again.
```

### 3. **Numbering Checker** (`scripts/check-spec-numbering.py`)

Analyzes ID numbering across all specs to detect duplicates and gaps.

**Features**:
- Detects duplicate IDs (hard error)
- Finds numbering gaps (warning)
- Groups by category
- Shows distribution statistics

**Usage**:

```bash
# Standard check (gaps are warnings)
python scripts/check-spec-numbering.py

# Strict mode (gaps are errors)
python scripts/check-spec-numbering.py --strict
```

**Example Output**:

```
🔍 Specification ID Numbering Analysis
============================================================

📊 Summary:
  Total unique IDs: 47
  ID categories: 6

✅ No duplicate IDs found

⚠️  NUMBERING GAPS DETECTED:

  REQ-F: Missing 2 number(s)
    [5, 12]

  ADR-: Missing 1 number(s)
    [3]

ℹ️  Note: Gaps may be intentional if requirements were removed.
   Run with --strict to treat gaps as errors.

📈 ID Distribution by Type:

  StR-                 :   8 IDs (range: 001-010)
  REQ-AUTH-F          :   5 IDs (range: 001-005)
  REQ-F               :  18 IDs (range: 001-020)
  REQ-NF              :   6 IDs (range: 001-006)
  ADR-                :   9 IDs (range: 001-010)
  ADR-INFRA-          :   1 IDs (range: 001-001)
```

### 4. **Unified CLI Tool** (`scripts/spec-cli.py`)

Comprehensive command-line interface for all spec operations.

**Commands**:

```bash
# Validate all specs
python scripts/spec-cli.py validate

# Validate specific files
python scripts/spec-cli.py validate 02-requirements/functional/*.md

# Create new spec (interactive)
python scripts/spec-cli.py create requirements --interactive

# Check numbering
python scripts/spec-cli.py check-numbering

# Generate compliance report
python scripts/spec-cli.py report --format markdown
```

**Report Example**:

```
# Specification Compliance Report

**Generated**: D:\Repos\copilot-instructions-template

## Validation Summary

- Total specs: 24
- ✅ Valid: 22
- ❌ Invalid: 2
- ⚠️  Warnings: 3

## ID Numbering

- Total IDs: 47
- ❌ Duplicates: 0
- ⚠️  Gaps: 3

## Compliance Score

**91.7%** (22/24 specs compliant)
```

### 5. **Existing Validator** (`scripts/validate-spec-structure.py`)

Low-level validation tool (used by other tools).

**Usage**:

```bash
# Validate all discovered specs
python scripts/validate-spec-structure.py

# Validate specific files
python scripts/validate-spec-structure.py 02-requirements/functional/*.md
```

## 📋 Recommended Workflow

### Creating New Specs

**Option 1: Using Creation Wizard (Recommended)**

```bash
# Interactive creation with all validations
python scripts/create-spec.py requirements --interactive
```

Benefits:
- ✅ Auto-generated ID (no conflicts)
- ✅ Pre-validated metadata
- ✅ Template populated correctly
- ✅ Follows naming conventions

**Option 2: Manual Creation**

```bash
# Copy template
cp spec-kit-templates/requirements-spec.md 02-requirements/functional/my-feature.md

# Edit metadata manually
# RISK: May have validation errors
```

### Before Committing

```bash
# Validate your changes
python scripts/spec-cli.py validate 02-requirements/functional/my-feature.md

# Check numbering
python scripts/check-spec-numbering.py

# If all good, commit (pre-commit hook runs automatically)
git add 02-requirements/functional/my-feature.md
git commit -m "Add: User authentication requirements (REQ-AUTH-F-001)"
```

### CI/CD Integration

Add to `.github/workflows/`:

```yaml
name: Spec Validation

on: [push, pull_request]

jobs:
  validate-specs:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - uses: actions/setup-python@v4
        with:
          python-version: '3.10'
      
      - name: Install dependencies
        run: pip install pyyaml jsonschema
      
      - name: Validate spec structure
        run: python scripts/validate-spec-structure.py
      
      - name: Check ID numbering
        run: python scripts/check-spec-numbering.py
      
      - name: Generate compliance report
        run: python scripts/spec-cli.py report --format markdown >> $GITHUB_STEP_SUMMARY
```

## 🔍 Validation Rules

### YAML Front Matter Requirements

**All Specs Must Have**:

```yaml
---
specType: requirements  # or architecture
standard: "ISO/IEC/IEEE 29148:2018"  # or relevant standard
phase: "02-requirements"  # or relevant phase
version: "1.0.0"  # Semantic versioning
author: "Your Name"
date: "2025-11-03"  # YYYY-MM-DD format
status: draft  # draft | review | approved | deprecated
---
```

**Requirements Specs Additional Fields**:

```yaml
traceability:
  stakeholderRequirements:
    - StR-001
    - StR-002
```

**Architecture Specs Additional Fields**:

```yaml
traceability:
  requirements:
    - REQ-F-001
    - REQ-NF-001
```

### ID Pattern Validation

**Supported Patterns**:

- `StR-001` or `StR-AUTH-001` (Stakeholder Requirements)
- `REQ-F-001` or `REQ-AUTH-F-001` (Functional Requirements)
- `REQ-NF-001` or `REQ-PERF-NF-001` (Non-Functional Requirements)
- `ADR-001` or `ADR-INFRA-001` (Architecture Decision Records)

**Category Rules**:
- Optional 4-character uppercase prefix
- Letters only (no numbers in category)
- Examples: AUTH, PAYM, SECUR, INFRA, CORE

### Common Validation Errors

| Error | Cause | Fix |
|-------|-------|-----|
| Missing YAML front matter | No `---` block at file start | Add front matter from template |
| Missing specType | Field not in front matter | Add `specType: requirements` |
| Invalid date format | Date not YYYY-MM-DD | Use `2025-11-03` format |
| Empty traceability array | No upstream requirements listed | Add at least one requirement ID |
| Invalid ID pattern | Typo in requirement ID | Use `REQ-F-001` format |
| Duplicate ID | Same ID used in multiple files | Check with numbering tool |

## 📈 Quality Gates

### Development Quality Gates

1. **Pre-commit** (Automatic)
   - YAML front matter valid
   - Schema compliance
   - No critical errors

2. **Pre-push** (Recommended)
   - All specs validated
   - No duplicate IDs
   - Numbering gaps reviewed

3. **Pull Request** (Required)
   - CI validation passes
   - Compliance report ≥90%
   - Code review approval

### Release Quality Gates

1. **Pre-release**
   - 100% spec validation
   - No duplicate IDs
   - Strict numbering check
   - Traceability matrix complete

2. **Production Release**
   - All quality gates passed
   - Standards compliance audit
   - Documentation updated

## 🎓 Best Practices

### DO ✅

- **Use the creation wizard** for new specs
- **Run validation** before committing
- **Install pre-commit hooks** for automatic checks
- **Review numbering gaps** regularly
- **Use categories** for complex projects
- **Link traceability** from the start
- **Update status** field as specs evolve

### DON'T ❌

- **Copy-paste without validation** (may have template placeholders)
- **Skip front matter** (breaks tooling)
- **Hardcode ID numbers** (use auto-numbering)
- **Ignore validation errors** (fix before committing)
- **Use random categories** (document your taxonomy)
- **Leave orphan specs** (maintain traceability)

## 🔧 Troubleshooting

### "Missing dependency" errors

```bash
pip install pyyaml jsonschema
```

### Pre-commit hook not running

```bash
# Reinstall
pre-commit install --force

# Or manual setup
chmod +x .git/hooks/pre-commit
```

### Schema validation fails

Check that your front matter matches the schema exactly:
- Field names are case-sensitive
- Dates must be YYYY-MM-DD
- Traceability must be an array
- Version must be semantic (1.0.0)

### ID numbering conflicts

```bash
# Find duplicates
python scripts/check-spec-numbering.py

# Regenerate IDs if needed
python scripts/create-spec.py requirements --interactive
```

## 📚 Related Documentation

- [Lifecycle Guide](lifecycle-guide.md) - Full development workflow
- [Spec-Driven Development](spec-driven-development.md) - Using specs as code
- [ID Taxonomy Guide](id-taxonomy-guide.md) - Category identifier usage
- [XP Practices](xp-practices.md) - Extreme Programming integration

---

**Questions?** Open an issue or consult the phase-specific copilot instructions in `.github/instructions/`.
