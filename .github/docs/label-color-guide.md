# GitHub Issue Label Color Guide

**Purpose**: Define consistent color coding for issue labels across all template repositories.

**Standards**: Visual hierarchy and cognitive load considerations for accessibility.

---

## 🎨 Color Palette Philosophy

### Color Families

| Family | Hex Range | Purpose | Examples |
|--------|-----------|---------|----------|
| **Blue** | `0052CC` - `5B9BD5` | Requirements, standards | StR, REQ-F, REQ-NF, standards |
| **Purple** | `7B68EE` - `D4C5F9` | Architecture, design decisions | ADR, ARC-C, QA-SC, backlog |
| **Green** | `0E8A16` - `7EE787` | Tests, completed, ready | TEST, unit/integration/e2e, status:ready |
| **Orange** | `FF9800` - `FFB74D` | Design, review | Design patterns, status:review |
| **Yellow** | `FBCA04` - `FFF9C4` | In-progress, implementation | status:in-progress, type:implementation |
| **Red** | `B60205` - `D93F0B` | Critical, blocked, bugs | priority:p0, status:blocked, bug |
| **Gray** | `6E7781` - `CFD3D7` | Closed, duplicate, invalid | status:closed, duplicate |
| **Teal/Cyan** | `006B75` - `B3E5FC` | Contexts, enhancements | Bounded Contexts, phase labels |

---

## 📋 Complete Label Definitions

