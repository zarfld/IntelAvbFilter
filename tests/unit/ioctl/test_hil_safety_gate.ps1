# test_hil_safety_gate.ps1
# TEST-HILGATE-001..: HIL safety gate and adapter/PHC state integrity enforced by Run-Tests-CI.ps1 (#343).
#
# The HIL tests are the Hardware* suites run by Run-Tests-CI.ps1 on the self-hosted runner.  This test
# proves the runner itself (not metadata) enforces:
#   - every Hardware* entry is classified (ReadOnly / Init / Mutating / Mutating:Target / Unsafe / Unknown);
#   - unclassified, Unsafe and non-target-aware Mutating entries are BLOCKED and never executed;
#   - target-aware Mutating entries run once per supported, enabled adapter with AVB_HIL_TARGET_DID set;
#   - a supported but disabled adapter is BLOCKED and never enabled; an unsupported adapter is informational;
#   - post-test adapter/PHC state is compared with the baseline (also after a timeout kill), a difference is
#     CLEANUP_FAILED and halts every later non-ReadOnly test.
#
# Isolation: the runner, verdict library, JUnit converter and the two SSOT files it parses are copied into a
# temporary tree; the lifecycle library is replaced by inert stubs and AVB_TEST_SANDBOX=1 replaces the adapter
# state source with a JSON file.  Nothing here opens \\.\IntelAvbFilter, queries PnP, or writes repo logs.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$RunCIPath = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILGATE: HIL safety gate + state integrity in Run-Tests-CI.ps1" -ForegroundColor Cyan

# ---------------------------------------------------------------------------------------------------------
# Static: registry coverage (parsed from the runner AST, nothing executed)
# ---------------------------------------------------------------------------------------------------------
$ast = [System.Management.Automation.Language.Parser]::ParseFile($RunCIPath, [ref]$null, [ref]$null)
function Get-RunnerList([string]$name) {
    $as = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq $name }, $true) | Select-Object -First 1
    if ($null -eq $as) { return $null }
    return @($as.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value })
}
$hwLists = [ordered]@{}
foreach ($n in 'HardwareUnitTests', 'HardwareIntegrationTests', 'HardwareMandatoryTests', 'HardwareMutationTests') { $hwLists[$n] = Get-RunnerList $n }
$hwEntries = @($hwLists.Values | ForEach-Object { $_ } | Sort-Object -Unique)
# Attended-only HardwareMutation entries (decision A 2026-10-21): executed outside GitHub Actions, a declared
# coverage gap under it.  Keys from the AST (hashtable keys are not string constants of the right side).
$attendedOnly = @()
$aoAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                        $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                        $x.Left.VariablePath.UserPath -eq 'HardwareMutationAttendedOnly' }, $true) | Select-Object -First 1
if ($null -ne $aoAst) { $aoHt = $aoAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true); $attendedOnly = @($aoHt.KeyValuePairs | ForEach-Object { $_.Item1.Value }) }
$hwEntries = @(@($hwEntries) + @($attendedOnly) | Sort-Object -Unique)

# Declared automatic-HIL coverage gaps: suite -> ordered entry -> reason (keys and values from the AST).
function Get-RunnerGapTable([string]$name) {
    $as = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq $name }, $true) | Select-Object -First 1
    $t = [ordered]@{}
    if ($null -eq $as) { return $null }
    $h = $as.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    foreach ($kv in $h.KeyValuePairs) {
        $v = $kv.Item2.Find({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true)
        $t[$kv.Item1.Value] = if ($v) { $v.Value } else { '' }
    }
    return $t
}
$autoSuites = [ordered]@{ HardwareMandatory = 'HardwareMandatoryTests'; HardwareUnit = 'HardwareUnitTests'; HardwareIntegration = 'HardwareIntegrationTests' }
$gapTables = [ordered]@{}
foreach ($s in $autoSuites.Keys) { $gapTables[$s] = Get-RunnerGapTable "${s}CoverageGaps" }
$gapEntries = @($gapTables.Values | Where-Object { $null -ne $_ } | ForEach-Object { @($_.Keys) } | Sort-Object -Unique)

$registry = @{}
$regAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq 'HilTestSafety' }, $true) | Select-Object -First 1
if ($null -ne $regAst) {
    $ht = $regAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    foreach ($kv in $ht.KeyValuePairs) { $registry[$kv.Item1.Value] = ($kv.Item2.Extent.Text.Trim() -replace "^'|'$", '') }
}
$validClasses = @('ReadOnly', 'Init', 'Mutating', 'Mutating:Target', 'Unsafe', 'Unknown')

# The suite lists are authoritative (Q 2026-10-22): an entry written as 'x.exe' is the test 'x' - the runner must
# normalize it for every registry lookup and resolve the built artifact through the build manifest (Build-Tests Name
# -> Output), never report a listed test as unclassified or "binary missing" because of its spelling.
$fnKey = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'ConvertTo-AvbHilEntryKey' }, $true)
$fnArt = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Resolve-AvbHilArtifactName' }, $true)
if ($fnKey) { . ([scriptblock]::Create($fnKey.Extent.Text)) }
if ($fnArt) { . ([scriptblock]::Create($fnArt.Extent.Text)) }
$keyCases = [ordered]@{ 'test_minimal_ioctl.exe' = 'test_minimal_ioctl'; 'test_clock_config' = 'test_clock_config'
                        'test_ptp_phc_stability --case UT-CORR-005' = 'test_ptp_phc_stability --case UT-CORR-005'
                        'x.exe --case A' = 'x --case A'; 'ps1:tests/unit/a.ps1' = 'ps1:tests/unit/a.ps1' }
$keyBad = @(if ($fnKey) { $keyCases.Keys | Where-Object { (ConvertTo-AvbHilEntryKey $_) -ne $keyCases[$_] } | ForEach-Object { "$_ -> $(ConvertTo-AvbHilEntryKey $_)" } } else { 'ConvertTo-AvbHilEntryKey missing' })
tc_check 'TEST-HILGATE-078-listed-entry-spelling-is-normalized-for-the-registry' ($keyBad.Count -eq 0) ($keyBad -join '; ')
$man = @{ ByName = @{ 'hw_timestamping_control' = [pscustomobject]@{ name = 'hw_timestamping_control'; artifact = 'hw_timestamping_control_test.exe' }
                      'test_ptp_freq' = [pscustomobject]@{ name = 'test_ptp_freq'; artifact = 'test_ptp_freq.exe' }
                      'avb_capability_validation' = [pscustomobject]@{ name = 'avb_capability_validation'; artifact = $null } } }
$artCases = [ordered]@{ 'hw_timestamping_control' = 'hw_timestamping_control_test.exe'; 'test_ptp_freq' = 'test_ptp_freq.exe'; 'test_unknown' = 'test_unknown.exe'
                        'test_minimal_ioctl.exe' = 'test_minimal_ioctl.exe'; 'avb_capability_validation' = 'avb_capability_validation.exe' }
$artBad = @(if ($fnArt) { $artCases.Keys | Where-Object { (Resolve-AvbHilArtifactName -BinName $_ -Manifest $man) -ne $artCases[$_] } | ForEach-Object { "$_ -> $(Resolve-AvbHilArtifactName -BinName $_ -Manifest $man)" } } else { 'Resolve-AvbHilArtifactName missing' })
$noMan = if ($fnArt) { Resolve-AvbHilArtifactName -BinName 'hw_timestamping_control' -Manifest $null } else { '' }
tc_check 'TEST-HILGATE-079-listed-entry-artifact-resolved-through-the-build-manifest-name' ($artBad.Count -eq 0 -and $noMan -eq 'hw_timestamping_control.exe') `
    ("{0}; without manifest -> {1}" -f ($artBad -join '; '), $noMan)

$missing = @($hwEntries | Where-Object { -not $registry.ContainsKey($(if ($fnKey) { ConvertTo-AvbHilEntryKey $_ } else { $_ })) })
tc_check 'TEST-HILGATE-001-every-hardware-entry-classified' (($registry.Count -gt 0) -and ($missing.Count -eq 0)) `
    ("registry entries={0}; unclassified: {1}" -f $registry.Count, ($missing -join ', '))

$badClass = @($registry.Keys | Where-Object { $validClasses -notcontains $registry[$_] } | ForEach-Object { "$_=$($registry[$_])" })
$hwKeys0  = @($hwEntries | ForEach-Object { if ($fnKey) { ConvertTo-AvbHilEntryKey $_ } else { $_ } })
$stale    = @($registry.Keys | Where-Object { ($hwKeys0 -notcontains $_) -and ($gapEntries -notcontains $_) -and ($registry[$_] -ne 'Unsafe') })
tc_check 'TEST-HILGATE-002-registry-values-valid-no-stale-keys' (($registry.Count -gt 0) -and $badClass.Count -eq 0 -and $stale.Count -eq 0) `
    ("invalid: {0}; stale (not in any Hardware list or coverage-gap table): {1}" -f ($badClass -join ', '), ($stale -join ', '))

# The suite lists are authoritative (Q 2026-10-22): safety is enforced at DISPATCH, never by keeping a test out of a
# list.  Runner gate + hazard holds, both from the runner AST.
foreach ($n in 'Get-AvbHilGateDecision', 'Get-AvbHilEffectiveGaps') {
    $f = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $n }, $true)
    if ($f) { . ([scriptblock]::Create($f.Extent.Text)) }
}
$holds = @{}
$hAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                       $x.Left.VariablePath.UserPath -eq 'HilHazardHolds' }, $true) | Select-Object -First 1
function Get-AstStringValue($Node) {   # string literal, or a variable assigned a string literal in the runner
    $s = $Node.Find({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true)
    if ($s) { return $s.Value }
    $v = $Node.Find({ param($x) $x -is [System.Management.Automation.Language.VariableExpressionAst] }, $true)
    if (-not $v) { return '' }
    $a = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                        $x.Left.VariablePath.UserPath -eq $v.VariablePath.UserPath }, $true) | Select-Object -First 1
    if (-not $a) { return '' }
    $s = $a.Right.Find({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true)
    if ($s) { return $s.Value } else { return '' }
}
if ($hAst) { foreach ($kv in $hAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true).KeyValuePairs) { $holds[$kv.Item1.Value] = Get-AstStringValue $kv.Item2 } }
function Get-Cls([string]$e) { $k = if ($fnKey) { ConvertTo-AvbHilEntryKey $e } else { $e }; if ($registry.ContainsKey($k)) { return $registry[$k] }; return '' }
function Get-Gate([string]$e, [string]$Target, [bool]$Gha, [string[]]$Auth = @()) {
    $k = if ($fnKey) { ConvertTo-AvbHilEntryKey $e } else { $e }
    return Get-AvbHilGateDecision -Entry $k -Class (Get-Cls $e) -Target $Target -Halt '' -TargetCount $(if ($Target) { 1 } else { 0 }) `
        -AuthorizedUnsafe $Auth -UnderGitHubActions $Gha -HazardHolds $holds
}
# 032: unattended (GitHub Actions) without an exact target the gate executes ReadOnly entries only - the old list
#      rule, now proven per listed entry by the runner's own decision; with a target only ReadOnly/Mutating:Target.
$autoBad = @()
foreach ($s in $autoSuites.Keys) {
    foreach ($e in @($hwLists[$autoSuites[$s]])) {
        $c = Get-Cls $e
        if ((Get-Gate $e '' $true).Action -eq 'Run' -and $c -ne 'ReadOnly') { $autoBad += "${s}: $e=$c runs without target" }
        if ((Get-Gate $e '0x125B' $true).Action -eq 'Run' -and $c -notin @('ReadOnly', 'Mutating:Target')) { $autoBad += "${s}: $e=$c runs with target" }
    }
}
$autoCount = @($autoSuites.Values | ForEach-Object { @($hwLists[$_]) }).Count
tc_check 'TEST-HILGATE-032-unattended-dispatch-runs-only-readonly-or-exact-target-entries' (($autoCount -gt 0) -and $autoBad.Count -eq 0 -and $null -ne (Get-Command Get-AvbHilGateDecision -ErrorAction SilentlyContinue)) `
    ("automatic entries={0}; dispatched against the rule: {1}" -f $autoCount, ($autoBad -join ', '))

# 081: #328 hazard hold.  An entry whose source can issue SETUP_TAS/SETUP_FP/SETUP_PTM is never executed - whatever its
#      class, target, authorization or attendance - until the driver TAS/FP restore defect is hardware-verified fixed;
#      the hold is visible (BLOCKED with the evidence reference), the entry stays listed.
$tsnEntries = @('avb_test_portability', 'avb_test_i217', 'avb_test_i225', 'avb_test_i226_um', 'test_ioctl_tas', 'test_ioctl_fp_ptm', 'test_tsn_ioctl_handlers_um', 'test_tsn_ioctl_handlers')
$holdBad = @()
foreach ($e in $tsnEntries) {
    if (-not $holds.ContainsKey($e) -or $holds[$e] -notmatch '#328') { $holdBad += "${e}: no #328 hold"; continue }
    foreach ($tc in @(@('', $true), @('0x125B', $true), @('0x125B', $false))) {
        $d = Get-Gate $e $tc[0] $tc[1] @($e)
        if ($d.Action -ne 'Block' -or $d.Reason -notmatch '#328') { $holdBad += "${e}: target='$($tc[0])' gha=$($tc[1]) -> $($d.Action) $($d.Reason)" }
    }
}
tc_check 'TEST-HILGATE-081-tsn-configuration-entries-are-hazard-held-visibly-not-delisted' ($holdBad.Count -eq 0) ($holdBad -join '; ')

# 080: a listed entry is executed (or gate-blocked with its reason) - never ALSO reported as a NOT EXECUTED coverage gap.
$effBad = @()
if (Get-Command Get-AvbHilEffectiveGaps -ErrorAction SilentlyContinue) {
    $fx = [ordered]@{ 'a' = 'Mutating: x'; 'b.exe' = 'Mutating: y'; 'c' = 'Init: z' }
    $eff = Get-AvbHilEffectiveGaps -GapTable $fx -TestList @('a', 'b', 'd')
    if ((@($eff.Keys) -join ',') -ne 'c') { $effBad += "fixture -> [$(@($eff.Keys) -join ',')] expected [c]" }
} else { $effBad += 'Get-AvbHilEffectiveGaps missing' }
tc_check 'TEST-HILGATE-080-listed-entries-are-never-reported-as-coverage-gaps' ($effBad.Count -eq 0) ($effBad -join '; ')

# Every gap is classified, really excluded (not also listed), justified (non-ReadOnly/Init class, reason),
# and its reason survives the JUnit case-name normalisation (no trailing parenthesis).
$gapBad = @()
foreach ($s in $autoSuites.Keys) {
    $t = $gapTables[$s]
    if ($null -eq $t) { $gapBad += "${s}: gap table missing"; continue }   # empty = no exclusion left (the goal)
    foreach ($e in @($t.Keys)) {
        $cls = $registry[$e]; $why = [string]$t[$e]
        if (-not $cls)                                  { $gapBad += "${s}: $e unclassified" }
        elseif ((@('ReadOnly', 'Init') -contains $cls) -and $why -cnotmatch "^$($cls): TEMPORARY POLICY EXCLUSION \d{4}-\d{2}-\d{2} ") { $gapBad += "${s}: $e is $cls (runnable; only an explicit, dated TEMPORARY POLICY EXCLUSION may exclude it)" }
        if (@($hwLists[$autoSuites[$s]]) -contains $e)  { $gapBad += "${s}: $e is both listed and a gap" }
        if ($why.Trim().Length -lt 10)                  { $gapBad += "${s}: $e has no reason" }
        if ($why -match '\)\s*$')                       { $gapBad += "${s}: $e reason ends with ')'" }
        if ($cls -and $why -notmatch ('^' + [regex]::Escape($cls) + ':')) { $gapBad += "${s}: $e reason does not start with its class '$($cls):'" }
    }
}
tc_check 'TEST-HILGATE-033-coverage-gaps-classified-excluded-and-justified' ($gapBad.Count -eq 0) ($gapBad -join '; ')

# 043: no required hardware test disappears silently.  The four tests that were only commented out of every list
# (P0 2026-10-21 sec. 1/5) are declared gaps - reported as NOT EXECUTED in every run - with their evidence-based class
# (sources: test_vv_corr_001_stability uses ENUM/OPEN/GET_TIMESTAMP/GET_HW_STATE/TEST_SEND_PTP only;
# test_s3_sleep_wake calls SetSuspendState; test_hot_plug devcon; test_lifecycle_coverage CM_Disable/Enable_DevNode).
$silent = @(); $wantCls = [ordered]@{ test_s3_sleep_wake = 'Unsafe'; test_vv_corr_001_stability = 'ReadOnly'; test_hot_plug = 'Unsafe'; test_lifecycle_coverage = 'Unsafe' }
foreach ($e in $wantCls.Keys) {
    if ($gapEntries -notcontains $e) { $silent += "$e is not a declared coverage gap (it would disappear from every run)" }
    if ($registry[$e] -ne $wantCls[$e]) { $silent += "$e classified '$($registry[$e])', expected '$($wantCls[$e])'" }
}
tc_check 'TEST-HILGATE-043-formerly-commented-out-tests-are-declared-gaps-never-silently-absent' ($silent.Count -eq 0) ($silent -join '; ')

# The TSN configuration entries (TAS/FP/PTM) and the observed irreversible PHC mutation are declared gaps.
$mustGap = @{ HardwareUnit = @('test_ioctl_tas', 'test_ioctl_fp_ptm', 'test_ptp_freq_complete')
              HardwareIntegration = @('avb_test_portability', 'avb_test_i217', 'avb_test_i225', 'avb_test_i226_um', 'test_tsn_ioctl_handlers_um') }
# Lists are authoritative (Q 2026-10-22): each entry is either a declared gap or listed and then contained at dispatch -
# TSN configuration by the #328 hazard hold (081); test_ptp_freq_complete (I226 TIMINCA CLEANUP_FAILED 2026-10-20) as
# exact-target with TIMINCA among the runner-verified states, so a repeat is CLEANUP_FAILED + halt, never hidden.
$notGap = @()
$verifiable = @(Get-RunnerList 'HilVerifiableState')
foreach ($s in $mustGap.Keys) { foreach ($e in $mustGap[$s]) {
    $isGap = ($null -ne $gapTables[$s] -and $gapTables[$s].Contains($e))
    $listed = (@($hwLists["${s}Tests"]) | ForEach-Object { if ($fnKey) { ConvertTo-AvbHilEntryKey $_ } else { $_ } }) -contains $e
    $contained = if ($e -eq 'test_ptp_freq_complete') { (Get-Cls $e) -eq 'Mutating:Target' -and $verifiable -contains 'TIMINCA' } else { $holds.ContainsKey($e) }
    if (-not ($isGap -or ($listed -and $contained))) { $notGap += "${s}: $e (gap=$isGap listed=$listed contained=$contained)" }
} }
tc_check 'TEST-HILGATE-034-tsn-and-irreversible-phc-entries-are-gaps-or-contained-at-dispatch' ($notGap.Count -eq 0) ("neither: {0}" -f ($notGap -join ', '))

# Coverage restored (Q 2026-10-22: repair, do not exclude).  test_ptp_phc_stability without arguments runs only
# UT-CORR-007/008 (main(): run_009 needs --case UT-CORR-009 or --allow-lifecycle, both runner-controlled); the
# driver-reinstall code in the binary is unreachable from the registered entry.  It executes, ReadOnly, not a gap.
$psGap = $gapTables['HardwareIntegration']
tc_check 'TEST-HILGATE-037-phc-stability-default-entry-executes-readonly' `
    (($registry['test_ptp_phc_stability'] -eq 'ReadOnly') -and (@($hwLists['HardwareIntegrationTests']) -contains 'test_ptp_phc_stability') -and
     -not (($null -ne $psGap) -and $psGap.Contains('test_ptp_phc_stability'))) `
    ("gap={0}; class={1}; listed={2}" -f $(if ($null -ne $psGap) { $psGap.Contains('test_ptp_phc_stability') } else { 'no table' }), $registry['test_ptp_phc_stability'], (@($hwLists['HardwareIntegrationTests']) -contains 'test_ptp_phc_stability'))
# test_ioctl_routing: INIT_DEVICE occurs only in the GetIoctlName() name table and is never issued; it sends
# ENUM_ADAPTERS, OPEN_ADAPTER, READ_REGISTER, GET_CLOCK_CONFIG -> ReadOnly; executed in HardwareUnit and HardwareMandatory.
$rtGap = @('HardwareUnit', 'HardwareMandatory' | Where-Object { $null -ne $gapTables[$_] -and $gapTables[$_].Contains('test_ioctl_routing') })
tc_check 'TEST-HILGATE-039-ioctl-routing-is-readonly-and-executes' `
    (($registry['test_ioctl_routing'] -eq 'ReadOnly') -and (@($hwLists['HardwareUnitTests']) -contains 'test_ioctl_routing') -and
     (@($hwLists['HardwareMandatoryTests']) -contains 'test_ioctl_routing') -and $rtGap.Count -eq 0) `
    ("class={0}; unit={1}; mandatory={2}; still a gap in: [{3}]" -f $registry['test_ioctl_routing'], (@($hwLists['HardwareUnitTests']) -contains 'test_ioctl_routing'),
     (@($hwLists['HardwareMandatoryTests']) -contains 'test_ioctl_routing'), ($rtGap -join ','))
# Init coverage is retained, not removed: every Init registration stays classified and is listed in a Hardware suite
# or a declared gap (never silently absent); each automatic suite keeps at least one ReadOnly entry, so the real-pass
# requirement (TEST-HILGATE-036) stays satisfiable.
$initKeys = @($registry.Keys | Where-Object { $registry[$_] -eq 'Init' } | Sort-Object)
$hwKeys = @($hwEntries | ForEach-Object { if ($fnKey) { ConvertTo-AvbHilEntryKey $_ } else { $_ } })
$initNotGap = @($initKeys | Where-Object { $gapEntries -notcontains $_ -and $hwKeys -notcontains $_ })
$noReadOnly = @($autoSuites.Keys | Where-Object { $sl = @($hwLists[$autoSuites[$_]]); @($sl | Where-Object { $registry[$_] -eq 'ReadOnly' }).Count -eq 0 })
$expectInit = @('test_clock_config', 'test_clock_working', 'test_dev_lifecycle', 'test_direct_clock', 'test_hw_state_machine', 'test_minimal_ioctl')
tc_check 'TEST-HILGATE-038-init-registrations-retained-as-declared-gaps-and-suites-keep-readonly-entries' `
    ((($initKeys -join ',') -eq ($expectInit -join ',')) -and $initNotGap.Count -eq 0 -and $noReadOnly.Count -eq 0) `
    ("init keys: [{0}]; not declared as gap: [{1}]; suites without a ReadOnly entry: [{2}]" -f ($initKeys -join ','), ($initNotGap -join ','), ($noReadOnly -join ','))


# 003: NIC/driver administration entries stay classified Unsafe; listed or not, dispatch never executes them under
#      GitHub Actions or without AVB_HIL_AUTHORIZE_UNSAFE naming the exact entry (attended run).
$unsafeKeys = @($registry.Keys | Where-Object { $registry[$_] -eq 'Unsafe' })
$needUnsafe = @('test_hot_plug', 'test_lifecycle_coverage' | Where-Object { $registry[$_] -ne 'Unsafe' })
$unsafeRuns = @(foreach ($e in $unsafeKeys) {
    if ((Get-Gate $e '0x125B' $true @($e)).Action -ne 'Block') { "${e}: runs under GitHub Actions" }
    if ((Get-Gate $e '0x125B' $false @()).Action -ne 'Block') { "${e}: runs attended without authorization" }
})
tc_check 'TEST-HILGATE-003-nic-admin-tests-unsafe-and-never-dispatched-unattended-or-unauthorized' (($registry.Count -gt 0) -and $unsafeRuns.Count -eq 0 -and $needUnsafe.Count -eq 0) `
    ("dispatched: {0}; not classified Unsafe: {1}" -f ($unsafeRuns -join ', '), ($needUnsafe -join ', '))

