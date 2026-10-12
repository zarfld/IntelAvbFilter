# test_identity_contract.ps1
# Behavioral tests for the HIL driver identity audit classification logic.
#
# The identity audit in ci-standards-compliance.yml classifies a HIL run as:
#   VERIFIED_CURRENT_BUILD  -- both SHA256 hashes present AND equal (string equality).
#   BASELINE_DRIVER_ONLY    -- both hashes present AND different.
#   UNVERIFIED              -- one or both hashes missing/empty.
#
# Critical invariant: the $mismatch flag is supplementary only.
# Missing, empty, or false $mismatch must NOT produce VERIFIED_CURRENT_BUILD
# when the actual hash strings differ.
#
# TEST-IDENTITY-001  equal hashes, mismatch absent  -> VERIFIED_CURRENT_BUILD
# TEST-IDENTITY-002  equal hashes, mismatch=false   -> VERIFIED_CURRENT_BUILD
# TEST-IDENTITY-003  unequal hashes, mismatch absent -> BASELINE_DRIVER_ONLY (NOT VERIFIED)
# TEST-IDENTITY-004  unequal hashes, mismatch=false  -> BASELINE_DRIVER_ONLY (NOT VERIFIED)
# TEST-IDENTITY-005  unequal hashes, mismatch=true   -> BASELINE_DRIVER_ONLY
# TEST-IDENTITY-006  installed hash missing           -> UNVERIFIED
# TEST-IDENTITY-007  build hash missing               -> UNVERIFIED
# TEST-IDENTITY-008  both hashes missing              -> UNVERIFIED
# TEST-IDENTITY-009  equal hashes with whitespace padding -> VERIFIED_CURRENT_BUILD (trim)
#
# Traces to: fix for P0.3 identity audit (#317, #328)
# No hardware, driver, or network required.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$total = 0; $passed = 0; $failed = 0

function tc_pass { param([string]$n)
    $script:total++; $script:passed++
    Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why)
    $script:total++; $script:failed++
    Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-IDENTITY-001..009: HIL Driver Identity Contract" -ForegroundColor Cyan
Write-Host "  Validates direct hash equality (NOT mismatch-flag gate)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------
# Identity classification function -- mirrors ci-standards-compliance.yml logic.
# Returns: "VERIFIED_CURRENT_BUILD", "BASELINE_DRIVER_ONLY", or "UNVERIFIED"
# ------------------------------------------------------------------
function Get-DriverIdentityContract {
    param(
        [string]$InstalledSha,
        [string]$BuildSha,
        [string]$MismatchFlag   # supplementary only; must not be the primary gate
    )

    $installedPresent = -not ([string]::IsNullOrWhiteSpace($InstalledSha))
    $buildPresent     = -not ([string]::IsNullOrWhiteSpace($BuildSha))

    # Primary gate: direct string equality of hash values (trimmed).
    $hashesEqual = $installedPresent -and $buildPresent -and
                   ($InstalledSha.Trim() -eq $BuildSha.Trim())

    if ($hashesEqual) {
        return "VERIFIED_CURRENT_BUILD"
    } elseif ($installedPresent -and $buildPresent) {
        return "BASELINE_DRIVER_ONLY"
    } else {
        return "UNVERIFIED"
    }
}

$sha_a = "AABBCCDD" * 8   # 64-char fake SHA256
$sha_b = "11223344" * 8   # different 64-char fake SHA256

# TEST-IDENTITY-001: equal hashes, mismatch absent -> VERIFIED_CURRENT_BUILD
$r = Get-DriverIdentityContract -InstalledSha $sha_a -BuildSha $sha_a -MismatchFlag ""
if ($r -eq "VERIFIED_CURRENT_BUILD") {
    tc_pass "TEST-IDENTITY-001: equal hashes + absent mismatch -> VERIFIED_CURRENT_BUILD"
} else {
    tc_fail "TEST-IDENTITY-001" ("Expected VERIFIED_CURRENT_BUILD, got '{0}'" -f $r)
}

# TEST-IDENTITY-002: equal hashes, mismatch=false -> VERIFIED_CURRENT_BUILD
$r = Get-DriverIdentityContract -InstalledSha $sha_a -BuildSha $sha_a -MismatchFlag "false"
if ($r -eq "VERIFIED_CURRENT_BUILD") {
    tc_pass "TEST-IDENTITY-002: equal hashes + mismatch=false -> VERIFIED_CURRENT_BUILD"
} else {
    tc_fail "TEST-IDENTITY-002" ("Expected VERIFIED_CURRENT_BUILD, got '{0}'" -f $r)
}

