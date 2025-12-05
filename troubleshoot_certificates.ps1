# Intel AVB Filter Driver - Certificate Troubleshooting Script
# Run as Administrator to diagnose certificate and signing issues

param(
    [Parameter(Mandatory=$false)]
    [switch]$FixCertificates,
    
    [Parameter(Mandatory=$false)]
    [switch]$RemoveAllCertificates,
    
    [Parameter(Mandatory=$false)]
    [string]$DriverPath = "$PSScriptRoot\x64\Debug\IntelAvbFilter"
)

# Require Administrator
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges"
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

function Check-TestSigning {
    Write-Step "Checking Test Signing Status"
    
    $bcdeditOutput = bcdedit /enum "{current}"
    if ($bcdeditOutput -match "testsigning\s+Yes") {
        Write-Success "Test signing is ENABLED"
        return $true
    } else {
        Write-Failure "Test signing is DISABLED"
        Write-Host "`n  To enable test signing, run:" -ForegroundColor Yellow
        Write-Host "    bcdedit /set testsigning on" -ForegroundColor White
        Write-Host "    shutdown /r /t 0" -ForegroundColor White
        Write-Host "`n  Or use the setup script:" -ForegroundColor Yellow
        Write-Host "    .\setup_driver.ps1 -EnableTestSigning" -ForegroundColor White
        return $false
    }
}

function Check-Certificates {
    Write-Step "Checking Certificate Stores"
    
    # Check Trusted Root
    Write-Host "`n[Trusted Root Certification Authorities]" -ForegroundColor Yellow
    $rootCerts = Get-ChildItem -Path "Cert:\LocalMachine\Root" | Where-Object {
        $_.Subject -like "*IntelAvbFilter*" -or 
        $_.Subject -like "*Microsoft*Test*" -or
        $_.Issuer -like "*IntelAvbFilter*"
    }
    
    if ($rootCerts) {
        Write-Success "Found $($rootCerts.Count) certificate(s) in Trusted Root"
        foreach ($cert in $rootCerts) {
            Write-Host "  Subject: $($cert.Subject)" -ForegroundColor Gray
            Write-Host "  Issuer: $($cert.Issuer)" -ForegroundColor Gray
            Write-Host "  Thumbprint: $($cert.Thumbprint)" -ForegroundColor Gray
            Write-Host "  Valid: $($cert.NotBefore) to $($cert.NotAfter)" -ForegroundColor Gray
            
            if ($cert.NotAfter -lt (Get-Date)) {
                Write-Warning "    Certificate EXPIRED!"
            }
            Write-Host ""
        }
    } else {
        Write-Warning "No certificates found in Trusted Root"
    }
    
    # Check Trusted Publishers
    Write-Host "[Trusted Publishers]" -ForegroundColor Yellow
    $pubCerts = Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {
        $_.Subject -like "*IntelAvbFilter*" -or 
        $_.Subject -like "*Microsoft*Test*" -or
        $_.Issuer -like "*IntelAvbFilter*"
    }
    
    if ($pubCerts) {
        Write-Success "Found $($pubCerts.Count) certificate(s) in Trusted Publishers"
        foreach ($cert in $pubCerts) {
            Write-Host "  Subject: $($cert.Subject)" -ForegroundColor Gray
            Write-Host "  Issuer: $($cert.Issuer)" -ForegroundColor Gray
            Write-Host "  Thumbprint: $($cert.Thumbprint)" -ForegroundColor Gray
            Write-Host "  Valid: $($cert.NotBefore) to $($cert.NotAfter)" -ForegroundColor Gray
            
            if ($cert.NotAfter -lt (Get-Date)) {
                Write-Warning "    Certificate EXPIRED!"
            }
            Write-Host ""
        }
    } else {
        Write-Failure "No certificates found in Trusted Publishers"
        Write-Host "  This is the most common cause of 'certificate error'" -ForegroundColor Yellow
    }
    
    # Compare stores
    if ($rootCerts -and $pubCerts) {
        $rootThumbprints = $rootCerts | Select-Object -ExpandProperty Thumbprint
        $pubThumbprints = $pubCerts | Select-Object -ExpandProperty Thumbprint
        
        $missingInPub = $rootThumbprints | Where-Object {$_ -notin $pubThumbprints}
        if ($missingInPub) {
            Write-Warning "Certificates in Root but missing in Trusted Publishers:"
            foreach ($thumbprint in $missingInPub) {
                $cert = $rootCerts | Where-Object {$_.Thumbprint -eq $thumbprint}
                Write-Host "  - $($cert.Subject)" -ForegroundColor Gray
            }
        }
    }
    
    return @{
        Root = $rootCerts
        TrustedPublisher = $pubCerts
    }
}

