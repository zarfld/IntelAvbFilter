# test_hil_dispatch_paths.ps1
# TEST-HILPATH-*: every route that executes test binaries on the HIL machine must pass through the authoritative
# dispatcher (tools/test/Run-Tests-CI.ps1: HIL safety classification + build provenance), or be retired.
#
# Characterization test (offline, static).  NONE of the analyzed scripts is executed.  Comments are stripped
# (PowerShell tokenizer; `rem` / `::` lines in .bat) before matching.  RED results document bypass paths that
# require migration; this file is intentionally NOT registered in any Run-Tests-CI suite.
#
# Classification:
#   supported           - authoritative dispatcher (Run-Tests-CI.ps1)
#   migration-required  - active scripts that execute test binaries without the dispatcher's gates
#   deprecated          - tools/archive/deprecated/** (must not be refactored or reactivated; retire)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILPATH: test-binary dispatch paths vs. the authoritative HIL dispatcher (static)" -ForegroundColor Cyan

function Get-CodeText([string]$Path) {
    $raw = [System.IO.File]::ReadAllText($Path)
    if ($Path -match '\.(bat|cmd)$') {
        return ((($raw -split "`r?`n") | Where-Object { $_ -notmatch '^\s*(@?rem\b|::)' }) -join "`n")
    }
    $errs = $null
    $toks = [System.Management.Automation.PSParser]::Tokenize($raw, [ref]$errs)
    $sb = New-Object System.Text.StringBuilder
    foreach ($t in $toks) {
        if ($t.Type -eq 'Comment') { continue }
        if ($t.Type -eq 'Variable') { [void]$sb.Append('$') }   # tokenizer drops the sigil from variable tokens
        [void]$sb.Append($t.Content).Append(' ')
    }
    return $sb.ToString()
}

# Execution evidence (direct) and delegation (to other dispatch scripts)
# (Tokenized code text separates member access: "$test . FullName".)
$ExecRx = '&\s*\$testPath\b|&\s*\$testExe\b|&\s*\$\w+\s*\.\s*FullName\b|"%TEST_EXE%"|&\s*\$exePath\b|Start-Process\s+[^\r\n]*\$exePath'
# Enumerating every *.exe counts only together with executing enumerated items (listing them is harmless).
$EnumAllRx = '(?s)Get-ChildItem\s+[^\r\n]*-Filter\s+"?\*\.exe"?(?=[\s)]|$).*&\s*\$\w+\s*\.\s*FullName\b'
$DispatchScripts = @('Run-Tests.ps1', 'Run-Tests-Elevated.ps1', 'Run-Tests-CI.ps1')
# Delegation = a dispatch-script name inside an actual command (invocation, Join-Path, Start-Process, ...),
# not inside a message (Write-*/Out-*) or a throw statement.  .bat: any non-comment mention.
function Get-DelegationTargets([string]$Path) {
    if ($Path -match '\.(bat|cmd)$') {
        $c = Get-CodeText $Path
        return @($DispatchScripts | Where-Object { [regex]::IsMatch($c, '(?<![-\w])' + [regex]::Escape($_)) })
    }
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$null)
    $cmds = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.CommandAst] }, $true)
    $texts = @($cmds | Where-Object { $_.GetCommandName() -notmatch '^(Write-|Out-)' } | ForEach-Object { $_.Extent.Text })
    return @($DispatchScripts | Where-Object { $n = $_; @($texts | Where-Object { [regex]::IsMatch($_, '(?<![-\w])' + [regex]::Escape($n)) }).Count -gt 0 })
}
function Get-PathFacts([string]$Rel) {
    $p = Join-Path $RepoRoot $Rel
    if (-not (Test-Path -LiteralPath $p)) { return @{ Exists = $false } }
    $c = Get-CodeText $p
    return @{
        Exists         = $true
        DirectExec     = [regex]::IsMatch($c, $ExecRx)
        EnumeratesAll  = [regex]::IsMatch($c, $EnumAllRx)
        DelegatesTo    = @(Get-DelegationTargets $p)
        Provenance     = [regex]::IsMatch($c, '\bGet-AvbHilProvenanceDecision\b')
        Classification = [regex]::IsMatch($c, '\bGet-AvbHilGateDecision\b')
    }
}

$Inventory = [ordered]@{
    'tools/test/Run-Tests-CI.ps1'                         = 'supported'
    'tools/test/Run-Tests.ps1'                            = 'migration-required'
    'tools/test/Run-Tests-Elevated.ps1'                   = 'migration-required'
    'tools/benchmark/Run-DriverBenchmark.ps1'             = 'migration-required'
    'tools/development/Check-System.ps1'                  = 'migration-required'
    'tools/archive/deprecated/test/Quick-Test-Debug.bat'  = 'deprecated'
    'tools/archive/deprecated/test/Quick-Test-Release.bat'= 'deprecated'
    'tools/archive/deprecated/FullSuite.ps1'              = 'deprecated'
}
$Facts = @{}
foreach ($k in $Inventory.Keys) { $Facts[$k] = Get-PathFacts $k }

