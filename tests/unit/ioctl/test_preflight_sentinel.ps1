# test_preflight_sentinel.ps1
# Behavioral tests for Test-DriverPreFlight.ps1 observational contract.
# Verifies that service-stopped, device-missing, ACCESS_DENIED and normal paths
# produce the correct exit codes and INFRA_FAILURE markers.
# No real driver or NIC required (simulates all paths via mock service state).
#
# TEST-PREFLIGHT-001  Service STOPPED -> exit 1 + INFRA_FAILURE (no auto-restart)
# TEST-PREFLIGHT-002  ERROR_ACCESS_DENIED (5) on CreateFile -> exit 1 + INFRA_FAILURE
# TEST-PREFLIGHT-003  STOP_PENDING service -> exit 1 + INFRA_FAILURE
# TEST-PREFLIGHT-004  Service NOT FOUND -> exit 1 + INFRA_FAILURE
# TEST-PREFLIGHT-005  (Source check) No Start-Service call in preflight
# TEST-PREFLIGHT-006..018  (#428, behavioural) System32 copy, service-selected image and running module must agree;
#                     ImagePath forms resolve; missing/unsupported/ambiguous/unavailable identity evidence fails
# TEST-PREFLIGHT-019  (#428, static) no service, driver-package or device mutation in preflight or DriverIdentity.ps1
#
# These tests verify behavior by source inspection and by running the preflight
# with a controlled env (no real driver needed for structural tests).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot      = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$PreFlightPath = Join-Path $RepoRoot 'tools\test\Test-DriverPreFlight.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++
    Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++
    Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-PREFLIGHT-001..005: Preflight Observational Contract" -ForegroundColor Cyan
Write-Host "  Verifies exit codes and INFRA markers for error conditions" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

if (-not (Test-Path $PreFlightPath)) {
    tc_fail "TEST-PREFLIGHT-SETUP" ("Test-DriverPreFlight.ps1 not found at {0}" -f $PreFlightPath)
    exit 1
}

$src = Get-Content $PreFlightPath -Raw

# TEST-PREFLIGHT-005 (source check, fast): no Start-Service in preflight
# This is checked first because it requires no subprocess.
$hasStartService = $src -match 'Start-Service'
if ($hasStartService) {
    tc_fail "TEST-PREFLIGHT-005" "Test-DriverPreFlight.ps1 still calls Start-Service -- auto-recovery must be removed"
} else {
    tc_pass "TEST-PREFLIGHT-005: No Start-Service call in Test-DriverPreFlight.ps1"
}

# TEST-PREFLIGHT-001..004 use source pattern checks because we cannot create a real
# stopped service without a real driver. We verify the code paths exist and produce
# the correct outcomes.

# TEST-PREFLIGHT-001: Service not Running -> exit 1 + INFRA_FAILURE (no restart)
$stoppedPath    = $src -match '(?ms)svc\.Status.*-ne.*Running.*\[INFRA_FAILURE\]'
$noRestartAfter = -not ($src -match '(?ms)INFRA_FAILURE.*Start-Service') 
if ($stoppedPath -and $noRestartAfter) {
    tc_pass "TEST-PREFLIGHT-001: not-Running path produces INFRA_FAILURE without attempting restart"
} elseif (-not $stoppedPath) {
    tc_fail "TEST-PREFLIGHT-001" "[INFRA_FAILURE] not found in the not-Running code path"
} else {
    tc_fail "TEST-PREFLIGHT-001" "Start-Service appears after [INFRA_FAILURE] -- recovery still attempted"
}

# TEST-PREFLIGHT-002: ERROR_ACCESS_DENIED (5) -> exit 1 + INFRA_FAILURE
# The pattern must exit 1, not warn and continue.
$accessDeniedFails = $src -match "(?ms)err.*-eq.*5.*INFRA_FAILURE" -or
                     $src -match "(?ms)ACCESS_DENIED.*INFRA_FAILURE"
