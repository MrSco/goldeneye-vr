"""Run gevr_modblood.c against a stub game: its sheets, its switches and clearing.

Each case writes the seven effect sheets into a folder, with at most one bad,
and runs the module in a process of its own (it tries its sheets once, as the
game does once a boot):
- seven good sheets load; Blood patch off and co-op keep the stock hit puffs;
  blood(false) clears what's on screen;
- a missing sheet, a wrong size, a bad signature and a file over 1 MiB each
  refuse the call, and the stock effects stay.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gevr_testcc import host_libc  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("spray", "stream", "splat", "ribbon", "mist", "pool", "drop")


def png(width, height, pad=0):
    """A small valid PNG header and data stream; pad adds a trailing chunk."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = zlib.compress(b"\0" * (height * (1 + width * 4)))
    body = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", raw)
    if pad:
        body += chunk(b"zzZz", b"\0" * pad)
    return b"\x89PNG\r\n\x1a\n" + body + chunk(b"IEND", b"")


def sheets(folder, bad=None, what=None):
    folder.mkdir()
    for name in NAMES:
        data = png(1024, 128)
        if name == bad:
            if what == "missing":
                continue
            if what == "size":
                data = png(512, 128)
            if what == "signature":
                data = b"\x89PNX" + data[4:]
            if what == "big":
                data = png(1024, 128, pad=1024 * 1024)
        (folder / f"blood-{name}.png").write_bytes(data)


def main():
    cc = os.environ.get("CC") or shutil.which("gcc") or "cc"
    with tempfile.TemporaryDirectory(prefix="gevr-modblood-") as temp:
        temp = Path(temp)
        exe = temp / "modblood_native.exe"
        cmd = [cc, "-std=gnu11", "-O1", "-fms-extensions", "-include", "stdarg.h",
               "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DGEVR_PC", "-DVERSION_US", *host_libc(),
               *["-I" + str(ROOT / path) for path in (".", "include", "include/PR", "src", "src/game", "port/include",
                                                       "port/include/net", "port/src", "port/src/net",
                                                       "port/external/pdphysics")],
               str(ROOT / "port/tests/modblood_native.c"), "-lm", "-o", str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        if build.returncode:
            print(build.stdout + build.stderr)
            raise SystemExit("modblood_native didn't build")
        cases = [("ok", None, None)] + [("refused", NAMES[i * 2 % 7], what)
                                         for i, what in enumerate(("missing", "size", "signature", "big"))]
        for n, (want, bad, what) in enumerate(cases):
            folder = temp / f"case{n}"
            sheets(folder, bad, what)
            run = subprocess.run([str(exe), str(folder), want], capture_output=True, text=True)
            print(f"{what or 'seven good sheets'}: {run.stdout.strip()}")
            if run.returncode:
                print(run.stderr)
                raise SystemExit(f"modblood: the {what or 'good'} case failed")
    print("modblood: sheets, switches and clearing checked")


main()
