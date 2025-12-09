# ADR-SCRIPT-001: Consolidated Script Architecture

**Status**: Accepted  
**Date**: 2025-12-09  
**Phase**: Phase 03 - Architecture Design  
**Priority**: Important (P1)

---

## Context

The IntelAvbFilter repository has accumulated **80+ batch (.bat) and PowerShell (.ps1) scripts** with severe organization and naming problems that significantly impact developer productivity and maintainability.

### Current Problem State

**Script Explosion**:
- **43 .bat files**
- **37 .ps1 files**
- **Total: 80+ automation scripts**

**Critical Issues**:

| Problem | Example | Impact |
|---------|---------|--------|
| **Naming Conflicts** | `setup_driver.ps1` vs `Setup-Driver.ps1` (case) | README references wrong script |
| **Extension Duplicates** | `Setup-Driver.ps1` vs `Setup-Driver.bat` | Unclear which to use |
| **Functional Overlap** | `Install-AvbFilter.ps1` vs `Install-Now.bat` | Same purpose, different names |
| **Unclear Distinctions** | `Build-AllTests-Honest.ps1` vs `Build-AllTests-TrulyHonest.ps1` | Which is more "honest"? |
| **Configuration Variants** | `Quick-Test-Debug.bat` + `Quick-Test-Release.bat` | Should be parameters |
| **Scattered Organization** | All 80+ scripts in root directory | No logical grouping |

### Script Count by Category

| Category | Count | Examples | Problem |
|----------|-------|----------|---------|
| **Driver Setup/Install** | 14+ | `setup_driver.ps1`, `Setup-Driver.ps1`, `Install-AvbFilter.ps1`, `Complete-Driver-Setup.bat` | Naming chaos |
| **Test Execution** | 10+ | `Quick-Test-Debug.bat`, `Test-Release.bat`, `test_hardware_only.bat` | Unclear hierarchy |
| **Driver Build** | 6+ | `Build-And-Sign-Driver.ps1`, `build_all_tests.cmd` | Mixed styles |
| **Certificate Management** | 4+ | `troubleshoot_certificates.ps1`, `Manage-Certificate.ps1`, `Generate-CATFile.ps1` | Overlapping |
| **Test Signing** | 5+ | `Enable-TestSigning.bat`, `fix_test_signing.bat` | Redundant |
| **Driver Update** | 5+ | `Update-Driver-Quick.bat`, `Smart-Update-Driver.bat`, `reinstall_debug_quick.bat` | Similar purposes |
| **Diagnostics** | 8+ | `run_complete_diagnostics.bat`, `Check-Driver-Status.ps1` | No clear owner |
| **Build Tools** | 10+ | `build_i226_test.bat`, `build_investigation_tools.cmd` | Device clutter |
| **Legacy/Unused** | 15+ | `Nuclear-Install.bat`, `Quick-Test-Release.bat`, `fix_deployment_config.ps1` | Dangerous/outdated |

### Discovery

**User Feedback**: *"the repo has reached a confusing number of bat and ps1 scripts for several features - a lot of them are redundant some of them outdated!"*

**Documentation Mismatch**: README.md references `setup_driver.ps1` (lowercase), but `Setup-Driver.ps1` (uppercase) also exists → unpredictable behavior

---

## Decision

**Consolidate 80+ scripts into ~15 organized, parameterized scripts** following PowerShell naming conventions, with clear directory structure and deprecation wrappers for backward compatibility.

### Architecture: Organized Script Hierarchy

```
Repository Root/
├── scripts/
│   ├── setup/
│   │   ├── Setup-Driver.ps1          ← Consolidates 14 setup scripts
│   │   ├── Manage-Certificate.ps1    ← Consolidates 4 certificate scripts
│   │   └── Enable-TestSigning.ps1    ← Test signing helper
│   │
│   ├── build/
│   │   ├── Build-Driver.ps1          ← Consolidates 6 build scripts
│   │   ├── Sign-Driver.ps1           ← Signing operations
│   │   └── Build-Tests.ps1           ← Test compilation
│   │
│   ├── test/
│   │   ├── Test-Driver.ps1           ← Consolidates 10 test scripts
│   │   ├── Run-Diagnostics.ps1       ← Diagnostic suite
│   │   └── Test-Configuration.ps1    ← Config validator
│   │
│   ├── development/
│   │   ├── Force-Driver-Reload.ps1   ← Dev shortcuts
│   │   ├── Check-Driver-Status.ps1   ← Status queries
│   │   └── Quick-Start.ps1           ← Quick dev setup
│   │
│   └── archive/
│       └── deprecated/
│           ├── Nuclear-Install.bat   ← DANGEROUS (archived)
│           └── [70+ obsolete scripts]
│
├── Setup-Driver.ps1       ← Symlink → scripts/setup/Setup-Driver.ps1
├── Test-Driver.ps1        ← Symlink → scripts/test/Test-Driver.ps1
└── Build-Driver.ps1       ← Symlink → scripts/build/Build-Driver.ps1
```

