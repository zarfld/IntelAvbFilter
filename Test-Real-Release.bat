@echo off
REM Test the ACTUAL newly built Release file
echo ========================================
echo Testing REAL Release Build (x64\Release\IntelAvbFilter.sys)
echo ========================================

echo [1] Unbinding...
netcfg -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo [2] Stopping...
net stop IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

echo [3] Copying real Release build to package folder...
copy /Y x64\Release\IntelAvbFilter.sys x64\Release\IntelAvbFilter\IntelAvbFilter.sys

echo [4] Installing Release build...
netcfg -v -l x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
timeout /t 3 /nobreak >nul

echo [5] Testing...
avb_test_i226.exe | findstr /C:"Summary" /C:"PTP:" /C:"SYSTIM" /C:"TIMINCA" /C:"TSAUXC"

echo.
echo ========================================
pause
