<#
.SYNOPSIS
    Regression tests for Parse-TestResults.ps1 aggregation semantics.

.DESCRIPTION
    Verifies that:
    - INFRA_FAILURE always produces a JUnit <failure/> element (never a pass)
    - SKIP_UNSUPPORTED produces a JUnit <skipped/> element (never a pass)
    - Generic FAIL produces a JUnit <failure/> element
    - PASS produces a clean pass element
    - BLOCKED, CLEANUP_FAILED produce <failure/> elements

    These tests run without hardware or a loaded driver.

$RepoRoot   = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
    Implements: TEST-PROV-L06 (Parse-TestResults aggregation invariants)
    Verifies:   #199, #317 (no false-pass from INFRA_FAILURE)
    Related:    #328 (driver-loss must not produce passing test cases)
#>

Write-Host "  TEST-PROV-L06 - no hardware required" -ForegroundColor Cyan
$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$ParseScript = Join-Path $RepoRoot 'tools\test\Parse-TestResults.ps1'
$TempDir     = Join-Path $env:TEMP "parse_results_regression_$(Get-Random)"
New-Item -ItemType Directory -Path $TempDir | Out-Null

$total  = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host "  [PASS] $n" -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

function Invoke-ParseTest {
    param([string]$LogContent, [string]$ExpectedXmlFragment, [string]$TestName)
    $logFile = Join-Path $TempDir "$TestName.log"
    $xmlFile = Join-Path $TempDir "$TestName.xml"
    $LogContent | Set-Content -Path $logFile -Encoding UTF8

    & $ParseScript -LogFile $logFile -OutputFile $xmlFile 2>$null
    if (-not (Test-Path $xmlFile)) {
        tc_fail $TestName "Parse-TestResults.ps1 produced no XML output"
        return
    }
    $xml = Get-Content $xmlFile -Raw
    if ($xml -like "*$ExpectedXmlFragment*") {
        tc_pass $TestName
    } else {
        tc_fail $TestName "XML did not contain [$ExpectedXmlFragment].`nActual XML:`n$xml"
    }
}

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  Parse-TestResults Aggregation Regression" -ForegroundColor Cyan
Write-Host "  TEST-PROV-L06 â€” no hardware required" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-01: INFRA_FAILURE must produce <failure/>, never a pass
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-01-infra-failure-is-failure" `
  -LogContent "  [INFRA_FAILURE] TEST-PROV-H01: driver absent on mandatory HIL runner" `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-02: SKIP_UNSUPPORTED must produce <skipped/>, never a pass or failure
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-02-skip-unsupported-is-skip" `
  -LogContent "  [SKIP_UNSUPPORTED] TEST-PROV-H03: adapter lacks INTEL_CAP_BASIC_1588" `
  -ExpectedXmlFragment "<skipped"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-03: Mix of INFRA_FAILURE and PASS â†’ still failure (any INFRA_FAILURE wins)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-03-infra-plus-pass-is-failure" `
  -LogContent @"
  [PASS] TEST-PROV-H01/adapter:0: no self-comparison detected
  [INFRA_FAILURE] TEST-PROV-H01/adapter:1: driver lost mid-suite
