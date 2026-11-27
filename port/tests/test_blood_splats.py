"""The Blood patch's splats on walls and floors (#342, src/game/gevr_blood.c).

Builds the splat geometry (Perfect Dark's splat sizes, the quad's sides on a
surface, wallhit.c's growth) into port/tests/blood_splat_native.c and runs it,
then checks the hooks that make the splats: the hit, the tick, the level
reset and the draw with the bullet holes.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def geometry():
    text = (ROOT / "src/game/gevr_blood.c").read_text(encoding="utf-8")
    start = text.index("/* BEGIN_BLOOD_GEOMETRY */")
    end = text.index("/* END_BLOOD_GEOMETRY */")
    return text[start:end]


def main():
    fixture = (ROOT / "port/tests/blood_splat_native.c").read_text(encoding="utf-8")
    fixture = fixture.replace("/* INSERT_BLOOD_GEOMETRY */", geometry())
    gcc = shutil.which("gcc") or "gcc"
    with tempfile.TemporaryDirectory(prefix="gevr-blood-splats-") as temp:
        source = Path(temp) / "blood_splat.c"
        exe = Path(temp) / "blood_splat"
        source.write_text(fixture, encoding="utf-8")
        result = subprocess.run(
            [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-function", str(source), "-o", str(exe), "-lm"],
            capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stderr)
        subprocess.run([str(exe)], check=True)

    chr_c = (ROOT / "src/game/chr.c").read_text(encoding="utf-8")
    stain = chr_c.index("chrCreateBloodStain(bhit->model, bhit->hitpart, bhit->node, &jointpos);")
    assert "gevrBloodChrHit(chr, &world, &shot->dir);" in chr_c[stain:stain + 600], \
        "chrHandleBulletHit: the wound's splats follow its stain"
    lv = (ROOT / "src/game/lv.c").read_text(encoding="utf-8")
    assert "gevrBloodReset();" in lv and "gevrBloodTick(g_ClockTimer);" in lv
    explosion = (ROOT / "src/game/explosion.c").read_text(encoding="utf-8")
    draw = explosion.index("gevrBloodDecals()")
    assert draw < explosion.index("VR_IMPACT_DL_END", draw), \
        "the splats draw inside the bullet holes' decal band"
    blood = (ROOT / "src/game/gevr_blood.c").read_text(encoding="utf-8")
    assert "randomGetNext" not in blood, "cosmetic: never the game's random sequence"
    print("blood splat checks passed")


if __name__ == "__main__":
    main()
