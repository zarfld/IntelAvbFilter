# Scripts Directory

This directory contains automation scripts for specification validation, creation, and management.

## 🎯 Core Tools

### Spec Creation & Validation

| Script | Purpose | Usage |
|--------|---------|-------|
| `create-spec.py` | Interactive spec creation wizard | `python scripts/create-spec.py requirements --interactive` |
| `validate-spec-structure.py` | Validate YAML front matter against schemas | `python scripts/validate-spec-structure.py` |
| `check-spec-numbering.py` | Check for ID duplicates and gaps | `python scripts/check-spec-numbering.py` |
| `spec-cli.py` | Unified CLI for all spec operations | `python scripts/spec-cli.py validate` |
| `pre-commit-hook.py` | Git pre-commit validation hook | Setup: `pre-commit install` |

### Traceability & Analysis

#### GitHub Issues-Based (Current)

| Script | Purpose | Usage |
|--------|---------|-------|
| **`github-issues-to-traceability-json.py`** | Generate traceability.json from GitHub Issues | `export GITHUB_TOKEN=xxx && python scripts/github-issues-to-traceability-json.py` |
| `github-traceability-report.py` | Generate human-readable traceability matrix from GitHub Issues | `export GITHUB_TOKEN=xxx && python scripts/github-traceability-report.py` |
| `github-orphan-check.py` | Find requirements without parent links | `export GITHUB_TOKEN=xxx && python scripts/github-orphan-check.py` |
| `trace_unlinked_requirements.py` | Find requirements without ADR/TEST links (reads traceability.json) | `python scripts/trace_unlinked_requirements.py --markdown` |
| `validate-traceability.py` | Validate traceability matrix completeness (CI validation) | `python scripts/validate-traceability.py` |

#### File-Based (Legacy - Being Phased Out)

| Script | Purpose | Status | Usage |
|--------|---------|--------|-------|
| `generate-traceability-matrix.py` | Generate traceability matrix | ⚠️ **Deprecated** | `python scripts/generate-traceability-matrix.py` |
| `validate-traceability.py` | Validate traceability links | ⚠️ **Use GitHub Actions** | `python scripts/validate-traceability.py` |
| `trace_unlinked_requirements.py` | Find unlinked requirements | ⚠️ **Use github-orphan-check.py** | `python scripts/trace_unlinked_requirements.py` |
| `generators/spec_parser.py` | Parse specs to JSON index | ⚠️ **Use GitHub API** | `python scripts/generators/spec_parser.py` |
| `generators/build_trace_json.py` | Build traceability JSON | ⚠️ **Use GitHub API** | `python scripts/generators/build_trace_json.py` |
| `generators/gen_tests.py` | Generate test skeletons | ✅ **Still Valid** | `python scripts/generators/gen_tests.py` |

## 📦 Dependencies

### GitHub Issues Scripts (Current - Recommended)

```bash
# Install from requirements.txt (recommended)
pip install -r requirements.txt

# Or install manually
pip install PyGithub requests pyyaml markdown
```

**Key Dependencies**:
- **PyGithub** - Official GitHub API library (robust pagination, rate limiting, authentication)
- **requests** - HTTP library for REST API calls
- **pyyaml** - YAML processing for configuration
- **markdown** - Markdown document generation

### File-Based Scripts (Legacy)

```bash
pip install pyyaml jsonschema
```

## 🔄 Migration to GitHub Issues

**Status**: In progress (see `docs/improvement_ideas/MIGRATION-PLAN-file-to-github-issues.md`)

### What's Changing

