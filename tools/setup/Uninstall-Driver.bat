@echo off
REM Wrapper: Uninstall Driver
REM Calls canonical Install-Driver.ps1 with preset parameters
powershell -ExecutionPolicy Bypass -File "%~dp0\Install-Driver.ps1" -UninstallDriver
