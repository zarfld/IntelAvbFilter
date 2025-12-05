@echo off
echo Installing Intel AVB Filter Driver (Debug)...
cd /d "%~dp0x64\Debug\IntelAvbFilter"
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
if %errorLevel% EQU 0 (
    echo.
    echo SUCCESS! Driver installed.
    timeout /t 2 /nobreak >nul
    sc query IntelAvbFilter
) else (
    echo.
    echo FAILED with error code: %errorLevel%
    echo Common error codes:
    echo   5 = Access Denied (needs Administrator)
    echo   2 = File not found
)
echo.
pause
