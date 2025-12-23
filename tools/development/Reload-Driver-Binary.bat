@echo off
REM Wrapper: Reload Driver Binary
REM Calls force_driver_reload.ps1 (the 30-line binary reload script)
powershell -ExecutionPolicy Bypass -File "%~dp0\force_driver_reload.ps1"
