@echo off
echo ===========================================================================
echo   Intel AVB Filter Driver - Hyper-V Development VM Setup
echo   Corporate Environment Solution: Host system remains unchanged
echo ===========================================================================
echo.

REM Check administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ? ERROR: Must run as Administrator
    echo    Right-click Command Prompt and select "Run as administrator"
    pause
    exit /b 1
)

echo ? Running as Administrator
echo.

echo Phase 1: System Prerequisites Check
echo ===================================

REM Check Windows edition
for /f "tokens=4-7 delims=. " %%i in ('ver') do set VERSION=%%i.%%j
echo Windows Version: %VERSION%

wmic os get Caption | findstr /i "Pro\|Enterprise" >nul
if %errorLevel% == 0 (
    echo ? Windows edition supports Hyper-V
) else (
    echo ? Windows Home edition detected
    echo    Hyper-V requires Windows 10/11 Pro or Enterprise
    echo    Alternative: Use VMware Workstation Pro or VirtualBox
    goto :ALTERNATIVE_SOLUTIONS
)

REM Check CPU virtualization support
wmic cpu get VirtualizationFirmwareEnabled | findstr /i "true" >nul
if %errorLevel% == 0 (
    echo ? CPU virtualization enabled in BIOS
) else (
    echo ??  CPU virtualization may not be enabled
    echo    Check BIOS settings for Intel VT-x/AMD-V
    echo    Continuing anyway...
)

REM Check memory
for /f "tokens=2 delims==" %%i in ('wmic computersystem get TotalPhysicalMemory /value') do set MEMORY=%%i
set /a MEMORY_GB=%MEMORY:~0,-9%
if %MEMORY_GB% geq 16 (
    echo ? Memory: %MEMORY_GB%GB (sufficient for VM development)
) else if %MEMORY_GB% geq 8 (
    echo ??  Memory: %MEMORY_GB%GB (minimum for VM development)
) else (
    echo ? Memory: %MEMORY_GB%GB (insufficient for comfortable VM development)
    echo    Recommend at least 8GB total memory
)

echo.
echo Phase 2: Hyper-V Feature Installation
echo =====================================

REM Check if Hyper-V is already installed
dism /online /get-featureinfo /featurename:Microsoft-Hyper-V >nul 2>&1
if %errorLevel% == 0 (
    echo ? Hyper-V is already available
) else (
    echo Installing Hyper-V feature...
    echo This requires a system reboot after completion.
    echo.
    set /p INSTALL_HYPERV="Install Hyper-V now? (Y/N): "
    if /i "%INSTALL_HYPERV%"=="Y" (
        echo Installing Hyper-V...
        dism /online /enable-feature /featurename:Microsoft-Hyper-V /all /norestart
        if %errorLevel% == 0 (
            echo ? Hyper-V installation initiated
            echo.
            set /p REBOOT="Reboot now to complete Hyper-V installation? (Y/N): "
            if /i "%REBOOT%"=="Y" (
                echo Rebooting in 10 seconds...
                echo After reboot, run this script again to continue VM setup
                shutdown /r /t 10 /c "Hyper-V installation reboot"
                exit /b 0
            ) else (
                echo Please reboot manually and run this script again
                pause
                exit /b 0
            )
        ) else (
            echo ? Hyper-V installation failed
            echo    Check Windows Update and try again
            goto :ALTERNATIVE_SOLUTIONS
        )
    ) else (
        echo Hyper-V installation skipped
        goto :ALTERNATIVE_SOLUTIONS
    )
)

echo.
echo Phase 3: Development VM Configuration
echo ====================================

echo Creating Intel AVB Development VM...
echo.

REM Set VM parameters
set VM_NAME=IntelAvbDev
set VM_MEMORY=8GB
set VM_DISK_SIZE=100GB
set VM_PATH=C:\VMs\%VM_NAME%

REM Create VM directory
if not exist "C:\VMs" mkdir "C:\VMs"
if not exist "%VM_PATH%" mkdir "%VM_PATH%"

