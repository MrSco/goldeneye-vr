"""Compile the actual game edge-slide function and clearance helper natively."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/game/bondview2.c").read_text(encoding="utf-8")
signature = "s32 bondviewTryEdgeMovePlayerCollision(struct coord3d *prior_next_pos, struct coord3d *collision_pt0, struct coord3d *collision_pt1)"
m = re.search(re.escape(signature) + r"\s*\{", source)
end, depth = m.end(), 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
fixture = (root / "port/tests/collision_slide_native.c").read_text(encoding="utf-8")
fixture = fixture.replace("/* INSERT_EDGE_MOVE */", source[m.start():end])
with tempfile.TemporaryDirectory(prefix="gevr-collision-slide-") as temp:
    temp = Path(temp)
    test, exe = temp / "test.c", temp / "test.exe"
    test.write_text(fixture)
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "port/include"), str(test),
                    str(root / "port/src/gevr_collision_slide.c"),
                    str(root / "port/src/gevr_frame_timing.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
