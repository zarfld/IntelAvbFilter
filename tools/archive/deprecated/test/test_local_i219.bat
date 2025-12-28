@echo off
echo === Intel AVB Filter Driver - Windows 11 Local Test ===
echo.

echo Step 1: Checking if running as Administrator...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running as Administrator
) else (
    echo ? ERROR: This script must be run as Administrator
    echo    Right-click Command Prompt and select "Run as administrator"
    pause
    exit /b 1
)

echo.
echo Step 2: Checking test signing...
bcdedit /enum | find "testsigning" | find "Yes" >nul
if %errorLevel% == 0 (
    echo ? Test signing is enabled
) else (
    echo ??  Test signing not enabled. Enabling now...
    bcdedit /set testsigning on
    echo ? Test signing enabled - REBOOT REQUIRED
    echo    Please reboot and run this script again
    pause
    exit /b 1
)

echo.
echo Step 3: Identifying Intel I219 controller...
wmic path Win32_NetworkAdapter where "Manufacturer like '%%Intel%%' and Name like '%%I219%%'" get Name,PNPDeviceID
if %errorLevel% == 0 (
    echo ? Intel controller found
) else (
    echo ? No Intel I219 controller found
    echo    Make sure you have an Intel I219 network adapter
)

echo.
echo Step 4: Checking if driver files exist...
if exist "ndislwf.sys" (
    echo ? Driver binary found: ndislwf.sys
) else (
    echo ? Driver binary not found: ndislwf.sys
    echo    Build the project first in Visual Studio
    pause
    exit /b 1
)

if exist "IntelAvbFilter.inf" (
    echo ? Driver INF found: IntelAvbFilter.inf
) else (
    echo ? Driver INF not found: IntelAvbFilter.inf
    echo    Make sure all driver files are in current directory
    pause
    exit /b 1
)

echo.
echo Step 5: Installing driver...
pnputil /add-driver IntelAvbFilter.inf /install
if %errorLevel% == 0 (
    echo ? Driver installation successful
) else (
    echo ? Driver installation failed
    echo    Check the error messages above
    pause
    exit /b 1
)

echo.
echo Step 6: Compiling test application...
if exist "avb_test_i219.c" (
    cl avb_test_i219.c /Fe:avb_test_i219.exe >nul 2>&1
    if exist "avb_test_i219.exe" (
        echo ? Test application compiled successfully
    ) else (
        echo ? Test application compilation failed
        echo    Make sure Visual Studio tools are in PATH
        pause
        exit /b 1
    )
) else (
    echo ? Test source not found: avb_test_i219.c
    pause
    exit /b 1
)

echo.
echo Step 7: Running I219 hardware test...
echo === STARTING HARDWARE TEST ===
avb_test_i219.exe

echo.
echo === Test Complete ===
echo.
echo If you see "REAL HARDWARE" messages above, your I219 is working!
echo If you see "SIMULATED" messages, check debug output with DebugView.exe
echo.
pause