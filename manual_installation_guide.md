/*++

Module Name:

    manual_installation_guide.md

Abstract:

    Step-by-step guide for manual Intel AVB Filter Driver installation
    Works with Secure Boot enabled - no test signing required

--*/

# Manual Installation Guide - Secure Boot Compatible

## Method: Device Manager Installation (Most Reliable)

This method works even with Secure Boot enabled because it uses Windows' built-in driver installation system.

### Step 1: Prepare Files

Ensure you have these files from the build:
- `x64\Debug\IntelAvbFilter.sys` (driver binary)
- `x64\Debug\IntelAvbFilter.inf` (installation information)
- `x64\Debug\IntelAvbFilter.cer` (test certificate)

### Step 2: Install Certificate (Optional but Recommended)

1. **Right-click** on `IntelAvbFilter.cer`
2. **Select "Install Certificate"**
3. **Store Location**: Select "Local Machine"
4. **Certificate Store**: Select "Place all certificates in the following store"
5. **Browse** and select "Trusted Root Certification Authorities"
6. **Click "Next"** and **"Finish"**

### Step 3: Manual Driver Installation

1. **Open Device Manager** (`devmgmt.msc`)

2. **Find your Intel I219 controller**:
   - Expand "Network adapters"
   - Look for "Intel(R) Ethernet Connection (22) I219-LM"

3. **Update Driver**:
   - Right-click the Intel I219 device
   - Select "Update driver"
   - Choose "Browse my computer for drivers"
   - Click "Let me pick from a list of available drivers"

4. **Install Filter Driver**:
   - Click "Have Disk..."
   - Browse to your project folder
   - Select `x64\Debug\IntelAvbFilter.inf`
   - Click "OK"

5. **Install the Driver**:
   - You should see "Intel AVB Filter Driver" or similar
   - Click "Next" to install
   - If Windows shows a security warning, click "Install this driver software anyway"

### Step 4: Verify Installation

1. **Check Device Manager**:
   - Look for no yellow warning signs
   - Driver should show as working properly

2. **Check Windows Event Log** (optional):
   - Open Event Viewer
   - Navigate to Windows Logs > System
   - Look for driver load events

### Step 5: Test Hardware Access

1. **Compile test application** (if not done):
   ```cmd
   cl avb_test_i219.c /Fe:avb_test_i219.exe
   ```

2. **Run the test**:
   ```cmd
   avb_test_i219.exe
   ```

3. **Expected Results**:
   - If successful: Shows device info and register values
   - If hardware access working: Debug output shows "(REAL HARDWARE)"
   - If still simulated: Shows simulated values

### Step 6: Monitor Debug Output

1. **Download DebugView.exe** from Microsoft Sysinternals
2. **Run as Administrator**
3. **Enable "Capture Kernel" option**
4. **Run your test again** and watch for:
   - `[TRACE] ==>AvbInitializeDevice: Transitioning to real hardware access`
   - `[INFO] Real hardware access enabled`
   - `[TRACE] AvbMmioReadReal: offset=0x..., value=0x... (REAL HARDWARE)`

## Troubleshooting

### "Windows cannot verify the publisher of this driver software"
- **Solution**: Install the certificate first (Step 2)
- **Alternative**: Click "Install this driver software anyway"

### "Windows has determined the driver software for this device is up to date"
- **Solution**: Use "Let me pick from a list" instead of automatic search
- **Alternative**: Uninstall current driver first, then install filter

### "The specified location does not contain a compatible software driver"
- **Check**: INF file exists and is not corrupted
- **Check**: Architecture matches (x64 system needs x64 driver)

### Driver installs but test application shows "Cannot open device"
- **Check**: Device name in test application matches INF file
- **Check**: Driver service is running (services.msc)
- **Try**: Reboot after driver installation

### Still shows simulated values instead of real hardware
- **This is normal**: Implementation has smart fallback
- **Check**: Debug output for BAR0 discovery messages
- **Possible**: Hardware access restrictions or permission issues

## Success Indicators

? **Driver Installation Success**:
- No errors in Device Manager
- Driver shown as "Working properly"
- Windows Event Log shows successful driver load

? **Hardware Access Success**:
- Test application shows device information
- Debug output contains "(REAL HARDWARE)" messages
- Register values look realistic (not obviously simulated)

? **Full System Success**:
- I219 controller properly detected
- IEEE 1588 timestamp registers accessible
- IOCTL interface working
- Ready for advanced TSN testing

---

*This method is based on Microsoft Windows Driver Samples documentation and works with Secure Boot enabled.*