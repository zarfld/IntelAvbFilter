# test_codeql_driver_config.ps1
# TEST-CODEQLCFG-*: offline contract of the CodeQL driver-analysis configuration (no CodeQL CLI, no network, no driver).
#
# Regression for CodeQL init failure "The QL pack 'microsoft/cpp-queries@0.0.5' which is referenced from
# .../windows-drivers/1.10.0/windows-driver-suites/mustfix.qls cannot be found" (runs 38997424025 .. 40782452229,
# 2026-10-17 onwards).  Cause: the analyze job consumed MSDT config/codeql-config.yml@development, which floats
# (MSDT 878c2db5b2 moved cpp-queries to 0.0.6) and itself floats windows-drivers@1.x (resolves to the published 1.10.0,
# whose suites pin cpp-queries 0.0.5).
#
# Checks (static):
#   - the analyze job uses the repository-controlled config, never a remote/moving one;
#   - every pack reference is an exact version; required coverage entries are present;
#   - the reviewed query inventory parses and is consistent with the pinned versions;
#   - the real-CodeQL dependency validation (tools/ci/Test-CodeQLDriverPacks.ps1) runs BEFORE init and analyze runs
#     after init, with no continue-on-error, and config/inventory changes trigger the workflow;
#   - the WHCP job stays independent of the new config;
#   - the failure classifier separates registry/network failures from dependency incompatibility on observed messages.
# A pass is NOT evidence that the packs resolve or that analysis runs: that is tier 2/3 (tools/ci/Test-CodeQLDriverPacks.ps1
# with a real CodeQL CLI, in .github/workflows/codeql-driver.yml).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""
Write-Host "TEST-CODEQLCFG: CodeQL driver configuration contract (offline, static)" -ForegroundColor Cyan

$WorkflowRel  = '.github/workflows/codeql-driver.yml'
$ConfigRel    = '.github/codeql/codeql-driver-config.yml'
$InventoryRel = '.github/codeql/codeql-driver-inventory.txt'
$LibRel       = 'tools/lib/CodeQLDriverPacks.ps1'
$ToolRel      = 'tools/ci/Test-CodeQLDriverPacks.ps1'

