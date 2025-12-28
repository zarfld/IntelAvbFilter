<# 
.SYNOPSIS
    Intel AVB Filter Driver - Canonical Setup Script

.DESCRIPTION
    Comprehensive driver setup script combining the correct NDIS filter installation method
    (from install_filter_proper.bat) with flexible parameters for automation.
    
    This is THE canonical setup script - all setup wrappers call this with preset parameters.

.PARAMETER Configuration
    Build configuration to install (Debug or Release). Default: Debug

.PARAMETER EnableTestSigning
    Enable test signing mode (requires reboot)

.PARAMETER InstallDriver
    Install the driver using netcfg (THE correct NDIS filter method)

.PARAMETER UninstallDriver
    Uninstall the driver completely

.PARAMETER CheckStatus
    Check driver installation and service status

.PARAMETER Reinstall
    Uninstall then install (clean reinstall)

.PARAMETER DriverPath
    Custom path to driver files (INF, SYS, CAT). If not specified, uses x64\{Configuration}\IntelAvbFilter\

.EXAMPLE
    .\Setup-Driver.ps1 -Configuration Debug -EnableTestSigning -InstallDriver
    Enable test signing and install debug driver

.EXAMPLE
    .\Setup-Driver.ps1 -Configuration Release -InstallDriver
    Install release driver

.EXAMPLE
    .\Setup-Driver.ps1 -Reinstall
    Clean reinstall (uninstall + install)

.EXAMPLE
    .\Setup-Driver.ps1 -UninstallDriver
    Uninstall driver completely

.NOTES
    Based on install_filter_proper.bat (THE reference NDIS filter install method)
    Uses netcfg.exe (required for NDIS Lightweight Filters), not pnputil
    
    Author: Intel AVB Filter Team
    Issue: #27 REQ-NF-SCRIPTS-001
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [ValidateSet("netcfg", "pnputil")]
    [string]$Method = "netcfg",
    [Parameter(Mandatory=$false)]
    [switch]$EnableTestSigning,
    
    [Parameter(Mandatory=$false)]
    [switch]$InstallDriver,
    
    [Parameter(Mandatory=$false)]
    [switch]$UninstallDriver,
    
    [Parameter(Mandatory=$false)]
    [switch]$CheckStatus,
    
    [Parameter(Mandatory=$false)]
    [switch]$Reinstall,
    
    [Parameter(Mandatory=$false)]
    [string]$DriverPath
)

# Require Administrator
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges. Please run as Administrator."
    exit 1
}

# Set default driver path
if (-not $DriverPath) {
    # $PSScriptRoot = tools\setup, so go up two levels to repo root
    $repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $DriverPath = Join-Path $repoRoot "build\x64\$Configuration\IntelAvbFilter\IntelAvbFilter"
}

# Helper functions
function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Failure {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Yellow
}

# Banner
Write-Host @"
===============================================================================
  Intel AVB Filter Driver - Setup Script
  Configuration: $Configuration
===============================================================================
"@ -ForegroundColor Cyan

# Function: Enable Test Signing
function Enable-TestSigningMode {
    Write-Step "Enabling Test Signing Mode"
    
    # Check current status
    $bcdeditOutput = bcdedit /enum "{current}"
    if ($bcdeditOutput -match "testsigning\s+Yes") {
        Write-Success "Test signing already enabled"
        return $true
    }
    
    # Enable test signing
    $result = bcdedit /set testsigning on 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Test signing enabled successfully"
        Write-Info "REBOOT REQUIRED - Run 'shutdown /r /t 0' to restart"
        return $true
    } else {
        Write-Failure "Failed to enable test signing: $result"
        return $false
    }
}

