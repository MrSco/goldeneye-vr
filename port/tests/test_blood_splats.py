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
    walls = explosion.index("gevrBloodDecalsDraw(gdl, 0)")
    floors = explosion.index("gevrBloodDecalsDraw(gdl, 1)")
    band_end = explosion.index("VR_IMPACT_DL_END */")
    assert walls < band_end < floors and explosion.count("VR_IMPACT_DL_END */") == 1, \
        "wall splats draw in the bullet holes' decal band (a flat splat on a rock wall " \
        "was cut off), floor splats after it with the plain offset (the band painted a " \
        "pool over the body lying in it)"
    assert "d->floor = normal[1] > 0.7f;" in (ROOT / "src/game/gevr_blood.c").read_text(encoding="utf-8")
    blood = (ROOT / "src/game/gevr_blood.c").read_text(encoding="utf-8")
    assert "randomGetNext" not in blood, "cosmetic: never the game's random sequence"
    assert "sub_GAME_7F04E9BC(prop, &shot);" in blood, \
        "a shot's splats test objects as the shot does (PD's objTestHit pass)"
    assert blood.count("BLOOD_POOL_TRIES") >= 3, \
        "a body with no floor below stops looking (no endless traces on the Quest)"
    assert "#ifdef GEVR_HEADLESS\n#define BLOOD_ON 0" in blood and "VrBloodPatch)" not in blood, \
        "the dedicated server makes no splats"
    create = blood[blood.index("static void bloodDecalCreate("):]
    assert create.index("if (most > 32000.0f) return;") < create.index("memset(d, 0"), \
        "a splat too far for a vertex leaves the slot's live one be"
    assert "void gevrExplosionCreateDecalOnProp(" in explosion, \
        "object splats go in the bullet holes' ring, on the object's matrix"
    print("blood splat checks passed")


if __name__ == "__main__":
    main()
