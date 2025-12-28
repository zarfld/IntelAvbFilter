# Run comprehensive_ioctl_test.exe as Administrator
# This ensures proper access to the kernel driver device interface

if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
{
    Write-Host "Restarting as Administrator..." -ForegroundColor Yellow
    Start-Process powershell.exe "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

Write-Host "Running comprehensive IOCTL test as Administrator..." -ForegroundColor Green
Write-Host ""

$testExe = Join-Path $PSScriptRoot "comprehensive_ioctl_test.exe"

if (Test-Path $testExe) {
    & $testExe
    Write-Host ""
    Write-Host "Press any key to exit..." -ForegroundColor Cyan
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
} else {
    Write-Host "ERROR: comprehensive_ioctl_test.exe not found!" -ForegroundColor Red
    Write-Host "Build it first with the test compilation task." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Press any key to exit..." -ForegroundColor Cyan
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}
