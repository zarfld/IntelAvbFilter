param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [Parameter(Mandatory=$false)]
    [switch]$Full,
    
    [Parameter(Mandatory=$false)]
    [string]$TestName,
    
    [Parameter(Mandatory=$false)]
    [string]$TestExecutable
)

$scriptPath = Join-Path $PSScriptRoot 'Run-Tests.ps1'

# Build command to run script and then pause
$command = "& '$scriptPath' -Configuration $Configuration"
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
$command += "; Write-Host ''; Write-Host 'Press any key to close...' -ForegroundColor Yellow; `$null = `$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')"

$arguments = @(
    '-NoProfile'
    '-ExecutionPolicy'
    'Bypass'
    '-Command'
    $command
)

Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait
