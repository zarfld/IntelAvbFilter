# Label Validation Quick Reference

**Purpose**: Ensure all GitHub Issues have proper labels for traceability and automation.

## 🚨 Validation Rules (Enforced Automatically)

### Required Labels (❌ Errors)

| Label Type | Requirement | Example |
|------------|-------------|---------|
| **Type** | Exactly 1 required | `type:requirement:functional` |
| **Priority** | Exactly 1 required* | `priority:p1` |
| **Status** | Exactly 1 required | `status:in-progress` |

*Bugs, questions, and documentation issues may skip priority.

### Recommended Labels (⚠️ Warnings)

| Label Type | When Required | Example |
|------------|---------------|---------|
| **Test Type** | When `type:test` | `test-type:unit` |
| **Phase** | All issues | `phase:02-requirements` |

### Auto-Applied Labels (✅ Automatic)

| Scenario | Label Applied | When |
|----------|---------------|------|
| New issue opened | `status:backlog` | If no status present |
| Issue closed | `status:closed` | If not already closed status |
| Issue with type | Suggested phase | If no phase present |

## 🔧 Using the Audit Script

### Find All Problems

```bash
# Check all open issues
python scripts/audit-issue-labels.py

# Include closed issues
python scripts/audit-issue-labels.py --state all

# Show all details (including info messages)
python scripts/audit-issue-labels.py --verbose
```

### Preview Fixes (Dry Run)

```bash
# See what would be fixed without making changes
python scripts/audit-issue-labels.py --dry-run
```

### Auto-Fix Issues

```bash
# Automatically fix safe corrections:
# - Add missing status:backlog
# - Add missing priority (inferred)
# - Add missing phase (inferred)
# - Remove duplicate priorities
# - Update closed issues to status:closed
python scripts/audit-issue-labels.py --fix-auto
```

### Using with GitHub Token

```bash
# Set token from environment
export GITHUB_TOKEN="ghp_yourtoken"
python scripts/audit-issue-labels.py

# Or pass directly
python scripts/audit-issue-labels.py --token ghp_yourtoken

# Specify repository explicitly
python scripts/audit-issue-labels.py --owner zarfld --repo copilot-instructions-template
```

## 📊 Understanding Audit Output

### Severity Levels

- **❌ Errors** - Must fix (blocks automation, breaks workflows)
- **⚠️ Warnings** - Should fix (improves organization, enables features)
- **ℹ️ Info** - Optional (nice-to-have, suggestions)
- **🔧 Auto-fixable** - Can be fixed automatically

### Example Output

```
📊 LABEL AUDIT REPORT
================================================================================

Total Issues: 15
Total Problems: 23
  ❌ Errors: 3
  ⚠️  Warnings: 12
  ℹ️  Info: 8
  🔧 Auto-fixable: 18

--------------------------------------------------------------------------------
❌ ERRORS (Must Fix)
--------------------------------------------------------------------------------

#42: Implement user authentication
  Rule: Missing Type Label
  Problem: Every issue must have at least one type:* label
  Current labels: priority:p1, status:in-progress
  Action: Add appropriate type label (e.g., type:requirement:functional)
  🔧 Auto-fixable: No

#56: Add password reset feature
  Rule: Multiple Priority Labels
  Problem: Issue has 2 priority labels: priority:p1, priority:p2
  Current labels: type:requirement:functional, priority:p1, priority:p2
  Action: Keep only one priority label (highest priority)
  🔧 Auto-fixable: Yes
```

## 🎯 Common Fixes

### Issue Has No Labels

**Problem**: Issue created without any labels

**Fix**: Manually add required labels
```bash
# Using GitHub CLI
gh issue edit 42 --add-label "type:requirement:functional,priority:p1,status:backlog"
```

**Or**: Let validation workflow auto-add `status:backlog`, then add type and priority manually

### Issue Missing Type Label

**Problem**: Cannot auto-fix (requires human judgment)

**Fix**: Determine issue type and add appropriate label
- Requirements: `type:requirement:functional` or `type:requirement:non-functional`
- Architecture: `type:architecture:decision` or `type:architecture:component`
- Tests: `type:test`
- Design: `type:design`
- Implementation: `type:implementation`