### Naming Convention

**Standard**: `<Verb>-<Noun>.ps1` (PowerShell convention)

**Rules**:
- ✅ **PascalCase** for multi-word actions/targets
- ✅ **Verb-Noun** structure (e.g., `Setup-Driver`, `Build-Tests`)
- ✅ **Parameters** instead of separate scripts (`-Configuration Debug` not `Quick-Test-Debug.bat`)
- ✅ **Prefer `.ps1`** over `.bat` (better error handling, cross-platform)

**Examples**:
- `Setup-Driver.ps1` (replaces `setup_driver.ps1`, `Setup-Driver.bat`, `Install-AvbFilter.ps1`)
- `Build-Driver.ps1` (replaces `Build-And-Sign-Driver.ps1`)
- `Test-Driver.ps1` (replaces `test_driver.ps1`, `Quick-Test-Debug.bat`, `test_hardware_only.bat`)

---

## Rationale

### 1. Eliminate Developer Confusion

**Before**: 80+ scripts, unclear which to use  
**After**: 15 consolidated scripts with clear purposes

**Impact**: New developers find automation in minutes, not hours

### 2. Parameter-Driven Design

**Before**: Separate scripts for each variant:
```powershell
Quick-Test-Debug.bat
Quick-Test-Release.bat
test_hardware_only.bat
test_local_i219.bat
```

**After**: Single parameterized script:
```powershell
Test-Driver.ps1 -Configuration Debug -Quick
Test-Driver.ps1 -Configuration Release -Quick
Test-Driver.ps1 -HardwareOnly
Test-Driver.ps1 -Device I219
```

**Benefit**: 1 script to maintain vs. 10

### 3. Organized Directory Structure

**Before**: 80 scripts scattered in root (impossible to navigate)  
**After**: Logical categories (setup/, build/, test/, development/)

**Benefit**: Clear separation of concerns, easy to find scripts

### 4. Consistent Naming

**Before**: `setup_driver.ps1` vs `Setup-Driver.ps1` vs `Setup-Driver.bat` (3 versions!)  
**After**: `Setup-Driver.ps1` (one authoritative version)

**Benefit**: No ambiguity, documentation references work

### 5. Backward Compatibility

**Deprecation Wrappers** keep old paths working:
```powershell
# Old: setup_driver.ps1
Write-Warning "setup_driver.ps1 is DEPRECATED"
Write-Host "Use: .\Setup-Driver.ps1"
& "$PSScriptRoot\scripts\setup\Setup-Driver.ps1" @PSBoundParameters
```

**Benefit**: 6-month grace period for migration

### 6. Single Source of Truth (SSOT)

Each function has ONE authoritative script:
- Setup/Install → `Setup-Driver.ps1`
- Build → `Build-Driver.ps1`
- Test → `Test-Driver.ps1`

**Benefit**: Updates in one place, no drift between duplicates

---

## Alternatives Considered

### Alternative 1: Keep All Scripts, Rename Only

**Approach**: Standardize naming but keep all 80+ scripts

**Rejected**:
- ❌ Still have 80+ scripts to maintain
- ❌ Functional overlap remains (Install-AvbFilter.ps1 vs Install-Now.bat)
- ❌ Doesn't solve root problem (too many scripts)

### Alternative 2: Delete All Old Scripts Immediately

**Approach**: Remove all old scripts, force users to new paths

**Rejected**:
- ❌ Breaking change for all users
- ❌ CI/CD pipelines break immediately
- ❌ Documentation references all invalid

**Why Not This**: Deprecation wrappers provide smooth migration path

### Alternative 3: Bash Scripts (.sh) for Cross-Platform

**Approach**: Use Bash instead of PowerShell for Linux/macOS support

