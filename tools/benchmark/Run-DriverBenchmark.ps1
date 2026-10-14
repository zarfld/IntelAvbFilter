<#
.SYNOPSIS
    Controlled Debug vs Release driver benchmark for IntelAvbFilter.

.DESCRIPTION
    Executes the 2×2 matrix (Debug driver / Release driver) × (Debug test exes / Release test exes)
    for a fixed set of latency-sensitive tests. Each driver configuration change requires explicit
    operator approval. All results are captured in timestamped per-cell log directories.

    Canonical tools delegated to:
      Build-Driver.ps1             — driver and test build
      Install-Driver-Elevated.ps1  — driver install/reinstall
      Test-DriverPreFlight.ps1     — device verification
      Run-Tests.ps1                — test execution (called directly; already elevated)

.NOTES
    NEVER run this script automatically in CI.
    NEVER run this script while a GitHub Actions job is active on this runner.
    Requires elevated PowerShell (Administrator).
    DbgView is disabled by default to avoid latency overhead on benchmark tests.

.PARAMETER SkipBuild
    Skip the driver/test build step (assumes artifacts already exist).

.PARAMETER TestConfigs
    Test exe configurations to benchmark. Default: Debug,Release.

.PARAMETER DryRun
    Print all planned steps and prompts without executing build, install, or tests.

.PARAMETER BenchmarkDir
    Root directory for benchmark result logs. Default: logs\benchmark\<timestamp>

.EXAMPLE
    .\Run-DriverBenchmark.ps1 -SkipBuild
    Skip build (both driver configs already built), run full 2x2 matrix.

.EXAMPLE
    .\Run-DriverBenchmark.ps1 -DryRun
    Preview all steps without executing anything.
#>
param(
    [switch]$SkipBuild,
    [ValidateSet('Debug','Release')]
    [string[]]$TestConfigs = @('Debug','Release'),
    [switch]$DryRun,
    [string]$BenchmarkDir = ""
)

$ErrorActionPreference = 'Stop'
# Note: Set-StrictMode is intentionally omitted. PowerShell strict mode propagates to
# all child scopes including canonical tool scripts (Build-Tests.ps1, etc.) which access
# optional hashtable keys like .Disabled that legitimately absent on most entries.
# PropertyNotFoundStrict would fire in those scripts. Correctness belongs in the tools.

# ─── Require elevation ───────────────────────────────────────────────────────
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
          ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script must be run as Administrator."
    exit 1
}

# ─── Paths ───────────────────────────────────────────────────────────────────
$repoRoot  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$buildTool = Join-Path $repoRoot 'tools\build\Build-Driver.ps1'
$btestTool = Join-Path $repoRoot 'tools\build\Build-Tests.ps1'
$instTool  = Join-Path $repoRoot 'tools\setup\Install-Driver-Elevated.ps1'
$pftool    = Join-Path $repoRoot 'tools\test\Test-DriverPreFlight.ps1'
$runTool   = Join-Path $repoRoot 'tools\test\Run-Tests.ps1'

foreach ($t in @($buildTool, $btestTool, $instTool, $pftool, $runTool)) {
    if (-not (Test-Path $t)) { Write-Error "Required tool not found: $t"; exit 1 }
}

$runId = Get-Date -Format 'yyyyMMdd_HHmmss'
if (-not $BenchmarkDir) {
    $BenchmarkDir = Join-Path $repoRoot "logs\benchmark\$runId"
}
New-Item -ItemType Directory -Path $BenchmarkDir -Force | Out-Null

