# Intel AVB Filter Driver - Test Signing Setup Script
# Run this script as Administrator

param(
    [Parameter(Mandatory=$false)]
    [switch]$EnableTestSigning,
    
    [Parameter(Mandatory=$false)]
    [switch]$CreateCertificate,
    
    [Parameter(Mandatory=$false)]
    [switch]$InstallDriver,
    
    [Parameter(Mandatory=$false)]
    [switch]$UninstallDriver,
    
    [Parameter(Mandatory=$false)]
    [switch]$CheckStatus,
    
    [Parameter(Mandatory=$false)]
    [string]$DriverPath
)

# Set default driver path relative to repository root
if (-not $DriverPath) {
    $repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $DriverPath = Join-Path $repoRoot "x64\Debug\IntelAvbFilter"
}

# Require Administrator privileges
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges. Please run as Administrator."
    exit 1
}

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "? $Message" -ForegroundColor Green
}

function Write-Failure {
    param([string]$Message)
    Write-Host "? $Message" -ForegroundColor Red
}

function Write-Warning {
    param([string]$Message)
    Write-Host "? $Message" -ForegroundColor Yellow
}

function Enable-TestSigningMode {
    Write-Step "Enabling Test Signing Mode"
    
    $result = bcdedit /set testsigning on
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Test signing enabled successfully"
        Write-Warning "REBOOT REQUIRED - Run 'shutdown /r /t 0' to restart"
        return $true
    } else {
        Write-Failure "Failed to enable test signing: $result"
        return $false
    }
}

function Check-TestSigningStatus {
    Write-Step "Checking Test Signing Status"
    
    $bcdeditOutput = bcdedit /enum "{current}"
    if ($bcdeditOutput -match "testsigning\s+Yes") {
        Write-Success "Test signing is ENABLED"
        return $true
    } else {
        Write-Failure "Test signing is DISABLED"
        Write-Host "  Run with -EnableTestSigning to enable it" -ForegroundColor Yellow
        return $false
    }
}