# TEST-IDENTITY-003: unequal hashes, mismatch absent -> BASELINE_DRIVER_ONLY (NOT VERIFIED)
# This is the previously broken case: ($mismatch -ne 'true') was true here
# because $mismatch was empty, so the old code returned VERIFIED_CURRENT_BUILD incorrectly.
$r = Get-DriverIdentityContract -InstalledSha $sha_a -BuildSha $sha_b -MismatchFlag ""
if ($r -eq "BASELINE_DRIVER_ONLY") {
    tc_pass "TEST-IDENTITY-003: unequal hashes + absent mismatch -> BASELINE_DRIVER_ONLY (prev false VERIFIED)"
} else {
    tc_fail "TEST-IDENTITY-003" ("Expected BASELINE_DRIVER_ONLY, got '{0}' (was false VERIFIED in old code)" -f $r)
}

# TEST-IDENTITY-004: unequal hashes, mismatch=false -> BASELINE_DRIVER_ONLY (NOT VERIFIED)
$r = Get-DriverIdentityContract -InstalledSha $sha_a -BuildSha $sha_b -MismatchFlag "false"
if ($r -eq "BASELINE_DRIVER_ONLY") {
    tc_pass "TEST-IDENTITY-004: unequal hashes + mismatch=false -> BASELINE_DRIVER_ONLY"
} else {
    tc_fail "TEST-IDENTITY-004" ("Expected BASELINE_DRIVER_ONLY, got '{0}'" -f $r)
}

# TEST-IDENTITY-005: unequal hashes, mismatch=true -> BASELINE_DRIVER_ONLY
$r = Get-DriverIdentityContract -InstalledSha $sha_a -BuildSha $sha_b -MismatchFlag "true"
if ($r -eq "BASELINE_DRIVER_ONLY") {
    tc_pass "TEST-IDENTITY-005: unequal hashes + mismatch=true -> BASELINE_DRIVER_ONLY"
} else {
    tc_fail "TEST-IDENTITY-005" ("Expected BASELINE_DRIVER_ONLY, got '{0}'" -f $r)
}

# TEST-IDENTITY-006: installed hash missing -> UNVERIFIED
$r = Get-DriverIdentityContract -InstalledSha "" -BuildSha $sha_a -MismatchFlag ""
if ($r -eq "UNVERIFIED") {
    tc_pass "TEST-IDENTITY-006: installed SHA missing -> UNVERIFIED"
} else {
    tc_fail "TEST-IDENTITY-006" ("Expected UNVERIFIED, got '{0}'" -f $r)
}

# TEST-IDENTITY-007: build hash missing -> UNVERIFIED
$r = Get-DriverIdentityContract -InstalledSha $sha_a -BuildSha "" -MismatchFlag ""
if ($r -eq "UNVERIFIED") {
    tc_pass "TEST-IDENTITY-007: build SHA missing -> UNVERIFIED"
} else {
    tc_fail "TEST-IDENTITY-007" ("Expected UNVERIFIED, got '{0}'" -f $r)
}

# TEST-IDENTITY-008: both hashes missing -> UNVERIFIED
$r = Get-DriverIdentityContract -InstalledSha "" -BuildSha "" -MismatchFlag "false"
if ($r -eq "UNVERIFIED") {
    tc_pass "TEST-IDENTITY-008: both SHAs missing -> UNVERIFIED"
} else {
    tc_fail "TEST-IDENTITY-008" ("Expected UNVERIFIED, got '{0}'" -f $r)
}

# TEST-IDENTITY-009: equal hashes with leading/trailing whitespace -> VERIFIED_CURRENT_BUILD
$r = Get-DriverIdentityContract -InstalledSha ("  " + $sha_a + "  ") -BuildSha $sha_a -MismatchFlag ""
if ($r -eq "VERIFIED_CURRENT_BUILD") {
    tc_pass "TEST-IDENTITY-009: equal hashes with whitespace padding -> VERIFIED_CURRENT_BUILD (trim applied)"
} else {
    tc_fail "TEST-IDENTITY-009" ("Expected VERIFIED_CURRENT_BUILD after trim, got '{0}'" -f $r)
}

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-IDENTITY: identity contract regression" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-IDENTITY: all identity contract invariants verified" -ForegroundColor Green
exit 0