**Rejected**:
- ❌ Target platform is Windows (NDIS kernel driver)
- ❌ PowerShell now cross-platform (pwsh on Linux/macOS)
- ❌ Existing scripts are PowerShell (consistency)

### Alternative 4: Single Monolithic Script

**Approach**: One mega-script with subcommands (`driver.ps1 setup`, `driver.ps1 build`)

**Rejected**:
- ❌ Violates Single Responsibility Principle
- ❌ Hard to maintain (1000+ line script)
- ❌ Difficult to test individual functions

**Why Consolidated is Better**: Organized scripts balance granularity vs. manageability

---

## Consequences

### Positive
- ✅ **70% Reduction**: 80+ scripts → ~15 consolidated scripts
- ✅ **Clear Naming**: 100% follow `Verb-Noun.ps1` convention
- ✅ **Organized**: Logical directory structure (setup/, build/, test/)
- ✅ **Discoverable**: Easy to find scripts by category
- ✅ **Maintainable**: Single script per function (no duplicates)
- ✅ **Parameter-Driven**: Flexibility without script explosion
- ✅ **Backward Compatible**: Deprecation wrappers preserve old paths

### Negative
- ❌ **Migration Effort**: 18 hours total (consolidation + testing + docs)
- ❌ **Breaking Change**: Users must update paths (6-month grace period)
- ❌ **Documentation Updates**: README, INSTALLATION_GUIDE, CI/CD workflows
- ❌ **Learning Curve**: Developers must learn new script locations

### Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Users miss migration notice** | Continue using old paths indefinitely | Deprecation warnings on every execution |
| **CI/CD breaks after 6 months** | Automated pipelines fail | Update all workflows during migration |
| **Functionality lost in consolidation** | Features accidentally removed | Comprehensive testing of consolidated scripts |
| **Documentation drift** | Docs reference wrong paths | Automated link validation in CI |

---

## Implementation Plan

### Phase 1: Analysis and Mapping (2 hours)

**Audit 80+ Scripts**:
1. Identify purpose, usage frequency, dependencies
2. Check git history for last usage
3. Search CI/CD workflows for references
4. Flag dangerous scripts (`Nuclear-Install.bat`)

**Consolidation Map**:

**Driver Setup** (14 scripts → 1):
- ✅ **Keep**: `setup_driver.ps1` (rename to `Setup-Driver.ps1`)
- ❌ **Delete**: `Setup-Driver.bat`, `Install-AvbFilter.ps1`, `Install-Now.bat`, `Install-Debug-Driver.bat`, `Complete-Driver-Setup.bat`, `Fix-And-Install.bat`, `Nuclear-Install.bat`, `install_certificate_method.bat`, `install_devcon_method.bat`, `install_fixed_driver.bat`, `install_ndis_filter.bat`, `install_smart_test.bat`, `reinstall_debug_quick.bat`

**Test Execution** (10 scripts → 1):
- ✅ **Keep**: `test_driver.ps1` (rename to `Test-Driver.ps1`)
- ❌ **Delete**: `Quick-Test-Debug.bat`, `Quick-Test-Release.bat`, `Test-Release.bat`, `Test-Real-Release.bat`, `test_hardware_only.bat`, `test_local_i219.bat`, `test_secure_boot_compatible.bat`, `run_tests.ps1`

**Build Scripts** (6 scripts → 1):
- ✅ **Keep**: `Build-And-Sign-Driver.ps1` (rename to `Build-Driver.ps1`)
- ✅ **Keep**: `Sign-Driver.ps1` (separate signing step)
- ❌ **Move**: `build_all_tests.cmd`, `build_i226_test.bat`, `build_investigation_tools.cmd` → `tools/`

**Certificate Management** (4 scripts → 1):
- ✅ **Keep**: `Manage-Certificate.ps1`
- ❌ **Merge**: `troubleshoot_certificates.ps1`, `Generate-CATFile.ps1`

### Phase 2: Core Consolidation (8 hours)

**1. Create Directory Structure**:
```powershell
New-Item -ItemType Directory -Path scripts/setup
New-Item -ItemType Directory -Path scripts/build
New-Item -ItemType Directory -Path scripts/test
New-Item -ItemType Directory -Path scripts/development
New-Item -ItemType Directory -Path scripts/archive/deprecated
```