# ─── Benchmark test list ─────────────────────────────────────────────────────
# Ordering is deliberate:
#   ptp_clock_control_test.exe runs TC-5b FIRST (per its own main() contract),
#   before Tests 1-4 mutate TIMINCA. All subsequent tests therefore run after
#   TIMINCA has been written and (optionally) restored.
#
# No --case filters forwarded to ptp_clock_control_test (it has none).
# test_ptp_phc_stability supports --case but the default (UT-CORR-007+008) is used
# so the same test scope applies across all cells.
#
# Excluded from this benchmark (non-relevant or potentially blocking):
#   test_atdecc_aen_protocol — always times out (no peer)
#   test_ioctl_tas           — TAS requires specific hardware support
#   test_lifecycle_coverage  — requires driver lifecycle; may leave state
#   test_hw_state_machine    — DID whitelist defect (D-Z2, not yet fixed)
$BenchmarkTests = @(
    'ptp_clock_control_test.exe'      # TC-5b PHC/QPC coherence; TC-5b runs first in main()
    'test_timestamp_latency.exe'      # P50/P95/P99 latency; no --case filter
    'test_tx_timestamp_retrieval.exe' # TC-CORR-TX-RX-001 per adapter; no --case filter
    'test_ptp_phc_stability.exe'      # UT-CORR-007+008 (PRE_SEND_PHC overhead); no --case needed
    'test_ptp_001.exe'                # TC-PTP-001-CORR rate stability; no --case filter
)

# Driver configs: Release first (already installed), then Debug
$DriverConfigs = @('Release','Debug')

# ─── Helpers ─────────────────────────────────────────────────────────────────
function Write-Banner([string]$msg) {
    Write-Host "" ; Write-Host ("=" * 70) -ForegroundColor Cyan
    Write-Host "  $msg" -ForegroundColor Cyan ; Write-Host ("=" * 70) -ForegroundColor Cyan
}
function Write-Ok([string]$m)   { Write-Host "[OK]   $m" -ForegroundColor Green }
function Write-Warn([string]$m) { Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Write-Fail([string]$m) { Write-Host "[FAIL] $m" -ForegroundColor Red }
function Write-Info([string]$m) { Write-Host "[INFO] $m" -ForegroundColor Gray }

function Require-Approval([string]$prompt) {
    if ($DryRun) { Write-Info "[DRY-RUN] Would prompt: $prompt"; return }
    Write-Host "" ; Write-Host $prompt -ForegroundColor Yellow
    Write-Host "  Type YES to proceed, anything else to abort: " -ForegroundColor Yellow -NoNewline
    $ans = Read-Host
    if ($ans -ne 'YES') { Write-Fail "Aborted by operator."; exit 1 }
}

function Get-DriverFileHash([string]$path) {
    if (Test-Path $path) { return (Get-FileHash $path -Algorithm SHA256).Hash }
    return $null
}

function Record-DriverIdentity([string]$cellId, [string]$driverConfig) {
    $driverStorePath = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter" `
        -ErrorAction SilentlyContinue).ImagePath
    $driverStoreResolved = if ($driverStorePath) {
        $driverStorePath -replace '\\SystemRoot', $env:SystemRoot
    } else { $null }

    $buildArt  = Join-Path $repoRoot "build\x64\$driverConfig\IntelAvbFilter\IntelAvbFilter.sys"
    $sys32     = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"

    $buildHash = Get-DriverFileHash $buildArt
    $sys32Hash = Get-DriverFileHash $sys32
    $storeHash = if ($driverStoreResolved) { Get-DriverFileHash $driverStoreResolved } else { $null }

    $contract = if ($buildHash -and $sys32Hash -and $buildHash -eq $sys32Hash -and
                    ($null -eq $storeHash -or $storeHash -eq $buildHash)) {
        "VERIFIED_FILE_MATCH"
    } elseif ($sys32Hash) {
        "BASELINE_DRIVER_ONLY"
    } else {
        "UNVERIFIED"
    }

    $rec = [ordered]@{
        Timestamp          = (Get-Date -Format 'o')
        CellId             = $cellId
        DriverConfig       = $driverConfig
        Commit             = (& git -C $repoRoot rev-parse HEAD 2>$null)
        DirtyTree          = ("$(git -C $repoRoot status --short 2>$null)".Trim() -ne "")
        DriverVersion      = if (Test-Path $sys32) { (Get-Item $sys32).VersionInfo.FileVersion } else { $null }
        DriverDate         = if (Test-Path $sys32) { (Get-Item $sys32).LastWriteTime.ToString('o') } else { $null }
        BuildArtifactSHA256  = $buildHash
        System32SHA256       = $sys32Hash
        DriverStoreSHA256    = $storeHash
        DriverStorePath      = $driverStoreResolved
        LoadedImageSHA256    = "UNVERIFIED (kernel debugger required)"
        Contract             = $contract
    }
    $rec | ConvertTo-Json | Out-File (Join-Path $BenchmarkDir "${cellId}_identity.json") -Encoding utf8

    Write-Ok "  Contract: $contract"
    Write-Info "  BuildArt : $($rec.BuildArtifactSHA256)"
    Write-Info "  System32 : $($rec.System32SHA256)"
    Write-Info "  DrvrStore: $($rec.DriverStoreSHA256)"
    if ($contract -ne 'VERIFIED_FILE_MATCH') {
        Write-Warn "  Results for cell $cellId are BASELINE_DRIVER_ONLY (not attributed to current build)"
    }
    return $rec
}

