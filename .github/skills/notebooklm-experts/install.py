#!/usr/bin/env python3
"""
First-time setup for NotebookLM Experts skill.
Creates an isolated .venv and installs all dependencies.

Usage:
    python install.py
"""

import os
import sys
import venv
import subprocess
from pathlib import Path


def main():
    skill_dir = Path(__file__).parent
    venv_dir = skill_dir / ".venv"
    requirements_file = skill_dir / "requirements.txt"

    if os.name == "nt":
        venv_pip = venv_dir / "Scripts" / "pip.exe"
        venv_python = venv_dir / "Scripts" / "python.exe"
    else:
        venv_pip = venv_dir / "bin" / "pip"
        venv_python = venv_dir / "bin" / "python"

    if venv_dir.exists():
        print(f"Virtual environment already exists at {venv_dir}")
        print("Delete .venv/ and re-run to force reinstall.")
        return

    print(f"Creating virtual environment at {venv_dir} ...")
    venv.create(venv_dir, with_pip=True)
    print("Virtual environment created.")

    print("Upgrading pip ...")
    # WHY: pip.exe cannot upgrade itself on Python 3.14+; must use python -m pip
    subprocess.run(
        [str(venv_python), "-m", "pip", "install", "--upgrade", "pip"],
        check=True,
    )

    print("Installing dependencies from requirements.txt ...")
    subprocess.run(
        [str(venv_python), "-m", "pip", "install", "-r", str(requirements_file)],
        check=True,
    )

    # WHY: patchright requires real Chrome (not Chromium) for reliable anti-detection
    print("Installing Google Chrome via patchright ...")
    try:
        subprocess.run(
            [str(venv_python), "-m", "patchright", "install", "chrome"],
            check=True,
        )
    except subprocess.CalledProcessError:
        print("Warning: Chrome install failed. Run manually:")
        print(f"  {venv_python} -m patchright install chrome")

    print("\nSetup complete!")
    print("Authenticate before first use:")
    print("  python scripts/run.py auth_manager.py setup")


if __name__ == "__main__":
    main()
