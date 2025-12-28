@echo off
echo ============================================
echo Complete Driver Setup with Certificate
echo ============================================
echo.

:: Navigate to repository root
if exist "tools\setup" (
    cd /d "%~dp0..\.."
) else (
    cd /d "%~dp0"
)

:: Check for admin
net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Must run as Administrator!
    echo.
    echo Right-click this file and select "Run as administrator"
    pause
    exit /b 1
)

echo [1] Extracting and installing certificate...
powershell -Command "$cert = (Get-AuthenticodeSignature build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys).SignerCertificate; $cert | Export-Certificate -FilePath '%TEMP%\WDKTestCert.cer' -Force | Out-Null"
if exist "%TEMP%\WDKTestCert.cer" (
    certutil -addstore Root "%TEMP%\WDKTestCert.cer" >nul 2>&1
    certutil -addstore TrustedPublisher "%TEMP%\WDKTestCert.cer" >nul 2>&1
    echo     Certificate installed to Root and TrustedPublisher stores
) else (
    echo     WARNING: Could not extract certificate
)
echo.

echo [2] Complete cleanup of old driver...
sc stop IntelAvbFilter >nul 2>&1
sc delete IntelAvbFilter >nul 2>&1
netcfg -u MS_IntelAvbFilter >nul 2>&1
timeout /t 2 /nobreak >nul
echo     Cleanup complete.
echo.

echo [3] Installing NDIS filter driver...
echo     Using: build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.inf
cd /d build\x64\Debug\IntelAvbFilter\IntelAvbFilter
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
cd /d ..\..\..\..\...

if %errorLevel% EQU 0 (
    echo.
    echo ========================================
    echo SUCCESS! Driver installed
    echo ========================================
    echo.
    echo [4] Checking service status...
    sc query IntelAvbFilter
    echo.
    echo [5] Testing driver...
    if exist avb_test_i226.exe (
        avb_test_i226.exe caps
        echo.
        echo Run full test: avb_test_i226.exe selftest
    ) else (
        echo Test tool not found
    )
) else (
    echo.
    echo ========================================
    echo FAILED! Error code: %errorLevel%
    echo ========================================
    echo.
    echo Common errors:
    echo   5 (0x80070005) = Access Denied
    echo     - Check: Running as Administrator?
    echo     - Check: Certificate installed?
    echo   430 (0x80070430) = Element not found
    echo     - Check: Driver binary has unresolved symbols?
    echo     - Check: Dependencies missing?
    echo.
    echo Checking Windows event log for details...
    powershell -Command "Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddMinutes(-2)} -MaxEvents 10 | Where-Object {$_.Message -match 'IntelAvbFilter|ndislwf|driver'} | Format-List TimeCreated, Message"
)

echo.
pause
