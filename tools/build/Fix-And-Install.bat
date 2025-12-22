@echo off
REM ============================================================================
REM Fix Intel AVB Filter Driver Installation Issues
REM ============================================================================

echo.
echo ============================================================================
echo Intel AVB Filter - Fix Installation Issues
echo ============================================================================
echo.

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Must run as Administrator
    pause
    exit /b 1
)

echo [1/5] Removing old service entries...
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% EQU 0 (
    echo   Stopping service...
    sc stop IntelAvbFilter >nul 2>&1
    timeout /t 3 /nobreak >nul
)

echo   Deleting service (forced)...
sc delete IntelAvbFilter >nul 2>&1
timeout /t 3 /nobreak >nul

echo   Uninstalling NDIS filter...
netcfg -v -u MS_IntelAvbFilter >nul 2>&1
timeout /t 3 /nobreak >nul

echo [2/5] Removing old driver package from driver store...
for /f "tokens=2" %%i in ('pnputil /enum-drivers ^| findstr /C:"oem69.inf"') do (
    echo   Removing %%i...
    pnputil /delete-driver %%i /uninstall /force >nul 2>&1
)

echo [3/5] Fixing certificate trust issue...

REM Navigate to repository root (batch file may be in tools/build/)
cd /d "%~dp0"
if exist "tools\build" (
    REM Script is in repository root
    cd /d "%~dp0x64\Debug\IntelAvbFilter"
) else (
    REM Script is in tools/build/ subdirectory
    cd /d "%~dp0..\..\x64\Debug\IntelAvbFilter"
)

REM Extract and reinstall certificate
powershell -NoProfile -ExecutionPolicy Bypass -Command "$cert = (Get-AuthenticodeSignature 'IntelAvbFilter.sys').SignerCertificate; if ($cert) { $certPath = Join-Path $env:TEMP 'WDKTestCert.cer'; [System.IO.File]::WriteAllBytes($certPath, $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)); certutil -addstore Root $certPath | Out-Null; certutil -addstore TrustedPublisher $certPath | Out-Null; Write-Host '  Certificate installed to Root and TrustedPublisher stores' }"

echo [4/5] Cleaning up registry and waiting for service deletion...
REM Force registry cleanup
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\IntelAvbFilter" /f >nul 2>&1
timeout /t 5 /nobreak >nul

REM Verify service is gone
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% EQU 0 (
    echo   WARNING: Service still exists, forcing reboot may be needed
) else (
    echo   Service successfully removed
)

echo [5/5] Installing driver...
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter
if %errorLevel% EQU 0 (
    echo.
    echo ============================================================================
    echo SUCCESS! Driver installed successfully!
    echo ============================================================================
    echo.
    timeout /t 2 /nobreak >nul
    sc query IntelAvbFilter
    echo.
    echo Next: Test with avb_test_i226.exe caps
) else (
    echo.
    echo ============================================================================
    echo INSTALLATION FAILED
    echo ============================================================================
    echo Error code: %errorLevel%
    echo.
    echo Check C:\Windows\INF\setupapi.dev.log for details
)

echo.
pause
