# Intel AVB Filter Driver - Complete Installation Guide

## Prerequisites

### System Requirements
- Windows 10 version 1809+ or Windows 11
- Intel Ethernet Controller (I210, I217, I219, I225, I226, I350, 82575, 82576, or 82580)
- Administrator privileges
- Visual Studio 2019+ with Windows Driver Kit (WDK) for development

---

## Installation Methods

There are **three** ways to install this driver, depending on your scenario:

### Method 1: Test Signing (Development/Testing) ? RECOMMENDED FOR TESTING
### Method 2: Self-Signed Certificate with Local Trust
### Method 3: Production Signing (WHQL/EV Certificate)

---

## Method 1: Test Signing Mode (Development/Testing)

This is the **easiest** method for development and testing. Windows will accept test-signed drivers.

### Step 1: Enable Test Signing Mode

Open **PowerShell or Command Prompt as Administrator**:

```powershell
# Enable test signing
bcdedit /set testsigning on

# Verify it's enabled
bcdedit /enum {current}
# Look for "testsigning                 Yes"

# REBOOT REQUIRED
shutdown /r /t 0
```

After reboot, you should see **"Test Mode"** watermark in the bottom-right corner of your desktop.

### Step 2: Verify Test Signing Status

```powershell
bcdedit /enum {current} | findstr /i "testsigning"
# Should show: testsigning              Yes
```

### Step 3: Install the Driver

```powershell
# Navigate to the driver package directory
cd "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter"

# Install the driver using pnputil
pnputil /add-driver IntelAvbFilter.inf /install

# Alternative method using netcfg (for filter drivers)
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
```

### Step 4: Verify Installation

```powershell
# Check if the driver loaded
sc query IntelAvbFilter

# Check device manager for the filter
Get-NetAdapter | Select Name, InterfaceDescription

# Verify device node creation
ls \\.\IntelAvbFilter -ErrorAction SilentlyContinue
```

### Step 5: Check Driver Logs

```powershell
# View debug output (requires DebugView from Sysinternals)
# Download: https://docs.microsoft.com/en-us/sysinternals/downloads/debugview
# Run DebugView as Administrator and enable "Capture Kernel"
```

---

## Method 2: Self-Signed Certificate with Local Trust

If you don't want the "Test Mode" watermark, you can create and trust your own certificate.

### Step 1: Create Self-Signed Certificate

Open **PowerShell as Administrator**:

```powershell
# Create a certificate for code signing
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=IntelAvbFilter Development" `
    -KeyUsage DigitalSignature `
    -FriendlyName "IntelAvbFilter Driver Certificate" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

# Export the certificate
$password = ConvertTo-SecureString -String "YourPassword" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath "C:\IntelAvbFilter_Cert.pfx" -Password $password
Export-Certificate -Cert $cert -FilePath "C:\IntelAvbFilter_Cert.cer"

# Display certificate details
$cert | Format-List Subject, Thumbprint, NotBefore, NotAfter
```

### Step 2: Install Certificate in Trusted Stores

```powershell
# Import certificate to Trusted Root Certification Authorities
Import-Certificate -FilePath "C:\IntelAvbFilter_Cert.cer" -CertStoreLocation "Cert:\LocalMachine\Root"

# Import certificate to Trusted Publishers (required for driver installation)
Import-Certificate -FilePath "C:\IntelAvbFilter_Cert.cer" -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher"

# Verify installation
Get-ChildItem -Path "Cert:\LocalMachine\Root" | Where-Object {$_.Subject -like "*IntelAvbFilter*"}
Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {$_.Subject -like "*IntelAvbFilter*"}
```

### Step 3: Sign the Driver with Your Certificate

You need to re-sign the driver package with your certificate:

```powershell
# Set certificate path and password
$certPath = "C:\IntelAvbFilter_Cert.pfx"
$certPassword = "YourPassword"

# Navigate to WDK tools directory (adjust path for your WDK version)
cd "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"

# Sign the driver .sys file
.\signtool.exe sign /v /f $certPath /p $certPassword /t http://timestamp.digicert.com `
    "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys"

