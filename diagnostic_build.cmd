@echo off
echo Diagnostic: Checking build outputs and paths
echo =============================================

echo.
echo Current directory: %CD%
echo.

echo Checking if build directory exists:
if exist "build\tools\avb_test\x64\Debug" (
    echo ? build\tools\avb_test\x64\Debug exists
    echo Contents:
    dir "build\tools\avb_test\x64\Debug\*.exe" 2>nul
    if errorlevel 1 echo ? No .exe files found
) else (
    echo ? build\tools\avb_test\x64\Debug does not exist
)

echo.
echo Testing one critical build with full output:
echo ===========================================

echo Changing to tools\avb_test directory...
cd tools\avb_test

echo Current directory: %CD%

echo.
echo Calling vcvars64.bat...
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo Building ChatGPT5 I226 TAS Validation with FULL output:
nmake -f chatgpt5_i226_tas_validation.mak clean
nmake -f chatgpt5_i226_tas_validation.mak

echo.
echo Checking what was created:
if exist "..\..\build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe" (
    echo ? File created at: ..\..\build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe
) else (
    echo ? File NOT created at: ..\..\build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe
)

echo.
echo Returning to root directory...
cd ..\..

echo Current directory: %CD%

echo.
echo Checking from root if file exists:
if exist "build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe" (
    echo ? File found from root: build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe
) else (
    echo ? File NOT found from root: build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe
)

echo.
echo Diagnostic complete.