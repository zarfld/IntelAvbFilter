# Migration Prompt: Local Markdown Requirements → GitHub Issues Infrastructure

**Purpose**: Migrate existing requirements artifacts from local Markdown files to GitHub Issues-based traceability infrastructure.

**Standards**: ISO/IEC/IEEE 29148:2018 (Requirements Engineering), ISO/IEC/IEEE 12207:2017 (Configuration Management)

**Target Audience**: Teams with existing requirements documentation in Markdown format who want to adopt the standards-compliant GitHub Issues workflow.

---

## 🎯 Migration Goals

1. **Preserve all requirements content** - No information loss during migration
2. **Establish bidirectional traceability** - Parent/child relationships via GitHub Issues
3. **Maintain version history** - Git history + GitHub Issue timeline
4. **Enable collaborative workflow** - Issue comments, assignments, status labels
5. **Automate where possible** - Scripts for bulk migration

---

## 📋 Pre-Migration Checklist

Before starting migration, ensure:

- [ ] **Backup created**: Full repository backup (including `.git/` folder)
- [ ] **GitHub repository exists**: Remote repository created and accessible
- [ ] **Issue templates installed**: `.github/ISSUE_TEMPLATE/` contains all templates (StR, REQ-F, REQ-NF, ADR, TEST, etc.)
- [ ] **Labels configured**: Status labels, type labels, priority labels exist
- [ ] **GitHub CLI installed**: `gh` command available (`gh --version`)
- [ ] **Authentication configured**: `gh auth status` succeeds
- [ ] **Team trained**: Team understands GitHub Issues workflow (see `docs/github-issue-workflow.md`)
- [ ] **Migration window scheduled**: No active development during migration (recommended)

---

## 🔍 Phase 1: Inventory and Analysis

### Step 1.1: Discover Existing Requirements Files

**Scan your repository for requirements artifacts**:

```bash
# Find all Markdown files in requirements folders
fd -e md . 01-stakeholder-requirements/ 02-requirements/ 03-architecture/decisions/

# Or using PowerShell
Get-ChildItem -Path "01-stakeholder-requirements","02-requirements","03-architecture/decisions" -Filter "*.md" -Recurse | Select-Object FullName
```

**Expected output**:
```
01-stakeholder-requirements/business-context/business-goals.md
01-stakeholder-requirements/stakeholders/stakeholder-register.md
02-requirements/functional/REQ-F-001-user-authentication.md
02-requirements/functional/REQ-F-002-data-validation.md
02-requirements/non-functional/REQ-NF-001-performance.md
03-architecture/decisions/ADR-001-authentication-strategy.md
03-architecture/decisions/ADR-002-database-choice.md
...
```

### Step 1.2: Analyze File Structure

**Examine a sample requirements file**:

```markdown
# REQ-F-001: User Authentication

## Description
The system shall allow users to authenticate using email and password.

## Priority
High

## Acceptance Criteria
- [ ] User can enter email and password
- [ ] System validates credentials against database
- [ ] Successful login redirects to dashboard
- [ ] Failed login shows error message

## Dependencies
- Database schema (User table)
- Email validation service

## Related Requirements
- REQ-F-002 (Password Reset)
- REQ-NF-001 (Performance: login <2 seconds)

## Traceability
- Parent: Business Goal BG-001 (Secure User Access)
- Verified by: TEST-AUTH-001, TEST-AUTH-002
```

**Identify migration mapping**:

| Markdown Section | GitHub Issue Field | Notes |
|------------------|-------------------|-------|
| `# REQ-F-001: ...` | Issue Title | Extract ID and description |
| `## Description` | Issue Body (main text) | Primary content |
| `## Priority` | Label: `priority:high` | Map to priority labels |
| `## Acceptance Criteria` | Issue Body (checklist) | Use GitHub task lists |
| `## Dependencies` | Issue Body + Labels | Add `depends-on` label |
| `## Related Requirements` | Issue Body (links) | Use `#N` syntax |
| `## Traceability / Parent` | Issue Body (`Traces to: #N`) | Establish parent links |
| `## Traceability / Verified by` | Issue Body (`Verified by: #N`) | Establish test links |