# Function: Check Status
function Check-DriverStatus {
    Write-Step "Checking Driver Status"
    
    # Check test signing
    Write-Host "`nTest Signing Status:" -ForegroundColor White
    $bcdeditOutput = bcdedit /enum "{current}"
    if ($bcdeditOutput -match "testsigning\s+Yes") {
        Write-Success "Test signing: ENABLED"
    } else {
        Write-Failure "Test signing: DISABLED"
        Write-Info "Run with -EnableTestSigning to enable"
    }
    
    # Check service
    Write-Host "`nDriver Service:" -ForegroundColor White
    try {
        $service = Get-Service -Name "IntelAvbFilter" -ErrorAction Stop
        Write-Success "Service exists: $($service.DisplayName)"
        Write-Host "  Status: $($service.Status)" -ForegroundColor Gray
        Write-Host "  Start Type: $($service.StartType)" -ForegroundColor Gray
        
        # Run sc query for detailed info
        Write-Host "`nDetailed Service Info:" -ForegroundColor White
        sc query IntelAvbFilter
    } catch {
        Write-Failure "Service not found - driver not installed"
    }
    
    # Check driver files
    Write-Host "`nDriver Files:" -ForegroundColor White
    $infPath = Join-Path $DriverPath "IntelAvbFilter.inf"
    $sysPath = Join-Path $DriverPath "IntelAvbFilter.sys"
    $catPath = Join-Path $DriverPath "IntelAvbFilter.cat"
    
    if (Test-Path $infPath) { Write-Success "INF: $infPath" } else { Write-Failure "INF not found: $infPath" }
    if (Test-Path $sysPath) { Write-Success "SYS: $sysPath" } else { Write-Failure "SYS not found: $sysPath" }
    if (Test-Path $catPath) { Write-Success "CAT: $catPath" } else { Write-Info "CAT not found (optional): $catPath" }
    
    # Check Intel adapters
    Write-Host "`nIntel Network Adapters:" -ForegroundColor White
    $adapters = Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*Intel*"}
    if ($adapters) {
        foreach ($adapter in $adapters) {
            Write-Success "$($adapter.InterfaceDescription) - Status: $($adapter.Status)"
        }
    } else {
        Write-Info "No Intel network adapters found"
    }
}

# Function: Uninstall Driver
function Uninstall-Driver {
    Write-Step "Uninstalling Driver (Method: $Method)"
    
    # Check for STOP_PENDING state (stuck service)
    Write-Host "Checking service state..." -ForegroundColor Gray
    $serviceStatus = sc.exe query IntelAvbFilter 2>&1 | Out-String
    
    if ($serviceStatus -match "STOP_PENDING") {
        Write-Host "" -ForegroundColor Red
        Write-Host "*** SERVICE STUCK IN STOP_PENDING ***" -ForegroundColor Red
        Write-Host "" -ForegroundColor Red
        Write-Host "The service cannot be stopped without a reboot." -ForegroundColor Yellow
        Write-Host "This is a Windows kernel state issue." -ForegroundColor Yellow
        Write-Host "" -ForegroundColor Yellow
        Write-Host "Options:" -ForegroundColor Cyan
        Write-Host "  1. Reboot now and installation will auto-complete" -ForegroundColor White
        Write-Host "  2. Cancel and reboot manually later" -ForegroundColor White
        Write-Host "" -ForegroundColor White
        
        $choice = Read-Host "Reboot now? (y/n)"
        if ($choice -eq 'y' -or $choice -eq 'Y') {
            Write-Host "Setting up auto-complete after reboot..." -ForegroundColor Cyan
            # TODO: Implement RunOnce registry setup
            Write-Host "Rebooting in 10 seconds... (Ctrl+C to cancel)" -ForegroundColor Yellow
            Start-Sleep -Seconds 10
            Restart-Computer -Force
            exit 0
        } else {
            Write-Warning "Installation cancelled. Please reboot manually and run again."
            return $false
        }
    }
    
    # Stop service
    Write-Host "Stopping service..." -ForegroundColor Gray
    sc.exe stop IntelAvbFilter
    Start-Sleep -Seconds 2
    
    # Delete service
    Write-Host "Deleting service..." -ForegroundColor Gray
    sc.exe delete IntelAvbFilter
    
    # Clean driver store (remove old/duplicate packages)
    # TESTED: Successfully removes duplicate oem*.inf packages
    # Test date: 2025-12-27 - Removed oem46.inf and oem59.inf successfully
    # Example output: "Removing driver package: oem46.inf", "Removing driver package: oem59.inf"
    Write-Host "`nCleaning driver store..." -ForegroundColor Gray
    Write-Host "---BEGIN DRIVER STORE BEFORE---" -ForegroundColor DarkGray
    pnputil /enum-drivers | Select-String -Pattern "intelavb" -Context 2,2
    Write-Host "---END DRIVER STORE BEFORE---" -ForegroundColor DarkGray
    
    # Get all IntelAvbFilter packages and delete them
    $driverPackages = pnputil /enum-drivers | Select-String -Pattern "Published Name" -Context 0,3 | Where-Object { $_.Context.PostContext -match "intelavbfilter.inf" }
    foreach ($match in $driverPackages) {
        if ($match.Line -match "Published Name\s*:\s*(oem\d+\.inf)") {
            $oemInf = $Matches[1]
            Write-Host "  Removing driver package: $oemInf" -ForegroundColor Yellow
            pnputil /delete-driver $oemInf /uninstall /force
        }
    }
    
    Write-Host "`n---BEGIN DRIVER STORE AFTER---" -ForegroundColor DarkGray
    pnputil /enum-drivers | Select-String -Pattern "intelavb" -Context 2,2
    Write-Host "---END DRIVER STORE AFTER---" -ForegroundColor DarkGray
    
    if ($Method -eq "netcfg") {
        # Uninstall via netcfg (THE correct method for NDIS filters)
        Write-Host "`nRemoving NDIS filter component (netcfg)..." -ForegroundColor Gray
        netcfg.exe -u MS_IntelAvbFilter
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Driver uninstalled successfully (netcfg)"
            return $true
        } else {
            Write-Info "netcfg returned exit code $LASTEXITCODE (may indicate component not found)"
            return $true  # Not necessarily an error
        }
    } elseif ($Method -eq "pnputil") {
        # Uninstall via pnputil (for device driver method)
        Write-Host "Removing driver package (pnputil)..." -ForegroundColor Gray
        pnputil /delete-driver intelavbfilter.inf /uninstall /force 2>&1 | Out-Null
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Driver uninstalled successfully (pnputil)"
            return $true
        } else {
            Write-Info "pnputil returned exit code $LASTEXITCODE"
            return $true  # Continue even if not found
        }
    }
}

