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

$registry = @{}
$regAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq 'HilTestSafety' }, $true) | Select-Object -First 1
if ($null -ne $regAst) {
    $ht = $regAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
    foreach ($kv in $ht.KeyValuePairs) { $registry[$kv.Item1.Value] = ($kv.Item2.Extent.Text.Trim() -replace "^'|'$", '') }
}
$validClasses = @('ReadOnly', 'Init', 'Mutating', 'Mutating:Target', 'Unsafe', 'Unknown')

$missing = @($hwEntries | Where-Object { -not $registry.ContainsKey($_) })
tc_check 'TEST-HILGATE-001-every-hardware-entry-classified' (($registry.Count -gt 0) -and ($missing.Count -eq 0)) `
    ("registry entries={0}; unclassified: {1}" -f $registry.Count, ($missing -join ', '))

$badClass = @($registry.Keys | Where-Object { $validClasses -notcontains $registry[$_] } | ForEach-Object { "$_=$($registry[$_])" })
$stale    = @($registry.Keys | Where-Object { ($hwEntries -notcontains $_) -and ($registry[$_] -ne 'Unsafe') })
tc_check 'TEST-HILGATE-002-registry-values-valid-no-stale-keys' (($registry.Count -gt 0) -and $badClass.Count -eq 0 -and $stale.Count -eq 0) `
    ("invalid: {0}; stale (not in any Hardware list): {1}" -f ($badClass -join ', '), ($stale -join ', '))

$unsafeKeys = @($registry.Keys | Where-Object { $registry[$_] -eq 'Unsafe' })
$unsafeListed = @($unsafeKeys | Where-Object { $hwEntries -contains $_ })
$needUnsafe = @('test_hot_plug', 'test_lifecycle_coverage' | Where-Object { $registry[$_] -ne 'Unsafe' })
tc_check 'TEST-HILGATE-003-nic-admin-tests-unsafe-and-not-in-unrestricted-suites' (($registry.Count -gt 0) -and $unsafeListed.Count -eq 0 -and $needUnsafe.Count -eq 0) `
    ("Unsafe entries still listed in a Hardware suite: {0}; not classified Unsafe: {1}" -f ($unsafeListed -join ', '), ($needUnsafe -join ', '))

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
$roExceptions  = @{ 'test_ioctl_abi' = 'sizeof/code checks only'; 'test_ptp_phc_stability' = 'no args runs UT-CORR-007/008 only; 005/006/009 need --case' }
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
foreach ($f in 'Run-Tests-CI.ps1', 'TestVerdict.ps1', 'Convert-Logs-To-JUnit.ps1') {
    Copy-Item (Join-Path $RepoRoot "tools\test\$f") (Join-Path $Tree "tools\test\$f")
}
@'
function Get-AvbLifecycleSnapshot { return $null }
function Write-LifecycleDiff { param($Before, $After, $Label) return $false }
'@ | Set-Content (Join-Path $Tree 'tools\test\Lib-AvbLifecycle.ps1') -Encoding UTF8
Copy-Item (Join-Path $RepoRoot 'src\avb_integration_fixed.c') (Join-Path $Tree 'src\avb_integration_fixed.c')
Copy-Item (Join-Path $RepoRoot 'include\intel_pci_ids.h')     (Join-Path $Tree 'include\intel_pci_ids.h')
$TreeRunner = Join-Path $Tree 'tools\test\Run-Tests-CI.ps1'

$Classes = 'test_sbfake_ro=ReadOnly;test_sbfake_ro2=ReadOnly;test_sbfake_init=Init;test_sbfake_mut=Mutating;' +
           'test_sbfake_tgt=Mutating:Target;test_sbfake_unsafe=Unsafe;test_sbfake_disabler=ReadOnly;' +
           'test_sbfake_sleeper=Init;test_sbfake_sleepchg=Init;test_sbfake_timinca=Mutating:Target'

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

function New-StateFile([string]$Path) {
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
    ($state | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Invoke-Sandbox {
    param([string]$Name, [string[]]$Tests, [string]$Suite = 'HardwareUnit', [int]$Timeout = 60,
          [hashtable]$ExtraEnv = @{}, [switch]$NoLogsDir)
    $dir  = Join-Path $Tmp $Name
    $logs = Join-Path $dir 'logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $stateFile = Join-Path $dir 'state.json'
    New-StateFile $stateFile
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
    $mDir = Join-Path $Tree 'build\tools\avb_test\x64\Debug'
    New-Item -ItemType Directory -Path $mDir -Force | Out-Null
    ([ordered]@{ schemaVersion = 1; producer = 'test_hil_safety_gate.ps1'; configuration = 'Debug'; platform = 'x64'; entries = $mEntries } |
        ConvertTo-Json -Depth 5) | Set-Content -LiteralPath (Join-Path $mDir 'test-build-manifest.json') -Encoding UTF8
    $envs = [ordered]@{
        AVB_TEST_SANDBOX = '1'; AVB_TEST_ADAPTER_STATE_FILE = $stateFile; AVB_TEST_SANDBOX_CLASSES = $Classes
        AVB_TEST_STATE_SETTLE_SECONDS = '1'; GITHUB_ACTIONS = ''; GITHUB_ENV = ''; AVB_HIL_TARGET_DID = ''
        AVB_HIL_AUTHORIZE_UNSAFE = ''; AVB_TEST_HEALTH_FAIL_AFTER = ''
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
    $lines += "& '$TreeRunner' -Suite $Suite -Tests @($testsLit) -TestTimeoutSeconds $Timeout -Configuration Debug $logsArg"
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
    '030-sandbox-requires-isolated-logs-dir')
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
tc_check 'TEST-HILGATE-016-readonly-and-init-run-once-without-target-env' `
    (($roRuns.Count -eq 2) -and (@($roRuns | Where-Object { $_ -notmatch 'target=$' }).Count -eq 0)) ("ran: {0}" -f ($rA.Ran -join ' / '))
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

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILGATE: safety gate regression" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILGATE: safety gate + state integrity verified" -ForegroundColor Green
exit 0
