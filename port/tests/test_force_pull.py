"""Compile force pull's arithmetic (port/src/gevr_pull.c) and run its checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="gevr-force-pull-") as temp:
    exe = Path(temp) / "force_pull.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Werror",
                    "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/force_pull_native.c"), str(ROOT / "port/src/gevr_pull.c"),
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    # As the game links it: GoldenEye's own atan2f/acosf/asinf over libm's.
    game = [str(ROOT / "src/game" / name) for name in ("math_atan2f.c", "math_asinfacosf.c", "math_asinacos.c")]
    exe = Path(temp) / "force_pull_game_math.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-D_LANGUAGE_C",
                    "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "include"), "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "src/game"),
                    str(ROOT / "port/tests/force_pull_native.c"), str(ROOT / "port/src/gevr_pull.c"), *game,
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
