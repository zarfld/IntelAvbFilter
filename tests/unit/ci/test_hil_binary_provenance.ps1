# test_hil_binary_provenance.ps1
# TEST-HILPROV-*: Run-Tests-CI.ps1 must dispatch only artifacts produced and authorized by the CURRENT build.
#
# Characterization test (iteration 1, offline).  It does NOT change CI behaviour and is not listed in any
# Run-Tests-CI suite.
#
# Incident reproduced (2026-10-20 09:14:39, #328 investigation F23): build\tools\avb_test\x64\Debug\
# test_tsn_ioctl_handlers.exe was an 8-April binary whose Build-Tests entry is `Disabled = $true`; the runner
# found it with Test-Path only, classified it ReadOnly, and executed it during a state-integrity HALT, where it
# issued SETUP_TAS / SETUP_FP / SETUP_PTM successfully.
#
# Provenance contract asserted (proposed; the current runner implements none of it):
#   - the build writes build\tools\avb_test\x64\<Config>\test-build-manifest.json listing every build entry
#     with status (built | disabled | failed) and, for built entries, the SHA256 of the produced artifact;
#   - in a Hardware* suite an item executes only if the manifest exists, lists the item as `built`, and the
#     SHA256 of the artifact about to execute equals the manifest value; otherwise it is BLOCKED
#     (TC-SAFETY-GATE-002) and never started.  ReadOnly class and an active HALT do not change this.
#   - modification time is never a provenance criterion (neither to authorize nor to reject).
#
# Isolation: same sandbox technique as tests/unit/ioctl/test_hil_safety_gate.ps1 - a copy of the runner in a
# temp tree, AVB_TEST_SANDBOX=1 (adapter state from JSON, device health stubbed, only ps1 fakes outside the
# repository may execute).  In the sandbox the fake ps1 is "the artifact that would execute".  Nothing here
# opens \\.\IntelAvbFilter, queries PnP, touches services/adapters, or writes repository logs.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$RunCIPath = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
$BuildPath = Join-Path $RepoRoot 'tools\build\Build-Tests.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILPROV: HIL dispatch must require current-build artifact provenance (offline characterization)" -ForegroundColor Cyan

# ---------------------------------------------------------------------------------------------------------
# Static (real repository): no runnable suite entry may resolve to a statically Disabled build entry
# ---------------------------------------------------------------------------------------------------------
$ast = [System.Management.Automation.Language.Parser]::ParseFile($RunCIPath, [ref]$null, [ref]$null)
function Get-RunnerList([string]$name) {
    $as = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq $name }, $true) | Select-Object -First 1
    if ($null -eq $as) { return @() }
    return @($as.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value })
}
$suiteEntries = @{}
foreach ($n in 'UnitTests', 'HardwareDependentTests', 'IntegrationTests', 'HardwareMandatoryTests', 'HardwareMutationTests', 'HardwareUnitTests', 'HardwareIntegrationTests') {
    foreach ($e in (Get-RunnerList $n)) {
        if ($e.StartsWith('ps1:')) { continue }
        $bin = ($e -split '\s+', 2)[0] -replace '\.exe$', ''
        if (-not $suiteEntries.ContainsKey($bin)) { $suiteEntries[$bin] = @() }
        $suiteEntries[$bin] += $n
    }
}

# Build-Tests entries: Name / Output / statically Disabled
$buildLines = Get-Content $BuildPath
$staticDisabled = @{}
for ($i = 0; $i -lt $buildLines.Count; $i++) {
    if ($buildLines[$i] -match '^\s*Name\s*=\s*"([^"]+)"') {
        $nm = $Matches[1]; $out = $nm; $dis = $false
        for ($j = $i + 1; $j -lt [Math]::Min($i + 16, $buildLines.Count); $j++) {
            if ($buildLines[$j] -match '^\s*Name\s*=') { break }
            if ($buildLines[$j] -match '^\s*Output\s*=\s*"([^"]+)"') { $out = [System.IO.Path]::GetFileNameWithoutExtension($Matches[1]) }
            if ($buildLines[$j] -match '^\s*Disabled\s*=\s*\$true') { $dis = $true }
            if ($buildLines[$j] -match '^\s*\},?\s*$') { break }
        }
        if ($dis) { $staticDisabled[$out] = $nm }
    }
}
$disabledListed = @($staticDisabled.Keys | Where-Object { $suiteEntries.ContainsKey($_) } | Sort-Object |
                    ForEach-Object { "{0} (listed in {1})" -f $_, (($suiteEntries[$_] | Sort-Object -Unique) -join ',') })