**2. Consolidate Setup-Driver.ps1**:
```powershell
# scripts/setup/Setup-Driver.ps1
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [switch]$EnableTestSigning,
    [switch]$InstallDriver,
    [switch]$UninstallDriver,
    [switch]$Update,
    [switch]$Reinstall,
    [switch]$Force,
    [switch]$CheckStatus
)

# Merge functionality from:
# - setup_driver.ps1
# - Install-AvbFilter.ps1
# - Install-Now.bat
# - Complete-Driver-Setup.bat
```

**3. Consolidate Test-Driver.ps1**:
```powershell
# scripts/test/Test-Driver.ps1
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [switch]$Quick,
    [switch]$HardwareOnly,
    [switch]$Diagnostics,
    
    [ValidateSet('I210', 'I217', 'I219', 'I225', 'I226')]
    [string]$Device
)

# Merge functionality from:
# - test_driver.ps1
# - Quick-Test-Debug.bat
# - test_hardware_only.bat
# - run_tests.ps1
```

**4. Consolidate Build-Driver.ps1**:
```powershell
# scripts/build/Build-Driver.ps1
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    
    [switch]$Sign,
    [switch]$Clean,
    [switch]$Tests
)

# Merge functionality from:
# - Build-And-Sign-Driver.ps1
```

### Phase 3: Deprecation Wrappers (3 hours)

**Create 20+ Wrappers** for most-used scripts:

```powershell
# setup_driver.ps1 (deprecation wrapper)
Write-Warning "⚠️  setup_driver.ps1 is DEPRECATED"
Write-Host "📢 Use: .\Setup-Driver.ps1 (or .\scripts\setup\Setup-Driver.ps1)"
Write-Host "🔄 Redirecting to new script in 5 seconds..."
Start-Sleep -Seconds 5

& "$PSScriptRoot\scripts\setup\Setup-Driver.ps1" @PSBoundParameters
```

**Wrappers Required**:
- `setup_driver.ps1` → `Setup-Driver.ps1`
- `Install-AvbFilter.ps1` → `Setup-Driver.ps1 -InstallDriver`
- `Quick-Test-Debug.bat` → `Test-Driver.ps1 -Quick -Configuration Debug`
- `test_hardware_only.bat` → `Test-Driver.ps1 -HardwareOnly`
- `troubleshoot_certificates.ps1` → `Manage-Certificate.ps1 -Troubleshoot`

### Phase 4: Documentation Updates (3 hours)

**Update Files**:

**1. README.md** (lines 160-180):
```markdown
# BEFORE:
.\setup_driver.ps1 -CheckStatus
.\troubleshoot_certificates.ps1

# AFTER:
.\Setup-Driver.ps1 -CheckStatus
.\Manage-Certificate.ps1 -Troubleshoot
```

**2. INSTALLATION_GUIDE.md**:
- Replace all script references
- Update installation flow diagrams

**3. CI/CD Workflows** (`.github/workflows/*.yml`):
```yaml
# BEFORE:
- run: .\Build-And-Sign-Driver.ps1

# AFTER:
- run: .\scripts\build\Build-Driver.ps1 -Sign
```

**4. VS Code Tasks** (`.vscode/tasks.json`):
```json
{
  "label": "Build Driver (Release)",
  "type": "shell",
  "command": ".\\scripts\\build\\Build-Driver.ps1",
  "args": ["-Configuration", "Release", "-Sign"]
}
```

**5. Create MIGRATION_GUIDE.md**:
- Document all script path changes
- Provide migration examples
- List deprecation timeline

### Phase 5: Validation (2 hours)

**Test Suite**:

```powershell
# Test-1: Consolidated Scripts Work
.\scripts\setup\Setup-Driver.ps1 -CheckStatus
.\scripts\test\Test-Driver.ps1 -Quick -Configuration Debug
.\scripts\build\Build-Driver.ps1 -Configuration Release -Sign

# Test-2: Deprecation Wrappers Redirect
.\setup_driver.ps1 -CheckStatus  # Should warn and work
.\Quick-Test-Debug.bat           # Should warn and work

# Test-3: Documentation Links Valid
Get-Content README.md | Select-String '\.\\.*\.ps1' | ForEach-Object {
    $script = $_.Matches.Value
    if (!(Test-Path $script)) {
        Write-Error "Broken link: $script"
    }
}

# Test-4: CI/CD Workflows Execute
gh workflow run ci-standards-compliance.yml
```

---

## Validation and Testing

