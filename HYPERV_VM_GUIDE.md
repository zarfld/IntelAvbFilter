# Intel AVB Filter Driver - Hyper-V VM Development Guide

## ?? **Quick Setup Overview**

**Perfect for corporate environments where:**
- ? Host system must remain unchanged
- ? Secure Boot cannot be disabled on host
- ? IT policies restrict driver installation
- ? You need complete development freedom

## ?? **Automated Setup**

### **Step 1: Run Setup Script**
```cmd
# Run as Administrator
setup_hyperv_vm_complete.bat
```

The script will:
1. ? Check system requirements
2. ? Install Hyper-V (if needed)
3. ? Create "IntelAvbDev" VM
4. ? Disable Secure Boot in VM
5. ? Configure 8GB RAM, 100GB disk
6. ? Set up networking

### **Step 2: Windows Installation**
1. VM will boot from Windows 11 ISO
2. Install Windows normally
3. Create admin account
4. Complete setup

### **Step 3: Development Environment**
```cmd
# In the VM, run as Administrator:

# Enable test signing
bcdedit /set testsigning on

# Reboot VM
shutdown /r /t 0
```

## ?? **VM Specifications**

| Component | Configuration | Purpose |
|-----------|--------------|---------|
| **Name** | IntelAvbDev | Intel AVB development VM |
| **Generation** | 2 | Modern UEFI with Secure Boot control |
| **Memory** | 8GB | Sufficient for Visual Studio + WDK |
| **Disk** | 100GB Dynamic | Development tools + source code |
| **Secure Boot** | ? **DISABLED** | Allows test signing |
| **Network** | NAT/External | Internet + host communication |

## ?? **Development Workflow in VM**

### **Phase 1: Environment Setup**
```cmd
# Install in VM:
# 1. Visual Studio 2022 Community/Professional
# 2. Windows Driver Kit (WDK)
# 3. Git for Windows

# Verify WDK installation
where signtool
where inf2cat
```

### **Phase 2: Project Transfer**
```cmd
# Option A: Copy from host
# Copy D:\Repos\IntelAvbFilter to VM

# Option B: Git clone in VM
git clone --recursive https://github.com/your-repo/IntelAvbFilter.git
cd IntelAvbFilter
```

### **Phase 3: Build and Test**
```cmd
# In VM - build driver
msbuild IntelAvbFilter.sln /p:Configuration=Debug /p:Platform=x64

# Generate catalog
inf2cat /driver:x64\Debug /os:10_X64

# Sign driver
signtool sign /a /v /s my /n "WDKTestCert" /fd sha256 x64\Debug\IntelAvbFilter.cat

# Install via Network Control Panel
# Test with hardware_test.exe
```

## ?? **Key Advantages**

### **Corporate Compliance**
- ? **Host system unchanged** - No IT policy violations
- ? **Isolated testing** - No impact on production environment
- ? **Reversible** - VM can be deleted/restored anytime

### **Development Freedom**
- ? **Test signing enabled** - Full driver development capability
- ? **Admin access** - Complete control over VM environment
- ? **Hardware testing** - Can pass through USB devices if needed

### **Risk Management**
- ? **Snapshots** - Save VM state before risky operations
- ? **Isolated network** - VM networking separate from host
- ? **Easy reset** - Delete VM and recreate if corrupted

## ?? **Hardware Testing Strategy**

### **Level 1: Driver Installation Testing**
- Install Intel AVB Filter Driver in VM
- Verify device interface creation
- Test IOCTL communication
- Monitor debug output

### **Level 2: Simulated Hardware Testing**  
- Use Intel specification-based simulation
- Validate register access patterns
- Test IEEE 1588 timestamp simulation
- Verify TSN configuration logic

### **Level 3: Real Hardware Integration (Advanced)**
- **USB Device Passthrough**: Pass Intel USB-Ethernet adapter to VM
- **PCIe Passthrough**: Advanced configuration for PCIe devices
- **Network Bridge**: Direct hardware access through bridging

## ?? **VM Management Commands**

### **PowerShell VM Control**
```powershell
# Start VM
Start-VM -Name "IntelAvbDev"

# Connect to VM
vmconnect.exe localhost "IntelAvbDev"

# Create snapshot
Checkpoint-VM -Name "IntelAvbDev" -SnapshotName "Clean Development Environment"

# Restore snapshot
Restore-VMSnapshot -VMName "IntelAvbDev" -Name "Clean Development Environment"

# Configure VM resources
Set-VMMemory -VMName "IntelAvbDev" -DynamicMemoryEnabled $true -MinimumBytes 4GB -MaximumBytes 16GB
Set-VMProcessor -VMName "IntelAvbDev" -Count 4
```

### **VM Networking**
```powershell
# Create external switch (for internet access)
New-VMSwitch -Name "IntelAvbExternal" -NetAdapterName "Ethernet" -AllowManagementOS $true

# Attach VM to external switch
Add-VMNetworkAdapter -VMName "IntelAvbDev" -SwitchName "IntelAvbExternal"
```

## ?? **Expected Results**

### **Success Indicators in VM:**
```
? Test signing: Enabled
? Driver installation: Success via Network Control Panel
? Device interface: \\.\IntelAvbFilter accessible
? Hardware tests: Real hardware messages or explicit failures
? Network: VM has internet access
```

### **Debug Output to Expect:**
```
[TRACE] ==>AvbInitializeDevice: Real hardware access mode
[INFO]  Intel hardware discovered: Intel I219 (simulated)
[TRACE] AvbMmioRead: offset=0x00000, value=0x48100248
[TRACE] Device interface created: \\Device\IntelAvbFilter
```

## ?? **Production Path from VM**

### **After VM Validation:**
1. **Code Signing Certificate**: Order EV certificate for production
2. **Host System Testing**: Install signed driver on host
3. **Production Deployment**: Roll out to target systems

### **VM as Continuous Integration:**
- Use VM for automated testing
- Integrate with build pipelines
- Regression testing for driver updates

## ?? **Bottom Line**

**Hyper-V Development VM gives you:**
- ? **Complete development freedom** without corporate policy conflicts
- ? **Safe testing environment** with easy recovery
- ? **Professional workflow** suitable for production development
- ? **Hardware compatibility** testing capabilities
- ? **Zero impact** on your host system

**Your Intel AVB Filter Driver development can proceed immediately in a controlled, corporate-compliant environment!** ??

---
*Hyper-V Development VM - Complete freedom within corporate constraints*