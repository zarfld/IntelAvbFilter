@echo off
REM Driver Update with Stuck Service Detection

:: Navigate to repository root
if exist "tools\development" (
    cd /d "%~dp0..\.."
) else (
    cd /d "%~dp0"
)

echo Intel AVB Filter - Smart Update
echo ================================
echo.

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Run as Administrator
    pause
    exit /b 1
)

echo Checking service state...
sc query IntelAvbFilter | find "STOP_PENDING" >nul
if %errorLevel% EQU 0 (
    echo.
    echo *** SERVICE STUCK IN STOP_PENDING ***
    echo.
    echo The service cannot be stopped without a reboot.
    echo This is a Windows kernel state issue.
    echo.
    echo Press any key to REBOOT NOW and complete update after reboot
    pause >nul
    
    REM Create auto-run script for after reboot
    echo @echo off > %TEMP%\avb_finish_install.bat
    echo cd /d "%~dp0" >> %TEMP%\avb_finish_install.bat
    echo netcfg -v -l x64\Release\IntelAvbFilter\IntelAvbFilter.inf -c s -i MS_IntelAvbFilter >> %TEMP%\avb_finish_install.bat
    echo if %%errorLevel%% EQU 0 ^( >> %TEMP%\avb_finish_install.bat
    echo     echo Driver installed successfully! >> %TEMP%\avb_finish_install.bat
    echo     sc query IntelAvbFilter >> %TEMP%\avb_finish_install.bat
    echo     avb_test_i226.exe selftest >> %TEMP%\avb_finish_install.bat
    echo ^) >> %TEMP%\avb_finish_install.bat
    echo pause >> %TEMP%\avb_finish_install.bat
    echo del %%0 >> %TEMP%\avb_finish_install.bat
    
    REM Add to RunOnce registry
    reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\RunOnce" /v AVBInstall /t REG_SZ /d "%TEMP%\avb_finish_install.bat" /f
    
    echo.
    echo After reboot, the installation will complete automatically.
    echo.
    shutdown /r /t 10 /c "Reboot required to update Intel AVB Filter driver"
    echo Rebooting in 10 seconds... (Run: shutdown /a to cancel)
    timeout /t 10
    exit /b 0
)

echo Service is not stuck, proceeding with update...
sc stop IntelAvbFilter
timeout /t 3 /nobreak >nul

netcfg -u MS_IntelAvbFilter
timeout /t 2 /nobreak >nul

cd /d "%~dp0x64\Release\IntelAvbFilter"
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

if %errorLevel% EQU 0 (
    echo.
    echo SUCCESS!
    timeout /t 2 /nobreak >nul
    sc query IntelAvbFilter
    echo.
    cd /d "%~dp0"
    avb_test_i226.exe selftest
) else (
    echo.
    echo FAILED: Error %errorLevel%
)

pause
