@echo off
REM Wrapper: Check Driver Status
REM Calls canonical Check-System.ps1
powershell -ExecutionPolicy Bypass -File "%~dp0\Check-System.ps1"
