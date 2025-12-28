param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,
    
    [Parameter(Mandatory=$true)]
    [ValidateSet('InstallDriver', 'Reinstall', 'UninstallDriver')]
    [string]$Action
)

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
