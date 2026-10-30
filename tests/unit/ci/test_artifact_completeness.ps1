# test_artifact_completeness.ps1
# TEST-COMPLETE-*: every declared test artifact is accounted for (Q, P0 full HIL coverage, 2026-10-21).
#
# Reconciles, offline and from the sources:
#   declared build targets (tools/build/Build-Tests.ps1, keyed by OUTPUT executable name - the runner resolves an
#   entry to <testExeDir>\<entry>.exe)  ->  runner registry / suite lists / coverage-gap tables (tools/test/Run-Tests-CI.ps1)
#   ->  explicit classification of everything that is not a runner entry ($TestArtifactClassification).
# Categories for a non-runner artifact: Diagnostic (utility, justified), Supporting (names its consumer), Obsolete
# (evidence required before removal).  Anything else is Unknown and FAILS - an artifact must never disappear silently.
# Executing every .exe because it exists is NOT the remedy: diagnostic tools may be deliberately disruptive.
# Execution records -> final verdicts (per run) are reconciled by the runner's results document, not here.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$Runner    = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
$BuildTests = Join-Path $RepoRoot 'tools\build\Build-Tests.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""
Write-Host "TEST-COMPLETE: build targets / test scripts vs runner registry and classification (offline)" -ForegroundColor Cyan

$ast = [System.Management.Automation.Language.Parser]::ParseFile($Runner, [ref]$null, [ref]$null)
function Get-Assign([string]$n) { $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq "`$$n" }, $true) | Select-Object -First 1 }
function Get-Strings([string]$n) { $a = Get-Assign $n; if (-not $a) { return @() }; @($a.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value }) }
function Get-Keys([string]$n) { $a = Get-Assign $n; if (-not $a) { return $null }; $h = $a.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true); if (-not $h) { return @() }; @($h.KeyValuePairs | ForEach-Object { $_.Item1.Value }) }
function Get-Table([string]$n) { $a = Get-Assign $n; $t = [ordered]@{}; if (-not $a) { return $null }; $h = $a.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    if ($h) { foreach ($kv in $h.KeyValuePairs) { $v = $kv.Item2.Find({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true); $t[$kv.Item1.Value] = if ($v) { $v.Value } else { '' } } }; return $t }

# Runner entries (executables: first token without .exe; ps1 tests: repository-relative path).
$listNames = 'UnitTests', 'IntegrationTests', 'HardwareUnitTests', 'HardwareIntegrationTests', 'HardwareMutationTests', 'HardwareMandatoryTests'
$gapNames  = 'HardwareUnitCoverageGaps', 'HardwareIntegrationCoverageGaps', 'HardwareMandatoryCoverageGaps', 'HardwareMutationAttendedOnly'
$entries = @(); foreach ($n in $listNames) { $entries += Get-Strings $n }; foreach ($n in $gapNames) { $k = Get-Keys $n; if ($k) { $entries += $k } }
$exeEntries = @($entries | Where-Object { $_ -notmatch '^ps1:' } | ForEach-Object { ($_ -split '\s+')[0] -replace '\.exe$', '' } | Sort-Object -Unique)
$ps1Entries = @($entries | Where-Object { $_ -match '^ps1:' } | ForEach-Object { ($_.Substring(4) -replace '\\', '/').ToLowerInvariant() } | Sort-Object -Unique)

# Build targets: Name + Output (the runner's key).
$bl = Get-Content -LiteralPath $BuildTests; $targets = @()
for ($i = 0; $i -lt $bl.Count; $i++) {
    if ($bl[$i] -match '^\s*Name\s*=\s*"([^"]+)"') {
        $nm = $Matches[1]; $out = $nm
        for ($j = $i + 1; $j -lt [Math]::Min($i + 16, $bl.Count); $j++) {
            if ($bl[$j] -match '^\s*Name\s*=') { break }
            if ($bl[$j] -match '^\s*Output\s*=\s*"([^"]+)"') { $out = [System.IO.Path]::GetFileNameWithoutExtension($Matches[1]); break }
        }
        $targets += [pscustomobject]@{ Name = $nm; Out = $out }
    }
}
$outs = @($targets | ForEach-Object { $_.Out })

# 000: the classification table exists and uses the allowed categories with a justification.
$cls = Get-Table 'TestArtifactClassification'
$clsBad = @()
if ($null -eq $cls) { $clsBad += '$TestArtifactClassification missing in tools/test/Run-Tests-CI.ps1' }
else {
    foreach ($k in @($cls.Keys)) {
        $v = [string]$cls[$k]
        if ($v -notmatch '^(Diagnostic|Supporting|Obsolete): \S') { $clsBad += "${k}: '$v' (must start with Diagnostic: / Supporting: / Obsolete: and a justification)" }
        elseif ($v -match '^Supporting:' -and $v -notmatch '(?i)consumer') { $clsBad += "${k}: Supporting without its consumer" }
        elseif ($v -match '^Obsolete:' -and $v -notmatch '(?i)evidence') { $clsBad += "${k}: Obsolete without evidence" }
        if ($exeEntries -contains $k -or $ps1Entries -contains $k.ToLowerInvariant()) { $clsBad += "${k}: classified AND a runner entry (one or the other)" }
    }
}
tc_check 'TEST-COMPLETE-000-classification-table-valid' ($clsBad.Count -eq 0) ($clsBad -join '; ')
$clsKeys = if ($cls) { @($cls.Keys) } else { @() }

# Runner resolution (Resolve-AvbHilArtifactName, Build-Tests Name -> Output): the artifact a runner entry executes.
$resFn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Resolve-AvbHilArtifactName' }, $true)
if ($resFn) { . ([scriptblock]::Create($resFn.Extent.Text)) }
$byNameMan = @{ ByName = @{} }
foreach ($t in $targets) { $byNameMan.ByName[$t.Name] = [pscustomobject]@{ name = $t.Name; artifact = "$($t.Out).exe" } }
function Get-EntryOut([string]$e) { if ($resFn) { return [System.IO.Path]::GetFileNameWithoutExtension((Resolve-AvbHilArtifactName -BinName $e -Manifest $byNameMan)) }; return $e }
$entryOuts = @($exeEntries | ForEach-Object { Get-EntryOut $_ })

# 001: every build target is a runner entry (active test, in a suite or declared gap) or explicitly classified.
$unknown = @($targets | Where-Object { $entryOuts -notcontains $_.Out -and $clsKeys -notcontains $_.Out } |
             ForEach-Object { if ($_.Name -ne $_.Out) { "$($_.Out) (Build-Tests Name '$($_.Name)')" } else { $_.Out } })
tc_check 'TEST-COMPLETE-001-every-build-target-is-a-runner-entry-or-classified' ($unknown.Count -eq 0) `
    ("{0} of {1} build target(s) Unknown - neither executed, nor a declared gap, nor classified: {2}" -f $unknown.Count, $targets.Count, ($unknown -join ', '))

# 002: every registered executable entry resolves (runner resolution above) to an artifact a build target produces.
$unbuilt = @($exeEntries | Where-Object { $outs -notcontains (Get-EntryOut $_) } | ForEach-Object {
    $e = $_; $byName = @($targets | Where-Object { $_.Name -eq $e })
    if ($byName.Count) { "$e (Build-Tests Name '$e' outputs '$($byName[0].Out).exe')" } else { "$e (no build target)" } })
tc_check 'TEST-COMPLETE-002-every-registered-executable-is-built-under-its-entry-name' ($unbuilt.Count -eq 0) `
    ("{0} registered entr(y/ies) the runner would report as binary missing: {1}" -f $unbuilt.Count, ($unbuilt -join ', '))

# 003: every unit-layer PowerShell test script is a runner entry or classified.
$scripts = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'tests\unit') -Recurse -File -Filter 'test_*.ps1' |
             ForEach-Object { $_.FullName.Substring($RepoRoot.Length + 1) -replace '\\', '/' })
