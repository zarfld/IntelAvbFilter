@echo off
REM Wrapper: Build and Sign Release
REM Calls canonical Build-Driver.ps1 with preset parameters
powershell -ExecutionPolicy Bypass -File "%~dp0\Build-Driver.ps1" -Configuration Release -Sign
