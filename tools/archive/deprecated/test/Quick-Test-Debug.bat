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

REM Detect if running from repo root or tools\test
set REPO_ROOT=.
if exist "tools\test\Quick-Test-Debug.bat" (
    REM Running from repo root
    set INF_PATH=build\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf
    set TEST_EXE=build\tools\avb_test\x64\Debug\avb_test_i226.exe
) else (
    REM Running from tools\test
    set INF_PATH=..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.inf
    set TEST_EXE=..\..\build\tools\avb_test\x64\Debug\avb_test_i226.exe
)

echo [1] Unbinding filter from adapters...
netcfg -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo [2] Stopping service...
net stop IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo [3] Installing new driver (DEBUG BUILD)...
netcfg -v -l "%INF_PATH%" -c s -i MS_IntelAvbFilter
if %errorLevel% neq 0 (
    echo ERROR: Installation failed
    pause
    exit /b 1
)

echo [4] Waiting for service to start...
timeout /t 3 /nobreak >nul

echo [5] Running test...
echo.
if exist "%TEST_EXE%" (
    "%TEST_EXE%" selftest
) else (
    echo ERROR: Test executable not found - build tests first
    echo Expected: %TEST_EXE%
)

echo.
echo ========================================
echo Check DebugView for these messages:
echo   !!! DEBUG: AvbBringUpHardware hw_state=...
echo   !!! DEBUG: Calling intel_init()...
echo   !!! DEBUG: PlatformInitWrapper called!
echo   !!! DEBUG: AvbPlatformInit ENTERED!
echo ========================================
pause
