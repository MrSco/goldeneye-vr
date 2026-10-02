"""Native regression checks for issue #30's production water loader and math."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "port/fast3d/gfx_pc.cpp").read_text(encoding="utf-8")
start = source.index("static void gfx_sp_sky_vertex(")
end = source.index("static void gfx_sp_modify_vertex(", start)
fixture = (root / "port/tests/surface_native.cpp").read_text(encoding="utf-8")
fixture = fixture.replace("/* INSERT_SKY_VERTEX_LOADER */", source[start:end])
with tempfile.TemporaryDirectory(prefix="gevr-surfaces-") as temp:
    cpp = Path(temp) / "surface_native.cpp"
    exe = Path(temp) / "surface_native.exe"
    cpp.write_text(fixture, encoding="utf-8")
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", "-static",
                    "-I" + str(root / "port/include"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
