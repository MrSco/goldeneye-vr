"""Bean regression checks and optional local CAFF asset sweep.

Usage: python port/tests/test_bean.py [path/to/xbla/files]
Game files stay outside the repository and are never copied into artifacts.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gevr-bean-") as directory:
    exe = Path(directory) / "bean.exe"
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Wextra",
        "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/gevr_bean_assets_test.c"),
        *[str(ROOT / "port/src" / (name + ".c")) for name in
          ("gevr_bean_load", "gevr_bean_logic", "gevr_bean_tex")],
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
    loader = Path(directory) / "loader.exe"
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/gevr_bean_loader_test.c"),
        str(ROOT / "port/src/gevr_bean_logic.c"), str(ROOT / "port/src/gevr_bean_tex.c"),
        "-lm", "-o", str(loader),
    ], check=True)
    subprocess.run([str(loader)], check=True)
    render = Path(directory) / "render.exe"
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-fms-extensions", "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
        *["-I" + str(ROOT / path) for path in (".", "include", "src", "src/game", "port/include")],
        str(ROOT / "port/tests/gevr_bean_render_test.c"), "-lm", "-o", str(render),
    ], check=True)
    for name in ("char/boilerbond", "gun/ak47"):
        marker = Path(directory) / "added-content/xbla/files/new" / name / "default.bin"
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.touch()
    subprocess.run([str(render)], cwd=directory, check=True)
    source = (ROOT / "src/game/bgroomtrans.c").read_text()
    start = source.index("Gfx * applyRoomMatrixToDisplayList(")
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    room = Path(directory) / "room.c"
    room.write_text((ROOT / "port/tests/gevr_bean_room_test.c").read_text()
                    .replace("/* PRODUCTION */", source[start:end]))
    room_exe = Path(directory) / "room.exe"
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-D_LANGUAGE_C", "-DGEVR", "-DPLATFORM_64BIT=1",
        "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"), str(room), "-o", str(room_exe),
    ], check=True)
    subprocess.run([str(room_exe)], check=True)
    if len(sys.argv) > 1:
        assets = Path(sys.argv[1]).resolve() / "new"
        samples = sorted(path for category in ("char", "head", "gun", "prop", "background", "skydome")
                         for path in (assets / category).glob("*/default.bin"))
        assert samples, "No local CAFF meshes found"
        failures = []
        fallbacks = 0
        for sample in samples:
            run = subprocess.run([str(exe), str(sample)], capture_output=True, text=True)
            if run.returncode:
                failures.append(sample.relative_to(assets))
                print(run.stderr, end="")
            elif run.stdout.startswith("FALLBACK"):
                fallbacks += 1
                print(run.stdout, end="")
        print(f"Bean asset sweep: {len(samples) - len(failures)}/{len(samples)} passed")
        print(f"{len(samples) - len(failures) - fallbacks} meshes, {fallbacks} N64 fallbacks")
        if failures:
            raise SystemExit("Failed assets: " + ", ".join(map(str, failures)))
