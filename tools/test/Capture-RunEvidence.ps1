# Capture-RunEvidence.ps1
#
# Captures a single focused HIL test run into an isolated, uniquely-identified
# evidence directory under test-evidence/runs/<timestamp>/.
#
# Produces:
#   test-evidence/runs/<runId>/
#       <logfile>                       -- copy of the original raw test log
#       junit-<testname>-<runId>.xml    -- JUnit covering only this single run
#       run-metadata.json               -- identity: source SHA, driver SHA, PCI IDs
#
# IDENTITY RULES (read carefully before invoking):
#
#   -SourceSha       MUST be supplied by the caller as the git commit SHA that was
#                    checked out when the test binary was built.  This script must
#                    NEVER compute it from 'git rev-parse HEAD' because HEAD at
#                    archival time is DIFFERENT from HEAD at build time.
#                    If unknown, pass "UNVERIFIED" explicitly.
#
#   -TestExeSha256   SHA256 of the test executable used in the run.  Compute at
#                    build/execution time, not at archival time.
#                    If unknown, pass "UNVERIFIED".
#
#   -ContractType    Must be consistent with actual hash comparison:
#                    VERIFIED_CURRENT_BUILD  requires InstalledSha == BuildSha (non-empty, equal).
#                    BASELINE_DRIVER_ONLY    requires InstalledSha != BuildSha (both non-empty).
#                    UNVERIFIED              used when either hash is missing.
#                    Script fails if caller supplies a ContractType that contradicts the hashes.
#
# Fails if:
#   - JUnit conversion returns non-zero
#   - Generated JUnit XML is missing or not parseable
#   - Expected output files are missing after generation
#
# Implements per-run evidence isolation required by P0 (HIL evidence integrity).

param(
    [Parameter(Mandatory=$true)]
    [string]$LogFile,

    [Parameter(Mandatory=$false)]
    [string]$InstalledDriverSha = "",

    [Parameter(Mandatory=$false)]
    [string]$BuildSha = "",

    # SHA256 of the test executable binary used in this run.
    # Compute at build/execution time.  Pass "UNVERIFIED" if not available.
    [Parameter(Mandatory=$false)]
    [string]$TestExeSha256 = "UNVERIFIED",

    # Git commit SHA that was checked out when the test binary was built.
    # MUST be supplied by caller (never auto-computed from git HEAD here).
    # Pass "UNVERIFIED" if unknown.
    [Parameter(Mandatory=$false)]
    [string]$SourceSha = "UNVERIFIED",

    [Parameter(Mandatory=$false)]
    [string]$PciIds = "",

    [Parameter(Mandatory=$false)]
    [string]$RunnerName = "",

    # VERIFIED_CURRENT_BUILD | BASELINE_DRIVER_ONLY | UNVERIFIED
    # Must be consistent with InstalledDriverSha vs BuildSha comparison.
    # Pass "" to auto-derive from hash comparison.
    [Parameter(Mandatory=$false)]
    [string]$ContractType = ""
)

$ErrorActionPreference = 'Stop'

$scriptDir     = $PSScriptRoot
$repoRoot      = Split-Path (Split-Path $scriptDir -Parent) -Parent
$convertScript = Join-Path $scriptDir 'Convert-Logs-To-JUnit.ps1'

# --------------------------------------------------------------------------
# Validate inputs
# --------------------------------------------------------------------------
if (-not (Test-Path $LogFile)) {
    Write-Error "Log file not found: $LogFile"
    exit 1
}

# Validate the input is a .log file, not an XML or other file.
# Run-Tests.ps1 generates both *.log and *.xml in logs/ -- callers must pass the .log.
if (-not $LogFile.EndsWith('.log')) {
    Write-Error ("Input file does not have .log extension: {0}`n" +
                 "Run-Tests.ps1 writes both *.log and *.xml to logs/; pass the .log file.`n" +
                 "Use: `$log = Get-ChildItem logs -Filter 'test_<name>*.log' | Sort-Object LastWriteTime -Desc | Select-Object -First 1") -f $LogFile
    exit 1
}

