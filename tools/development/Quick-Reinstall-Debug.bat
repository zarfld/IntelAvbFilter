@echo off
REM Wrapper: Quick Reinstall Debug
REM Calls canonical Install-Driver.ps1 with preset parameters
powershell -ExecutionPolicy Bypass -File "%~dp0\..\setup\Install-Driver.ps1" -Configuration Debug -Reinstall