tc_check 'TEST-HILPROV-010-no-suite-entry-resolves-to-a-disabled-build-entry' ($staticDisabled.Count -gt 0 -and $disabledListed.Count -eq 0) `
    ("statically Disabled yet dispatchable: {0}" -f ($disabledListed -join '; '))

# Informational only (machine state, not a verdict): disabled-entry binaries present in local build output
foreach ($cfg in 'Debug', 'Release') {
    $dir = Join-Path $RepoRoot "build\tools\avb_test\x64\$cfg"
    foreach ($stem in $staticDisabled.Keys) {
        $p = Join-Path $dir "$stem.exe"
        if (Test-Path -LiteralPath $p) {
            $fi = Get-Item -LiteralPath $p
            Write-Host ("  [INFO] local leftover of Disabled entry: {0} ({1} B, written {2:yyyy-MM-dd HH:mm}, SHA256 {3})" -f $p, $fi.Length, $fi.LastWriteTime, (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.Substring(0, 16)) -ForegroundColor Yellow
        }
    }
}

# ---------------------------------------------------------------------------------------------------------
# Behavioral: sandbox copy of the runner with a synthetic build manifest
# ---------------------------------------------------------------------------------------------------------
$Tmp    = Join-Path $env:TEMP ("hilprov_{0}" -f (Get-Random))
$Tree   = Join-Path $Tmp 'tree'
$Fakes  = Join-Path $Tmp 'fakes'
$RanLog = Join-Path $Tmp 'ran.log'
foreach ($d in (Join-Path $Tree 'tools\test'), (Join-Path $Tree 'src'), (Join-Path $Tree 'include'), $Fakes) {
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}
foreach ($f in 'Run-Tests-CI.ps1', 'TestVerdict.ps1', 'Convert-Logs-To-JUnit.ps1') {
    Copy-Item (Join-Path $RepoRoot "tools\test\$f") (Join-Path $Tree "tools\test\$f")
}
@'
function Get-AvbLifecycleSnapshot { return $null }
function Write-LifecycleDiff { param($Before, $After, $Label) return $false }
'@ | Set-Content (Join-Path $Tree 'tools\test\Lib-AvbLifecycle.ps1') -Encoding UTF8
Copy-Item (Join-Path $RepoRoot 'src\avb_integration_fixed.c') (Join-Path $Tree 'src\avb_integration_fixed.c')
Copy-Item (Join-Path $RepoRoot 'include\intel_pci_ids.h')     (Join-Path $Tree 'include\intel_pci_ids.h')
$TreeRunner   = Join-Path $Tree 'tools\test\Run-Tests-CI.ps1'
$ManifestDir  = Join-Path $Tree 'build\tools\avb_test\x64\Debug'
$ManifestPath = Join-Path $ManifestDir 'test-build-manifest.json'
New-Item -ItemType Directory -Path $ManifestDir -Force | Out-Null

$Classes = 'test_sbprov_halter=Init;test_sbprov_disabled=ReadOnly;test_sbprov_current=ReadOnly;test_sbprov_stale=ReadOnly;' +
           'test_sbprov_mismatch=ReadOnly;test_sbprov_newmtime=ReadOnly;test_sbprov_oldmtime=ReadOnly;test_sbprov_missing=ReadOnly'

function New-Fake([string]$Name, [string]$Action = '') {
    $p = Join-Path $Fakes "$Name.ps1"
    $ran = $RanLog.Replace("'", "''")
    $body = "Add-Content -LiteralPath '$ran' -Value ('RAN $Name')`r`n$Action`r`nWrite-Host '  [PASS] TC-SBPROV-001: $Name ok'`r`nexit 0`r`n"
    Set-Content -LiteralPath $p -Value $body -Encoding UTF8
    return $p
}
function Get-Entry([string]$Path) { return ('ps1:' + $Path.Replace('\', '/')) }
function Get-Sha([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Get-ShaOfText([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return (($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text)) | ForEach-Object { $_.ToString('X2') }) -join '') } finally { $sha.Dispose() }
}
function Write-Manifest([object[]]$Entries) {
    $m = [ordered]@{ schema = 1; configuration = 'Debug'; producer = 'tools/build/Build-Tests.ps1'; entries = $Entries }
    ($m | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $ManifestPath -Encoding UTF8
}

function New-StateFile([string]$Path) {
    $state = [ordered]@{
        adapters = @(
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_125B&SUBSYS_00008086&REV_04\SB01'; PnpStatus = 'OK'; Problem = 0; AdminStatus = 'Up'; MediaStatus = 'Up'; FilterBound = 'True'; Name = 'SB I226' },
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_1533&SUBSYS_00008086&REV_03\SB02'; PnpStatus = 'OK'; Problem = 0; AdminStatus = 'Up'; MediaStatus = 'Up'; FilterBound = 'True'; Name = 'SB I210' }
        )
        clock = @(
            [ordered]@{ Index = 0; Did = '0x125B'; Ok = $true; Timinca = 0x18000000; Tsauxc = 0 },
            [ordered]@{ Index = 1; Did = '0x1533'; Ok = $true; Timinca = 0;          Tsauxc = 0 }
        )
    }
    ($state | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Edit-StateSnippet([string]$Edit) {
    return "`$sf = `$env:AVB_TEST_ADAPTER_STATE_FILE; `$s = Get-Content -LiteralPath `$sf -Raw | ConvertFrom-Json; $Edit; " +
           "(`$s | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath `$sf -Encoding UTF8"
}

function Invoke-Sandbox {
    param([string]$Name, [string[]]$Tests, [string]$Suite = 'HardwareUnit')
    $dir  = Join-Path $Tmp $Name
    $logs = Join-Path $dir 'logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $stateFile = Join-Path $dir 'state.json'
    New-StateFile $stateFile
    if (Test-Path $RanLog) { Remove-Item $RanLog -Force }
    $envs = [ordered]@{
        AVB_TEST_SANDBOX = '1'; AVB_TEST_ADAPTER_STATE_FILE = $stateFile; AVB_TEST_SANDBOX_CLASSES = $Classes
        AVB_TEST_STATE_SETTLE_SECONDS = '1'; GITHUB_ACTIONS = ''; GITHUB_ENV = ''; AVB_HIL_TARGET_DID = ''
        AVB_HIL_AUTHORIZE_UNSAFE = ''; AVB_TEST_HEALTH_FAIL_AFTER = ''
    }
    $lines = @()
    foreach ($k in $envs.Keys) {
        if ($envs[$k] -eq '') { $lines += "Remove-Item Env:$k -ErrorAction SilentlyContinue" }
        else { $lines += ("`$env:{0} = '{1}'" -f $k, $envs[$k].Replace("'", "''")) }
    }
    $testsLit = ($Tests | ForEach-Object { "'" + $_.Replace("'", "''") + "'" }) -join ', '
    $lines += "& '$TreeRunner' -Suite $Suite -Tests @($testsLit) -TestTimeoutSeconds 60 -Configuration Debug -LogsDir '$logs'"
    $lines += 'exit $LASTEXITCODE'
    $wrapper = Join-Path $dir 'wrapper.ps1'
    $lines | Set-Content -LiteralPath $wrapper -Encoding UTF8
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -NonInteractive -File `"$wrapper`""
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $dir
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(300000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT'; Xml = ''; Ran = @() } }
    $xml = (Get-ChildItem $logs -Filter 'junit-*.xml' -ErrorAction SilentlyContinue | ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n"
    $ran = @(); if (Test-Path $RanLog) { $ran = @(Get-Content $RanLog) }
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result); Xml = $xml; Ran = $ran }
}
function Tail($r) { return (($r.Out -split "`n" | Select-Object -Last 20) -join ' | ') }
function Get-RanCount($r, [string]$Name) { return @($r.Ran | Where-Object { $_ -eq "RAN $Name" }).Count }
function Test-ProvBlocked($r, [string]$Name) {
    return ((Get-RanCount $r $Name) -eq 0) -and ($r.Out -match ("\[BLOCKED\] TC-SAFETY-GATE-002: [^\r\n]*{0}" -f [regex]::Escape($Name)))
}

