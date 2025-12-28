param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [Parameter(Mandatory=$false)]
    [switch]$Full
)

$scriptPath = Join-Path $PSScriptRoot 'Run-Tests.ps1'

# Build command to run script and then pause
$command = "& '$scriptPath' -Configuration $Configuration"
if ($Full) {
    $command += " -Full"
}
$command += "; Write-Host ''; Write-Host 'Press any key to close...' -ForegroundColor Yellow; `$null = `$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')"

$arguments = @(
    '-NoProfile'
    '-ExecutionPolicy'
    'Bypass'
    '-Command'
    $command
)

Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait
