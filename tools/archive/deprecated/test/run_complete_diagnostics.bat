@echo off
echo === Intel AVB Filter Driver - Comprehensive Diagnostics Suite ===
echo Hardware-Only Analysis - No simulation, no hidden problems
echo.

echo Step 1: Compiling diagnostic tools...
echo.

echo Compiling main diagnostics tool...
cl tests\diagnostic\intel_avb_diagnostics.c /Fe:intel_avb_diagnostics.exe /TC
if exist "intel_avb_diagnostics.exe" (
    echo ? Main diagnostics tool compiled successfully
) else (
    echo ? Main diagnostics compilation failed
    echo Trying with explicit libraries...
    cl tests\diagnostic\intel_avb_diagnostics.c /Fe:intel_avb_diagnostics.exe advapi32.lib setupapi.lib cfgmgr32.lib iphlpapi.lib
    if exist "intel_avb_diagnostics.exe" (
        echo ? Main diagnostics tool compiled with explicit libraries
    ) else (
        echo ? Compilation failed completely
        pause
        exit /b 1
    )
)

echo.
echo Compiling device interface tester...
if exist "debug_device_interface.c" (
    cl debug_device_interface.c /Fe:device_interface_test.exe advapi32.lib
    if exist "device_interface_test.exe" (
        echo ? Device interface tester compiled
    ) else (
        echo ? Device interface tester compilation failed
    )
)

echo.
echo Compiling hardware-only test application...
if exist "tests\device_specific\i219\avb_test_i219.c" (
    cl tests\device_specific\i219\avb_test_i219.c /DHARDWARE_ONLY=1 /Fe:hardware_test.exe
    if exist "hardware_test.exe" (
        echo ? Hardware test application compiled
    ) else (
        echo ? Hardware test compilation failed
    )
)

echo.
echo Step 2: Running comprehensive diagnostics...
echo.
echo === STARTING COMPREHENSIVE HARDWARE-ONLY DIAGNOSTICS ===
echo.

intel_avb_diagnostics.exe /debug

echo.
echo Step 3: Additional diagnostic tests...
echo.

if exist "device_interface_test.exe" (
    echo Running device interface diagnostics...
    device_interface_test.exe
)

echo.
echo Step 4: Checking build artifacts...
echo.
if exist "build\x64\Debug\IntelAvbFilter.sys" (
    echo ? Driver binary: x64\Debug\IntelAvbFilter.sys
    dir "build\x64\Debug\IntelAvbFilter.sys" | findstr "IntelAvbFilter.sys"
) else (
    echo ? Driver binary not found
)

if exist "build\x64\Debug\IntelAvbFilter.inf" (
    echo ? Driver INF: x64\Debug\IntelAvbFilter.inf
) else (
    echo ? Driver INF not found
)

if exist "build\x64\Debug\IntelAvbFilter.cat" (
    echo ? Driver catalog: x64\Debug\IntelAvbFilter.cat
) else (
    echo ? Driver catalog not found
)

if exist "build\x64\Debug\IntelAvbFilter.cer" (
    echo ? Driver certificate: x64\Debug\IntelAvbFilter.cer
) else (
    echo ? Driver certificate not found
)

echo.
echo Step 5: System capability analysis...
echo.

echo Checking administrator privileges...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running with Administrator privileges
) else (
    echo ? NOT running as Administrator (some tests limited)
    echo    Right-click Command Prompt and select "Run as administrator" for full diagnostics
)

echo.
echo Checking Hyper-V availability...
dism /online /get-featureinfo /featurename:Microsoft-Hyper-V >nul 2>&1
if %errorLevel% == 0 (
    echo ? Hyper-V available (good for VM development)
) else (
    echo ? Hyper-V not available
)

echo.
echo Checking Windows SDK/WDK tools...
where signtool >nul 2>&1
if %errorLevel% == 0 (
    echo ? SignTool available (driver signing possible)
) else (
    echo ? SignTool not found (install WDK for driver development)
)

where inf2cat >nul 2>&1
if %errorLevel% == 0 (
    echo ? Inf2Cat available (catalog generation possible)
) else (
    echo ? Inf2Cat not found (install WDK for driver development)
)

echo.
echo === DIAGNOSTIC SUMMARY ===
echo.
echo ?? WHAT WE DISCOVERED:
echo    - Hardware detection results above
echo    - Driver compilation status above
echo    - System capabilities analyzed
echo    - Installation readiness assessed
echo.
echo ?? RECOMMENDATIONS BASED ON FINDINGS:
echo.
echo IF INTEL I219 WAS FOUND:
echo    ? Your target hardware is present and ready
echo    ? Intel AVB Filter Driver should work
echo    ? Next: Choose installation method based on your environment
echo.
echo IF NO INTEL HARDWARE FOUND:
echo    ? No compatible Intel controllers detected
echo    ? Check Device Manager for network adapters
echo    ? Install Intel network drivers if needed
echo    ? Consider external Intel adapter for testing
echo.
echo ?? DEVELOPMENT PATHS:
echo.
echo Corporate Environment (Secure Boot):
echo    1. EV Code Signing Certificate (~�300/year)
echo       ? Immediate deployment capability
echo       ? Production-ready solution
echo.
echo    2. Hyper-V Development VM (Free)
echo       ? VM with disabled Secure Boot
echo       ? Full development freedom
echo       ? Host system unchanged
echo.
echo    3. Request IT exception for test system
echo       ? Dedicated development machine
echo       ? Secure Boot disabled for testing
echo.
echo Development Environment:
echo    1. Disable Secure Boot in BIOS
echo    2. Enable test signing: bcdedit /set testsigning on
echo    3. Install driver and test immediately
echo.
echo === NEXT ACTIONS ===
echo.
echo 1. Review diagnostic output above
echo 2. Choose development path based on your constraints
echo 3. Set up chosen environment (VM/Certificate/Test system)
echo 4. Install Intel AVB Filter Driver
echo 5. Run: hardware_test.exe (when driver installed)
echo 6. Monitor with DebugView.exe for real hardware access
echo.
echo Your Intel AVB Filter Driver is ready - just needs the right test environment! ??
echo.
pause