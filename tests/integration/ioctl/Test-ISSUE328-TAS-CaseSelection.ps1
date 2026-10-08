# Test-ISSUE328-TAS-CaseSelection.ps1
# Relates to: #328
#
# Purpose:
#   Prove that test_ioctl_tas.exe can run a single TC-TAS case in isolation.
#   This supports deterministic predecessor narrowing for the #328 bad-suite
#   investigation without conflating multiple TAS state transitions in one run.
#
# Run:
#   .\tests\integration\ioctl\Test-ISSUE328-TAS-CaseSelection.ps1
#   .\tests\integration\ioctl\Test-ISSUE328-TAS-CaseSelection.ps1 -SelectedCase TC-TAS-009

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidatePattern('^TC-TAS-\d{3}$')]
    [string]$SelectedCase = 'TC-TAS-009'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$runner = Join-Path $repoRoot 'tools\test\Run-Tests-Elevated.ps1'
$logsDir = Join-Path $repoRoot 'logs'
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$logFile = Join-Path $logsDir "issue328_tas_case_selection_$($SelectedCase)_$timestamp.log"

if (-not (Test-Path $runner)) {
    Write-Host "FAIL  TC-ISSUE328-TAS-SELECT-001: Runner not found at $runner" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
}

Write-Host "=== TC-ISSUE328-TAS-SELECT-001: Single TAS testcase selection ===" -ForegroundColor Cyan
Write-Host "Selected case: $SelectedCase" -ForegroundColor Gray

& $runner -Configuration $Configuration `
          -TestName 'test_ioctl_tas.exe' `
          -TestArgs "--case $SelectedCase" `
          -LogFile $logFile

if (-not (Test-Path $logFile)) {
    Write-Host "FAIL  TC-ISSUE328-TAS-SELECT-001: Expected log file was not created: $logFile" -ForegroundColor Red
    exit 1
}

$logContent = Get-Content -Path $logFile -Raw
$caseMatches = [regex]::Matches($logContent, '\[TC-TAS-\d{3}\]') |
    ForEach-Object { $_.Value.Trim('[', ']') } |
    Select-Object -Unique

$selectedPresent = $caseMatches -contains $SelectedCase
$unexpectedCases = @($caseMatches | Where-Object { $_ -ne $SelectedCase })
$totalLine = Select-String -Path $logFile -Pattern ' TOTAL:\s+\d+' | Select-Object -First 1

if (-not $selectedPresent) {
    Write-Host "FAIL  TC-ISSUE328-TAS-SELECT-001: Selected case $SelectedCase did not appear in output" -ForegroundColor Red
    Write-Host "  Log: $logFile" -ForegroundColor Yellow
    exit 1
}

if ($unexpectedCases.Count -gt 0) {
    Write-Host "FAIL  TC-ISSUE328-TAS-SELECT-001: Unexpected TAS cases also ran: $($unexpectedCases -join ', ')" -ForegroundColor Red
    Write-Host "  Log: $logFile" -ForegroundColor Yellow
    exit 1
}

if (-not $totalLine -or $totalLine.Matches.Count -eq 0 -or $totalLine.Line -notmatch 'TOTAL:\s+1\b') {
    $observed = if ($totalLine) { $totalLine.Line.Trim() } else { '<missing>' }
    Write-Host "FAIL  TC-ISSUE328-TAS-SELECT-001: Expected TOTAL: 1, observed: $observed" -ForegroundColor Red
    Write-Host "  Log: $logFile" -ForegroundColor Yellow
    exit 1
}

Write-Host "PASS  TC-ISSUE328-TAS-SELECT-001: Only $SelectedCase ran" -ForegroundColor Green
Write-Host "  Log: $logFile" -ForegroundColor Gray
exit 0
