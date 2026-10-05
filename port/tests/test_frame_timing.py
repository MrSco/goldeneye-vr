"""Production CPU collector, asynchronous GPU timer and cached-eye replay tests."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
gcc, gxx = shutil.which("gcc") or "gcc", shutil.which("g++") or "g++"


def extract(source, signature):
    match = re.search(re.escape(signature) + r"\s*\{", source)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


with tempfile.TemporaryDirectory(prefix="gevr-frame-timing-") as temp:
    temp = Path(temp)
    obj = temp / "timing.o"
    inc = ["-I" + str(ROOT / "port/include")]
    flags = ["-O2", "-Wall", "-Wextra", "-Werror"]
    subprocess.run([gcc, "-std=c11", *flags, *inc, "-c",
                    str(ROOT / "port/src/gevr_frame_timing.c"), "-o", str(obj)], check=True)
    exe = temp / "cpu.exe"
    subprocess.run([gcc, "-std=c11", *flags, *inc,
                    str(ROOT / "port/tests/frame_timing_native.c"), str(obj), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    game = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
    start = game.index('    // This `if` block looks like Perfect Dark bbike0f0d3c60')
    end = game.index('    /**', start)
    fixture = (ROOT / "port/tests/collision_paths_native.c").read_text(encoding="utf-8")
    fixture = fixture.replace("/* INSERT_WRAPPER */", extract(game,
        "static s32 gevrBondMoveResult(GevrMoveAttempt kind, s32 result,\n    const struct coord3d *edge0, const struct coord3d *edge1)"))
    fixture = fixture.replace("/* INSERT_PATH */", game[start:end])
    cpp = temp / "collision.c"
    cpp.write_text(fixture)
    exe = temp / "collision.exe"
    subprocess.run([gcc, "-std=c11", *flags, *inc, str(cpp), str(obj), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    (temp / "SDL.h").write_text('void *SDL_GL_GetProcAddress(const char *name);\n')
    exe = temp / "gpu.exe"
    subprocess.run([gxx, "-std=c++17", *flags, *inc, "-I" + str(temp),
                    "-I" + str(ROOT / "port/fast3d"),
                    str(ROOT / "port/tests/gpu_timer_native.cpp"),
                    str(ROOT / "port/fast3d/gevr_gpu_timer.cpp"), str(obj), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    gl = (ROOT / "port/fast3d/gfx_opengl.cpp").read_text(encoding="utf-8")
    fixture = (ROOT / "port/tests/replay_state_native.cpp").read_text(encoding="utf-8")
    fixture = fixture.replace("/* INSERT_DRAW_STRUCT */", extract(gl, "struct GevrEyeDraw") + ";")
    fixture = fixture.replace("/* INSERT_REPLAY */", extract(gl,
        "void gfx_vr_eye_replay(const float* delta, const float* hand0, const float* hand1)"))
    cpp = temp / "replay.cpp"
    cpp.write_text(fixture)
    exe = temp / "replay.exe"
    subprocess.run([gxx, "-std=c++17", *flags, str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