- **Requirements tracking** → GitHub Issues with templates
- **ID assignment** → Auto-generated issue numbers (no more duplicates)
- **Traceability** → Native issue linking (#123 syntax)
- **Validation** → GitHub Actions workflows
- **Reports** → API-based scripts (github-traceability-report.py)

### Migration Timeline

- ✅ **Phase 1**: Issue templates created (completed)
- ✅ **Phase 2**: Copilot instructions updated (completed)
- ✅ **Phase 3**: GitHub API scripts created (completed)
- ⏳ **Phase 4**: Data migration (pending)
- ⏳ **Phase 5**: Deprecate file-based scripts (pending)

**Using New Scripts**

**Generate traceability report**:
```bash
export GITHUB_TOKEN=ghp_xxx
export GITHUB_REPOSITORY=owner/repo  # Optional: auto-detected in CI
python scripts/github-traceability-report.py > reports/traceability.md
```

**Check for orphans**:
```bash
export GITHUB_TOKEN=ghp_xxx
export GITHUB_REPOSITORY=owner/repo  # Optional: auto-detected in CI
python scripts/github-orphan-check.py
```

**Repository Auto-Detection**:
- **In GitHub Actions**: `GITHUB_REPOSITORY` is automatically set to `owner/repo`
- **Local testing**: Set `GITHUB_REPOSITORY=owner/repo` or `REPO_OWNER` and `REPO_NAME` separately
- **Fallback**: Defaults to `zarfld/IntelAvbFilter` if not set

**GitHub Actions** (automatic):
- `.github/workflows/traceability-check.yml` - Runs on PR/issue changes
- `.github/workflows/ci-standards-compliance.yml` - Comprehensive validation

## 🚀 Quick Start

### Create a new specification

```bash
# Interactive mode with all prompts
python scripts/create-spec.py requirements --interactive

# Quick create
python scripts/create-spec.py architecture
```

### Validate specifications

```bash
# Validate all specs
python scripts/spec-cli.py validate

# Validate specific file
python scripts/validate-spec-structure.py 02-requirements/functional/auth.md

# Check ID numbering
python scripts/check-spec-numbering.py
```

### Setup pre-commit hooks

```bash
# Using pre-commit framework (recommended)
pip install pre-commit
pre-commit install

# Manual installation
cp scripts/pre-commit-hook.py .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

### Generate compliance report

```bash
# Text format
python scripts/spec-cli.py report

# Markdown format
python scripts/spec-cli.py report --format markdown > compliance-report.md
```

## 📋 Spec Creation Workflow

1. **Create spec** using wizard (auto-numbers, validates)
2. **Edit content** - fill in requirements, acceptance criteria
3. **Validate** before committing
4. **Commit** - pre-commit hook validates automatically

See [docs/spec-creation-workflow.md](../docs/spec-creation-workflow.md) for detailed guide.

## 🔧 Script Details

### create-spec.py

**Features**:
- Auto-generates next available ID
- Validates against JSON schema
- Supports categorized IDs (e.g., REQ-AUTH-F-001)
- Pre-fills template with metadata
- Interactive prompts for all required fields

**Example**:
```bash
python scripts/create-spec.py requirements --interactive
```

### validate-spec-structure.py

**Checks**:
- YAML front matter present
- All required fields exist
- Field formats match schema
- Traceability arrays valid
- ID patterns correct

**Exit codes**:
- 0: Success
- 1: Validation errors
- 2: Internal error

### check-spec-numbering.py

**Reports**:
- Duplicate IDs (error)
- Numbering gaps (warning)
- ID distribution by type
- Coverage statistics

**Modes**:
- Standard: Gaps are warnings
- Strict (`--strict`): Gaps are errors

### spec-cli.py

**Unified interface** for:
- Creating specs
- Validating specs
- Checking numbering
- Generating reports

**Subcommands**:
```bash
spec-cli.py validate [files...]
spec-cli.py create <type> [--interactive]
spec-cli.py check-numbering [--strict]
spec-cli.py report [--format text|markdown]
```

### github-issues-to-traceability-json.py

**Core bridge script** between GitHub Issues and validation tools:
- Fetches all issues from repository via PyGithub
- Extracts traceability links (#N syntax) from issue bodies
- Determines issue type from labels (StR, REQ-F, REQ-NF, ADR, ARC-C, QA-SC, TEST)
- Builds bidirectional traceability (forward + backward links)
- Calculates coverage metrics (ADR linkage, scenario linkage, test linkage)
- Generates `build/traceability.json` for other validation tools

**Output format** (`build/traceability.json`):
```json
{
  "source": "github-issues",
  "repository": "owner/repo",
  "generated_at": "2025-06-12T10:30:00Z",
  "metrics": {
    "overall_coverage": {"total": 50, "linked": 45, "percentage": 90.0},
    "requirement_to_ADR": {"...": "..."},
    "requirement_to_test": {"...": "..."}
  },
  "items": [{
    "number": 123,
    "type": "REQ-F",
    "title": "User Login",
    "state": "open",
    "labels": ["type:requirement:functional", "phase:02-requirements"],
    "body": "User story description... Traces to: #45"
  }],
  "forward_links": {"123": [45, 67]},
  "backward_links": {"45": [123]}
}
```

**Environment variables**:
- `GITHUB_TOKEN` - Required for API authentication
- `GITHUB_REPOSITORY` - Auto-detected in CI, manual for local use

**Example**:
```bash
export GITHUB_TOKEN=ghp_xxx
export GITHUB_REPOSITORY=owner/repo
python scripts/github-issues-to-traceability-json.py
# Output: build/traceability.json
```

### trace_unlinked_requirements.py

**Find requirements without architecture/test links**:
- Reads from `build/traceability.json` (generated by github-issues-to-traceability-json.py)
- Falls back to `build/spec-index.json` if traceability.json missing
- Checks bidirectional linkage (forward + backward references)
- Enriches output with metadata (file path, title)

**Output formats**:
- Human-readable summary (default)
- JSON (`--json`) - machine-readable with metadata
- Markdown table (`--markdown`) - for documentation

**Example**:
```bash
# Generate traceability.json first
python scripts/github-issues-to-traceability-json.py

# Find unlinked requirements
python scripts/trace_unlinked_requirements.py --markdown
```

### validate-traceability.py

**CI validation script** for traceability completeness:
- Reads from `reports/github-traceability.md` and `reports/orphan-check.log`
- Validates that all requirements have linked elements
- Lists specific unlinked requirements (not just counts)
- Exit code 1 if validation fails (blocks CI)

**Used in GitHub Actions**:
```yaml
- name: Validate traceability
  run: python scripts/validate-traceability.py
```

### pre-commit-hook.py

**Automatic validation** on git commit:
- Validates only staged spec files
- Blocks commit on errors
- Can be bypassed with `--no-verify`

## 🎓 Standards Compliance

All scripts enforce:
- **ISO/IEC/IEEE 29148:2018** - Requirements specifications
- **ISO/IEC/IEEE 42010:2011** - Architecture descriptions
- **JSON Schema Draft 7** - YAML front matter validation

## 📚 Related Documentation

- [Spec Creation Workflow](../docs/spec-creation-workflow.md) - Complete workflow guide
- [ID Taxonomy Guide](../docs/id-taxonomy-guide.md) - Category identifier usage
- [Lifecycle Guide](../docs/lifecycle-guide.md) - Full development process

## 🐛 Troubleshooting

**Import errors**: Scripts use runtime path manipulation - linter warnings are expected

**Missing dependencies**:
```bash
pip install pyyaml jsonschema
```

**Pre-commit not running**:
```bash
pre-commit install --force
```

**Schema validation fails**: Check YAML front matter matches schema exactly (case-sensitive, proper types)

---

For questions, see [docs/spec-creation-workflow.md](../docs/spec-creation-workflow.md) or open an issue.
