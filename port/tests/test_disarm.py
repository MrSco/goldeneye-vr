"""Compile a disarmed guard's decisions (port/src/gevr_disarm.c) and run their checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="gevr-disarm-") as temp:
    exe = Path(temp) / "disarm.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Werror",
                    "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/disarm_native.c"), str(ROOT / "port/src/gevr_disarm.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
