# GitHub Repository Label Configuration Script (PowerShell with GitHub API)
# Purpose: Create all labels needed for GitHub Issues-based requirements tracking
# Standards: ISO/IEC/IEEE 29148:2018 (Requirements Engineering)
# Requires: GITHUB_TOKEN environment variable

$token = $env:GITHUB_TOKEN
if (-not $token) {
    Write-Host "ERROR: GITHUB_TOKEN environment variable not set" -ForegroundColor Red
    Write-Host "Please set with: `$env:GITHUB_TOKEN = 'your_token_here'" -ForegroundColor Yellow
    exit 1
}

$repo = "zarfld/copilot-instructions-template"  # Update if different
$apiUrl = "https://api.github.com/repos/$repo/labels"

$headers = @{
    "Authorization" = "Bearer $token"
    "Accept" = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2022-11-28"
}

# Define all labels
$labels = @(
    # Requirement Type Labels
    @{ name = "stakeholder-requirement"; description = "Business context and stakeholder needs (StR)"; color = "0E8A16" }
    @{ name = "functional-requirement"; description = "Functional system requirements (REQ-F)"; color = "1D76DB" }
    @{ name = "non-functional-requirement"; description = "Quality attributes and constraints (REQ-NF)"; color = "5319E7" }
    @{ name = "architecture-decision"; description = "Architecture decision record (ADR)"; color = "F9D0C4" }
    @{ name = "architecture-component"; description = "Architecture component specification (ARC-C)"; color = "FBCA04" }
    @{ name = "quality-scenario"; description = "ATAM quality attribute scenario (QA-SC)"; color = "D4C5F9" }
    @{ name = "test-case"; description = "Verification and validation test case (TEST)"; color = "C5DEF5" }
    
    # Phase Labels
    @{ name = "phase-01"; description = "Phase 01: Stakeholder Requirements"; color = "D93F0B" }
    @{ name = "phase-02"; description = "Phase 02: Requirements Analysis"; color = "E99695" }
    @{ name = "phase-03"; description = "Phase 03: Architecture Design"; color = "F9D0C4" }
    @{ name = "phase-04"; description = "Phase 04: Detailed Design"; color = "FEF2C0" }
    @{ name = "phase-05"; description = "Phase 05: Implementation"; color = "BFD4F2" }
    @{ name = "phase-06"; description = "Phase 06: Integration"; color = "C2E0C6" }
    @{ name = "phase-07"; description = "Phase 07: Verification & Validation"; color = "5319E7" }
    @{ name = "phase-08"; description = "Phase 08: Transition/Deployment"; color = "1D76DB" }
    @{ name = "phase-09"; description = "Phase 09: Operation & Maintenance"; color = "0E8A16" }
    
    # Priority Labels
    @{ name = "priority-critical"; description = "P0: Critical priority - blocking"; color = "B60205" }
    @{ name = "priority-high"; description = "P1: High priority"; color = "D93F0B" }
    @{ name = "priority-medium"; description = "P2: Medium priority"; color = "FBCA04" }
    @{ name = "priority-low"; description = "P3: Low priority"; color = "0E8A16" }
    
    # Integrity Level Labels
    @{ name = "integrity-1"; description = "Integrity Level 1: Highest criticality"; color = "B60205" }
    @{ name = "integrity-2"; description = "Integrity Level 2: High criticality"; color = "D93F0B" }
    @{ name = "integrity-3"; description = "Integrity Level 3: Medium criticality"; color = "FBCA04" }
    @{ name = "integrity-4"; description = "Integrity Level 4: Low criticality"; color = "0E8A16" }
    
    # Status Labels
    @{ name = "status-draft"; description = "Draft state - work in progress"; color = "EDEDED" }
    @{ name = "status-ready"; description = "Ready for implementation"; color = "C2E0C6" }
    @{ name = "status-blocked"; description = "Blocked - waiting on dependency"; color = "B60205" }
    @{ name = "status-in-review"; description = "Under review"; color = "FBCA04" }
    
    # Verification Method Labels
    @{ name = "verify-inspection"; description = "Verification by inspection/review"; color = "C5DEF5" }
    @{ name = "verify-analysis"; description = "Verification by analysis"; color = "BFD4F2" }
    @{ name = "verify-demonstration"; description = "Verification by demonstration"; color = "5319E7" }
    @{ name = "verify-test"; description = "Verification by testing"; color = "1D76DB" }
)

$created = 0
$skipped = 0
$errors = 0

foreach ($label in $labels) {
    try {
        $body = $label | ConvertTo-Json
        $response = Invoke-RestMethod -Uri $apiUrl -Method Post -Headers $headers -Body $body -ContentType "application/json"
        Write-Host "[OK] Created label: $($label.name)" -ForegroundColor Green
        $created++
    }
    catch {
        if ($_.Exception.Response.StatusCode -eq 422) {
            Write-Host "[SKIP] Label already exists: $($label.name)" -ForegroundColor Yellow
            $skipped++
        }
        else {
            Write-Host "[ERROR] Failed to create label: $($label.name) - $($_.Exception.Message)" -ForegroundColor Red
            $errors++
        }
    }
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
Write-Host "Created: $created labels" -ForegroundColor Green
Write-Host "Skipped: $skipped labels (already exist)" -ForegroundColor Yellow
Write-Host "Errors: $errors labels" -ForegroundColor Red
Write-Host "Total: $($labels.Count) labels defined" -ForegroundColor Cyan
