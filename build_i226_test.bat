@echo off
echo Building I226 AVB Test Tool...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "tools/vs_compile.ps1" -BuildCmd "cl /nologo /W4 /Zi /I include /I external/intel_avb/lib /I . tools/avb_test/avb_test_um.c /Fe:avb_test_i226.exe"

if %errorLevel% EQU 0 (
    echo.
    echo Build successful! Created: avb_test_i226.exe
    echo.
    echo Run it with:
    echo   avb_test_i226.exe selftest
) else (
    echo.
    echo Build failed!
)

pause
