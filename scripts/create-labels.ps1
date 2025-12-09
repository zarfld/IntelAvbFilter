# GitHub Repository Label Configuration Script (PowerShell)
# Purpose: Create all labels needed for GitHub Issues-based requirements tracking
# Standards: ISO/IEC/IEEE 29148:2018 (Requirements Engineering)

# Requirement Type Labels
gh label create "stakeholder-requirement" --description "Business context and stakeholder needs (StR)" --color "0E8A16"
gh label create "functional-requirement" --description "Functional system requirements (REQ-F)" --color "1D76DB"
gh label create "non-functional-requirement" --description "Quality attributes and constraints (REQ-NF)" --color "5319E7"
gh label create "architecture-decision" --description "Architecture decision record (ADR)" --color "F9D0C4"
gh label create "architecture-component" --description "Architecture component specification (ARC-C)" --color "FBCA04"
gh label create "quality-scenario" --description "ATAM quality attribute scenario (QA-SC)" --color "D4C5F9"
gh label create "test-case" --description "Verification and validation test case (TEST)" --color "C5DEF5"

# Phase Labels (Lifecycle Phases)
gh label create "phase-01" --description "Phase 01: Stakeholder Requirements" --color "D93F0B"
gh label create "phase-02" --description "Phase 02: Requirements Analysis" --color "E99695"
gh label create "phase-03" --description "Phase 03: Architecture Design" --color "F9D0C4"
gh label create "phase-04" --description "Phase 04: Detailed Design" --color "FEF2C0"
gh label create "phase-05" --description "Phase 05: Implementation" --color "BFD4F2"
gh label create "phase-06" --description "Phase 06: Integration" --color "C2E0C6"
gh label create "phase-07" --description "Phase 07: Verification & Validation" --color "5319E7"
gh label create "phase-08" --description "Phase 08: Transition/Deployment" --color "1D76DB"
gh label create "phase-09" --description "Phase 09: Operation & Maintenance" --color "0E8A16"

# Priority Labels
gh label create "priority-critical" --description "P0: Critical priority - blocking" --color "B60205"
gh label create "priority-high" --description "P1: High priority" --color "D93F0B"
gh label create "priority-medium" --description "P2: Medium priority" --color "FBCA04"
gh label create "priority-low" --description "P3: Low priority" --color "0E8A16"

# Integrity Level Labels (IEEE 1012-2016)
gh label create "integrity-1" --description "Integrity Level 1: Highest criticality" --color "B60205"
gh label create "integrity-2" --description "Integrity Level 2: High criticality" --color "D93F0B"
gh label create "integrity-3" --description "Integrity Level 3: Medium criticality" --color "FBCA04"
gh label create "integrity-4" --description "Integrity Level 4: Low criticality" --color "0E8A16"

# Status Labels (Workflow States)
gh label create "status-draft" --description "Draft state - work in progress" --color "EDEDED"
gh label create "status-ready" --description "Ready for implementation" --color "C2E0C6"
gh label create "status-blocked" --description "Blocked - waiting on dependency" --color "B60205"
gh label create "status-in-review" --description "Under review" --color "FBCA04"

# Verification Method Labels
gh label create "verify-inspection" --description "Verification by inspection/review" --color "C5DEF5"
gh label create "verify-analysis" --description "Verification by analysis" --color "BFD4F2"
gh label create "verify-demonstration" --description "Verification by demonstration" --color "5319E7"
gh label create "verify-test" --description "Verification by testing" --color "1D76DB"

Write-Host "✅ All labels created successfully!" -ForegroundColor Green
Write-Host "ℹ️  Total: 34 labels (7 types + 9 phases + 4 priorities + 4 integrity + 4 status + 4 verification)" -ForegroundColor Cyan