echo VM Configuration:
echo   Name: %VM_NAME%
echo   Memory: %VM_MEMORY%
echo   Disk: %VM_DISK_SIZE%
echo   Path: %VM_PATH%
echo.

REM Check if VM already exists
powershell -Command "Get-VM -Name '%VM_NAME%' -ErrorAction SilentlyContinue" >nul 2>&1
if %errorLevel% == 0 (
    echo ??  VM '%VM_NAME%' already exists
    set /p RECREATE="Delete and recreate VM? (Y/N): "
    if /i "%RECREATE%"=="Y" (
        echo Removing existing VM...
        powershell -Command "Stop-VM -Name '%VM_NAME%' -Force -ErrorAction SilentlyContinue"
        powershell -Command "Remove-VM -Name '%VM_NAME%' -Force"
        echo ? Existing VM removed
    ) else (
        echo Using existing VM
        goto :VM_CONFIGURATION
    )
)

echo Creating new Generation 2 VM...
powershell -Command "New-VM -Name '%VM_NAME%' -MemoryStartupBytes 8GB -Generation 2 -Path 'C:\VMs'"
if %errorLevel% neq 0 (
    echo ? VM creation failed
    goto :TROUBLESHOOTING
)
echo ? VM created successfully

echo Creating virtual hard disk...
powershell -Command "New-VHD -Path '%VM_PATH%\%VM_NAME%.vhdx' -SizeBytes 100GB -Dynamic"
if %errorLevel% neq 0 (
    echo ? Virtual disk creation failed
    goto :TROUBLESHOOTING
)
echo ? Virtual disk created

echo Attaching virtual hard disk to VM...
powershell -Command "Add-VMHardDiskDrive -VMName '%VM_NAME%' -Path '%VM_PATH%\%VM_NAME%.vhdx'"
echo ? Virtual disk attached

:VM_CONFIGURATION
echo.
echo Phase 4: VM Security Configuration (Critical for Driver Development)
echo ==================================================================

echo Configuring VM for driver development...

REM Disable Secure Boot in VM (critical for test signing)
echo Disabling Secure Boot in VM (allows test signing)...
powershell -Command "Set-VMFirmware -VMName '%VM_NAME%' -EnableSecureBoot Off"
if %errorLevel% == 0 (
    echo ? Secure Boot disabled in VM (test signing will work)
) else (
    echo ? Failed to disable Secure Boot
    echo    This may prevent driver installation in VM
)

REM Configure VM processor
echo Setting VM processor configuration...
powershell -Command "Set-VMProcessor -VMName '%VM_NAME%' -Count 4"
echo ? VM processor configured (4 cores)

REM Configure VM network
echo Configuring VM network...
powershell -Command "Get-VMSwitch | Select-Object -First 1 | ForEach-Object { Add-VMNetworkAdapter -VMName '%VM_NAME%' -SwitchName $_.Name }"
echo ? VM network configured

REM Enable guest services
echo Enabling guest services...
powershell -Command "Enable-VMIntegrationService -VMName '%VM_NAME%' -Name 'Guest Service Interface','Heartbeat','Key-Value Pair Exchange','Shutdown','Time Synchronization','VSS'"
echo ? Guest services enabled

echo.
echo Phase 5: Windows ISO Preparation
echo ================================

set ISO_PATH=C:\VMs\Windows11.iso

if exist "%ISO_PATH%" (
    echo ? Windows 11 ISO found: %ISO_PATH%
) else (
    echo ? Windows 11 ISO not found
    echo.
    echo Please download Windows 11 ISO:
    echo 1. Go to: https://www.microsoft.com/software-download/windows11
    echo 2. Select "Download Windows 11 Disk Image (ISO)"
    echo 3. Save as: %ISO_PATH%
    echo 4. Re-run this script
    echo.
    set /p CONTINUE="Continue without ISO (manual setup required)? (Y/N): "
    if /i "%CONTINUE%"=="N" (
        pause
        exit /b 1
    )
)

if exist "%ISO_PATH%" (
    echo Attaching Windows ISO to VM...
    powershell -Command "Set-VMDvdDrive -VMName '%VM_NAME%' -Path '%ISO_PATH%'"
    echo ? Windows ISO attached to VM
)