"@ `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-04: BLOCKED must produce <failure/> (prerequisite not met is blocking)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-04-blocked-is-failure" `
  -LogContent "  [BLOCKED] TEST-PROV-H05: required test executable not found" `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-05: CLEANUP_FAILED must produce <failure/>
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Invoke-ParseTest -TestName "L06-05-cleanup-failed-is-failure" `
  -LogContent "  [CLEANUP_FAILED] UT-CORR-005: PHC restore failed - hardware state uncertain" `
  -ExpectedXmlFragment "<failure"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-06: Pure PASS produces no <failure/> and no <skipped/>
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$logFile06 = Join-Path $TempDir "L06-06-pass.log"
$xmlFile06 = Join-Path $TempDir "L06-06-pass.xml"
"  [PASS] TEST-PROV-H01: no self-comparison regression detected" | Set-Content $logFile06 -Encoding UTF8
& $ParseScript -LogFile $logFile06 -OutputFile $xmlFile06 2>$null
if (Test-Path $xmlFile06) {
    $xml06 = Get-Content $xmlFile06 -Raw
    if ($xml06 -match 'failures="0"' -and $xml06 -match 'skipped="0"') {
        tc_pass "L06-06-pure-pass-no-failure-no-skip"
    } else {
        tc_fail "L06-06-pure-pass-no-failure-no-skip" "PASS result incorrectly produced failure/skip element"
    }
} else {
    tc_fail "L06-06-pure-pass-no-failure-no-skip" "No XML output for PASS log"
}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# L06-07: INFRA_FAILURE in mandatory HIL context â€” outcome must be failures=1
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$logFile07 = Join-Path $TempDir "L06-07-infra.log"
$xmlFile07 = Join-Path $TempDir "L06-07-infra.xml"
"  [INFRA_FAILURE] TEST-PROV-H02/adapter:0: no packets sent on capable hardware" | Set-Content $logFile07 -Encoding UTF8
& $ParseScript -LogFile $logFile07 -OutputFile $xmlFile07 2>$null
if (Test-Path $xmlFile07) {
    $xml07 = Get-Content $xmlFile07 -Raw
    if ($xml07 -match 'failures="1"' -and $xml07 -notmatch 'failures="0"') {
        tc_pass "L06-07-infra-failure-count-is-1"
    } else {
        tc_fail "L06-07-infra-failure-count-is-1" "failures attribute is not 1 in XML`n$xml07"
    }
} else {
    tc_fail "L06-07-infra-failure-count-is-1" "No XML output"
}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# Cleanup
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#
# ---------------------------------------------------------------------------
# L06-08 .. L06-17: verdict contract (SKIP_UNSUPPORTED / SKIP_NOT_IMPLEMENTED /
# INCONCLUSIVE).  See docs/testing/test-verdict-contract.md.
#
#   SKIP_UNSUPPORTED     hardware genuinely lacks the capability  -> <skipped>, green
#   SKIP_NOT_IMPLEMENTED hardware supports it, driver does not    -> <skipped>, but
#                        the case is INCOMPLETE: a mandatory run exits non-zero
#   INCONCLUSIVE         insufficient evidence                    -> <failure> when
#                        mandatory, <skipped> (label kept) when -OptionalVerification
#   FAIL / BLOCKED / CLEANUP_FAILED / INFRA_FAILURE               -> <failure>
# ---------------------------------------------------------------------------
function Invoke-ParseRaw {
    param([string]$TestName, [string]$LogContent, [switch]$Optional)
    $logFile = Join-Path $TempDir "$TestName.log"
    $xmlFile = Join-Path $TempDir "$TestName.xml"
    $LogContent | Set-Content -Path $logFile -Encoding UTF8
    $code = -1; $err = ''
    try {
        $global:LASTEXITCODE = 0
        if ($Optional) { & $ParseScript -LogFile $logFile -OutputFile $xmlFile -OptionalVerification *>$null }
        else           { & $ParseScript -LogFile $logFile -OutputFile $xmlFile *>$null }
        $code = $LASTEXITCODE
    } catch { $err = $_.Exception.Message }
    $xml = if (Test-Path $xmlFile) { Get-Content $xmlFile -Raw } else { '' }
    # The generated header comment documents the element names; match on elements only.
    $xml = [regex]::Replace($xml, '(?s)<!--.*?-->', '')
    return @{ Xml = $xml; Exit = $code; Error = $err }
}