function Create-SelfSignedCert {
    Write-Step "Creating Self-Signed Certificate"
    
    # Check if certificate already exists
    $existingCert = Get-ChildItem -Path "Cert:\CurrentUser\My" | Where-Object {$_.Subject -like "*IntelAvbFilter Development*"}
    if ($existingCert) {
        Write-Warning "Certificate already exists with thumbprint: $($existingCert.Thumbprint)"
        $overwrite = Read-Host "Do you want to create a new certificate? (y/n)"
        if ($overwrite -ne 'y') {
            return $existingCert
        }
    }
    
    # Create new certificate
    $cert = New-SelfSignedCertificate -Type CodeSigningCert `
        -Subject "CN=IntelAvbFilter Development" `
        -KeyUsage DigitalSignature `
        -FriendlyName "IntelAvbFilter Driver Certificate" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    
    if ($cert) {
        Write-Success "Certificate created successfully"
        Write-Host "  Subject: $($cert.Subject)"
        Write-Host "  Thumbprint: $($cert.Thumbprint)"
        Write-Host "  Valid From: $($cert.NotBefore)"
        Write-Host "  Valid To: $($cert.NotAfter)"
        
        # Export certificate
        $certPath = "$env:TEMP\IntelAvbFilter_Cert.cer"
        Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null
        Write-Success "Certificate exported to: $certPath"
        
        # Install to trusted stores
        Write-Step "Installing certificate to trusted stores"
        
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" -ErrorAction SilentlyContinue | Out-Null
        Write-Success "Certificate installed to Trusted Root Certification Authorities"
        
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" -ErrorAction SilentlyContinue | Out-Null
        Write-Success "Certificate installed to Trusted Publishers"
        
        return $cert
    } else {
        Write-Failure "Failed to create certificate"
        return $null
    }
}

function Install-IntelAvbDriver {
    param([string]$Path)
    
    Write-Step "Installing Intel AVB Filter Driver"
    
    # Verify path exists
    if (-not (Test-Path $Path)) {
        Write-Failure "Driver path not found: $Path"
        Write-Host "  Please build the driver first or specify correct path with -DriverPath" -ForegroundColor Yellow
        return $false
    }
    
    # Verify required files
    $infPath = Join-Path $Path "IntelAvbFilter.inf"
    $sysPath = Join-Path $Path "IntelAvbFilter.sys"
    $catPath = Join-Path $Path "IntelAvbFilter.cat"
    
    if (-not (Test-Path $infPath)) {
        Write-Failure "INF file not found: $infPath"
        return $false
    }
    
    if (-not (Test-Path $sysPath)) {
        Write-Failure "SYS file not found: $sysPath"
        return $false
    }
    
    Write-Host "  INF: $infPath" -ForegroundColor Gray
    Write-Host "  SYS: $sysPath" -ForegroundColor Gray
    Write-Host "  CAT: $catPath" -ForegroundColor Gray
    
    # Check test signing
    $testSigning = Check-TestSigningStatus
    if (-not $testSigning) {
        Write-Failure "Test signing is not enabled. Enable it first with -EnableTestSigning"
        return $false
    }
    
    # Install using netcfg (preferred for NDIS filter drivers)
    Write-Step "Installing driver using netcfg"
    $netcfgResult = netcfg -v -l "$infPath" -c s -i MS_IntelAvbFilter 2>&1
    
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Driver installed successfully"
        
        # Verify installation
        Start-Sleep -Seconds 2
        $service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
        if ($service) {
            Write-Success "Service status: $($service.Status)"
            
            # Check if device node exists
            $deviceExists = Test-Path "\\.\IntelAvbFilter" -ErrorAction SilentlyContinue
            if ($deviceExists) {
                Write-Success "Device node created: \\.\IntelAvbFilter"
            } else {
                Write-Warning "Device node not created (may require Intel hardware)"
            }
        } else {
            Write-Warning "Service not found in service list"
        }
        
        return $true
    } else {
        Write-Failure "Failed to install driver"
        Write-Host "  Output: $netcfgResult" -ForegroundColor Yellow
        
        # Try alternative method with pnputil
        Write-Step "Trying alternative installation with pnputil"
        $pnpResult = pnputil /add-driver "$infPath" /install 2>&1
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Driver installed successfully (using pnputil)"
            return $true
        } else {
            Write-Failure "Both installation methods failed"
            Write-Host "  pnputil output: $pnpResult" -ForegroundColor Yellow
            return $false
        }
    }
}

function Uninstall-IntelAvbDriver {
    Write-Step "Uninstalling Intel AVB Filter Driver"
    
    # Try netcfg first
    $netcfgResult = netcfg -v -u MS_IntelAvbFilter 2>&1
    
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Driver uninstalled successfully (netcfg)"
        return $true
    } else {
        Write-Warning "netcfg uninstall failed, trying pnputil"
        
        # Find and remove with pnputil
        $drivers = pnputil /enum-drivers | Select-String -Pattern "IntelAvbFilter" -Context 1,0
        
        if ($drivers) {
            foreach ($driver in $drivers) {
                $oemInf = ($driver.Line -split '\s+')[1]
                if ($oemInf) {
                    Write-Host "  Removing: $oemInf"
                    pnputil /delete-driver $oemInf /uninstall /force
                    
                    if ($LASTEXITCODE -eq 0) {
                        Write-Success "Driver uninstalled successfully (pnputil)"
                        return $true
                    }
                }
            }
        }
        
        Write-Failure "Failed to uninstall driver"
        return $false
    }
}

function Show-DriverStatus {
    Write-Step "Intel AVB Filter Driver Status"
    
    # Test signing status
    Write-Host "`n[Test Signing]" -ForegroundColor Cyan
    $bcdeditOutput = bcdedit /enum "{current}"
    if ($bcdeditOutput -match "testsigning\s+Yes") {
        Write-Success "Test signing: ENABLED"
    } else {
        Write-Failure "Test signing: DISABLED"
    }
    
    # Certificate status
    Write-Host "`n[Certificates]" -ForegroundColor Cyan
    $certs = Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {$_.Subject -like "*IntelAvbFilter*"}
    if ($certs) {
        Write-Success "Self-signed certificate installed: $($certs.Count) certificate(s)"
        foreach ($cert in $certs) {
            Write-Host "  Thumbprint: $($cert.Thumbprint)" -ForegroundColor Gray
            Write-Host "  Valid: $($cert.NotBefore) to $($cert.NotAfter)" -ForegroundColor Gray
        }
    } else {
        Write-Warning "No self-signed certificate found"
    }
    
    # Service status
    Write-Host "`n[Service Status]" -ForegroundColor Cyan
    $service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
    if ($service) {
        Write-Success "Service found: $($service.Status)"
        Write-Host "  Display Name: $($service.DisplayName)" -ForegroundColor Gray
        Write-Host "  Start Type: $($service.StartType)" -ForegroundColor Gray
    } else {
        Write-Warning "Service not installed"
    }
    
    # Device node status
    Write-Host "`n[Device Node]" -ForegroundColor Cyan
    try {
        $deviceExists = [System.IO.File]::Exists("\\.\IntelAvbFilter")
        if ($deviceExists) {
            Write-Success "Device node exists: \\.\IntelAvbFilter"
        } else {
            Write-Warning "Device node not found (may require Intel hardware)"
        }
    } catch {
        Write-Warning "Cannot check device node: $($_.Exception.Message)"
    }
    
    # Intel adapters
    Write-Host "`n[Intel Network Adapters]" -ForegroundColor Cyan
    $intelAdapters = Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*Intel*"}
    if ($intelAdapters) {
        Write-Success "Found $($intelAdapters.Count) Intel adapter(s):"
        foreach ($adapter in $intelAdapters) {
            Write-Host "  - $($adapter.InterfaceDescription)" -ForegroundColor Gray
            
            # Get hardware ID
            try {
                $device = Get-PnpDevice -InstanceId $adapter.PnPDeviceID -ErrorAction SilentlyContinue
                if ($device) {
                    Write-Host "    Hardware ID: $($device.HardwareID[0])" -ForegroundColor DarkGray
                }
            } catch {
                # Ignore errors
            }
        }
    } else {
        Write-Warning "No Intel network adapters found"
    }
}

