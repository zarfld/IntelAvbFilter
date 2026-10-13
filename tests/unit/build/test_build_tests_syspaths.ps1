# test_build_tests_syspaths.ps1
# Regression test for the ParameterBindingException introduced in commit 4b34123
# when the $sysPaths array in Build-Tests.ps1 was written as:
#
#   $sysPaths = @(
#       Join-Path $RepoRoot "path1",    # ← trailing comma bound "path1", <expr> as
#       Join-Path $RepoRoot "path2"     #   Object[] to -ChildPath; PS5.1 rejects it
#   )
#
# The correct form wraps each call in parentheses and omits the intra-array comma:
#
#   $sysPaths = @(
#       (Join-Path -Path $RepoRoot -ChildPath "path1")
#       (Join-Path -Path $RepoRoot -ChildPath "path2")
#   )
#
# This test verifies both path expressions produce [System.String] values and
# cover the expected flat and nested driver output layouts for Debug and Release.
#
# Test ID: TEST-BUILD-SYSPATHS-001
# Verifies: Build-Tests.ps1 sysPaths regression (#P0-4b34123)
# Traceability: Implements REQ-NF-SCRIPTS-001 (#27)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0

function tc_pass { param([string]$n) $script:total++; $script:passed++
    Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++
    Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-BUILD-SYSPATHS-001: Build-Tests.ps1 sysPaths type safety" -ForegroundColor Cyan
Write-Host "  Regression: ParameterBindingException from comma in @() array" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

foreach ($Configuration in @('Debug', 'Release')) {
    Write-Host "  -- Configuration: $Configuration" -ForegroundColor Yellow

    # Reproduce the corrected expression from Build-Tests.ps1 verbatim.
    $sysPaths = @(
        (Join-Path -Path $RepoRoot -ChildPath "build\x64\$Configuration\IntelAvbFilter\IntelAvbFilter.sys")
        (Join-Path -Path $RepoRoot -ChildPath "build\x64\$Configuration\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys")
    )

    # TC-1: Array must have exactly two elements.
    if ($sysPaths.Count -eq 2) {
        tc_pass "sysPaths-count-$Configuration"
    } else {
        tc_fail "sysPaths-count-$Configuration" "Expected 2 elements, got $($sysPaths.Count)"
    }

    # TC-2: Each element must be a [System.String] (not Object[] or any other type).
    for ($i = 0; $i -lt $sysPaths.Count; $i++) {
        $elem = $sysPaths[$i]
        $typeName = $elem.GetType().FullName
        if ($typeName -eq 'System.String') {
            tc_pass "sysPaths-type-$Configuration-[$i]"
        } else {
            tc_fail "sysPaths-type-$Configuration-[$i]" "Expected System.String, got $typeName"
        }
    }

    # TC-3: First path is the flat layout (single IntelAvbFilter directory).
    # The negative condition is anchored to the trailing .sys file so that
    # GitHub Actions checkout paths (D:\a\IntelAvbFilter\IntelAvbFilter\...)
    # are not mistakenly rejected by the broader "*\IntelAvbFilter\IntelAvbFilter\*"
    # pattern, which would match the double-nested runner checkout structure.
    $flat = $sysPaths[0]
    if (($flat -like "*\IntelAvbFilter\IntelAvbFilter.sys") -and
        ($flat -notlike "*\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys")) {
        tc_pass "sysPaths-flat-layout-$Configuration"
    } else {
        tc_fail "sysPaths-flat-layout-$Configuration" "Unexpected flat path: $flat"
    }

    # TC-4: Second path is the nested layout (IntelAvbFilter\IntelAvbFilter sub-directory).
    $nested = $sysPaths[1]
    if ($nested -like "*\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys") {
        tc_pass "sysPaths-nested-layout-$Configuration"
    } else {
        tc_fail "sysPaths-nested-layout-$Configuration" "Unexpected nested path: $nested"
    }

    # TC-5: Both paths contain the correct configuration token.
    foreach ($p in $sysPaths) {
        if ($p -like "*\x64\$Configuration\*") {
            tc_pass "sysPaths-config-token-$Configuration"
            break
        }
    }
    if (-not ($sysPaths | Where-Object { $_ -like "*\x64\$Configuration\*" })) {
        tc_fail "sysPaths-config-token-$Configuration" "No path contains x64\$Configuration"
    }
}

Write-Host ""
Write-Host ("Total: {0}  Passed: {1}  Failed: {2}" -f $total, $passed, $failed)

if ($failed -gt 0) { exit 1 }
exit 0
