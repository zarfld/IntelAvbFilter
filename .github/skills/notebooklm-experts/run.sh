#!/usr/bin/env bash
# Runs any notebooklm-experts script using the isolated .venv Python.
# Usage: ./run.sh auth_manager.py status
#        ./run.sh ask_question.py --question "..."

SKILL_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_PYTHON="$SKILL_DIR/.venv/bin/python"

if [ ! -f "$VENV_PYTHON" ]; then
    echo "[notebooklm-experts] .venv not found. Run setup first:"
    echo "  python install.py"
    exit 1
fi

"$VENV_PYTHON" "$SKILL_DIR/scripts/run.py" "$@"