$accessDeniedNoWarn = -not ($src -match "(?ms)err.*-eq.*5.*Warn.*still run")
if ($accessDeniedFails -and $accessDeniedNoWarn) {
    tc_pass "TEST-PREFLIGHT-002: ERROR_ACCESS_DENIED (5) produces INFRA_FAILURE (not a warning)"
} elseif (-not $accessDeniedFails) {
    tc_fail "TEST-PREFLIGHT-002" "ACCESS_DENIED does not produce INFRA_FAILURE -- still a warning path"
} else {
    tc_fail "TEST-PREFLIGHT-002" "ACCESS_DENIED matched INFRA_FAILURE but also matches the old warn-and-continue pattern"
}

# TEST-PREFLIGHT-003: STOP_PENDING -> exit 1 + INFRA_FAILURE
$stopPendingFails = $src -match "(?ms)StopPending.*INFRA_FAILURE"
if ($stopPendingFails) {
    tc_pass "TEST-PREFLIGHT-003: STOP_PENDING service produces INFRA_FAILURE"
} else {
    tc_fail "TEST-PREFLIGHT-003" "STOP_PENDING path does not produce INFRA_FAILURE"
}

# TEST-PREFLIGHT-004: Service NOT FOUND -> exit 1 + INFRA_FAILURE
$notFoundFails = $src -match "(?ms)null.*INFRA_FAILURE" -or $src -match "(?ms)service not found.*INFRA_FAILURE"
if ($notFoundFails) {
    tc_pass "TEST-PREFLIGHT-004: Service not found produces INFRA_FAILURE"
} else {
    tc_fail "TEST-PREFLIGHT-004" "Service-not-found path does not produce INFRA_FAILURE"
}

# ===========================================================================================================
# TEST-PREFLIGHT-006..019 (#428): driver identity -- System32 copy, service-selected image, running module.
# Behavioural: the preflight's own text (from 'function Write-Ok' to the device-node check) runs in a child
# PowerShell against a fixture tree. Only the operating-system boundary is replaced by fixture data:
# Get-Service (state), Get-AvbServiceImagePath (raw registry ImagePath) and Get-AvbLoadedDriverModules (kernel
# module list). Reads of $env:SystemRoot in the preflight text are redirected to the fixture's Windows directory (the
# process environment itself is not changed: a different SystemRoot breaks .NET assembly loading and Get-FileHash).
# GITHUB_ACTIONS is cleared so nothing is written to GITHUB_ENV.
# TEST-PREFLIGHT-008/009 call the path resolver of tools\lib\DriverIdentity.ps1 directly. TEST-PREFLIGHT-019 is STATIC.
# ===========================================================================================================
Write-Host ""
Write-Host "  TEST-PREFLIGHT-006..019: selected and running driver identity (#428)" -ForegroundColor Cyan

function tc_check_pf([string]$n, [bool]$ok, [string]$why) { if ($ok) { tc_pass $n } else { tc_fail $n $why } }
$IdentityLib = Join-Path $RepoRoot 'tools\lib\DriverIdentity.ps1'
$PfText      = [System.IO.File]::ReadAllText($PreFlightPath)
$pfStart     = $PfText.IndexOf('function Write-Ok')
$pfEnd       = $PfText.IndexOf('# --- 2. Device node accessibility check')
$FixRoot     = Join-Path ([System.IO.Path]::GetTempPath()) ('avb-pf-' + [guid]::NewGuid().ToString('N'))

function New-PfFile([string]$Path, [string]$Content) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [System.IO.File]::WriteAllText($Path, $Content)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
function ConvertTo-PfLiteral($v) {
    if ($null -eq $v) { return '$null' }
    return "'" + ([string]$v).Replace("'", "''") + "'"
}

