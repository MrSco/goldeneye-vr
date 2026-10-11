"""The Dam speedboat: 1.75x retail, the north jetty, the crossing, the landing."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def test_dam_speedboat():
    with tempfile.TemporaryDirectory(prefix="gevr-speedboat-") as temp:
        temp = Path(temp)
        exe = temp / "test.exe"
        subprocess.run(
            [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-I" + str(ROOT / "port/include"),
             str(ROOT / "port/tests/speedboat_native.c"),
             str(ROOT / "port/src/gevr_speedboat.c"),
             "-lm", "-o", str(exe)],
            check=True)
        subprocess.run([str(exe)], check=True)


test_dam_speedboat()