# The runner must never change adapter administration state (it only observes).
$runnerCode = [regex]::Replace([System.IO.File]::ReadAllText($RunCIPath), '(?m)#.*$', '')
$adminRx = '\b(Enable-PnpDevice|Disable-PnpDevice|Enable-NetAdapter|Disable-NetAdapter|Restart-NetAdapter|Enable-NetAdapterBinding|Disable-NetAdapterBinding|Set-NetAdapter\w*|Reset-NetAdapter\w*|pnputil|devcon|CM_Enable_DevNode|CM_Disable_DevNode|Restart-Service|Start-Service|Stop-Service)\b'
$adminHits = @([regex]::Matches($runnerCode, $adminRx) | ForEach-Object { $_.Value } | Sort-Object -Unique)
tc_check 'TEST-HILGATE-004-runner-contains-no-adapter-or-service-administration' ($adminHits.Count -eq 0) ("found: {0}" -f ($adminHits -join ', '))

# ---------------------------------------------------------------------------------------------------------
# Static: registry vs. source (comment- and string-stripped, local #include "..." followed)
# ---------------------------------------------------------------------------------------------------------
$buildLines = Get-Content (Join-Path $RepoRoot 'tools\build\Build-Tests.ps1')
$buildMap = @{}
for ($i = 0; $i -lt $buildLines.Count; $i++) {
    if ($buildLines[$i] -match '^\s*Name\s*=\s*"([^"]+)"') {
        $nm = $Matches[1]; $srcs = @()
        for ($j = $i + 1; $j -lt [Math]::Min($i + 14, $buildLines.Count); $j++) {
            if ($buildLines[$j] -match '^\s*Name\s*=') { break }
            if ($buildLines[$j] -match '^\s*(Source|ExtraSources)\s*=\s*"([^"]+)"') { $srcs += ($Matches[2] -split '\s+') }
        }
        if (-not $buildMap.ContainsKey($nm)) { $buildMap[$nm] = $srcs }
    }
}
function Get-StrippedSource([string]$bin, [bool]$keepStrings) {
    if (-not $buildMap.ContainsKey($bin)) { return $null }
    $queue = [System.Collections.Generic.List[string]]::new()
    foreach ($s in $buildMap[$bin]) { if ($s -notmatch 'avb_test_common\.c$') { $queue.Add((Join-Path $RepoRoot ($s -replace '/', '\'))) } }
    $seen = @(); $code = ''
    while ($queue.Count -gt 0) {
        $f = [System.IO.Path]::GetFullPath($queue[0]); $queue.RemoveAt(0)
        if ($seen -contains $f -or -not (Test-Path -LiteralPath $f)) { continue }
        $seen += $f
        $raw = [System.IO.File]::ReadAllText($f)
        foreach ($m in [regex]::Matches($raw, '#include\s+"([^"]+)"')) {
            $cand = Join-Path (Split-Path $f) $m.Groups[1].Value
            if ((Test-Path -LiteralPath $cand) -and $cand -notmatch '\\include\\' -and $cand -notmatch 'avb_test_common\.h$' -and $cand -notmatch 'avb_verdict\.h$') { $queue.Add($cand) }
        }
        $t = [regex]::Replace($raw, '(?s)/\*.*?\*/', ' ')
        $t = [regex]::Replace($t, '//[^\r\n]*', ' ')
        if (-not $keepStrings) { $t = [regex]::Replace($t, '"(?:\\.|[^"\\])*"', '""') }
        $code += "`n" + $t
    }
    return $code
}
$mutRx  = '\bIOCTL_AVB_(WRITE_REGISTER|SET_TIMESTAMP|SETUP_TAS|SETUP_FP|SETUP_PTM|MDIO_WRITE|SETUP_QAV|ADJUST_FREQUENCY|SET_HW_TIMESTAMPING|SET_RX_TIMESTAMP|SET_QUEUE_TIMESTAMP|SET_TARGET_TIME|PHC_OFFSET_ADJUST|SET_PORT_LATENCY|SET_LAUNCH_TIME|DISARM_TAS|RESET_STATISTICS|VLAN_ENABLE|VLAN_DISABLE|EEE_ENABLE|EEE_DISABLE|PFC_ENABLE|SRP_REGISTER_STREAM|SRP_DEREGISTER_STREAM)\b|\b(AvbWriteReg|AvbWriteRegVerified|AvbTestAdjustFrequency|AvbTestI210RestoreVerified|AvbTestI210ExpectRejectedUnchanged)\s*\('
$nicRx  = 'CM_Disable_DevNode|CM_Enable_DevNode|CM_Reenumerate_DevNode|SetupDiCallClassInstaller|DIF_PROPERTYCHANGE|SetSuspendState|ControlService|\bdevcon\b|\bpnputil\b|Disable-NetAdapter|Enable-NetAdapter|Restart-NetAdapter'
# Documented exceptions (each states why the hit is not executed by the registered entry).
$roExceptions  = @{ 'test_ioctl_abi' = 'sizeof/code checks only'; 'test_ptp_phc_stability' = 'no args runs UT-CORR-007/008 only; 005/006/009 need --case'
                    'test_ioctl_routing' = 'IOCTL_AVB_INIT_DEVICE only as a case label of GetIoctlName(); never passed to DeviceIoControl' }
$nicExceptions = @{ 'test_dev_lifecycle' = 'devcon appears only in a SKIP message'; 'test_statistics_counters' = 'devcon appears only in a SKIP message'
                    'test_ptp_phc_stability' = 'ControlService/system only in UT-CORR-009 (--case UT-CORR-009 / --allow-lifecycle)'
                    'test_ptp_phc_stability --case UT-CORR-005' = 'UT-CORR-009 not selected'; 'test_ptp_phc_stability --case UT-CORR-006' = 'UT-CORR-009 not selected' }

$bad005 = @(); $bad006 = @(); $bad007 = @(); $bad008 = @(); $bad009 = @()
foreach ($k in @($registry.Keys)) {
    $cls = $registry[$k]
    if ($cls -eq 'Unknown') { continue }
    $bin = ($k -split '\s+', 2)[0]
    $code = Get-StrippedSource $bin $false
    $codeS = Get-StrippedSource $bin $true
    if ($null -eq $code) { $bad008 += "${k}: no build entry"; continue }
    $mut = [regex]::IsMatch($code, $mutRx)
    $init = [regex]::IsMatch($code, '\bIOCTL_AVB_INIT_DEVICE\b')
    if ($cls -eq 'Mutating:Target') {
        $sel = [regex]::IsMatch($code, '\bAvbTestSelectTarget(Index)?\s*\(')
        $own = [regex]::IsMatch($code, '\bCreateFile[AW]?\s*\(|\bIOCTL_AVB_ENUM_ADAPTERS\b|\bIOCTL_AVB_OPEN_ADAPTER\b')
        $shared = [regex]::IsMatch($code, '\bAvbEnumerateAdapters\s*\(') -and [regex]::IsMatch($code, '\bAvbOpenAdapter\s*\(') -and
                  (($buildMap[$bin] -join ' ') -match 'avb_test_common\.c')
        if (-not ($sel -or ($shared -and -not $own))) { $bad005 += $k }
    }
    if ($cls -eq 'ReadOnly' -and ($mut -or $init) -and -not $roExceptions.ContainsKey($k)) { $bad006 += $k }
    if ($cls -eq 'Init' -and ($mut -or -not $init)) { $bad007 += $k }
    if (($cls -eq 'Mutating' -or $cls -eq 'Mutating:Target') -and -not $mut) { $bad008 += $k }
    if ($cls -ne 'Unsafe' -and [regex]::IsMatch($codeS, $nicRx) -and -not $nicExceptions.ContainsKey($k)) { $bad009 += $k }
}
tc_check 'TEST-HILGATE-005-target-aware-entries-select-the-exact-target' ($registry.Count -gt 0 -and $bad005.Count -eq 0) ("not exact-target-aware: {0}" -f ($bad005 -join ', '))
tc_check 'TEST-HILGATE-006-readonly-entries-contain-no-mutating-ioctl' ($registry.Count -gt 0 -and $bad006.Count -eq 0) ("mutating code in ReadOnly: {0}" -f ($bad006 -join ', '))
tc_check 'TEST-HILGATE-007-init-entries-only-init' ($registry.Count -gt 0 -and $bad007.Count -eq 0) ("Init misclassified: {0}" -f ($bad007 -join ', '))
tc_check 'TEST-HILGATE-008-mutating-entries-really-mutate' ($registry.Count -gt 0 -and $bad008.Count -eq 0) ("Mutating without mutating code: {0}" -f ($bad008 -join ', '))
tc_check 'TEST-HILGATE-009-nic-admin-only-in-unsafe-entries' ($registry.Count -gt 0 -and $bad009.Count -eq 0) ("NIC/driver administration outside Unsafe: {0}" -f ($bad009 -join ', '))

$commonC = [System.IO.File]::ReadAllText((Join-Path $RepoRoot 'tests\common\avb_test_common.c'))
$enumBody = [regex]::Match($commonC, '(?s)int\s+AvbEnumerateAdapters\s*\([^)]*\)\s*\{(.*?)\r?\n\}').Groups[1].Value
tc_check 'TEST-HILGATE-031-shared-enumerator-honours-target-and-fails-loudly' `
    (($enumBody -match 'AvbTestFilterToTarget\s*\(\s*out\s*,\s*count\s*\)') -and ($enumBody -match '\[BLOCKED\] TC-TARGET-001') -and ($enumBody -match '\bexit\s*\(\s*1\s*\)')) `
    'AvbEnumerateAdapters does not apply AvbTestFilterToTarget with a loud exit(1) on an unresolvable target'

# ---------------------------------------------------------------------------------------------------------
# Behavioral: sandbox copy of the runner
# ---------------------------------------------------------------------------------------------------------
$Tmp   = Join-Path $env:TEMP ("hilgate_{0}" -f (Get-Random))
$Tree  = Join-Path $Tmp 'tree'
$Fakes = Join-Path $Tmp 'fakes'
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
Copy-Item (Join-Path $RepoRoot 'tools\lib\HilLock.ps1') (Join-Path $Tree 'tools\lib\HilLock.ps1')   # machine safety record (Run-Tests-CI dot-sources it)
Copy-Item (Join-Path $RepoRoot 'tools\lib\PhcRestoration.ps1') (Join-Path $Tree 'tools\lib\PhcRestoration.ps1')   # PHC evaluator (D1)
@'
function Get-AvbLifecycleSnapshot { return $null }
function Write-LifecycleDiff { param($Before, $After, $Label) return $false }
'@ | Set-Content (Join-Path $Tree 'tools\test\Lib-AvbLifecycle.ps1') -Encoding UTF8
Copy-Item (Join-Path $RepoRoot 'src\avb_integration_fixed.c') (Join-Path $Tree 'src\avb_integration_fixed.c')
Copy-Item (Join-Path $RepoRoot 'include\intel_pci_ids.h')     (Join-Path $Tree 'include\intel_pci_ids.h')
$TreeRunner = Join-Path $Tree 'tools\test\Run-Tests-CI.ps1'

$Classes = 'test_sbfake_ro=ReadOnly;test_sbfake_ro2=ReadOnly;test_sbfake_init=Init;test_sbfake_mut=Mutating;' +
           'test_sbfake_tgt=Mutating:Target;test_sbfake_unsafe=Unsafe;test_sbfake_disabler=ReadOnly;' +
           'test_sbfake_sleeper=Init;test_sbfake_sleepchg=Init;test_sbfake_timinca=Mutating:Target;test_sbfake_skiponly=ReadOnly;' +
           'test_sbfake_tsauxc=ReadOnly;test_sbfake_link=ReadOnly;test_sbfake_setup=ReadOnly;test_sbfake_tgtphc=Mutating:Target;test_sbfake_lockdrop=ReadOnly;test_sbfake_tgtsleep=Mutating:Target;' +
           'test_sbfake_rosleep=ReadOnly;test_sbfake_rochild=ReadOnly;test_sbfake_roslow=ReadOnly;test_sbfake_tgtsetup=Mutating:Target;test_sbfake_tgtsetupphc=Mutating:Target;' +
           'test_sbfake_tgtts=Mutating:Target'
# Declared mutated state of the sandbox fakes (the runner's per-entry mutation matrix, $HilMutatedState).  tgt / timinca
# modify only TIMINCA, which the runner re-reads itself: restoration verified, fan-out continues (positive coverage of
# TEST-HILGATE-015/016/026).  tgtts also changes timestamp-control configuration, which the runner cannot observe:
# INCONCLUSIVE_RESTORATION and global halt.  tgtphc also sets the PHC time: NOT_JUDGED by the runner (Q 2026-10-27),
# INCONCLUSIVE_RESTORATION without a global halt.  Undeclared non-ReadOnly fakes (init, mut, ...) modify unknown state.
$Mutates = 'test_sbfake_tgtsleep=TIMINCA;test_sbfake_tgt=TIMINCA;test_sbfake_timinca=TIMINCA;test_sbfake_tgtphc=TIMINCA,PHC_TIME;test_sbfake_tgtts=TIMINCA,TS_CONTROL'

function New-Fake([string]$Name, [string]$Action = '') {
    $p = Join-Path $Fakes "$Name.ps1"
    $ran = $RanLog.Replace("'", "''")
    $body = "Add-Content -LiteralPath '$ran' -Value ('RAN $Name target=' + `$env:AVB_HIL_TARGET_DID)`r`n" +
            "$Action`r`n" +
            "Write-Host '  [PASS] TC-SBFAKE-001: $Name ok'`r`nexit 0`r`n"
    Set-Content -LiteralPath $p -Value $body -Encoding UTF8
    return ('ps1:' + $p.Replace('\', '/'))
}

# State edit snippet executed inside a fake (reads the JSON the runner reads).
function Edit-StateSnippet([string]$Edit) {
    return "`$sf = `$env:AVB_TEST_ADAPTER_STATE_FILE; `$s = Get-Content -LiteralPath `$sf -Raw | ConvertFrom-Json; $Edit; " +
           "(`$s | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath `$sf -Encoding UTF8"
}

function New-StateFile([string]$Path, [switch]$AllSupportedEnabled) {
    $state = [ordered]@{
        adapters = @(
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_125B&SUBSYS_00008086&REV_04\SB01'; PnpStatus = 'OK';    Problem = 0;  AdminStatus = 'Up';   MediaStatus = 'Up'; FilterBound = 'True';  Name = 'SB I226' },
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_1533&SUBSYS_00008086&REV_03\SB02'; PnpStatus = 'OK';    Problem = 0;  AdminStatus = 'Up';   MediaStatus = 'Up'; FilterBound = 'True';  Name = 'SB I210' },
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_15B7&SUBSYS_00008086&REV_31\SB03'; PnpStatus = 'Error'; Problem = 22; AdminStatus = 'Down'; MediaStatus = 'Disabled'; FilterBound = 'Unknown'; Name = 'SB I219' },
            [ordered]@{ InstanceId = 'PCI\VEN_8086&DEV_1539&SUBSYS_00008086&REV_03\SB04'; PnpStatus = 'OK';    Problem = 0;  AdminStatus = 'Up';   MediaStatus = 'Up'; FilterBound = 'False'; Name = 'SB I211' }
        )
        clock = @(
            [ordered]@{ Index = 0; Did = '0x125B'; Ok = $true; Timinca = 0x08000000; Tsauxc = 0 },
            [ordered]@{ Index = 1; Did = '0x1533'; Ok = $true; Timinca = 0;          Tsauxc = 0 }
        )
    }
    # Without the disabled I219 (TC-INFRA-ADAPTER-001 BLOCKED, a failure in every default run).
    if ($AllSupportedEnabled) { $state.adapters = @($state.adapters | Where-Object { $_.Problem -eq 0 }) }
    ($state | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Invoke-Sandbox {
    param([string]$Name, [string[]]$Tests, [string]$Suite = 'HardwareUnit', [int]$Timeout = 60,
          [hashtable]$ExtraEnv = @{}, [switch]$NoLogsDir, [switch]$AllSupportedEnabled, [string]$PriorResultsDir = '', [string]$SafetyFile = '',
          [string]$LockFile = '', [string]$RunnerArgs = '', [string]$SessionFile = '', [string]$StateEdit = '', [string]$Configuration = 'Debug')
    $dir  = Join-Path $Tmp $Name
    $logs = Join-Path $dir 'logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $stateFile = Join-Path $dir 'state.json'
    New-StateFile $stateFile -AllSupportedEnabled:$AllSupportedEnabled
    if ($StateEdit) { $s = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json; & ([scriptblock]::Create($StateEdit)) $s; ($s | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $stateFile -Encoding UTF8 }
    if (Test-Path $RanLog) { Remove-Item $RanLog -Force }
    # Hardware* suites dispatch only manifest-authorized artifacts (TEST-HILPROV, #328 F23); sandbox fakes are
    # artifacts, so record each existing fake as built by an enabled entry with its SHA256.
    $mEntries = @(); $mSeen = @{}
    foreach ($t in $Tests) {
        if (-not $t.StartsWith('ps1:')) { continue }
        $fp = $t.Substring(4).Replace('/', '\')
        if ($mSeen.ContainsKey($fp) -or -not (Test-Path -LiteralPath $fp)) { continue }
        $mSeen[$fp] = $true
        $mEntries += ,([ordered]@{ name = [System.IO.Path]::GetFileNameWithoutExtension($fp); type = 'sandbox-fake'; enabled = $true
                                   status = 'built'; reason = ''; artifact = [System.IO.Path]::GetFileName($fp)
                                   sha256 = (Get-FileHash -LiteralPath $fp -Algorithm SHA256).Hash
                                   sourceRevision = 'sandbox-rev'; sourceDirty = $false })
    }
    $mDir = Join-Path $Tree "build\tools\avb_test\x64\$Configuration"
    New-Item -ItemType Directory -Path $mDir -Force | Out-Null
    ([ordered]@{ schemaVersion = 1; producer = 'test_hil_safety_gate.ps1'; configuration = $Configuration; platform = 'x64'; entries = $mEntries } |
        ConvertTo-Json -Depth 5) | Set-Content -LiteralPath (Join-Path $mDir 'test-build-manifest.json') -Encoding UTF8
    $envs = [ordered]@{
        AVB_TEST_SANDBOX = '1'; AVB_TEST_ADAPTER_STATE_FILE = $stateFile; AVB_TEST_SANDBOX_CLASSES = $Classes
        AVB_TEST_SANDBOX_MUTATES = $Mutates; AVB_TEST_HIL_SAFETY_FILE = $SafetyFile; AVB_TEST_HIL_LOCK_FILE = $LockFile; AVB_TEST_HIL_SESSION_FILE = $SessionFile
        GITHUB_RUN_ID = ''; GITHUB_RUN_ATTEMPT = ''; GITHUB_JOB = ''
        AVB_TEST_STATE_SETTLE_SECONDS = '1'; GITHUB_ACTIONS = ''; GITHUB_ENV = ''; AVB_HIL_TARGET_DID = ''
        AVB_HIL_AUTHORIZE_UNSAFE = ''; AVB_TEST_HEALTH_FAIL_AFTER = ''; AVB_TEST_SANDBOX_TIMEOUTS = ''
        AVB_TEST_SANDBOX_SOURCE_REVISION = 'sandbox-rev'; AVB_TEST_SANDBOX_SOURCE_DIRTY = '0'
    }
    foreach ($k in $ExtraEnv.Keys) { $envs[$k] = $ExtraEnv[$k] }
    $lines = @()
    foreach ($k in $envs.Keys) {
        if ($envs[$k] -eq '') { $lines += "Remove-Item Env:$k -ErrorAction SilentlyContinue" }
        else { $lines += ("`$env:{0} = '{1}'" -f $k, $envs[$k].Replace("'", "''")) }
    }
    $testsLit = ($Tests | ForEach-Object { "'" + $_.Replace("'", "''") + "'" }) -join ', '
    $logsArg = if ($NoLogsDir) { '' } else { "-LogsDir '$logs'" }
    if ($PriorResultsDir) { $logsArg += " -PriorResultsDir '$PriorResultsDir'" }
    if ($RunnerArgs) { $logsArg += " $RunnerArgs" }
    # Unattended sandbox runs own a CI lock exactly like a hil.yml job (deploy step took it); everything else lets the
    # runner acquire its own sandbox lock.
    if ($envs['GITHUB_ACTIONS'] -eq 'true' -and -not $LockFile) {
        $lf = Join-Path $dir 'ci-hil.lock'
        ([ordered]@{ kind = 'ci'; label = 'sandbox job'; host = $env:COMPUTERNAME; pid = 1; runId = 'sbx-run'; runAttempt = '1'; job = 'sbx-job'; acquiredUtc = '2026-10-21T00:00:00Z' } | ConvertTo-Json) | Set-Content -LiteralPath $lf -Encoding UTF8
        $lines += "`$env:AVB_TEST_HIL_LOCK_FILE = '$lf'"; $lines += "`$env:GITHUB_RUN_ID = 'sbx-run'"; $lines += "`$env:GITHUB_RUN_ATTEMPT = '1'"; $lines += "`$env:GITHUB_JOB = 'sbx-job'"
    }
    $lines += "& '$TreeRunner' -Suite $Suite -Tests @($testsLit) -TestTimeoutSeconds $Timeout -Configuration $Configuration $logsArg"
    $lines += 'exit $LASTEXITCODE'
    $wrapper = Join-Path $dir 'wrapper.ps1'
    $lines | Set-Content -LiteralPath $wrapper -Encoding UTF8
    $stateHashBefore = (Get-FileHash -LiteralPath $stateFile -Algorithm SHA256).Hash

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -NonInteractive -File `"$wrapper`""
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $dir
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(300000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT'; Xml = ''; Ran = @(); Logs = @(); StateFile = $stateFile; StateHashBefore = $stateHashBefore; Dir = $dir } }
    $xml = (Get-ChildItem $logs -Filter 'junit-*.xml' -ErrorAction SilentlyContinue | ForEach-Object { Get-Content $_.FullName -Raw }) -join "`n"
    $ran = @(); if (Test-Path $RanLog) { $ran = @(Get-Content $RanLog) }
    $logNames = @(Get-ChildItem $logs -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result); Xml = $xml; Ran = $ran; Logs = $logNames
              StateFile = $stateFile; StateHashBefore = $stateHashBefore; Dir = $dir }
}
function Tail($r) { return (($r.Out -split "`n" | Select-Object -Last 25) -join ' | ') }
function Test-FailureCase($r, [string]$idRx) { return ($r.Xml -match ('<testcase name="' + $idRx + '[^"]*"[^>]*><failure')) }

# Without sandbox support the runner would run its real driver health check (opens the device) - never do
# that from an offline test.  Fail every behavioral case instead of executing it.
$behavioralIds = @('010-disabled-supported-adapter-blocked-not-enabled', '011-unsupported-adapter-informational-only',
    '012-unclassified-blocked-not-executed', '013-unsafe-blocked-authorization-ignored-under-github-actions',
    '014-mutating-without-exact-targeting-blocked-not-executed', '015-target-aware-mutating-fans-out-per-enabled-supported-did',
    '016-readonly-and-init-run-once-without-target-env', '017-blocked-entries-fail-the-suite',
    '018-sandbox-never-exports-hil-identity-or-junit-to-github-env',
    '020-state-change-is-cleanup-failed', '021-halt-blocks-later-non-readonly-but-not-readonly',
    '022-runner-never-restores-or-enables-the-adapter', '023-timeout-kill-still-verifies-post-state',
    '024-after-timeout-cleanup-failed-later-init-blocked', '025-timeout-of-non-readonly-halts-mutation-without-cleanup-failed',
    '026-timinca-change-is-cleanup-failed-for-that-target-only', '027-unsafe-runs-only-with-exact-entry-authorization',
    '028-sandbox-refuses-real-test-binaries', '029-hardware-entry-blocked-outside-hardware-suite',
    '030-sandbox-requires-isolated-logs-dir', '035-coverage-gaps-reported-as-skipped-junit-cases-never-executed',
    '036-hardware-suite-without-a-pass-fails', '039-init-blocked-unattended-but-runs-attended')
if ([System.IO.File]::ReadAllText($TreeRunner) -notmatch '\$env:AVB_TEST_SANDBOX\b') {
    foreach ($id in $behavioralIds) { tc_fail "TEST-HILGATE-$id" 'runner has no sandbox mode - behavioral case NOT executed (it would open the real device)' }
    Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
    Write-Host "[FAIL] TEST-HILGATE: safety gate regression" -ForegroundColor Red
    exit 1
}

# --- Run A: classification gate + baseline adapter availability -------------------------------------------
$tRo    = New-Fake 'test_sbfake_ro'
$tUncl  = New-Fake 'test_sbfake_unclassified'
$tUns   = New-Fake 'test_sbfake_unsafe'
$tMut   = New-Fake 'test_sbfake_mut'
$tTgt   = New-Fake 'test_sbfake_tgt'
$tInit  = New-Fake 'test_sbfake_init'
$ghEnv  = Join-Path $Tmp 'github_env_A.txt'
$rA = Invoke-Sandbox 'runA' @($tRo, $tUncl, $tUns, $tMut, $tTgt, $tInit) -ExtraEnv @{
    GITHUB_ACTIONS = 'true'; GITHUB_ENV = $ghEnv; AVB_HIL_AUTHORIZE_UNSAFE = 'test_sbfake_unsafe' }

tc_check 'TEST-HILGATE-010-disabled-supported-adapter-blocked-not-enabled' `
    (($rA.Out -match '\[BLOCKED\] TC-INFRA-ADAPTER-001/adapter:\d+: DID 0x15B7 ') -and (Test-FailureCase $rA 'TC-INFRA-ADAPTER-001/adapter:\d+: DID 0x15B7') -and
     ((Get-FileHash -LiteralPath $rA.StateFile -Algorithm SHA256).Hash -eq $rA.StateHashBefore)) (Tail $rA)
tc_check 'TEST-HILGATE-011-unsupported-adapter-informational-only' `
    (($rA.Out -match '\[INFO\] unsupported Intel adapter DID 0x1539') -and ($rA.Out -notmatch '\[BLOCKED\][^\r\n]*0x1539') -and
     (@($rA.Ran | Where-Object { $_ -match 'target=0x1539' }).Count -eq 0)) (Tail $rA)
tc_check 'TEST-HILGATE-012-unclassified-blocked-not-executed' `
    ((Test-FailureCase $rA 'TC-SAFETY-GATE-001: [^"]*test_sbfake_unclassified') -and (@($rA.Ran | Where-Object { $_ -match 'test_sbfake_unclassified' }).Count -eq 0)) (Tail $rA)
tc_check 'TEST-HILGATE-013-unsafe-blocked-authorization-ignored-under-github-actions' `
    ((Test-FailureCase $rA 'TC-SAFETY-GATE-001: [^"]*test_sbfake_unsafe') -and ($rA.Out -match 'ignored under GitHub Actions') -and
     (@($rA.Ran | Where-Object { $_ -match 'test_sbfake_unsafe' }).Count -eq 0)) (Tail $rA)
tc_check 'TEST-HILGATE-014-mutating-without-exact-targeting-blocked-not-executed' `
    ((Test-FailureCase $rA 'TC-SAFETY-GATE-001: [^"]*test_sbfake_mut[^_]') -and (@($rA.Ran | Where-Object { $_ -match 'RAN test_sbfake_mut ' }).Count -eq 0)) (Tail $rA)
$tgtRuns = @($rA.Ran | Where-Object { $_ -match '^RAN test_sbfake_tgt ' } | ForEach-Object { ($_ -split 'target=')[1].Trim() } | Sort-Object)
tc_check 'TEST-HILGATE-015-target-aware-mutating-fans-out-per-enabled-supported-did' `
    ((($tgtRuns -join ',') -eq '0x125B,0x1533') -and (@($rA.Logs | Where-Object { $_ -match '^test_sbfake_tgt@125B\.exe_' }).Count -eq 1) -and
     (@($rA.Logs | Where-Object { $_ -match '^test_sbfake_tgt@1533\.exe_' }).Count -eq 1)) ("targets run: [{0}]; logs: {1}; {2}" -f ($tgtRuns -join ','), ($rA.Logs -join ','), (Tail $rA))
$roRuns = @($rA.Ran | Where-Object { $_ -match '^RAN test_sbfake_(ro|init) ' })
# Run A is unattended (GITHUB_ACTIONS=true): ReadOnly runs once without a target; Init is blocked by policy (039).
tc_check 'TEST-HILGATE-016-readonly-runs-once-without-target-env-init-blocked-unattended' `
    (($roRuns.Count -eq 1) -and ($roRuns[0] -match '^RAN test_sbfake_ro target=$') -and (Test-FailureCase $rA 'TC-SAFETY-GATE-001: [^"]*test_sbfake_init[^"]*not approved for unattended HIL')) ("ran: {0}; {1}" -f ($rA.Ran -join ' / '), (Tail $rA))
tc_check 'TEST-HILGATE-017-blocked-entries-fail-the-suite' ($rA.Exit -eq 1) ("exit={0}" -f $rA.Exit)
$ghText = ''; if (Test-Path -LiteralPath $ghEnv) { $ghText = [System.IO.File]::ReadAllText($ghEnv) }
tc_check 'TEST-HILGATE-018-sandbox-never-exports-hil-identity-or-junit-to-github-env' ($ghText.Trim() -eq '') ("GITHUB_ENV received: {0}" -f ($ghText -replace "`r?`n", ' | '))

function Get-RanCount($r, [string]$rx) { return @($r.Ran | Where-Object { $_ -match $rx }).Count }
function Read-State($r) { return (Get-Content -LiteralPath $r.StateFile -Raw | ConvertFrom-Json) }

# --- Run B: a test changes adapter state -> CLEANUP_FAILED, halt, and the runner does not repair it -------
$tDis = New-Fake 'test_sbfake_disabler' (Edit-StateSnippet "`$a = @(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_125B' })[0]; `$a.Problem = 22; `$a.PnpStatus = 'Error'; `$a.AdminStatus = 'Down'")
$tRo2 = New-Fake 'test_sbfake_ro2'
$rB = Invoke-Sandbox 'runB' @($tDis, $tTgt, $tInit, $tRo2)
tc_check 'TEST-HILGATE-020-state-change-is-cleanup-failed' `
    (($rB.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbfake_disabler\.ps1: [^\r\n]*DEV_125B[^\r\n]*: Problem 0 -> 22') -and
     (Test-FailureCase $rB 'TC-INFRA-STATE-001: after test_sbfake_disabler')) (Tail $rB)
tc_check 'TEST-HILGATE-021-halt-blocks-later-non-readonly-but-not-readonly' `
    ((Get-RanCount $rB '^RAN test_sbfake_(tgt|init) ') -eq 0 -and (Get-RanCount $rB '^RAN test_sbfake_ro2 ') -eq 1 -and
     @([regex]::Matches($rB.Out, '\[BLOCKED\] TC-SAFETY-GATE-001: [^\r\n]*test_sbfake_(tgt|init)\.ps1: state-integrity halt')).Count -eq 3) ("ran: {0}; {1}" -f ($rB.Ran -join ' / '), (Tail $rB))
$sB = Read-State $rB
$a125 = @($sB.adapters | Where-Object { $_.InstanceId -match 'DEV_125B' })[0]
tc_check 'TEST-HILGATE-022-runner-never-restores-or-enables-the-adapter' (($a125.Problem -eq 22) -and ($a125.AdminStatus -eq 'Down')) ("final 125B: problem={0} admin={1}" -f $a125.Problem, $a125.AdminStatus)

# --- Run C: timeout kill after a state change -> post-test state is still verified ------------------------
$tSlc = New-Fake 'test_sbfake_sleepchg' ((Edit-StateSnippet "`$a = @(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_1533' })[0]; `$a.AdminStatus = 'Down'") + "`r`nStart-Sleep -Seconds 120")
$rC = Invoke-Sandbox 'runC' @($tSlc, $tInit) -Timeout 5
tc_check 'TEST-HILGATE-023-timeout-kill-still-verifies-post-state' `
    (($rC.Out -match 'test_sbfake_sleepchg\.ps1 TIMEOUT') -and ($rC.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbfake_sleepchg\.ps1: [^\r\n]*DEV_1533[^\r\n]*AdminStatus Up -> Down')) (Tail $rC)
tc_check 'TEST-HILGATE-024-after-timeout-cleanup-failed-later-init-blocked' ((Get-RanCount $rC '^RAN test_sbfake_init ') -eq 0 -and $rC.Exit -eq 1) ("ran: {0}" -f ($rC.Ran -join ' / '))

# --- Run D: timeout kill without an observable change -> halt (state not fully observable) -----------------
$tSlp = New-Fake 'test_sbfake_sleeper' 'Start-Sleep -Seconds 120'
$rD = Invoke-Sandbox 'runD' @($tSlp, $tTgt, $tRo) -Timeout 5
tc_check 'TEST-HILGATE-025-timeout-of-non-readonly-halts-mutation-without-cleanup-failed' `
    (($rD.Out -match 'test_sbfake_sleeper\.ps1 TIMEOUT') -and ($rD.Out -notmatch '\[CLEANUP_FAILED\]') -and ($rD.Out -match '\[HALT\][^\r\n]*killed after a timeout') -and
     (Get-RanCount $rD '^RAN test_sbfake_tgt ') -eq 0 -and (Get-RanCount $rD '^RAN test_sbfake_ro ') -eq 1) ("ran: {0}; {1}" -f ($rD.Ran -join ' / '), (Tail $rD))

# --- Run E: PHC frequency (TIMINCA) left changed on one target -> CLEANUP_FAILED for exactly that item -------
$tTim = New-Fake 'test_sbfake_timinca' ("if (`$env:AVB_HIL_TARGET_DID -eq '0x1533') { " + (Edit-StateSnippet "`$c = @(`$s.clock | Where-Object { `$_.Did -eq '0x1533' })[0]; `$c.Timinca = 34") + " }")
$rE = Invoke-Sandbox 'runE' @($tTim, $tRo)
tc_check 'TEST-HILGATE-026-timinca-change-is-cleanup-failed-for-that-target-only' `
    (($rE.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbfake_timinca\.ps1@0x1533: DID 0x1533 \(driver index 1\): TIMINCA 0x00000000 -> 0x00000022') -and
     ($rE.Out -notmatch 'CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbfake_timinca\.ps1@0x125B') -and (Get-RanCount $rE '^RAN test_sbfake_ro ') -eq 1) (Tail $rE)

# --- Run F: Unsafe runs only when the exact entry is authorized outside GitHub Actions -------------------
$rF1 = Invoke-Sandbox 'runF1' @($tUns) -ExtraEnv @{ AVB_HIL_AUTHORIZE_UNSAFE = $tUns }
$rF2 = Invoke-Sandbox 'runF2' @($tUns) -ExtraEnv @{ AVB_HIL_AUTHORIZE_UNSAFE = 'test_sbfake_unsafe' }
tc_check 'TEST-HILGATE-027-unsafe-runs-only-with-exact-entry-authorization' `
    ((Get-RanCount $rF1 '^RAN test_sbfake_unsafe ') -eq 1 -and (Get-RanCount $rF2 '^RAN test_sbfake_unsafe ') -eq 0 -and
     ($rF2.Out -match 'requires AVB_HIL_AUTHORIZE_UNSAFE to name this exact entry')) ("F1 ran={0}; F2 ran={1}; {2}" -f (Get-RanCount $rF1 'unsafe'), (Get-RanCount $rF2 'unsafe'), (Tail $rF2))

# --- Run G: sandbox never executes real binaries; a hardware entry never runs in a non-Hardware suite -------
$rG1 = Invoke-Sandbox 'runG1' @('test_ioctl_version')
$rG2 = Invoke-Sandbox 'runG2' @('test_hot_plug') -Suite 'Unit'
tc_check 'TEST-HILGATE-028-sandbox-refuses-real-test-binaries' `
    (($rG1.Out -match '\[BLOCKED\] TC-SAFETY-GATE-001: test_ioctl_version: sandbox mode') -and $rG1.Exit -eq 1) (Tail $rG1)
tc_check 'TEST-HILGATE-029-hardware-entry-blocked-outside-hardware-suite' `
    (($rG2.Out -match '\[BLOCKED\] TC-SAFETY-GATE-001: test_hot_plug: hardware test requested outside a Hardware\* suite') -and $rG2.Exit -eq 1) (Tail $rG2)

# --- Run H: sandbox mode refuses the default (HIL evidence) logs directory ---------------------------------
$rH = Invoke-Sandbox 'runH' @($tRo) -NoLogsDir
tc_check 'TEST-HILGATE-030-sandbox-requires-isolated-logs-dir' `
    (($rH.Exit -eq 1) -and ($rH.Out -match 'AVB_TEST_SANDBOX=1 requires -LogsDir') -and (Get-RanCount $rH 'RAN') -eq 0 -and
     -not (Test-Path (Join-Path $Tree 'logs'))) (Tail $rH)

# --- Run I: the suite's own list (no -Tests) reports every declared coverage gap as a FAILING case -----------
# Requirement changed 2026-10-21 (P0 full HIL coverage): a required test that is not executed (fixture/restoration or
# infrastructure missing) is never a skip and never green.  Sandbox mode refuses every real binary (TEST-HILGATE-028),
# so nothing from the real list executes.
$rI = Invoke-Sandbox 'runI' @() -Suite 'HardwareMandatory'
$gapI = @(if ($null -ne $gapTables['HardwareMandatory']) { $gapTables['HardwareMandatory'].Keys })
$gapMissing = @($gapI | Where-Object { $rI.Xml -notmatch ('<testcase name="TC-HIL-COVERAGE-GAP-001: ' + [regex]::Escape($_) + ' NOT EXECUTED by automatic HIL[^"]*"[^>]*><failure') })
$gapCases = [regex]::Matches($rI.Xml, '<testcase name="TC-HIL-COVERAGE-GAP-001: ').Count
$gapSkipped = [regex]::Matches($rI.Xml, '<testcase name="TC-HIL-COVERAGE-GAP-001: [^"]*"[^>]*><skipped').Count
tc_check 'TEST-HILGATE-035-coverage-gaps-reported-as-failing-junit-cases-never-executed-never-skipped' `
    (($gapI.Count -gt 0) -and $gapMissing.Count -eq 0 -and $gapCases -eq $gapI.Count -and $gapSkipped -eq 0 -and (Get-RanCount $rI 'RAN') -eq 0 -and
     ($rI.Out -match ('Coverage gaps \(required, NOT executed\): ' + $gapI.Count)) -and ($rI.Out -notmatch 'included in Skipped') -and $rI.Exit -eq 1) `
    ("gaps={0}; cases={1}; skipped={2}; missing: {3}; exit={4}; {5}" -f $gapI.Count, $gapCases, $gapSkipped, ($gapMissing -join ','), $rI.Exit, (Tail $rI))

# 035b: overall decision (pure, AST-extracted): no failure but a coverage gap or INCOMPLETE testcase -> INCOMPLETE, exit 1.
$ovFn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Get-AvbRunOverall' }, $true)
if ($null -eq $ovFn) { tc_fail 'TEST-HILGATE-035b-unexecuted-required-test-makes-the-run-incomplete-never-passed' 'Get-AvbRunOverall not found in Run-Tests-CI.ps1' }
else {
    . ([scriptblock]::Create($ovFn.Extent.Text))
    $ov = @(
        @(@{ IsHardwareSuite = $true;  Passed = 5; Failed = 0; Incomplete = 0; CoverageGaps = 3 }, 'INCOMPLETE',   1),
        @(@{ IsHardwareSuite = $true;  Passed = 5; Failed = 0; Incomplete = 2; CoverageGaps = 0 }, 'INCOMPLETE',   1),
        @(@{ IsHardwareSuite = $true;  Passed = 5; Failed = 1; Incomplete = 0; CoverageGaps = 3 }, 'FAILED',       1),
        @(@{ IsHardwareSuite = $true;  Passed = 0; Failed = 0; Incomplete = 0; CoverageGaps = 3 }, 'INCOMPLETE',   1),
        @(@{ IsHardwareSuite = $true;  Passed = 0; Failed = 0; Incomplete = 0; CoverageGaps = 0 }, 'NOT_VERIFIED', 1),
        @(@{ IsHardwareSuite = $false; Passed = 0; Failed = 0; Incomplete = 0; CoverageGaps = 0 }, 'NO_TESTS',     0),
        @(@{ IsHardwareSuite = $true;  Passed = 5; Failed = 0; Incomplete = 0; CoverageGaps = 0 }, 'PASSED',       0))
    $ovBad = @(); foreach ($c in $ov) { $p = $c[0]; $o = Get-AvbRunOverall @p; if ($o.Overall -ne $c[1] -or $o.Code -ne $c[2]) { $ovBad += ("{0} -> {1}/{2}, expected {3}/{4}" -f (($p.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ','), $o.Overall, $o.Code, $c[1], $c[2]) } }
    tc_check 'TEST-HILGATE-035b-unexecuted-required-test-makes-the-run-incomplete-never-passed' ($ovBad.Count -eq 0) ($ovBad -join '; ')
}

# --- Run K: a halt of an EARLIER suite of the same HIL job carries over (-PriorResultsDir) -------------------
# hil.yml runs the suites as separate runner processes; each takes a fresh baseline.  Without the carry-over a
# CLEANUP_FAILED / TIMEOUT / INFRA / state halt in one suite (or a suite killed mid-run: its in-progress marker is
# left behind) would let the next suite adopt the damaged state as baseline and mutate again (P0 2026-10-21 sec. 2).
function New-PriorDir([string]$Name, [string]$Verdict, [string]$Halt = '', [switch]$Marker) {
    $d = Join-Path $Tmp $Name; $s = Join-Path $d 'HardwareUnit'; New-Item -ItemType Directory -Path $s -Force | Out-Null
    if ($Verdict) {
        ([ordered]@{ schema = 'avb-runner-results/1'; suite = 'HardwareUnit'; configuration = 'Debug'; overall = 'FAILED'; exitCode = 1; stateHalt = $Halt
                     records = @([ordered]@{ name = 'test_prior.exe'; verdict = $Verdict }) } | ConvertTo-Json -Depth 5) |
            Set-Content -LiteralPath (Join-Path $s 'results-HardwareUnit-Debug-20261021_120000_000.json') -Encoding UTF8
    }
    if ($Marker) { '{"suite":"HardwareUnit"}' | Set-Content -LiteralPath (Join-Path $s 'run-in-progress.json') -Encoding UTF8 }
    return $d
}
$pCf  = New-PriorDir 'priorCleanup' 'CLEANUP_FAILED'
$pTo  = New-PriorDir 'priorTimeout' 'FAILED(TIMEOUT)'
$pHl  = New-PriorDir 'priorHalt' 'PASSED' -Halt 'adapter state not observable at baseline'
$pMk  = New-PriorDir 'priorKilled' '' -Marker
$pOk  = New-PriorDir 'priorClean' 'PASSED'
$pUv  = New-PriorDir 'priorUnverified' 'INCONCLUSIVE_RESTORATION'
$kRes = [ordered]@{}
$kDirs = [ordered]@{ cleanup = $pCf; timeout = $pTo; halt = $pHl; killed = $pMk; unverified = $pUv; clean = $pOk }
foreach ($k in $kDirs.GetEnumerator()) {
    $kRes[$k.Key] = Invoke-Sandbox ("runK_" + $k.Key) @($tInit, $tRo) -Suite 'HardwareMutation' -PriorResultsDir $k.Value -AllSupportedEnabled
}
$kBad = @()
foreach ($k in 'cleanup', 'timeout', 'halt', 'killed', 'unverified') {
    $r = $kRes[$k]
    if ((Get-RanCount $r '^RAN test_sbfake_init ') -ne 0 -or (Get-RanCount $r '^RAN test_sbfake_ro ') -ne 1 -or
        $r.Out -notmatch '\[BLOCKED\] TC-SAFETY-GATE-001: [^\r\n]*test_sbfake_init\.ps1: state-integrity halt \(prior suite') { $kBad += "${k}: " + (Tail $r) }
}
$rKc = $kRes['clean']
if ((Get-RanCount $rKc '^RAN test_sbfake_init ') -ne 1 -or $rKc.Out -match 'prior suite') { $kBad += 'clean prior results must not halt: ' + (Tail $rKc) }
$leftMarker = @(Get-ChildItem (Join-Path $rKc.Dir 'logs') -Filter 'run-in-progress.json' -Recurse -ErrorAction SilentlyContinue).Count
if ($leftMarker -ne 0) { $kBad += 'a completed run left its run-in-progress.json marker behind' }
tc_check 'TEST-HILGATE-040-earlier-suite-halt-or-killed-run-blocks-later-mutation-readonly-continues' ($kBad.Count -eq 0) ($kBad -join ' ### ')

# --- Run L: TSAUXC and link changes are enforced (P0 2026-10-21 sec. 2: never downgraded to warnings) --------
$tAux  = New-Fake 'test_sbfake_tsauxc' (Edit-StateSnippet "`$c = @(`$s.clock | Where-Object { `$_.Did -eq '0x1533' })[0]; `$c.Tsauxc = 4")
$tLink = New-Fake 'test_sbfake_link' (Edit-StateSnippet "`$a = @(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_125B' })[0]; `$a.MediaStatus = 'Disconnected'")
$rL1 = Invoke-Sandbox 'runL1' @($tAux, $tInit) -AllSupportedEnabled
$rL2 = Invoke-Sandbox 'runL2' @($tLink, $tInit) -AllSupportedEnabled
tc_check 'TEST-HILGATE-041-tsauxc-and-link-changes-are-cleanup-failed-and-halt-never-warnings' `
    (($rL1.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbfake_tsauxc\.ps1: [^\r\n]*DID 0x1533 \(driver index 1\): TSAUXC 0x00000000 -> 0x00000004') -and
     ($rL2.Out -match '\[CLEANUP_FAILED\] TC-INFRA-STATE-001: after test_sbfake_link\.ps1: [^\r\n]*DEV_125B[^\r\n]*: link Up -> Disconnected') -and
     (Get-RanCount $rL1 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rL2 '^RAN test_sbfake_init ') -eq 0 -and
     ($rL1.Out + $rL2.Out) -notmatch 'WARN:STATE\][^\r\n]*(TSAUXC|link)') `
    ("tsauxc: {0} ### link: {1}" -f (Tail $rL1), (Tail $rL2))

# --- Run M: a state-changing test whose restoration the runner cannot fully verify is never PASSED -------------
# The runner verifies adapter presence/PnP/admin/link/binding and TIMINCA/TSAUXC, NOT PHC time continuity, rate or
# offset (and none of TAS/FP/Qav/EEE/PFC/VLAN/MDIO/timestamp-control state).  Until it does, every executed
# Init/Mutating/Mutating:Target/Unsafe test gets an INCONCLUSIVE_RESTORATION record (missing evidence, distinct from
# CLEANUP_FAILED = observed damage) and - "stop subsequent mutating tests if restoration cannot be verified" (Q P0
# 2026-10-21, refined the same day) - halts every later non-ReadOnly test; ReadOnly tests continue.  The unverifiable
# state here is configuration (TS_CONTROL); PHC time/rate alone no longer halt (TEST-HILGATE-083, Q 2026-10-27).
$tTgtPhc = New-Fake 'test_sbfake_tgtphc'
$tTgtTs  = New-Fake 'test_sbfake_tgtts'
$rM = Invoke-Sandbox 'runM' @($tTgtTs, $tInit, $tRo) -AllSupportedEnabled -ExtraEnv @{ AVB_HIL_TARGET_DID = '0x125B' }
$mUnv = @([regex]::Matches($rM.Out, '\[INCONCLUSIVE_RESTORATION\] TC-INFRA-STATE-002: after (test_sbfake_\w+)\.ps1')) | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$mJson = @(Get-ChildItem (Join-Path $rM.Dir 'logs') -Filter 'results-*.json' -ErrorAction SilentlyContinue | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
$mRec = @($mJson | ForEach-Object { @($_.records) } | Where-Object { $_.name -match 'STATE-UNVERIFIED' })
tc_check 'TEST-HILGATE-042-unverifiable-restoration-is-inconclusive-restoration-halts-later-mutation-never-passed' `
    ((Get-RanCount $rM '^RAN test_sbfake_tgtts ') -ge 1 -and (@($mUnv) -join ',') -eq 'test_sbfake_tgtts' -and
     ($rM.Out -match 'TC-INFRA-STATE-002: [^\r\n]*PHC time') -and (Test-FailureCase $rM 'TC-INFRA-STATE-002') -and $rM.Exit -eq 1 -and
     ($rM.Out -match '\[HALT\][^\r\n]*restoration not verified') -and (Get-RanCount $rM '^RAN test_sbfake_init ') -eq 0 -and
     (Get-RanCount $rM '^RAN test_sbfake_ro ') -eq 1 -and $mRec.Count -ge 1 -and @($mRec | Where-Object { $_.verdict -ne 'INCONCLUSIVE_RESTORATION' }).Count -eq 0 -and
     ($rM.Out -notmatch '\[CLEANUP_FAILED\] TC-INFRA-STATE')) `
    ("unverified records for: [{0}] json verdicts=[{1}] exit={2}; {3}" -f (@($mUnv) -join ','), (@($mRec | ForEach-Object { $_.verdict }) -join ','), $rM.Exit, (Tail $rM))

# 046 (negative counterpart of the positive fan-out tests 015/016/026; option 1, Q 2026-10-21): restoration that the
# runner cannot verify on the FIRST target halts every later mutation globally - the remaining targets are BLOCKED,
# each record naming its intended adapter - while ReadOnly tests continue.  Per-adapter isolation (option 2) is not
# enabled until isolation is hardware-verified.
$rF = Invoke-Sandbox 'runF46' @($tTgtTs, $tRo) -AllSupportedEnabled
$fRan = @($rF.Ran | Where-Object { $_ -match '^RAN test_sbfake_tgtts ' })
$fBlk = @([regex]::Matches($rF.Out, '\[BLOCKED\] TC-SAFETY-GATE-001: [^\r\n]*test_sbfake_tgtts\.ps1: state-integrity halt[^\r\n]*restoration not verified[^\r\n]*--- not executed; intended target (0x[0-9A-F]{4})') | ForEach-Object { $_.Groups[1].Value })
tc_check 'TEST-HILGATE-046-unverified-restoration-blocks-remaining-targets-globally-readonly-continues' `
    ($fRan.Count -eq 1 -and $fBlk.Count -ge 1 -and @($fBlk | Where-Object { $fRan[0] -match $_ }).Count -eq 0 -and
     (Get-RanCount $rF '^RAN test_sbfake_ro ') -eq 1 -and $rF.Exit -eq 1) `
    ("ran=[{0}] blocked targets=[{1}] exit={2}; gate lines: {3}" -f ($fRan -join ' / '), ($fBlk -join ','), $rF.Exit,
     (@([regex]::Matches($rF.Out, '(?m)^.*TC-SAFETY-GATE-001: .*tgtts.*$') | ForEach-Object { $_.Value.Trim() }) -join ' | '))

# 083 (Q 2026-10-27): PHC time/rate are NOT_JUDGED by the runner and are not a global safety criterion.  An entry whose
# declared state is verifiable configuration plus PHC_TIME/PHC_RATE gets a nonpassing INCONCLUSIVE_RESTORATION record
# (TC-INFRA-STATE-004, never PASSED: restoration of the PHC is the test's own responsibility and is not evidenced by the
# runner) but no halt and no safety event: every target and the following Init entry still run.
$rP = Invoke-Sandbox 'runP83' @($tTgtPhc, $tInit, $tRo) -AllSupportedEnabled
$pRan = @($rP.Ran | Where-Object { $_ -match '^RAN test_sbfake_tgtphc ' })
$pNj  = @([regex]::Matches($rP.Out, '\[INCONCLUSIVE_RESTORATION\] TC-INFRA-STATE-004: after test_sbfake_tgtphc\.ps1[^\r\n]*PHC_TIME[^\r\n]*NOT_JUDGED'))
$pJson = @(Get-ChildItem (Join-Path $rP.Dir 'logs') -Filter 'results-*.json' -ErrorAction SilentlyContinue | ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json })
$pRec = @($pJson | ForEach-Object { @($_.records) } | Where-Object { $_.name -match 'PHC-NOT-JUDGED' })
$pSf  = Join-Path $rP.Dir 'logs\sandbox-hil-safety.json'
$pEv  = @(if (Test-Path -LiteralPath $pSf) { @((Get-Content -LiteralPath $pSf -Raw | ConvertFrom-Json).events) | Where-Object { $_ -and [string]$_.trigger.executable -match 'tgtphc' } })
tc_check 'TEST-HILGATE-083-phc-not-judged-is-inconclusive-restoration-without-global-halt-never-passed' `
    ($pRan.Count -ge 2 -and $pNj.Count -eq $pRan.Count -and $pRec.Count -eq $pRan.Count -and
     @($pRec | Where-Object { $_.verdict -ne 'INCONCLUSIVE_RESTORATION' }).Count -eq 0 -and
     ($rP.Out -notmatch '\[HALT\][^\r\n]*tgtphc') -and $pEv.Count -eq 0 -and (Test-Path -LiteralPath $pSf) -and
     ($rP.Out -notmatch 'TC-INFRA-STATE-002: after test_sbfake_tgtphc') -and
     (Get-RanCount $rP '^RAN test_sbfake_init ') -eq 1 -and (Get-RanCount $rP '^RAN test_sbfake_ro ') -eq 1 -and $rP.Exit -eq 1) `
    ("tgtphc ran=[{0}] not-judged lines={1} json=[{2}] init ran={3} exit={4}; {5}" -f ($pRan -join ' / '), $pNj.Count,
     (@($pRec | ForEach-Object { $_.verdict }) -join ','), (Get-RanCount $rP '^RAN test_sbfake_init '), $rP.Exit, (Tail $rP))

# 047: the mutation matrix uses the fixed state vocabulary and only classified non-ReadOnly entries; the runner verifies
# exactly ADAPTER, TIMINCA, TSAUXC itself (anything else needs a new independent check, never a declaration).
$mmBad = @()
$vocab = @('ADAPTER', 'TIMINCA', 'TSAUXC', 'PHC_TIME', 'PHC_RATE', 'TAS', 'FP', 'PTM', 'QAV', 'PFC', 'EEE', 'VLAN', 'MDIO', 'TS_CONTROL', 'TARGET_TIME', 'DRIVER_LIFECYCLE')
$vsAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq '$HilVerifiableState' }, $true) | Select-Object -First 1
$vs = if ($vsAst) { @($vsAst.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value }) } else { @() }
if (($vs -join ',') -ne 'ADAPTER,TIMINCA,TSAUXC') { $mmBad += "runner-verifiable state is [$($vs -join ',')], expected [ADAPTER,TIMINCA,TSAUXC]" }
$msAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq '$HilMutatedState' }, $true) | Select-Object -First 1
if ($null -eq $msAst) { $mmBad += '$HilMutatedState missing' }
else {
    $msHt = $msAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    foreach ($kv in @($msHt.KeyValuePairs)) {
        $e = $kv.Item1.Value
        if (-not $registry[$e] -or $registry[$e] -eq 'ReadOnly') { $mmBad += "$e declared but classified '$($registry[$e])'" }
        foreach ($s in @($kv.Item2.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value })) {
            if ($vocab -notcontains $s) { $mmBad += "${e}: unknown state '$s'" }
        }
    }
}
tc_check 'TEST-HILGATE-047-mutation-matrix-vocabulary-and-runner-verifiable-state' ($mmBad.Count -eq 0) ($mmBad -join '; ')

# --- Run P: machine-wide safety record (tools/lib/HilLock.ps1; Q 2026-10-21) -----------------------------------
# The runner reads the record before any test: HALTED or UNAVAILABLE blocks every Init/Mutating test (ReadOnly runs);
# a safety halt it raises is persisted, so a LATER run with a fresh, clean baseline is still blocked.  The sandbox uses
# AVB_TEST_HIL_SAFETY_FILE; the machine record in %ProgramData% is never touched by these tests.
. (Join-Path $RepoRoot 'tools\lib\HilLock.ps1')
$sfHalted = Join-Path $Tmp 'safety_halted.json'
$null = Initialize-AvbHilSafetyRecord -Path $sfHalted -Operator 'test' -Reason 'TEST-HILGATE-048'
$null = Add-AvbHilSafetyEvent -Path $sfHalted -Event ([ordered]@{ verdict = 'CLEANUP_FAILED'; trigger = [ordered]@{ executable = 'earlier_run.exe' } })
$rP1 = Invoke-Sandbox 'runP1' @($tInit, $tRo) -AllSupportedEnabled -SafetyFile $sfHalted
tc_check 'TEST-HILGATE-048-halted-machine-record-blocks-mutation-readonly-continues' `
    ((Get-RanCount $rP1 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rP1 '^RAN test_sbfake_ro ') -eq 1 -and
     $rP1.Out -match 'TC-SAFETY-GATE-001: [^\r\n]*test_sbfake_init\.ps1: state-integrity halt \(machine safety record HALTED' -and $rP1.Exit -eq 1) (Tail $rP1)
$rP2 = Invoke-Sandbox 'runP2' @($tInit, $tRo) -AllSupportedEnabled -SafetyFile (Join-Path $Tmp 'safety_absent.json')
tc_check 'TEST-HILGATE-049-missing-machine-record-fails-closed-for-mutation' `
    ((Get-RanCount $rP2 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rP2 '^RAN test_sbfake_ro ') -eq 1 -and
     $rP2.Out -match 'state-integrity halt \(machine safety record UNAVAILABLE[^\r\n]*missing' -and -not (Test-Path (Join-Path $Tmp 'safety_absent.json'))) (Tail $rP2)
$sfShared = Join-Path $Tmp 'safety_shared.json'
$null = Initialize-AvbHilSafetyRecord -Path $sfShared -Operator 'test' -Reason 'TEST-HILGATE-050'
$rP3 = Invoke-Sandbox 'runP3' @($tDis, $tRo2) -SafetyFile $sfShared                        # run 1: CLEANUP_FAILED
$sP3 = Read-AvbHilSafetyState -Path $sfShared
$eP3 = if ($sP3.Record) { @($sP3.Record.events)[0] } else { $null }
$rP4 = Invoke-Sandbox 'runP4' @($tInit, $tRo) -AllSupportedEnabled -SafetyFile $sfShared   # run 2: fresh clean state
tc_check 'TEST-HILGATE-050-safety-halt-persists-to-a-later-run-with-a-clean-baseline' `
    ($sP3.State -eq 'HALTED' -and $eP3 -and $eP3.verdict -eq 'CLEANUP_FAILED' -and $eP3.trigger.executable -match 'test_sbfake_disabler' -and
     $eP3.trigger.suite -eq 'HardwareUnit' -and $null -ne $eP3.initialState -and $null -ne $eP3.lastKnownState -and @($eP3.evidence).Count -ge 1 -and
     (Get-RanCount $rP4 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rP4 '^RAN test_sbfake_ro ') -eq 1 -and $rP4.Out -match 'machine safety record HALTED') `
    ("record={0} verdict={1} trigger={2}; run2: {3}" -f $sP3.State, $(if ($eP3) { $eP3.verdict }), $(if ($eP3) { $eP3.trigger.executable }), (Tail $rP4))

# --- Run Q: exclusive hardware access = lock OWNED by this execution (tools/lib/HilLock.ps1; Q 2026-10-21) --------
$tLockDrop = New-Fake 'test_sbfake_lockdrop' "Remove-Item -LiteralPath `$env:AVB_TEST_HIL_LOCK_FILE -Force"
function New-LockFile([string]$Name, $Owner) { $p = Join-Path $Tmp $Name; ($Owner | ConvertTo-Json) | Set-Content -LiteralPath $p -Encoding UTF8; return $p }
# 051: another live owner (PID 4 = System: alive, never an ancestor) -> bounded wait, then Init BLOCKED; ReadOnly runs; lock untouched
$lfOther = New-LockFile 'other.lock' ([ordered]@{ kind = 'local'; label = 'another operator run'; host = $env:COMPUTERNAME; pid = 4; acquiredUtc = '2026-10-21T00:00:00Z' })
$rQ1 = Invoke-Sandbox 'runQ1' @($tInit, $tRo) -AllSupportedEnabled -LockFile $lfOther -RunnerArgs '-HilLockWaitSeconds 0'
tc_check 'TEST-HILGATE-051-lock-owned-by-another-process-blocks-mutation-readonly-continues' `
    ((Get-RanCount $rQ1 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rQ1 '^RAN test_sbfake_ro ') -eq 1 -and
     $rQ1.Out -match 'state-integrity halt \(machine lock not owned by this execution' -and ((Get-Content $lfOther -Raw) -match 'another operator run')) (Tail $rQ1)
# 052: lock absent -> the runner acquires it before mutation and releases it at completion
$lfAbsent = Join-Path $Tmp 'absent.lock'
$rQ2 = Invoke-Sandbox 'runQ2' @($tInit, $tRo) -AllSupportedEnabled -LockFile $lfAbsent
tc_check 'TEST-HILGATE-052-absent-lock-is-acquired-before-mutation-and-released' `
    ((Get-RanCount $rQ2 '^RAN test_sbfake_init ') -eq 1 -and $rQ2.Out -match 'machine lock acquired by this run' -and -not (Test-Path $lfAbsent)) (Tail $rQ2)
# 053: lock lost during execution -> no further mutation, LOCK_LOST persisted in the machine safety record
# A CLEAR record through the real bootstrap path: initialize (UNBASELINED) + authorized baseline under the lock.
function New-ClearSafetyFile([string]$Name, [object[]]$PhcTolerance = @()) {
    $f = Join-Path $Tmp $Name; $null = Initialize-AvbHilSafetyRecord -Path $f -Operator 'test' -Reason $Name
    $bl = Join-Path $Tmp "$Name.baseline.json"
    $doc = [ordered]@{ schema = 'avb-hil-baseline-authorization/1'; host = $env:COMPUTERNAME; verdict = 'BASELINE_VERIFIED'
                       inventory = @([ordered]@{ did = '0x125B'; mac = '00-11-22-33-44-55' }); measurements = @([ordered]@{ name = 'TIMINCA'; value = '0x08000000' }) }
    if ($PhcTolerance.Count -gt 0) { $doc.phcTolerance = $PhcTolerance }
    ($doc | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $bl -Encoding UTF8
    $lp = Join-Path $Tmp "$Name.lock"; $lk = Enter-AvbHilLock -Kind 'local' -Label 'test bootstrap' -Path $lp
    $a = Approve-AvbHilSafetyBaseline -Path $f -Operator 'test' -Authorization 'synthetic sandbox baseline' -LockOwner $lk.Owner -LockPath $lp -BaselineEvidence $bl
    $null = Exit-AvbHilLock -Owner $lk.Owner -Path $lp
    if (-not $a.Ok) { throw "bootstrap of $Name failed: $($a.Why)" }
    return $f
}
$sfLost = New-ClearSafetyFile 'safety_lost.json'
$rQ3 = Invoke-Sandbox 'runQ3' @($tLockDrop, $tInit, $tRo2) -AllSupportedEnabled -LockFile (Join-Path $Tmp 'lost.lock') -SafetyFile $sfLost
$sQ3 = Read-AvbHilSafetyState -Path $sfLost
tc_check 'TEST-HILGATE-053-lock-lost-stops-mutation-and-is-recorded' `
    ((Get-RanCount $rQ3 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rQ3 '^RAN test_sbfake_ro2 ') -eq 1 -and $rQ3.Out -match 'machine lock lost' -and
     $sQ3.State -ne 'CLEAR' -and @(@($sQ3.Record.events) | Where-Object { $_.verdict -eq 'LOCK_LOST' }).Count -eq 1) ("record={0}; {1}" -f $sQ3.State, (Tail $rQ3))
# 054: unattended (GitHub Actions): owned only by the same run/attempt/job; absent lock is NOT acquired (fail closed)
$lfCiOther = New-LockFile 'ci_other.lock' ([ordered]@{ kind = 'ci'; label = 'other job'; host = $env:COMPUTERNAME; pid = 1; runId = 'sbx-run'; runAttempt = '1'; job = 'other-job'; acquiredUtc = '2026-10-21T00:00:00Z' })
$ghEnv = @{ GITHUB_ACTIONS = 'true'; GITHUB_RUN_ID = 'sbx-run'; GITHUB_RUN_ATTEMPT = '1'; GITHUB_JOB = 'sbx-job' }
$rQ4 = Invoke-Sandbox 'runQ4' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $ghEnv -LockFile $lfCiOther
$lfCiAbsent = Join-Path $Tmp 'ci_absent.lock'
$rQ5 = Invoke-Sandbox 'runQ5' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $ghEnv -LockFile $lfCiAbsent
tc_check 'TEST-HILGATE-054-unattended-run-needs-its-own-job-lock-and-never-acquires-one' `
    ((Get-RanCount $rQ4 '^RAN test_sbfake_tgt ') -eq 0 -and $rQ4.Out -match 'not owned by this execution[^\r\n]*other-job' -and
     (Get-RanCount $rQ5 '^RAN test_sbfake_tgt ') -eq 0 -and -not (Test-Path $lfCiAbsent) -and (Get-RanCount $rQ5 '^RAN test_sbfake_ro ') -eq 1) `
    ("otherJob: {0} ### absent: {1}" -f (Tail $rQ4), (Tail $rQ5))

# --- Run R: one original SESSION baseline per exclusive HIL session (Q 2026-10-21) -------------------------------
# Session = ci:<run>:<attempt> (spans hil-debug AND hil-release) or local:<host>:<pid>:<lock acquired>.  Stored beside the
# lock (sandbox: AVB_TEST_HIL_SESSION_FILE), independent of suite results; created only by a lock-owning run on a CLEAR
# machine, never overwritten after a failure; every suite compares its start and end with it (configuration only - PHC
# time progression is not a difference).  Distinct from the machine's approved baseline (safety record) and from the
# per-suite / per-test snapshots.  Probe: test_sbfake_tgt (Mutating:Target, TIMINCA only) runs unattended when owned.
$gh = @{ GITHUB_ACTIONS = 'true' }
function Read-Session([string]$P) { if (Test-Path -LiteralPath $P) { try { return (Get-Content -LiteralPath $P -Raw | ConvertFrom-Json) } catch { return $null } }; return $null }
function Get-SessFail($r) { return @([regex]::Matches($r.Out, '\[(CLEANUP_FAILED|FAILED_SETUP)\] TC-INFRA-SESSION-\d{3}[^\r\n]*') | ForEach-Object { $_.Value }) }
$adminDown = "`$a = @(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_1533' })[0]; `$a.AdminStatus = 'Down'"
$timChg   = "`$c = @(`$s.clock | Where-Object { `$_.Did -eq '0x1533' })[0]; `$c.Timinca = 34"

# 055 unchanged configuration across suites: created once, later suites compare clean, mutation proceeds
$ss1 = Join-Path $Tmp 'sess_unchanged.json'
$rR1a = Invoke-Sandbox 'runR1a' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss1 -Suite 'HardwareUnit'
$s1a = Read-Session $ss1
$rR1b = Invoke-Sandbox 'runR1b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss1 -Suite 'HardwareIntegration'
$s1b = Read-Session $ss1
tc_check 'TEST-HILGATE-055-session-baseline-created-once-and-reused-across-suites' `
    ($s1a -and $s1b -and $s1a.sessionId -eq 'ci:sbx-run:1' -and $s1b.createdUtc -eq $s1a.createdUtc -and ($s1b.checks | Measure-Object).Count -ge 3 -and
     (@($s1b.checks) | Where-Object { ($_.diffs | Measure-Object).Count -gt 0 } | Measure-Object).Count -eq 0 -and
     @(Get-SessFail $rR1a).Count -eq 0 -and @(Get-SessFail $rR1b).Count -eq 0 -and (Get-RanCount $rR1b '^RAN test_sbfake_tgt ') -ge 1) `
    ("id={0} created a/b={1}/{2} checks={3}; {4}" -f $(if ($s1a) { $s1a.sessionId }), $(if ($s1a) { $s1a.createdUtc }), $(if ($s1b) { $s1b.createdUtc }), $(if ($s1b) { @($s1b.checks).Count }), (Tail $rR1b))
# 056 unexpected change between suites
$ss2 = Join-Path $Tmp 'sess_suites.json'
$null = Invoke-Sandbox 'runR2a' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss2
$rR2b = Invoke-Sandbox 'runR2b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss2 -StateEdit $adminDown -Suite 'HardwareIntegration'
tc_check 'TEST-HILGATE-056-change-between-suites-is-cleanup-failed-against-the-session-baseline' `
    (@(Get-SessFail $rR2b | Where-Object { $_ -match '^\[CLEANUP_FAILED\] TC-INFRA-SESSION-001[^\r\n]*DEV_1533[^\r\n]*AdminStatus Up -> Down' }).Count -eq 1 -and
     (Get-RanCount $rR2b '^RAN test_sbfake_tgt ') -eq 0 -and (Get-RanCount $rR2b '^RAN test_sbfake_ro ') -eq 1) (((Get-SessFail $rR2b) -join ' | ') + ' ### ' + (Tail $rR2b))
# 057 unexpected change between Debug and Release (same CI run, other job)
$ss3 = Join-Path $Tmp 'sess_cfg.json'
$null = Invoke-Sandbox 'runR3a' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss3 -Configuration 'Debug'
$rR3b = Invoke-Sandbox 'runR3b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss3 -Configuration 'Release' -StateEdit $timChg
tc_check 'TEST-HILGATE-057-change-between-debug-and-release-is-detected-against-the-same-session' `
    (@(Get-SessFail $rR3b | Where-Object { $_ -match 'TC-INFRA-SESSION-001[^\r\n]*Release[^\r\n]*TIMINCA 0x00000000 -> 0x00000022' }).Count -eq 1 -and (Get-RanCount $rR3b '^RAN test_sbfake_tgt ') -eq 0) (((Get-SessFail $rR3b) -join ' | ') + ' ### ' + (Tail $rR3b))
# 058 crash/timeout: the session baseline and the halt survive; the next suite uses the SAME baseline
$tTgtSleep = New-Fake 'test_sbfake_tgtsleep' 'Start-Sleep -Seconds 120'
$sf4 = New-ClearSafetyFile 'safety_sess_crash.json'; $ss4 = Join-Path $Tmp 'sess_crash.json'
$null = Invoke-Sandbox 'runR4a' @($tTgtSleep) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss4 -SafetyFile $sf4 -Timeout 5
$s4a = Read-Session $ss4
$rR4b = Invoke-Sandbox 'runR4b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss4 -SafetyFile $sf4
$s4b = Read-Session $ss4
tc_check 'TEST-HILGATE-058-timeout-keeps-session-baseline-and-halt-for-the-next-suite' `
    ($s4a -and $s4b.createdUtc -eq $s4a.createdUtc -and (Get-RanCount $rR4b '^RAN test_sbfake_tgt ') -eq 0 -and $rR4b.Out -match 'machine safety record HALTED') `
    ("created a/b={0}/{1}; {2}" -f $(if ($s4a) { $s4a.createdUtc }), $(if ($s4b) { $s4b.createdUtc }), (Tail $rR4b))
# 059 existing machine-wide halt: no session baseline is established from an unverified state
$ss5 = Join-Path $Tmp 'sess_halted.json'
$rR5 = Invoke-Sandbox 'runR5' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss5 -SafetyFile $sfHalted
tc_check 'TEST-HILGATE-059-no-session-baseline-is-created-while-the-machine-is-halted' (-not (Test-Path $ss5) -and $rR5.Out -match 'no session baseline') (Tail $rR5)
# 060 corrupt session baseline: FAILED_SETUP, mutation blocked, file preserved byte-identical
$ss6 = Join-Path $Tmp 'sess_corrupt.json'; '{ corrupt' | Set-Content -LiteralPath $ss6
$h6 = (Get-FileHash $ss6).Hash
$rR6 = Invoke-Sandbox 'runR6' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss6
tc_check 'TEST-HILGATE-060-corrupt-session-baseline-is-failed-setup-and-preserved' `
    (@(Get-SessFail $rR6 | Where-Object { $_ -match '^\[FAILED_SETUP\] TC-INFRA-SESSION-000' }).Count -eq 1 -and (Get-RanCount $rR6 '^RAN test_sbfake_tgt ') -eq 0 -and (Get-FileHash $ss6).Hash -eq $h6) (((Get-SessFail $rR6) -join ' | ') + ' ### ' + (Tail $rR6))
# 061 foreign host: blocked and preserved; a finished OTHER session on a CLEAR machine is archived, never overwritten
$ss7 = Join-Path $Tmp 'sess_foreign.json'
([ordered]@{ schema = 'avb-hil-session/1'; sessionId = 'ci:sbx-run:1'; host = 'OTHER-HOST'; createdUtc = 'x'; baseline = $null; checks = @() } | ConvertTo-Json) | Set-Content -LiteralPath $ss7
$h7 = (Get-FileHash $ss7).Hash
$rR7 = Invoke-Sandbox 'runR7' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss7
$ss8 = Join-Path $Tmp 'sess_old.json'
$null = Invoke-Sandbox 'runR8a' @($tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss8
$o8 = Read-Session $ss8
if ($o8) { $o8.sessionId = 'ci:older-run:1'; ($o8 | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $ss8 -Encoding UTF8 }
$rR8 = Invoke-Sandbox 'runR8b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss8
$arch = @(Get-ChildItem $Tmp -Filter 'sess_old.json.prev-*.json')
$n8 = Read-Session $ss8
tc_check 'TEST-HILGATE-061-foreign-session-blocked-preserved-other-session-archived-not-overwritten' `
    (@(Get-SessFail $rR7 | Where-Object { $_ -match 'TC-INFRA-SESSION-000[^\r\n]*OTHER-HOST' }).Count -eq 1 -and (Get-FileHash $ss7).Hash -eq $h7 -and
     $arch.Count -eq 1 -and ((Get-Content $arch[0].FullName -Raw) -match 'ci:older-run:1') -and $n8 -and $n8.sessionId -eq 'ci:sbx-run:1') `
    ("foreign: {0} ### archived={1} new={2}" -f ((Get-SessFail $rR7) -join ' | '), $arch.Count, $(if ($n8) { $n8.sessionId }))
# 062 concurrent access: a run that does not own the lock neither reads nor creates the session baseline
$ss9 = Join-Path $Tmp 'sess_concurrent.json'
$rR9 = Invoke-Sandbox 'runR9' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $ghEnv -LockFile $lfCiOther -SessionFile $ss9
tc_check 'TEST-HILGATE-062-run-without-lock-ownership-never-touches-the-session-baseline' (-not (Test-Path $ss9) -and (Get-RanCount $rR9 '^RAN test_sbfake_tgt ') -eq 0) (Tail $rR9)
# 063 legitimate PHC time progression is not a configuration difference
$ss10 = Join-Path $Tmp 'sess_time.json'
$null = Invoke-Sandbox 'runR10a' @($tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss10 -StateEdit "foreach (`$c in `$s.clock) { `$c | Add-Member -NotePropertyName Systim -NotePropertyValue 1000 -Force }"
$rR10b = Invoke-Sandbox 'runR10b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss10 -StateEdit "foreach (`$c in `$s.clock) { `$c | Add-Member -NotePropertyName Systim -NotePropertyValue 987654321000 -Force }"
tc_check 'TEST-HILGATE-063-phc-time-progression-is-not-a-session-difference' ((Test-Path $ss10) -and @(Get-SessFail $rR10b).Count -eq 0 -and (Get-RanCount $rR10b '^RAN test_sbfake_tgt ') -ge 1) (Tail $rR10b)
# 064 failed restoration, then a new suite: the damage stays visible against the ORIGINAL baseline, never re-baselined
$sf11 = New-ClearSafetyFile 'safety_sess_fail.json'; $ss11 = Join-Path $Tmp 'sess_fail.json'
$rR11a = Invoke-Sandbox 'runR11a' @($tTim, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss11 -SafetyFile $sf11
$s11a = Read-Session $ss11
$rR11b = Invoke-Sandbox 'runR11b' @($tTgt, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss11 -SafetyFile $sf11 -StateEdit $timChg -Suite 'HardwareIntegration'
$s11b = Read-Session $ss11
# Decision B: every check stays visible (STATE-001 + SESSION-002 in run A, SESSION-001 in run B) but the damage is ONE
# incident: run A opens it per test and confirms it at suite end; run B confirms the SAME incident id (loaded from the
# machine record) - one machine-record event with all observations, never re-baselined.
$a11St = @([regex]::Matches($rR11a.Out, '\[CLEANUP_FAILED\] TC-INFRA-STATE-001[^\r\n]*incident ([0-9a-f-]{36}) opened') | ForEach-Object { $_.Groups[1].Value })
$a11Se = @([regex]::Matches($rR11a.Out, '\[CLEANUP_FAILED\] TC-INFRA-SESSION-002[^\r\n]*incident ([0-9a-f-]{36}) confirmed') | ForEach-Object { $_.Groups[1].Value })
$b11S1 = @([regex]::Matches($rR11b.Out, '\[CLEANUP_FAILED\] TC-INFRA-SESSION-001[^\r\n]*TIMINCA[^\r\n]*incident ([0-9a-f-]{36}) confirmed') | ForEach-Object { $_.Groups[1].Value })
$b11S2 = @([regex]::Matches($rR11b.Out, '\[CLEANUP_FAILED\] TC-INFRA-SESSION-002[^
]*TIMINCA[^
]*incident ([0-9a-f-]{36}) confirmed') | ForEach-Object { $_.Groups[1].Value })
$ev11 = @(@((Read-AvbHilSafetyState -Path $sf11).Record.events) | Where-Object { $_.PSObject.Properties['incidentKey'] -and $_.incidentKey -match '\|TIMINCA\|' })
# four independent observations of the one incident: A per-test (opened), A session end, B session start, B session end
tc_check 'TEST-HILGATE-064-failed-restoration-stays-visible-against-the-original-baseline-in-the-next-suite' `
    ($a11St.Count -eq 1 -and $a11Se.Count -eq 1 -and $b11S1.Count -eq 1 -and $b11S2.Count -eq 1 -and $a11Se[0] -eq $a11St[0] -and $b11S1[0] -eq $a11St[0] -and
     $b11S2[0] -eq $a11St[0] -and $ev11.Count -eq 1 -and $ev11[0].eventId -eq $a11St[0] -and @($ev11[0].observations).Count -eq 4 -and
     $s11a -and $s11b.createdUtc -eq $s11a.createdUtc -and (Get-RanCount $rR11b '^RAN test_sbfake_tgt ') -eq 0) `
    ("a: state=[{0}] session-end=[{1}] ### b: session-start=[{2}] ### events={3} observations={4}" -f ($a11St -join ','), ($a11Se -join ','), ($b11S1 -join ','), $ev11.Count, $(if ($ev11.Count) { @($ev11[0].observations).Count }))

# --- Run S: elevation and unambiguous identity are verified independently before mutation (Q 2026-10-21) -----------
# 065: a non-elevated token never mutates (elevation is necessary, never sufficient); ReadOnly continues
$rS1 = Invoke-Sandbox 'runS1' @($tInit, $tRo) -AllSupportedEnabled -ExtraEnv @{ AVB_TEST_SANDBOX_ELEVATED = '0' }
tc_check 'TEST-HILGATE-065-non-elevated-execution-never-mutates' `
    ((Get-RanCount $rS1 '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rS1 '^RAN test_sbfake_ro ') -eq 1 -and $rS1.Out -match 'state-integrity halt \(not elevated') (Tail $rS1)
# 066: two present adapters with the target DID (e.g. 6x I226 lab machines): identity ambiguous -> that target refused,
#      never "first match"; an unambiguous target still runs
$dupDid = "`$s.adapters = @(@(`$s.adapters) + @([pscustomobject]@{ InstanceId = 'PCI\VEN_8086&DEV_1533&SUBSYS_00008086&REV_03\SB05'; PnpStatus = 'OK'; Problem = 0; AdminStatus = 'Up'; MediaStatus = 'Up'; FilterBound = 'True'; Name = 'SB I210 #2' }))"
# Existing contract (characterized, unchanged): the baseline drops a duplicated DID from the exact targets
# (TC-INFRA-ADAPTER-003) and the child's AVB_HIL_TARGET_DID comes only from that list - an operator-supplied
# AVB_HIL_TARGET_DID never reaches the test, so an ambiguous adapter is never mutated.
$rS2 = Invoke-Sandbox 'runS2' @($tTgt, $tRo) -AllSupportedEnabled -StateEdit $dupDid
$s2Ran = @($rS2.Ran | Where-Object { $_ -match '^RAN test_sbfake_tgt ' } | ForEach-Object { ($_ -split 'target=')[1].Trim() })
$rS3 = Invoke-Sandbox 'runS3' @($tTgt, $tRo) -AllSupportedEnabled -StateEdit $dupDid -ExtraEnv @{ AVB_HIL_TARGET_DID = '0x1533' }
$s3Ran = @($rS3.Ran | Where-Object { $_ -match '^RAN test_sbfake_tgt ' } | ForEach-Object { ($_ -split 'target=')[1].Trim() })
tc_check 'TEST-HILGATE-066-ambiguous-adapter-identity-is-never-a-mutation-target' `
    (($s2Ran -join ',') -eq '0x125B' -and $rS2.Out -match 'TC-INFRA-ADAPTER-003: DID 0x1533 is present more than once' -and ($s3Ran -join ',') -eq '0x125B') `
    ("fan-out ran=[{0}]; with operator AVB_HIL_TARGET_DID=0x1533 ran=[{1}]" -f ($s2Ran -join ','), ($s3Ran -join ','))

# --- Run T: cleanup INCIDENTS vs verification OBSERVATIONS (Decision B, Q 2026-10-22) ------------------------------
# One persistent discrepancy = one incident; every independent check (per-test TC-INFRA-STATE-001, session start
# TC-INFRA-SESSION-001, session end TC-INFRA-SESSION-002) stays a separately visible observation referencing it.
# Incident key = adapter identity | component | expected | observed.  Pure decision functions extracted from the runner.
$fnCmp = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Compare-AvbHilState' }, $true)
$fnRes = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Resolve-AvbHilIncident' }, $true)
if ($null -eq $fnRes) { tc_check 'TEST-HILGATE-067-incident-resolution-pure-cases' $false 'Resolve-AvbHilIncident not found in Run-Tests-CI.ps1' }
else {
    . ([scriptblock]::Create($fnCmp.Extent.Text)); . ([scriptblock]::Create($fnRes.Extent.Text))
    function New-St([uint32]$Tim1533 = 0, [uint32]$Aux1533 = 0, [uint32]$Tim125B = 0x08000000, [string]$Admin1533 = 'Up') {
        return @{ Ok = $true; Error = ''; ClockOk = $true; ClockError = ''
                  Adapters = @([pscustomobject]@{ InstanceId = 'PCI\VEN_8086&DEV_1533\SB02'; Did = '0x1533'; PnpStatus = 'OK'; Problem = 0; AdminStatus = $Admin1533; MediaStatus = 'Up'; FilterBound = 'True'; Name = 'I210' })
                  Clock = @([pscustomobject]@{ Index = 0; Did = '0x125B'; Ok = $true; Timinca = $Tim125B; Tsauxc = 0; Error = '' },
                            [pscustomobject]@{ Index = 1; Did = '0x1533'; Ok = $true; Timinca = $Tim1533; Tsauxc = $Aux1533; Error = '' }) }
    }
    $base = New-St
    $c1 = Compare-AvbHilState $base (New-St -Tim1533 34)
    $c2 = Compare-AvbHilState $base (New-St -Tim1533 34 -Aux1533 4)
    $c6 = Compare-AvbHilState $base (New-St -Tim1533 34 -Tim125B 34)
    $bad = @()
    if (@($c1.Items).Count -ne 1 -or $c1.Items[0].Key -ne 'drv:0x1533#1|TIMINCA|0x00000000|0x00000022' -or $c1.Items[0].IdentityVerified) { $bad += "structured TIMINCA item: $(@($c1.Items | ForEach-Object { $_.Key }) -join ',')" }
    # case 1: per-test failure confirmed at suite end -> one incident, two observations
    $open = [ordered]@{}
    $o1 = Resolve-AvbHilIncident -Open $open -Item $c1.Items[0] -Check 'per-test' -Record 'x[STATE]'
    $o2 = Resolve-AvbHilIncident -Open $open -Item $c1.Items[0] -Check 'session-end' -Record 'HIL-SESSION[end]'
    if ($o1.Role -ne 'opened' -or $o2.Role -ne 'confirmed' -or $o1.IncidentId -ne $o2.IncidentId -or @($open[$c1.Items[0].Key].observations).Count -ne 2) { $bad += "case1: $($o1.Role)/$($o2.Role) ids $($o1.IncidentId)/$($o2.IncidentId)" }
    # case 2: TIMINCA plus an additional TSAUXC change -> two incidents
    $open2 = [ordered]@{}; $ids2 = @($c2.Items | ForEach-Object { (Resolve-AvbHilIncident -Open $open2 -Item $_ -Check 'per-test' -Record 'x').IncidentId } | Sort-Object -Unique)
    if ($ids2.Count -ne 2) { $bad += "case2: $($ids2.Count) incident(s) for TIMINCA+TSAUXC" }
    # case 4: discrepancy persisting into the next suite (open incident loaded from the machine record) -> same incident
    $open4 = [ordered]@{}; $open4[$c1.Items[0].Key] = @{ IncidentId = 'evt-persisted'; Key = $c1.Items[0].Key; observations = @() }
    $o4 = Resolve-AvbHilIncident -Open $open4 -Item $c1.Items[0] -Check 'session-start' -Record 'HIL-SESSION[start]'
    if ($o4.Role -ne 'confirmed' -or $o4.IncidentId -ne 'evt-persisted') { $bad += "case4: $($o4.Role) $($o4.IncidentId)" }
    # case 5: verified restored earlier (event closed -> not loaded as open), recurs -> NEW incident
    $o5 = Resolve-AvbHilIncident -Open ([ordered]@{}) -Item $c1.Items[0] -Check 'per-test' -Record 'y'
    if ($o5.Role -ne 'opened' -or $o5.IncidentId -eq 'evt-persisted') { $bad += "case5: $($o5.Role) $($o5.IncidentId)" }
    # case 6: same register changed on two distinct adapters -> separate incidents
    $open6 = [ordered]@{}; $ids6 = @($c6.Items | ForEach-Object { (Resolve-AvbHilIncident -Open $open6 -Item $_ -Check 'per-test' -Record 'x').IncidentId } | Sort-Object -Unique)
    if ($ids6.Count -ne 2) { $bad += "case6: $($ids6.Count) incident(s) for TIMINCA on two adapters" }
    # case 8: an unreadable component is an explicit unobservable item, never a difference and never an incident
    $c8 = Compare-AvbHilState $base @{ Ok = $true; Error = ''; ClockOk = $false; ClockError = 'GET_CLOCK_CONFIG failed'; Adapters = $base.Adapters; Clock = @() }
    if (@($c8.Items | Where-Object { $_.Kind -eq 'unobservable' }).Count -lt 1 -or @($c8.Items | Where-Object { $_.Kind -eq 'diff' }).Count -ne 0) { $bad += "case8: kinds $(@($c8.Items | ForEach-Object { $_.Kind }) -join ',')" }
    tc_check 'TEST-HILGATE-067-incident-resolution-pure-cases' ($bad.Count -eq 0) ($bad -join ' ### ')
}

# 068 (sandbox): a genuinely NEW discrepancy between suites opens a NEW incident (never folded into an older one),
# while a persisting one only adds an observation to the existing machine-record event.
$sf12 = New-ClearSafetyFile 'safety_incidents.json'; $ss12 = Join-Path $Tmp 'sess_incidents.json'
$rT1 = Invoke-Sandbox 'runT1' @($tTim, $tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss12 -SafetyFile $sf12           # TIMINCA@1533 damaged
$rT2 = Invoke-Sandbox 'runT2' @($tRo) -AllSupportedEnabled -ExtraEnv $gh -SessionFile $ss12 -SafetyFile $sf12 -Suite 'HardwareIntegration' `
           -StateEdit ($timChg + "; `$a = @(`$s.adapters | Where-Object { `$_.InstanceId -match 'DEV_125B' })[0]; `$a.AdminStatus = 'Down'")  # persists + NEW change
$ev12 = @((Read-AvbHilSafetyState -Path $sf12).Record.events)
$timEv = @($ev12 | Where-Object { $_.PSObject.Properties['incidentKey'] -and $_.incidentKey -match '\|TIMINCA\|' })
$admEv = @($ev12 | Where-Object { $_.PSObject.Properties['incidentKey'] -and $_.incidentKey -match '\|AdminStatus\|' })
$t2Roles = @([regex]::Matches($rT2.Out, '\[CLEANUP_FAILED\] TC-INFRA-SESSION-001[^\r\n]*') | ForEach-Object { $_.Value })
tc_check 'TEST-HILGATE-068-persisting-discrepancy-confirmed-new-discrepancy-opens-new-incident' `
    ($timEv.Count -eq 1 -and @($timEv[0].observations).Count -ge 3 -and $admEv.Count -eq 1 -and
     ($t2Roles -join ' ') -match "incident $([regex]::Escape([string]$timEv[0].eventId)) confirmed" -and ($t2Roles -join ' ') -match 'incident [0-9a-f-]+ opened') `
    ("TIMINCA events={0} obs={1}; AdminStatus events={2}; run2: {3}" -f $timEv.Count, $(if ($timEv.Count) { @($timEv[0].observations).Count }), $admEv.Count, ($t2Roles -join ' | '))

# 069: LocalSuites layout - the prior-results directory (logs\LocalSuites_<ts>) lies INSIDE the runner's logs directory.
# Defect found in the physical run LocalSuites_20261021_173805: the atdecc FAILED(TIMEOUT) halt of HardwareUnit never
# reached HardwareMutation (no 'prior suite' line) because Get-AvbPriorHalt skipped everything under its own logs dir.
$lsPrior = Join-Path $Tmp 'runLS\logs\LocalSuites_x'; New-Item -ItemType Directory -Path $lsPrior -Force | Out-Null
([ordered]@{ schema = 'avb-runner-results/1'; suite = 'HardwareUnit'; configuration = 'Debug'; overall = 'FAILED'; exitCode = 1; stateHalt = ''
             records = @([ordered]@{ name = 'test_prior.exe'; verdict = 'CLEANUP_FAILED' }) } | ConvertTo-Json -Depth 5) |
    Set-Content -LiteralPath (Join-Path $lsPrior 'results-Debug-HardwareUnit.json') -Encoding UTF8
$rLS = Invoke-Sandbox 'runLS' @($tInit, $tRo) -Suite 'HardwareMutation' -AllSupportedEnabled -PriorResultsDir $lsPrior
tc_check 'TEST-HILGATE-069-prior-halt-found-when-prior-results-lie-inside-the-runner-logs-dir' `
    ((Get-RanCount $rLS '^RAN test_sbfake_init ') -eq 0 -and (Get-RanCount $rLS '^RAN test_sbfake_ro ') -eq 1 -and $rLS.Out -match 'state-integrity halt \(prior suite') (Tail $rLS)

# 045: a test that cannot establish its initial conditions prints [FAILED_SETUP]: nonpassing even with exit 0, kept
# distinct in the results document (FAILED_SETUP) and JUnit (<failure> labelled FAILED_SETUP).
$tSetup = New-Fake 'test_sbfake_setup' "Write-Host '  [FAILED_SETUP] TC-SBFAKE-SETUP-001: target adapter not uniquely identified'"
$rS = Invoke-Sandbox 'runS' @($tSetup, $tRo) -AllSupportedEnabled
$sRec = @(Get-ChildItem (Join-Path $rS.Dir 'logs') -Filter 'results-*.json' -ErrorAction SilentlyContinue | ForEach-Object { @((Get-Content $_.FullName -Raw | ConvertFrom-Json).records) } | Where-Object { $_.name -match 'test_sbfake_setup' })
tc_check 'TEST-HILGATE-045-failed-setup-is-nonpassing-and-distinct-in-json-and-junit' `
    ($rS.Exit -eq 1 -and $sRec.Count -eq 1 -and $sRec[0].verdict -eq 'FAILED_SETUP' -and
     ($rS.Xml -match '(?s)<testcase name="TC-SBFAKE-SETUP-001[^"]*"[^>]*><failure[^>]*message="\[FAILED_SETUP\]')) `
    ("exit={0} verdict={1}; {2}" -f $rS.Exit, $(if ($sRec.Count) { $sRec[0].verdict }), (Tail $rS))

# --- Run N: decision A - UT-CORR-005/006 are attended-only; under GitHub Actions they are NOT EXECUTED gaps -------
# Their executable also contains the UT-CORR-009 driver lifecycle path (process creation; TEST-HILSUITE-REAL-003).
$want = @('test_ptp_phc_stability --case UT-CORR-005', 'test_ptp_phc_stability --case UT-CORR-006')
$rN1 = Invoke-Sandbox 'runN1' @() -Suite 'HardwareMutation' -AllSupportedEnabled -ExtraEnv @{ GITHUB_ACTIONS = 'true' }
$rN2 = Invoke-Sandbox 'runN2' @() -Suite 'HardwareMutation' -AllSupportedEnabled
$nBad = @()
if ((@($attendedOnly | Sort-Object) -join '|') -ne ($want -join '|')) { $nBad += "attended-only table: [$($attendedOnly -join ', ')]" }
foreach ($e in $want) {
    if (@($hwLists['HardwareMutationTests']) -contains $e) { $nBad += "$e still in `$HardwareMutationTests (gate-runnable under GitHub Actions)" }
    if ($rN1.Out -notmatch ('\[BLOCKED\] TC-HIL-COVERAGE-GAP-001: ' + [regex]::Escape($e) + ' NOT EXECUTED by automatic HIL[^\r\n]*UT-CORR-009')) { $nBad += "GHA: no NOT EXECUTED gap naming UT-CORR-009 for $e" }
    if ($rN2.Out -notmatch ('TC-SAFETY-GATE-001: ' + [regex]::Escape($e) + ': sandbox mode')) { $nBad += "attended: $e not dispatched" }
    if ($rN2.Out -match ('TC-HIL-COVERAGE-GAP-001: ' + [regex]::Escape($e))) { $nBad += "attended: $e reported as a gap" }
}
if ((Get-RanCount $rN1 'RAN') -ne 0 -or $rN1.Exit -ne 1 -or $rN1.Out -notmatch 'HardwareMutation is INCOMPLETE') { $nBad += "GHA run: ran=$(Get-RanCount $rN1 'RAN') exit=$($rN1.Exit) " + (Tail $rN1) }
tc_check 'TEST-HILGATE-044-ut-corr-005-006-attended-only-not-executed-gaps-under-github-actions' ($nBad.Count -eq 0) ($nBad -join ' ### ')

# --- Run J: a Hardware* suite in which nothing passed (skip-only) is a failure, not hardware verification ----
$tSkip = Join-Path $Fakes 'test_sbfake_skiponly.ps1'
Set-Content -LiteralPath $tSkip -Encoding UTF8 -Value ("Add-Content -LiteralPath '{0}' -Value 'RAN test_sbfake_skiponly target='`r`n" -f $RanLog.Replace("'", "''") +
    "Write-Host '  [SKIP] TC-SBFAKE-002: skip only'`r`nexit 2`r`n")
$rJ = Invoke-Sandbox 'runJ' @('ps1:' + $tSkip.Replace('\', '/')) -AllSupportedEnabled
# Control: the same fixture with one passing fake exits 0, so the failure above comes from "no pass" alone.
$rJ2 = Invoke-Sandbox 'runJ2' @($tRo) -AllSupportedEnabled
tc_check 'TEST-HILGATE-036-hardware-suite-without-a-pass-fails' `
    (((Get-RanCount $rJ '^RAN test_sbfake_skiponly ') -eq 1) -and $rJ.Exit -eq 1 -and ($rJ.Out -match 'no hardware test PASSED in HardwareUnit') -and
     ($rJ.Out -notmatch '\[FAIL\] OVERALL: \d+ test\(s\) FAILED') -and $rJ2.Exit -eq 0) `
    ("ran={0}; exit={1}; control exit={2}; {3} || control: {4}" -f (Get-RanCount $rJ 'RAN'), $rJ.Exit, $rJ2.Exit, (Tail $rJ), (Tail $rJ2))

# --- Run K: Init is blocked unattended (GitHub Actions) but still runs attended (coverage not removed) -----------
$rK1 = Invoke-Sandbox 'runK1' @($tInit, $tRo) -AllSupportedEnabled -ExtraEnv @{ GITHUB_ACTIONS = 'true' }
$rK2 = Invoke-Sandbox 'runK2' @($tInit, $tRo) -AllSupportedEnabled
tc_check 'TEST-HILGATE-039-init-blocked-unattended-but-runs-attended' `
    (((Get-RanCount $rK1 '^RAN test_sbfake_init ') -eq 0) -and ((Get-RanCount $rK1 '^RAN test_sbfake_ro ') -eq 1) -and
     (Test-FailureCase $rK1 'TC-SAFETY-GATE-001: [^"]*test_sbfake_init[^"]*not approved for unattended HIL') -and $rK1.Exit -eq 1 -and
     ((Get-RanCount $rK2 '^RAN test_sbfake_init ') -eq 1) -and ((Get-RanCount $rK2 '^RAN test_sbfake_ro ') -eq 1) -and
     # Attended: executed; nonpassing ONLY because its PHC restoration is not independently verified (TEST-HILGATE-042;
     # expectation changed 2026-10-21 from exit 0 by the P0 rule "no mutation accepted without verified restoration").
     $rK2.Exit -eq 1 -and ($rK2.Out -match '\[INCONCLUSIVE_RESTORATION\] TC-INFRA-STATE-002: after test_sbfake_init\.ps1 \(Init\)') -and
     ($rK2.Out -match 'Failed : 1\b') -and ($rK2.Out -notmatch '\[(FAIL|BLOCKED|CLEANUP_FAILED)\] TC-')) `
    ("unattended: ran={0} exit={1} | attended: ran={2} exit={3} | {4}" -f ($rK1.Ran -join '/'), $rK1.Exit, ($rK2.Ran -join '/'), $rK2.Exit, (Tail $rK1))

# --- D2 (Q 2026-10-22): a genuinely ReadOnly timeout stays FAILED(TIMEOUT) but halts no later mutation once the process
# tree has terminated (outstanding I/O resolved) and the state is verified unchanged; otherwise the halt stays.
function Get-ExecRecords($r, [string]$rx) {
    return @(Get-ChildItem (Join-Path $r.Dir 'logs') -Filter 'results-*.json' -ErrorAction SilentlyContinue |
        ForEach-Object { @((Get-Content $_.FullName -Raw | ConvertFrom-Json).records) } | Where-Object { $_.executed -and $_.name -match $rx })
}
function Get-Disposition($rec) { if ($rec -and $rec.PSObject.Properties['timeoutDisposition']) { return [string]$rec.timeoutDisposition }; return '(none)' }
$tRoSlp = New-Fake 'test_sbfake_rosleep' 'Start-Sleep -Seconds 120'
$rTo1 = Invoke-Sandbox 'runTO1' @($tRoSlp, $tInit) -Timeout 3 -AllSupportedEnabled
$to1 = @(Get-ExecRecords $rTo1 'test_sbfake_rosleep')
tc_check 'TEST-HILGATE-070-readonly-timeout-terminated-state-verified-stays-failed-but-does-not-halt' `
    ($to1.Count -eq 1 -and $to1[0].verdict -eq 'FAILED(TIMEOUT)' -and (Get-Disposition $to1[0]) -eq 'TERMINATED_STATE_VERIFIED' -and
     (Get-RanCount $rTo1 '^RAN test_sbfake_init ') -eq 1 -and $rTo1.Out -notmatch '\[HALT\][^\r\n]*test_sbfake_rosleep' -and $rTo1.Exit -eq 1) `
    ("records={0} verdict={1} disposition={2} exit={3}; ran: {4}; {5}" -f $to1.Count, $(if ($to1.Count) { $to1[0].verdict }), (Get-Disposition $(if ($to1.Count) { $to1[0] })), $rTo1.Exit, ($rTo1.Ran -join ' / '), (Tail $rTo1))

# 071: cross-suite - only a ReadOnly FAILED(TIMEOUT) with a verified disposition is not a prior halt; a record without
# the disposition (older runner) or a non-ReadOnly timeout still halts (fail closed).
function New-TimeoutPrior([string]$Name, [string]$Class, [string]$Disposition) {
    $d = Join-Path $Tmp $Name; New-Item -ItemType Directory -Path $d -Force | Out-Null
    $rec = [ordered]@{ name = 'test_prior.exe'; verdict = 'FAILED(TIMEOUT)'; executed = $true; safetyClass = $Class }
    if ($Disposition) { $rec.timeoutDisposition = $Disposition }
    ([ordered]@{ schema = 'avb-runner-results/1'; suite = 'HardwareUnit'; configuration = 'Debug'; overall = 'FAILED'; exitCode = 1; stateHalt = ''; records = @($rec) } |
        ConvertTo-Json -Depth 5) | Set-Content -LiteralPath (Join-Path $d 'results-Debug-HardwareUnit.json') -Encoding UTF8
    return $d
}
$rTo2a = Invoke-Sandbox 'runTO2a' @($tInit, $tRo) -AllSupportedEnabled -PriorResultsDir (New-TimeoutPrior 'priorRoVerified' 'ReadOnly' 'TERMINATED_STATE_VERIFIED')
$rTo2b = Invoke-Sandbox 'runTO2b' @($tInit, $tRo) -AllSupportedEnabled -PriorResultsDir (New-TimeoutPrior 'priorRoLegacy' 'ReadOnly' '')
$rTo2c = Invoke-Sandbox 'runTO2c' @($tInit, $tRo) -AllSupportedEnabled -PriorResultsDir (New-TimeoutPrior 'priorInitTimeout' 'Init' 'TERMINATED_STATE_VERIFIED')
tc_check 'TEST-HILGATE-071-only-a-verified-readonly-timeout-is-not-a-prior-halt' `
    ((Get-RanCount $rTo2a '^RAN test_sbfake_init ') -eq 1 -and $rTo2a.Out -notmatch 'prior suite' -and
     (Get-RanCount $rTo2b '^RAN test_sbfake_init ') -eq 0 -and $rTo2b.Out -match 'state-integrity halt \(prior suite' -and
     (Get-RanCount $rTo2c '^RAN test_sbfake_init ') -eq 0 -and $rTo2c.Out -match 'state-integrity halt \(prior suite' -and
     (Get-RanCount $rTo2b '^RAN test_sbfake_ro ') -eq 1 -and (Get-RanCount $rTo2c '^RAN test_sbfake_ro ') -eq 1) `
    ("verified: ran={0} | legacy: ran={1} | init: ran={2} || {3}" -f ($rTo2a.Ran -join '/'), ($rTo2b.Ran -join '/'), ($rTo2c.Ran -join '/'), (Tail $rTo2a))

# 072: a child process of a timed-out ReadOnly test is terminated by the runner and verified gone before the disposition.
$childPidFile = Join-Path $Tmp 'rochild.pid'
$tRoChild = New-Fake 'test_sbfake_rochild' ("`$cp = Start-Process powershell.exe -ArgumentList '-NoProfile','-Command','Start-Sleep -Seconds 120' -WindowStyle Hidden -PassThru; " +
    "Set-Content -LiteralPath '$($childPidFile.Replace("'", "''"))' -Value `$cp.Id; Start-Sleep -Seconds 120")
$rTo3 = Invoke-Sandbox 'runTO3' @($tRoChild, $tInit) -Timeout 4 -AllSupportedEnabled
$childPid = if (Test-Path $childPidFile) { [int](Get-Content $childPidFile -Raw).Trim() } else { 0 }
$childAlive = ($childPid -gt 0) -and ($null -ne (Get-Process -Id $childPid -ErrorAction SilentlyContinue))
if ($childAlive) { Stop-Process -Id $childPid -Force -ErrorAction SilentlyContinue }   # never leave the fixture child behind
$to3 = @(Get-ExecRecords $rTo3 'test_sbfake_rochild')
tc_check 'TEST-HILGATE-072-timed-out-readonly-child-processes-are-terminated-and-verified' `
    ($childPid -gt 0 -and -not $childAlive -and $to3.Count -eq 1 -and (Get-Disposition $to3[0]) -eq 'TERMINATED_STATE_VERIFIED' -and
     (Get-RanCount $rTo3 '^RAN test_sbfake_init ') -eq 1) `
    ("child pid={0} aliveAfterRun={1} disposition={2}; ran: {3}; {4}" -f $childPid, $childAlive, (Get-Disposition $(if ($to3.Count) { $to3[0] })), ($rTo3.Ran -join ' / '), (Tail $rTo3))

# 073: per-test timeout ($HilTestTimeoutSeconds; sandbox: AVB_TEST_SANDBOX_TIMEOUTS) replaces a shorter suite timeout for
# that entry only; the runner's table gives test_atdecc_aen_protocol 420 s (3 adapters x (30+60+31) s of test waits).
$tRoSlow = New-Fake 'test_sbfake_roslow' 'Start-Sleep -Seconds 6'
$rTo4 = Invoke-Sandbox 'runTO4' @($tRoSlow, $tRoSlp) -Timeout 3 -AllSupportedEnabled -ExtraEnv @{ AVB_TEST_SANDBOX_TIMEOUTS = 'test_sbfake_roslow=30' }
$toAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                        $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and $x.Left.VariablePath.UserPath -eq 'HilTestTimeoutSeconds' }, $true) | Select-Object -First 1
$atdecc = $null
if ($toAst) { foreach ($kv in @($toAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true).KeyValuePairs)) { if ($kv.Item1.Value -eq 'test_atdecc_aen_protocol') { $atdecc = $kv.Item2.Extent.Text } } }
tc_check 'TEST-HILGATE-073-per-test-timeout-applies-to-that-entry-only' `
    ($rTo4.Out -notmatch 'test_sbfake_roslow\.ps1 TIMEOUT' -and $rTo4.Out -match 'test_sbfake_roslow\.ps1 PASSED' -and $rTo4.Out -match 'test_sbfake_rosleep\.ps1 TIMEOUT after 3s' -and
     $atdecc -eq '420') `
    ("atdecc table value={0}; {1}" -f $atdecc, (Tail $rTo4))

# --- D1 (Q 2026-10-22): a Mutating test that reports FAILED_SETUP before any state change ("no state modified") is not
# INCONCLUSIVE_RESTORATION from its static class alone - only when the runner independently verifies that nothing changed:
# adapter/TIMINCA/TSAUXC equal and the PHC timeline continuous (pre-test vs post-test sample) on every adapter.
$phcObservable = 'param($s) foreach ($c in $s.clock) { $c | Add-Member -NotePropertyName PhcOffsetNs -NotePropertyValue 0 -Force }'
$setupLine = "Write-Host '  [FAILED_SETUP] TC-SBFAKE-SETUP-002/adapter:0: precondition not established --- no state modified'; exit 1"
$tTgtSetup = New-Fake 'test_sbfake_tgtsetup' $setupLine
# Synthetic, labelled tolerance (sandbox PHC windows are provenance 'synthetic'; the evaluator rejects a synthetic tolerance
# for hardware measurements).  No hardware tolerance exists yet: on the HIL host the D1 path stays INCONCLUSIVE (077).
$synTol = @(foreach ($d in '0x125B', '0x1533') { [ordered]@{ did = $d; source = 'hardware-characterization'; offsetNs = 5000000; rateChangePpb = 1000000
                                                          evidence = 'SYNTHETIC TEST FIXTURE - not a measured tolerance' } })
$rD1a = Invoke-Sandbox 'runD1a' @($tTgtSetup, $tTgt) -AllSupportedEnabled -StateEdit $phcObservable -SafetyFile (New-ClearSafetyFile 'safety_d1a.json' $synTol)
$d1a = @(Get-ExecRecords $rD1a 'test_sbfake_tgtsetup')
tc_check 'TEST-HILGATE-074-failed-setup-without-observed-change-is-not-inconclusive-restoration' `
    ($rD1a.Out -notmatch '\[INCONCLUSIVE_RESTORATION\][^\r\n]*test_sbfake_tgtsetup' -and @([regex]::Matches($rD1a.Out, '\[STATE\] after test_sbfake_tgtsetup[^\r\n]*no state change')).Count -eq 2 -and
     $d1a.Count -eq 2 -and @($d1a | Where-Object { $_.verdict -eq 'FAILED_SETUP' }).Count -eq 2 -and
     (Get-RanCount $rD1a '^RAN test_sbfake_tgt ') -eq 2 -and $rD1a.Exit -eq 1) `
    ("records={0} verdicts={1} ran: {2}; state lines: {3}" -f $d1a.Count, (($d1a | ForEach-Object { $_.verdict }) -join ','), ($rD1a.Ran -join ' / '),
     ((@([regex]::Matches($rD1a.Out, '(?m)^.*(\[STATE\]|\[INCONCLUSIVE_RESTORATION\]|\[HALT\]).*$') | ForEach-Object { $_.Value.Trim() })) -join ' || '))

# 075: the same claim while the PHC timeline jumped (a SET_TIMESTAMP the test did not admit) -> INCONCLUSIVE_RESTORATION
# stays and halts later mutation.
$tTgtSetupPhc = New-Fake 'test_sbfake_tgtsetupphc' ("if (`$env:AVB_HIL_TARGET_DID -eq '0x125B') { " +
    (Edit-StateSnippet "`$c = @(`$s.clock | Where-Object { `$_.Did -eq '0x125B' })[0]; `$c.PhcOffsetNs = -300000000000") + " }`r`n" + $setupLine)
$rD1b = Invoke-Sandbox 'runD1b' @($tTgtSetupPhc, $tTgt) -AllSupportedEnabled -StateEdit $phcObservable -SafetyFile (New-ClearSafetyFile 'safety_d1b.json' $synTol)
tc_check 'TEST-HILGATE-075-failed-setup-claim-contradicted-by-a-phc-jump-stays-inconclusive-and-halts' `
    ($rD1b.Out -match '\[INCONCLUSIVE_RESTORATION\] TC-INFRA-STATE-002: after test_sbfake_tgtsetupphc[^\r\n]*no-mutation claim is NOT verified[^\r\n]*0x125B: CLEANUP_FAILED[^\r\n]*outside tolerance' -and
     (Get-RanCount $rD1b '^RAN test_sbfake_tgtsetupphc ') -ge 1 -and (Get-RanCount $rD1b '^RAN test_sbfake_tgt ') -eq 0) `
    ("ran: {0}; {1}" -f ($rD1b.Ran -join ' / '), (Tail $rD1b))

# 076: the claim cannot be verified when the PHC timeline is not observable -> INCONCLUSIVE_RESTORATION stays (fail closed).
$rD1c = Invoke-Sandbox 'runD1c' @($tTgtSetup, $tTgt) -AllSupportedEnabled -SafetyFile (New-ClearSafetyFile 'safety_d1c.json' $synTol)
tc_check 'TEST-HILGATE-076-failed-setup-claim-without-phc-observation-stays-inconclusive' `
    ($rD1c.Out -match '\[INCONCLUSIVE_RESTORATION\] TC-INFRA-STATE-002: after test_sbfake_tgtsetup[^\r\n]*no-mutation claim is NOT verified[^\r\n]*0x125B: FAILED_SETUP[^\r\n]*paired read' -and (Get-RanCount $rD1c '^RAN test_sbfake_tgt ') -eq 0) `
    ("ran: {0}; state lines: {1}" -f ($rD1c.Ran -join ' / '),
     ((@([regex]::Matches($rD1c.Out, '(?m)^.*(\[STATE\]|\[INCONCLUSIVE_RESTORATION\]|\[HALT\]).*$') | ForEach-Object { $_.Value.Trim() })) -join ' || '))

# 077: no hardware-derived tolerance (the current HIL state: none characterized) -> the claim is NOT verified and
# INCONCLUSIVE_RESTORATION stays; the evaluator never invents a threshold.
$rD1d = Invoke-Sandbox 'runD1d' @($tTgtSetup, $tTgt) -AllSupportedEnabled -StateEdit $phcObservable
tc_check 'TEST-HILGATE-077-failed-setup-claim-without-hardware-tolerance-stays-inconclusive' `
    ($rD1d.Out -match '\[INCONCLUSIVE_RESTORATION\] TC-INFRA-STATE-002: after test_sbfake_tgtsetup[^\r\n]*NOT verified[^\r\n]*no hardware-derived tolerance' -and
     (Get-RanCount $rD1d '^RAN test_sbfake_tgt ') -eq 0) `
    ("ran: {0}; state lines: {1}" -f ($rD1d.Ran -join ' / '), ((@([regex]::Matches($rD1d.Out, '(?m)^.*(\[STATE\]|\[INCONCLUSIVE_RESTORATION\]).*$') | ForEach-Object { $_.Value.Trim() })) -join ' || '))

# 082: UT-CORR-009 restores the driver it found (Q 2026-10-27). Defect from FullSuite_20261024_113438: the Release
# executable ran 'Install-Driver-Elevated.ps1 -Configuration Debug', a hard-coded literal, so a Release run would
# install the Debug driver, and no check compared the driver after the reinstall with the one found before it.
# Required: before the service stop, the configuration to reinstall is chosen by matching the installed
# System32\drivers\IntelAvbFilter.sys against the installer's source for each configuration
# (Install-Driver.ps1: build\x64\<cfg>\IntelAvbFilter\IntelAvbFilter). After the reinstall, a different installed
# driver is CLEANUP_FAILED. The check runs on comment-stripped source so that comments cannot satisfy it.
$lcSrc = Get-Content -LiteralPath (Join-Path $RepoRoot 'tests\integration\ptp_corr\test_ptp_phc_lifecycle.c') -Raw
$lcCode = [regex]::Replace($lcSrc, '(?s)/\*.*?\*/|//[^\r\n]*', '')
$lcBad = @()
if ($lcCode -match '-Configuration (Debug|Release)') { $lcBad += "hard-coded reinstall configuration '$($Matches[0])'" }
if ($lcCode -notmatch '-Configuration %s') { $lcBad += 'reinstall command does not take the configuration at run time (-Configuration %s)' }
foreach ($cfg in 'Debug', 'Release') {
    # PowerShell does not escape backslashes: this is the C source text L"build\\x64\\<cfg>\\...".
    if ($lcCode.IndexOf("build\\x64\\$cfg\\IntelAvbFilter\\IntelAvbFilter\\IntelAvbFilter.sys") -lt 0) { $lcBad += "installer source for $cfg not compared" }
}
$iSel = $lcCode.IndexOf('select_restore_configuration(&'); $iRun = $lcCode.IndexOf('run_installer(found.')   # the call, not the definition
if ($iSel -lt 0 -or $iRun -lt 0 -or $iSel -gt $iRun) { $lcBad += "restore configuration not selected before the installer runs (select@$iSel installer@$iRun)" }
# 2026-10-27 (Q: failure handling, #328). Defect observed on HIL by the existing chain (FullSuite_20261024_113438, Release
# UT-CORR-009 13:03:58): its installer chain hung in the netcfg install with every adapter disabled (transcript
# %TEMP%\install-driver-20261024_130403.log). The test produced no verdict and no evidence, was killed at 1800 s, and
# the chain stayed alive until 2026-10-26. The causes are in the source: an in-process ControlService(STOP)
# with no bound, an unbounded system() wait, Install-Driver-Elevated.ps1, whose RunAs child is interactive
# (Install-Driver.ps1 STOP_PENDING branch: Read-Host / Restart-Computer) and shows installer output only after it
# exits, and a failed reinstall reported as FAIL without evaluating restoration.
if ($lcCode -match '\bControlService\s*\(') { $lcBad += 'in-process ControlService(STOP) (unbounded while the driver unload blocks, #328)' }
if ($lcCode -match '\bsystem\s*\(') { $lcBad += 'system() (unbounded wait, no evidence while it hangs)' }
if ($lcCode -match 'Install-Driver-Elevated') { $lcBad += 'Install-Driver-Elevated.ps1 (interactive RunAs child; output only after exit)' }
if ($lcCode -notmatch 'Install-Driver\.ps1 -Configuration %s -Reinstall' -or $lcCode -notmatch '-NonInteractive') { $lcBad += 'installer not run as Install-Driver.ps1 -NonInteractive ... -Reinstall' }
if ($lcCode -notmatch 'WaitForSingleObject\s*\([^;]*LC_INSTALL_TIMEOUT_MS') { $lcBad += 'installer wait not bounded by LC_INSTALL_TIMEOUT_MS' }
if ($lcCode -notmatch 'logs\\\\ut_corr_009_install_') { $lcBad += 'installer output not preserved under logs\ (live evidence)' }
# Restoration verdict lines in the runner's grammar: Run-Tests-CI.ps1 halts later mutation on '^\s*\[CLEANUP_FAILED\]'
# and on INCONCLUSIVE_RESTORATION testcase lines (tools/lib/TestVerdict.ps1, Get-AvbVerdictCounts).
foreach ($tag in 'CLEANUP_FAILED', 'INCONCLUSIVE_RESTORATION') {
    if ($lcCode -notmatch ('"\s*\[' + $tag + '\] UT-CORR-009 ')) { $lcBad += "no '[$tag] UT-CORR-009' restoration verdict line" }
}
# STATIC: this is a contract on the source text only. It is not runtime evidence; runtime behaviour is verified only by
# HIL through Run-Tests-CI.ps1 (Unsafe entry, AVB_HIL_AUTHORIZE_UNSAFE naming it).
tc_check 'TEST-HILGATE-082-ut-corr-009-reinstall-and-restoration-source-contract-STATIC-not-runtime-evidence' ($lcBad.Count -eq 0) ($lcBad -join ' ### ')

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILGATE: safety gate regression" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILGATE: safety gate + state integrity verified" -ForegroundColor Green
exit 0