function Assert-Parse {
    param([string]$Name, [hashtable]$R, [string[]]$Match = @(), [string[]]$NoMatch = @(), [int]$ExitCode = -99)
    $why = @()
    if ($R.Error) { $why += "parser error: $($R.Error)" }
    foreach ($m in $Match)   { if ($R.Xml -notmatch $m) { $why += "missing /$m/" } }
    foreach ($m in $NoMatch) { if ($R.Xml -match $m)    { $why += "unexpected /$m/" } }
    if ($ExitCode -ne -99 -and $R.Exit -ne $ExitCode) { $why += "exit=$($R.Exit) expected $ExitCode" }
    if ($why.Count -eq 0) { tc_pass $Name } else { tc_fail $Name (($why -join '; ') + "`nXML:`n$($R.Xml)") }
}

# L06-08: SKIP_NOT_IMPLEMENTED alone -> JUnit skipped, labelled, never a pass; mandatory exit 1
$r = Invoke-ParseRaw 'L06-08' '  [SKIP_NOT_IMPLEMENTED] TC-TAS-001/adapter:0: ntstatus=0xC0000002 did=0x125B DISARM_TAS not implemented'
Assert-Parse 'L06-08-not-implemented-is-labelled-skip-and-incomplete' $r `
    -Match @('<skipped message="[^"]*\[SKIP_NOT_IMPLEMENTED\]', 'failures="0"', 'skipped="1"') -ExitCode 1

# L06-09: PASS on adapter 0 + NOT_IMPLEMENTED on adapter 1 is NOT "PARTIAL COVERAGE" pass
$r = Invoke-ParseRaw 'L06-09' @"
  [PASS] TC-X-001/adapter:0: ok
  [SKIP_NOT_IMPLEMENTED] TC-X-001/adapter:1: ntstatus=0xC0000002
"@
Assert-Parse 'L06-09-pass-plus-not-implemented-is-incomplete-not-partial-pass' $r `
    -Match @('<skipped message="incomplete \(SKIP_NOT_IMPLEMENTED\)', 'passed on \[adapter:0\]', 'adapter:1: \[SKIP_NOT_IMPLEMENTED\]') `
    -NoMatch @('PARTIAL COVERAGE') -ExitCode 1

# L06-10: -OptionalVerification keeps the label and the skipped element but does not fail the run
$r = Invoke-ParseRaw 'L06-10' '  [SKIP_NOT_IMPLEMENTED] TC-X-002/adapter:0: ntstatus=0xC0000002' -Optional
Assert-Parse 'L06-10-optional-not-implemented-keeps-label-exit-0' $r `
    -Match @('<skipped message="[^"]*\[SKIP_NOT_IMPLEMENTED\]', 'failures="0"') -ExitCode 0

# L06-11: SKIP_UNSUPPORTED keeps its label; all-unsupported is a green skip; PASS+UNSUPPORTED stays PARTIAL
$r = Invoke-ParseRaw 'L06-11a' @"
  [SKIP_UNSUPPORTED] TC-X-003/adapter:0: ntstatus=0xC00000BB did=0x1533 lacks capability bit
  [SKIP_UNSUPPORTED] TC-X-003/adapter:1: ntstatus=0xC00000BB did=0x15BB lacks capability bit
"@
Assert-Parse 'L06-11a-all-unsupported-is-labelled-green-skip' $r `
    -Match @('<skipped message="[^"]*\[SKIP_UNSUPPORTED\]', 'failures="0"') -ExitCode 0
$r = Invoke-ParseRaw 'L06-11b' @"
  [PASS] TC-X-004/adapter:0: ok
  [SKIP_UNSUPPORTED] TC-X-004/adapter:1: ntstatus=0xC00000BB
"@
Assert-Parse 'L06-11b-pass-plus-unsupported-is-partial-coverage-pass' $r `
    -Match @('PARTIAL COVERAGE') -NoMatch @('<skipped', '<failure') -ExitCode 0