# Sign the catalog file
.\signtool.exe sign /v /f $certPath /p $certPassword /t http://timestamp.digicert.com `
    "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter\IntelAvbFilter.cat"

# Verify signatures
.\signtool.exe verify /v /pa `
    "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys"
```

### Step 4: Install the Driver

```powershell
# Install using pnputil
pnputil /add-driver "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf" /install

# Or using netcfg for NDIS filter drivers
netcfg -v -l "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf" -c s -i MS_IntelAvbFilter
```

---

## Method 3: Production Signing (WHQL/EV Certificate)

For production deployment, you need a Windows Hardware Quality Labs (WHQL) signature or EV certificate.

### Requirements
- **EV Code Signing Certificate** from a trusted CA (DigiCert, GlobalSign, etc.)
- **Windows Hardware Lab Kit (HLK)** for WHQL testing
- **Microsoft Partner Center account** for driver submission

### Process Overview
1. Purchase EV Code Signing Certificate (~$200-400/year)
2. Sign driver with EV certificate
3. Submit to Microsoft Partner Center for WHQL certification
4. Receive signed driver package from Microsoft
5. Distribute to end users

For detailed instructions, see: https://docs.microsoft.com/en-us/windows-hardware/drivers/dashboard/

---

## Troubleshooting Common Installation Issues

### Issue 1: "Driver is not signed" Error

**Symptoms**: Installation fails with certificate or signature error

**Solution**:
```powershell
# 1. Verify test signing is enabled
bcdedit /enum {current} | findstr /i "testsigning"
# Should show: testsigning              Yes

# 2. If not enabled, enable and reboot
bcdedit /set testsigning on
shutdown /r /t 0

# 3. After reboot, verify "Test Mode" watermark appears
```

### Issue 2: Certificate Already Installed but Still Fails

**Symptoms**: Certificate is in store but driver won't install

**Solution**:
```powershell
# 1. Remove old certificates
Get-ChildItem -Path "Cert:\LocalMachine\Root" | Where-Object {$_.Subject -like "*IntelAvbFilter*"} | Remove-Item
Get-ChildItem -Path "Cert:\LocalMachine\TrustedPublisher" | Where-Object {$_.Subject -like "*IntelAvbFilter*"} | Remove-Item

# 2. Re-import to BOTH stores
Import-Certificate -FilePath "C:\IntelAvbFilter_Cert.cer" -CertStoreLocation "Cert:\LocalMachine\Root"
Import-Certificate -FilePath "C:\IntelAvbFilter_Cert.cer" -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher"

# 3. Verify certificates are present
certlm.msc
# Navigate to: Trusted Root Certification Authorities > Certificates
# Navigate to: Trusted Publishers > Certificates
# Ensure your certificate appears in BOTH locations
```

### Issue 3: Driver Loads but Device Node Not Created

**Symptoms**: Installation succeeds but `\\.\IntelAvbFilter` doesn't exist

**Possible Causes**:
1. **No Intel hardware detected** - Driver only loads on systems with supported Intel Ethernet controllers
2. **Wrong VID/DID** - Your Intel controller isn't in the supported list

**Solution**:
```powershell
# Check your Intel Ethernet adapter hardware ID
Get-NetAdapter | ForEach-Object {
    $deviceID = (Get-PnpDevice -InstanceId $_.PnPDeviceID).HardwareID
    [PSCustomObject]@{
        Name = $_.InterfaceDescription
        HardwareID = $deviceID
    }
}

# Look for Intel devices (VEN_8086)
# Example: PCI\VEN_8086&DEV_15F3  (I219 controller)
```

If your Intel controller's Device ID (DEV_xxxx) isn't in the supported list, you'll need to add it to the INF file.

### Issue 4: "Object reference not set to an instance of an object" Build Error

