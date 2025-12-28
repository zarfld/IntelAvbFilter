@echo off
REM Proper NDIS Filter Installation using netcfg
echo === Intel AVB Filter - Proper NDIS Installation ===
echo.

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Must run as Administrator!
    pause
    exit /b 1
)

cd /d "%~dp0"

echo Step 1: Stopping service if running...
sc stop IntelAvbFilter >nul 2>&1

echo Step 2: Removing old installations...
netcfg.exe -u MS_IntelAvbFilter

echo Step 3: Installing filter component via netcfg...
netcfg.exe -l "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.inf" -c s -i MS_IntelAvbFilter

if %errorLevel% NEQ 0 (
    echo.
    echo ERROR: netcfg installation failed!
    echo Error code: %errorLevel%
    pause
    exit /b 1
)

echo.
echo Step 4: Starting service...
sc start IntelAvbFilter

echo.
echo Step 5: Checking service status...
sc query IntelAvbFilter

echo.
echo Installation complete!
echo Check DebugView for driver messages.
pause