# L06-12: INCONCLUSIVE - failure when mandatory, labelled skip when optional
$r = Invoke-ParseRaw 'L06-12a' '  [INCONCLUSIVE] TC-X-005/adapter:0: capability unknown; ntstatus=0xC00000BB'
Assert-Parse 'L06-12a-inconclusive-mandatory-is-failure' $r `
    -Match @('<failure[^>]*type="INCONCLUSIVE"', '\[INCONCLUSIVE\]', 'failures="1"') -ExitCode 1
$r = Invoke-ParseRaw 'L06-12b' '  [INCONCLUSIVE] TC-X-005/adapter:0: capability unknown; ntstatus=0xC00000BB' -Optional
Assert-Parse 'L06-12b-inconclusive-optional-is-labelled-skip-not-verified' $r `
    -Match @('<skipped message="[^"]*\[INCONCLUSIVE\]', 'failures="0"') -NoMatch @('<failure') -ExitCode 0

# L06-13: FAIL and NOT_IMPLEMENTED on the same testcase: the failure wins and the gap stays visible
$r = Invoke-ParseRaw 'L06-13' @"
  [FAIL] TC-X-006/adapter:0: invalid provenance
  [SKIP_NOT_IMPLEMENTED] TC-X-006/adapter:1: ntstatus=0xC0000002
"@
Assert-Parse 'L06-13-fail-wins-over-not-implemented' $r `
    -Match @('<failure[^>]*message="[^"]*invalid provenance', 'SKIP_NOT_IMPLEMENTED', 'failures="1"') -ExitCode 1

# L06-14: the exact failure class survives normalisation (type attribute)
$r = Invoke-ParseRaw 'L06-14a' '  [BLOCKED] TC-X-007/adapter:0: no safe restore path'
Assert-Parse 'L06-14a-blocked-type-preserved' $r -Match @('<failure[^>]*type="BLOCKED"') -ExitCode 1
$r = Invoke-ParseRaw 'L06-14b' '  [FAIL] TC-X-008/adapter:0: wrong'
Assert-Parse 'L06-14b-fail-type-preserved' $r -Match @('<failure[^>]*type="FAIL"') -ExitCode 1

# L06-15: testcase id, adapter identity and the original NTSTATUS survive into the XML
$r = Invoke-ParseRaw 'L06-15' '  [SKIP_NOT_IMPLEMENTED] TC-TAS-009/adapter:2: did=0x125B ntstatus=0xC0000002 op=DISARM_TAS'
Assert-Parse 'L06-15-evidence-ids-adapter-ntstatus-preserved' $r `
    -Match @('name="TC-TAS-009"', 'adapter:2', 'did=0x125B', 'ntstatus=0xC0000002')

# L06-16: legacy "[SKIP] ID: SKIP_UNSUPPORTED ..." text form (used by existing C tests) keeps its class
$r = Invoke-ParseRaw 'L06-16a' '  [SKIP] TEST-PROV-H04/adapter:0: SKIP_UNSUPPORTED: adapter lacks INTEL_CAP_BASIC_1588'
Assert-Parse 'L06-16a-legacy-skip-unsupported-text-is-labelled' $r -Match @('<skipped message="[^"]*\[SKIP_UNSUPPORTED\]') -ExitCode 0
$r = Invoke-ParseRaw 'L06-16b' '  [SKIP] TC-X-010/adapter:0: SKIP_NOT_IMPLEMENTED: driver op missing'
Assert-Parse 'L06-16b-legacy-skip-not-implemented-text-is-incomplete' $r -Match @('\[SKIP_NOT_IMPLEMENTED\]') -ExitCode 1

# L06-17: regression guard - plain [SKIP] (e.g. link down) + PASS remains PARTIAL COVERAGE pass
$r = Invoke-ParseRaw 'L06-17' @"
  [PASS] TC-X-011/adapter:0: ok
  [SKIP] TC-X-011/adapter:1: link down
"@
Assert-Parse 'L06-17-plain-skip-plus-pass-unchanged' $r -Match @('PARTIAL COVERAGE') -NoMatch @('<skipped', '<failure') -ExitCode 0

Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`n========================================================" -ForegroundColor Cyan
Write-Host "  Summary: $passed/$total passed, $failed failed" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-PROV-L06: $failed aggregation regression(s) detected" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-PROV-L06: all aggregation invariants correct" -ForegroundColor Green
exit 0
