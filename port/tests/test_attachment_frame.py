"""Test that an optic can use only its attachment's current drawn frame."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from gevr_testcc import host_libc

ROOT = Path(__file__).resolve().parents[2]


def main():
    cc = os.environ.get("CC") or shutil.which("gcc") or "cc"
    with tempfile.TemporaryDirectory(prefix="gevr-attachment-frame-") as temp:
        exe = Path(temp) / ("attachment_frame.exe" if os.name == "nt" else "attachment_frame")
        cmd = [cc, "-std=gnu11", "-O1", "-fms-extensions", "-include", "stdarg.h",
               "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US", *host_libc(),
               *["-I" + str(ROOT / path) for path in (".", "include", "src", "src/game", "port/include")],
               str(Path(__file__).with_name("attachment_frame_native.c").resolve()), "-o", str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        if build.returncode:
            raise RuntimeError(build.stderr)
        subprocess.run([str(exe)], check=True)
    print("PASS: attachment poses, mirroring, next-tick lookup, and stale or undrawn frame rejection; another "
          "player's kept apart, drawn once on its held gun at the level's scale, this player's own placed first; its "
          "optic's folded housing as told; its pack gun body in place of the item's model (hand lists, flash at its "
          "muzzle), for its gun alone, reset, gone with its player, within the shared files; a guard's pack mesh once, "
          "at the level's scale")


if __name__ == "__main__":
    main()
