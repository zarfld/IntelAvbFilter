# test_hil_binary_provenance.ps1
# TEST-HILPROV-*: Run-Tests-CI.ps1 must dispatch only artifacts produced and authorized by the CURRENT build,
# and Build-Tests.ps1 must record that authorization truthfully (#328 investigation F23).
#
# Incident (2026-10-20 09:14:39): build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe was an 8-April
# binary whose Build-Tests entry is `Disabled = $true`; the runner found it with Test-Path only, classified it
# ReadOnly and executed it during a state-integrity HALT, where it issued SETUP_TAS / SETUP_FP / SETUP_PTM.
#
# Contract under test:
#   Build-Tests.ps1 writes build\tools\avb_test\x64\<Config>\test-build-manifest.json (schemaVersion 1):
#     one entry per build entry, sorted by name: enabled + prerequisites, the outcome of this run
#     (built | disabled | skipped | failed | not-built), the artifact name and - only for artifacts this run
#     produced - their SHA256; written atomically; deterministic.
#   Run-Tests-CI.ps1, in Hardware* suites, executes an artifact (test exe, ps1 outside the repository, sandbox
#     fake) only if the manifest exists for this configuration, records the artifact as `built` by an enabled
#     entry, the artifact's SHA256 matches, the entry was built from a clean committed revision equal to the
#     checked-out HEAD and the checkout has no tracked changes (no carry-over of earlier build records; a matching
#     hash is never sufficient on its own); otherwise BLOCKED (TC-SAFETY-GATE-002), a failure in JUnit and a
#     nonzero suite result.  Checked before the safety class and HALT rules.  Modification time is never used.
#     Repository-tracked ps1 tests (run from source) and hosted Unit/Integration suites are not affected.
#
# Layers: static checks of the real repository; unit tests of the real functions (extracted from the scripts
# by AST, run against synthetic .exe files that are never executed); behavioral runs of a sandboxed runner copy
# (AVB_TEST_SANDBOX=1: adapter state from JSON, device health stubbed, only ps1 fakes outside the repository
# may execute - the same technique as tests/unit/ioctl/test_hil_safety_gate.ps1).  Nothing here opens
# \\.\IntelAvbFilter, queries PnP, touches services/adapters, or writes repository logs.

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
Write-Host "TEST-HILPROV: HIL dispatch requires current-build artifact provenance" -ForegroundColor Cyan

$Tmp = Join-Path $env:TEMP ("hilprov_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

# ---------------------------------------------------------------------------------------------------------
# Static (real repository)
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
$registry = @{}
$regAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq 'HilTestSafety' }, $true) | Select-Object -First 1
if ($null -ne $regAst) {
    $ht = $regAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    foreach ($kv in $ht.KeyValuePairs) { $registry[$kv.Item1.Value] = ($kv.Item2.Extent.Text.Trim() -replace "^'|'$", '') }
}

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
tc_check 'TEST-HILPROV-011-removed-entry-not-reclassified-and-um-variant-stays-mutating' `
    ((-not $registry.ContainsKey('test_tsn_ioctl_handlers')) -and ($registry['test_tsn_ioctl_handlers_um'] -eq 'Mutating')) `
    ("registry: test_tsn_ioctl_handlers={0}; test_tsn_ioctl_handlers_um={1}" -f $(if ($registry.ContainsKey('test_tsn_ioctl_handlers')) { $registry['test_tsn_ioctl_handlers'] } else { '<absent>' }), $registry['test_tsn_ioctl_handlers_um'])
$self = 'ps1:tests/unit/ci/test_hil_binary_provenance.ps1'
tc_check 'TEST-HILPROV-012-registered-in-hosted-unit-suite' (((Get-RunnerList 'UnitTests') -contains $self) -and (Test-Path -LiteralPath (Join-Path $RepoRoot 'tests\unit\ci\test_hil_binary_provenance.ps1'))) `
    "$self not listed in `$UnitTests"

# Informational only (machine state, not a verdict): disabled-entry binaries present in local build output
foreach ($cfg in 'Debug', 'Release') {
    $dir = Join-Path $RepoRoot "build\tools\avb_test\x64\$cfg"
    foreach ($stem in $staticDisabled.Keys) {
        $p = Join-Path $dir "$stem.exe"
        if (Test-Path -LiteralPath $p) {
            $fi = Get-Item -LiteralPath $p
            Write-Host ("  [INFO] local leftover of Disabled entry: {0} ({1} B, written {2:yyyy-MM-dd HH:mm}) - not dispatchable without a manifest record" -f $p, $fi.Length, $fi.LastWriteTime) -ForegroundColor Yellow
        }
    }
}

# ---------------------------------------------------------------------------------------------------------
# Unit: real functions extracted by AST (never executes Build-Tests.ps1 or Run-Tests-CI.ps1)
# ---------------------------------------------------------------------------------------------------------
function Get-FunctionText([string]$Path, [string[]]$Names) {
    $a = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$null)
    $txt = ''
    foreach ($n in $Names) {
        $f = $a.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $n }, $false)
        if ($null -eq $f) { throw "function $n not found in $Path" }
        $txt += $f.Extent.Text + "`n"
    }
    return $txt
}
$fnMissing = ''
try {
    . ([scriptblock]::Create((Get-FunctionText $BuildPath @('Get-TestBuildArtifactName', 'New-TestBuildManifest', 'Write-TestBuildManifest'))))
    . (Join-Path $RepoRoot 'tools\lib\NativeExit.ps1')   # the runner's exit-code library (used by Get-AvbSourceIdentity)
    . (Join-Path $RepoRoot 'tools\lib\SourceDigest.ps1') # build-input digest (local pre-commit provenance)
    . ([scriptblock]::Create((Get-FunctionText $RunCIPath @('Get-AvbManifestProp', 'Read-AvbBuildManifest', 'Get-AvbHilProvenanceDecision', 'Test-AvbHilNeedsProvenance', 'Get-AvbSourceIdentity'))))
} catch { $fnMissing = $_.Exception.Message }
tc_check 'TEST-HILPROV-019-provenance-functions-present' ($fnMissing -eq '') $fnMissing

