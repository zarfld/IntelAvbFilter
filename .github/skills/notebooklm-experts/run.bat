@echo off
:: Runs any notebooklm-experts script using the isolated .venv Python.
:: Usage: run.bat auth_manager.py status
::        run.bat ask_question.py --question "..."

set "SKILL_DIR=%~dp0"
set "VENV_PYTHON=%SKILL_DIR%.venv\Scripts\python.exe"

if not exist "%VENV_PYTHON%" (
    echo [notebooklm-experts] .venv not found. Run setup first:
    echo   python install.py
    exit /b 1
)

"%VENV_PYTHON%" "%SKILL_DIR%scripts\run.py" %*