# Main execution
Write-Host "Intel AVB Filter Driver - Test Signing Setup" -ForegroundColor Magenta
Write-Host "================================================" -ForegroundColor Magenta

if ($EnableTestSigning) {
    Enable-TestSigningMode
}

if ($CreateCertificate) {
    Create-SelfSignedCert
}

if ($InstallDriver) {
    Install-IntelAvbDriver -Path $DriverPath
}

if ($UninstallDriver) {
    Uninstall-IntelAvbDriver
}

if ($CheckStatus -or (-not $EnableTestSigning -and -not $CreateCertificate -and -not $InstallDriver -and -not $UninstallDriver)) {
    Show-DriverStatus
}

Write-Host "`n" -NoNewline

# Show usage if no parameters
if ($PSBoundParameters.Count -eq 0) {
    Write-Host "Usage Examples:" -ForegroundColor Yellow
    Write-Host "  .\setup_driver.ps1 -CheckStatus           # Check current status" -ForegroundColor Gray
    Write-Host "  .\setup_driver.ps1 -EnableTestSigning     # Enable test signing (requires reboot)" -ForegroundColor Gray
    Write-Host "  .\setup_driver.ps1 -CreateCertificate     # Create and install self-signed certificate" -ForegroundColor Gray
    Write-Host "  .\setup_driver.ps1 -InstallDriver         # Install the driver" -ForegroundColor Gray
    Write-Host "  .\setup_driver.ps1 -UninstallDriver       # Uninstall the driver" -ForegroundColor Gray
    Write-Host "`nFull setup sequence:" -ForegroundColor Yellow
    Write-Host "  1. .\setup_driver.ps1 -EnableTestSigning  # Enable test signing" -ForegroundColor Gray
    Write-Host "  2. shutdown /r /t 0                       # Reboot computer" -ForegroundColor Gray
    Write-Host "  3. .\setup_driver.ps1 -InstallDriver      # Install driver after reboot" -ForegroundColor Gray
}
