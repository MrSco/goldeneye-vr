"""Exercise production camera clearance against synthetic native triangles.

No ROM/device is needed; geometry enumeration is mocked while the game's
actual ray/triangle intersection, ceiling solver and camera context run.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
bg = (ROOT / "src/game/bg.c").read_text(encoding="utf-8")
game = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
fixture = (ROOT / "port/tests/camera_ceiling_native.c").read_text(encoding="utf-8")
fixture = fixture.replace("/* INSERT_BG */", block(bg, "void gevrBgClampCameraCeiling("))
fixture = fixture.replace("/* INSERT_GAME */", "\n".join(block(game, signature) for signature in (
    "void gevrClampCameraCeiling(", "static void gevrGameCameraCeiling(")))

with tempfile.TemporaryDirectory(prefix="gevr-camera-ceiling-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test"
    source.write_text(fixture, encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O1", "-g", "-fms-extensions",
                    "-D_POSIX_C_SOURCE=200809L",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                    "-I" + str(ROOT), "-I" + str(ROOT / "include"),
                    "-I" + str(ROOT / "src"), "-I" + str(ROOT / "port/include"),
                    str(source), str(ROOT / "src/game/line_tri_intersect.c"),
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
