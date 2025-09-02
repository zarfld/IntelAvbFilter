@echo off
REM Simple build script that uses the working compilation method
REM Avoids terminal commands that get stuck

echo Building investigation tools individually...
echo ==========================================

REM Use the working MSVC compiler directly
set MSVC_PATH="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64"
set SDK_INCLUDE="C:\Program Files (x86)\Windows Kits\10\include\10.0.26100.0"
set SDK_LIB="C:\Program Files (x86)\Windows Kits\10\lib\10.0.26100.0"

set INCLUDE_FLAGS=/I. /I"external\intel_avb\include" /I"%SDK_INCLUDE%\um" /I"%SDK_INCLUDE%\shared"
set COMPILE_FLAGS=/nologo /W4 /Zi /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DWIN64 /D_WIN64
set LINK_FLAGS=/nologo /DEBUG /MACHINE:X64 /LIBPATH:"%SDK_LIB%\um\x64"
set LIBS=kernel32.lib user32.lib advapi32.lib

REM Ensure output directory exists
if not exist "build\tools\avb_test\x64\Debug" mkdir "build\tools\avb_test\x64\Debug"

echo.
echo Building ChatGPT5 I226 TAS Validation...
%MSVC_PATH%\cl %COMPILE_FLAGS% %INCLUDE_FLAGS% /Fo"build\tools\avb_test\x64\Debug\\" /Fe"build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe" "tools\avb_test\chatgpt5_i226_tas_validation.c" /link %LINK_FLAGS% %LIBS%

echo.
echo Building Hardware Investigation Tool...
%MSVC_PATH%\cl %COMPILE_FLAGS% %INCLUDE_FLAGS% /Fo"build\tools\avb_test\x64\Debug\\" /Fe"build\tools\avb_test\x64\Debug\hardware_investigation_tool.exe" "tools\avb_test\hardware_investigation_tool.c" /link %LINK_FLAGS% %LIBS%

echo.
echo Building TSN IOCTL Handlers Test...
%MSVC_PATH%\cl %COMPILE_FLAGS% %INCLUDE_FLAGS% /Fo"build\tools\avb_test\x64\Debug\\" /Fe"build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe" "tools\avb_test\test_tsn_ioctl_handlers_um.c" /link %LINK_FLAGS% %LIBS%

echo.
echo Building TSN Hardware Activation Validation...
%MSVC_PATH%\cl %COMPILE_FLAGS% %INCLUDE_FLAGS% /Fo"build\tools\avb_test\x64\Debug\\" /Fe"build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe" "tools\avb_test\tsn_hardware_activation_validation.c" /link %LINK_FLAGS% %LIBS%

echo.
echo Build complete! All investigation tools should now be available.
echo Ready for hardware evidence reproduction on other systems.