if ([System.IO.File]::ReadAllText($TreeRunner) -notmatch '\$env:AVB_TEST_SANDBOX\b') {
    tc_fail 'TEST-HILPROV-000-runner-sandbox-available' 'runner has no sandbox mode - behavioral cases NOT executed (they would open the real device)'
    Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
    exit 1
}

# --- Run A: exact replica of 09:14:39 - HALT, then a ReadOnly item whose build entry is Disabled ----------
$pHalter   = New-Fake 'test_sbprov_halter' (Edit-StateSnippet "`$a = @(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_1533' })[0]; `$a.AdminStatus = 'Down'")
$pDisabled = New-Fake 'test_sbprov_disabled' "Write-Host 'stale binary: would issue SETUP_TAS/SETUP_FP/SETUP_PTM'"
Write-Manifest @(
    [ordered]@{ name = 'test_sbprov_halter';   status = 'built';    sha256 = (Get-Sha $pHalter) },
    [ordered]@{ name = 'test_sbprov_disabled'; status = 'disabled'; reason = 'KM placeholder only (mirrors Build-Tests.ps1 test_tsn_ioctl_handlers)' }
)
$rA = Invoke-Sandbox 'runA' @((Get-Entry $pHalter), (Get-Entry $pDisabled))

tc_check 'TEST-HILPROV-000-fixture-reproduces-halt-before-readonly-item' `
    (($rA.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbprov_halter') -and ($rA.Out -match '\[HALT\] state-integrity halt')) (Tail $rA)
tc_check 'TEST-HILPROV-001-disabled-build-entry-blocks-dispatch-even-readonly-during-halt' (Test-ProvBlocked $rA 'test_sbprov_disabled') `
    ("expected BLOCKED TC-SAFETY-GATE-002 and not started; observed ran={0}" -f (Get-RanCount $rA 'test_sbprov_disabled'))

