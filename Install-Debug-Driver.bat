@echo off
REM ============================================================================
REM Intel AVB Filter Driver - Debug Installation Script
REM ============================================================================
REM Purpose: Clean installation of Debug driver with proper certificate handling
REM ============================================================================

echo.
echo ============================================================================
echo Intel AVB Filter Driver - Debug Installation
echo ============================================================================
echo.

REM Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: This script must be run as Administrator
    echo Right-click and select "Run as administrator"
    pause
    exit /b 1
)

echo [1/6] Setting up environment...
cd /d "%~dp0"
set DRIVER_DIR=x64\Debug\IntelAvbFilter
set DRIVER_INF=%DRIVER_DIR%\IntelAvbFilter.inf
set DRIVER_SYS=%DRIVER_DIR%\IntelAvbFilter.sys
set DRIVER_CAT=%DRIVER_DIR%\intelavbfilter.cat

REM Verify files exist
echo [2/6] Verifying driver files...
if not exist "%DRIVER_SYS%" (
    echo ERROR: Driver file not found: %DRIVER_SYS%
    echo Please build the Debug configuration first
    pause
    exit /b 1
)
if not exist "%DRIVER_INF%" (
    echo ERROR: INF file not found: %DRIVER_INF%
    pause
    exit /b 1
)
echo   Found: IntelAvbFilter.sys (%~z1 bytes)
echo   Found: IntelAvbFilter.inf
if exist "%DRIVER_CAT%" echo   Found: intelavbfilter.cat

REM Stop and remove any existing installation
echo [3/6] Cleaning up previous installation...
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% EQU 0 (
    echo   Stopping service...
    sc stop IntelAvbFilter >nul 2>&1
    timeout /t 2 /nobreak >nul
    echo   Deleting service...
    sc delete IntelAvbFilter >nul 2>&1
    timeout /t 2 /nobreak >nul
)

echo   Removing NDIS filter binding...
netcfg -v -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul

REM Verify certificate in TrustedPublisher
echo [4/6] Verifying test certificate...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$cert = (Get-AuthenticodeSignature '%DRIVER_SYS%').SignerCertificate; if ($cert) { $existing = Get-ChildItem Cert:\LocalMachine\TrustedPublisher | Where-Object {$_.Thumbprint -eq $cert.Thumbprint}; if ($existing) { Write-Host '  Certificate already trusted' } else { Write-Host '  Installing certificate to TrustedPublisher...'; $cert | Export-Certificate -FilePath '%TEMP%\wdktest.cer' -Force | Out-Null; certutil -addstore TrustedPublisher '%TEMP%\wdktest.cer' } }" >nul 2>&1

REM Install driver
echo [5/6] Installing NDIS filter driver...
netcfg -v -l "%DRIVER_INF%" -c s -i MS_IntelAvbFilter
if %errorLevel% NEQ 0 (
    echo.
    echo ERROR: Driver installation failed with code: %errorLevel%
    echo.
    echo Common causes:
    echo   - Certificate not trusted (check TrustedPublisher store)
    echo   - Test signing not enabled (run: bcdedit /set testsigning on)
    echo   - INF syntax errors
    echo   - Missing driver files
    echo.
    echo Checking event log for errors...
    powershell -NoProfile -Command "Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-5)} -MaxEvents 10 -ErrorAction SilentlyContinue | Where-Object {$_.Message -match 'IntelAvbFilter'} | Format-List TimeCreated, LevelDisplayName, Message"
    pause
    exit /b 1
)

REM Verify installation
echo [6/6] Verifying installation...
timeout /t 3 /nobreak >nul
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Service not created
    pause
    exit /b 1
)

echo.
echo ============================================================================
echo Installation Complete!
echo ============================================================================
echo.
echo Driver Status:
sc query IntelAvbFilter
echo.
echo Next Steps:
echo   1. Run: avb_test_i226.exe caps
echo   2. Check device interface: \\.\IntelAvbFilter
echo   3. Run full test: avb_test_i226.exe selftest
echo   4. Monitor with DebugView for detailed logging
echo.
echo ============================================================================
pause