# A path is gated iff it enforces both gates itself, or it never executes binaries itself and delegates only
# to gated paths (Run-Tests.ps1 / Run-Tests-Elevated.ps1 are evaluated transitively).
$Gated = @{}
function Test-Gated([string]$Rel, [string[]]$Stack = @()) {
    if ($Gated.ContainsKey($Rel)) { return $Gated[$Rel] }
    if ($Stack -contains $Rel) { return $false }
    $f = $Facts[$Rel]
    if (-not $f.Exists) { $Gated[$Rel] = $true; return $true }
    if ($f.Provenance -and $f.Classification) { $Gated[$Rel] = $true; return $true }
    if ($f.DirectExec -or $f.EnumeratesAll) { $Gated[$Rel] = $false; return $false }
    $ok = $true
    foreach ($d in $f.DelegatesTo) {
        $dk = @($Inventory.Keys | Where-Object { $_ -match ('/' + [regex]::Escape($d) + '$') })[0]
        if (-not $dk -or -not (Test-Gated $dk ($Stack + $Rel))) { $ok = $false }
    }
    $Gated[$Rel] = $ok
    return $ok
}
function Show-Facts([string]$Rel) {
    $f = $Facts[$Rel]
    if (-not $f.Exists) { return "$Rel absent" }
    return ("{0} [{1}]: direct-exec={2} enumerates-all-exe={3} delegates=[{4}] provenance={5} classification={6}" -f $Rel, $Inventory[$Rel],
            $f.DirectExec, $f.EnumeratesAll, ($f.DelegatesTo -join ','), $f.Provenance, $f.Classification)
}

Write-Host ""
foreach ($k in $Inventory.Keys) { Write-Host ("  [INFO] {0}" -f (Show-Facts $k)) -ForegroundColor Yellow }
Write-Host ""

tc_check 'TEST-HILPATH-000-authoritative-dispatcher-enforces-provenance-and-classification' (Test-Gated 'tools/test/Run-Tests-CI.ps1') (Show-Facts 'tools/test/Run-Tests-CI.ps1')
tc_check 'TEST-HILPATH-001-run-tests-ps1-routes-through-the-gates' (Test-Gated 'tools/test/Run-Tests.ps1') (Show-Facts 'tools/test/Run-Tests.ps1')
tc_check 'TEST-HILPATH-002-run-tests-elevated-routes-through-the-gates' (Test-Gated 'tools/test/Run-Tests-Elevated.ps1') (Show-Facts 'tools/test/Run-Tests-Elevated.ps1')
tc_check 'TEST-HILPATH-003-driver-benchmark-routes-through-the-gates' (Test-Gated 'tools/benchmark/Run-DriverBenchmark.ps1') (Show-Facts 'tools/benchmark/Run-DriverBenchmark.ps1')
tc_check 'TEST-HILPATH-009-check-system-does-not-execute-test-binaries' (Test-Gated 'tools/development/Check-System.ps1') (Show-Facts 'tools/development/Check-System.ps1')
$bats = @('tools/archive/deprecated/test/Quick-Test-Debug.bat', 'tools/archive/deprecated/test/Quick-Test-Release.bat')
tc_check 'TEST-HILPATH-004-deprecated-quick-test-launchers-do-not-execute-binaries' (@($bats | Where-Object { -not (Test-Gated $_) }).Count -eq 0) (($bats | ForEach-Object { Show-Facts $_ }) -join ' || ')
tc_check 'TEST-HILPATH-005-deprecated-fullsuite-routes-through-the-gates' (Test-Gated 'tools/archive/deprecated/FullSuite.ps1') (Show-Facts 'tools/archive/deprecated/FullSuite.ps1')
tc_check 'TEST-HILPATH-006-no-full-suite-launcher-bat-reintroduced' (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'Run-Full-Tests.bat'))) 'Run-Full-Tests.bat exists at the repository root'

# Inventory guard: every ACTIVE script (outside tools/archive/deprecated) that executes or enumerates test
# binaries, or delegates to a dispatch script, must be classified above (a new bypass cannot appear silently).
# Everything under tools/archive/deprecated is classified `deprecated`; dispatch-capable ones are listed (008).
$unknown = @(); $deprecatedCapable = @()
# -Include is ignored with -LiteralPath on Windows PowerShell 5.1: filter extensions explicitly.
foreach ($f in @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'tools') -Recurse -File | Where-Object { $_.Extension -in '.ps1', '.psm1', '.bat', '.cmd' })) {
    $rel = $f.FullName.Substring($RepoRoot.Length + 1).Replace('\', '/')
    if ($Inventory.Contains($rel)) { continue }
    try { $c = Get-CodeText $f.FullName; $dl = @(Get-DelegationTargets $f.FullName) } catch { $unknown += "$rel (unparseable)"; continue }
    $hits = [regex]::IsMatch($c, $ExecRx) -or [regex]::IsMatch($c, $EnumAllRx) -or
            (@($dl | Where-Object { $_ -ne 'Run-Tests-CI.ps1' }).Count -gt 0) -or
            ([regex]::IsMatch($c, 'avb_test[\\/]+x64') -and [regex]::IsMatch($c, '&\s*\$|Start-Process|Invoke-Expression'))
    if (-not $hits) { continue }
    if ($rel -like 'tools/archive/deprecated/*') { $deprecatedCapable += $rel } else { $unknown += $rel }
}
tc_check 'TEST-HILPATH-007-every-active-test-binary-dispatch-path-is-classified' ($unknown.Count -eq 0) ("unclassified active dispatch-capable scripts: {0}" -f ($unknown -join ', '))
tc_check 'TEST-HILPATH-008-no-other-deprecated-script-can-dispatch-test-binaries' ($deprecatedCapable.Count -eq 0) ("deprecated, dispatch-capable (retire): {0}" -f ($deprecatedCapable -join ', '))

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILPATH: test binaries can be dispatched outside the authoritative HIL gates (characterization)" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILPATH: all test-binary dispatch routes pass the authoritative HIL gates" -ForegroundColor Green
exit 0
