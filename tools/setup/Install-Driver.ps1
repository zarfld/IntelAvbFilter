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
    # Package directory created by WDK with signed driver and catalog
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
    
    # Unregister ETW manifest before stopping the service
    # Implements: #65 (REQ-F-EVENT-LOG-001)
    $installedMan = "C:\Windows\System32\drivers\IntelAvbFilter.man"
    if (Test-Path $installedMan) {
        Write-Host "Unregistering ETW manifest (wevtutil um)..." -ForegroundColor Gray
        wevtutil um $installedMan 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Success "ETW manifest unregistered"
        } else {
            Write-Info "wevtutil um returned $LASTEXITCODE (may be OK if not previously registered)"
        }
        Remove-Item $installedMan -Force -ErrorAction SilentlyContinue
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
        # CRITICAL: Disable ALL adapters that have MS_IntelAvbFilter bound FIRST,
        # so NDIS fully unbinds the LWF and releases the .sys image from kernel memory
        # BEFORE netcfg -u runs. Only disabling "Intel" adapters is wrong -- the LWF
        # binds to every ethernet adapter regardless of vendor.
        Write-Host "`nDisabling all adapters bound to MS_IntelAvbFilter (force NDIS LWF unbind)..." -ForegroundColor Gray
        $script:adaptersDisabledByUninstall = @()
        try {
            # Get every adapter that has the filter bound (Enabled or not)
            $boundAdapterNames = @(Get-NetAdapterBinding -ComponentID 'MS_IntelAvbFilter' -ErrorAction SilentlyContinue |
                Where-Object { $_.Enabled } | Select-Object -ExpandProperty Name)

            if ($boundAdapterNames.Count -eq 0) {
                # Fallback: filter not registered yet, disable all ethernet adapters (any state)
                Write-Host "  (MS_IntelAvbFilter binding not found - disabling all ethernet adapters)" -ForegroundColor DarkGray
                $script:adaptersDisabledByUninstall = @(Get-NetAdapter |
                    Where-Object { $_.MediaType -eq '802.3' -and $_.Status -ne 'Disabled' })
            } else {
                $script:adaptersDisabledByUninstall = @(Get-NetAdapter |
                    Where-Object { $boundAdapterNames -contains $_.Name -and $_.Status -ne 'Disabled' })
            }

            foreach ($a in $script:adaptersDisabledByUninstall) {
                Write-Host "  Disabling: $($a.Name)" -ForegroundColor DarkGray
                Disable-NetAdapter -Name $a.Name -Confirm:$false -ErrorAction SilentlyContinue
            }
            if ($script:adaptersDisabledByUninstall.Count -gt 0) { Start-Sleep -Seconds 3 }
        } catch {
            Write-Host "  Warning: adapter disable failed, continuing..." -ForegroundColor Yellow
        }

        Write-Host "`nRemoving NDIS filter component (netcfg)..." -ForegroundColor Gray
        netcfg.exe -u MS_IntelAvbFilter
        
        # CRITICAL: Manually clean DriverStore FileRepository cache
        # pnputil doesn't properly delete cached NDIS filter drivers
        # This is THE fix for "old driver stuck in memory" (#192-211 issues)
        Write-Host "`nCleaning DriverStore FileRepository cache..." -ForegroundColor Gray
        $driverStoreBase = "C:\Windows\System32\DriverStore\FileRepository"
        $cachedDrivers = Get-ChildItem $driverStoreBase -Filter "intelavbfilter.inf_*" -ErrorAction SilentlyContinue
        
        if ($cachedDrivers) {
            foreach ($folder in $cachedDrivers) {
                $sysFile = Join-Path $folder.FullName "IntelAvbFilter.sys"
                if (Test-Path $sysFile) {
                    $info = Get-Item $sysFile
                    Write-Host "  Deleting cached driver: $($folder.Name) ($($info.Length) bytes)" -ForegroundColor Yellow
                }
                Remove-Item $folder.FullName -Recurse -Force -ErrorAction SilentlyContinue
            }
            Write-Success "DriverStore cache cleaned"
        } else {
            Write-Host "  No cached drivers found (already clean)" -ForegroundColor Gray
        }
        
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
    
    # Declared before the netcfg/pnputil branch so adapters can always be re-enabled
    # from any exit point (success, failure, or unhandled exception).
    $adaptersToReEnable = @()

    if ($Method -eq "netcfg") {
        # NDIS Filter method (THE CORRECT method for NDIS Lightweight Filters)
        
        # Step 2: Adapters were already disabled in Uninstall-Driver (script:adaptersDisabledByUninstall).
        # If Install-Driver is called standalone (no prior uninstall), disable them here.
        Write-Host "Step 2: Ensuring Intel adapters are disabled (NDIS unbind)..." -ForegroundColor Gray
        if (-not $script:adaptersDisabledByUninstall) {
            $adaptersToReEnable = @()
            try {
                $boundAdapterNames = @(Get-NetAdapterBinding -ComponentID 'MS_IntelAvbFilter' -ErrorAction SilentlyContinue |
                    Where-Object { $_.Enabled } | Select-Object -ExpandProperty Name)
                $adaptersToReEnable = if ($boundAdapterNames.Count -gt 0) {
                    @(Get-NetAdapter | Where-Object { $boundAdapterNames -contains $_.Name -and $_.Status -eq 'Up' })
                } else {
                    @(Get-NetAdapter | Where-Object { $_.MediaType -eq '802.3' -and $_.Status -eq 'Up' })
                }
                foreach ($a in $adaptersToReEnable) {
                    Write-Host "  Disabling: $($a.Name)" -ForegroundColor DarkGray
                    Disable-NetAdapter -Name $a.Name -Confirm:$false -ErrorAction SilentlyContinue
                }
                if ($adaptersToReEnable.Count -gt 0) { Start-Sleep -Seconds 3 }
            } catch {
                Write-Host "  Warning: adapter disable failed, continuing..." -ForegroundColor Yellow
            }
        } else {
            Write-Host "  Already disabled by uninstall step." -ForegroundColor DarkGray
            $adaptersToReEnable = $script:adaptersDisabledByUninstall
        }

        Write-Host "Step 2b: Removing old NDIS filter component (netcfg)..." -ForegroundColor Gray
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
        $installedSys  = "C:\Windows\System32\drivers\IntelAvbFilter.sys"
        if (Test-Path $sysFile) {
            Write-Host "  Copying .sys to C:\Windows\System32\drivers\..." -ForegroundColor Gray
            # The kernel holds a FILE_SHARE_DELETE image-section lock on the loaded .sys.
            # We cannot overwrite it, but we CAN rename it out of the way, then copy
            # the new file in under the original name.
            # Use a timestamp-based backup name so a previously-locked .sys.old never
            # blocks the rename (the old backup stays on disk until next reboot cleans it up).
            $oldSysBak = "C:\Windows\System32\drivers\IntelAvbFilter.sys.old"
            try {
                if (Test-Path $installedSys) {
                    # If a previous backup still exists (kernel-locked from last install),
                    # rotate it to a timestamp name so our rename always has a free destination.
                    if (Test-Path $oldSysBak) {
                        $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
                        $rotated = "C:\Windows\System32\drivers\IntelAvbFilter.sys.bak.$stamp"
                        Rename-Item $oldSysBak $rotated -Force -ErrorAction SilentlyContinue
                        # If the rotate also failed (still locked) just leave it - we continue
                        # and the primary rename of .sys -> .sys.old may still succeed because
                        # the source (.sys) has FILE_SHARE_DELETE and the destination is now free.
                    }
                    Rename-Item $installedSys $oldSysBak -Force -ErrorAction Stop
                    Write-Host "  Renamed locked .sys to .sys.old (will be deleted after reboot)" -ForegroundColor DarkGray
                }
                Copy-Item $sysFile $installedSys -Force -ErrorAction Stop
                Write-Host "  .sys copied successfully" -ForegroundColor DarkGray
            } catch {
                # Rename also failed - fall back to scheduling replacement on next reboot
                Write-Host "  WARNING: Cannot replace locked .sys directly. Scheduling for reboot..." -ForegroundColor Yellow
                $tempSys = "C:\Windows\System32\drivers\IntelAvbFilter.sys.new"
                Copy-Item $sysFile $tempSys -Force
                $regKey = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager"
                $existing = (Get-ItemProperty $regKey "PendingFileRenameOperations" -ErrorAction SilentlyContinue).PendingFileRenameOperations
                $ops = [string[]]@(
                    "\??\$installedSys", "",           # delete the old locked file on reboot
                    "\??\$tempSys", "\??\$installedSys" # rename .new -> .sys on reboot
                )
                Set-ItemProperty $regKey "PendingFileRenameOperations" -Value ($existing + $ops) -Type MultiString
                Write-Host "  [REBOOT REQUIRED] New driver will be activated after next reboot." -ForegroundColor Yellow
            }
        }

        # Step 3a.5: Register ETW manifest (Windows Event Log / System channel)
        # Implements: #65 (REQ-F-EVENT-LOG-001), Closes: #269 (TEST-EVENT-LOG-001)
        $manFile       = Join-Path $driverDir "IntelAvbFilter.man"
        $installedMan  = "C:\Windows\System32\drivers\IntelAvbFilter.man"
        if (Test-Path $manFile) {
            Write-Host "  Copying ETW manifest to C:\Windows\System32\drivers\..." -ForegroundColor Gray
            Copy-Item $manFile "C:\Windows\System32\drivers\" -Force

            # ETW REGISTRATION (Closes #269 TC-1):
            # wevtutil im runs BEFORE netcfg -l so the WINEVT\Publishers registry key is
            # ready before the driver loads.  The EventLog service restart (which fires the
            # McGenControlCallbackV2 enable callback) is done AFTER netcfg -l (see Step 4b
            # below), once the driver IS loaded and EtwRegister has been called.  Restarting
            # EventLog before driver load risks the "pending enable" not being delivered to
            # a kernel-mode provider that registers later.
            wevtutil um $installedMan 2>&1 | Out-Null  # Remove stale registration first
            icacls $installedSys /grant "NT SERVICE\EventLog:(R)" 2>&1 | Out-Null
            $wevtOut = wevtutil im $installedMan "/mf:$installedSys" "/rf:$installedSys" 2>&1
            if ($LASTEXITCODE -eq 0) {
                Write-Success "ETW provider manifest registered (WINEVT registry updated)"
            } else {
                Write-Info "wevtutil im returned $LASTEXITCODE : $wevtOut"
            }
        } else {
            Write-Info "ETW manifest not found at $manFile - skipping ETW registration"
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
            # SAFETY: adapters were disabled in Step 2 -- always re-enable before returning
            Write-Host "`n[SAFETY] Re-enabling Intel adapters after failed install..." -ForegroundColor Yellow
            foreach ($a in $adaptersToReEnable) {
                Enable-NetAdapter -Name $a.Name -Confirm:$false -ErrorAction SilentlyContinue
            }
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

    # Step 3c: Verify ETW registration (already registered in step 3a.5 before netcfg).
    # Registration was moved pre-netcfg so EventLog subscription is live before DriverEntry fires.
    if (Test-Path $installedMan) {
        Write-Host "`nStep 3c: Verifying ETW provider registration..." -ForegroundColor Gray
        $gpOut = wevtutil gp IntelAvbFilter 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Success "ETW provider active: IntelAvbFilter events routing to System log"
        } else {
            # Fallback: re-register if somehow missing (should not happen)
            Write-Info "ETW provider not found, re-registering..."
            icacls $installedSys /grant "NT SERVICE\EventLog:(R)" 2>&1 | Out-Null
            wevtutil im $installedMan "/mf:$installedSys" "/rf:$installedSys" 2>&1 | Out-Null
        }
    }

    # Step 4: Re-enable Intel adapters (triggers NDIS filter bind)
    Write-Host "`nStep 4: Re-enabling Intel network adapters (triggers NDIS filter bind)..." -ForegroundColor Gray
    # Use whichever list captured the disabled adapters (uninstall-time preferred)
    $adaptersToEnable = if ($script:adaptersDisabledByUninstall) {
        $script:adaptersDisabledByUninstall
    } elseif ($adaptersToReEnable) {
        $adaptersToReEnable
    } else {
        @(Get-NetAdapter | Where-Object { $_.InterfaceDescription -match 'Intel' })
    }
    try {
        foreach ($adapter in $adaptersToEnable) {
            Write-Host "  Enabling: $($adapter.Name)" -ForegroundColor DarkGray
            Enable-NetAdapter -Name $adapter.Name -Confirm:$false -ErrorAction SilentlyContinue
        }
        Write-Host "  Adapters enabled" -ForegroundColor Green
    } catch {
        Write-Host "  Warning: Adapter enable failed, continuing..." -ForegroundColor Yellow
    }
    Start-Sleep -Seconds 2

    # Step 4b: Restart EventLog service AFTER driver is loaded and EtwRegister has been called.
    # ETW SUBSCRIPTION FIX (Closes #269 TC-1..TC-4, TC-7, TC-9..TC-11):
    # When EventLog restarts, it calls EtwEnableTraceEx2 for every WINEVT\Publishers GUID.
    # Because the driver IS NOW loaded (EtwRegister called in DriverEntry), the kernel
    # fires McGenControlCallbackV2(EVENT_CONTROL_CODE_ENABLE_PROVIDER) SYNCHRONOUSLY during
    # the EtwEnableTraceEx2 call.  This sets IntelAvbFilterEnableBits[0] so EtwWriteTransfer
    # delivers events to Application.evtx.  Doing this BEFORE driver load risks the
    # "pending enable" not being re-delivered to a just-registered kernel-mode provider.
    if (Test-Path $installedMan) {
        Write-Host "`nStep 4b: Restarting EventLog service (ETW subscription callback)..." -ForegroundColor Gray
        Stop-Service -Name EventLog -Force -ErrorAction SilentlyContinue
        Start-Service -Name EventLog -ErrorAction SilentlyContinue
        Write-Host "  Waiting 3s for McGenControlCallbackV2 to set IntelAvbFilterEnableBits..." -ForegroundColor DarkGray
        Start-Sleep -Seconds 3
        Write-Success "EventLog restarted - ETW subscription callback delivered to loaded driver"
    }

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