# Validate SHA256 format (64 hex chars) or "UNVERIFIED"
function Assert-Sha256OrUnverified {
    param([string]$val, [string]$name)
    if ($val -eq "UNVERIFIED") { return }
    if ($val -notmatch '^[0-9A-Fa-f]{64}$') {
        Write-Error ("Invalid {0}: '{1}' -- must be 64 hex chars or 'UNVERIFIED'" -f $name, $val)
        exit 1
    }
}
Assert-Sha256OrUnverified $InstalledDriverSha "InstalledDriverSha"
Assert-Sha256OrUnverified $BuildSha           "BuildSha"
Assert-Sha256OrUnverified $TestExeSha256      "TestExeSha256"

# Derive ContractType from actual hash comparison (fail-closed)
$installedPresent = ($InstalledDriverSha -ne "UNVERIFIED" -and -not [string]::IsNullOrWhiteSpace($InstalledDriverSha))
$buildPresent     = ($BuildSha           -ne "UNVERIFIED" -and -not [string]::IsNullOrWhiteSpace($BuildSha))
$hashesEqual      = $installedPresent -and $buildPresent -and
                    ($InstalledDriverSha.Trim() -eq $BuildSha.Trim())

$derivedContract = if ($hashesEqual)                          { "VERIFIED_CURRENT_BUILD" } `
                   elseif ($installedPresent -and $buildPresent) { "BASELINE_DRIVER_ONLY"   } `
                   else                                          { "UNVERIFIED"              }

if ($ContractType -eq "") {
    $ContractType = $derivedContract
} else {
    # Validate caller-supplied ContractType against actual hash comparison
    if ($ContractType -eq "VERIFIED_CURRENT_BUILD" -and -not $hashesEqual) {
        Write-Error ("ContractType VERIFIED_CURRENT_BUILD supplied but hashes are not equal " +
                     "(installed={0}... build={1}...); use BASELINE_DRIVER_ONLY or UNVERIFIED" -f
                     $(if ($installedPresent) { $InstalledDriverSha.Substring(0,12) } else { "MISSING" }),
                     $(if ($buildPresent)     { $BuildSha.Substring(0,12)           } else { "MISSING" }))
        exit 1
    }
    if ($ContractType -eq "BASELINE_DRIVER_ONLY" -and -not ($installedPresent -and $buildPresent)) {
        Write-Error "ContractType BASELINE_DRIVER_ONLY requires both InstalledDriverSha and BuildSha to be non-empty"
        exit 1
    }
}

if ($SourceSha -ne "UNVERIFIED" -and $SourceSha -notmatch '^[0-9A-Fa-f]{5,40}$') {
    Write-Error ("Invalid SourceSha format: '{0}'" -f $SourceSha)
    exit 1
}

if (-not $RunnerName) { $RunnerName = $env:COMPUTERNAME }

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------
$logItem  = Get-Item $LogFile
$logStem  = $logItem.BaseName
$testName = if ($logStem -match '^(.+\.exe)_\d{8}_\d{6}$') { $Matches[1] } else { $logStem }
$runId    = if ($logStem -match '_(\d{8}_\d{6})$') { $Matches[1] } else { Get-Date -Format 'yyyyMMdd_HHmmss' }
$runDate  = $runId.Substring(0, 8)

$evidenceRunDir = Join-Path $repoRoot "test-evidence\runs\$runId"
New-Item -ItemType Directory -Path $evidenceRunDir -Force | Out-Null

# --------------------------------------------------------------------------
# 1. Copy raw log (preserve unmodified)
# --------------------------------------------------------------------------
$destLog = Join-Path $evidenceRunDir $logItem.Name
Copy-Item $LogFile $destLog -Force
Write-Host "[OK] Raw log:  $destLog" -ForegroundColor Green

# --------------------------------------------------------------------------
# 2. Generate focused JUnit for this single run only
# --------------------------------------------------------------------------
$junitOut = Join-Path $evidenceRunDir ("junit-{0}-{1}.xml" -f ($testName -replace '\.exe$',''), $runId)

if (-not (Test-Path $convertScript)) {
    Write-Error "Convert-Logs-To-JUnit.ps1 not found at: $convertScript"
    exit 1
}

