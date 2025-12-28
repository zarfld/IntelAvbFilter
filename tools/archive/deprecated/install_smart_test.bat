@echo off
echo === Intel AVB Filter Driver - Smart Installation Test ===
echo This method auto-detects file locations and works with Secure Boot
echo.

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"
echo Current directory: %CD%

echo.
echo Step 1: Checking Administrator privileges...
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
echo Step 2: Locating driver files...
set DRIVER_FOUND=0

REM Check multiple possible locations for driver files
if exist "..\..\x64\Debug\IntelAvbFilter.sys" (
    set DRIVER_PATH=..\..\x64\Debug
    set DRIVER_FOUND=1
    echo ? Found driver files in: x64\Debug\
) else if exist "..\..\Debug\IntelAvbFilter.sys" (
    set DRIVER_PATH=..\..\Debug
    set DRIVER_FOUND=1
    echo ? Found driver files in: Debug\
) else if exist "IntelAvbFilter.sys" (
    set DRIVER_PATH=.
    set DRIVER_FOUND=1
    echo ? Found driver files in current directory
)

if %DRIVER_FOUND%==0 (
    echo ? Driver files not found. Build the project first.
    echo Looking for: IntelAvbFilter.sys, IntelAvbFilter.inf, IntelAvbFilter.cer
    pause
    exit /b 1
)

echo.
echo Step 3: Verifying driver files...
if exist "%DRIVER_PATH%\IntelAvbFilter.sys" (
    echo ? Driver binary: %DRIVER_PATH%\IntelAvbFilter.sys
) else (
    echo ? Missing: IntelAvbFilter.sys
    pause
    exit /b 1
)

if exist "%DRIVER_PATH%\IntelAvbFilter.inf" (
    echo ? Driver INF: %DRIVER_PATH%\IntelAvbFilter.inf
) else (
    echo ? Missing: IntelAvbFilter.inf
    pause
    exit /b 1
)

if exist "%DRIVER_PATH%\IntelAvbFilter.cer" (
    echo ? Certificate: %DRIVER_PATH%\IntelAvbFilter.cer
) else (
    echo ??  Certificate not found - will try without it
)

echo.
echo Step 4: Checking Intel I219 hardware...
wmic path Win32_PnPEntity where "DeviceID like '%%VEN_8086%%' and DeviceID like '%%DEV_0DC7%%'" get Name,DeviceID,Status | findstr "I219"
if %errorLevel% == 0 (
    echo ? Intel I219 controller confirmed
) else (
    echo ??  I219 not found, but continuing...
)

echo.
echo Step 5: Installing certificate (if available)...
if exist "%DRIVER_PATH%\IntelAvbFilter.cer" (
    certutil -addstore root "%DRIVER_PATH%\IntelAvbFilter.cer" >nul 2>&1
    if %errorLevel% == 0 (
        echo ? Certificate installed to Trusted Root store
    ) else (
        echo ??  Certificate installation failed, but continuing...
    )
) else (
    echo ??  No certificate available
)

echo.
echo Step 6: Installing driver via pnputil...
pnputil /add-driver "%DRIVER_PATH%\IntelAvbFilter.inf" /install
if %errorLevel% == 0 (
    echo ? Driver installation SUCCESSFUL!
    echo.
    echo Driver should now be loaded. You can verify in Device Manager.
    goto :TEST_DRIVER
) else (
    echo ? pnputil installation failed
    echo.
    echo Trying alternative methods...
    goto :ALTERNATIVE_METHODS
)

:TEST_DRIVER
echo.
echo Step 7: Compiling test application (if needed)...
if not exist "avb_test_i219.exe" (
    if exist "avb_test_i219.c" (
        echo Compiling test application...
        cl avb_test_i219.c /Fe:avb_test_i219.exe >nul 2>&1
        if exist "avb_test_i219.exe" (
            echo ? Test application compiled
        ) else (
            echo ? Test compilation failed
            echo Try manually: cl avb_test_i219.c /Fe:avb_test_i219.exe
        )
    ) else (
        echo ? Test source not found: avb_test_i219.c
    )
)

echo.
echo Step 8: Running I219 hardware test...
if exist "avb_test_i219.exe" (
    echo === STARTING HARDWARE TEST ===
    avb_test_i219.exe
    echo === TEST COMPLETE ===
) else (
    echo ? Test application not available
)

goto :DEBUG_INFO

:ALTERNATIVE_METHODS
echo.
echo === Alternative Installation Methods ===
echo.
echo Method A: DevCon Installation
where devcon.exe >nul 2>&1
if %errorLevel% == 0 (
    echo ? DevCon available - attempting installation...
    devcon install "%DRIVER_PATH%\IntelAvbFilter.inf" "*"
) else if exist "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe" (
    echo ? Found WDK DevCon - attempting installation...
    "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe" install "%DRIVER_PATH%\IntelAvbFilter.inf" "*"
) else (
    echo ? DevCon not found
)

echo.
echo Method B: Manual Device Manager Installation
echo 1. Open Device Manager (devmgmt.msc)
echo 2. Find your Intel I219 network adapter
echo 3. Right-click ? Update Driver ? Browse ? Have Disk
echo 4. Point to: %CD%\%DRIVER_PATH%\IntelAvbFilter.inf
echo 5. Install the Intel AVB Filter Driver

:DEBUG_INFO
echo.
echo === Debug Information Setup ===
echo.
echo To monitor driver activity:
echo 1. Download DebugView.exe from Microsoft Sysinternals
echo 2. Run DebugView as Administrator
echo 3. Options ? Capture Kernel (enable)
echo 4. Look for messages containing "Avb" or "IntelAvb"
echo.
echo Expected success messages:
echo - "Real hardware access enabled"
echo - "AvbMmioReadReal: offset=0x... (REAL HARDWARE)"
echo.
echo If you see these, your I219 hardware access is working!
echo.
pause