function Check-DriverSignature {
    param([string]$Path)
    
    Write-Step "Checking Driver Signature"
    
    $sysPath = Join-Path $Path "IntelAvbFilter.sys"
    $catPath = Join-Path $Path "IntelAvbFilter.cat"
    
    if (-not (Test-Path $sysPath)) {
        Write-Failure "Driver file not found: $sysPath"
        return $false
    }
    
    Write-Host "  Driver: $sysPath" -ForegroundColor Gray
    
    # Check file signature
    Write-Host "`n[Driver .SYS Signature]" -ForegroundColor Yellow
    try {
        $signature = Get-AuthenticodeSignature -FilePath $sysPath
        
        Write-Host "  Status: $($signature.Status)" -ForegroundColor $(
            if ($signature.Status -eq 'Valid') {'Green'} 
            elseif ($signature.Status -eq 'UnknownError') {'Yellow'} 
            else {'Red'}
        )
        
        if ($signature.SignerCertificate) {
            Write-Host "  Signer: $($signature.SignerCertificate.Subject)" -ForegroundColor Gray
            Write-Host "  Issuer: $($signature.SignerCertificate.Issuer)" -ForegroundColor Gray
            Write-Host "  Valid: $($signature.SignerCertificate.NotBefore) to $($signature.SignerCertificate.NotAfter)" -ForegroundColor Gray
        }
        
        if ($signature.Status -ne 'Valid') {
            Write-Host "  Status Message: $($signature.StatusMessage)" -ForegroundColor Yellow
        }
    } catch {
        Write-Failure "Failed to check signature: $($_.Exception.Message)"
    }
    
    # Check catalog signature
    if (Test-Path $catPath) {
        Write-Host "`n[Catalog .CAT Signature]" -ForegroundColor Yellow
        try {
            $catSignature = Get-AuthenticodeSignature -FilePath $catPath
            
            Write-Host "  Status: $($catSignature.Status)" -ForegroundColor $(
                if ($catSignature.Status -eq 'Valid') {'Green'} 
                elseif ($catSignature.Status -eq 'UnknownError') {'Yellow'} 
                else {'Red'}
            )
            
            if ($catSignature.SignerCertificate) {
                Write-Host "  Signer: $($catSignature.SignerCertificate.Subject)" -ForegroundColor Gray
            }
        } catch {
            Write-Warning "Failed to check catalog signature"
        }
    } else {
        Write-Warning "Catalog file not found: $catPath"
    }
    
    return $signature
}

function Fix-CertificateInstallation {
    Write-Step "Fixing Certificate Installation"
    
    # Find certificate in personal store
    $personalCert = Get-ChildItem -Path "Cert:\CurrentUser\My" | Where-Object {$_.Subject -like "*IntelAvbFilter*"}
    
    if (-not $personalCert) {
        Write-Failure "No certificate found in Personal store (Cert:\CurrentUser\My)"
        Write-Host "`n  To create a new certificate, run:" -ForegroundColor Yellow
        Write-Host "    .\setup_driver.ps1 -CreateCertificate" -ForegroundColor White
        return $false
    }
    
    Write-Success "Found certificate: $($personalCert.Subject)"
    Write-Host "  Thumbprint: $($personalCert.Thumbprint)" -ForegroundColor Gray
    
    # Export certificate
    $certPath = "$env:TEMP\IntelAvbFilter_Fix.cer"
    Export-Certificate -Cert $personalCert -FilePath $certPath -Force | Out-Null
    Write-Success "Exported certificate to: $certPath"
    
    # Remove old certificates from both stores
    Write-Step "Removing old certificates"
    
    Get-ChildItem -Path "Cert:\LocalMachine\Root" | Where-Object {$_.Subject -like "*IntelAvbFilter*"} | ForEach-Object {
        Write-Host "  Removing from Root: $($_.Thumbprint)" -ForegroundColor Gray
        Remove-Item -Path "Cert:\LocalMachine\Root\$($_.Thumbprint)" -Force -ErrorAction SilentlyContinue
    }
    
    Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {$_.Subject -like "*IntelAvbFilter*"} | ForEach-Object {
        Write-Host "  Removing from TrustedPublisher: $($_.Thumbprint)" -ForegroundColor Gray
        Remove-Item -Path "Cert:\LocalMachine\TrustedPublisher\$($_.Thumbprint)" -Force -ErrorAction SilentlyContinue
    }
    
    # Install to both stores
    Write-Step "Installing certificate to both stores"
    
    try {
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" -ErrorAction Stop | Out-Null
        Write-Success "Installed to Trusted Root Certification Authorities"
    } catch {
        Write-Failure "Failed to install to Root: $($_.Exception.Message)"
    }
    
    try {
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" -ErrorAction Stop | Out-Null
        Write-Success "Installed to Trusted Publishers"
    } catch {
        Write-Failure "Failed to install to TrustedPublisher: $($_.Exception.Message)"
    }
    
    # Verify installation
    Start-Sleep -Seconds 1
    $rootCheck = Get-ChildItem -Path "Cert:\LocalMachine\Root" | Where-Object {$_.Thumbprint -eq $personalCert.Thumbprint}
    $pubCheck = Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {$_.Thumbprint -eq $personalCert.Thumbprint}
    
    if ($rootCheck -and $pubCheck) {
        Write-Success "Certificate successfully installed to BOTH stores"
        return $true
    } else {
        Write-Failure "Certificate installation verification failed"
        if (-not $rootCheck) { Write-Host "  Missing from: Trusted Root" -ForegroundColor Red }
        if (-not $pubCheck) { Write-Host "  Missing from: Trusted Publishers" -ForegroundColor Red }
        return $false
    }
}

