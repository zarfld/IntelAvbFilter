# Reboot and Test Driver
# This script configures the driver to start at boot and reboots

Write-Host "=== Preparing Driver for Boot-Time Load ===" -ForegroundColor Cyan
Write-Host ""

# Check admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "? ERROR: Must run as Administrator" -ForegroundColor Red
    exit 1
}

Write-Host "Configuring driver for boot-time load..." -ForegroundColor Yellow
sc.exe config IntelAvbFilter start= boot

Write-Host ""
Write-Host "Driver is now configured to load at system boot." -ForegroundColor Green
Write-Host ""
Write-Host "IMPORTANT: After reboot, run this command to test:" -ForegroundColor Yellow
Write-Host "  .\avb_test_i226.exe selftest" -ForegroundColor White
Write-Host ""
Write-Host "Or check DebugView (download from Microsoft Sysinternals):" -ForegroundColor Yellow  
Write-Host "  1. Download DebugView64.exe" -ForegroundColor Cyan
Write-Host "  2. Run as Administrator" -ForegroundColor Cyan
Write-Host "  3. Enable Capture > Capture Kernel" -ForegroundColor Cyan
Write-Host "  4. Look for [IntelAvbFilter] messages" -ForegroundColor Cyan
Write-Host ""

$response = Read-Host "Reboot now? (y/n)"
if ($response -eq 'y' -or $response -eq 'Y') {
    Write-Host ""
    Write-Host "Rebooting in 5 seconds..." -ForegroundColor Yellow
    shutdown /r /t 5 /c "Rebooting to load Intel AVB Filter Driver"
} else {
    Write-Host ""
    Write-Host "Please reboot manually when ready." -ForegroundColor Yellow
}
