@echo off
REM Quick driver update - handles stuck services

:: Navigate to repository root
if exist "tools\development" (
    cd /d "%~dp0..\.."
) else (
    cd /d "%~dp0"
)

echo Updating Intel AVB Filter Driver...
echo.

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Run as Administrator
    pause
    exit /b 1
)

echo [1] Force stop service...
sc stop IntelAvbFilter
timeout /t 1 /nobreak >nul

echo [2] Uninstall NDIS filter...
netcfg -u MS_IntelAvbFilter
timeout /t 2 /nobreak >nul

echo [3] Install updated driver...
cd /d "%~dp0x64\Release\IntelAvbFilter"
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

if %errorLevel% EQU 0 (
    echo.
    echo SUCCESS! Driver updated.
    timeout /t 2 /nobreak >nul
    sc query IntelAvbFilter
    echo.
    cd /d "%~dp0"
    echo Testing...
    avb_test_i226.exe selftest
) else (
    echo.
    echo FAILED: Error %errorLevel%
)

pause
