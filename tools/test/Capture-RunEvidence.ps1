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
# Usage:
#   .\tools\test\Capture-RunEvidence.ps1 -LogFile logs\test_tx_timestamp_provenance.exe_20261012_194857.log
#   .\tools\test\Capture-RunEvidence.ps1 -LogFile <path> -InstalledDriverSha <sha> -BuildSha <sha>
#
# The caller supplies identity hashes explicitly; they are NOT computed inside
# this script to avoid inadvertent modifications or re-computation from stale files.
#
# Implements per-run evidence isolation required by P0 (HIL evidence integrity).

param(
    [Parameter(Mandatory=$true)]
    [string]$LogFile,

    [Parameter(Mandatory=$false)]
    [string]$InstalledDriverSha = "",

    [Parameter(Mandatory=$false)]
    [string]$BuildSha = "",

    [Parameter(Mandatory=$false)]
    [string]$SourceSha = "",

    [Parameter(Mandatory=$false)]
    [string]$PciIds = "",           # e.g. "8086:1533 8086:125B 8086:15B7"

    [Parameter(Mandatory=$false)]
    [string]$RunnerName = "",

    [Parameter(Mandatory=$false)]
    [string]$ContractType = ""      # VERIFIED_CURRENT_BUILD | BASELINE_DRIVER_ONLY | UNVERIFIED
)

$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
$repoRoot  = Split-Path (Split-Path $scriptDir -Parent) -Parent
$convertScript = Join-Path $scriptDir 'Convert-Logs-To-JUnit.ps1'

if (-not (Test-Path $LogFile)) {
    Write-Error "Log file not found: $LogFile"
    exit 1
}

$logItem   = Get-Item $LogFile
$logStem   = $logItem.BaseName   # e.g. test_tx_timestamp_provenance.exe_20261012_194857
$testName  = if ($logStem -match '^(.+\.exe)_\d{8}_\d{6}$') { $Matches[1] } else { $logStem }

# Extract run timestamp from log filename or use now
$runId = if ($logStem -match '_(\d{8}_\d{6})$') { $Matches[1] } else { Get-Date -Format 'yyyyMMdd_HHmmss' }
$runDate = $runId.Substring(0, 8)

# Determine identity if not supplied
if (-not $SourceSha) {
    try { $SourceSha = (& git rev-parse HEAD 2>$null).Trim() } catch {}
}
if (-not $InstalledDriverSha) {
    $drvPath = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
    if (Test-Path $drvPath) {
        $InstalledDriverSha = (Get-FileHash $drvPath -Algorithm SHA256).Hash
    }
}
if (-not $BuildSha) {
    $builtShaFile = Join-Path $repoRoot "build\driver-sha256.txt"
    if (Test-Path $builtShaFile) {
        # The file may contain "<SHA>  <path>" (Get-FileHash format); take only the hash.
        $BuildSha = (Get-Content $builtShaFile -Raw).Trim() -replace '\s+.*$',''
    }
}
if (-not $ContractType) {
    $iPresent = -not [string]::IsNullOrWhiteSpace($InstalledDriverSha)
    $bPresent = -not [string]::IsNullOrWhiteSpace($BuildSha)
    $ContractType = if ($iPresent -and $bPresent -and $InstalledDriverSha.Trim() -eq $BuildSha.Trim()) {
        "VERIFIED_CURRENT_BUILD"
    } elseif ($iPresent -and $bPresent) {
        "BASELINE_DRIVER_ONLY"
    } else { "UNVERIFIED" }
}
if (-not $RunnerName) { $RunnerName = $env:COMPUTERNAME }

# Create isolated evidence directory
$evidenceRunDir = Join-Path $repoRoot "test-evidence\runs\$runId"
New-Item -ItemType Directory -Path $evidenceRunDir -Force | Out-Null

# 1. Copy raw log
$destLog = Join-Path $evidenceRunDir $logItem.Name
Copy-Item $LogFile $destLog -Force
Write-Host "[OK] Raw log:  $destLog" -ForegroundColor Green

# 2. Generate focused JUnit for this single run only
$junitOut = Join-Path $evidenceRunDir ("junit-{0}-{1}.xml" -f ($testName -replace '\.exe$',''), $runId)

if (Test-Path $convertScript) {
    # Create a temp log dir with only this log so Convert-Logs-To-JUnit picks it up exclusively
    $tmpLogDir = Join-Path $env:TEMP ("run_evidence_{0}" -f (Get-Random))
    New-Item -ItemType Directory -Path $tmpLogDir -Force | Out-Null
    Copy-Item $LogFile (Join-Path $tmpLogDir $logItem.Name) -Force

    powershell -ExecutionPolicy Bypass -NonInteractive -File $convertScript `
        -LogsDir $tmpLogDir `
        -RunDate $runDate `
        -OutputFile $junitOut 2>&1 | Where-Object { $_ -match '\[OK\]|\[WARN\]|Summary' } | ForEach-Object { Write-Host "  $_" }

    Remove-Item $tmpLogDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "[OK] JUnit:    $junitOut" -ForegroundColor Green
} else {
    Write-Warning "Convert-Logs-To-JUnit.ps1 not found; JUnit not generated"
}

# 3. Write run-metadata.json
$meta = [ordered]@{
    run_id              = $runId
    captured_at         = (Get-Date -Format 'yyyy-MM-ddTHH:mm:sszzz')
    runner              = $RunnerName
    source_sha          = $SourceSha
    installed_driver_sha256 = $InstalledDriverSha
    built_driver_sha256 = $BuildSha
    identity_contract   = $ContractType
    pci_ids             = $PciIds
    test_name           = $testName
    log_file            = $logItem.Name
    junit_file          = [System.IO.Path]::GetFileName($junitOut)
}
$metaPath = Join-Path $evidenceRunDir "run-metadata.json"
$meta | ConvertTo-Json -Depth 4 | Set-Content $metaPath -Encoding UTF8
Write-Host "[OK] Metadata: $metaPath" -ForegroundColor Green

Write-Host ""
Write-Host "=== Evidence captured ===" -ForegroundColor Cyan
Write-Host "  Run ID:           $runId"
Write-Host "  Contract:         $ContractType"
Write-Host "  Source SHA:       $SourceSha"
Write-Host "  Installed driver: $($InstalledDriverSha.Substring(0,[Math]::Min(16,$InstalledDriverSha.Length)))..."
Write-Host "  Directory:        $evidenceRunDir"
