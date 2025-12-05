@echo off
echo ========================================
echo Intel AVB Filter Driver - Quick Setup
echo ========================================
echo.

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Administrator privileges required
    echo.
    echo Right-click this file and select "Run as administrator"
    echo.
    pause
    exit /b 1
)

echo [1] Checking test signing mode...
bcdedit /enum {current} | findstr /i "testsigning" | findstr /i "Yes" >nul
if %errorLevel% EQU 0 (
    echo     Status: ENABLED
    goto :InstallDriver
) else (
    echo     Status: DISABLED
    echo.
    echo Test signing mode is required for development drivers.
    echo This allows Windows to load your self-signed driver.
    echo.
    echo What this does:
    echo   - Adds "Test Mode" watermark to desktop corners
    echo   - Allows loading of test-signed drivers
    echo   - Requires reboot to take effect
    echo.
    set /p response="Enable test signing mode? (y/n): "
    if /i "%response%" NEQ "y" (
        echo.
        echo Cannot continue without test signing mode.
        echo Alternative: Purchase EV code signing certificate (~$300/year^)
        pause
        exit /b 1
    )
    
    echo.
    echo [2] Enabling test signing mode...
    bcdedit /set testsigning on
    if %errorLevel% NEQ 0 (
        echo     ERROR: Failed to enable test signing
        echo.
        echo Possible causes:
        echo   - Secure Boot is enabled (disable in BIOS^)
        echo   - BitLocker is active
        echo   - Group Policy restrictions
        echo.
        pause
        exit /b 1
    )
    
    echo     SUCCESS: Test signing enabled
    echo.
    echo ========================================
    echo REBOOT REQUIRED
    echo ========================================
    echo.
    echo Press any key to reboot now, or close to reboot later...
    pause >nul
    shutdown /r /t 5 /c "Rebooting to enable test signing mode"
    exit /b 0
)

:InstallDriver
echo.
echo [2] Checking for driver build...
if not exist "x64\Release\IntelAvbFilter\IntelAvbFilter.sys" (
    echo     ERROR: Release build not found
    echo     Build the driver first: msbuild /p:Configuration=Release;Platform=x64
    pause
    exit /b 1
)
echo     Found: x64\Release\IntelAvbFilter\IntelAvbFilter.sys
echo.

echo [3] Removing old driver installation...
echo     Stopping service...
sc stop IntelAvbFilter >nul 2>&1
sc delete IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo     Uninstalling old filter bindings...
netcfg -u MS_IntelAvbFilter >nul 2>&1

echo     Removing cached drivers...
for /f "tokens=1" %%i in ('pnputil /enum-drivers ^| findstr /C:"intelavbfilter.inf"') do (
    pnputil /delete-driver %%i /uninstall /force >nul 2>&1
)

del /q %SystemRoot%\System32\drivers\IntelAvbFilter.sys 2>nul
timeout /t 2 /nobreak >nul
echo.

echo [4] Installing NDIS filter driver (using netcfg)...
echo     IMPORTANT: NDIS filters must use netcfg, not pnputil!
netcfg -v -l x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

if %errorLevel% NEQ 0 (
    echo     ERROR: netcfg failed!
    echo.
    echo     This is the required method for NDIS filter drivers.
    echo     Make sure:
    echo       1. You're running as Administrator
    echo       2. The INF contains NetCfgInstanceId in [Install] section
    echo       3. No other instances are running
    pause
    exit /b 1
)
echo     SUCCESS: Filter driver installed and bound to network adapters
echo.

echo [5] Verifying service registration...
sc start IntelAvbFilter
echo.

echo [6] Checking service status...
sc query IntelAvbFilter
echo.

echo [7] Testing device interface...
timeout /t 2 /nobreak >nul

REM Try I226-specific test first, then fall back to I210 test
if exist "avb_test_i226.exe" (
    echo     Running I226 test...
    avb_test_i226.exe caps
) else if exist "avb_test_i210_um.exe" (
    echo     Running test...
    echo     Note: Test tool works with all Intel controllers (I210/I219/I225/I226^)
    avb_test_i210_um.exe caps
) else (
    echo     Test application not found
    echo     Build it first: build_i226_test.bat
    goto :EOF
)

if %errorLevel% EQU 0 (
    echo.
    echo ========================================
    echo SUCCESS! Driver is working with your I226 adapters!
    echo ========================================
    echo.
    if exist "avb_test_i226.exe" (
        echo Run full test suite:
        echo   avb_test_i226.exe selftest
    ) else (
        echo Run full test suite:
        echo   avb_test_i210_um.exe selftest
    )
    echo.
    echo Your I226 adapters support:
    echo   - IEEE 1588 PTP
    echo   - Time-Aware Shaper (TAS^)
    echo   - Frame Preemption (FP^)
    echo   - PCIe PTM
    echo.
) else (
    echo.
    echo Device interface not accessible yet.
    echo Try rebooting if this persists.
    echo.
)

echo Setup complete!
pause