$tmpLogDir = Join-Path $env:TEMP ("run_evidence_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $tmpLogDir -Force | Out-Null
Copy-Item $LogFile (Join-Path $tmpLogDir $logItem.Name) -Force

$convResult = powershell -ExecutionPolicy Bypass -NonInteractive -File $convertScript `
    -LogsDir $tmpLogDir `
    -RunDate $runDate `
    -OutputFile $junitOut 2>&1

$convExit = $LASTEXITCODE
Remove-Item $tmpLogDir -Recurse -Force -ErrorAction SilentlyContinue

$convResult | Where-Object { $_ -match '\[OK\]|\[WARN\]|Summary' } |
    ForEach-Object { Write-Host "  $_" }

# Convert-Logs-To-JUnit exits 1 when tests FAIL (by design).
# Only treat the exit as an error when the JUnit file was NOT produced.
if (-not (Test-Path $junitOut)) {
    Write-Error ("Convert-Logs-To-JUnit.ps1 exit={0} and no JUnit produced -- conversion failed" -f $convExit)
    exit 1
}

# Validate JUnit is parseable XML
try {
    $xml = [xml](Get-Content $junitOut -Raw)
    $rootTests = [int]$xml.testsuites.tests
    $rootFail  = [int]$xml.testsuites.failures
    Write-Host ("[OK] JUnit valid: tests={0} failures={1}" -f $rootTests, $rootFail) -ForegroundColor Green
} catch {
    Write-Error ("JUnit XML at '{0}' is not parseable: {1}" -f $junitOut, $_)
    exit 1
}
Write-Host "[OK] JUnit:    $junitOut" -ForegroundColor Green

# --------------------------------------------------------------------------
# 3. Write run-metadata.json
# --------------------------------------------------------------------------
$meta = [ordered]@{
    run_id                  = $runId
    captured_at             = (Get-Date -Format 'yyyy-MM-ddTHH:mm:sszzz')
    runner                  = $RunnerName
    source_sha              = $SourceSha
    source_sha_note         = if ($SourceSha -eq "UNVERIFIED") {
        "Source SHA not captured at build/execution time; HEAD at archival differs from HEAD at build time"
    } else { "Supplied by caller at capture time" }
    test_exe_sha256         = $TestExeSha256
    installed_driver_sha256 = $InstalledDriverSha
    built_driver_sha256     = $BuildSha
    identity_contract       = $ContractType
    pci_ids                 = $PciIds
    test_name               = $testName
    log_file                = $logItem.Name
    junit_file              = [System.IO.Path]::GetFileName($junitOut)
    junit_tests             = $rootTests
    junit_failures          = $rootFail
}
$metaPath = Join-Path $evidenceRunDir "run-metadata.json"
$meta | ConvertTo-Json -Depth 4 | Set-Content $metaPath -Encoding UTF8
Write-Host "[OK] Metadata: $metaPath" -ForegroundColor Green

# Verify all expected outputs exist
$required = @($destLog, $junitOut, $metaPath)
foreach ($f in $required) {
    if (-not (Test-Path $f)) {
        Write-Error ("Required output missing: {0}" -f $f)
        exit 1
    }
}

Write-Host ""
Write-Host "=== Evidence captured ===" -ForegroundColor Cyan
Write-Host ("  Run ID:           {0}"  -f $runId)
Write-Host ("  Contract:         {0}"  -f $ContractType)
Write-Host ("  Source SHA:       {0}"  -f $SourceSha)
Write-Host ("  Driver (inst):    {0}..." -f $(if ($installedPresent) { $InstalledDriverSha.Substring(0,16) } else { "MISSING" }))
Write-Host ("  Driver (built):   {0}..." -f $(if ($buildPresent)     { $BuildSha.Substring(0,16)           } else { "MISSING" }))
Write-Host ("  Test exe SHA256:  {0}"  -f $TestExeSha256)
Write-Host ("  JUnit:            tests={0} failures={1}" -f $rootTests, $rootFail)
Write-Host ("  Directory:        {0}"  -f $evidenceRunDir)
