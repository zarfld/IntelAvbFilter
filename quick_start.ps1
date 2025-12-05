# Intel AVB Filter Driver - Quick Start Guide
# Run this script to get step-by-step installation instructions

Write-Host @"
??????????????????????????????????????????????????????????????????
?                                                                ?
?         Intel AVB Filter Driver - Quick Start Guide           ?
?                                                                ?
??????????????????????????????????????????????????????????????????

"@ -ForegroundColor Cyan

Write-Host "This guide will help you install the Intel AVB Filter Driver." -ForegroundColor White
Write-Host ""

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "??  WARNING: You are NOT running as Administrator!" -ForegroundColor Yellow
    Write-Host "   This driver requires Administrator privileges to install." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To run as Administrator:" -ForegroundColor Cyan
    Write-Host "  1. Right-click on PowerShell" -ForegroundColor White
    Write-Host "  2. Select 'Run as Administrator'" -ForegroundColor White
    Write-Host "  3. Navigate back to this directory and run this script again" -ForegroundColor White
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "? Running as Administrator" -ForegroundColor Green
Write-Host ""

# Detect current status
Write-Host "Detecting current system status..." -ForegroundColor Cyan
Write-Host ""

# Check test signing
$testSigningEnabled = $false
$bcdeditOutput = bcdedit /enum "{current}"
if ($bcdeditOutput -match "testsigning\s+Yes") {
    $testSigningEnabled = $true
    Write-Host "? Test signing: ENABLED" -ForegroundColor Green
} else {
    Write-Host "? Test signing: DISABLED" -ForegroundColor Red
}

# Check certificates
$certsInstalled = $false
$certs = Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" -ErrorAction SilentlyContinue | 
    Where-Object {$_.Subject -like "*IntelAvbFilter*"}
if ($certs) {
    $certsInstalled = $true
    Write-Host "? Certificates: INSTALLED" -ForegroundColor Green
} else {
    Write-Host "? Certificates: NOT INSTALLED" -ForegroundColor Red
}

# Check driver installation
$driverInstalled = $false
$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if ($service) {
    $driverInstalled = $true
    Write-Host "? Driver: INSTALLED ($($service.Status))" -ForegroundColor Green
} else {
    Write-Host "? Driver: NOT INSTALLED" -ForegroundColor Red
}

# Check Intel hardware
$intelAdapters = Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*Intel*"}
if ($intelAdapters) {
    Write-Host "? Intel adapters: DETECTED ($($intelAdapters.Count) adapter(s))" -ForegroundColor Green
} else {
    Write-Host "? Intel adapters: NONE DETECTED" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "????????????????????????????????????????????????????????????????" -ForegroundColor Cyan
Write-Host ""

# Provide recommendations based on status
if ($driverInstalled) {
    Write-Host "?? Driver is already installed!" -ForegroundColor Green
    Write-Host ""
    Write-Host "To test the driver:" -ForegroundColor Cyan
    Write-Host "  cd tools\avb_test\x64\Debug" -ForegroundColor White
    Write-Host "  .\avb_test.exe" -ForegroundColor White
    Write-Host ""
    Write-Host "To uninstall:" -ForegroundColor Cyan
    Write-Host "  .\setup_driver.ps1 -UninstallDriver" -ForegroundColor White
    Write-Host ""
    
} elseif (-not $testSigningEnabled) {
    Write-Host "?? INSTALLATION STEPS:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Step 1: Enable Test Signing (REQUIRES REBOOT)" -ForegroundColor Cyan
    Write-Host "  Command: .\setup_driver.ps1 -EnableTestSigning" -ForegroundColor White
    Write-Host "  Then:    shutdown /r /t 0" -ForegroundColor White
    Write-Host ""
    Write-Host "Step 2: After reboot, run this script again to continue" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "??  Note: Your desktop will show 'Test Mode' watermark after reboot." -ForegroundColor Yellow
    Write-Host "   This is normal for driver development." -ForegroundColor Yellow
    Write-Host ""
    
    $response = Read-Host "Enable test signing now? (y/n)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Host ""
        Write-Host "Enabling test signing..." -ForegroundColor Cyan
        $result = bcdedit /set testsigning on
        if ($LASTEXITCODE -eq 0) {
            Write-Host "? Test signing enabled successfully!" -ForegroundColor Green
            Write-Host ""
            Write-Host "?? REBOOT REQUIRED" -ForegroundColor Yellow
            Write-Host "   Run this command to reboot:" -ForegroundColor Yellow
            Write-Host "   shutdown /r /t 0" -ForegroundColor White
            Write-Host ""
            Write-Host "   After reboot, run this script again to install the driver." -ForegroundColor Yellow
        } else {
            Write-Host "? Failed to enable test signing: $result" -ForegroundColor Red
        }
    }
    
} elseif (-not $certsInstalled) {
    Write-Host "?? INSTALLATION STEPS:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Step 1: Fix Certificate Installation" -ForegroundColor Cyan
    Write-Host "  Command: .\troubleshoot_certificates.ps1 -FixCertificates" -ForegroundColor White
    Write-Host ""
    Write-Host "Step 2: Install the Driver" -ForegroundColor Cyan
    Write-Host "  Command: .\setup_driver.ps1 -InstallDriver" -ForegroundColor White
    Write-Host ""
    
    $response = Read-Host "Fix certificates now? (y/n)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Host ""
        Write-Host "Running certificate fix..." -ForegroundColor Cyan
        & "$PSScriptRoot\troubleshoot_certificates.ps1" -FixCertificates
        
        Write-Host ""
        $response2 = Read-Host "Install driver now? (y/n)"
        if ($response2 -eq 'y' -or $response2 -eq 'Y') {
            Write-Host ""
            Write-Host "Installing driver..." -ForegroundColor Cyan
            & "$PSScriptRoot\setup_driver.ps1" -InstallDriver
        }
    }
    
} else {
    Write-Host "?? INSTALLATION STEP:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Install the Driver" -ForegroundColor Cyan
    Write-Host "  Command: .\setup_driver.ps1 -InstallDriver" -ForegroundColor White
    Write-Host ""
    
    $response = Read-Host "Install driver now? (y/n)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Host ""
        Write-Host "Installing driver..." -ForegroundColor Cyan
        & "$PSScriptRoot\setup_driver.ps1" -InstallDriver
    }
}

Write-Host ""
Write-Host "????????????????????????????????????????????????????????????????" -ForegroundColor Cyan
Write-Host ""
Write-Host "For detailed documentation:" -ForegroundColor Cyan
Write-Host "  • Installation Guide: INSTALLATION_GUIDE.md" -ForegroundColor White
Write-Host "  • Main README: README.md" -ForegroundColor White
Write-Host "  • Troubleshooting: .\troubleshoot_certificates.ps1" -ForegroundColor White
Write-Host ""
Write-Host "For help and issues:" -ForegroundColor Cyan
Write-Host "  • GitHub: https://github.com/zarfld/IntelAvbFilter/issues" -ForegroundColor White
Write-Host ""

Read-Host "Press Enter to exit"
