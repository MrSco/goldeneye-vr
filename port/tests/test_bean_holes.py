"""Holes in Bean's level meshes closed at load (port/src/gevr_bean_holes.inc.h) and
its doorways fitted to GoldenEye's doors (port/src/gevr_bean_doors.inc.h).

usage: python3 port/tests/test_bean_holes.py [XBLA_FILES]
(or GEVR_BEAN_FILES). Made-up levels always; with the player's own files the
release's Statue too (new/background/statuepark/default.bin).
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
    statue = Path(files) / "new/background/statuepark/default.bin" if files else None
    with tempfile.TemporaryDirectory(prefix="gevr-bean-holes-") as directory:
        exe = Path(directory) / "holes"
        subprocess.run([os.environ.get("CC") or shutil.which("gcc") or "cc",
                        "-std=c11", "-O1", "-g", "-include", "stdarg.h",
                        "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "port/src"),
                        str(ROOT / "port/tests/bean_holes_native.c"),
                        str(ROOT / "port/src/gevr_bean_logic.c"),
                        str(ROOT / "port/src/gevr_bean_tex.c"), "-lm", "-o", str(exe)], check=True)
        doors = Path(directory) / "doors"
        subprocess.run([os.environ.get("CC") or shutil.which("gcc") or "cc",
                        "-std=c11", "-O1", "-g", "-include", "stdarg.h",
                        "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "port/src"),
                        str(ROOT / "port/tests/bean_doors_native.c"),
                        str(ROOT / "port/src/gevr_bean_logic.c"),
                        str(ROOT / "port/src/gevr_bean_tex.c"), "-lm", "-o", str(doors)], check=True)
        run = subprocess.run([str(doors)], capture_output=True, text=True)
        sys.stdout.write(run.stdout)
        if run.returncode:
            sys.stderr.write(run.stderr)
            raise SystemExit(run.returncode)
        args = [str(exe)]
        if statue and statue.is_file():
            args.append(str(statue))
        else:
            print("SKIP Statue: no XBLA files given")
        run = subprocess.run(args, capture_output=True, text=True)
        sys.stdout.write("".join(l for l in run.stdout.splitlines(True) if not l.startswith("gevr bean:")))
        if run.returncode:
            sys.stderr.write(run.stderr)
            raise SystemExit(run.returncode)


if __name__ == "__main__":
    main()