**Symptoms**: Build completes but shows this error during post-build signing

**Cause**: Visual Studio WDK deployment configuration issue

**Solution**: This is a **cosmetic error** in the WDK tooling and doesn't affect the driver. You can safely ignore it if:
- Build shows "1 succeeded, 0 failed"
- IntelAvbFilter.sys is created
- Driver package is signed successfully

To fix the error:
1. Open Visual Studio ? Project Properties
2. Navigate to: Driver Install ? Deployment
3. Remove any invalid target computer configurations
4. Disable deployment if you're not using remote test machines

### Issue 4A: "No connection could be made because the target machine actively refused it" 

**Symptoms**: Build succeeds but shows deployment connection error:
```
Error: 10061 (ConnectionRefused)
Error message: No connection could be made because the target machine actively refused it 127.0.0.1:50005
```

**Cause**: Visual Studio is trying to deploy/test on a remote machine that doesn't exist

**Solution 1 - Use Fix Script**:
```powershell
.\fix_deployment_config.ps1
```

**Solution 2 - Manual Fix in Visual Studio**:
1. Right-click `IntelAvbFilter` project ? **Properties**
2. Navigate to: **Configuration Properties** ? **Driver Install** ? **Deployment**
3. **UNCHECK** "Enable deployment"
4. **UNCHECK** "Remove previous driver versions before deployment"
5. Click **Apply** ? **OK**
6. **Rebuild Solution**

**Solution 3 - Clean Visual Studio Cache**:
```powershell
# Close Visual Studio first!
cd C:\Users\dzarf\source\repos\IntelAvbFilter
Remove-Item -Path ".vs" -Recurse -Force
# Reopen Visual Studio and rebuild
```

**Note**: This error doesn't affect the compiled driver. The driver is still built successfully and can be installed normally.

### Issue 5: "Cannot find file" during netcfg

**Symptoms**: `netcfg -l IntelAvbFilter.inf` fails with "file not found"

**Solution**:
```powershell
# Use absolute path
netcfg -v -l "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf" -c s -i MS_IntelAvbFilter

# Verify all required files are present
ls "C:\Users\dzarf\source\repos\IntelAvbFilter\x64\Debug\IntelAvbFilter"
# Should show: IntelAvbFilter.inf, IntelAvbFilter.sys, IntelAvbFilter.cat
```

---

## Uninstalling the Driver

### Uninstall using pnputil

```powershell
# List installed drivers
pnputil /enum-drivers | findstr /i "IntelAvbFilter"

# Uninstall by published name (example: oem123.inf)
pnputil /delete-driver oem123.inf /uninstall /force
```

### Uninstall using netcfg

```powershell
# Remove the filter service
netcfg -v -u MS_IntelAvbFilter
```

### Disable Test Signing (Optional)

```powershell
# Disable test signing
bcdedit /set testsigning off

# Reboot
shutdown /r /t 0
```

---

## Verifying Installation Success

### 1. Check Driver Service Status

```powershell
# Query service
sc query IntelAvbFilter
# Should show: STATE: 4 RUNNING

# Or using Get-Service
Get-Service -Name IntelAvbFilter
```

### 2. Check Device Node

```powershell
# Verify device node creation (requires Intel hardware)
[System.IO.File]::Exists("\\.\IntelAvbFilter")
# Should return: True

# Try opening device
$handle = [System.IO.File]::Open("\\.\IntelAvbFilter", [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::ReadWrite)
$handle.Close()
# Should succeed if Intel hardware is present
```

### 3. Check Event Viewer

```powershell
# Open Event Viewer
eventvwr.msc

# Navigate to: Windows Logs ? System
# Filter by: Source = "IntelAvbFilter"
# Look for informational events indicating successful load
```

### 4. Use DebugView for Kernel Debug Output

