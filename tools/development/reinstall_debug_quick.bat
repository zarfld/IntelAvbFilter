@echo off
REM Quick reinstall of Debug driver with registry diagnostic
REM Must run as Administrator

echo Reinstalling Debug Driver...
echo.

REM Check admin
net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Must run as Administrator
    pause
    exit /b 1
)

cd /d "%~dp0"

:: Navigate to repository root
if exist "tools\development" (
    cd /d "%~dp0..\.."
) else (
    cd /d "%~dp0"
)

echo [1/4] Unbinding from adapters...
pnputil /disable-device "PCI\VEN_8086&DEV_125B" >nul 2>&1

echo [2/4] Removing old driver...
pnputil /delete-driver intelavbfilter.inf /uninstall /force >nul 2>&1

echo [3/4] Installing new Debug driver...
pnputil /add-driver x64\Debug\IntelAvbFilter\IntelAvbFilter.inf /install

echo [4/4] Re-enabling adapters...
pnputil /enable-device "PCI\VEN_8086&DEV_125B" >nul 2>&1

echo.
echo Done! Driver timestamp should now be:
dir /TW "build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys" | findstr IntelAvbFilter.sys
echo.
echo Verify with: sc query IntelAvbFilter
echo.
pause
