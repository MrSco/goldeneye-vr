"""Native checks of production display math, refresh policy, settings and lifecycle.

Like test_surfaces.py, compile production functions with mocked runtime/GL calls;
fault injection never allocates large buffers or mutates headset settings.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
engine = (root / "port/vr/vr_openxr.cpp").read_text(encoding="utf-8")


def function(signature):
    match = re.search(re.escape(signature) + r"\s*\{", engine)
    assert match, signature
    end = match.end()
    depth = 1
    while depth:
        depth += (engine[end] == "{") - (engine[end] == "}")
        end += 1
    return engine[match.start():end]


parts = {
    "REFRESH": ["static bool vr_cache_refresh_rates(void)",
                'extern "C" int vr_get_supported_refresh_rates(int *rates, int capacity)',
                "static void vr_request_refresh_rate(void)"],
    "RESOURCES": ["static bool vr_create_swapchains()", "static bool vr_ensure_swapchain_images()",
                  "static bool vr_attach_eye_fbo(GLuint colorTexture, bool validate)", "static bool vr_create_eye_fbos()",
                  "static void vr_destroy_menu_swapchains()"],
    "LIFECYCLE": ['extern "C" bool openxr_initialize_vr(JavaVM* vm, jobject activity, ANativeWindow* window)',
                  'extern "C" void vr_shutdown()', 'extern "C" void vr_initialize()'],
}
fixture = (root / "port/tests/vr_display_native.cpp").read_text(encoding="utf-8")
for marker, signatures in parts.items():
    fixture = fixture.replace(f"/* INSERT_{marker} */", "\n\n".join(map(function, signatures)))

compiler = shutil.which("g++") or "g++"
with tempfile.TemporaryDirectory(prefix="gevr-vr-display-") as temp:
    temp = Path(temp)
    cpp = temp / "vr_display_native.cpp"
    cpp.write_text(fixture, encoding="utf-8")
    exe = temp / "vr_display_native.exe"
    subprocess.run([compiler, "-std=c++17", "-O2", "-DANDROID", "-I" + str(root / "port/vr"),
                    "-I" + str(root / "port/include"),
                    "-I" + str(root / "OpenXR/Include"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=temp, check=True)

    # Link the actual settings reader/writer and platform defaults, not copied parsing logic.
    for android in (False, True):
        exe = temp / ("settings-quest.exe" if android else "settings-desktop.exe")
        subprocess.run([compiler, "-std=c++17", "-O2", "-D_LANGUAGE_C", *(["-DANDROID"] if android else []),
                        "-include", str(root / "port/tests/vr_display_settings_stubs.h"),
                        "-I" + str(root / "include"), "-I" + str(root / "port/vr"),
                        "-I" + str(root / "port/include"),
                        str(root / "port/tests/vr_display_settings_native.cpp"),
                        str(root / "port/vr/vr_settings.cpp"), str(root / "port/vr/vr_settings_defaults.c"),
                        "-o", str(exe)], check=True)
        ini = temp / "goldeneye-vr.ini"
        ini.unlink(missing_ok=True)
        subprocess.run([str(exe)], cwd=temp, check=True, stdout=subprocess.DEVNULL)
        for fast in (0, 1):
            for bodies in (0, 12, 24, 48):
                subprocess.run([str(exe), "rules_write", str(fast), str(bodies)], cwd=temp, check=True)
                subprocess.run([str(exe), "rules_read", str(fast), str(bodies)], cwd=temp, check=True)
        ini.unlink(missing_ok=True)
        ini.write_text("[VR]\nRefreshRate=120\n", encoding="utf-8")
        subprocess.run([str(exe)], cwd=temp, check=True, stdout=subprocess.DEVNULL)
        for rate in (0, 72, 80, 90, 120, 87):
            subprocess.run([str(exe), "write", str(rate)], cwd=temp, check=True, stdout=subprocess.DEVNULL)
            subprocess.run([str(exe), "read", str(rate)], cwd=temp, check=True, stdout=subprocess.DEVNULL)
        ini.write_text("DisplayHz=-10\n", encoding="utf-8")
        subprocess.run([str(exe), "read", "0"], cwd=temp, check=True, stdout=subprocess.DEVNULL)
        for choice in (0, 1, 2):
            for gesture in (0, 1):
                subprocess.run([str(exe), "watch_write", str(choice), str(gesture)], cwd=temp, check=True)
                subprocess.run([str(exe), "watch_read", str(choice), str(gesture)], cwd=temp, check=True)
        for invalid in ("-1", "3", "999", "garbage", "", "0.5", "2junk"):
            ini.write_text("WatchFaceStatus=" + invalid + "\n", encoding="utf-8")
            subprocess.run([str(exe), "watch_read", "1", "1"], cwd=temp, check=True)
        ini.unlink(missing_ok=True)
        subprocess.run([str(exe), "fit_write"], cwd=temp, check=True, stdout=subprocess.DEVNULL)
        subprocess.run([str(exe), "fit_read"], cwd=temp, check=True, stdout=subprocess.DEVNULL)
        print("PASS: " + ("Quest" if android else "desktop") + " defaults, saved rates, Auto round trips, missing/legacy keys, load-once")
        print("PASS: gun fit, GoldenEye X's own and the scopes' trims round trip")
        print("PASS: watch defaults On, all status/gesture round trips, invalid settings and load-once")
        print("PASS: Fast reinforcements defaults off and round trips independently of every body count")
