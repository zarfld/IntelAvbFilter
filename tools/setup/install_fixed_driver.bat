@echo off
REM Complete cleanup and reinstall of IntelAvbFilter
echo ========================================
echo IntelAvbFilter - Complete Reinstall
echo Critical Fix: NetCfgInstanceId in [Install] section
echo ========================================
echo.

echo [1/5] Stopping service and unloading driver...
sc stop IntelAvbFilter >nul 2>&1
sc delete IntelAvbFilter >nul 2>&1
timeout /t 3 /nobreak >nul

echo [2/5] Removing ALL old driver packages...
for /f "tokens=1" %%i in ('pnputil /enum-drivers ^| findstr /C:"intelavbfilter.inf"') do (
    echo      Removing %%i...
    pnputil /delete-driver %%i /uninstall /force 2>nul
)

echo [3/5] Clearing driver cache...
del /q %SystemRoot%\System32\drivers\IntelAvbFilter.sys 2>nul
del /q %SystemRoot%\System32\DriverStore\FileRepository\intelavbfilter*\* /s 2>nul
timeout /t 2 /nobreak >nul

echo [4/5] Installing new driver with NetCfgInstanceId fix...
netcfg -v -l ..\..\x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: netcfg failed! Trying pnputil method...
    pnputil /add-driver x64\Release\IntelAvbFilter\IntelAvbFilter.inf /install
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Both installation methods failed!
        echo Make sure you run this script as Administrator.
        pause
        exit /b 1
    )
)

echo [5/5] Starting service...
net start IntelAvbFilter

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo SUCCESS! Driver loaded successfully!
    echo ========================================
    echo.
    echo Check DebugView - you should see:
    echo   "NDIS filter driver registered successfully"
    echo   NOT "NdisFRegisterFilterDriver failed"
) else (
    echo.
    echo ========================================
    echo Service start failed!
    echo ========================================
    echo Check DebugView for error details.
)

echo.
echo Press any key to exit...
pause >nul