### Requirements Labels (Blue Family)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `type:requirement:stakeholder` | ![#0052CC](https://via.placeholder.com/15/0052CC/0052CC.png) | `0052CC` | Stakeholder requirement (StR) - highest level, business needs |
| `type:requirement:functional` | ![#1D76DB](https://via.placeholder.com/15/1D76DB/1D76DB.png) | `1D76DB` | Functional requirement (REQ-F) - system functionality |
| `type:requirement:non-functional` | ![#5B9BD5](https://via.placeholder.com/15/5B9BD5/5B9BD5.png) | `5B9BD5` | Non-functional requirement (REQ-NF) - quality attributes |

**Rationale**: Blue hierarchy (deep → medium → light) reflects traceability flow: StR → REQ-F → REQ-NF

---

### Architecture Labels (Purple Family)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `type:architecture:decision` | ![#7B68EE](https://via.placeholder.com/15/7B68EE/7B68EE.png) | `7B68EE` | Architecture Decision Record (ADR) - strategic choices |
| `type:architecture:component` | ![#9370DB](https://via.placeholder.com/15/9370DB/9370DB.png) | `9370DB` | Architecture Component (ARC-C) - component specifications |
| `type:architecture:quality-scenario` | ![#BA55D3](https://via.placeholder.com/15/BA55D3/BA55D3.png) | `BA55D3` | Quality Scenario (QA-SC) - ATAM scenarios |

**Rationale**: Purple distinguishes architecture from requirements while maintaining hierarchy.

---

### Test Labels (Green Family)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `type:test` | ![#0E8A16](https://via.placeholder.com/15/0E8A16/0E8A16.png) | `0E8A16` | Test case (TEST) - verification/validation |
| `test-type:unit` | ![#26A641](https://via.placeholder.com/15/26A641/26A641.png) | `26A641` | Unit test - individual components |
| `test-type:integration` | ![#3FB950](https://via.placeholder.com/15/3FB950/3FB950.png) | `3FB950` | Integration test - component interactions |
| `test-type:e2e` | ![#57D467](https://via.placeholder.com/15/57D467/57D467.png) | `57D467` | End-to-end test - complete workflows |
| `test-type:acceptance` | ![#7EE787](https://via.placeholder.com/15/7EE787/7EE787.png) | `7EE787` | Acceptance test - user requirements |

**Rationale**: Green = passing/verification. Gradient from dark (unit) to light (acceptance) reflects scope.

---

### Design Labels (Orange Family)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `type:design` | ![#FF9800](https://via.placeholder.com/15/FF9800/FF9800.png) | `FF9800` | Design specification or pattern |
| `pattern:entity` | ![#FFB74D](https://via.placeholder.com/15/FFB74D/FFB74D.png) | `FFB74D` | DDD Entity pattern |
| `pattern:value-object` | ![#FFB74D](https://via.placeholder.com/15/FFB74D/FFB74D.png) | `FFB74D` | DDD Value Object pattern |
| `pattern:aggregate` | ![#FFB74D](https://via.placeholder.com/15/FFB74D/FFB74D.png) | `FFB74D` | DDD Aggregate pattern |
| `pattern:repository` | ![#FFB74D](https://via.placeholder.com/15/FFB74D/FFB74D.png) | `FFB74D` | DDD Repository pattern |

**Rationale**: Orange = design/planning phase. Light orange for specific patterns.

---

### Status Labels (Multi-color by State)

| Label | Color | Hex | Description | Rationale |
|-------|-------|-----|-------------|-----------|
| `status:backlog` | ![#D4C5F9](https://via.placeholder.com/15/D4C5F9/D4C5F9.png) | `D4C5F9` | Not yet prioritized | Light purple = waiting |
| `status:ready` | ![#0E8A16](https://via.placeholder.com/15/0E8A16/0E8A16.png) | `0E8A16` | Ready for work | Green = go |
| `status:in-progress` | ![#FBCA04](https://via.placeholder.com/15/FBCA04/FBCA04.png) | `FBCA04` | Actively working | Yellow = caution/active |
| `status:review` | ![#FF9800](https://via.placeholder.com/15/FF9800/FF9800.png) | `FF9800` | Awaiting review | Orange = attention needed |
| `status:testing` | ![#1D76DB](https://via.placeholder.com/15/1D76DB/1D76DB.png) | `1D76DB` | In testing | Blue = verification |
| `status:blocked` | ![#B60205](https://via.placeholder.com/15/B60205/B60205.png) | `B60205` | Blocked | Red = stop |
| `status:completed` | ![#0E8A16](https://via.placeholder.com/15/0E8A16/0E8A16.png) | `0E8A16` | Complete & verified | Green = success |
| `status:closed` | ![#6E7781](https://via.placeholder.com/15/6E7781/6E7781.png) | `6E7781` | Closed | Gray = done/archived |

**Rationale**: Color = state emotion (green = go, yellow = caution, red = stop, gray = inactive).

---

### Priority Labels (Red/Orange/Yellow Gradient)

| Label | Color | Hex | Description | Rationale |
|-------|-------|-----|-------------|-----------|
| `priority:p0` | ![#B60205](https://via.placeholder.com/15/B60205/B60205.png) | `B60205` | Critical - must have | Red = urgent/critical |
| `priority:p1` | ![#D93F0B](https://via.placeholder.com/15/D93F0B/D93F0B.png) | `D93F0B` | High - should have | Dark orange = high |
| `priority:p2` | ![#FBCA04](https://via.placeholder.com/15/FBCA04/FBCA04.png) | `FBCA04` | Medium - could have | Yellow = medium |
| `priority:p3` | ![#FEF2C0](https://via.placeholder.com/15/FEF2C0/FEF2C0.png) | `FEF2C0` | Low - nice to have | Light yellow = low |

**Rationale**: Heat map (hot red → warm orange → cool yellow) reflects urgency.

---

### Phase Labels (Pastel Rainbow)

| Label | Color | Hex | Phase |
|-------|-------|-----|-------|
| `phase:01-stakeholder-requirements` | ![#C5DEF5](https://via.placeholder.com/15/C5DEF5/C5DEF5.png) | `C5DEF5` | Phase 01 (light blue) |
| `phase:02-requirements` | ![#C5DEF5](https://via.placeholder.com/15/C5DEF5/C5DEF5.png) | `C5DEF5` | Phase 02 (light blue) |
| `phase:03-architecture` | ![#E4C9F5](https://via.placeholder.com/15/E4C9F5/E4C9F5.png) | `E4C9F5` | Phase 03 (light purple) |
| `phase:04-design` | ![#FFDAB9](https://via.placeholder.com/15/FFDAB9/FFDAB9.png) | `FFDAB9` | Phase 04 (light orange) |
| `phase:05-implementation` | ![#FFF9C4](https://via.placeholder.com/15/FFF9C4/FFF9C4.png) | `FFF9C4` | Phase 05 (light yellow) |
| `phase:06-integration` | ![#FFE5B4](https://via.placeholder.com/15/FFE5B4/FFE5B4.png) | `FFE5B4` | Phase 06 (peach) |
| `phase:07-verification-validation` | ![#C8E6C9](https://via.placeholder.com/15/C8E6C9/C8E6C9.png) | `C8E6C9` | Phase 07 (light green) |
| `phase:08-transition` | ![#B3E5FC](https://via.placeholder.com/15/B3E5FC/B3E5FC.png) | `B3E5FC` | Phase 08 (light cyan) |
| `phase:09-operation-maintenance` | ![#CFD8DC](https://via.placeholder.com/15/CFD8DC/CFD8DC.png) | `CFD8DC` | Phase 09 (light gray) |

**Rationale**: Pastel colors = informational, not actionable. Distinct hues for quick phase identification.

---

### Bounded Context Labels (DDD - Teal Family)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `context:authentication` | ![#006B75](https://via.placeholder.com/15/006B75/006B75.png) | `006B75` | Authentication Bounded Context |
| `context:core-domain` | ![#B60205](https://via.placeholder.com/15/B60205/B60205.png) | `B60205` | Core Domain - competitive advantage |
| `context:supporting` | ![#FBCA04](https://via.placeholder.com/15/FBCA04/FBCA04.png) | `FBCA04` | Supporting Subdomain |
| `context:generic` | ![#D4C5F9](https://via.placeholder.com/15/D4C5F9/D4C5F9.png) | `D4C5F9` | Generic Subdomain - commodity |

**Rationale**: Core = red (critical), Supporting = yellow (important), Generic = light purple (low priority).

---

### Standards Labels (Consistent Blue)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `standard:ISO-29148` | ![#0052CC](https://via.placeholder.com/15/0052CC/0052CC.png) | `0052CC` | ISO/IEC/IEEE 29148:2018 Requirements |
| `standard:IEEE-1016` | ![#0052CC](https://via.placeholder.com/15/0052CC/0052CC.png) | `0052CC` | IEEE 1016-2009 Design Descriptions |
| `standard:ISO-42010` | ![#0052CC](https://via.placeholder.com/15/0052CC/0052CC.png) | `0052CC` | ISO/IEC/IEEE 42010:2011 Architecture |
| `standard:IEEE-1012` | ![#0052CC](https://via.placeholder.com/15/0052CC/0052CC.png) | `0052CC` | IEEE 1012-2016 Verification & Validation |

**Rationale**: Consistent blue = official/standards. Same color for all standards labels.

---

### Management Labels (GitHub Defaults Enhanced)

| Label | Color | Hex | Description |
|-------|-------|-----|-------------|
| `bug` | ![#D73A4A](https://via.placeholder.com/15/D73A4A/D73A4A.png) | `D73A4A` | Something isn't working |
| `enhancement` | ![#A2EEEF](https://via.placeholder.com/15/A2EEEF/A2EEEF.png) | `A2EEEF` | New feature or request |
| `documentation` | ![#0075CA](https://via.placeholder.com/15/0075CA/0075CA.png) | `0075CA` | Documentation improvements |
| `duplicate` | ![#CFD3D7](https://via.placeholder.com/15/CFD3D7/CFD3D7.png) | `CFD3D7` | Issue already exists |
| `invalid` | ![#E4E669](https://via.placeholder.com/15/E4E669/E4E669.png) | `E4E669` | Not valid |
| `wontfix` | ![#FFFFFF](https://via.placeholder.com/15/FFFFFF/FFFFFF.png) | `FFFFFF` | Won't be worked on |
| `help-wanted` | ![#008672](https://via.placeholder.com/15/008672/008672.png) | `008672` | Extra attention needed |
| `good-first-issue` | ![#7057FF](https://via.placeholder.com/15/7057FF/7057FF.png) | `7057FF` | Good for newcomers |
| `question` | ![#D876E3](https://via.placeholder.com/15/D876E3/D876E3.png) | `D876E3` | Further information requested |

**Rationale**: GitHub default colors for familiarity. Users already know these colors.

---

## 🎨 Visual Hierarchy

### Primary (Action Required)
```
Red (B60205) → Orange (FF9800) → Yellow (FBCA04) → Green (0E8A16)
├─ Blocked       ├─ Review      ├─ In Progress   └─ Ready/Complete
└─ Critical      └─ Design      └─ P2             └─ Tests Pass
```

### Secondary (Informational)
```
Blue (0052CC - 5B9BD5) → Requirements, Standards
Purple (7B68EE - BA55D3) → Architecture
Pastel Colors → Phases (background info)
```

### Tertiary (Management)
```
Gray (6E7781) → Closed, Archived
Light Colors → Low priority, generic
```

---

## 🔍 Label Combinations

### Common Patterns

**New Functional Requirement**:
```
type:requirement:functional
+ phase:02-requirements
+ priority:p1
+ status:ready
```

**Architecture Decision for Core Domain**:
```
type:architecture:decision
+ phase:03-architecture
+ context:core-domain
+ status:in-progress
```

**Critical Bug in Testing**:
```
bug
+ priority:p0
+ status:blocked
+ phase:07-verification-validation
```

**Unit Test for Aggregate Pattern**:
```
type:test
+ test-type:unit
+ pattern:aggregate
+ status:completed
```

---

## 📊 Label Usage Statistics (Expected)

| Category | Count | Purpose |
|----------|-------|---------|
| Requirements | 3 | StR, REQ-F, REQ-NF |
| Architecture | 3 | ADR, ARC-C, QA-SC |
| Tests | 5 | TEST + 4 types |
| Design | 5 | Design + 4 patterns |
| Status | 8 | Workflow states |
| Priority | 4 | P0 → P3 |
| Phases | 9 | 01 → 09 |
| Contexts | 4 | Core, Supporting, Generic, Auth |
| Standards | 4 | ISO/IEEE standards |
| Management | 9 | Bug, enhancement, etc. |
| **TOTAL** | **54** | Complete set |

---

## ✅ Setup Instructions

### Option 1: Automated (Recommended)

```bash
# Trigger workflow manually
gh workflow run setup-issue-labels.yml
```

Or push changes to `.github/workflows/setup-issue-labels.yml` to trigger automatically.

### Option 2: Manual via GitHub UI

1. Go to **Issues** → **Labels**
2. Click **New label**
3. Copy name, color (hex without #), and description from tables above
4. Repeat for all 54 labels

### Option 3: GitHub CLI Bulk Import

```bash
# Create labels.json from workflow YAML
# Then import:
gh label create --repo owner/repo --file labels.json
```

---

## 🔄 Maintenance

### When to Update Labels

- **New phase added**: Add `phase:NN-name` with pastel color
- **New Bounded Context**: Add `context:name` with teal or priority color
- **New DDD pattern**: Add `pattern:name` with light orange (`FFB74D`)
- **New standard adopted**: Add `standard:NAME` with blue (`0052CC`)

### Consistency Rules

1. ✅ **Use namespace prefixes**: `type:`, `status:`, `priority:`, `phase:`, `context:`, `pattern:`, `test-type:`
2. ✅ **Lowercase with hyphens**: `non-functional`, not `NonFunctional` or `non_functional`
3. ✅ **Keep descriptions concise**: Under 80 characters
4. ✅ **Maintain color families**: Don't use random colors

---

## 🎓 Training: How to Choose Labels

### For Requirements
1. Start with type: `type:requirement:stakeholder`, `functional`, or `non-functional`
2. Add phase: `phase:01-stakeholder-requirements` or `phase:02-requirements`
3. Add priority: `priority:p0` through `priority:p3`
4. Add status: `status:backlog` → `status:ready` → ...

### For Architecture
1. Start with type: `type:architecture:decision`, `component`, or `quality-scenario`
2. Add phase: `phase:03-architecture`
3. Add context: `context:core-domain`, `supporting`, or `generic`
4. Add status: `status:in-progress` → `status:review` → ...

### For Tests
1. Start with: `type:test`
2. Add test type: `test-type:unit`, `integration`, `e2e`, or `acceptance`
3. Add pattern (if DDD): `pattern:aggregate`, `entity`, etc.
4. Add status: `status:completed` when verified

---

## 🚨 Label Validation and Enforcement

### Automated Validation Rules

The repository enforces label requirements via **`.github/workflows/label-validation.yml`**:

#### Required Labels (Errors - Must Fix)

| Rule | Requirement | Auto-Fix |
|------|-------------|----------|
| **Type Label** | Every issue MUST have at least one `type:*` label | ❌ No (requires human judgment) |
| **Single Priority** | Issues MUST have exactly one `priority:*` label (max 1) | ✅ Yes (removes duplicates) |
| **Single Status** | Issues MUST have exactly one `status:*` label (max 1) | ✅ Yes (removes old status) |

#### Recommended Labels (Warnings - Should Fix)

| Rule | Requirement | Auto-Fix |
|------|-------------|----------|
| **Priority for Non-Bugs** | All non-bug/question issues should have priority | ✅ Yes (infers from type/title) |
| **Test Type** | `type:test` issues should have `test-type:*` | ❌ No (requires human judgment) |
| **Pattern Labels** | `pattern:*` only on design/implementation issues | ❌ No (context-dependent) |

#### Auto-Applied Labels (Info - Automatic)

| Rule | Behavior | When |
|------|----------|------|
| **New Issue Status** | Automatically adds `status:backlog` | Issue opened without status |
| **Closed Issue Status** | Automatically updates to `status:closed` | Issue closed without correct status |
| **Phase Label** | Suggests phase based on type | Issue missing phase label |

### Audit Existing Issues

Use the **audit script** to find issues with missing or incorrect labels:

```bash
# Check all open issues for labeling problems
python scripts/audit-issue-labels.py

# See all problems including info messages
python scripts/audit-issue-labels.py --verbose

# Preview what would be auto-fixed (dry run)
python scripts/audit-issue-labels.py --dry-run

# Automatically fix all auto-fixable issues
python scripts/audit-issue-labels.py --fix-auto
```

#### Audit Report Format

The script categorizes problems by severity:

- **❌ Errors** - Must be fixed (missing type, duplicate priorities)
- **⚠️ Warnings** - Should be fixed (missing priority, test type)
- **ℹ️ Info** - Optional suggestions (missing phase label)
- **🔧 Auto-fixable** - Can be fixed automatically by script

#### Common Issues Found

| Problem | Severity | Fix |
|---------|----------|-----|
| No labels at all | ❌ Error | Manually add `type:*`, `priority:*`, `status:*` |
| Missing type label | ❌ Error | Add appropriate `type:*` label |
| Multiple priorities | ❌ Error | Keep only highest priority |
| Multiple statuses | ❌ Error | Keep only current status |
| Missing priority | ⚠️ Warning | Auto-fix infers from context |
| Missing status | ⚠️ Warning | Auto-fix adds `status:backlog` |
| Missing phase | ℹ️ Info | Auto-fix infers from type |
| Pattern on wrong type | ⚠️ Warning | Remove pattern or add design/implementation type |

### Validation Workflow Triggers

The label validation workflow runs automatically on:

- ✅ **Issue opened** - Validates initial labels
- ✅ **Issue edited** - Re-validates after changes
- ✅ **Issue labeled** - Validates new label additions
- ✅ **Issue unlabeled** - Ensures required labels still present
- ✅ **Issue reopened** - Re-validates status

### Best Practices for Label Compliance

1. **Use Issue Templates** - Pre-populate labels in `.github/ISSUE_TEMPLATE/*.yml`
2. **Review Audit Reports** - Run audit script weekly to catch unlabeled issues
3. **Fix Errors First** - Address ❌ errors before ⚠️ warnings
4. **Let Auto-Fix Work** - Use `--fix-auto` for safe automatic corrections
5. **Manual Judgment** - Some labels (type, test-type) require human decision
6. **Update Status Regularly** - Move issues through workflow (backlog → ready → in-progress → review → testing → completed → closed)

## 🔗 Related Documentation

- [GitHub Issue Workflow](../../docs/github-issue-workflow.md) - Status label usage
- [Issue Templates](../ISSUE_TEMPLATE/) - Default labels per template
- [Traceability Guide](../../docs/ci-cd-workflows.md) - Label-based traceability
- [Label Validation Workflow](../workflows/label-validation.yml) - Automated enforcement
- [Audit Script](../../scripts/audit-issue-labels.py) - Find unlabeled issues

---

**Standards Alignment**: GitHub best practices, WCAG accessibility guidelines (color contrast)

**Version**: 1.1  
**Last Updated**: 2025-11-27  
**Owner**: DevOps Team