### Step 1.3: Create Migration Inventory

**Generate migration inventory spreadsheet**:

```bash
# Create CSV for tracking
echo "File Path,Type,ID,Title,Priority,Status,Has Parent,Has Tests,Migration Status" > migration-inventory.csv

# Example entries
echo "02-requirements/functional/REQ-F-001-user-authentication.md,REQ-F,REQ-F-001,User Authentication,High,Draft,Yes,Yes,Pending" >> migration-inventory.csv
echo "02-requirements/functional/REQ-F-002-data-validation.md,REQ-F,REQ-F-002,Data Validation,Medium,Draft,Yes,No,Pending" >> migration-inventory.csv
```

**Sample inventory**:

| File Path | Type | ID | Title | Priority | Has Parent | Has Tests | Migration Status |
|-----------|------|----|----|----------|------------|-----------|------------------|
| `02-requirements/functional/REQ-F-001-...` | REQ-F | REQ-F-001 | User Authentication | High | Yes | Yes | ✅ Migrated → #12 |
| `02-requirements/functional/REQ-F-002-...` | REQ-F | REQ-F-002 | Data Validation | Medium | Yes | No | ⏳ In Progress |
| `03-architecture/decisions/ADR-001-...` | ADR | ADR-001 | Auth Strategy | N/A | Yes | No | 🔴 Pending |

---

## 🛠️ Phase 2: Automated Migration Script

### Step 2.1: Install Prerequisites

```bash
# Install GitHub CLI (if not already installed)
# Windows (using winget)
winget install --id GitHub.cli

# Or download from https://cli.github.com/

# Install jq (JSON processor) for parsing
winget install jqlang.jq

# Authenticate with GitHub
gh auth login
```

### Step 2.2: Migration Script (PowerShell)

**Create `scripts/migrate-md-to-issues.ps1`**:

```powershell
<#
.SYNOPSIS
    Migrate local Markdown requirements to GitHub Issues
.DESCRIPTION
    Parses Markdown files and creates corresponding GitHub Issues with proper labels, traceability, and content
.PARAMETER RepoOwner
    GitHub repository owner (e.g., 'zarfld')
.PARAMETER RepoName
    GitHub repository name (e.g., 'copilot-instructions-template')
.PARAMETER SourceFolder
    Folder containing Markdown requirements (e.g., '02-requirements/functional')
.PARAMETER IssueType
    Type of issue to create ('stakeholder-requirement', 'functional-requirement', 'non-functional-requirement', 'architecture-decision', 'test-case')
.PARAMETER DryRun
    If specified, only simulate migration without creating issues
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$RepoOwner,
    
    [Parameter(Mandatory=$true)]
    [string]$RepoName,
    
    [Parameter(Mandatory=$true)]
    [string]$SourceFolder,
    
    [Parameter(Mandatory=$true)]
    [ValidateSet('stakeholder-requirement', 'functional-requirement', 'non-functional-requirement', 'architecture-decision', 'test-case')]
    [string]$IssueType,
    
    [switch]$DryRun
)

# Initialize tracking
$migrationLog = @()
$issueMapping = @{}  # Maps file path to issue number

# Get all Markdown files
$mdFiles = Get-ChildItem -Path $SourceFolder -Filter "*.md" -Recurse

Write-Host "Found $($mdFiles.Count) Markdown files to migrate" -ForegroundColor Cyan

foreach ($file in $mdFiles) {
    Write-Host "`nProcessing: $($file.Name)" -ForegroundColor Yellow
    
    # Parse Markdown file
    $content = Get-Content -Path $file.FullName -Raw
    
    # Extract title (first heading)
    if ($content -match '^#\s+(.+)$') {
        $title = $matches[1]
    } else {
        Write-Warning "No title found in $($file.Name), skipping..."
        continue
    }
    
    # Extract sections
    $description = ""
    $priority = "medium"
    $acceptanceCriteria = @()
    $traceabilityParent = ""
    $verifiedBy = @()
    $dependsOn = @()
    
    # Parse Description section
    if ($content -match '(?s)##\s+Description\s*\n(.+?)(?=\n##|\z)') {
        $description = $matches[1].Trim()
    }
    
    # Parse Priority
    if ($content -match '##\s+Priority\s*\n(.+)') {
        $priorityText = $matches[1].Trim()
        $priority = switch -Regex ($priorityText) {
            'Critical|P0' { 'p0' }
            'High|P1' { 'p1' }
            'Medium|P2' { 'p2' }
            'Low|P3' { 'p3' }
            default { 'p2' }
        }
    }
    
    # Parse Acceptance Criteria (task list)
    if ($content -match '(?s)##\s+Acceptance Criteria\s*\n(.+?)(?=\n##|\z)') {
        $criteriaText = $matches[1].Trim()
        $acceptanceCriteria = $criteriaText -split '\n' | Where-Object { $_ -match '^\s*-\s+\[' }
    }
    
    # Parse Traceability - Parent
    if ($content -match 'Parent:\s*(.+)') {
        $traceabilityParent = $matches[1].Trim()
    } elseif ($content -match 'Traces to:\s*(.+)') {
        $traceabilityParent = $matches[1].Trim()
    }
    
    # Parse Verified By
    if ($content -match 'Verified by:\s*(.+)') {
        $verifiedByText = $matches[1].Trim()
        $verifiedBy = $verifiedByText -split '[,\s]+' | Where-Object { $_ -match '\w+' }
    }
    
    # Parse Dependencies
    if ($content -match '(?s)##\s+Dependencies\s*\n(.+?)(?=\n##|\z)') {
        $depsText = $matches[1].Trim()
        $dependsOn = $depsText -split '\n' | Where-Object { $_ -match '^\s*-\s+' } | ForEach-Object { $_.Trim() -replace '^\s*-\s+', '' }
    }
    
    # Build GitHub Issue body
    $issueBody = @"
## Description

$description

## Acceptance Criteria

$($acceptanceCriteria -join "`n")

## Dependencies

$($dependsOn | ForEach-Object { "- $_" } | Out-String)

## Traceability

Traces to:  $traceabilityParent

**Verified by**: $($verifiedBy -join ', ')

---

