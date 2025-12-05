# Certificate Error Solution - Complete Guide

## Problem Summary

You're getting certificate errors when trying to install the Intel AVB Filter Driver, even after manually installing the certificate. This is a **very common issue** with Windows driver development.

## Root Cause

Windows driver installation requires certificates to be in **TWO specific locations**:
1. **Trusted Root Certification Authorities** - Establishes trust
2. **Trusted Publishers** - Required for kernel driver installation

Most people only install to the first location, missing the second one!

## Quick Solution ?

We've created automated PowerShell scripts to fix this for you:

### Option 1: Fully Automated (Recommended)
```powershell
# 1. Open PowerShell as Administrator
# 2. Navigate to driver directory
cd C:\Users\dzarf\source\repos\IntelAvbFilter

# 3. Run the quick start script - it will guide you through everything
.\quick_start.ps1
```

The script will:
- Check your current system status
- Tell you exactly what needs to be done
- Offer to fix issues automatically
- Guide you step-by-step

### Option 2: Manual Step-by-Step

If you prefer manual control:

```powershell
# Step 1: Check current status
.\setup_driver.ps1 -CheckStatus

# Step 2: Enable test signing (if not already enabled)
.\setup_driver.ps1 -EnableTestSigning
shutdown /r /t 0  # Reboot required

# Step 3: After reboot, fix certificate installation
.\troubleshoot_certificates.ps1 -FixCertificates

# Step 4: Install the driver
.\setup_driver.ps1 -InstallDriver

# Step 5: Verify installation
.\setup_driver.ps1 -CheckStatus
```

## What Each Script Does

### `quick_start.ps1` - Interactive Installation Guide
- Detects current system state
- Provides step-by-step instructions
- Offers to run commands for you
- Perfect for first-time setup

**Usage**: `.\quick_start.ps1`

### `setup_driver.ps1` - Driver Installation Manager
- Enables test signing mode
- Creates self-signed certificates
- Installs/uninstalls the driver
- Checks installation status

**Usage**:
```powershell
.\setup_driver.ps1 -CheckStatus           # Check status
.\setup_driver.ps1 -EnableTestSigning     # Enable test signing
.\setup_driver.ps1 -CreateCertificate     # Create certificate
.\setup_driver.ps1 -InstallDriver         # Install driver
.\setup_driver.ps1 -UninstallDriver       # Uninstall driver
```

### `troubleshoot_certificates.ps1` - Certificate Diagnostic Tool
- Diagnoses certificate problems
- Fixes certificate installation automatically
- Removes old/stale certificates
- Verifies proper certificate placement

**Usage**:
```powershell
.\troubleshoot_certificates.ps1               # Diagnose issues
.\troubleshoot_certificates.ps1 -FixCertificates  # Auto-fix
.\troubleshoot_certificates.ps1 -RemoveAllCertificates  # Clean slate
```

## Common Error Messages and Solutions

### Error: "The system cannot find the file specified" (Error 2)
**Meaning**: Driver device node not created  
**Cause**: Either no Intel hardware detected, or driver didn't load  
**Solution**: This is **actually correct** if you don't have Intel hardware!

### Error: "Windows cannot verify the digital signature" (Error 52)
**Meaning**: Certificate issue  
**Solution**: 
```powershell
.\troubleshoot_certificates.ps1 -FixCertificates
```

### Error: "Test signing is not enabled" 
**Solution**:
```powershell
.\setup_driver.ps1 -EnableTestSigning
shutdown /r /t 0  # Reboot
```

### Error: "Access is denied" (Error 5)
**Meaning**: Not running as Administrator  
**Solution**: Right-click PowerShell ? "Run as Administrator"

## Understanding Test Signing Mode

### What is Test Signing?
Test signing mode allows Windows to load drivers signed with test certificates (not production WHQL certificates).

### Enable Test Signing:
```powershell
bcdedit /set testsigning on
shutdown /r /t 0  # Reboot required
```

### Disable Test Signing:
```powershell
bcdedit /set testsigning off
shutdown /r /t 0  # Reboot required
```

### Side Effect:
- Your desktop will show **"Test Mode"** watermark in bottom-right corner
- This is **normal** and indicates test signing is active
- Required for development drivers

## Certificate Stores Explained

Windows has multiple certificate stores. For driver signing, you need:

### Store 1: Trusted Root Certification Authorities
- **Path**: `Cert:\LocalMachine\Root`
- **Purpose**: Establishes that the certificate authority is trusted
- **View**: Run `certlm.msc` ? Trusted Root Certification Authorities ? Certificates

### Store 2: Trusted Publishers (Critical for Drivers!)
- **Path**: `Cert:\LocalMachine\TrustedPublisher`
- **Purpose**: Allows kernel-mode driver installation from this publisher
- **View**: Run `certlm.msc` ? Trusted Publishers ? Certificates

**Both are required!** Most installation issues come from missing the second store.

## Verifying Certificate Installation

### Method 1: Using our script
```powershell
.\troubleshoot_certificates.ps1
```