function Read-RepoText([string]$Rel) {
    $p = Join-Path $RepoRoot ($Rel -replace '/', '\')
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { return $null }
    return [System.IO.File]::ReadAllText($p)
}

# Required coverage (pack + path, version-independent).  Removing an entry is a coverage reduction and needs an
# explicit, reviewed change of this list -- never a silent config edit.
$RequiredEntries = @(
    'microsoft/cpp-queries:codeql-suites/cpp-code-scanning.qls',
    'microsoft/windows-drivers:windows-driver-suites/recommended.qls',
    'microsoft/windows-drivers:drivers/general/queries/experimental/DriverIsolationZwViolation1/DriverIsolationZwViolation1.ql',
    'microsoft/windows-drivers:drivers/general/queries/experimental/DriverIsolationZwViolation2/DriverIsolationZwViolation2.ql',
    'microsoft/windows-drivers:drivers/general/queries/experimental/DriverIsolationRtlViolation/DriverIsolationRtlViolation.ql'
)

# 001: library loads (pure functions shared with the real-CodeQL tool).
$libText = Read-RepoText $LibRel
tc_check 'TEST-CODEQLCFG-001-library-present' ($null -ne $libText) "$LibRel missing"
$libOk = $false
if ($null -ne $libText) {
    try { . (Join-Path $RepoRoot ($LibRel -replace '/', '\')); $libOk = $true } catch { tc_check 'TEST-CODEQLCFG-001b-library-loads' $false $_.Exception.Message }
}

# 002: analyze job uses the repository config; no remote/moving config anywhere in the analyze job.
$wf = Read-RepoText $WorkflowRel
$analyze = $null; $whcp = $null
if ($null -ne $wf) {
    $m = [regex]::Match($wf, '(?ms)^  analyze:\s*$(.*?)(?=^  analyze-whcp:\s*$)')
    if ($m.Success) { $analyze = $m.Groups[1].Value }
    $m2 = [regex]::Match($wf, '(?ms)^  analyze-whcp:\s*$(.*)\z')
    if ($m2.Success) { $whcp = $m2.Groups[1].Value }
}
tc_check 'TEST-CODEQLCFG-002a-analyze-job-found' ($null -ne $analyze) "no 'analyze:' job block before 'analyze-whcp:' in $WorkflowRel"
$cfgFiles = @(); if ($null -ne $analyze) { $cfgFiles = @([regex]::Matches($analyze, '(?m)^\s*config-file:\s*(\S+)\s*$') | ForEach-Object { $_.Groups[1].Value }) }
tc_check 'TEST-CODEQLCFG-002b-analyze-uses-repository-config' (($cfgFiles.Count -eq 1) -and ($cfgFiles[0] -eq "./$ConfigRel")) ("config-file values: [{0}], expected exactly './{1}'" -f ($cfgFiles -join ', '), $ConfigRel)
tc_check 'TEST-CODEQLCFG-002c-no-moving-upstream-reference' (($null -ne $analyze) -and ($analyze -notmatch '@development\b') -and ($analyze -notmatch '(?m)^\s*config-file:\s*[\w.-]+/[\w.-]+/.+@')) 'analyze job references a remote or moving (@development) config'

# 003: config parses (strict subset), default queries disabled, every pack reference is an exact version.
$cfgText = Read-RepoText $ConfigRel
tc_check 'TEST-CODEQLCFG-003a-config-present' ($null -ne $cfgText) "$ConfigRel missing"
$cfg = $null
if ($libOk -and $null -ne $cfgText) {
    $cfg = Read-AvbCodeQLConfigPacks $cfgText
    tc_check 'TEST-CODEQLCFG-003b-config-parses' $cfg.Ok $cfg.Why
    tc_check 'TEST-CODEQLCFG-003c-default-queries-disabled-as-before' ($cfg.Ok -and $cfg.DisableDefault) 'disable-default-queries must stay true (MSDT configuration; queries come only from the pinned packs)'
    $loose = @($cfg.Packs | Where-Object { -not $_.Exact } | ForEach-Object { $_.Spec })
    tc_check 'TEST-CODEQLCFG-003d-exact-pack-versions' ($cfg.Ok -and $cfg.Packs.Count -gt 0 -and $loose.Count -eq 0) ("non-exact pack references: {0}" -f ($loose -join ' | '))
    $verByPack = @{}; $conflict = @()
    foreach ($p in $cfg.Packs) { if ($verByPack.ContainsKey($p.Pack) -and $verByPack[$p.Pack] -ne $p.Version) { $conflict += "$($p.Pack)@$($verByPack[$p.Pack]) vs @$($p.Version)" }; $verByPack[$p.Pack] = $p.Version }
    tc_check 'TEST-CODEQLCFG-003e-one-version-per-pack' ($conflict.Count -eq 0) ($conflict -join ' | ')
    # 004: required coverage entries present.
    $have = @($cfg.Packs | ForEach-Object { "$($_.Pack):$($_.Path)" })
    $missing = @($RequiredEntries | Where-Object { $have -notcontains $_ })
    tc_check 'TEST-CODEQLCFG-004-required-coverage-entries-present' ($cfg.Ok -and $missing.Count -eq 0) ("missing: {0}" -f ($missing -join ' | '))
}

# 005: reviewed inventory parses, is non-empty, contains the experimental isolation queries, and every pack@version
#      in it is exactly the version pinned in the config (re-baselining is required with every version change).
$invText = Read-RepoText $InventoryRel
tc_check 'TEST-CODEQLCFG-005a-inventory-present' ($null -ne $invText) "$InventoryRel missing"
if ($libOk -and $null -ne $invText) {
    $inv = Read-AvbCodeQLInventory $invText
    tc_check 'TEST-CODEQLCFG-005b-inventory-parses' ($inv.Ok -and $inv.Entries.Count -gt 0) $(if ($inv.Ok) { 'inventory is empty' } else { $inv.Why })
    if ($inv.Ok -and $null -ne $cfg -and $cfg.Ok) {
        $pinned = @{}; foreach ($p in $cfg.Packs) { $pinned[$p.Pack] = $p.Version }
        $bad = @($inv.Entries | Where-Object { -not $pinned.ContainsKey($_.Pack) -or $pinned[$_.Pack] -ne $_.Version } | ForEach-Object { $_.Key } | Select-Object -First 5)
        tc_check 'TEST-CODEQLCFG-005c-inventory-matches-pinned-versions' ($bad.Count -eq 0) ("inventory entries not at the pinned version (first 5): {0}" -f ($bad -join ' | '))
        $iso = @($inv.Entries | Where-Object { $_.Path -match '/experimental/DriverIsolation(ZwViolation1|ZwViolation2|RtlViolation)/' })
        tc_check 'TEST-CODEQLCFG-005d-inventory-has-isolation-queries' ($iso.Count -eq 3) ("{0} of 3 DriverIsolation queries in inventory" -f $iso.Count)
        $noId = @($inv.Entries | Where-Object { [string]::IsNullOrEmpty($_.Id) } | ForEach-Object { $_.Key } | Select-Object -First 5)
        tc_check 'TEST-CODEQLCFG-005e-inventory-ids-recorded' ($noId.Count -eq 0) ("entries without @id (first 5): {0}" -f ($noId -join ' | '))
    }
}

# 006: workflow wiring -- dependency validation before init, analyze after init, nothing may swallow failures,
#      and changes to the config / inventory / tool trigger the workflow.
if ($null -ne $analyze) {
    $iResolve = $analyze.IndexOf('Test-CodeQLDriverPacks.ps1 -Mode Resolve')
    $iInit    = $analyze.IndexOf('github/codeql-action/init@')
    $iAnalyze = $analyze.IndexOf('github/codeql-action/analyze@')
    tc_check 'TEST-CODEQLCFG-006a-dependency-validation-before-init' ($iResolve -ge 0 -and $iInit -gt $iResolve) "expected '$ToolRel -Mode Resolve' step before codeql-action/init (resolve=$iResolve init=$iInit)"
    # 006b (a '$ToolRel -Mode Analysis' step after analyze) removed 2026-10-29 (Q): no approved requirement demands a
    # post-analysis SARIF check; it was introduced by dbd93d5/7ab62b9 only.  Its approved half -- the full analysis
    # (tier 3) actually runs after init -- is kept as 006h; 006b is not reused.
    tc_check 'TEST-CODEQLCFG-006h-analyze-after-init' ($iInit -ge 0 -and $iAnalyze -gt $iInit) "expected codeql-action/analyze step after codeql-action/init (init=$iInit analyze=$iAnalyze)"
    tc_check 'TEST-CODEQLCFG-006c-no-continue-on-error' ($analyze -notmatch 'continue-on-error') 'continue-on-error in analyze job'
    # 006f: production analysis CLI pinned to the same version as the validation CLI (one toolchain version set;
    #       an update must change both and pass all tiers).
    $valVer  = [regex]::Match($analyze, '(?m)^\s*\$Version\s*=\s*"(\d+\.\d+\.\d+)"').Groups[1].Value
    $toolVer = [regex]::Match($analyze, '(?m)^\s*tools:\s*https://github\.com/github/codeql-action/releases/download/codeql-bundle-v(\d+\.\d+\.\d+)/codeql-bundle-win64\.tar\.(zst|gz)\s*$').Groups[1].Value
    tc_check 'TEST-CODEQLCFG-006f-analysis-cli-pinned-to-validation-cli' ($valVer -ne '' -and $toolVer -eq $valVer) ("validation CLI '{0}', init tools bundle '{1}'" -f $valVer, $toolVer)
    # 006g: SARIF + validation report preserved as a run artifact even on failure (evidence must be inspectable).
    $up = [regex]::Match($analyze, '(?ms)uses:\s*actions/upload-artifact@\S+\s*\n\s*if:\s*always\(\)\s*\n(.*?)(?=\n\s{6}- name:|\z)')
    tc_check 'TEST-CODEQLCFG-006g-results-uploaded-always' ($up.Success -and $up.Groups[1].Value -match 'build/codeql-results') 'analyze job must upload build/codeql-results with if: always()'
}
if ($null -ne $wf) {
    $trig = @('.github/codeql/**', $ToolRel, $LibRel) | Where-Object { ([regex]::Matches($wf, '(?m)^\s*-\s*"' + [regex]::Escape($_) + '"\s*$')).Count -lt 2 }
    tc_check 'TEST-CODEQLCFG-006d-config-changes-trigger-workflow' (@($trig).Count -eq 0) ("not in both push and pull_request paths: {0}" -f (@($trig) -join ', '))
}
tc_check 'TEST-CODEQLCFG-006e-tool-present' ($null -ne (Read-RepoText $ToolRel)) "$ToolRel missing"

# 007: WHCP certification job independent of the CI configuration and still on its own matrix.
tc_check 'TEST-CODEQLCFG-007-whcp-job-independent' (($null -ne $whcp) -and ($whcp -notmatch [regex]::Escape($ConfigRel)) -and ($whcp -match '\$Version\s*=\s*"2\.15\.4"') -and ($whcp -match '--branch WHCP_22H2')) 'analyze-whcp must not use the CI config and must keep CLI 2.15.4 + WHCP_22H2'

# 008: config parser rejects the defective upstream config (verbatim MSDT config/codeql-config.yml @ 878c2db5b2).
if ($libOk) {
    $upstream = @'
name: "CodeQL config"
disable-default-queries: true

packs:
  - microsoft/cpp-queries@0.0.6:codeql-suites/cpp-code-scanning.qls
  - microsoft/windows-drivers@1.x:windows-driver-suites/recommended.qls
  - microsoft/windows-drivers@1.x:drivers\general\queries\experimental\DriverIsolationZwViolation1\DriverIsolationZwViolation1.ql
  - microsoft/windows-drivers@1.x:drivers\general\queries\experimental\DriverIsolationZwViolation2\DriverIsolationZwViolation2.ql
  - microsoft/windows-drivers@1.x:drivers\general\queries\experimental\DriverIsolationRtlViolation\DriverIsolationRtlViolation.ql
'@
    $u = Read-AvbCodeQLConfigPacks $upstream
    $uLoose = @($u.Packs | Where-Object { -not $_.Exact })
    tc_check 'TEST-CODEQLCFG-008a-upstream-1.x-rejected' ($u.Ok -and $uLoose.Count -eq 4) ("parse ok={0}, non-exact={1} (expected 4 x windows-drivers@1.x)" -f $u.Ok, $uLoose.Count)
    foreach ($s in @('microsoft/windows-drivers@^1.10.0:x.qls', 'microsoft/windows-drivers@*:x.qls', 'microsoft/windows-drivers:x.qls', 'microsoft/windows-drivers@1.10:x.qls')) {
        $r = Read-AvbCodeQLConfigPacks ("disable-default-queries: true`npacks:`n  - $s`n")
        tc_check "TEST-CODEQLCFG-008b-non-exact-rejected ($s)" ($r.Ok -and -not $r.Packs[0].Exact) "accepted as exact"
    }
    $odd = Read-AvbCodeQLConfigPacks "disable-default-queries: true`npacks:`n  - microsoft/cpp-queries@0.0.5:a.qls`nqueries:`n  - uses: ./x.ql`n"
    tc_check 'TEST-CODEQLCFG-008c-unsupported-config-keys-fail' (-not $odd.Ok) 'unknown top-level key accepted (would hide extra/removed queries from this contract)'

    # 009: failure classifier on observed messages (sources in the fixture names).  Primary error = first fatal line.
    $fx = @(
        @{ n = 'run-40782452229-missing-pack'; c = 'DEPENDENCY_FAILURE'; t = "A fatal error occurred: The QL pack 'microsoft/cpp-queries@0.0.5' which is referenced from C:\Users\runneradmin\.codeql\packages\microsoft\windows-drivers\1.10.0\windows-driver-suites\mustfix.qls cannot be found." },
        @{ n = 'local-2026-10-29-blob-reset';  c = 'INFRA_FAILURE';      t = "Package specifications to check for download: microsoft/cpp-queries@0.0.6`nA fatal error occurred: Error downloading blob.`n(eventual cause: SocketException `"Connection reset`")" },
        @{ n = 'local-2026-10-29-consequential'; c = 'DEPENDENCY_FAILURE'; t = "A fatal error occurred: Query pack microsoft/cpp-queries@0.0.6:codeql-suites/cpp-code-scanning.qls cannot be found. Check the spelling of the pack and the version range." },
        @{ n = 'unclassified-fatal';           c = 'TOOL_FAILURE';       t = "A fatal error occurred: something new" }
    )
    foreach ($f in $fx) {
        $r = Get-AvbCodeQLFailure $f.t
        tc_check "TEST-CODEQLCFG-009-classify ($($f.n))" ($r.Category -eq $f.c -and $r.Primary -match 'A fatal error occurred') ("got {0} / primary '{1}', expected {2}" -f $r.Category, $r.Primary, $f.c)
    }

    # 010: query identity normalisation and exact inventory comparison (added / removed / changed id).
    $id = ConvertTo-AvbQueryKey 'D:\a\_temp\cache\packages\microsoft\windows-drivers\1.10.0\drivers\general\queries\X\X.ql'
    tc_check 'TEST-CODEQLCFG-010a-query-key' ($id.Key -eq 'microsoft/windows-drivers@1.10.0:drivers/general/queries/X/X.ql') ("got '{0}'" -f $id.Key)
    $exp = (Read-AvbCodeQLInventory "a/b@1.0.0:q1.ql`tid/one`na/b@1.0.0:q2.ql`tid/two`n").Entries
    $act = (Read-AvbCodeQLInventory "a/b@1.0.0:q2.ql`tid/TWO`na/b@1.0.0:q3.ql`tid/three`n").Entries
    $d = Compare-AvbCodeQLInventory $exp $act
    tc_check 'TEST-CODEQLCFG-010b-inventory-diff' (($d.Removed -join ',') -eq 'a/b@1.0.0:q1.ql' -and ($d.Added -join ',') -eq 'a/b@1.0.0:q3.ql' -and ($d.Changed -join ',') -eq 'a/b@1.0.0:q2.ql' -and -not $d.Equal) ("removed={0} added={1} changed={2}" -f ($d.Removed -join ','), ($d.Added -join ','), ($d.Changed -join ','))
    $same = Compare-AvbCodeQLInventory $exp $exp
    tc_check 'TEST-CODEQLCFG-010c-inventory-equal' $same.Equal 'identical inventories reported as different'
}

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-CODEQLCFG: CodeQL driver configuration contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-CODEQLCFG: CodeQL driver configuration contract holds (static check only; not CodeQL resolution evidence)" -ForegroundColor Green
exit 0