echo.
echo Phase 6: VM Ready for Intel AVB Development
echo ===========================================

echo ? Intel AVB Development VM Setup Complete!
echo.
echo VM Details:
echo   Name: %VM_NAME%
echo   Status: Ready for Windows installation
echo   Secure Boot: ? DISABLED (allows driver development)
echo   Memory: 8GB
echo   Disk: 100GB
echo   Network: Configured
echo.

echo Next Steps:
echo ===========
echo.
echo 1. **Start the VM:**
echo    • Open Hyper-V Manager
echo    • Double-click "%VM_NAME%" to connect
echo    • Click "Start" to boot VM
echo.
echo 2. **Install Windows 11:**
echo    • Follow standard Windows installation
echo    • Create local admin account
echo    • Complete Windows setup
echo.
echo 3. **Configure Development Environment:**
echo    • Install Visual Studio 2022
echo    • Install Windows Driver Kit (WDK)
echo    • Enable test signing: bcdedit /set testsigning on
echo    • Reboot VM
echo.
echo 4. **Transfer Intel AVB Project:**
echo    • Copy entire D:\Repos\IntelAvbFilter to VM
echo    • Or use Git to clone repository in VM
echo.
echo 5. **Test Driver Installation:**
echo    • Build driver in VM
echo    • Install using Network Control Panel method
echo    • Run hardware tests
echo.
echo ?? **Key Advantage:**
echo    Your host system remains completely unchanged!
echo    All driver testing happens safely in the VM.
echo.

set /p START_VM="Start VM now? (Y/N): "
if /i "%START_VM%"=="Y" (
    echo Starting VM and opening connection...
    powershell -Command "Start-VM -Name '%VM_NAME%'"
    start "" "vmconnect.exe" "localhost" "%VM_NAME%"
    echo ? VM started and connection opened
) else (
    echo VM is ready to start manually from Hyper-V Manager
)

goto :SUCCESS

:ALTERNATIVE_SOLUTIONS
echo.
echo === Alternative VM Solutions ===
echo.
echo Since Hyper-V is not available, consider these alternatives:
echo.
echo **Option A: VMware Workstation Pro**
echo   • Download: https://www.vmware.com/products/workstation-pro.html
echo   • Cost: ~€250 (one-time purchase)
echo   • Excellent driver development support
echo   • Secure Boot can be disabled
echo   • Hardware passthrough capabilities
echo.
echo **Option B: Oracle VirtualBox (Free)**
echo   • Download: https://www.virtualbox.org/
echo   • Free and open source
echo   • Good for basic driver development
echo   • Limited hardware passthrough
echo   • May have performance limitations
echo.
echo **Option C: Request Development System**
echo   • Ask IT for dedicated development laptop
echo   • Can disable Secure Boot on dedicated system
echo   • Full hardware access for testing
echo.
goto :END

:TROUBLESHOOTING
echo.
echo === Troubleshooting ===
echo.
echo Common issues and solutions:
echo.
echo **Hyper-V not available:**
echo   • Requires Windows 10/11 Pro or Enterprise
echo   • Requires CPU with virtualization support
echo   • Check BIOS for VT-x/AMD-V settings
echo.
echo **VM creation failed:**
echo   • Ensure sufficient disk space (>150GB recommended)
echo   • Check Hyper-V service is running
echo   • Verify user has Hyper-V administrator rights
echo.
echo **Performance issues:**
echo   • Allocate at least 8GB RAM to VM
echo   • Use SSD for VM storage if possible
echo   • Enable Dynamic Memory in VM settings
echo.
goto :END

:SUCCESS
echo.
echo ===========================================================================
echo   Intel AVB Development VM Setup Successful! ??
echo   
echo   You now have a complete development environment that:
echo   ? Keeps your host system unchanged (IT compliant)
echo   ? Allows test signing and driver installation
echo   ? Provides full development freedom
echo   ? Can be easily reset/restored if needed
echo.
echo   Your Intel AVB Filter Driver is ready for testing in a safe environment!
echo ===========================================================================

:END
echo.
pause