# --- Run B: provenance matrix ---------------------------------------------------------------------------
$pCurrent  = New-Fake 'test_sbprov_current'
$pStale    = New-Fake 'test_sbprov_stale'
$pMismatch = New-Fake 'test_sbprov_mismatch'
$pNewMtime = New-Fake 'test_sbprov_newmtime'
$pOldMtime = New-Fake 'test_sbprov_oldmtime'
$pMissing  = Join-Path $Fakes 'test_sbprov_missing.ps1'      # listed as built but the artifact is absent
(Get-Item -LiteralPath $pNewMtime).LastWriteTime = (Get-Date).AddDays(1)
(Get-Item -LiteralPath $pOldMtime).LastWriteTime = Get-Date '2020-01-01'
Write-Manifest @(
    [ordered]@{ name = 'test_sbprov_current';  status = 'built'; sha256 = (Get-Sha $pCurrent) },
    [ordered]@{ name = 'test_sbprov_mismatch'; status = 'built'; sha256 = (Get-ShaOfText 'content produced by the current build') },
    [ordered]@{ name = 'test_sbprov_newmtime'; status = 'built'; sha256 = (Get-ShaOfText 'content produced by the current build (newmtime)') },
    [ordered]@{ name = 'test_sbprov_oldmtime'; status = 'built'; sha256 = (Get-Sha $pOldMtime) },
    [ordered]@{ name = 'test_sbprov_missing';  status = 'built'; sha256 = (Get-ShaOfText 'missing') }
)   # test_sbprov_stale deliberately absent: not produced by the current build
$rB = Invoke-Sandbox 'runB' @((Get-Entry $pCurrent), (Get-Entry $pStale), (Get-Entry $pMismatch), (Get-Entry $pNewMtime), (Get-Entry $pOldMtime), (Get-Entry $pMissing))

tc_check 'TEST-HILPROV-002-artifact-absent-from-current-build-manifest-is-blocked' (Test-ProvBlocked $rB 'test_sbprov_stale') `
    ("expected BLOCKED; observed ran={0}" -f (Get-RanCount $rB 'test_sbprov_stale'))
tc_check 'TEST-HILPROV-003-artifact-sha256-mismatch-is-blocked' (Test-ProvBlocked $rB 'test_sbprov_mismatch') `
    ("expected BLOCKED; observed ran={0}" -f (Get-RanCount $rB 'test_sbprov_mismatch'))
tc_check 'TEST-HILPROV-004-newer-mtime-does-not-authorize-a-mismatched-artifact' (Test-ProvBlocked $rB 'test_sbprov_newmtime') `
    ("expected BLOCKED; observed ran={0}" -f (Get-RanCount $rB 'test_sbprov_newmtime'))
tc_check 'TEST-HILPROV-005-authorized-current-build-artifact-runs' ((Get-RanCount $rB 'test_sbprov_current') -eq 1) `
    ("expected ran=1; observed ran={0}; {1}" -f (Get-RanCount $rB 'test_sbprov_current'), (Tail $rB))
tc_check 'TEST-HILPROV-006-old-mtime-does-not-block-an-authorized-artifact' ((Get-RanCount $rB 'test_sbprov_oldmtime') -eq 1) `
    ("expected ran=1; observed ran={0}" -f (Get-RanCount $rB 'test_sbprov_oldmtime'))
tc_check 'TEST-HILPROV-007-missing-artifact-is-not-executed' `
    (((Get-RanCount $rB 'test_sbprov_missing') -eq 0) -and ($rB.Out -match 'test_sbprov_missing\.ps1 \(binary not found|TC-SAFETY-GATE-00[12]: [^\r\n]*test_sbprov_missing')) (Tail $rB)
tc_check 'TEST-HILPROV-008-provenance-blocks-fail-the-suite' ($rB.Exit -eq 1) ("expected exit 1; observed exit {0}" -f $rB.Exit)

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILPROV: dispatch does not enforce artifact provenance (characterization)" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILPROV: dispatch enforces current-build artifact provenance" -ForegroundColor Green
exit 0