if ($fnMissing -eq '') {
    $U = Join-Path $Tmp 'unit'; New-Item -ItemType Directory -Path $U -Force | Out-Null
    function New-SynthExe([string]$Name, [string]$Content) { $p = Join-Path $U $Name; Set-Content -LiteralPath $p -Value $Content -Encoding ASCII; return $p }
    $exeBuilt   = New-SynthExe 't_built.exe'   'MZ synthetic built'
    $exeStaleF  = New-SynthExe 't_failed.exe'  'MZ stale leftover of a failed entry'
    $exeStaleD  = New-SynthExe 't_disabled.exe' 'MZ stale leftover of a disabled entry'
    $tests = @(
        @{ Name = 't_disabled'; Type = 'cl'; Output = 't_disabled.exe'; Disabled = $true },
        @{ Name = 't_built';    Type = 'cl'; Output = 't_built.exe' },
        @{ Name = 't_failed';   Type = 'cl'; Output = 't_failed.exe' },
        @{ Name = 't_prereq';   Type = 'cl'; Output = 't_prereq.exe'; Disabled = $true; DisabledReason = 'prerequisite missing: Npcap SDK not found at C:\npcap-sdk-1.16' },
        @{ Name = 't_skipped';  Type = 'nmake'; Makefile = 'x.mak' },
        @{ Name = 't_nmake';    Type = 'nmake'; Makefile = 'y.mak' },
        @{ Name = 't_vanished'; Type = 'cl'; Output = 't_vanished.exe' },
        @{ Name = 't_carry';    Type = 'cl'; Output = 't_carry.exe' },
        @{ Name = 't_nowdis';   Type = 'cl'; Output = 't_nowdis.exe'; Disabled = $true }
    )
    $outcomes = @{
        't_disabled' = @{ Status = 'disabled'; Reason = 'entry is Disabled in Build-Tests.ps1'; OutputPath = $null }
        't_built'    = @{ Status = 'built';  Reason = ''; OutputPath = $exeBuilt }
        't_failed'   = @{ Status = 'failed'; Reason = 'compiler exit code 2 or output missing'; OutputPath = $null }
        't_prereq'   = @{ Status = 'disabled'; Reason = 'prerequisite missing: Npcap SDK not found at C:\npcap-sdk-1.16'; OutputPath = $null }
        't_skipped'  = @{ Status = 'skipped'; Reason = 'VS dev shell not initialized (nmake not invoked)'; OutputPath = $null }
        't_nmake'    = @{ Status = 'built'; Reason = ''; OutputPath = $null }
        't_vanished' = @{ Status = 'built'; Reason = ''; OutputPath = (Join-Path $U 't_vanished.exe') }
    }   # t_carry, t_nowdis: not selected in this run (-TestName) although an earlier run built them
    $m = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $tests -Outcomes $outcomes -SourceRevision 'rev-now' -SourceDirty $false
    $byName = @{}; foreach ($e in @($m.entries)) { $byName[$e.name] = $e }
    function Show-E($e) { return ("status={0} enabled={1} artifact={2} sha={3} reason={4}" -f $e.status, $e.enabled, $e.artifact, $e.sha256, $e.reason) }

    $eb = $byName['t_built']
    tc_check 'TEST-HILPROV-020-built-entry-records-artifact-and-sha256' `
        (($eb.status -eq 'built') -and $eb.enabled -and ($eb.artifact -eq 't_built.exe') -and ($eb.sha256 -eq (Get-FileHash -LiteralPath $exeBuilt -Algorithm SHA256).Hash) -and ($eb.sourceRevision -eq 'rev-now')) (Show-E $eb)
    $ef = $byName['t_failed']
    tc_check 'TEST-HILPROV-021-failed-build-never-hashes-a-leftover-executable' (($ef.status -eq 'failed') -and ($null -eq $ef.sha256) -and ($ef.artifact -eq 't_failed.exe')) (Show-E $ef)
    $ed = $byName['t_disabled']
    tc_check 'TEST-HILPROV-022-disabled-entry-with-stale-exe-is-recorded-disabled-without-sha' (($ed.status -eq 'disabled') -and (-not $ed.enabled) -and ($null -eq $ed.sha256)) (Show-E $ed)
    $ep = $byName['t_prereq']
    tc_check 'TEST-HILPROV-023-runtime-prerequisite-is-recorded' (($ep.status -eq 'disabled') -and ($ep.prerequisites -match 'Npcap SDK') -and (-not $ep.enabled)) (Show-E $ep)
    $es = $byName['t_skipped']
    tc_check 'TEST-HILPROV-024-skipped-build-claims-nothing' (($es.status -eq 'skipped') -and ($null -eq $es.sha256)) (Show-E $es)
    $en = $byName['t_nmake']
    tc_check 'TEST-HILPROV-025-build-without-declared-artifact-is-not-dispatchable' (($en.status -eq 'built') -and ($null -eq $en.artifact) -and ($null -eq $en.sha256)) (Show-E $en)
    $ev = $byName['t_vanished']
    tc_check 'TEST-HILPROV-026-reported-success-without-artifact-is-failed' (($ev.status -eq 'failed') -and ($null -eq $ev.sha256)) (Show-E $ev)
    $ec = $byName['t_carry']; $ew = $byName['t_nowdis']
    $bmParams = @((Get-Command New-TestBuildManifest).Parameters.Keys)
    tc_check 'TEST-HILPROV-027-records-of-earlier-runs-are-never-carried-over' `
        (($ec.status -eq 'not-built') -and ($null -eq $ec.sha256) -and ($ec.reason -match 'never carried over') -and
         ($ew.status -eq 'disabled') -and ($null -eq $ew.sha256) -and ($bmParams -notcontains 'Previous')) `
        ("not-selected: {0} | disabled: {1} | params: {2}" -f (Show-E $ec), (Show-E $ew), ($bmParams -join ','))
    $rev = @($tests[($tests.Count - 1)..0])
    $m2 = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $rev -Outcomes $outcomes -SourceRevision 'rev-now' -SourceDirty $false
    $j1 = $m | ConvertTo-Json -Depth 6; $j2 = $m2 | ConvertTo-Json -Depth 6
    $names = @($m.entries | ForEach-Object { $_.name })
    tc_check 'TEST-HILPROV-028-manifest-is-deterministic-and-sorted' (($j1 -ceq $j2) -and (($names -join ',') -eq ((@($names | Sort-Object)) -join ','))) `
        ("equal={0}; order={1}" -f ($j1 -ceq $j2), ($names -join ','))
    $mp = Join-Path $U 'test-build-manifest.json'
    Set-Content -LiteralPath $mp -Value '{"old":true}' -Encoding ASCII
    Write-TestBuildManifest -Manifest $m -Path $mp
    $back = Get-Content -LiteralPath $mp -Raw | ConvertFrom-Json
    $left = @(Get-ChildItem -LiteralPath $U -Filter 'test-build-manifest.json.tmp-*')
    Remove-Item -LiteralPath $mp -Force
    Write-TestBuildManifest -Manifest $m -Path $mp
    tc_check 'TEST-HILPROV-029-atomic-write-replaces-and-creates-without-temp-leftovers' `
        (($back.schemaVersion -eq 1) -and (@($back.entries).Count -eq $tests.Count) -and ($left.Count -eq 0) -and (Test-Path -LiteralPath $mp) -and
         (@(Get-ChildItem -LiteralPath $U -Filter 'test-build-manifest.json.tmp-*').Count -eq 0)) ("tmp leftovers={0}" -f $left.Count)

    # Runner decision functions on synthetic artifacts
    $script:repoRoot = $RepoRoot
    Write-TestBuildManifest -Manifest $m -Path $mp
    $rm = Read-AvbBuildManifest -Path $mp -Configuration 'Debug'
    $srcOk = @{ Ok = $true; Revision = 'rev-now'; Dirty = $false; Error = '' }
    $d1 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rm -Source $srcOk
    $d2 = Get-AvbHilProvenanceDecision -ArtifactPath $exeStaleD -Manifest $rm -Source $srcOk
    $d3 = Get-AvbHilProvenanceDecision -ArtifactPath $exeStaleF -Manifest $rm -Source $srcOk
    $dupM = [ordered]@{ schemaVersion = 1; configuration = 'Debug'; platform = 'x64'; entries = @(
        [ordered]@{ name = 'a'; enabled = $true; status = 'built'; artifact = 't_built.exe'; sha256 = (Get-FileHash -LiteralPath $exeBuilt).Hash },
        [ordered]@{ name = 'b'; enabled = $true; status = 'built'; artifact = 't_built.exe'; sha256 = (Get-FileHash -LiteralPath $exeBuilt).Hash }) }
    $dupP = Join-Path $U 'dup.json'; ($dupM | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $dupP -Encoding UTF8
    $d4 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest (Read-AvbBuildManifest -Path $dupP -Configuration 'Debug') -Source $srcOk
    $repoPs1 = Join-Path $RepoRoot 'tests\unit\ioctl\test_hil_safety_gate.ps1'
    tc_check 'TEST-HILPROV-030-runner-decisions-on-synthetic-artifacts' `
        (($d1.Action -eq 'Run') -and ($d2.Action -eq 'Block') -and ($d2.Reason -match 'Disabled') -and ($d3.Action -eq 'Block') -and ($d3.Reason -match "status is 'failed'") -and
         ($d4.Action -eq 'Block') -and ($d4.Reason -match 'ambiguous')) ("built={0} disabled={1}:{2} failed={3}:{4} dup={5}:{6}" -f $d1.Action, $d2.Action, $d2.Reason, $d3.Action, $d3.Reason, $d4.Action, $d4.Reason)
    tc_check 'TEST-HILPROV-031-scope-repository-ps1-exempt-everything-else-checked' `
        ((-not (Test-AvbHilNeedsProvenance -Entry 'ps1:tests/unit/ioctl/test_hil_safety_gate.ps1' -ArtifactPath $repoPs1)) -and
         (Test-AvbHilNeedsProvenance -Entry 'ps1:C:/elsewhere/x.ps1' -ArtifactPath 'C:\elsewhere\x.ps1') -and
         (Test-AvbHilNeedsProvenance -Entry 'test_ioctl_version' -ArtifactPath (Join-Path $RepoRoot 'build\tools\avb_test\x64\Debug\test_ioctl_version.exe'))) 'scope decision wrong'

    # Source identity: a matching SHA256 is never sufficient on its own
    $mOldRev = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $tests -Outcomes $outcomes -SourceRevision 'rev-old' -SourceDirty $false
    $pOld = Join-Path $U 'm_oldrev.json'; Write-TestBuildManifest -Manifest $mOldRev -Path $pOld
    $mDirty = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $tests -Outcomes $outcomes -SourceRevision 'rev-now' -SourceDirty $true
    $pDirty = Join-Path $U 'm_dirty.json'; Write-TestBuildManifest -Manifest $mDirty -Path $pDirty
    $mUnknown = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $tests -Outcomes $outcomes -SourceRevision $null -SourceDirty $null
    $pUnknown = Join-Path $U 'm_unknown.json'; Write-TestBuildManifest -Manifest $mUnknown -Path $pUnknown
    $d5 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest (Read-AvbBuildManifest -Path $pOld -Configuration 'Debug') -Source $srcOk
    tc_check 'TEST-HILPROV-032-artifact-built-at-another-commit-is-blocked-despite-matching-sha' `
        (($d5.Action -eq 'Block') -and ($d5.Reason -match "built from revision 'rev-old' but the checkout is 'rev-now'")) ("{0}: {1}" -f $d5.Action, $d5.Reason)
    $d6 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest (Read-AvbBuildManifest -Path $pDirty -Configuration 'Debug') -Source $srcOk
    tc_check 'TEST-HILPROV-033-artifact-built-from-uncommitted-sources-is-blocked' `
        (($d6.Action -eq 'Block') -and ($d6.Reason -match 'uncommitted or unknown source state')) ("{0}: {1}" -f $d6.Action, $d6.Reason)
    $d7 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rm -Source @{ Ok = $true; Revision = 'rev-now'; Dirty = $true; Error = '' }
    tc_check 'TEST-HILPROV-034-sources-edited-after-the-build-at-same-head-block' `
        (($d7.Action -eq 'Block') -and ($d7.Reason -match 'uncommitted tracked changes')) ("{0}: {1}" -f $d7.Action, $d7.Reason)
    $d8 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rm -Source @{ Ok = $false; Revision = $null; Dirty = $null; Error = 'git rev-parse HEAD failed' }
    $d9 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest (Read-AvbBuildManifest -Path $pUnknown -Configuration 'Debug') -Source $srcOk
    tc_check 'TEST-HILPROV-035-unknown-source-identity-fails-closed' `
        (($d8.Action -eq 'Block') -and ($d8.Reason -match 'source revision unavailable') -and ($d9.Action -eq 'Block')) ("current={0}: {1} | build={2}: {3}" -f $d8.Action, $d8.Reason, $d9.Action, $d9.Reason)
    # Under GitHub Actions the checked-out HEAD must equal the dispatched GITHUB_SHA (read-only git on this checkout).
    $script:IsSandbox = $false; $script:repoRoot = $RepoRoot
    $saveGa = $env:GITHUB_ACTIONS; $saveSha = $env:GITHUB_SHA; $saveEv = $env:GITHUB_EVENT_NAME; $saveExp = $env:AVB_HIL_EXPECTED_REVISION
    try {
        Remove-Item Env:GITHUB_EVENT_NAME, Env:AVB_HIL_EXPECTED_REVISION -ErrorAction SilentlyContinue
        $env:GITHUB_ACTIONS = 'true'; $env:GITHUB_SHA = ('0' * 40)
        $sMis = Get-AvbSourceIdentity
        $head = (& git -C $RepoRoot rev-parse HEAD).Trim()
        $env:GITHUB_SHA = $head
        $sOk = Get-AvbSourceIdentity
        # workflow_run (.github/workflows/hil.yml): GITHUB_SHA is the default-branch tip; the authorized upstream
        # head_sha arrives as AVB_HIL_EXPECTED_REVISION and is mandatory.
        $env:GITHUB_EVENT_NAME = 'workflow_run'; $env:GITHUB_SHA = ('f' * 40)
        $wNone = Get-AvbSourceIdentity
        $env:AVB_HIL_EXPECTED_REVISION = $head
        $wOk = Get-AvbSourceIdentity
        $env:AVB_HIL_EXPECTED_REVISION = ('0' * 40)
        $wMis = Get-AvbSourceIdentity
        $env:AVB_HIL_EXPECTED_REVISION = 'master'
        $wBad = Get-AvbSourceIdentity
    } finally {
        $env:GITHUB_ACTIONS = $saveGa; $env:GITHUB_SHA = $saveSha; $env:GITHUB_EVENT_NAME = $saveEv; $env:AVB_HIL_EXPECTED_REVISION = $saveExp
    }
    tc_check 'TEST-HILPROV-040-github-actions-checkout-must-equal-authorized-sha' `
        ((-not $sMis.Ok) -and ($sMis.Error -match 'not the authorized commit GITHUB_SHA') -and $sOk.Ok -and ($sOk.Revision -eq $head)) ("mismatch: ok={0} '{1}' | match: ok={2}" -f $sMis.Ok, $sMis.Error, $sOk.Ok)
    tc_check 'TEST-HILPROV-041-workflow-run-binds-head-to-the-authorized-upstream-sha-not-github-sha' `
        ((-not $wNone.Ok) -and ($wNone.Error -match 'required for workflow_run') -and $wOk.Ok -and ($wOk.Revision -eq $head) -and
         (-not $wMis.Ok) -and ($wMis.Error -match 'AVB_HIL_EXPECTED_REVISION') -and (-not $wBad.Ok)) `
        ("none: {0} '{1}' | ok: {2} | mismatch: {3} '{4}' | malformed: {5}" -f $wNone.Ok, $wNone.Error, $wOk.Ok, $wMis.Ok, $wMis.Error, $wBad.Ok)
    $outcomesReenable = @{ 't_built' = @{ Status = 'failed'; Reason = 'compiler exit code 2 or output missing'; OutputPath = $null } }
    $testsReenable = @(@{ Name = 't_built'; Type = 'cl'; Output = 't_built.exe' }, @{ Name = 't_reenabled'; Type = 'cl'; Output = 't_reenabled.exe' })
    $mPartial = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $testsReenable -Outcomes $outcomesReenable -SourceRevision 'rev-now' -SourceDirty $false
    $pb = @($mPartial.entries | Where-Object { $_.name -eq 't_built' })[0]; $pr = @($mPartial.entries | Where-Object { $_.name -eq 't_reenabled' })[0]
    tc_check 'TEST-HILPROV-036-failed-partial-rebuild-and-reenabled-entry-authorize-nothing' `
        (($pb.status -eq 'failed') -and ($null -eq $pb.sha256) -and ($pr.status -eq 'not-built') -and ($null -eq $pr.sha256)) ("failed: {0} | re-enabled: {1}" -f (Show-E $pb), (Show-E $pr))

    # --- Local pre-commit provenance (tools/lib/SourceDigest.ps1): uncommitted builds are attributable by build-input digest;
    # CI keeps the strict clean-commit rule. -------------------------------------------------------------------------
    $dg = ('A' * 64); $dgOther = ('B' * 64)
    $mL = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $tests -Outcomes $outcomes -SourceRevision 'rev-now' -SourceDirty $true -SourceDigest $dg -SourceDigestFiles 7 -BuiltUtc '2026-10-21T08:00:00.0000000Z'
    $mpL = Join-Path $U 'manifest-local.json'; Write-TestBuildManifest -Manifest $mL -Path $mpL
    $rmL = Read-AvbBuildManifest -Path $mpL -Configuration 'Debug'
    $mN = New-TestBuildManifest -Configuration 'Debug' -Platform 'x64' -Tests $tests -Outcomes $outcomes -SourceRevision 'rev-now' -SourceDirty $true
    $mpN = Join-Path $U 'manifest-nodigest.json'; Write-TestBuildManifest -Manifest $mN -Path $mpN
    $rmN = Read-AvbBuildManifest -Path $mpN -Configuration 'Debug'
    $srcLocal = @{ Ok = $true; Revision = 'rev-now'; Dirty = $true; Mode = 'local'; Digest = $dg; DigestError = ''; Error = '' }
    $l1 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmL -Source $srcLocal
    tc_check 'TEST-HILPROV-042-local-dirty-build-runs-when-build-inputs-are-unchanged' ($l1.Action -eq 'Run') ("{0}: {1}" -f $l1.Action, $l1.Reason)
    $l2 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmL -Source ($srcLocal.Clone() | ForEach-Object { $_.Digest = $dgOther; $_ })
    tc_check 'TEST-HILPROV-043-source-change-after-build-invalidates-local-provenance' (($l2.Action -eq 'Block') -and ($l2.Reason -match 'build inputs changed after')) ("{0}: {1}" -f $l2.Action, $l2.Reason)
    $l3 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmN -Source $srcLocal
    tc_check 'TEST-HILPROV-044-local-build-without-digest-is-blocked' (($l3.Action -eq 'Block') -and ($l3.Reason -match 'no build-input digest')) ("{0}: {1}" -f $l3.Action, $l3.Reason)
    $srcLocalNoDg = @{ Ok = $true; Revision = 'rev-now'; Dirty = $true; Mode = 'local'; Digest = $null; DigestError = 'git ls-files failed'; Error = '' }
    $l4 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmL -Source $srcLocalNoDg
    tc_check 'TEST-HILPROV-045-unavailable-current-digest-fails-closed' (($l4.Action -eq 'Block') -and ($l4.Reason -match 'digest unavailable')) ("{0}: {1}" -f $l4.Action, $l4.Reason)
    $srcCiDirty = @{ Ok = $true; Revision = 'rev-now'; Dirty = $true; Mode = 'ci'; Digest = $dg; DigestError = ''; Error = '' }
    $srcCiClean = @{ Ok = $true; Revision = 'rev-now'; Dirty = $false; Mode = 'ci'; Digest = $dg; DigestError = ''; Error = '' }
    $c1 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmL -Source $srcCiDirty
    $c2 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmL -Source $srcCiClean
    $c3 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rm -Source $srcOk
    tc_check 'TEST-HILPROV-046-ci-strict-provenance-unchanged-dirty-tree-or-dirty-build-blocks-even-with-matching-digest' `
        (($c1.Action -eq 'Block') -and ($c1.Reason -match 'uncommitted tracked changes') -and ($c2.Action -eq 'Block') -and ($c2.Reason -match 'uncommitted or unknown source state') -and ($c3.Action -eq 'Run')) `
        ("dirty tree: {0} {1} | dirty build: {2} {3} | clean: {4}" -f $c1.Action, $c1.Reason, $c2.Action, $c2.Reason, $c3.Action)
    $srcLocalRev = @{ Ok = $true; Revision = 'rev-next'; Dirty = $true; Mode = 'local'; Digest = $dg; DigestError = ''; Error = '' }
    $l5 = Get-AvbHilProvenanceDecision -ArtifactPath $exeBuilt -Manifest $rmL -Source $srcLocalRev
    $l6 = Get-AvbHilProvenanceDecision -ArtifactPath $exeStaleD -Manifest $rmL -Source $srcLocal
    tc_check 'TEST-HILPROV-047-local-mode-still-requires-same-head-and-a-current-enabled-build-entry' `
        (($l5.Action -eq 'Block') -and ($l5.Reason -match "built from revision 'rev-now'") -and ($l6.Action -eq 'Block')) ("rev: {0} {1} | stale/disabled: {2} {3}" -f $l5.Action, $l5.Reason, $l6.Action, $l6.Reason)
    tc_check 'TEST-HILPROV-048-manifest-records-provenance-mode-digest-and-build-time' `
        (($mL.provenanceMode -eq 'local-worktree') -and ($mL.sourceDigest -eq $dg) -and ($mL.sourceDigestFiles -eq 7) -and ($mL.builtUtc -eq '2026-10-21T08:00:00.0000000Z') -and ((Get-Content -LiteralPath $BuildPath -Raw) -match '-BuiltUtc \(\(Get-Date\)\.ToUniversalTime\(\)') -and
         (@($mL.entries | Where-Object { $_.sourceDigest -ne $dg }).Count -eq 0) -and ($m.provenanceMode -eq 'clean-commit') -and ($mN.provenanceMode -eq 'unattributable')) `
        ("modes: local={0} clean={1} nodigest={2}" -f $mL.provenanceMode, $m.provenanceMode, $mN.provenanceMode)

    # Real digest function on a throw-away git repository (no repository files touched).
    $G = Join-Path $U 'digest_repo'
    New-Item -ItemType Directory -Path (Join-Path $G 'include'), (Join-Path $G 'tests'), (Join-Path $G 'docs') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $G 'include\a.h') -Value '#define A 1' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $G 'tests\t.c') -Value 'int main(void){return 0;}' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $G 'docs\readme.md') -Value 'docs' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $G '.gitignore') -Value '*.obj' -Encoding ASCII
    $prevEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $G init -q 2>$null | Out-Null; & git -C $G -c user.email=t@t -c user.name=t add -A 2>$null | Out-Null
    & git -C $G -c user.email=t@t -c user.name=t -c core.autocrlf=false commit -q -m init 2>$null | Out-Null
    $ErrorActionPreference = $prevEap
    function DG { return (Get-AvbBuildInputDigest -RepoRoot $G -Roots @('include', 'tests') -Submodules @()) }
    $g0 = DG
    Set-Content -LiteralPath (Join-Path $G 'tests\t.c') -Value 'int main(void){return 1;}' -Encoding ASCII; $g1 = DG
    Set-Content -LiteralPath (Join-Path $G 'tests\t.c') -Value 'int main(void){return 0;}' -Encoding ASCII; $g2 = DG
    Set-Content -LiteralPath (Join-Path $G 'tests\new_helper.h') -Value '#define NEW 1' -Encoding ASCII; $g3 = DG
    Remove-Item -LiteralPath (Join-Path $G 'tests\new_helper.h'); Set-Content -LiteralPath (Join-Path $G 'tests\x.obj') -Value 'obj' -Encoding ASCII; $g4 = DG
    Set-Content -LiteralPath (Join-Path $G 'docs\readme.md') -Value 'docs changed' -Encoding ASCII; $g5 = DG
    Remove-Item -LiteralPath (Join-Path $G 'include\a.h'); $g6 = DG
    $gSub = Get-AvbBuildInputDigest -RepoRoot $G -Roots @('include', 'tests') -Submodules @('missing_submodule')
    tc_check 'TEST-HILPROV-049-build-input-digest-tracks-edits-new-and-deleted-inputs-and-is-deterministic' `
        ($g0.Ok -and ($g1.Digest -ne $g0.Digest) -and ($g2.Digest -eq $g0.Digest) -and ($g3.Digest -ne $g0.Digest) -and ($g4.Digest -eq $g0.Digest) -and
         ($g5.Digest -eq $g0.Digest) -and ($g6.Digest -ne $g0.Digest) -and -not $gSub.Ok) `
        ("ok={0} edit={1} revert={2} new={3} ignored={4} outside={5} deleted={6} missingSubmodule.Ok={7} {8}" -f $g0.Ok, ($g1.Digest -ne $g0.Digest), ($g2.Digest -eq $g0.Digest),
         ($g3.Digest -ne $g0.Digest), ($g4.Digest -eq $g0.Digest), ($g5.Digest -eq $g0.Digest), ($g6.Digest -ne $g0.Digest), $gSub.Ok, $g0.Error)
}

