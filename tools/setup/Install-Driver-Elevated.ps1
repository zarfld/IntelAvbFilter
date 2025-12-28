param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,
    
    [Parameter(Mandatory=$true)]
    [ValidateSet('InstallDriver', 'Reinstall', 'UninstallDriver')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'

try {
    $scriptPath = Join-Path $PSScriptRoot 'Install-Driver.ps1'

$arguments = @(
    '-NoProfile'
    '-ExecutionPolicy'
    'Bypass'
    '-File'
    $scriptPath
    '-Configuration'
    $Configuration
    "-$Action"
)

    Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait

} catch {
    Write-Host "ERROR: Failed to launch elevated script" -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Yellow
    exit 1
}
