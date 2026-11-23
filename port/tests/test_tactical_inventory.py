"""Exercise the Tactical inventory and tracked interaction state machine."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CC = os.environ.get("CC") or shutil.which("clang") or "gcc"

with tempfile.TemporaryDirectory(prefix="gevr-tactical-inventory-") as scratch:
    native = Path(scratch) / "inventory-test.exe"
    subprocess.run([CC, "-std=c11", "-Wall", "-Wextra", "-Wno-missing-field-initializers",
                    "-I", str(ROOT / "port/include"), str(ROOT / "port/tests/tactical_inventory_native.c"),
                    str(ROOT / "port/src/gevr_tactical_inventory.c"), "-lm", "-o", str(native)], check=True)
    subprocess.run([str(native)], check=True)
