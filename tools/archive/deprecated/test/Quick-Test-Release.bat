@echo off
REM Quick test script for Release build - Updated version with proper package copy
echo ========================================
echo Intel AVB Filter - Release Build Test
echo ========================================
echo.

REM Detect if running from repo root or tools\test
if exist "tools\test\Quick-Test-Release.bat" (
    REM Running from repo root
    set SYS_SRC=build\x64\Release\IntelAvbFilter.sys
    set SYS_DST=build\x64\Release\IntelAvbFilter\IntelAvbFilter.sys
    set INF_PATH=build\x64\Release\IntelAvbFilter\IntelAvbFilter.inf
    set TEST_EXE=build\tools\avb_test\x64\Release\avb_test_i226.exe
) else (
    REM Running from tools\test
    set SYS_SRC=..\..\build\x64\Release\IntelAvbFilter.sys
    set SYS_DST=..\..\build\x64\Release\IntelAvbFilter\IntelAvbFilter.sys
    set INF_PATH=..\..\build\x64\Release\IntelAvbFilter\IntelAvbFilter.inf
    set TEST_EXE=..\..\build\tools\avb_test\x64\Release\avb_test_i226.exe
)

REM Step 0: Copy actual build to package directory
echo [0] Updating package with latest Release build...
copy /Y "%SYS_SRC%" "%SYS_DST%" >nul
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
netcfg -v -l "%INF_PATH%" -c s -i MS_IntelAvbFilter
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
if exist "%TEST_EXE%" (
    "%TEST_EXE%"
) else (
    echo ERROR: Test executable not found - build tests first
    echo Expected: %TEST_EXE%
)
echo.

echo ========================================
echo Test Complete - Release Build
echo ========================================
pause
