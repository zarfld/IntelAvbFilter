# Intel AVB Filter Driver - Manual Hyper-V Setup Steps

## ?? **Your System Status**
- ? **Windows 11 Enterprise** - Perfect for Hyper-V!
- ? **Intel I219-LM (0x0DC7)** - Hardware confirmed
- ? **Corporate environment** - VM solution ideal

## ?? **Manual Setup Steps**

### **Step 1: Enable Hyper-V (Run as Administrator)**

**Option A: Using Windows Features**
1. Press `Windows + R`
2. Type: `appwiz.cpl`
3. Click "Turn Windows features on or off"
4. Check ?? **Hyper-V** (both Platform and Tools)
5. Click OK, restart when prompted

**Option B: Using PowerShell (As Administrator)**
```powershell
# Enable Hyper-V
Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V -All

# Restart required
Restart-Computer
```

**Option C: Using Command Prompt (As Administrator)**
```cmd
dism /online /enable-feature /featurename:Microsoft-Hyper-V /all
```

### **Step 2: Create Development VM**

**After reboot, open PowerShell as Administrator:**

```powershell
# Create VM directory
New-Item -ItemType Directory -Path "C:\VMs" -Force

# Create the VM
New-VM -Name "IntelAvbDev" -MemoryStartupBytes 8GB -Generation 2 -Path "C:\VMs"

# Create virtual disk
New-VHD -Path "C:\VMs\IntelAvbDev\IntelAvbDev.vhdx" -SizeBytes 100GB -Dynamic

# Attach disk to VM
Add-VMHardDiskDrive -VMName "IntelAvbDev" -Path "C:\VMs\IntelAvbDev\IntelAvbDev.vhdx"

# ?? CRITICAL: Disable Secure Boot (allows driver development)
Set-VMFirmware -VMName "IntelAvbDev" -EnableSecureBoot Off

# Configure VM resources
Set-VMProcessor -VMName "IntelAvbDev" -Count 4
Set-VMMemory -VMName "IntelAvbDev" -DynamicMemoryEnabled $true -MinimumBytes 4GB -MaximumBytes 16GB

# Enable integration services
Enable-VMIntegrationService -VMName "IntelAvbDev" -Name "Guest Service Interface","Heartbeat","Key-Value Pair Exchange","Shutdown","Time Synchronization","VSS"

Write-Host "? VM Created Successfully!" -ForegroundColor Green
```

### **Step 3: Download Windows 11 ISO**

1. Go to: https://www.microsoft.com/software-download/windows11
2. Select "Download Windows 11 Disk Image (ISO)"
3. Save as: `C:\VMs\Windows11.iso`

**Attach ISO to VM:**
```powershell
# Attach Windows ISO
Set-VMDvdDrive -VMName "IntelAvbDev" -Path "C:\VMs\Windows11.iso"
```

### **Step 4: Start VM and Install Windows**

```powershell
# Start the VM
Start-VM -Name "IntelAvbDev"

# Connect to VM
vmconnect.exe localhost "IntelAvbDev"
```

**In the VM:**
1. Install Windows 11 normally
2. Create local admin account  
3. Complete Windows setup
4. Install Windows updates

### **Step 5: Configure Development Environment in VM**

**In the VM, run as Administrator:**

```cmd
# Enable test signing (CRITICAL for driver development)
bcdedit /set testsigning on

# Reboot VM
shutdown /r /t 0
```

**After VM reboot, install development tools:**

1. **Visual Studio 2022** (Community/Professional)
   - Download from: https://visualstudio.microsoft.com/downloads/

2. **Windows Driver Kit (WDK)**
   - Download from: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk

3. **Git for Windows** (optional)
   - Download from: https://git-scm.com/download/win

### **Step 6: Transfer Your Intel AVB Project**

**Option A: Copy files from host**
- Use VM's file sharing or USB passthrough
- Copy entire `D:\Repos\IntelAvbFilter` folder to VM

**Option B: Git clone in VM**
```cmd
git clone --recursive https://github.com/your-repo/IntelAvbFilter.git
```

### **Step 7: Build and Test in VM**

```cmd
# Build the driver
msbuild IntelAvbFilter.sln /p:Configuration=Debug /p:Platform=x64

# Generate catalog
inf2cat /driver:x64\Debug /os:10_X64

# Sign driver (test certificate)
signtool sign /a /v /s my /n "WDKTestCert" /fd sha256 x64\Debug\IntelAvbFilter.cat

# Install via Network Control Panel method
# Test with your hardware test applications
```

## ?? **Quick Verification Commands**

**Check if Hyper-V is installed:**
```powershell
Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V
```

**List VMs:**
```powershell
Get-VM
```

**VM Status:**
```powershell
Get-VM -Name "IntelAvbDev" | Select Name, State, CPUUsage, MemoryAssigned, Uptime
```

## ?? **Key Benefits of This Setup**

### **Corporate Compliance**
- ? Host system completely unchanged
- ? All driver testing isolated in VM
- ? Can be removed without trace
- ? No IT policy violations

### **Development Freedom**
- ? Test signing enabled in VM only
- ? Full admin rights in VM
- ? Install any development tools
- ? Kernel driver development possible

### **Risk Management**
- ? VM snapshots for easy recovery
- ? Isolated from host network
- ? No impact on production system
- ? Easy to delete and recreate

## ?? **Expected Results**

**After setup, you'll have:**
- ? Windows 11 VM with test signing enabled
- ? Visual Studio + WDK installed
- ? Your Intel AVB Filter Driver project
- ? Ability to install and test drivers safely
- ? Complete development environment

**Driver testing in VM should show:**
```
? bcdedit shows: testsigning Yes
? Driver installs via Network Control Panel
? Device interface \\.\IntelAvbFilter accessible
? Hardware tests show real hardware access or explicit failures
```

## ?? **Next Steps After VM Setup**

1. **Validate driver installation** in VM
2. **Test all IOCTL functionality** 
3. **Debug hardware access** with DebugView.exe
4. **Fix any integration issues** 
5. **Prepare for production** with code signing certificate

**Your Intel AVB Filter Driver development can proceed immediately in this safe, corporate-compliant environment!** ??

---
*Manual Hyper-V Setup - Complete control over your development environment*