# ─── Pre-run environment check ───────────────────────────────────────────────
Write-Banner "Pre-run environment check"

$commit = (& git -C $repoRoot rev-parse HEAD 2>$null)
$dirty  = ("$(git -C $repoRoot status --short 2>$null)".Trim() -ne "")
Write-Info "Commit    : $commit"
Write-Info "Dirty tree: $dirty"
if ($dirty) { Write-Warn "Uncommitted changes exist. Results tied to $commit + local modifications." }

$workers = @(Get-Process "Runner.Worker" -ErrorAction SilentlyContinue)
if ($workers.Count -gt 0) {
    Write-Fail "Runner.Worker is active (PID $($workers[0].Id)). A CI job may be running."
    Write-Fail "Abort: do not install drivers while a CI job is active."
    if (-not $DryRun) { exit 1 }
    Write-Warn "[DRY-RUN] Would abort here."
}

$preinstalledSHA = Get-DriverFileHash "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
$preinstalledDate = if (Test-Path "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys") {
    (Get-Item "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys").LastWriteTime.ToString('o')
}
Write-Info "Pre-benchmark installed driver:"
Write-Info "  SHA256: $preinstalledSHA"
Write-Info "  Date  : $preinstalledDate"

# Record rollback baseline
[pscustomobject]@{ SHA256=$preinstalledSHA; Date=$preinstalledDate; Commit=$commit } |
    ConvertTo-Json | Out-File (Join-Path $BenchmarkDir "rollback_baseline.json") -Encoding utf8
Write-Ok "Rollback baseline saved to $BenchmarkDir\rollback_baseline.json"
Write-Info "Manual rollback (if needed after benchmark):"
Write-Info "  1. Identify which build config produced SHA $preinstalledSHA"
Write-Info "  2. Run: .\tools\setup\Install-Driver-Elevated.ps1 -Configuration (Debug or Release) -Action Reinstall"
Write-Info "  3. Verify installed SHA = $preinstalledSHA"

# ─── Stage 1: Build ──────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Banner "Stage 1: Build"

    # Read and pin build number so both configs share the same value
    $bnFile = Join-Path $repoRoot 'build_number.txt'
    $bn = if (Test-Path $bnFile) { [int](Get-Content $bnFile) + 1 } else { 1 }
    Write-Info "Pinning build number to $bn for both driver configs"

    foreach ($cfg in @('Debug','Release')) {
        Write-Banner "  Build driver: $cfg"
        Set-Content $bnFile $bn.ToString()  # pin before each call so both share same number
        if ($DryRun) {
            Write-Info "[DRY-RUN] Build-Driver.ps1 -Configuration $cfg -Sign"
        } else {
            & $buildTool -Configuration $cfg -Sign
            if ($LASTEXITCODE -ne 0) { Write-Fail "Driver build failed: $cfg"; exit 1 }
        }
        $sha = Get-DriverFileHash (Join-Path $repoRoot "build\x64\$cfg\IntelAvbFilter\IntelAvbFilter.sys")
        Write-Ok "[$cfg] SHA256: $sha"
        [pscustomobject]@{ Config=$cfg; SHA256=$sha; BuildNumber=$bn; Commit=$commit } |
            ConvertTo-Json | Out-File (Join-Path $BenchmarkDir "build_${cfg}.json") -Encoding utf8
    }

    foreach ($tc in $TestConfigs) {
        Write-Banner "  Build test exes: $tc"
        if ($DryRun) {
            Write-Info "[DRY-RUN] Build-Tests.ps1 -Configuration $tc"
        } else {
            & $btestTool -Configuration $tc
            if ($LASTEXITCODE -ne 0) { Write-Fail "Test build failed: $tc"; exit 1 }
        }
    }
} else {
    Write-Info "SkipBuild: using existing artifacts."
    foreach ($cfg in @('Debug','Release')) {
        $sha = Get-DriverFileHash (Join-Path $repoRoot "build\x64\$cfg\IntelAvbFilter\IntelAvbFilter.sys")
        Write-Info "[$cfg] existing SYS SHA256: $sha"
    }
}

