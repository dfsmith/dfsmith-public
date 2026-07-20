#!/usr/bin/env python3
"""Build a dfsmith.net MSIX package for move_windows_to_mouse."""

import subprocess
import sys
from pathlib import Path

script = Path(__file__).with_name("build_msix.py")
if not script.exists():
    raise FileNotFoundError(f"Missing build script: {script}")

command = [
    sys.executable,
    str(script),
    "--publisher",
    "CN=dfsmith.net",
    "--identity",
    "MoveWindowsToMouse.App",
    "--display-name",
    "Move Windows to Mouse",
    "--publisher-display-name",
    "dfsmith.net",
    "--version",
    "1.0.0.0",
    "--certificate",
    "dfsmith.pfx",
]

print("Running dfsmith MSIX build:", " ".join(command))
subprocess.run(command, check=True)
