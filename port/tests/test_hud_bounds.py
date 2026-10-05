"""Compile the production asynchronous HUD crop against a strict GL driver mock."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gevr-hud-bounds-") as temp:
    exe = Path(temp) / "test.exe"
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "port/fast3d"),
                    str(ROOT / "port/tests/hud_bounds_native.cpp"),
                    str(ROOT / "port/fast3d/gevr_hud_bounds.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
