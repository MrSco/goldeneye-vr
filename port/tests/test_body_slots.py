"""Compile the body slots' arithmetic (port/src/gevr_bodyslot.c) and run its checks."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

# the slots' categories are the wheel's, in the same order
view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
header = (ROOT / "port/include/gevr_bodyslot.h").read_text(encoding="utf-8")
wheel = re.search(r"enum \{ (GEVR_WC_PISTOLS[^}]*)\}", view).group(1)
wheel = [name.strip().replace("GEVR_WC_", "") for name in wheel.split(",") if name.strip() != "GEVR_WC_COUNT"]
slots = re.search(r"enum \{\s*(GEVR_BODY_CAT_PISTOLS[^}]*)\}", header).group(1)
slots = [name.strip().replace("GEVR_BODY_CAT_", "") for name in slots.split(",") if name.strip()]
assert wheel == slots, (wheel, slots)

with tempfile.TemporaryDirectory(prefix="gevr-body-slots-") as temp:
    exe = Path(temp) / "body_slots.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Werror",
                    "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/body_slots_native.c"), str(ROOT / "port/src/gevr_bodyslot.c"),
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
