"""Compile and run the shared Tactical preset codec test."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CC = os.environ.get("CC") or shutil.which("clang") or "gcc"
with tempfile.TemporaryDirectory(prefix="gevr-preset-") as scratch:
    native = Path(scratch) / "preset-test.exe"
    subprocess.run([CC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "port/include"), str(ROOT / "port/tests/tactical_presets_native.c"),
                    str(ROOT / "port/src/gevr_tactical_presets.c"), "-o", str(native)], check=True)
    subprocess.run([str(native), *sys.argv[1:]], check=True)
