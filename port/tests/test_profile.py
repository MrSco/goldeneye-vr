"""Deterministic clock tests for the production bounded profiling collector."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gevr-profile-") as d:
    exe=Path(d)/"test.exe"
    subprocess.run([shutil.which("gcc") or "gcc","-std=c11","-O2","-Wall","-Wextra","-Werror",
        "-DGEVR_TIMING_TEST_CLOCK","-I"+str(root/"port/include"),str(root/"port/src/gevr_frame_timing.c"),
        str(root/"port/tests/profile_native.c"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,cwd=d)
    exe=Path(d)/"metrics.exe"
    subprocess.run([shutil.which("g++") or "g++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
        "-Wno-missing-field-initializers",  # OpenXR's {type} initialization idiom
        "-I"+str(root/"port/include"),"-I"+str(root/"port/vr"),"-I"+str(root/"OpenXR/Include"),
        str(root/"port/vr/gevr_xr_metrics.cpp"),str(root/"port/tests/xr_metrics_native.cpp"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,cwd=d)
