# Automatisches Zertifikatsmanagement f�r Intel AVB Filter
# Exportiert und installiert das Testzertifikat automatisch

Write-Host "Managing Intel AVB Filter test certificate..." -ForegroundColor Green

# Zertifikat aus PrivateCertStore exportieren
$certs = Get-ChildItem -Path "Cert:\CurrentUser\PrivateCertStore" | Where-Object { $_.Subject -match "Intel AVB Filter Test Certificate" }

if ($certs.Count -eq 0) {
    Write-Error "No Intel AVB Filter test certificate found in PrivateCertStore"
    exit 1
}

# Neuestes Zertifikat verwenden
$cert = $certs | Sort-Object NotAfter -Descending | Select-Object -First 1
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$certPath = Join-Path $repoRoot "IntelAvbTestCert.cer"

Write-Host "Exporting certificate:" -ForegroundColor Yellow
Write-Host "  Subject: $($cert.Subject)" -ForegroundColor Cyan
Write-Host "  Thumbprint: $($cert.Thumbprint)" -ForegroundColor Cyan
Write-Host "  Expires: $($cert.NotAfter)" -ForegroundColor Cyan

# Zertifikat exportieren
$cert | Export-Certificate -FilePath $certPath -Force
Write-Host "Certificate exported to: $certPath" -ForegroundColor Green

# Pr�fen ob als Admin ausgef�hrt wird
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")

if ($isAdmin) {
    Write-Host "Installing certificate to Trusted Root Certification Authorities..." -ForegroundColor Yellow
    try {
        Import-Certificate -FilePath $certPath -CertStoreLocation "Cert:\LocalMachine\Root" -ErrorAction Stop
        Write-Host "Certificate successfully installed to Trusted Root!" -ForegroundColor Green
        
        Write-Host "`nNext steps:" -ForegroundColor Green
        Write-Host "1. Ensure test signing is enabled: bcdedit /set testsigning on" -ForegroundColor Yellow
        Write-Host "2. Restart computer if test signing was just enabled" -ForegroundColor Yellow
        Write-Host "3. Install driver: pnputil /add-driver IntelAvbFilter.inf /install" -ForegroundColor Yellow
    } catch {
        Write-Error "Failed to install certificate: $($_.Exception.Message)"
        Write-Host "Manual installation required:" -ForegroundColor Yellow
        Write-Host "1. Run: certmgr.msc" -ForegroundColor Cyan
        Write-Host "2. Right-click 'Trusted Root Certification Authorities' -> Certificates" -ForegroundColor Cyan
        Write-Host "3. Select 'All Tasks' -> 'Import'" -ForegroundColor Cyan
        Write-Host "4. Import file: $certPath" -ForegroundColor Cyan
    }
} else {
    Write-Host "Not running as Administrator. Manual certificate installation required:" -ForegroundColor Yellow
    Write-Host "1. Run PowerShell as Administrator" -ForegroundColor Cyan
    Write-Host "2. Import-Certificate -FilePath '$certPath' -CertStoreLocation 'Cert:\LocalMachine\Root'" -ForegroundColor Cyan
    Write-Host "Or use GUI: certmgr.msc" -ForegroundColor Cyan
}

# Zertifikat-Info f�r Debugging
Write-Host "`nCertificate Details:" -ForegroundColor Green
Write-Host "Location: Cert:\CurrentUser\PrivateCertStore" -ForegroundColor Cyan
Write-Host "File: $certPath" -ForegroundColor Cyan
Write-Host "Thumbprint: $($cert.Thumbprint)" -ForegroundColor Cyan