### Issue Has Multiple Priorities

**Problem**: Duplicate priority labels (e.g., both `priority:p1` and `priority:p2`)

**Fix**: Auto-fixable - run `--fix-auto` or manually remove lower priority

```bash
# Auto-fix
python scripts/audit-issue-labels.py --fix-auto

# Or manually
gh issue edit 56 --remove-label "priority:p2"
```

### Test Issue Missing Test Type

**Problem**: Issue has `type:test` but no `test-type:*`

**Fix**: Add appropriate test type
- Unit tests: `test-type:unit`
- Integration tests: `test-type:integration`
- End-to-end tests: `test-type:e2e`
- Acceptance tests: `test-type:acceptance`

```bash
gh issue edit 78 --add-label "test-type:unit"
```

### Closed Issue Has Wrong Status

**Problem**: Issue closed but still has `status:in-progress` or `status:review`

**Fix**: Auto-fixable - run `--fix-auto` or manually update

```bash
# Auto-fix
python scripts/audit-issue-labels.py --fix-auto

# Or manually
gh issue edit 90 --remove-label "status:in-progress" --add-label "status:closed"
```

## 📋 Workflow Behavior

### When Validation Runs

The label-validation.yml workflow triggers on:
- ✅ Issue opened
- ✅ Issue edited
- ✅ Issue reopened
- ✅ Label added
- ✅ Label removed

### What Happens

1. **Checks all validation rules** (10 rules)
2. **Auto-fixes safe issues** (status:backlog, status:closed)
3. **Posts comment** if errors or warnings found
4. **Fails workflow** if critical errors present

### Example Validation Comment

When an issue violates rules, the workflow posts a comment:

```markdown
## 🏷️ Label Validation Report

### ❌ Errors (Must Fix)

**Missing Type Label**
- Every issue must have at least one `type:*` label
- **Action**: Add one of: `type:requirement:functional`, `type:architecture:decision`, `type:test`, etc.

### 💡 Suggestions (Auto-Applied or Optional)

**Auto-assign Status**
- New issue without status will be assigned `status:backlog`
- Status label will be added automatically

---

### 📚 Label Guidelines

**Required Labels**:
- `type:*` - Issue type (requirement, architecture, test, etc.)
- `priority:*` - Priority level (p0-p3)
- `status:*` - Current status (backlog, ready, in-progress, etc.)

See [Label Color Guide](.github/docs/label-color-guide.md) for complete reference.
```

## 🎓 Best Practices

### Creating New Issues

1. **Use issue templates** - Pre-populated with correct labels
2. **Add all required labels** - Type, priority, status
3. **Add recommended labels** - Phase, context (if applicable)
4. **Verify traceability** - Link to parent issues

### Maintaining Existing Issues

1. **Run audit weekly** - `python scripts/audit-issue-labels.py`
2. **Fix errors first** - Address ❌ before ⚠️
3. **Use auto-fix** - Safe corrections applied automatically
4. **Update status regularly** - Move through workflow stages
5. **Remove old status labels** - Keep only current status

### Label Hygiene

✅ **Do**:
- Use predefined labels from `setup-issue-labels.yml`
- Update status as work progresses
- Add phase labels to track lifecycle stage
- Use test-type for all test issues
- Let automation fix safe issues

❌ **Don't**:
- Create ad-hoc labels
- Assign multiple priorities
- Assign multiple statuses
- Leave issues without type label
- Ignore validation comments

## 🔗 Related Files

- **Workflow**: `.github/workflows/label-validation.yml`
- **Audit Script**: `scripts/audit-issue-labels.py`
- **Label Guide**: `.github/docs/label-color-guide.md`
- **Label Setup**: `.github/workflows/setup-issue-labels.yml`
- **Traceability**: `.github/workflows/issue-validation.yml`

---

**Quick Commands**:
```bash
# Check issues
python scripts/audit-issue-labels.py

# Preview fixes
python scripts/audit-issue-labels.py --dry-run

# Apply fixes
python scripts/audit-issue-labels.py --fix-auto

# Check closed issues too
python scripts/audit-issue-labels.py --state all --fix-auto
```