function Remove-AllIntelCertificates {
    Write-Step "Removing All IntelAvbFilter Certificates"
    
    Write-Warning "This will remove ALL IntelAvbFilter certificates from all stores"
    $confirm = Read-Host "Are you sure? (yes/no)"
    
    if ($confirm -ne 'yes') {
        Write-Host "Cancelled" -ForegroundColor Yellow
        return
    }
    
    $stores = @(
        "Cert:\CurrentUser\My",
        "Cert:\LocalMachine\Root",
        "Cert:\LocalMachine\TrustedPublisher"
    )
    
    foreach ($store in $stores) {
        Write-Host "`nChecking: $store" -ForegroundColor Cyan
        $certs = Get-ChildItem -Path $store | Where-Object {$_.Subject -like "*IntelAvbFilter*"}
        
        if ($certs) {
            foreach ($cert in $certs) {
                Write-Host "  Removing: $($cert.Subject) [$($cert.Thumbprint)]" -ForegroundColor Gray
                Remove-Item -Path "$store\$($cert.Thumbprint)" -Force -ErrorAction SilentlyContinue
            }
            Write-Success "Removed $($certs.Count) certificate(s) from $store"
        } else {
            Write-Host "  No certificates found" -ForegroundColor Gray
        }
    }
}

function Show-Recommendations {
    param(
        [bool]$TestSigningEnabled,
        [hashtable]$Certificates,
        [object]$Signature
    )
    
    Write-Step "Recommendations"
    
    $issues = @()
    
    # Check test signing
    if (-not $TestSigningEnabled) {
        $issues += "Test signing is not enabled"
        Write-Host "1. Enable test signing:" -ForegroundColor Yellow
        Write-Host "   bcdedit /set testsigning on" -ForegroundColor White
        Write-Host "   shutdown /r /t 0" -ForegroundColor White
        Write-Host ""
    }
    
    # Check certificates
    if (-not $Certificates.TrustedPublisher) {
        $issues += "No certificate in Trusted Publishers store"
        Write-Host "2. Install certificate to Trusted Publishers:" -ForegroundColor Yellow
        Write-Host "   .\troubleshoot_certificates.ps1 -FixCertificates" -ForegroundColor White
        Write-Host ""
    }
    
    # Check signature status
    if ($Signature -and $Signature.Status -ne 'Valid') {
        $issues += "Driver signature is not valid"
        Write-Host "3. Re-sign the driver or ensure test signing is enabled" -ForegroundColor Yellow
        Write-Host ""
    }
    
    if ($issues.Count -eq 0) {
        Write-Success "No issues detected! Driver should install successfully."
        Write-Host "`nTo install the driver, run:" -ForegroundColor Cyan
        Write-Host "  .\setup_driver.ps1 -InstallDriver" -ForegroundColor White
    } else {
        Write-Host "`nDetected $($issues.Count) issue(s):" -ForegroundColor Red
        $issues | ForEach-Object { Write-Host "  • $_" -ForegroundColor Red }
        Write-Host "`nComplete Setup Sequence:" -ForegroundColor Cyan
        Write-Host "  1. .\setup_driver.ps1 -EnableTestSigning" -ForegroundColor White
        Write-Host "  2. shutdown /r /t 0  # Reboot" -ForegroundColor White
        Write-Host "  3. .\troubleshoot_certificates.ps1 -FixCertificates" -ForegroundColor White
        Write-Host "  4. .\setup_driver.ps1 -InstallDriver" -ForegroundColor White
    }
}

# Main execution
Write-Host "Intel AVB Filter Driver - Certificate Troubleshooting" -ForegroundColor Magenta
Write-Host "========================================================" -ForegroundColor Magenta

$testSigning = Check-TestSigning
$certificates = Check-Certificates
$signature = Check-DriverSignature -Path $DriverPath

if ($FixCertificates) {
    Fix-CertificateInstallation
    
    # Re-check after fix
    Write-Host "`n" -NoNewline
    $certificates = Check-Certificates
}

if ($RemoveAllCertificates) {
    Remove-AllIntelCertificates
}

# Show recommendations if not performing actions
if (-not $FixCertificates -and -not $RemoveAllCertificates) {
    Show-Recommendations -TestSigningEnabled $testSigning -Certificates $certificates -Signature $signature
}

Write-Host "`n"
