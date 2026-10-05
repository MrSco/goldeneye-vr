"""Run production interpolation, XR bridge, and fresh-draw uniform code natively.

Only OpenXR tracking and GL uniform calls are mocked. The C implementation is
compiled as C, then linked to extracted production C++ integration functions.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    match = re.search(re.escape(signature) + r"(?:\s|//[^\n]*\n)*\{", source)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


xr = (ROOT / "port/vr/vr_openxr.cpp").read_text(encoding="utf-8")
gl = (ROOT / "port/fast3d/gfx_opengl.cpp").read_text(encoding="utf-8")
game = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
fixture = (ROOT / "port/tests/locomotion_native.cpp").read_text(encoding="utf-8")
runtime = [
    'extern "C" void gevrVrLocomotionReset(void)',
    'extern "C" void gevrVrLocomotionSnapshot(const float position[3], const float tracking[3],\n                                        float yaw, uint64_t sequence)',
    'extern "C" void gevrVrCameraWorld(const float position[3], const float look[3], const float up[3])',
    'static void vr_quat_to_mat3(const XrQuaternionf& q, float m[9])',
    'static bool vr_locomotion_camera(const GevrPresentationCamera& source,\n                                const GevrLocomotionPose& confirmed,\n                                const std::array<XrView, 2>& sourceViews)',
    'extern "C" int gevrVrPresentationDelta(float out[16])',
    'extern "C" void gevrVrMarkEyesRendered(int stereo)',
    'extern "C" int gevrVrRedrawDelta(float out[16])',
    'extern "C" int gevrVrRedrawHandDelta(int hand, float out[16])',
    'extern "C" void gevrVrMarkRedrawn(void)',
]
backend = [
    'static void gevr_eye_proj_times(const float* delta, float M[16])',
    'void gfx_vr_eye_record(bool on, const float* proj, bool invert_y, const float* presentation)',
    'void gfx_vr_eye_hand(int ctrl)',
    'static void gevr_eye_present_draw(void)',
]
fixture = fixture.replace("/* INSERT_RUNTIME */", "\n\n".join(function(xr, s) for s in runtime))
fixture = fixture.replace("/* INSERT_BACKEND */", "\n\n".join(function(gl, s) for s in backend))
fixture = fixture.replace("/* INSERT_TELEPORT */", function(game, "void gevrNotifyTeleport(void)"))
with tempfile.TemporaryDirectory(prefix="gevr-locomotion-") as temp:
    temp = Path(temp)
    cpp, obj, exe = temp / "test.cpp", temp / "locomotion.o", temp / "test.exe"
    cpp.write_text(fixture, encoding="utf-8")
    includes = ["-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "OpenXR/Include")]
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    *includes, "-c", str(ROOT / "port/src/gevr_locomotion.c"), "-o", str(obj)], check=True)
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                    *includes, str(cpp), str(obj), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
