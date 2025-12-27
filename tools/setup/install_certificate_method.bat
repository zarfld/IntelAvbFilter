@echo off
echo === Intel AVB Filter Driver - Certificate-Based Installation ===
echo This method works with Secure Boot ENABLED (no test signing required)
echo.

echo Step 1: Checking Administrator privileges...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running as Administrator
) else (
    echo ? ERROR: This script must be run as Administrator
    pause
    exit /b 1
)

echo.
echo Step 2: Installing test certificate to system trust store...
if exist "..\..\x64\Debug\IntelAvbFilter.cer" (
    echo Installing certificate: IntelAvbFilter.cer
    certutil -addstore root "..\..\x64\Debug\IntelAvbFilter.cer"
    if %errorLevel% == 0 (
        echo ? Certificate installed successfully
    ) else (
        echo ? Certificate installation failed
        echo This might still work - continuing...
    )
) else (
    echo ? Certificate file not found: x64\Debug\IntelAvbFilter.cer
    echo Make sure the project is built first
    pause
    exit /b 1
)

echo.
echo Step 3: Installing driver via INF (Certificate-based method)...
if exist "..\..\x64\Debug\IntelAvbFilter.inf" (
    echo Attempting driver installation without test signing...
    pnputil /add-driver "..\..\x64\Debug\IntelAvbFilter.inf" /install
    if %errorLevel% == 0 (
        echo ? Driver installation successful!
        echo The driver should now be loaded and ready for testing.
    ) else (
        echo ? Driver installation failed
        echo.
        echo Possible causes:
        echo 1. Certificate not trusted by system
        echo 2. Secure Boot policy too restrictive
        echo 3. Driver signature verification failed
        echo.
        echo Trying DevCon method as fallback...
        goto :DEVCON_METHOD
    )
) else (
    echo ? Driver INF not found: x64\Debug\IntelAvbFilter.inf
    pause
    exit /b 1
)

echo.
echo Step 4: Running hardware test...
echo === STARTING I219 HARDWARE TEST ===
if exist "avb_test_i219.exe" (
    avb_test_i219.exe
) else (
    echo ? Test application not found
    echo Compile it: cl avb_test_i219.c /Fe:avb_test_i219.exe
    pause
    exit /b 1
)
goto :END

:DEVCON_METHOD
echo.
echo === Fallback: DevCon Installation Method ===
echo This uses Windows Device Console (DevCon) tool...

REM Check if DevCon is available
where devcon.exe >nul 2>&1
if %errorLevel% == 0 (
    echo ? DevCon found in PATH
    devcon install "build\x64\Debug\IntelAvbFilter.inf" "*"
) else (
    echo Looking for DevCon in WDK installation...
    if exist "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe" (
        echo ? DevCon found in WDK
        "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe" install "build\x64\Debug\IntelAvbFilter.inf" "*"
    ) else (
        echo ? DevCon not found
        echo Download DevCon from Microsoft or use WDK installation
    )
)

:END
echo.
echo === Installation Complete ===
echo.
echo If successful, check Device Manager for:
echo - "Intel AVB Filter Driver" or similar entry
echo - No yellow warning signs
echo.
echo Next: Run debug tools to monitor kernel messages:
echo 1. Download DebugView.exe from Microsoft Sysinternals
echo 2. Run as Administrator
echo 3. Enable "Capture Kernel" option
echo 4. Run avb_test_i219.exe and watch for "(REAL HARDWARE)" messages
echo.
pause