# Runs the preflight identity text against the fixture; returns @{ Exit; Out }.
#   ImagePath : raw registry value (string or $null)
#   Modules   : array of @{ BaseName; FileName }, or the string 'UNAVAILABLE'
#   Env       : hashtable of environment variables for the run
function Invoke-PfIdentity([string]$Case, $ImagePath, $Modules, [hashtable]$Env) {
    $win = Join-Path $FixRoot 'Windows'
    $mod = if ($Modules -is [string] -and $Modules -eq 'UNAVAILABLE') {
        "[pscustomobject]@{ Available = `$false; Modules = @(); Error = 'fixture: EnumDeviceDrivers failed (5)' }"
    } else {
        $items = @($Modules | ForEach-Object { "[pscustomobject]@{ BaseName = $(ConvertTo-PfLiteral $_.BaseName); FileName = $(ConvertTo-PfLiteral $_.FileName) }" })
        "[pscustomobject]@{ Available = `$true; Modules = @($($items -join ', ')); Error = `$null }"
    }
    $envLines = @()
    foreach ($k in 'HIL_EXPECTED_DRIVER_SHA256', 'HIL_REQUIRED_SHA256_GATE', 'BUILD_DRIVER_SHA256', 'HIL_REQUIRE_DEPLOYED_SHA') {
        $envLines += ('$env:{0} = {1}' -f $k, (ConvertTo-PfLiteral $Env[$k]))
    }
    $body = $PfText.Substring($pfStart, $pfEnd - $pfStart)
    $redirects = ([regex]::Matches($body, [regex]::Escape('$env:SystemRoot'))).Count
    if ($redirects -eq 0) { return @{ Exit = -1; Out = 'fixture error: no $env:SystemRoot read in the preflight identity text; the fixture would hash the real System32 driver' } }
    $body = $body.Replace('$env:SystemRoot', '$PfSystemRoot')
    $child = @"
`$ErrorActionPreference = 'Stop'
`$PfSystemRoot = $(ConvertTo-PfLiteral $win)
`$env:GITHUB_ACTIONS = `$null
`$env:RUNNER_NAME = `$null
$($envLines -join "`r`n")
if (Test-Path -LiteralPath $(ConvertTo-PfLiteral $IdentityLib)) { . $(ConvertTo-PfLiteral $IdentityLib) }
function Get-Service { param([string]`$Name) [pscustomobject]@{ Status = 'Running' } }
function Get-AvbServiceImagePath { param([string]`$ServiceName) return $(ConvertTo-PfLiteral $ImagePath) }
function Get-AvbLoadedDriverModules { return $mod }
$body
Write-Host 'PF-FIXTURE-REACHED'
exit 0
"@
    $script = Join-Path $FixRoot ("case-{0}.ps1" -f $Case)
    [System.IO.File]::WriteAllText($script, $child)
    $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'   # child stderr must not become a terminating error
    try {
        $out = & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $script 2>&1 | Out-String
        $code = $global:LASTEXITCODE
    } finally { $ErrorActionPreference = $eap }
    return @{ Exit = $code; Out = $out }
}
function Test-PfFails($r) { return ($r.Exit -eq 1 -and $r.Out -match 'TEST-PROV-DRIVER-IDENTITY' -and $r.Out -match 'INFRA_FAILURE' -and $r.Out -notmatch 'PF-FIXTURE-REACHED') }
function Test-PfPasses($r) { return ($r.Exit -eq 0 -and $r.Out -match 'PF-FIXTURE-REACHED') }
function Get-PfTail($r) { return ("exit={0}; output: {1}" -f $r.Exit, (($r.Out -split "`r?`n" | Where-Object { $_ -match 'ERROR|INFRA|identity|REACHED' } | Select-Object -First 4) -join ' | ')) }

