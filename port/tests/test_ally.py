"""Run the production campaign companion controller on a synthetic mission."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="gevr-ally-") as temp:
    exe = Path(temp) / "ally-test"
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
            "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + value for value in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                        "VERSION=2", "VERSION_US=1", "LANG_US=1", "REFRESH_NTSC=1")]
    args += ["-I" + str(ROOT / path) for path in
             (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    subprocess.run(args + [str(ROOT / "src/game/gevr_ally.c"),
                           str(ROOT / "port/tests/ally_native.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
