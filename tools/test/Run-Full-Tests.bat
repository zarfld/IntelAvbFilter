@echo off
REM Wrapper: Run Full Tests
REM Calls canonical Run-Tests.ps1 with preset parameters
powershell -ExecutionPolicy Bypass -File "%~dp0\Run-Tests.ps1" -Full