# ---------------------------------------------------------------------------------------------------------
# Behavioral: sandbox copy of the runner with synthetic build manifests
# ---------------------------------------------------------------------------------------------------------
$Tree   = Join-Path $Tmp 'tree'
$Fakes  = Join-Path $Tmp 'fakes'
$RanLog = Join-Path $Tmp 'ran.log'
foreach ($d in (Join-Path $Tree 'tools\test'), (Join-Path $Tree 'src'), (Join-Path $Tree 'include'), $Fakes) {
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}
foreach ($f in 'Run-Tests-CI.ps1', 'Convert-Logs-To-JUnit.ps1') {
    Copy-Item (Join-Path $RepoRoot "tools\test\$f") (Join-Path $Tree "tools\test\$f")
}
New-Item -ItemType Directory -Path (Join-Path $Tree 'tools\lib') -Force | Out-Null
Copy-Item (Join-Path $RepoRoot 'tools\lib\TestVerdict.ps1') (Join-Path $Tree 'tools\lib\TestVerdict.ps1')
Copy-Item (Join-Path $RepoRoot 'tools\lib\NativeExit.ps1') (Join-Path $Tree 'tools\lib\NativeExit.ps1')
Copy-Item (Join-Path $RepoRoot 'tools\lib\SourceDigest.ps1') (Join-Path $Tree 'tools\lib\SourceDigest.ps1')
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
           'test_sbprov_mismatch=ReadOnly;test_sbprov_newmtime=ReadOnly;test_sbprov_oldmtime=ReadOnly;test_sbprov_missing=ReadOnly;' +
           'test_sbprov_failed=ReadOnly;test_sbprov_skipped=ReadOnly;test_sbprov_nomanifest=ReadOnly;test_sbprov_wrongcfg=ReadOnly'

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
function New-MEntry([string]$Name, [string]$Status, [bool]$Enabled = $true, [string]$Sha = $null, [string]$Reason = '', $Dirty = $false, $Digest = $null) {
    return [ordered]@{ name = $Name; type = 'sandbox-fake'; enabled = $Enabled; status = $Status; reason = $Reason; artifact = "$Name.ps1"; sha256 = $Sha
                       sourceRevision = 'sandbox-rev'; sourceDirty = $Dirty; sourceDigest = $Digest }
}
function Write-Manifest([object[]]$Entries, [string]$Configuration = 'Debug') {
    $m = [ordered]@{ schemaVersion = 1; producer = 'test_hil_binary_provenance.ps1'; configuration = $Configuration; platform = 'x64'; entries = $Entries }
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
    param([string]$Name, [string[]]$Tests, [string]$Suite = 'HardwareUnit', [string]$SourceRevision = 'sandbox-rev', [string]$SourceDirty = '0',
          [string]$SourceMode = '', [string]$SourceDigest = '')
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
        AVB_TEST_SANDBOX_SOURCE_REVISION = $SourceRevision; AVB_TEST_SANDBOX_SOURCE_DIRTY = $SourceDirty
        AVB_TEST_SANDBOX_SOURCE_MODE = $SourceMode; AVB_TEST_SANDBOX_SOURCE_DIGEST = $SourceDigest
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
function Test-ProvBlocked($r, [string]$Name, [string]$ReasonRx = '.') {
    return ((Get-RanCount $r $Name) -eq 0) -and
           ($r.Out -match ("\[BLOCKED\] TC-SAFETY-GATE-002: [^\r\n]*{0}[^\r\n]*provenance: [^\r\n]*{1}" -f [regex]::Escape($Name), $ReasonRx))
}
function Show-Prov($r, [string]$Name) {
    $l = @($r.Out -split "`n" | Where-Object { $_ -match [regex]::Escape($Name) -and $_ -match 'BLOCKED|RAN|SKIP' } | Select-Object -First 1)
    return ("ran={0}; line={1}" -f (Get-RanCount $r $Name), $(if ($l.Count) { $l[0].Trim() } else { '<none>' }))
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
    (New-MEntry 'test_sbprov_halter' 'built' $true (Get-Sha $pHalter)),
    (New-MEntry 'test_sbprov_disabled' 'disabled' $false $null 'KM placeholder only (mirrors Build-Tests.ps1 test_tsn_ioctl_handlers)')
)
$rA = Invoke-Sandbox 'runA' @((Get-Entry $pHalter), (Get-Entry $pDisabled))
tc_check 'TEST-HILPROV-000-fixture-reproduces-halt-before-readonly-item' `
    (($rA.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbprov_halter') -and ($rA.Out -match '\[HALT\] state-integrity halt')) (Tail $rA)
tc_check 'TEST-HILPROV-001-disabled-build-entry-blocks-dispatch-even-readonly-during-halt' (Test-ProvBlocked $rA 'test_sbprov_disabled' 'Disabled') (Show-Prov $rA 'test_sbprov_disabled')

# --- Run B: provenance matrix ---------------------------------------------------------------------------
$pCurrent  = New-Fake 'test_sbprov_current'
$pStale    = New-Fake 'test_sbprov_stale'
$pMismatch = New-Fake 'test_sbprov_mismatch'
$pNewMtime = New-Fake 'test_sbprov_newmtime'
$pOldMtime = New-Fake 'test_sbprov_oldmtime'
$pFailed   = New-Fake 'test_sbprov_failed'
$pSkipped  = New-Fake 'test_sbprov_skipped'
$pMissing  = Join-Path $Fakes 'test_sbprov_missing.ps1'      # listed as built but the artifact is absent
(Get-Item -LiteralPath $pNewMtime).LastWriteTime = (Get-Date).AddDays(1)
(Get-Item -LiteralPath $pOldMtime).LastWriteTime = Get-Date '2020-01-01'
Write-Manifest @(
    (New-MEntry 'test_sbprov_current'  'built' $true (Get-Sha $pCurrent)),
    (New-MEntry 'test_sbprov_mismatch' 'built' $true (Get-ShaOfText 'content produced by the current build')),
    (New-MEntry 'test_sbprov_newmtime' 'built' $true (Get-ShaOfText 'content produced by the current build (newmtime)')),
    (New-MEntry 'test_sbprov_oldmtime' 'built' $true (Get-Sha $pOldMtime)),
    (New-MEntry 'test_sbprov_missing'  'built' $true (Get-ShaOfText 'missing')),
    (New-MEntry 'test_sbprov_failed'   'failed' $true $null 'compiler exit code 2 or output missing'),
    (New-MEntry 'test_sbprov_skipped'  'skipped' $true $null 'VS dev shell not initialized (nmake not invoked)')
)   # test_sbprov_stale deliberately absent: not produced by the current build
$rB = Invoke-Sandbox 'runB' @((Get-Entry $pCurrent), (Get-Entry $pStale), (Get-Entry $pMismatch), (Get-Entry $pNewMtime), (Get-Entry $pOldMtime),
                              (Get-Entry $pMissing), (Get-Entry $pFailed), (Get-Entry $pSkipped))

tc_check 'TEST-HILPROV-002-artifact-absent-from-current-build-manifest-is-blocked' (Test-ProvBlocked $rB 'test_sbprov_stale' 'not recorded') (Show-Prov $rB 'test_sbprov_stale')
tc_check 'TEST-HILPROV-003-artifact-sha256-mismatch-is-blocked' (Test-ProvBlocked $rB 'test_sbprov_mismatch' 'SHA256 mismatch') (Show-Prov $rB 'test_sbprov_mismatch')
tc_check 'TEST-HILPROV-004-newer-mtime-does-not-authorize-a-mismatched-artifact' (Test-ProvBlocked $rB 'test_sbprov_newmtime' 'SHA256 mismatch') (Show-Prov $rB 'test_sbprov_newmtime')
tc_check 'TEST-HILPROV-005-authorized-current-build-artifact-runs' ((Get-RanCount $rB 'test_sbprov_current') -eq 1) (Show-Prov $rB 'test_sbprov_current')
tc_check 'TEST-HILPROV-006-old-mtime-does-not-block-an-authorized-artifact' ((Get-RanCount $rB 'test_sbprov_oldmtime') -eq 1) (Show-Prov $rB 'test_sbprov_oldmtime')
tc_check 'TEST-HILPROV-007-missing-artifact-is-not-executed' `
    (((Get-RanCount $rB 'test_sbprov_missing') -eq 0) -and ($rB.Out -match 'test_sbprov_missing\.ps1 \(binary not found|TC-SAFETY-GATE-00[12]: [^\r\n]*test_sbprov_missing')) (Show-Prov $rB 'test_sbprov_missing')
tc_check 'TEST-HILPROV-008-provenance-blocks-fail-the-suite' ($rB.Exit -eq 1) ("expected exit 1; observed exit {0}" -f $rB.Exit)
tc_check 'TEST-HILPROV-015-failed-build-entry-is-blocked' (Test-ProvBlocked $rB 'test_sbprov_failed' "status is 'failed'") (Show-Prov $rB 'test_sbprov_failed')
tc_check 'TEST-HILPROV-016-skipped-build-entry-is-blocked' (Test-ProvBlocked $rB 'test_sbprov_skipped' "status is 'skipped'") (Show-Prov $rB 'test_sbprov_skipped')
tc_check 'TEST-HILPROV-018-provenance-block-is-a-junit-failure' `
    ($rB.Xml -match '<testcase name="TC-SAFETY-GATE-002: [^"]*test_sbprov_stale[^"]*"[^>]*><failure') ("junit length={0}" -f $rB.Xml.Length)

# --- Run C: no manifest at all -> every non-repository artifact blocked ----------------------------------
$pNoMan = New-Fake 'test_sbprov_nomanifest'
Remove-Item -LiteralPath $ManifestPath -Force
$rC = Invoke-Sandbox 'runC' @((Get-Entry $pNoMan))
tc_check 'TEST-HILPROV-013-missing-manifest-blocks-and-fails-the-suite' ((Test-ProvBlocked $rC 'test_sbprov_nomanifest' 'manifest not found') -and $rC.Exit -eq 1) (Show-Prov $rC 'test_sbprov_nomanifest')

# --- Run D: manifest of another configuration -------------------------------------------------------------
$pWrongCfg = New-Fake 'test_sbprov_wrongcfg'
Write-Manifest @((New-MEntry 'test_sbprov_wrongcfg' 'built' $true (Get-Sha $pWrongCfg))) -Configuration 'Release'
$rD = Invoke-Sandbox 'runD' @((Get-Entry $pWrongCfg))
tc_check 'TEST-HILPROV-014-manifest-of-another-configuration-is-rejected' (Test-ProvBlocked $rD 'test_sbprov_wrongcfg' "configuration 'Release'") (Show-Prov $rD 'test_sbprov_wrongcfg')

# --- Run F/G: checkout moved to another commit / tracked sources edited after the build -------------------
$pRev = New-Fake 'test_sbprov_wrongcfg'
Write-Manifest @((New-MEntry 'test_sbprov_wrongcfg' 'built' $true (Get-Sha $pRev)))
$rF = Invoke-Sandbox 'runF' @((Get-Entry $pRev)) -SourceRevision 'sandbox-rev-NEXT'
tc_check 'TEST-HILPROV-037-checkout-at-new-commit-without-rebuild-is-blocked' `
    ((Test-ProvBlocked $rF 'test_sbprov_wrongcfg' "built from revision 'sandbox-rev' but the checkout is 'sandbox-rev-NEXT'") -and $rF.Exit -eq 1) (Show-Prov $rF 'test_sbprov_wrongcfg')
$rG = Invoke-Sandbox 'runG' @((Get-Entry $pRev)) -SourceDirty '1'
tc_check 'TEST-HILPROV-038-dirty-checkout-at-same-head-is-blocked' `
    ((Test-ProvBlocked $rG 'test_sbprov_wrongcfg' 'uncommitted tracked changes') -and $rG.Exit -eq 1) (Show-Prov $rG 'test_sbprov_wrongcfg')
$rH = Invoke-Sandbox 'runH' @((Get-Entry $pRev))
tc_check 'TEST-HILPROV-039-same-clean-revision-control-runs' ((Get-RanCount $rH 'test_sbprov_wrongcfg') -eq 1) (Show-Prov $rH 'test_sbprov_wrongcfg')

# --- Run L: local pre-commit provenance end to end (uncommitted working tree, build-input digest) -----------
$dgL = ('C' * 64)
$pLoc = New-Fake 'test_sbprov_wrongcfg'
Write-Manifest @((New-MEntry 'test_sbprov_wrongcfg' 'built' $true (Get-Sha $pLoc) '' $true $dgL))
$rL1 = Invoke-Sandbox 'runL1' @((Get-Entry $pLoc)) -SourceDirty '1' -SourceMode 'local' -SourceDigest $dgL
tc_check 'TEST-HILPROV-050-local-uncommitted-build-runs-and-is-labelled-local-evidence' `
    (((Get-RanCount $rL1 'test_sbprov_wrongcfg') -eq 1) -and ($rL1.Out -match '\[LOCAL-EVIDENCE\]') -and ($rL1.Out -match "provenance mode: local; build-input digest: $dgL")) (Show-Prov $rL1 'test_sbprov_wrongcfg')
$rL2 = Invoke-Sandbox 'runL2' @((Get-Entry $pLoc)) -SourceDirty '1' -SourceMode 'local' -SourceDigest ('D' * 64)
tc_check 'TEST-HILPROV-051-local-edit-after-build-blocks-the-binary-and-fails-the-suite' `
    ((Test-ProvBlocked $rL2 'test_sbprov_wrongcfg' 'build inputs changed after') -and $rL2.Exit -eq 1) (Show-Prov $rL2 'test_sbprov_wrongcfg')
$rL3 = Invoke-Sandbox 'runL3' @((Get-Entry $pLoc)) -SourceDigest $dgL
tc_check 'TEST-HILPROV-052-ci-mode-rejects-a-local-worktree-build-despite-matching-digest' `
    ((Test-ProvBlocked $rL3 'test_sbprov_wrongcfg' 'uncommitted or unknown source state') -and $rL3.Exit -eq 1 -and ($rL3.Out -notmatch '\[LOCAL-EVIDENCE\]')) (Show-Prov $rL3 'test_sbprov_wrongcfg')
# --- Run E: hosted Unit suite is not subject to the HIL provenance gate -----------------------------------
$pHosted = New-Fake 'test_sbprov_hosted'
Remove-Item -LiteralPath $ManifestPath -Force
$rE = Invoke-Sandbox 'runE' @((Get-Entry $pHosted)) -Suite 'Unit'
tc_check 'TEST-HILPROV-017-hosted-unit-suite-not-gated-by-hil-provenance' `
    (((Get-RanCount $rE 'test_sbprov_hosted') -eq 1) -and ($rE.Out -notmatch 'TC-SAFETY-GATE-002') -and $rE.Exit -eq 0) (Show-Prov $rE 'test_sbprov_hosted')

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILPROV: dispatch does not enforce artifact provenance" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILPROV: dispatch enforces current-build artifact provenance" -ForegroundColor Green
exit 0
