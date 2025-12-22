@echo off
echo === Intel AVB Filter Driver - Secure Boot Compatible Test ===
echo.

echo NOTICE: Secure Boot is blocking test signing.
echo This script provides alternative testing methods.
echo.

echo Step 1: Checking if running as Administrator...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running as Administrator
) else (
    echo ? ERROR: This script must be run as Administrator
    echo    Right-click Command Prompt and select "Run as administrator"
    pause
    exit /b 1
)

echo.
echo Step 2: Checking Secure Boot status...
powershell -command "Confirm-SecureBootUEFI" >nul 2>&1
if %errorLevel% == 0 (
    echo ??  Secure Boot is ENABLED
    echo    This blocks test signing but we can still test some features
) else (
    echo ? Secure Boot is disabled or not supported
)

echo.
echo Step 3: Installing test certificate...
if exist "x64\Debug\IntelAvbFilter.cer" (
    certutil -addstore root "x64\Debug\IntelAvbFilter.cer" >nul 2>&1
    if %errorLevel% == 0 (
        echo ? Test certificate installed successfully
    ) else (
        echo ??  Certificate installation failed, but continuing...
    )
) else (
    echo ? Certificate file not found: x64\Debug\IntelAvbFilter.cer
    echo    Make sure the project is built
)

echo.
echo Step 4: Attempting driver installation with certificate...
if exist "x64\Debug\IntelAvbFilter.inf" (
    echo Trying driver installation...
    pnputil /add-driver "x64\Debug\IntelAvbFilter.inf" /install
    if %errorLevel% == 0 (
        echo ? Driver installation successful
        echo.
        echo Step 5: Running hardware test...
        echo === STARTING HARDWARE TEST ===
        if exist "avb_test_i219.exe" (
            avb_test_i219.exe
        ) else (
            echo ? Test application not found: avb_test_i219.exe
            echo    Compile it first: cl avb_test_i219.c /Fe:avb_test_i219.exe
        )
    ) else (
        echo ? Driver installation failed
        echo.
        echo SECURE BOOT WORKAROUNDS:
        echo 1. Disable Secure Boot in BIOS/UEFI settings
        echo 2. Use bcdedit /set testsigning on (after disabling Secure Boot)
        echo 3. Or test in a VM without Secure Boot
    )
) else (
    echo ? Driver INF not found: x64\Debug\IntelAvbFilter.inf
    echo    Build the project first
)

echo.
echo === Alternative Testing Options ===
echo.
echo If driver installation fails due to Secure Boot:
echo 1. Disable Secure Boot in BIOS (recommended for development)
echo 2. Use Windows Test Mode (requires Secure Boot disabled)
echo 3. Test in a virtual machine
echo 4. Use Device Simulation Mode (limited testing)
echo.

echo Checking Intel I219 hardware anyway...
wmic path Win32_NetworkAdapter where "Manufacturer like '%%Intel%%' and Name like '%%I219%%'" get Name,PNPDeviceID

echo.
pause