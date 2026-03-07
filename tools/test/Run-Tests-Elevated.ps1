param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [Parameter(Mandatory=$false)]
    [switch]$Full,
    
    [Parameter(Mandatory=$false)]
    [string]$TestName,
    
    [Parameter(Mandatory=$false)]
    [string]$TestExecutable,
    
    [Parameter(Mandatory=$false)]
    [string]$LogFile
)

$scriptPath = Join-Path $PSScriptRoot 'Run-Tests.ps1'

# Default log file if not specified - use logs directory
if (-not $LogFile) {
    if ($TestName -or $Full) {
        $repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
        $logsDir = Join-Path $repoRoot "logs"
        if (-not (Test-Path $logsDir)) {
            New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
        }
        $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
        
        if ($TestName) {
            $LogFile = Join-Path $logsDir "$TestName`_$timestamp.log"
        } elseif ($Full) {
            $LogFile = Join-Path $logsDir "full_test_suite_$timestamp.log"
        }
    }
}

# Build command to run script with transcript logging
if ($LogFile) {
    $command = "Start-Transcript -Path '$LogFile' -Force; "
} else {
    $command = ""
}
$command += "& '$scriptPath' -Configuration $Configuration"
if ($Full) {
    $command += " -Full"
}
if ($TestName) {
    # Ensure .exe extension for test name
    $testExe = if ($TestName -notmatch '\.exe$') { "$TestName.exe" } else { $TestName }
    $command += " -TestExecutable '$testExe'"
}
if ($TestExecutable) {
    $command += " -TestExecutable '$TestExecutable'"
}
# Always collect logs when logging to file
if ($LogFile) {
    $command += " -CollectLogs"
}
if ($LogFile) {
    $command += "; Stop-Transcript"
}
$command += "; Write-Host ''; Write-Host 'Press any key to close...' -ForegroundColor Yellow; `$null = `$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')"

$arguments = @(
    '-NoProfile'
    '-ExecutionPolicy'
    'Bypass'
    '-Command'
    $command
)

if ($LogFile) {
    Write-Host "Output will be logged to: $LogFile" -ForegroundColor Cyan
}

Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait

if ($LogFile -and (Test-Path $LogFile)) {
    Write-Host "`nLog file created: $LogFile" -ForegroundColor Green
}
