"""Run the production frame-pool regression harness for crash report 4bde84c2."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
fixture = (ROOT / "port/tests/dyn_native.c").read_text(encoding="utf-8")
fixture = fixture.replace("../../src/game/dyn.c", (ROOT / "src/game/dyn.c").as_posix())
with tempfile.TemporaryDirectory(prefix="gevr-dyn-") as temp:
    source = Path(temp) / "dyn_native.c"
    executable = Path(temp) / "dyn_native.exe"
    source.write_text(fixture, encoding="utf-8")
    command = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
               "-Wno-builtin-declaration-mismatch"]
    command += ["-D" + value for value in (
        "GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    command += ["-I" + str(ROOT / directory) for directory in (
        ".", "port/include", "include", "src", "src/game", "port/src")]
    command += [str(source), "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