**Migrated from**: ``$($file.FullName)``  
**Original commit**: $(git log -1 --format="%H" -- $file.FullName)
"@
    
    # Determine labels
    $labels = @("type:$IssueType", "priority:$priority", "phase:migration", "migrated-from-md")
    
    if ($DryRun) {
        Write-Host "  [DRY RUN] Would create issue:" -ForegroundColor Magenta
        Write-Host "    Title: $title" -ForegroundColor White
        Write-Host "    Labels: $($labels -join ', ')" -ForegroundColor White
        Write-Host "    Body length: $($issueBody.Length) chars" -ForegroundColor White
    } else {
        # Create GitHub Issue
        try {
            $issueJson = gh issue create `
                --repo "$RepoOwner/$RepoName" `
                --title "$title" `
                --body "$issueBody" `
                --label ($labels -join ',') `
                --json number,url | ConvertFrom-Json
            
            $issueNumber = $issueJson.number
            $issueUrl = $issueJson.url
            
            Write-Host "  ✅ Created issue #$issueNumber" -ForegroundColor Green
            Write-Host "     URL: $issueUrl" -ForegroundColor Cyan
            
            # Store mapping
            $issueMapping[$file.FullName] = $issueNumber
            
            # Log migration
            $migrationLog += [PSCustomObject]@{
                FilePath = $file.FullName
                IssueNumber = $issueNumber
                IssueUrl = $issueUrl
                Title = $title
                Type = $IssueType
                Priority = $priority
                Status = 'Migrated'
                Timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
            }
            
            # Add original file as comment (for reference)
            $fileComment = @"
### Original File Content

``````markdown
$content
``````

**File preserved in git history**: ``git log --follow -- $($file.FullName)``
"@
            
            gh issue comment $issueNumber --repo "$RepoOwner/$RepoName" --body "$fileComment" | Out-Null
            
            # Small delay to avoid rate limiting
            Start-Sleep -Milliseconds 500
            
        } catch {
            Write-Error "Failed to create issue for $($file.Name): $_"
            $migrationLog += [PSCustomObject]@{
                FilePath = $file.FullName
                IssueNumber = 'N/A'
                IssueUrl = 'N/A'
                Title = $title
                Type = $IssueType
                Priority = $priority
                Status = "Failed: $_"
                Timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
            }
        }
    }
}

# Export migration log
$logFile = "migration-log-$(Get-Date -Format 'yyyyMMdd-HHmmss').csv"
$migrationLog | Export-Csv -Path $logFile -NoTypeInformation

Write-Host "`n✅ Migration complete!" -ForegroundColor Green
Write-Host "   Log saved to: $logFile" -ForegroundColor Cyan
Write-Host "   Total migrated: $($migrationLog.Where({$_.Status -eq 'Migrated'}).Count)" -ForegroundColor Green
Write-Host "   Total failed: $($migrationLog.Where({$_.Status -like 'Failed*'}).Count)" -ForegroundColor Red

# Export issue mapping (for Phase 3: linking)
$mappingFile = "issue-mapping-$(Get-Date -Format 'yyyyMMdd-HHmmss').json"
$issueMapping | ConvertTo-Json | Out-File -FilePath $mappingFile

Write-Host "   Issue mapping saved to: $mappingFile" -ForegroundColor Cyan
```

### Step 2.3: Run Migration (Dry Run First)

```powershell
# Test migration without creating issues
.\scripts\migrate-md-to-issues.ps1 `
    -RepoOwner "zarfld" `
    -RepoName "copilot-instructions-template" `
    -SourceFolder "02-requirements/functional" `
    -IssueType "functional-requirement" `
    -DryRun

# Review output, then run actual migration
.\scripts\migrate-md-to-issues.ps1 `
    -RepoOwner "zarfld" `
    -RepoName "copilot-instructions-template" `
    -SourceFolder "02-requirements/functional" `
    -IssueType "functional-requirement"
```

**Expected output**:
```
Found 25 Markdown files to migrate

Processing: REQ-F-001-user-authentication.md
  ✅ Created issue #12
     URL: https://github.com/zarfld/IntelAvbFilter/issues/12

Processing: REQ-F-002-data-validation.md
  ✅ Created issue #13
     URL: https://github.com/zarfld/IntelAvbFilter/issues/13

...

✅ Migration complete!
   Log saved to: migration-log-20251202-143022.csv
   Total migrated: 25
   Total failed: 0
   Issue mapping saved to: issue-mapping-20251202-143022.json
```

---

## 🔗 Phase 3: Establish Traceability Links

### Step 3.1: Parse Traceability References

**Traceability patterns in Markdown**:
```markdown
## Traceability
- Parent: Business Goal BG-001
- Traces to: StR-001 (Stakeholder Requirement)
- Depends on: REQ-F-005, REQ-F-006
- Verified by: TEST-AUTH-001, TEST-AUTH-002
```

**Goal**: Replace text references with GitHub Issue links (`#N`)

### Step 3.2: Link Issues Script

**Create `scripts/link-migrated-issues.ps1`**:

```powershell
<#
.SYNOPSIS
    Establish traceability links between migrated GitHub Issues
.DESCRIPTION
    Updates issue descriptions to replace text references with GitHub Issue links (#N)
.PARAMETER MappingFile
    JSON file from Phase 2 migration (issue-mapping-*.json)
.PARAMETER RepoOwner
    GitHub repository owner
.PARAMETER RepoName
    GitHub repository name
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$MappingFile,
    
    [Parameter(Mandatory=$true)]
    [string]$RepoOwner,
    
    [Parameter(Mandatory=$true)]
    [string]$RepoName
)

# Load issue mapping (FilePath → IssueNumber)
$mapping = Get-Content -Path $MappingFile | ConvertFrom-Json

# Build reverse mapping (ID → IssueNumber)
$idToIssue = @{}
foreach ($entry in $mapping.PSObject.Properties) {
    $filePath = $entry.Name
    $issueNumber = $entry.Value
    
    # Extract ID from file path (e.g., REQ-F-001 from "REQ-F-001-user-authentication.md")
    if ($filePath -match '([A-Z]+-[A-Z]+-\d+|[A-Z]+-\d+)') {
        $id = $matches[1]
        $idToIssue[$id] = $issueNumber
    }
}

Write-Host "Loaded $($idToIssue.Count) ID → Issue mappings" -ForegroundColor Cyan

# Process each migrated issue
foreach ($entry in $mapping.PSObject.Properties) {
    $issueNumber = $entry.Value
    
    Write-Host "`nProcessing issue #$issueNumber..." -ForegroundColor Yellow
    
    # Get current issue body
    $issue = gh issue view $issueNumber --repo "$RepoOwner/$RepoName" --json body | ConvertFrom-Json
    $body = $issue.body
    
    # Replace text references with #N links
    $updatedBody = $body
    $replacements = 0
    
    foreach ($id in $idToIssue.Keys) {
        $targetIssue = $idToIssue[$id]
        
        # Replace patterns like "REQ-F-001" or "Traces to: REQ-F-001"
        if ($updatedBody -match [regex]::Escape($id)) {
            $updatedBody = $updatedBody -replace [regex]::Escape($id), "#$targetIssue"
            $replacements++
            Write-Host "  Replaced $id → #$targetIssue" -ForegroundColor Green
        }
    }
    
    if ($replacements -gt 0) {
        # Update issue body
        gh issue edit $issueNumber --repo "$RepoOwner/$RepoName" --body "$updatedBody"
        Write-Host "  ✅ Updated issue #$issueNumber ($replacements links)" -ForegroundColor Green
    } else {
        Write-Host "  ⏭️  No changes needed for issue #$issueNumber" -ForegroundColor Gray
    }
    
    Start-Sleep -Milliseconds 300
}

Write-Host "`n✅ Traceability linking complete!" -ForegroundColor Green
```

### Step 3.3: Run Linking

```powershell
.\scripts\link-migrated-issues.ps1 `
    -MappingFile "issue-mapping-20251202-143022.json" `
    -RepoOwner "zarfld" `
    -RepoName "copilot-instructions-template"
```

**Expected output**:
```
Loaded 25 ID → Issue mappings

Processing issue #12...
  Replaced StR-001 → #5
  Replaced TEST-AUTH-001 → #45
  Replaced TEST-AUTH-002 → #46
  ✅ Updated issue #12 (3 links)

Processing issue #13...
  Replaced REQ-F-001 → #12
  ✅ Updated issue #13 (1 links)

...

✅ Traceability linking complete!
```

---

## 📦 Phase 4: Archive Original Files

### Step 4.1: Move Files to Archive

**Preserve original Markdown files for reference**:

```powershell
# Create archive folder
New-Item -ItemType Directory -Force -Path "archive/pre-migration-$(Get-Date -Format 'yyyyMMdd')"

# Move migrated files
$mdFiles = Get-ChildItem -Path "02-requirements" -Filter "*.md" -Recurse

foreach ($file in $mdFiles) {
    $relativePath = $file.FullName -replace [regex]::Escape($PWD), ''
    $archivePath = "archive/pre-migration-$(Get-Date -Format 'yyyyMMdd')$relativePath"
    
    # Create parent directory
    $archiveDir = Split-Path -Path $archivePath -Parent
    New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
    
    # Move file
    Move-Item -Path $file.FullName -Destination $archivePath
    Write-Host "Archived: $($file.Name) → $archivePath" -ForegroundColor Cyan
}

# Commit archive
git add archive/
git commit -m "archive: move pre-migration Markdown requirements to archive

- Migrated to GitHub Issues infrastructure (see migration-log-*.csv)
- Original files preserved in archive/pre-migration-YYYYMMDD/
- Git history preserved: git log --follow -- <path>
- Traceability established via GitHub Issues (#N links)"
```

### Step 4.2: Update Documentation

**Create migration notice in README**:

```markdown
## ⚠️ Migration Notice (December 2025)

This repository has migrated from local Markdown requirements to **GitHub Issues-based traceability**.

### What Changed?
- **Before**: Requirements stored in `02-requirements/*.md` files
- **After**: Requirements tracked as GitHub Issues with proper traceability

### Finding Old Requirements
- **GitHub Issues**: All requirements now tracked at https://github.com/zarfld/IntelAvbFilter/issues
- **Original files**: Archived in `archive/pre-migration-20251202/` (read-only)
- **Git history**: Run `git log --follow -- 02-requirements/functional/REQ-F-001-*.md`

### Migration Resources
- **Migration log**: `migration-log-20251202-143022.csv` (file → issue mapping)
- **Migration guide**: `docs/migration-md-to-github-issues.prompt.md`
- **GitHub workflow**: `docs/github-issue-workflow.md`
```

---

## ✅ Phase 5: Verification and Validation

### Step 5.1: Traceability Validation

**Run traceability validation script**:

```bash
# Validate all traceability links are valid
python scripts/validate-traceability.py --check-issues

# Expected output:
# ✅ REQ-F-001 (#12): Traces to #5 (valid)
# ✅ REQ-F-002 (#13): Traces to #5 (valid), Depends on #12 (valid)
# ❌ REQ-NF-001 (#20): Traces to #999 (invalid - issue not found)
```

### Step 5.2: Coverage Check

**Verify all Markdown files were migrated**:

```powershell
# Compare file count vs. issue count
$mdCount = (Get-ChildItem -Path "archive/pre-migration-*" -Filter "*.md" -Recurse).Count
$issueCount = (Get-Content migration-log-*.csv | Select-Object -Skip 1).Count

Write-Host "Markdown files: $mdCount"
Write-Host "GitHub Issues: $issueCount"

if ($mdCount -eq $issueCount) {
    Write-Host "✅ All files migrated!" -ForegroundColor Green
} else {
    Write-Host "⚠️  Mismatch detected! Review migration log." -ForegroundColor Yellow
}
```

### Step 5.3: Smoke Test

**Test GitHub Issues workflow**:

```bash
# Create a new test requirement
gh issue create \
  --repo zarfld/copilot-instructions-template \
  --title "TEST: Verify GitHub Issues workflow" \
  --body "This is a test issue to verify the migration was successful." \
  --label "type:test,phase:07"

# Link to existing requirement
gh issue comment <issue-number> --body "Related to #12 (User Authentication)"

# Close test issue
gh issue close <issue-number> --comment "Migration verification complete ✅"
```

---

## 🚨 Common Issues and Solutions

### Issue 1: GitHub Rate Limiting

**Symptom**: Migration script fails with "API rate limit exceeded"

**Solution**:
```powershell
# Add delays between API calls (already in script)
Start-Sleep -Milliseconds 500  # Increase to 1000 if needed

# Or split migration into batches
.\migrate-md-to-issues.ps1 -SourceFolder "02-requirements/functional" -IssueType "functional-requirement"
# Wait 1 hour
.\migrate-md-to-issues.ps1 -SourceFolder "02-requirements/non-functional" -IssueType "non-functional-requirement"
```

### Issue 2: Broken Traceability Links

**Symptom**: Issue references like "#999" point to non-existent issues

**Solution**:
```powershell
# Review issue mapping
Get-Content issue-mapping-*.json

# Manually update broken links
gh issue edit 12 --body "Updated traceability: Traces to #5 (corrected)"
```

### Issue 3: Lost Formatting

**Symptom**: Code blocks, tables, or special characters broken in GitHub Issues

**Solution**:
```powershell
# Escape special characters in script
$issueBody = $content -replace '`', '``'  # Escape backticks
$issueBody = $issueBody -replace '\$', '`$'  # Escape dollar signs

# Or preserve original as comment
gh issue comment <issue> --body "See original file in archive/ for formatting"
```

---

## 📚 Post-Migration Best Practices

### 1. Update Team Workflow

**Train team on GitHub Issues workflow**:
- Read `docs/github-issue-workflow.md`
- Practice creating issues with templates
- Understand status labels (`status:backlog`, `status:in-progress`, etc.)

### 2. Automate Status Updates

**Set up GitHub Actions** (see `docs/github-issue-workflow.md`):
- Auto-label new issues
- Update status when PRs are merged
- Close completed issues automatically

### 3. Regular Traceability Audits

**Schedule monthly checks**:
```bash
# Run validation
python scripts/validate-traceability.py --report

# Review orphaned requirements (no parent links)
python scripts/trace_unlinked_requirements.py
```

### 4. Archive Cleanup

**After 6 months, consider removing archive** (if confident in migration):
```bash
# Verify GitHub Issues are authoritative
# Delete archive (optional - recommend keeping for 1 year)
git rm -r archive/pre-migration-20251202/
git commit -m "cleanup: remove pre-migration archive (migration successful, 6+ months old)"
```

---

## 🎯 Success Criteria

Migration is complete when:

- [x] All Markdown requirements migrated to GitHub Issues
- [x] Bidirectional traceability established (`#N` links work)
- [x] Issue templates installed and validated
- [x] Status labels configured and applied
- [x] Team trained on GitHub Issues workflow
- [x] Migration log and issue mapping preserved
- [x] Original files archived with git history intact
- [x] Documentation updated (README, migration notice)
- [x] Traceability validation passes 100%
- [x] No information loss verified

---

## 📖 Related Documentation

- **[GitHub Issue Workflow](github-issue-workflow.md)** - Daily workflow for managing issues
- **[Root Copilot Instructions](../.github/copilot-instructions.md)** - Issue-driven development overview
- **[Traceability Guide](../docs/improvement_ideas/using_github_issues_as_infrastructure_for_REQ_Tracability.md)** - Detailed traceability patterns

---

## 🆘 Need Help?

**Common questions**:

1. **Q: Can I keep Markdown files alongside GitHub Issues?**  
   A: Not recommended - creates dual sources of truth. Choose one: GitHub Issues (recommended for traceability) or Markdown (simpler, but less collaborative).

2. **Q: How do I link requirements after migration?**  
   A: Use `#N` syntax in issue bodies and comments (e.g., "Traces to #12"). GitHub auto-links issues.

3. **Q: What if I find errors after migration?**  
   A: Edit issues directly via GitHub UI or `gh issue edit <number>`. Changes are tracked in issue timeline.

4. **Q: Can I migrate back to Markdown?**  
   A: Yes, but not recommended. Export issues via GitHub API or `gh issue list --json` and regenerate Markdown files.

---

**Version**: 1.0  
**Last Updated**: December 2, 2025  
**Maintained By**: Standards Compliance Team