### Test-1: Script Count Reduction
```powershell
# Count scripts before consolidation
(Get-ChildItem -Filter *.ps1,*.bat).Count  # Expected: 80+

# Count scripts after consolidation (excluding archive/)
(Get-ChildItem scripts/ -Recurse -Filter *.ps1 -Exclude archive).Count
# Expected: ~15
```

### Test-2: Naming Convention Compliance
```powershell
# Verify all scripts follow Verb-Noun.ps1 pattern
Get-ChildItem scripts/ -Recurse -Filter *.ps1 -Exclude archive | Where-Object {
    $_.Name -notmatch '^[A-Z][a-z]+-[A-Z][a-z]+\.ps1$'
}
# Expected: 0 violations
```

### Test-3: Functional Equivalence
```powershell
# Verify old functionality preserved
.\Setup-Driver.ps1 -CheckStatus           # Was: .\setup_driver.ps1 -CheckStatus
.\Test-Driver.ps1 -Quick -Configuration Debug  # Was: .\Quick-Test-Debug.bat
.\Build-Driver.ps1 -Sign                  # Was: .\Build-And-Sign-Driver.ps1
```

### Test-4: Documentation Accuracy
```bash
# Verify all script references in docs exist
grep -r '\\.*\.ps1' README.md INSTALLATION_GUIDE.md docs/ | while read line; do
    script=$(echo "$line" | grep -oP '\\[^\s]+\.ps1')
    [ ! -f "$script" ] && echo "BROKEN: $script"
done
# Expected: 0 broken links
```

---

## Compliance

**Standards**:
- ISO/IEC 25010:2011 (Maintainability - Analyzability, Modifiability)
- PowerShell Best Practices (Microsoft Learn)

**Principles**:
- **Single Responsibility**: Each script has one clear purpose
- **DRY (Don't Repeat Yourself)**: No duplicate functionality
- **Parameter-Driven**: Use parameters instead of separate scripts
- **Explicit Over Implicit**: Clear naming indicates purpose

---

## Traceability

Traces to: 
- #27 (REQ-NF-SCRIPTS-001: Consolidate 80+ Redundant Scripts)

**Related**:
- #22 (REQ-NF-CLEANUP-001: Remove Redundant/Outdated Source Files)
- #24 (REQ-NF-SSOT-001: Single Source of Truth) - Similar consolidation pattern

**Verified by**:
- TEST-SCRIPT-CONS-001: Script count reduced by >70%
- TEST-SCRIPT-NAME-001: All scripts follow naming convention
- TEST-SCRIPT-DEPR-001: Deprecated scripts redirect correctly
- TEST-SCRIPT-DOCS-001: Zero broken script links in documentation

---

## Migration Strategy

### Timeline

**Month 1-2**: Consolidation phase
- Implement consolidated scripts
- Create deprecation wrappers
- Update documentation

**Month 3-8**: Deprecation period (6 months)
- Old scripts work with warnings
- Users migrate at their own pace
- CI/CD updated incrementally

**Month 9+**: Archive phase
- Move deprecated scripts to `scripts/archive/`
- Remove deprecation wrappers
- Only new scripts remain

### User Communication

**Announcement** (via README, CHANGELOG, GitHub Releases):
```markdown
## Script Consolidation (Breaking Change)

We've consolidated 80+ scripts into 15 organized scripts:
- ✅ Driver Setup: Use `Setup-Driver.ps1`
- ✅ Testing: Use `Test-Driver.ps1`
- ✅ Building: Use `Build-Driver.ps1`

**Old scripts will work for 6 months** with deprecation warnings.

See MIGRATION_GUIDE.md for details.
```

---

## Notes

**Migration Effort**: ~18 hours total
- 2 hours: Analysis and mapping
- 8 hours: Core consolidation
- 3 hours: Deprecation wrappers
- 3 hours: Documentation updates
- 2 hours: Validation

**Impact**: HIGH (70% reduction in script count, major UX improvement)

**Breaking Change**: Yes (6-month deprecation period for smooth migration)

**Dangerous Scripts Archived**:
- `Nuclear-Install.bat` (aggressively removes driver without checks)
- `fix_deployment_config.ps1` (modifies system config without validation)
- `install_devcon_method.bat` (uses deprecated devcon.exe tool)

---

**Status**: Accepted  
**Deciders**: Architecture Team  
**Date**: 2025-12-09