# ─── Stage 2: 2×2 matrix ─────────────────────────────────────────────────────
Write-Banner "Stage 2: Benchmark matrix"
Write-Info "Driver configs  : $($DriverConfigs -join ', ')"
Write-Info "Test exe configs: $($TestConfigs -join ', ')"
Write-Info "Tests           : $($BenchmarkTests -join ', ')"
Write-Info "DbgView         : DISABLED (latency benchmark; enable separately for diagnostics)"

$allResults = [System.Collections.Generic.List[object]]::new()

foreach ($driverConfig in $DriverConfigs) {
    Write-Banner "Driver config: $driverConfig"

    $buildArtSHA  = Get-DriverFileHash (Join-Path $repoRoot "build\x64\$driverConfig\IntelAvbFilter\IntelAvbFilter.sys")
    $installedSHA = Get-DriverFileHash "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
    $needsInstall = ($buildArtSHA -ne $installedSHA)

    if ($needsInstall) {
        Write-Info "Build artifact SHA : $buildArtSHA"
        Write-Info "Currently installed: $installedSHA"
        Require-Approval @"
=== DRIVER INSTALLATION REQUIRED ===
  Action    : Reinstall $driverConfig driver
  From SHA  : $installedSHA
  To SHA    : $buildArtSHA
  RISK      : All network adapters will be DISABLED briefly (NDIS unbind).
              Network connections (RDP, GitHub runner, SSH) will be interrupted.
              Confirm no CI jobs are active and no remote operations depend on this machine.
Type YES to continue:
"@
        if ($DryRun) {
            Write-Info "[DRY-RUN] Would install $driverConfig driver"
        } else {
            & $instTool -Configuration $driverConfig -Action Reinstall
            if ($LASTEXITCODE -ne 0) { Write-Fail "Install failed for $driverConfig."; exit 1 }
            Write-Ok "$driverConfig driver installed."
            Write-Info "Waiting 10 s for NDIS to settle..."
            Start-Sleep -Seconds 10
        }
    } else {
        Write-Ok "$driverConfig driver already installed (SHA matches). No reinstall needed."
    }

    # Preflight
    Write-Info "Running preflight..."
    if ($DryRun) {
        Write-Info "[DRY-RUN] Test-DriverPreFlight.ps1"
    } else {
        & $pftool
        if ($LASTEXITCODE -ne 0) { Write-Fail "Preflight FAILED after $driverConfig install."; exit 1 }
        Write-Ok "Preflight passed."
    }

    $identityCell = "identity_DRV${driverConfig}"
    $identity = Record-DriverIdentity $identityCell $driverConfig

    # Run tests for each test exe config
    foreach ($testConfig in $TestConfigs) {
        $cellId = "DRV${driverConfig}_EXE${testConfig}"
        Write-Banner "Cell: $cellId"

        $cellLogDir = Join-Path $BenchmarkDir $cellId
        New-Item -ItemType Directory -Path $cellLogDir -Force | Out-Null

        $cellSummary = [ordered]@{
            CellId       = $cellId
            DriverConfig = $driverConfig
            TestConfig   = $testConfig
            Timestamp    = (Get-Date -Format 'o')
            DriverIdentityContract = $identity.Contract
            Tests        = [ordered]@{}
        }

        foreach ($testExe in $BenchmarkTests) {
            $logFile = Join-Path $cellLogDir ("$($testExe -replace '\.exe$','')_$(Get-Date -Format 'HHmmss').log")
            Write-Info "--- $testExe ---"
            Write-Info "    Log: $logFile"

            if ($DryRun) {
                Write-Info "[DRY-RUN] Run-Tests.ps1 -Configuration $testConfig -TestExecutable $testExe -CollectLogs"
                $cellSummary.Tests[$testExe] = @{ ExitCode = 0; LogFile = $logFile }
                continue
            }

            # Already elevated — call Run-Tests.ps1 directly (no second UAC hop needed)
            $inner = "Start-Transcript -Path '$logFile' -Force | Out-Null; " +
                     "`$_ec=0; try { & '$runTool' -Configuration $testConfig -TestExecutable '$testExe' -CollectLogs; " +
                     "`$_ec=`$LASTEXITCODE } catch { `$_ec=1 } finally { Stop-Transcript | Out-Null }; exit `$_ec"
            $proc = Start-Process powershell `
                -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-Command',$inner) `
                -Wait -PassThru -WindowStyle Hidden
            $ec = if ($proc) { $proc.ExitCode } else { -1 }

            $cellSummary.Tests[$testExe] = @{ ExitCode = $ec; LogFile = $logFile }
            $status = if ($ec -eq 0) { "PASS" } elseif ($ec -eq 2) { "SKIP" } else { "FAIL($ec)" }
            Write-Info "    Exit: $status"
        }

        $allResults.Add($cellSummary)
        $cellSummary | ConvertTo-Json -Depth 5 |
            Out-File (Join-Path $BenchmarkDir "${cellId}_summary.json") -Encoding utf8
        Write-Ok "Cell $cellId complete."
    }
}

