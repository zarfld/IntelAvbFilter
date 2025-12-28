<#
.SYNOPSIS
    Intel AVB Filter Driver - Clean Installation Script

.DESCRIPTION
    CANONICAL clean install script - consolidates:
    - IntelAvbFilter-Cleanup.ps1 (full cleanup with OEM removal)
    - reinstall_debug_quick.bat (quick reinstall)
    
    Performs complete cleanup before fresh installation:
    - Unbind from all adapters
    - Remove all OEM driver packages
    - Clean registry entries
    - Fresh install with test signing support
    
.PARAMETER Configuration
    Build configuration (Debug or Release). Default: Debug

.PARAMETER AllAdapters
    Unbind from all adapters instead of specific adapter

.PARAMETER EnableTestSigning
    Enable test signing mode (requires reboot)

.PARAMETER CertPath
    Path to test certificate to import

.EXAMPLE
    .\Clean-Install-Driver.ps1 -Configuration Debug
    Clean install Debug driver

.EXAMPLE
    .\Clean-Install-Driver.ps1 -Configuration Release -AllAdapters -EnableTestSigning
    Clean install Release with test signing on all adapters

.NOTES
    This is the most thorough installation method
    Use when standard install fails or for fresh start
#>

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    
    [switch]$AllAdapters,
    [switch]$EnableTestSigning,
    [string]$CertPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Intel AVB Filter - Clean Installation" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check admin
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: Administrator privileges required" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as administrator'" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

# Navigate to repository root
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\setup$') {
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
    Set-Location $RepoRoot
}

# Determine paths
$infPath = "x64\$Configuration\IntelAvbFilter\IntelAvbFilter.inf"
if (-not (Test-Path $infPath)) {
    Write-Host "ERROR: Driver INF not found at $infPath" -ForegroundColor Red
    Write-Host "Build the driver first!" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "[1/7] Unbinding filter from adapters..." -ForegroundColor Yellow
if ($AllAdapters) {
    Write-Host "    Unbinding from ALL adapters" -ForegroundColor Gray
} else {
    Write-Host "    Unbinding from default adapter" -ForegroundColor Gray
}
netcfg -u MS_IntelAvbFilter 2>&1 | Out-Null
Start-Sleep -Seconds 2
Write-Host "    ✓ Filter unbound" -ForegroundColor Green

Write-Host "[2/7] Stopping service..." -ForegroundColor Yellow
sc.exe stop IntelAvbFilter 2>&1 | Out-Null
Start-Sleep -Seconds 2
Write-Host "    ✓ Service stopped" -ForegroundColor Green

Write-Host "[3/7] Removing ALL OEM driver packages..." -ForegroundColor Yellow
# Find all OEM packages matching intelavbfilter.inf
$oemPackages = pnputil /enum-drivers | Select-String -Pattern "oem\d+\.inf" -Context 0,2 | 
    Where-Object { $_.Context.PostContext -match "intelavbfilter\.inf" } |
    ForEach-Object { ($_ -split '\s+')[0] }

$removedCount = 0
foreach ($oemInf in $oemPackages) {
    Write-Host "    Removing $oemInf..." -ForegroundColor Gray
    pnputil /delete-driver $oemInf /uninstall /force 2>&1 | Out-Null
    $removedCount++
}

if ($removedCount -gt 0) {
    Write-Host "    ✓ Removed $removedCount OEM package(s)" -ForegroundColor Green
} else {
    Write-Host "    ℹ No existing OEM packages found" -ForegroundColor Gray
}

Start-Sleep -Seconds 2

Write-Host "[4/7] Cleaning driver store..." -ForegroundColor Yellow
$driverStore = Get-ChildItem "C:\Windows\System32\DriverStore\FileRepository" -Filter "intelavbfilter*" -Directory -ErrorAction SilentlyContinue
foreach ($dir in $driverStore) {
    Write-Host "    Removing: $($dir.Name)" -ForegroundColor Gray
    Remove-Item $dir.FullName -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "    ✓ Driver store cleaned" -ForegroundColor Green

Write-Host "[5/7] Cleaning registry..." -ForegroundColor Yellow
$regPaths = @(
    "HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter",
    "HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter\Parameters"
)
foreach ($regPath in $regPaths) {
    if (Test-Path $regPath) {
        Write-Host "    Removing: $regPath" -ForegroundColor Gray
        Remove-Item $regPath -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Write-Host "    ✓ Registry cleaned" -ForegroundColor Green

# Enable test signing if requested
if ($EnableTestSigning) {
    Write-Host "[6/7] Enabling test signing..." -ForegroundColor Yellow
    bcdedit /set testsigning on | Out-Null
    Write-Host "    ✓ Test signing enabled (reboot required)" -ForegroundColor Green
    
    if ($CertPath -and (Test-Path $CertPath)) {
        Write-Host "    Installing certificate: $CertPath" -ForegroundColor Gray
        certutil -addstore Root $CertPath | Out-Null
        certutil -addstore TrustedPublisher $CertPath | Out-Null
        Write-Host "    ✓ Certificate installed" -ForegroundColor Green
    }
} else {
    Write-Host "[6/7] Test signing not requested" -ForegroundColor Gray
}

Write-Host "[7/7] Installing fresh driver..." -ForegroundColor Yellow
$driverFile = Get-Item "x64\$Configuration\IntelAvbFilter\IntelAvbFilter.sys"
Write-Host "    Driver: $($driverFile.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) ($($driverFile.Length) bytes)" -ForegroundColor Gray

try {
    $result = netcfg -v -l $infPath -c s -i MS_IntelAvbFilter 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "    ✓ Driver installed and bound" -ForegroundColor Green
    } else {
        Write-Host "    ⚠ netcfg returned code $LASTEXITCODE" -ForegroundColor Yellow
    }
} catch {
    Write-Host "    ERROR: Installation failed: $_" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

Start-Sleep -Seconds 2

# Verify installation
Write-Host ""
Write-Host "Verifying installation..." -ForegroundColor Cyan
$service = Get-Service -Name IntelAvbFilter -ErrorAction SilentlyContinue
if ($service) {
    Write-Host "  ✓ Service exists: $($service.Status)" -ForegroundColor Green
    if ($service.Status -ne 'Running') {
        Write-Host "  ⚠ Starting service..." -ForegroundColor Yellow
        Start-Service -Name IntelAvbFilter
        Start-Sleep -Seconds 1
        $service = Get-Service -Name IntelAvbFilter
        Write-Host "  ✓ Service now: $($service.Status)" -ForegroundColor Green
    }
} else {
    Write-Host "  ✗ Service not found!" -ForegroundColor Red
}

# Check registry
$regKey = "HKLM:\SYSTEM\CurrentControlSet\Services\TCPIP6\Parameters\Wfp\NdisFilterDrivers\INTEL_AvbFilter"
if (Test-Path $regKey) {
    Write-Host "  ✓ Registry binding confirmed" -ForegroundColor Green
} else {
    Write-Host "  ⚠ Registry binding not found (may need adapter restart)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Clean installation complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($EnableTestSigning) {
    Write-Host "⚠ REBOOT REQUIRED for test signing to take effect!" -ForegroundColor Yellow
    Write-Host ""
}

Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Check DebugView for driver initialization" -ForegroundColor Gray
Write-Host "  2. Run test suite: tools\test\Run-Tests.ps1 -Configuration $Configuration" -ForegroundColor Gray
Write-Host ""
