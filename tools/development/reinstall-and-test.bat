@echo off
REM Quick reinstall and test script for Intel AVB Filter Driver

:: Navigate to repository root
if exist "tools\development" (
    cd /d "%~dp0..\.."
) else (
    cd /d "%~dp0"
)

echo ============================================================================
echo Intel AVB Filter Driver - Reinstall and Test
echo ============================================================================
echo.

echo [1/4] Stopping and removing old driver...
sc stop IntelAvbFilter 2>nul
sc delete IntelAvbFilter 2>nul
timeout /t 2 /nobreak >nul

echo [2/4] Installing new driver...
call Install-Debug-Driver.bat
if errorlevel 1 (
    echo ERROR: Driver installation failed!
    pause
    exit /b 1
)

echo.
echo [3/4] Starting driver...
sc start IntelAvbFilter
if errorlevel 1 (
    echo ERROR: Driver start failed!
    pause
    exit /b 1
)

echo.
echo [4/4] Running tests...
echo.
echo ========== TEST 1: Minimal enumeration test ==========
test_minimal.exe
echo.

echo ========== TEST 2: Direct clock config test ==========
test_direct_clock.exe
echo.

echo ========== TEST 3: Clock config with OPEN test ==========
test_clock_config.exe
echo.

echo ============================================================================
echo Tests complete! Check output above for capabilities.
echo Expected: Caps=0x000001BF (not 0x00000000)
echo ============================================================================
pause
