@echo off
REM Quick test script for Release build - Updated version with proper package copy
echo ========================================
echo Intel AVB Filter - Release Build Test
echo ========================================
echo.

REM Step 0: Copy actual build to package directory
echo [0] Updating package with latest Release build...
copy /Y x64\Release\IntelAvbFilter.sys x64\Release\IntelAvbFilter\IntelAvbFilter.sys >nul
if errorlevel 1 (
    echo ERROR: Failed to copy Release build!
    pause
    exit /b 1
)
echo     Package updated: x64\Release\IntelAvbFilter\IntelAvbFilter.sys
echo.

REM Step 1: Unbind filter
echo [1] Unbinding filter...
netcfg -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

REM Step 2: Stop service
echo [2] Stopping service...
net stop IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

REM Step 3: Install filter
echo [3] Installing Release build...
netcfg -v -l x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
if errorlevel 1 (
    echo ERROR: Installation failed!
    pause
    exit /b 1
)
echo     Installation successful
timeout /t 3 /nobreak >nul

REM Step 4: Run tests
echo.
echo ========================================
echo [4] Running AVB Tests...
echo ========================================
avb_test_i226.exe
echo.

echo ========================================
echo Test Complete - Release Build
echo ========================================
pause
