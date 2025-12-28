@echo off
echo === Intel AVB Filter Driver - DevCon Installation Method ===
echo This method uses Microsoft DevCon tool (works with signed drivers)
echo.

echo Checking for DevCon tool...
set DEVCON_PATH=""

REM Check common DevCon locations
if exist "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe" (
    set DEVCON_PATH="C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe"
    echo ? Found DevCon in WDK: %DEVCON_PATH%
) else if exist "C:\Program Files (x86)\Windows Kits\10\Tools\x86\devcon.exe" (
    set DEVCON_PATH="C:\Program Files (x86)\Windows Kits\10\Tools\x86\devcon.exe"  
    echo ? Found DevCon (x86) in WDK: %DEVCON_PATH%
) else (
    where devcon.exe >nul 2>&1
    if %errorLevel% == 0 (
        set DEVCON_PATH=devcon.exe
        echo ? Found DevCon in PATH
    ) else (
        echo ? DevCon not found
        echo.
        echo DevCon is part of WDK installation. Install locations:
        echo 1. C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe
        echo 2. Download from Microsoft: https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon
        echo.
        pause
        exit /b 1
    )
)

echo.
echo Preparing driver files...
if not exist "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys" (
    echo ? Driver binary not found: build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys
    echo Build the project first
    pause
    exit /b 1
)

if not exist "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.inf" (
    echo ? Driver INF not found: build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.inf
    echo Build the project first
    pause
    exit /b 1
)

echo ? Driver files ready
echo.

echo Installing certificate first...
if exist "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.cer" (
    certutil -addstore root "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.cer" >nul 2>&1
    echo ? Certificate installation attempted
)

echo.
echo Using DevCon to install driver...
echo Command: %DEVCON_PATH% install "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.inf" Root\IntelAvbFilter

%DEVCON_PATH% install "..\..\build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.inf" Root\IntelAvbFilter

if %errorLevel% == 0 (
    echo ? DevCon installation successful!
    echo.
    echo Verifying installation...
    %DEVCON_PATH% status Root\IntelAvbFilter
    echo.
    echo Running hardware test...
    if exist "avb_test_i219.exe" (
        avb_test_i219.exe
    ) else (
        echo ? Test application not found
        echo Compile it: cl avb_test_i219.c /Fe:avb_test_i219.exe
    )
) else (
    echo ? DevCon installation failed
    echo.
    echo Common issues:
    echo 1. Driver signature not trusted
    echo 2. INF file has errors  
    echo 3. Hardware ID mismatch
    echo.
    echo Try manual Device Manager installation:
    echo 1. Open Device Manager
    echo 2. Right-click your Intel I219 adapter
    echo 3. Update Driver -> Browse -> Have Disk
    echo 4. Point to: x64\Debug\IntelAvbFilter.inf
)

echo.
pause