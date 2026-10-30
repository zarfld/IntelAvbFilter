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
      Run-Tests-CI.ps1             — test execution: registered entries only (provenance, HIL safety gate, state check)

    The whole matrix holds the HIL machine lock (tools\lib\HilLock.ps1); builds, installs and test runs started by
    this script run inside it.  Since 2026-10-29 (TEST-HILPATH-003) no test program is started outside the dispatcher:
    ptp_clock_control_test is Mutating (TC-5b ADJUST_FREQUENCY / SET_TIMESTAMP / INIT_DEVICE) and is reported
    BLOCKED until its read-only/mutating split (#340) - a visible gap in this benchmark, never a silent skip.

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
$runCi     = Join-Path $repoRoot 'tools\test\Run-Tests-CI.ps1'

foreach ($t in @($buildTool, $btestTool, $instTool, $pftool, $runCi)) {
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
# Registered runner entry -> the suite that lists it (tools\test\Run-Tests-CI.ps1).
$BenchmarkTests = [ordered]@{
    'ptp_clock_control_test'      = 'HardwareUnit'         # TC-5b PHC/QPC coherence; Mutating -> BLOCKED until #340
    'test_timestamp_latency'      = 'HardwareUnit'         # P50/P95/P99 latency; no --case filter
    'test_tx_timestamp_retrieval' = 'HardwareUnit'         # TC-CORR-TX-RX-001 per adapter; no --case filter
    'test_ptp_phc_stability'      = 'HardwareIntegration'  # UT-CORR-007+008 (PRE_SEND_PHC overhead); no --case needed
    'test_ptp_001'                = 'HardwareIntegration'  # TC-PTP-001-CORR rate stability; no --case filter
}

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

function Get-OemInfForImagePath([string]$imagePath) {
    # Returns the oem*.inf name that registered the given DriverStore path.
    # Reads HKLM service DisplayName which INetCfg stamps with "@oem<N>.inf,..." on install.
    $dn = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter" `
           -ErrorAction SilentlyContinue).DisplayName
    if ($dn -match '@(oem\d+\.inf)') { return $Matches[1] }
    return $null
}

function Record-DriverIdentity([string]$cellId, [string]$driverConfig) {
    # ── 1. Build artifact SHA (outer build = package binary, SHA proven identical) ──
    $buildArtPath  = Join-Path $repoRoot "build\x64\$driverConfig\IntelAvbFilter\IntelAvbFilter.sys"
    $buildArtHash  = Get-DriverFileHash $buildArtPath
    $buildArtSize  = if (Test-Path $buildArtPath) { (Get-Item $buildArtPath).Length } else { $null }

    # ── 2. Configured ImagePath (what the kernel SCM will load on next start) ──
    $svcReg         = Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter" `
                      -ErrorAction SilentlyContinue
    $imagePathRaw   = $svcReg.ImagePath           # e.g. \SystemRoot\System32\DriverStore\...
    $imagePathRes   = if ($imagePathRaw) { $imagePathRaw -replace '\\SystemRoot', $env:SystemRoot } else { $null }
    $imagePathHash  = if ($imagePathRes) { Get-DriverFileHash $imagePathRes } else { $null }
    $imagePathSize  = if ($imagePathRes -and (Test-Path $imagePathRes)) { (Get-Item $imagePathRes).Length } else { $null }
    $imagePathDate  = if ($imagePathRes -and (Test-Path $imagePathRes)) { (Get-Item $imagePathRes).LastWriteTime.ToString('o') } else { $null }
    $registeredOem  = Get-OemInfForImagePath $imagePathRaw

    # ── 3. System32 copy (written by installer as a reference copy, NOT what kernel loads) ──
    $sys32Path = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
    $sys32Hash = Get-DriverFileHash $sys32Path
    $sys32Size = if (Test-Path $sys32Path) { (Get-Item $sys32Path).Length } else { $null }
    $sys32Date = if (Test-Path $sys32Path) { (Get-Item $sys32Path).LastWriteTime.ToString('o') } else { $null }

    # ── 4. Driver version from System32 copy (version resource stays meaningful) ──
    $driverVersion = if (Test-Path $sys32Path) { (Get-Item $sys32Path).VersionInfo.FileVersion } else { $null }

    # ── 5. Classification ────────────────────────────────────────────────────
    # VERIFIED_FILE_MATCH   : ImagePath file SHA == build artifact SHA.
    #                         This is the highest provenance achievable without a kernel debugger.
    # DRIVER_IDENTITY_MISMATCH: ImagePath file SHA != build artifact SHA.
    #                         Driver under test is NOT the selected build configuration.
    #                         All test results must be labelled accordingly.
    # DRIVER_IDENTITY_UNKNOWN: ImagePath absent, unresolvable, or file missing.
    # LOADED_IMAGE           : Always UNVERIFIED (requires kernel debugger or ETW module load events).
    $contract = if (-not $imagePathHash -or -not $buildArtHash) {
        "DRIVER_IDENTITY_UNKNOWN"
    } elseif ($imagePathHash -eq $buildArtHash) {
        "VERIFIED_FILE_MATCH"
    } else {
        "DRIVER_IDENTITY_MISMATCH"
    }

    $rec = [ordered]@{
        Timestamp              = (Get-Date -Format 'o')
        CellId                 = $cellId
        DriverConfig           = $driverConfig
        Commit                 = (& git -C $repoRoot rev-parse HEAD 2>$null)
        DirtyTree              = ("$(git -C $repoRoot status --short 2>$null)".Trim() -ne "")

        # Build artifact (selected configuration)
        BuildArtifactPath      = $buildArtPath
        BuildArtifactSHA256    = $buildArtHash
        BuildArtifactSize      = $buildArtSize

        # ImagePath (authoritative for what kernel loads on next boot/service start)
        ServiceImagePathRaw    = $imagePathRaw
        ServiceImagePathResolved = $imagePathRes
        ServiceImagePathSHA256 = $imagePathHash
        ServiceImagePathSize   = $imagePathSize
        ServiceImagePathDate   = $imagePathDate
        RegisteredOemInf       = $registeredOem

        # System32 copy (reference copy; NOT what the kernel loads when INF uses DIRID 13)
        System32Path           = $sys32Path
        System32SHA256         = $sys32Hash
        System32Size           = $sys32Size
        System32Date           = $sys32Date

        # Version resource
        DriverVersion          = $driverVersion

        # Loaded kernel image — cannot be verified without kernel debugger
        LoadedImageSHA256      = "UNVERIFIED -- kernel debugger or ETW module-load event required"

        # Identity classification
        Contract               = $contract
    }
    $rec | ConvertTo-Json | Out-File (Join-Path $BenchmarkDir "${cellId}_identity.json") -Encoding utf8

    # Console output
    $contractColor = if ($contract -eq "VERIFIED_FILE_MATCH") { "Green" }
                     elseif ($contract -eq "DRIVER_IDENTITY_MISMATCH") { "Red" }
                     else { "Yellow" }
    Write-Host "  Contract: $contract" -ForegroundColor $contractColor
    Write-Info "  Build artifact  SHA: $buildArtHash  ($driverConfig, $buildArtSize B)"
    Write-Info "  ImagePath       SHA: $imagePathHash  ($imagePathSize B)"
    Write-Info "  System32 copy   SHA: $sys32Hash  ($sys32Size B)"
    Write-Info "  Registered OEM INF : $registeredOem"
    Write-Info "  LoadedImage        : UNVERIFIED"

    if ($contract -eq "DRIVER_IDENTITY_MISMATCH") {
        Write-Warn ""
        Write-Warn "  DRIVER_IDENTITY_MISMATCH: tests will execute against a driver that"
        Write-Warn "  does NOT match the selected $driverConfig build artifact."
        Write-Warn "  Results for this cell CANNOT be attributed to the $driverConfig driver."
        Write-Warn "  Actual loaded driver: $imagePathHash (via $imagePathRes)"
        Write-Warn "  Expected            : $buildArtHash"
    } elseif ($contract -eq "DRIVER_IDENTITY_UNKNOWN") {
        Write-Warn "  Identity cannot be established. Results are UNATTRIBUTABLE."
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

# ─── Machine lock for the whole matrix ───────────────────────────────────────
# Builds, driver reinstalls and test runs below change what a HIL execution verifies; none may run beside another
# execution.  Children (Build-*, Install-Driver*, Run-Tests-CI) run inside this owner's context.
. (Join-Path $repoRoot 'tools\lib\HilLock.ps1')
$benchLock = $null
if (-not $DryRun) {
    $benchLock = Enter-AvbHilLock -Kind 'local' -Label 'Run-DriverBenchmark' -WaitSeconds 0
    if (-not $benchLock.Ok) { Write-Fail "Machine lock held by another execution ($($benchLock.Why)): not building, installing or testing."; exit 1 }
    if ($benchLock.PreviousOwner) { Write-Warn "Took over a stale machine lock: $($benchLock.PreviousOwner.why)" }
    Write-Info "Machine lock held: $(Get-AvbHilLockPath)"
}
try {

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

        foreach ($testExe in @($BenchmarkTests.Keys)) {
            $suite = $BenchmarkTests[$testExe]
            $logFile = Join-Path $cellLogDir ("${testExe}_$(Get-Date -Format 'HHmmss').log")
            $entryLogs = Join-Path $cellLogDir $testExe
            Write-Info "--- $testExe ($suite) ---"
            Write-Info "    Log: $logFile"

            if ($DryRun) {
                Write-Info "[DRY-RUN] Run-Tests-CI.ps1 -Suite $suite -Configuration $testConfig -Tests $testExe -LogsDir $entryLogs"
                $cellSummary.Tests[$testExe] = @{ ExitCode = 0; LogFile = $logFile }
                continue
            }

            # Already elevated and inside this script's machine lock (the child is a descendant of the owner).
            $inner = "Start-Transcript -Path '$logFile' -Force | Out-Null; " +
                     "`$_ec=0; try { & '$runCi' -Suite $suite -Configuration $testConfig -Tests @('$testExe') -LogsDir '$entryLogs' " +
                     "-ResultsJson '$(Join-Path $entryLogs 'results.json')'; " +
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

} finally {
    if ($benchLock -and $benchLock.Ok) { $null = Exit-AvbHilLock -Owner $benchLock.Owner }
}
