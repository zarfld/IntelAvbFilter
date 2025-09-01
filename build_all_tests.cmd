@echo off
REM Build All User-Mode AVB Tests
echo Building all user-mode AVB test tools...

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo =======================================================
echo Building AVB Diagnostic Test...
echo =======================================================
nmake -f tools\avb_test\avb_diagnostic.mak clean
nmake -f tools\avb_test\avb_diagnostic.mak
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building AVB Hardware State Test...
echo =======================================================
nmake -f tools\avb_test\avb_hw_state_test.mak clean
nmake -f tools\avb_test\avb_hw_state_test.mak
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building TSN IOCTL Handler Test...
echo =======================================================
nmake -f tools\avb_test\tsn_ioctl_test.mak clean
nmake -f tools\avb_test\tsn_ioctl_test.mak
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building TSN Hardware Activation Validation Test...
echo =======================================================  
nmake -f tools\avb_test\tsn_hardware_activation_validation.mak clean
nmake -f tools\avb_test\tsn_hardware_activation_validation.mak
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building I210 Basic Test...
echo =======================================================
nmake -f tools\avb_test\avb_test.mak clean
nmake -f tools\avb_test\avb_test.mak
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building I226 Test (from correct directory)...
echo =======================================================
cd tools\avb_test
nmake -f avb_i226_test.mak clean
nmake -f avb_i226_test.mak
cd ..\..
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building I226 Advanced Test (from correct directory)...
echo =======================================================
cd tools\avb_test
nmake -f avb_i226_advanced.mak clean
nmake -f avb_i226_advanced.mak
cd ..\..
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Building Multi-Adapter Test (from correct directory)...
echo =======================================================
cd tools\avb_test
nmake -f avb_multi_adapter.mak clean
nmake -f avb_multi_adapter.mak
cd ..\..
if errorlevel 1 goto :error

echo.
echo =======================================================
echo Build Summary
echo =======================================================
echo Checking built executables:
if exist "build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe" (
    echo  ? AVB Diagnostic Test
) else (
    echo  ? AVB Diagnostic Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\avb_hw_state_test.exe" (
    echo  ? AVB Hardware State Test
) else (
    echo  ? AVB Hardware State Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe" (
    echo  ? TSN IOCTL Handler Test
) else (
    echo  ? TSN IOCTL Handler Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe" (
    echo  ? TSN Hardware Activation Validation Test  
) else (
    echo  ? TSN Hardware Activation Validation Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\avb_test_i210.exe" (
    echo  ? I210 Basic Test
) else (
    echo  ? I210 Basic Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\avb_i226_test.exe" (
    echo  ? I226 Test
) else (
    echo  ? I226 Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe" (
    echo  ? I226 Advanced Test
) else (
    echo  ? I226 Advanced Test - Missing
)

if exist "build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe" (
    echo  ? Multi-Adapter Test
) else (
    echo  ? Multi-Adapter Test - Missing
)

echo.
echo All working tests built successfully!
echo Tests are ready for hardware validation.
goto :end

:error
echo.
echo ? Build failed! Check the error messages above.
exit /b 1

:end
echo.
echo Build completed successfully!