"""Bean materials on real files: decals over a base, an opaque logo, no water cleared.

usage: python3 port/tests/test_bean_material.py XBLA_FILES [LOADER_C]
(or GEVR_BEAN_FILES). XBLA_FILES holds new/; the player's own files, else SKIP.
LOADER_C swaps in another gevr_bean_load.c, to show the checks fail on it.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    files = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_BEAN_FILES")
    if not files or not (Path(files) / "new").is_dir():
        print("SKIP: pass the XBLA files folder to check the Bean materials")
        return
    loader = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "port/src/gevr_bean_load.c"
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("BEAN_SANITIZE") else []
    with tempfile.TemporaryDirectory(prefix="gevr-bean-material-") as temp:
        exe = Path(temp) / "material"
        subprocess.run([
            os.environ.get("CC") or shutil.which("gcc") or "cc", "-std=c11", "-O2", "-include", "stdarg.h",
            *sanitize, "-I" + str(ROOT / "port/include"),
            str(ROOT / "port/tests/gevr_bean_material_test.c"), str(loader),
            *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_tex", "gevr_bean_logic")],
            "-lm", "-o", str(exe)], check=True)
        run = subprocess.run([str(exe), files], capture_output=True, text=True)
    print(run.stdout.strip())
    if run.returncode:
        print(run.stderr[-800:])
        sys.exit(1)


main()
