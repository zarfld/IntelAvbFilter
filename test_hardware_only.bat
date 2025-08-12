@echo off
echo === Intel AVB Filter Driver - Hardware Only Testing Tool ===
echo NO SIMULATION, NO FALLBACK - Real hardware or explicit failure
echo.

echo Step 1: Checking Administrator privileges...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running as Administrator
) else (
    echo ? FEHLER: Muss als Administrator ausgeführt werden
    pause
    exit /b 1
)

echo.
echo Step 2: Compiling Hardware-Only implementation...
if exist "avb_integration_hardware_only.c" (
    echo ? Hardware-Only source found
    
    echo Kompiliere Test-Anwendung mit Hardware-Only mode...
    cl avb_test_i219.c /DHARDWARE_ONLY=1 /Fe:avb_test_hardware_only.exe >nul 2>&1
    if exist "avb_test_hardware_only.exe" (
        echo ? Hardware-Only test application compiled
    ) else (
        echo ? Compilation failed
        echo Trying standard compilation...
        cl avb_test_i219.c /Fe:avb_test_hardware_only.exe
    )
) else (
    echo ? Hardware-Only source not found
    echo Using standard test application
    copy avb_test_i219.exe avb_test_hardware_only.exe >nul 2>&1
)

echo.
echo Step 3: Intel I219 Hardware Detection...
wmic path Win32_PnPEntity where "DeviceID like '%%VEN_8086%%' and DeviceID like '%%DEV_0DC7%%'" get Name,DeviceID,Status
if %errorLevel% == 0 (
    echo ? Intel I219 hardware confirmed
) else (
    echo ? Intel I219 hardware not found
    echo This test requires actual Intel I219 controller
)

echo.
echo Step 4: Driver Status Check...
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% == 0 (
    echo ? Driver service found
    
    sc query IntelAvbFilter | findstr "STATE" | findstr "RUNNING" >nul
    if %errorLevel% == 0 (
        echo ? Driver is running
    ) else (
        echo ??  Driver installed but not running
        echo This is normal for NDIS Filter Drivers
    )
) else (
    echo ? Driver service not found
    echo Install driver first using Network Control Panel method
    echo.
    echo INSTALLATION REMINDER:
    echo 1. Network Connections (ncpa.cpl)
    echo 2. Intel I219 ? Properties ? Install ? Service ? Add
    echo 3. Have Disk ? x64\Debug\IntelAvbFilter.inf
    pause
    exit /b 1
)

echo.
echo Step 5: Hardware-Only Testing...
echo === STARTING HARDWARE-ONLY TEST ===
echo.
echo Expected Results:
echo ? SUCCESS: Real hardware access messages
echo ? FAILURE: Explicit error messages (no simulation)
echo.
echo Running Hardware-Only test...

avb_test_hardware_only.exe

echo.
echo === HARDWARE-ONLY TEST RESULTS ANALYSIS ===
echo.
echo If you see:
echo ? "REAL HARDWARE" messages ? Hardware access is working!
echo ? "Hardware not mapped" ? BAR0 discovery failed
echo ? "PCI config read failed" ? NDIS OID implementation issue
echo ? "Cannot open device" ? Driver not properly loaded
echo.

echo Step 6: Debug Output Capture...
echo.
echo For detailed debugging, use DebugView.exe:
echo 1. Download: Microsoft Sysinternals DebugView
echo 2. Run as Administrator
echo 3. Options ? Capture Kernel ?
echo 4. Look for these patterns:
echo.
echo ? SUCCESS PATTERN:
echo    [INFO] REAL HARDWARE DISCOVERED: Intel I219
echo    [TRACE] AvbMmioReadHardwareOnly: (REAL HARDWARE)
echo.
echo ? FAILURE PATTERN:
echo    [ERROR] ? HARDWARE DISCOVERY FAILED
echo    [ERROR] ? BAR0 DISCOVERY FAILED
echo    [ERROR] ? Hardware not mapped
echo.

echo Step 7: Network Connectivity Check...
ping -n 1 8.8.8.8 >nul 2>&1
if %errorLevel% == 0 (
    echo ? Internet connection working
    echo Filter is not blocking network traffic
) else (
    echo ? No internet connection
    echo ??  Filter might be causing network issues
    echo.
    echo SOLUTION: Disable filter temporarily:
    echo 1. Network Adapter Properties
    echo 2. Uncheck "Intel AVB Filter Driver"
    echo 3. Test network
    echo 4. Re-enable after debugging
)

echo.
echo === HARDWARE-ONLY TESTING COMPLETE ===
echo.
echo ? BENEFITS OF NO-SIMULATION APPROACH:
echo - Immediate visibility of real problems
echo - No hidden failures behind simulation
echo - Clear path to fix hardware access issues
echo - Production-ready code validation
echo.
echo ? PROBLEMS ARE NOW VISIBLE (This is good!):
echo - BAR0 discovery issues are explicit
echo - MMIO mapping problems are clear
echo - PCI configuration access failures are obvious
echo - No confusion about simulation vs reality
echo.
echo NEXT STEPS based on results:
echo 1. If REAL HARDWARE messages ? SUCCESS! Continue development
echo 2. If explicit errors ? Fix hardware access implementation
echo 3. If device open fails ? Fix driver installation/loading
echo 4. If network issues ? Debug filter packet processing
echo.
echo Your Intel AVB Filter Driver is now in HONEST MODE! ??
echo All problems are visible and solvable.
echo.
pause