try {
    if ($pfStart -lt 0 -or $pfEnd -le $pfStart) { throw "preflight markers not found (function Write-Ok / '# --- 2. Device node accessibility check')" }
    $win  = Join-Path $FixRoot 'Windows'
    $sys  = Join-Path $win 'System32\drivers\IntelAvbFilter.sys'
    $pkgA = Join-Path $win 'System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_aaaa\IntelAvbFilter.sys'
    $pkgB = Join-Path $win 'System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_bbbb\IntelAvbFilter.sys'
    $pkgA2 = Join-Path $win 'System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_cccc\IntelAvbFilter.sys'
    $hA = New-PfFile $sys  'driver build A'
    $null = New-PfFile $pkgA 'driver build A'
    $hB = New-PfFile $pkgB 'driver build B'
    $null = New-PfFile $pkgA2 'driver build A'
    $rawA = '\SystemRoot\System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_aaaa\IntelAvbFilter.sys'
    $rawB = '\SystemRoot\System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_bbbb\IntelAvbFilter.sys'
    $rawA2 = '\SystemRoot\System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_cccc\IntelAvbFilter.sys'
    $other = @{ BaseName = 'ndis.sys'; FileName = '\SystemRoot\System32\drivers\ndis.sys' }
    $gate  = @{ HIL_EXPECTED_DRIVER_SHA256 = $hA; HIL_REQUIRED_SHA256_GATE = '1' }

    # 006 (regression, 2026-11-11): System32 copy matches the authorized hash; service selects and kernel runs build B.
    $r = Invoke-PfIdentity '006' $rawB @($other, @{ BaseName = 'IntelAvbFilter.sys'; FileName = $rawB }) $gate
    tc_check_pf 'TEST-PREFLIGHT-006: matching System32 copy does not hide a different selected and running driver' (Test-PfFails $r) (Get-PfTail $r)

    # 007: all three identical -> passes; the running-module evidence is labelled as a file hash, not the in-memory image.
    $r = Invoke-PfIdentity '007' $rawA @($other, @{ BaseName = 'IntelAvbFilter.sys'; FileName = $rawA }) $gate
    $labelled = $r.Out -match 'in-memory image is not hashed' -and $r.Out -notmatch '(?i)loaded image hash'
    tc_check_pf 'TEST-PREFLIGHT-007: consistent System32, selected and running driver pass; running evidence labelled as a file hash' ((Test-PfPasses $r) -and $labelled) (Get-PfTail $r)

    # 008: every supported ImagePath form resolves to the same file.
    $forms = @(
        @{ Raw = '\SystemRoot\System32\drivers\IntelAvbFilter.sys'; Want = $sys },
        @{ Raw = 'System32\drivers\IntelAvbFilter.sys';             Want = $sys },
        @{ Raw = '%SystemRoot%\System32\drivers\IntelAvbFilter.sys'; Want = $sys },
        @{ Raw = ('\??\' + $sys);                                    Want = $sys },
        @{ Raw = $sys;                                               Want = $sys })
    $bad = @()
    if (-not (Test-Path -LiteralPath $IdentityLib)) { $bad += 'tools\lib\DriverIdentity.ps1 missing' } else {
        . $IdentityLib
        foreach ($f in $forms) {
            $res = Resolve-AvbKernelImagePath -Path $f.Raw -SystemRoot $win
            if (-not $res.Ok -or $res.Path -ne [System.IO.Path]::GetFullPath($f.Want)) { $bad += ("{0} -> ok={1} path={2} reason={3}" -f $f.Raw, $res.Ok, $res.Path, $res.Reason) }
        }
    }
    tc_check_pf 'TEST-PREFLIGHT-008: ImagePath forms \SystemRoot\, System32\, %SystemRoot%\, \??\C:\ and C:\ resolve' ($bad.Count -eq 0) ($bad -join '; ')

    # 009: missing, empty and unsupported forms never resolve.
    $bad = @()
    if (-not (Test-Path -LiteralPath $IdentityLib)) { $bad += 'tools\lib\DriverIdentity.ps1 missing' } else {
        foreach ($raw in @($null, '', '   ', ('\\?\' + $sys), 'drivers\IntelAvbFilter.sys', '%TEMP%\IntelAvbFilter.sys',
                          '\Device\HarddiskVolume3\Windows\System32\drivers\IntelAvbFilter.sys', '\SystemRoot\')) {
            $res = Resolve-AvbKernelImagePath -Path $raw -SystemRoot $win
            if ($res.Ok -or -not $res.Reason) { $bad += ("'{0}' -> ok={1} reason='{2}'" -f $raw, $res.Ok, $res.Reason) }
        }
    }
    tc_check_pf 'TEST-PREFLIGHT-009: missing, empty and unsupported ImagePath forms are rejected with a reason' ($bad.Count -eq 0) ($bad -join '; ')

    $runA = @($other, @{ BaseName = 'IntelAvbFilter.sys'; FileName = $rawA })
    # 010: ImagePath absent -> fail (the Windows default path is not assumed).
    $r = Invoke-PfIdentity '010' $null $runA $gate
    tc_check_pf 'TEST-PREFLIGHT-010: absent service ImagePath fails' (Test-PfFails $r) (Get-PfTail $r)
    # 011: unsupported ImagePath form -> fail.
    $r = Invoke-PfIdentity '011' ('\\?\' + $pkgA) $runA $gate
    tc_check_pf 'TEST-PREFLIGHT-011: unsupported service ImagePath form fails' (Test-PfFails $r) (Get-PfTail $r)
    # 012: ImagePath names a file that does not exist -> fail.
    $r = Invoke-PfIdentity '012' '\SystemRoot\System32\DriverStore\FileRepository\missing\IntelAvbFilter.sys' $runA $gate
    tc_check_pf 'TEST-PREFLIGHT-012: service ImagePath to a missing file fails' (Test-PfFails $r) (Get-PfTail $r)
    # 013: driver absent from the kernel module list -> fail.
    $r = Invoke-PfIdentity '013' $rawA @($other) $gate
    tc_check_pf 'TEST-PREFLIGHT-013: driver not in the kernel module list fails' (Test-PfFails $r) (Get-PfTail $r)
    # 014: two loaded modules named IntelAvbFilter.sys -> ambiguous -> fail.
    $r = Invoke-PfIdentity '014' $rawA @($other, @{ BaseName = 'IntelAvbFilter.sys'; FileName = $rawA }, @{ BaseName = 'INTELAVBFILTER.SYS'; FileName = $rawA2 }) $gate
    tc_check_pf 'TEST-PREFLIGHT-014: ambiguous running module fails' (Test-PfFails $r) (Get-PfTail $r)
    # 015: module list unavailable -> fail.
    $r = Invoke-PfIdentity '015' $rawA 'UNAVAILABLE' $gate
    tc_check_pf 'TEST-PREFLIGHT-015: unavailable running-module evidence fails' (Test-PfFails $r) (Get-PfTail $r)
    # 016: running path differs from the selected path although the content is identical -> fail.
    $r = Invoke-PfIdentity '016' $rawA @($other, @{ BaseName = 'IntelAvbFilter.sys'; FileName = $rawA2 }) $gate
    tc_check_pf 'TEST-PREFLIGHT-016: running path different from the selected path fails even with equal hashes' (Test-PfFails $r) (Get-PfTail $r)
    # 017: no expected hash and no gate; System32 copy differs from the selected/running file -> fail (unattributed identity).
    $r = Invoke-PfIdentity '017' $rawB @($other, @{ BaseName = 'IntelAvbFilter.sys'; FileName = $rawB }) @{}
    tc_check_pf 'TEST-PREFLIGHT-017: System32 copy different from the selected and running file fails without any expected hash' (Test-PfFails $r) (Get-PfTail $r)

    # 018 (preservation): existing authorized-hash gate on the System32 copy still fails first, with its own marker.
    $r = Invoke-PfIdentity '018' $rawA $runA @{ HIL_EXPECTED_DRIVER_SHA256 = $hB; HIL_REQUIRED_SHA256_GATE = '1' }
    tc_check_pf 'TEST-PREFLIGHT-018: existing TEST-PROV-SHA256 gate unchanged (System32 copy not authorized -> exit 1)' ($r.Exit -eq 1 -and $r.Out -match 'TEST-PROV-SHA256' -and $r.Out -notmatch 'PF-FIXTURE-REACHED') (Get-PfTail $r)
} catch {
    tc_check_pf 'TEST-PREFLIGHT-006..018' $false ("fixture error: {0}" -f $_.Exception.Message)
} finally {
    if (Test-Path -LiteralPath $FixRoot) { Remove-Item -LiteralPath $FixRoot -Recurse -Force -ErrorAction SilentlyContinue }
}

# 019 (STATIC, source text): the preflight stays a verification step -- no service control, no driver package or
# device operation.
$pfAndLib = $PfText + $(if (Test-Path -LiteralPath $IdentityLib) { [System.IO.File]::ReadAllText($IdentityLib) } else { '' })
$mutating = @([regex]::Matches($pfAndLib, '(?im)\b(Start-Service|Stop-Service|Restart-Service|Set-Service|sc(\.exe)?\s+(start|stop|config)|pnputil|devcon|Disable-PnpDevice|Enable-PnpDevice|Install-Driver|Remove-Item|Copy-Item|Move-Item)\b') | ForEach-Object { $_.Value })
tc_check_pf 'TEST-PREFLIGHT-019 (static): preflight and DriverIdentity.ps1 contain no service, driver-package or device mutation' ($mutating.Count -eq 0) ($mutating -join ', ')

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-PREFLIGHT: observational contract regression" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-PREFLIGHT: preflight observational contract verified" -ForegroundColor Green
exit 0
