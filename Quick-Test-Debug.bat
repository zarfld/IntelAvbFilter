@echo off
REM Quick reload and test script - Run as Administrator
echo ========================================
echo Intel AVB Filter - Quick Reload for Debugging
echo ========================================
echo.

REM Check admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Administrator privileges required
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

echo [1] Unbinding filter from adapters...
netcfg -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo [2] Stopping service...
net stop IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo [3] Installing new driver (DEBUG BUILD)...
netcfg -v -l x64\Debug\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
if %errorLevel% neq 0 (
    echo ERROR: Installation failed
    pause
    exit /b 1
)

echo [4] Waiting for service to start...
timeout /t 3 /nobreak >nul

echo [5] Running test...
echo.
avb_test_i226.exe selftest

echo.
echo ========================================
echo Check DebugView for these messages:
echo   !!! DEBUG: AvbBringUpHardware hw_state=...
echo   !!! DEBUG: Calling intel_init()...
echo   !!! DEBUG: PlatformInitWrapper called!
echo   !!! DEBUG: AvbPlatformInit ENTERED!
echo ========================================
pause
