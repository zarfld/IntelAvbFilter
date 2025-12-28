<#
.SYNOPSIS
    Intel AVB Filter - Certificate Management Script

.DESCRIPTION
    Exports and installs test certificates for driver development.
    Supports extracting certificates from:
    - Signed driver files (.sys)
    - Certificate store (PrivateCertStore)
    - Direct .cer files

.PARAMETER Method
    Certificate extraction method:
    - ExtractFromDriver: Extract from signed .sys file (most common)
    - PrivateCertStore: Export from user's PrivateCertStore
    - File: Use existing .cer file

.PARAMETER DriverPath
    Path to signed driver (.sys) file when using ExtractFromDriver method

.PARAMETER CertFile
    Path to existing .cer file when using File method

.PARAMETER Configuration
    Build configuration (Debug/Release) - used to auto-locate driver

.PARAMETER InstallToTrustedPublisher
    Also install certificate to TrustedPublisher store (required for some scenarios)

.EXAMPLE
    .\Install-Certificate.ps1 -Method ExtractFromDriver -Configuration Debug
    Extract certificate from Debug driver and install

.EXAMPLE
    .\Install-Certificate.ps1 -Method ExtractFromDriver -DriverPath "build\x64\Release\IntelAvbFilter\IntelAvbFilter.sys"
    Extract from specific driver file

.EXAMPLE
    .\Install-Certificate.ps1 -Method PrivateCertStore
    Export certificate from PrivateCertStore and install

.NOTES
    Author: Intel AVB Filter Team
    Issue: #27 REQ-NF-SCRIPTS-001
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("ExtractFromDriver", "PrivateCertStore", "File")]
    [string]$Method = "ExtractFromDriver",
    
    [Parameter(Mandatory=$false)]
    [string]$DriverPath,
    
    [Parameter(Mandatory=$false)]
    [string]$CertFile,
    
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [switch]$InstallToTrustedPublisher
)

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

# Calculate repository root
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# Banner
Write-Host @"
===============================================================================
  Intel AVB Filter - Certificate Installation
  Method: $Method
===============================================================================
"@ -ForegroundColor Cyan

# Determine certificate source and extract
$cert = $null
$certPath = Join-Path $env:TEMP "WDKTestCert.cer"

switch ($Method) {
    "ExtractFromDriver" {
        Write-Step "Extracting certificate from signed driver"
        
        # Auto-locate driver if not specified
        if (-not $DriverPath) {
            $DriverPath = Join-Path $repoRoot "build\x64\$Configuration\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys"
        }
        
        if (-not (Test-Path $DriverPath)) {
            Write-Failure "Driver file not found: $DriverPath"
            Write-Host "Build the driver first or specify correct -DriverPath" -ForegroundColor Yellow
            exit 1
        }
        
        Write-Host "  Driver: $DriverPath" -ForegroundColor Cyan
        
        try {
            $signature = Get-AuthenticodeSignature -FilePath $DriverPath
            $cert = $signature.SignerCertificate
            
            if (-not $cert) {
                Write-Failure "No certificate found in driver signature"
                Write-Host "Make sure the driver is signed with Build-And-Sign.ps1" -ForegroundColor Yellow
                exit 1
            }
            
            Write-Host "  Subject: $($cert.Subject)" -ForegroundColor Cyan
            Write-Host "  Thumbprint: $($cert.Thumbprint)" -ForegroundColor Cyan
            Write-Host "  Expires: $($cert.NotAfter)" -ForegroundColor Cyan
            
            # Export certificate
            $certBytes = $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
            [System.IO.File]::WriteAllBytes($certPath, $certBytes)
            Write-Success "Certificate extracted to: $certPath"
            
        } catch {
            Write-Failure "Failed to extract certificate: $($_.Exception.Message)"
            exit 1
        }
    }
    
    "PrivateCertStore" {
        Write-Step "Exporting certificate from PrivateCertStore"
        
        $certs = Get-ChildItem -Path "Cert:\CurrentUser\PrivateCertStore" | Where-Object { $_.Subject -match "Intel AVB Filter" }
        
        if ($certs.Count -eq 0) {
            Write-Failure "No Intel AVB Filter certificate found in PrivateCertStore"
            Write-Host "Try -Method ExtractFromDriver instead" -ForegroundColor Yellow
            exit 1
        }
        
        # Use newest certificate
        $cert = $certs | Sort-Object NotAfter -Descending | Select-Object -First 1
        
        Write-Host "  Subject: $($cert.Subject)" -ForegroundColor Cyan
        Write-Host "  Thumbprint: $($cert.Thumbprint)" -ForegroundColor Cyan
        Write-Host "  Expires: $($cert.NotAfter)" -ForegroundColor Cyan
        
        $cert | Export-Certificate -FilePath $certPath -Force | Out-Null
        Write-Success "Certificate exported to: $certPath"
    }
    
    "File" {
        Write-Step "Using existing certificate file"
        
        if (-not $CertFile) {
            Write-Failure "-CertFile parameter required when using File method"
            exit 1
        }
        
        if (-not (Test-Path $CertFile)) {
            Write-Failure "Certificate file not found: $CertFile"
            exit 1
        }
        
        $certPath = $CertFile
        Write-Host "  File: $certPath" -ForegroundColor Cyan
        Write-Success "Using existing certificate file"
    }
}

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Failure "This script requires Administrator privileges"
    Write-Host "`nTo install certificate, run as Administrator:" -ForegroundColor Yellow
    Write-Host "  Import-Certificate -FilePath '$certPath' -CertStoreLocation 'Cert:\LocalMachine\Root'" -ForegroundColor Cyan
    if ($InstallToTrustedPublisher) {
        Write-Host "  Import-Certificate -FilePath '$certPath' -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher'" -ForegroundColor Cyan
    }
    Write-Host "Or use GUI: certmgr.msc" -ForegroundColor Cyan
    exit 1
}

# Install certificate to Root store
Write-Step "Installing certificate to Trusted Root Certification Authorities"

try {
    Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" -ErrorAction Stop | Out-Null
    Write-Success "Certificate installed to Root store"
} catch {
    Write-Failure "Failed to install to Root store: $($_.Exception.Message)"
    Write-Host "`nManual installation via certutil:" -ForegroundColor Yellow
    Write-Host "  certutil -addstore Root `"$certPath`"" -ForegroundColor Cyan
    exit 1
}

# Optionally install to TrustedPublisher store
if ($InstallToTrustedPublisher) {
    Write-Step "Installing certificate to TrustedPublisher store"
    
    try {
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" -ErrorAction Stop | Out-Null
        Write-Success "Certificate installed to TrustedPublisher store"
    } catch {
        Write-Failure "Failed to install to TrustedPublisher: $($_.Exception.Message)"
        Write-Host "`nManual installation via certutil:" -ForegroundColor Yellow
        Write-Host "  certutil -addstore TrustedPublisher `"$certPath`"" -ForegroundColor Cyan
    }
}

# Summary
Write-Host "`n===============================================================================" -ForegroundColor Green
Write-Host "  Certificate Installation Complete!" -ForegroundColor Green
Write-Host "===============================================================================" -ForegroundColor Green
Write-Host "`nNext steps:" -ForegroundColor Yellow
Write-Host "1. Ensure test signing is enabled: bcdedit /set testsigning on" -ForegroundColor Cyan
Write-Host "2. Restart computer if test signing was just enabled" -ForegroundColor Cyan
Write-Host "3. Install driver: .\tools\setup\Install-Driver.ps1 -InstallDriver" -ForegroundColor Cyan