$orphan = @($scripts | Where-Object { $ps1Entries -notcontains $_.ToLowerInvariant() -and $clsKeys -notcontains $_ })
tc_check 'TEST-COMPLETE-003-every-unit-test-script-is-registered-or-classified' ($orphan.Count -eq 0) `
    ("{0} of {1} test script(s) never executed by any runner list: {2}" -f $orphan.Count, $scripts.Count, ($orphan -join ', '))

# 004: one build target per output executable.  Two targets linking the same <Output>.exe into the shared test
#      directory make the executed program depend on build order (avb_test_project's avb_test_um.c vs.
#      avb_test_i210_device's device_specific/i210 source, 2026-10-29) - the runner and its provenance cannot tell
#      which source a result belongs to.
$dupOut = @($targets | Group-Object Out | Where-Object { $_.Count -gt 1 } |
            ForEach-Object { "$($_.Name).exe <- $((@($_.Group | ForEach-Object { $_.Name })) -join ' + ')" })
tc_check 'TEST-COMPLETE-004-every-output-executable-has-exactly-one-build-target' ($dupOut.Count -eq 0) `
    ("{0} output(s) produced by more than one build target: {1}" -f $dupOut.Count, ($dupOut -join '; '))

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-COMPLETE: artifacts disappear from verification" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-COMPLETE: every artifact is accounted for" -ForegroundColor Green
exit 0
