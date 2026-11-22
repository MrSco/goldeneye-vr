"""Compile Jedi & Ninja core (port/src/gevr_jedi_ninja.c) and run its checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CC = shutil.which("gcc") or "gcc"

with tempfile.TemporaryDirectory(prefix="gevr-jedi-ninja-") as temp:
    exe = Path(temp) / "jedi_ninja.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-Wall", "-Werror",
        "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/jedi_ninja_native.c"),
        str(ROOT / "port/src/gevr_jedi_ninja.c"),
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify compatibility when GoldenEye overrides math_atan2f/asinf/acosf.
    game = [str(ROOT / "src/game" / name) for name in ("math_atan2f.c", "math_asinfacosf.c", "math_asinacos.c")]
    exe = Path(temp) / "jedi_ninja_game_math.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-D_LANGUAGE_C", "-Wno-incompatible-pointer-types",
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "src"),
        "-I" + str(ROOT / "src/game"),
        str(ROOT / "port/tests/jedi_ninja_native.c"),
        str(ROOT / "port/src/gevr_jedi_ninja.c"),
        *game,
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