### Method 2: Manual verification
```powershell
# Check Trusted Root
Get-ChildItem -Path "Cert:\LocalMachine\Root" | Where-Object {$_.Subject -like "*IntelAvbFilter*"}

# Check Trusted Publishers (Critical!)
Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {$_.Subject -like "*IntelAvbFilter*"}
```

Both should show your certificate!

### Method 3: GUI verification
1. Run `certlm.msc` (Certificate Manager for Local Machine)
2. Navigate to **Trusted Root Certification Authorities** ? **Certificates**
3. Look for "IntelAvbFilter Development"
4. Navigate to **Trusted Publishers** ? **Certificates**
5. Look for "IntelAvbFilter Development" here too!

## Complete Installation Sequence

### First Time Installation:

```powershell
# 1. Open PowerShell as Administrator
# 2. Navigate to driver directory
cd C:\Users\dzarf\source\repos\IntelAvbFilter

# 3. Check current status
.\setup_driver.ps1 -CheckStatus

# 4. Enable test signing (if needed)
.\setup_driver.ps1 -EnableTestSigning
shutdown /r /t 0

# 5. After reboot, fix certificates
.\troubleshoot_certificates.ps1 -FixCertificates

# 6. Install driver
.\setup_driver.ps1 -InstallDriver

# 7. Verify installation
.\setup_driver.ps1 -CheckStatus
```

### Expected Final Status:
```
? Test signing: ENABLED
? Certificates: INSTALLED
? Driver: INSTALLED (Running)
? Intel adapters: DETECTED (X adapter(s))
```

## Troubleshooting Installation Failures

If installation still fails after following the above steps:

### 1. Check Windows Event Viewer
```powershell
eventvwr.msc
# Navigate to: Windows Logs ? System
# Look for Source: "IntelAvbFilter" or "DriverFrameworks-UserMode"
```

### 2. Verify Driver Files
```powershell
# Check all required files exist
cd x64\Debug\IntelAvbFilter
ls IntelAvbFilter.*
# Should show: .inf, .sys, .cat files
```

### 3. Check Driver Signature
```powershell
.\troubleshoot_certificates.ps1
# Look at the "Driver Signature" section
# Status should be "Valid" or "UnknownError" (OK in test mode)
```

### 4. Manual Certificate Creation
If auto-generation fails, create manually:
```powershell
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=IntelAvbFilter Development" `
    -KeyUsage DigitalSignature `
    -FriendlyName "IntelAvbFilter Driver Certificate" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

# Export
Export-Certificate -Cert $cert -FilePath "C:\IntelAvbFilter_Cert.cer"

# Install to BOTH stores
Import-Certificate -FilePath "C:\IntelAvbFilter_Cert.cer" -CertStoreLocation "Cert:\LocalMachine\Root"
Import-Certificate -FilePath "C:\IntelAvbFilter_Cert.cer" -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher"
```

## Production Deployment (No Certificate Errors)

For production systems, you need a **WHQL (Windows Hardware Quality Labs)** certificate from Microsoft:

### Requirements:
1. **EV Code Signing Certificate** (~$200-400/year)
2. **Windows Hardware Lab Kit (HLK)** testing
3. **Microsoft Partner Center** account
4. Driver submission and approval

### Process:
1. Purchase EV certificate from trusted CA (DigiCert, GlobalSign, etc.)
2. Run HLK tests on your driver
3. Submit to Microsoft Partner Center
4. Receive WHQL-signed driver
5. Distribute to users (no test signing needed!)

### Benefits:
- No test signing mode required
- No certificate installation needed
- Automatic Windows Update distribution
- Required for enterprise deployment

## Getting More Help

### Documentation:
- **Complete Install Guide**: `INSTALLATION_GUIDE.md`
- **Main README**: `README.md`
- **Microsoft Docs**: https://docs.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing

### Tools:
- **Interactive Guide**: `.\quick_start.ps1`
- **Installation Manager**: `.\setup_driver.ps1 -CheckStatus`
- **Certificate Fixer**: `.\troubleshoot_certificates.ps1`

### Support:
- **GitHub Issues**: https://github.com/zarfld/IntelAvbFilter/issues
- **Windows Driver Forums**: https://docs.microsoft.com/en-us/answers/topics/windows-hardware-driver-kit.html

## Quick Reference Commands

```powershell
# Complete setup (automated)
.\quick_start.ps1

# Enable test signing
.\setup_driver.ps1 -EnableTestSigning
shutdown /r /t 0

# Fix certificates
.\troubleshoot_certificates.ps1 -FixCertificates

# Install driver
.\setup_driver.ps1 -InstallDriver

# Check status
.\setup_driver.ps1 -CheckStatus

# Uninstall
.\setup_driver.ps1 -UninstallDriver

# Diagnose problems
.\troubleshoot_certificates.ps1

# Manual certificate check
certlm.msc
```

## Summary

The certificate error you're experiencing is solved by ensuring the certificate is in **both required stores**:
1. Trusted Root Certification Authorities
2. **Trusted Publishers** ? Most commonly forgotten!

Use our automated scripts to fix this:
```powershell
.\troubleshoot_certificates.ps1 -FixCertificates
```

Or follow the complete setup sequence with the interactive guide:
```powershell
.\quick_start.ps1
```

**This should completely resolve your certificate installation issues!** ?
