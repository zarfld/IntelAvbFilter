@echo off
setlocal enabledelayedexpansion

echo Building all user-mode AVB test tools...
echo =====================================

REM Initialize VS2022 x64 environment once
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
	echo ? Failed to initialize Visual Studio environment
	echo Please ensure Visual Studio 2022 with WDK is installed
	exit /b 1
)

REM Track actual build results
set SUCCESS_COUNT=0
set FAIL_COUNT=0

echo.
echo Building core tests...

REM Build 1: AVB Diagnostic Test
echo - Building AVB Diagnostic Test...
nmake -f tools\avb_test\avb_diagnostic.mak clean >nul 2>&1
nmake -f tools\avb_test\avb_diagnostic.mak >nul 2>&1
if exist "build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe" (
	echo   ? AVB Diagnostic Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? AVB Diagnostic Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 2: AVB Hardware State Test
echo - Building AVB Hardware State Test...
nmake -f tools\avb_test\avb_hw_state_test.mak clean >nul 2>&1
nmake -f tools\avb_test\avb_hw_state_test.mak >nul 2>&1
if exist "build\tools\avb_test\x64\Debug\avb_hw_state_test.exe" (
	echo   ? AVB Hardware State Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? AVB Hardware State Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 3: TSN IOCTL Handler Test
echo - Building TSN IOCTL Handler Test...
del build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers_um.obj >nul 2>&1
nmake -f tools\avb_test\tsn_ioctl_test.mak >nul 2>&1
if exist "build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe" (
	echo   ? TSN IOCTL Handler Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? TSN IOCTL Handler Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 4: TSN Hardware Activation Validation Test
echo - Building TSN Hardware Activation Validation Test...
del build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.obj >nul 2>&1
nmake -f tools\avb_test\tsn_hardware_activation_validation.mak >nul 2>&1
if exist "build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe" (
	echo   ? TSN Hardware Activation Validation Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? TSN Hardware Activation Validation Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 5: AVB Capability Validation Test
echo - Building AVB Capability Validation Test...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\avb_capability_validation_test_um.obj >nul 2>&1
nmake -f avb_capability_validation.mak >nul 2>&1
cd ..\..
if exist "build\tools\avb_test\x64\Debug\avb_capability_validation_test.exe" (
	echo   ? AVB Capability Validation Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? AVB Capability Validation Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 6: AVB Device Separation Validation Test
echo - Building AVB Device Separation Validation Test...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\avb_device_separation_test_um.obj >nul 2>&1
nmake -f avb_device_separation_validation.mak >nul 2>&1
cd ..\..
if exist "build\tools\avb_test\x64\Debug\avb_device_separation_test.exe" (
	echo   ? AVB Device Separation Validation Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? AVB Device Separation Validation Test FAILED
	set /a FAIL_COUNT+=1
)

echo.
echo Building critical investigation tools...

REM Build 7: ChatGPT5 I226 TAS Validation - CRITICAL for evidence reproduction
echo - Building ChatGPT5 I226 TAS Validation...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.obj >nul 2>&1
nmake -f chatgpt5_i226_tas_validation.mak clean >nul 2>&1
nmake -f chatgpt5_i226_tas_validation.mak
cd ..\..
if exist "build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe" (
	echo   ? ChatGPT5 I226 TAS Validation - CRITICAL TOOL
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? ChatGPT5 I226 TAS Validation FAILED - CRITICAL TOOL
	set /a FAIL_COUNT+=1
)

REM Build 8: Hardware Investigation Tool - CRITICAL for evidence reproduction
echo - Building Hardware Investigation Tool...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\hardware_investigation_tool.obj >nul 2>&1
nmake -f hardware_investigation.mak clean >nul 2>&1
nmake -f hardware_investigation.mak
cd ..\..
if exist "build\tools\avb_test\x64\Debug\hardware_investigation_tool.exe" (
	echo   ? Hardware Investigation Tool - CRITICAL TOOL
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? Hardware Investigation Tool FAILED - CRITICAL TOOL
	set /a FAIL_COUNT+=1
)

REM Build 9: Corrected I226 TAS Test - CRITICAL for evidence reproduction
echo - Building Corrected I226 TAS Test...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\corrected_i226_tas_test.obj >nul 2>&1
nmake -f corrected_i226_tas_test.mak clean >nul 2>&1
nmake -f corrected_i226_tas_test.mak
cd ..\..
if exist "build\tools\avb_test\x64\Debug\corrected_i226_tas_test.exe" (
	echo   ? Corrected I226 TAS Test - CRITICAL TOOL
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? Corrected I226 TAS Test FAILED - CRITICAL TOOL
	set /a FAIL_COUNT+=1
)

REM Build 10: Critical Prerequisites Investigation - CRITICAL for evidence reproduction
echo - Building Critical Prerequisites Investigation...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\critical_prerequisites_investigation.obj >nul 2>&1
nmake -f critical_prerequisites_investigation.mak clean >nul 2>&1
nmake -f critical_prerequisites_investigation.mak
cd ..\..
if exist "build\tools\avb_test\x64\Debug\critical_prerequisites_investigation.exe" (
	echo   ? Critical Prerequisites Investigation - CRITICAL TOOL
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? Critical Prerequisites Investigation FAILED - CRITICAL TOOL
	set /a FAIL_COUNT+=1
)

REM Build 11: Enhanced TAS Investigation - CRITICAL for evidence reproduction
echo - Building Enhanced TAS Investigation...
cd tools\avb_test
del ..\..\build\tools\avb_test\x64\Debug\enhanced_tas_investigation.obj >nul 2>&1
nmake -f enhanced_tas_investigation.mak clean >nul 2>&1
nmake -f enhanced_tas_investigation.mak
cd ..\..
if exist "build\tools\avb_test\x64\Debug\enhanced_tas_investigation.exe" (
	echo   ? Enhanced TAS Investigation - CRITICAL TOOL
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? Enhanced TAS Investigation FAILED - CRITICAL TOOL
	set /a FAIL_COUNT+=1
)

echo.
echo Building device-specific tests...

REM Build 12: I210 Basic Test
echo - Building I210 Basic Test...
#nmake -f tools\avb_test\avb_test.mak clean >nul 2>&1
nmake -f tools\avb_test\avb_test.mak >nul 2>&1
if exist "build\tools\avb_test\x64\Debug\avb_test_i210.exe" (
	echo   ? I210 Basic Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? I210 Basic Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 13: I226 Test
echo - Building I226 Test...
cd tools\avb_test
nmake -f avb_i226_test.mak clean >nul 2>&1
nmake -f avb_i226_test.mak >nul 2>&1
cd ..\..
if exist "build\tools\avb_test\x64\Debug\avb_i226_test.exe" (
	echo   ? I226 Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? I226 Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 14: I226 Advanced Test
echo - Building I226 Advanced Test...
cd tools\avb_test
nmake -f avb_i226_advanced.mak clean >nul 2>&1
nmake -f avb_i226_advanced.mak >nul 2>&1
cd ..\..
if exist "build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe" (
	echo   ? I226 Advanced Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? I226 Advanced Test FAILED
	set /a FAIL_COUNT+=1
)

REM Build 15: Multi-Adapter Test
echo - Building Multi-Adapter Test...
cd tools\avb_test
nmake -f avb_multi_adapter.mak clean >nul 2>&1
nmake -f avb_multi_adapter.mak >nul 2>&1
cd ..\..
if exist "build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe" (
	echo   ? Multi-Adapter Test
	set /a SUCCESS_COUNT+=1
) else (
	echo   ? Multi-Adapter Test FAILED
	set /a FAIL_COUNT+=1
)

echo.
echo =====================================
echo HONEST BUILD SUMMARY
echo =====================================
echo Total tests: 15 (including 5 CRITICAL investigation tools)
echo Successful: !SUCCESS_COUNT!
echo Failed: !FAIL_COUNT!

if !FAIL_COUNT! EQU 0 (
	echo.
	echo ? ALL TESTS BUILT SUCCESSFULLY
	echo ? Ready for hardware validation
	echo ? CRITICAL INVESTIGATION TOOLS AVAILABLE FOR EVIDENCE REPRODUCTION
	exit /b 0
) else (
	echo.
	echo ? BUILD FAILURES DETECTED
	echo ? !FAIL_COUNT! test(s) failed to build
	echo ? Only !SUCCESS_COUNT! tests are available
	echo.
	if !FAIL_COUNT! LEQ 5 (
		echo ?? Core tests may be working - check if critical investigation tools built
	)
	echo This is honest reporting - no false advertising
	exit /b 1
)