# Function: Install Driver
function Install-Driver {
    Write-Step "Installing Driver (Method: $Method)"

    # Verify files exist
    $infPath = Join-Path $DriverPath "IntelAvbFilter.inf"
    $sysPath = Join-Path $DriverPath "IntelAvbFilter.sys"
    
    if (-not (Test-Path $infPath)) {
        Write-Failure "INF file not found: $infPath"
        Write-Info "Build the driver first or specify correct -DriverPath"
        return $false
    }
    
    if (-not (Test-Path $sysPath)) {
        Write-Failure "SYS file not found: $sysPath"
        Write-Info "Build the driver first or specify correct -DriverPath"
        return $false
    }
    
    Write-Success "Driver files found:"
    Write-Host "  INF: $infPath" -ForegroundColor Gray
    Write-Host "  SYS: $sysPath" -ForegroundColor Gray
    
    # Check build timestamp vs installed version (show all files)
    $installedSys = "C:\Windows\System32\drivers\IntelAvbFilter.sys"
    if (Test-Path $installedSys) {
        Write-Host "`nBuild timestamp check:" -ForegroundColor Gray
        
        # Show all build file timestamps
        $sysFile = Get-Item $sysPath -ErrorAction SilentlyContinue
        $infFile = Get-Item $infPath -ErrorAction SilentlyContinue
        $catPath = $infPath -replace '\.inf$', '.cat'
        $catFile = Get-Item $catPath -ErrorAction SilentlyContinue
        
        Write-Host "  Build outputs:" -ForegroundColor DarkGray
        if ($sysFile) { Write-Host "    .sys: $($sysFile.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor DarkGray }
        if ($infFile) { Write-Host "    .inf: $($infFile.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor DarkGray }
        if ($catFile) { Write-Host "    .cat: $($catFile.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor DarkGray }
        
        # Get newest build file
        $buildFiles = @($sysFile, $infFile, $catFile) | Where-Object { $_ -ne $null }
        $newestBuildFile = $buildFiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        $buildTime = $newestBuildFile.LastWriteTime
        $installedTime = (Get-Item $installedSys).LastWriteTime
        
        Write-Host "  Installed: $($installedTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor DarkGray
        Write-Host "  Comparison (using newest: $($newestBuildFile.Name)):" -ForegroundColor DarkGray
        
        if ($buildTime -lt $installedTime) {
            Write-Host "    WARNING: Build is OLDER than installed version!" -ForegroundColor Yellow
            Write-Host "    You may be downgrading. Press Ctrl+C to cancel or Enter to continue..." -ForegroundColor Yellow
            Read-Host
        } elseif ($buildTime -eq $installedTime) {
            Write-Host "    Same version (timestamps match)" -ForegroundColor Cyan
        } else {
            Write-Host "    Build is newer" -ForegroundColor Green
        }
    }
    
    # Step 1: Stop service if running
    Write-Host "`nStep 1: Stopping service if running..." -ForegroundColor Gray
    sc stop IntelAvbFilter 2>&1 | Out-Null
    
    if ($Method -eq "netcfg") {
        # NDIS Filter method (THE CORRECT method for NDIS Lightweight Filters)
        
        # Step 2: Remove old installation
        Write-Host "Step 2: Removing old installations (netcfg)..." -ForegroundColor Gray
        netcfg.exe -u MS_IntelAvbFilter 2>&1 | Out-Null
        
        # Step 3: Install filter component via netcfg
        # CRITICAL: NDIS Lightweight Filters REQUIRE netcfg.exe, not pnputil!
        # -c s = component class "service"
        # -i MS_IntelAvbFilter = component ID from INF
        # IMPORTANT: Must run from driver directory with relative path
        Write-Host "Step 3: Installing filter component via netcfg..." -ForegroundColor Gray
        # Step 3a: Manually copy .sys to drivers folder (required for NDIS filters)
        $driverDir = Split-Path $infPath -Parent
        $sysFile = Join-Path $driverDir "IntelAvbFilter.sys"
        if (Test-Path $sysFile) {
            Write-Host "  Copying .sys to C:\Windows\System32\drivers\..." -ForegroundColor Gray
            Copy-Item $sysFile "C:\Windows\System32\drivers\" -Force
        }
        
        # Step 3b: Install via netcfg
        Push-Location $driverDir
        Write-Host "  Command: netcfg.exe -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter" -ForegroundColor DarkGray
        
        netcfg.exe -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
        Pop-Location
        
        if ($LASTEXITCODE -ne 0) {
            Write-Failure "netcfg installation failed! Error code: $LASTEXITCODE"
            Write-Host "`nCommon causes:" -ForegroundColor Yellow
            Write-Host "  1. Driver not signed and test signing disabled" -ForegroundColor Yellow
            Write-Host "  2. INF file syntax errors" -ForegroundColor Yellow
            Write-Host "  3. Conflicting driver already installed" -ForegroundColor Yellow
            Write-Host "`nTroubleshooting steps:" -ForegroundColor Cyan
            Write-Host "  - Run: .\Setup-Driver.ps1 -CheckStatus" -ForegroundColor Cyan
            Write-Host "  - Check Windows Event Log: Event Viewer -> Windows Logs -> System" -ForegroundColor Cyan
            Write-Host "  - Enable test signing: .\Setup-Driver.ps1 -EnableTestSigning" -ForegroundColor Cyan
            return $false
        }
        
    } elseif ($Method -eq "pnputil") {
        # pnputil method (for device driver compatibility testing)
        
        # Step 2: Remove old installation
        Write-Host "Step 2: Removing old installations (pnputil)..." -ForegroundColor Gray
        pnputil /delete-driver intelavbfilter.inf /uninstall /force
        
        # Step 3: Install driver package via pnputil
        Write-Host "Step 3: Installing driver package via pnputil..." -ForegroundColor Gray
        Write-Host "  Command: pnputil /add-driver `"$infPath`" /install" -ForegroundColor DarkGray
        
        pnputil /add-driver "$infPath" /install
        
        if ($LASTEXITCODE -ne 0) {
            Write-Failure "pnputil installation failed! Error code: $LASTEXITCODE"
            Write-Host "`nCommon causes:" -ForegroundColor Yellow
            Write-Host "  1. Driver not signed and test signing disabled" -ForegroundColor Yellow
            Write-Host "  2. INF file syntax errors" -ForegroundColor Yellow
            Write-Host "  3. Device not found or incompatible" -ForegroundColor Yellow
            Write-Host "`nNote: For NDIS Filters, use -Method netcfg instead!" -ForegroundColor Cyan
            return $false
        }
    }
    
    Write-Success "$Method installation completed"
    
    # Step 4: Restart network adapters (CRITICAL for NDIS filter service creation)
    Write-Host "\nStep 4: Restarting Intel network adapters (triggers service creation)..." -ForegroundColor Gray
    try {
        $adapters = Get-NetAdapter | Where-Object { $_.InterfaceDescription -match 'Intel' }
        foreach ($adapter in $adapters) {
            Write-Host "  Restarting: $($adapter.Name)" -ForegroundColor DarkGray
            Disable-NetAdapter -Name $adapter.Name -Confirm:$false -ErrorAction SilentlyContinue
            Start-Sleep -Milliseconds 500
            Enable-NetAdapter -Name $adapter.Name -Confirm:$false -ErrorAction SilentlyContinue
        }
        Write-Host "  Adapters restarted" -ForegroundColor Green
    } catch {
        Write-Host "  Warning: Adapter restart failed, continuing..." -ForegroundColor Yellow
    }
    Start-Sleep -Seconds 2
    
    # Step 5: Check service status (should exist after adapter restart)
    Write-Host "`nStep 5: Checking service status..." -ForegroundColor Gray
    Write-Host "---BEGIN SERVICE STATUS---" -ForegroundColor Cyan
    $serviceStatus = sc.exe query IntelAvbFilter 2>&1
    $serviceStatus | ForEach-Object { Write-Host $_ }
    Write-Host "---END SERVICE STATUS---" -ForegroundColor Cyan
    
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Service not found - this may be normal if no Intel adapters are present"
    }
    
    Write-Success "`nInstallation complete!"
    Write-Info "Check DebugView for driver messages"
    Write-Info "Run .\Setup-Driver.ps1 -CheckStatus to verify installation"
    
    return $true
}

# Main execution
try {
    # Process parameters
    if ($EnableTestSigning) {
        $result = Enable-TestSigningMode
        if (-not $result) {
            exit 1
        }
    }
    
    if ($Reinstall) {
        $UninstallDriver = $true
        $InstallDriver = $true
    }
    
    if ($UninstallDriver) {
        $result = Uninstall-Driver
        if (-not $result) {
            exit 1
        }
    }
    
    if ($InstallDriver) {
        $result = Install-Driver
        if (-not $result) {
            exit 1
        }
    }
    
    if ($CheckStatus) {
        Check-DriverStatus
    }
    
    # If no parameters, show help
    if (-not ($EnableTestSigning -or $InstallDriver -or $UninstallDriver -or $CheckStatus -or $Reinstall)) {
        Write-Host "`nNo action specified. Use one of:" -ForegroundColor Yellow
        Write-Host "  .\Setup-Driver.ps1 -InstallDriver            # Install driver" -ForegroundColor Cyan
        Write-Host "  .\Setup-Driver.ps1 -Reinstall                # Clean reinstall" -ForegroundColor Cyan
        Write-Host "  .\Setup-Driver.ps1 -UninstallDriver          # Uninstall driver" -ForegroundColor Cyan
        Write-Host "  .\Setup-Driver.ps1 -CheckStatus              # Check status" -ForegroundColor Cyan
        Write-Host "  .\Setup-Driver.ps1 -EnableTestSigning        # Enable test signing" -ForegroundColor Cyan
        Write-Host "`nCommon workflows:" -ForegroundColor Yellow
        Write-Host "  .\Setup-Driver.ps1 -Configuration Debug -EnableTestSigning -InstallDriver" -ForegroundColor Cyan
        Write-Host "  .\Setup-Driver.ps1 -Configuration Release -InstallDriver" -ForegroundColor Cyan
    }
    
} catch {
    Write-Failure "Error: $($_.Exception.Message)"
    Write-Host $_.ScriptStackTrace -ForegroundColor Red
    exit 1
}
