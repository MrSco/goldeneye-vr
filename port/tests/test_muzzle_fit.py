"""Run bondview2.c's barrel-tip fit check: a pack body's authored muzzle turns it off."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
production = function(view, "s32 gevrMuzzleFitAvailable(void)")
fixture = (ROOT / "port/tests/muzzle_fit_native.c").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="gevr-muzzle-fit-") as temp:
    source, exe = Path(temp) / "muzzle_fit_native.c", Path(temp) / "muzzle_fit_native.exe"
    source.write_text(fixture.replace("/* PRODUCTION */", production), encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Werror", "-Wno-unused-function",
                    str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
