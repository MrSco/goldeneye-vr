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
