@echo off
echo ============================================
echo Install Freshly Built Driver
echo ============================================
echo.

:: Check for admin
net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Must run as Administrator!
    echo Right-click this file and select "Run as administrator"
    pause
    exit /b 1
)

echo [1] Complete cleanup of old driver...
sc stop IntelAvbFilter >nul 2>&1
sc delete IntelAvbFilter >nul 2>&1
netcfg -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul
echo     Cleanup complete.
echo.

echo [2] Installing newly built driver...
echo     INF: x64\Release\IntelAvbFilter\IntelAvbFilter.inf
netcfg -v -l ..\..\x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

if %errorLevel% EQU 0 (
    echo.
    echo [3] SUCCESS! Checking service status...
    sc query IntelAvbFilter
    echo.
    echo [4] Testing with avb_test_i226.exe...
    if exist avb_test_i226.exe (
        avb_test_i226.exe caps
    ) else (
        echo     Test tool not found. Build it first.
    )
) else (
    echo.
    echo [3] FAILED! Error code: %errorLevel%
    echo.
    echo Common error codes:
    echo   5 (0x80070005) = Access Denied - Run as Administrator
    echo   430 (0x80070430) = Element not found - Driver binary issue
    echo   1275 = Driver load failed - Check dependencies
)

echo.
pause