1. Download DebugView: https://docs.microsoft.com/en-us/sysinternals/downloads/debugview
2. Run as Administrator
3. Enable: Capture ? Capture Kernel
4. Filter: Edit ? Filter/Highlight ? "IntelAvb"
5. Look for initialization messages:
   ```
   IntelAvb: Intel device registry initialized with full IGB support
   IntelAvb: Modern: I210, I217, I219, I226, I350, I354
   IntelAvb: Legacy: 82575, 82576, 82580
   ```

---

## Testing the Driver

### Basic Connectivity Test

```powershell
# Run the AVB test application
cd "C:\Users\dzarf\source\repos\IntelAvbFilter\tools\avb_test"
.\avb_test.exe

# Expected output:
# Opening device \\.\IntelAvbFilter...
# Device opened successfully
# Intel device count: 1
# Device 0: VID=0x8086, DID=0x15F3, Capabilities=0x000000C3
```

### IOCTL Test

```powershell
# Test device enumeration
.\avb_test.exe enum

# Test register access
.\avb_test.exe read 0x00000

# Test PTP clock
.\avb_test.exe ptp
```

---

## Building from Source

If you need to rebuild the driver:

### Prerequisites
1. Visual Studio 2019 or later
2. Windows Driver Kit (WDK) 10.0.19041 or later
3. Windows SDK 10.0.22000 or later

### Build Steps

```powershell
# Clone repository
git clone --recursive https://github.com/zarfld/IntelAvbFilter.git
cd IntelAvbFilter

# Open solution in Visual Studio
start IntelAvbFilter.sln

# Or build from command line
msbuild IntelAvbFilter.sln /p:Configuration=Debug /p:Platform=x64

# Build output location:
# x64\Debug\IntelAvbFilter\IntelAvbFilter.sys
```

---

## Advanced Configuration

### Changing Debug Output Level

Edit `flt_dbg.h` and modify `DBG_INIT_LEVEL`:

```c
// Debug levels:
// DL_NONE   = 0  (No output)
// DL_ERROR  = 1  (Errors only)
// DL_WARN   = 2  (Warnings + Errors)
// DL_INFO   = 3  (Info + Warnings + Errors)
// DL_TRACE  = 4  (Everything)

#define DBG_INIT_LEVEL  DL_INFO  // Change to desired level
```

Rebuild and reinstall the driver.

---

## Security Considerations

### Test Signing Risks
- **Test Mode reduces security** - any test-signed driver can load
- **Use only in development/test environments**
- **Disable test signing in production systems**

### Self-Signed Certificate Risks
- **Less secure than WHQL** - no Microsoft validation
- **Requires manual trust installation** on each machine
- **Suitable for internal/corporate deployments only**

### Production WHQL Benefits
- **Microsoft validation** - driver passes quality tests
- **Automatic trust** - no manual certificate installation
- **Windows Update delivery** - seamless deployment
- **Required for most enterprises** and OEM scenarios

---

## Getting Help

### Common Resources
- **WDK Documentation**: https://docs.microsoft.com/en-us/windows-hardware/drivers/
- **NDIS Driver Development**: https://docs.microsoft.com/en-us/windows-hardware/drivers/network/
- **Driver Signing**: https://docs.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing

### Project-Specific Help
- **GitHub Issues**: https://github.com/zarfld/IntelAvbFilter/issues
- **Build Logs**: Check `x64\Debug\build.log`
- **Debug Output**: Use DebugView (see above)

---

## Quick Reference Card

### Enable Test Signing
```powershell
bcdedit /set testsigning on
shutdown /r /t 0
```

### Install Driver
```powershell
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
```

### Uninstall Driver
```powershell
netcfg -v -u MS_IntelAvbFilter
```

### Disable Test Signing
```powershell
bcdedit /set testsigning off
shutdown /r /t 0
```

### Check Installation
```powershell
sc query IntelAvbFilter
Get-NetAdapter | Select Name, InterfaceDescription
```

---

**Note**: This driver is currently in **development/testing phase**. For production deployment, obtain proper WHQL certification from Microsoft.