# ─── Final state and rollback reminder ──────────────────────────────────────
Write-Banner "Benchmark complete"
Write-Info "Results root: $BenchmarkDir"

$finalSHA = Get-DriverFileHash "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
Write-Info "Final installed driver SHA: $finalSHA"
if ($finalSHA -ne $preinstalledSHA) {
    Write-Warn "Installed driver changed from pre-benchmark state."
    Write-Warn "Pre-benchmark SHA: $preinstalledSHA"
    Write-Warn "Current SHA      : $finalSHA"
    Write-Warn ""
    Write-Warn "ROLLBACK PROCEDURE:"
    Write-Warn "  1. Determine which build config produced SHA $preinstalledSHA"
    Write-Warn "     (check logs\benchmark\$runId\rollback_baseline.json)"
    Write-Warn "      2. Run: .\tools\setup\Install-Driver-Elevated.ps1 -Configuration (Debug or Release) -Action Reinstall"
    Write-Warn "  3. Confirm: Get-FileHash C:\Windows\System32\drivers\IntelAvbFilter.sys"
    Write-Warn "     Expected: $preinstalledSHA"
} else {
    Write-Ok "Installed driver unchanged from pre-benchmark baseline."
}

Write-Info ""
Write-Info "=== Stage 3 analysis: parse log files ==="
Write-Info "  For each cell in: $BenchmarkDir\DRV*_EXE*\"
Write-Info "  Extract from test_timestamp_latency log:"
Write-Info "    P50/P95/P99 latency per adapter"
Write-Info "  Extract from test_ptp_phc_stability log:"
Write-Info "    UT-CORR-007 mean/stddev delta per adapter"
Write-Info "    UT-CORR-008 burst out_of_range count and stddev"
Write-Info "  Extract from test_tx_timestamp_retrieval log:"
Write-Info "    TC-CORR-TX-RX-001 max_delta and pass rate per adapter"
Write-Info "  Extract from ptp_clock_control_test log:"
Write-Info "    TC-5b PHC/QPC rate per adapter (w1/w2/w3)"
Write-Info "  Extract from test_ptp_001 log:"
Write-Info "    TC-PTP-001-CORR pass rate and mean ratio per adapter"
