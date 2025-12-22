@echo off
REM ============================================================================
REM Nuclear Option - Complete Driver Cleanup and Reinstall
REM ============================================================================

echo.
echo ============================================================================
echo Intel AVB Filter - Nuclear Cleanup and Reinstall
echo ============================================================================
echo.

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ERROR: Must run as Administrator
    pause
    exit /b 1
)

echo [PHASE 1] AGGRESSIVE SERVICE CLEANUP
echo =====================================

echo Step 1: Stop all related processes...
taskkill /F /IM netcfg.exe >nul 2>&1
taskkill /F /IM pnputil.exe >nul 2>&1
timeout /t 2 /nobreak >nul

echo Step 2: Force stop service...
sc stop IntelAvbFilter >nul 2>&1
net stop IntelAvbFilter >nul 2>&1
timeout /t 3 /nobreak >nul

echo Step 3: Delete service from registry...
reg delete "HKLM\SYSTEM\CurrentControlSet\Services\IntelAvbFilter" /f >nul 2>&1
reg delete "HKLM\SYSTEM\ControlSet001\Services\IntelAvbFilter" /f >nul 2>&1
reg delete "HKLM\SYSTEM\ControlSet002\Services\IntelAvbFilter" /f >nul 2>&1

echo Step 4: Remove NDIS filter bindings...
netcfg -v -u MS_IntelAvbFilter >nul 2>&1

echo Step 5: Clean driver store...
for /f "tokens=*" %%i in ('pnputil /enum-drivers ^| findstr /C:"intelavbfilter.inf"') do (
    for /f "tokens=2" %%j in ("%%i") do (
        if not "%%j"=="" (
            echo   Removing %%j from driver store...
            pnputil /delete-driver %%j /uninstall /force
        )
    )
)

echo.
echo [PHASE 2] REBOOT CHECK
echo ======================
echo.
echo CRITICAL: If service is stuck in deletion, Windows needs a reboot.
echo.
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% EQU 0 (
    echo *** SERVICE STILL EXISTS - REBOOT REQUIRED ***
    echo.
    echo The service is in a zombie state and cannot be removed without reboot.
    echo.
    echo Options:
    echo   1. Press any key to REBOOT NOW
    echo   2. Close this window to reboot manually later
    echo.
    pause
    shutdown /r /t 30 /c "Rebooting to clean up driver service"
    echo.
    echo Rebooting in 30 seconds... (Run: shutdown /a to cancel)
    echo After reboot, run: Install-Clean-Driver.bat
    echo.
    timeout /t 30
    exit /b 0
)

echo Service successfully removed!
echo.

echo [PHASE 3] CERTIFICATE INSTALLATION
echo ===================================

cd /d "%~dp0x64\Debug\IntelAvbFilter"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
"$cert = (Get-AuthenticodeSignature 'IntelAvbFilter.sys').SignerCertificate; ^
if ($cert) { ^
    $certPath = Join-Path $env:TEMP 'WDKTestCert.cer'; ^
    [System.IO.File]::WriteAllBytes($certPath, $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)); ^
    Write-Host 'Installing certificate to Root store...'; ^
    certutil -addstore Root $certPath; ^
    Write-Host 'Installing certificate to TrustedPublisher store...'; ^
    certutil -addstore TrustedPublisher $certPath; ^
} else { ^
    Write-Host 'ERROR: Cannot extract certificate from driver'; ^
    exit 1; ^
}"

if %errorLevel% NEQ 0 (
    echo.
    echo ERROR: Certificate installation failed
    pause
    exit /b 1
)

echo.
echo [PHASE 4] DRIVER INSTALLATION
echo ==============================

echo Installing driver...
netcfg -v -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter

if %errorLevel% EQU 0 (
    echo.
    echo ============================================================================
    echo SUCCESS! Driver installed!
    echo ============================================================================
    echo.
    timeout /t 2 /nobreak >nul
    sc query IntelAvbFilter
    echo.
    echo Testing device interface...
    cd /d "%~dp0"
    avb_test_i226.exe caps
) else (
    echo.
    echo ============================================================================
    echo INSTALLATION FAILED - Error: %errorLevel%
    echo ============================================================================
    echo.
    echo Checking setup log...
    powershell -Command "Get-Content C:\Windows\INF\setupapi.dev.log -Tail 50 | Select-String -Pattern 'IntelAvbFilter|Error|failed' -Context 1,1 | Select-Object -Last 10"
)

echo.
pause
