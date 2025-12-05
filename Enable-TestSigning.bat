@echo off
REM ============================================================================
REM Enable Test Signing for Windows Driver Development
REM ============================================================================
REM Purpose: Configure Windows to allow installation of test-signed drivers
REM REQUIRES: Administrator privileges and system reboot
REM ============================================================================

echo.
echo ============================================================================
echo Enable Test Signing Mode
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

echo [1/4] Checking current boot configuration...
echo.
bcdedit /enum {current} | findstr /i "testsigning nointegritychecks"
echo.

echo [2/4] Enabling test signing mode...
bcdedit /set testsigning on
if %errorLevel% NEQ 0 (
    echo ERROR: Failed to enable test signing
    echo This may indicate Secure Boot is enabled
    pause
    exit /b 1
)
echo   Test signing mode enabled

echo.
echo [3/4] Disabling driver signature enforcement (optional)...
bcdedit /set nointegritychecks on
if %errorLevel% NEQ 0 (
    echo WARNING: Could not disable integrity checks (may be restricted by policy)
) else (
    echo   Integrity checks disabled
)

echo.
echo [4/4] Verifying configuration...
echo.
bcdedit /enum {current} | findstr /i "testsigning nointegritychecks"

echo.
echo ============================================================================
echo Configuration Complete!
echo ============================================================================
echo.
echo IMPORTANT: You MUST reboot for changes to take effect
echo.
echo After reboot:
echo   1. Test signing watermark will appear in desktop corners
echo   2. You can install the driver: Install-Debug-Driver.bat
echo   3. Test with: avb_test_i226.exe caps
echo.
echo Press any key to REBOOT NOW or close this window to reboot later...
pause >nul
shutdown /r /t 10 /c "Rebooting to enable test signing mode for driver development"
echo.
echo System will reboot in 10 seconds...
echo To cancel: shutdown /a
echo.
timeout /t 10
