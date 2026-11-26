"""A pack body's authored muzzle, against the game's own code with stub callers.

- bondview2.c gevrMuzzleFitAvailable: the muzzle turns Gun fit's barrel tip off.
- gevr_modgame.c gevrModGameGunModel with input.c gevrIsThrowable: a thrown
  weapon refuses a muzzle before anything loads; a gun takes it.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run(fixture_name, production, *flags):
    fixture = (ROOT / "port/tests" / fixture_name).read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="gevr-muzzle-fit-") as temp:
        source, exe = Path(temp) / fixture_name, Path(temp) / (fixture_name + ".exe")
        source.write_text(fixture.replace("/* PRODUCTION */", production), encoding="utf-8")
        subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Werror", "-Wno-unused-function",
                        *flags, str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
run("muzzle_fit_native.c", function(view, "s32 gevrMuzzleFitAvailable(void)"))

game = (ROOT / "src/game/gevr_modgame.c").read_text(encoding="utf-8")
inputs = (ROOT / "port/src/input.c").read_text(encoding="utf-8")
constants = (ROOT / "src/bondconstants.h").read_text(encoding="utf-8")
# the game's item ids, as bondconstants.h numbers them
items = constants[constants.index("typedef enum ITEM_IDS"):]
items = items[:items.index("} ITEM_IDS;") + len("} ITEM_IDS;")]
run("gun_model_native.c",
    "typedef int s32;\n" + items + "\n" + function(inputs, "s32 gevrIsThrowable(s32 item)") + "\n"
    + function(game, "int gevrModGameGunModel(int hand, int item, const char *name"))

gun = (ROOT / "src/game/gun.c").read_text(encoding="utf-8")
mesh = (ROOT / "src/game/gevr_modmesh.c").read_text(encoding="utf-8")
tables = gun[gun.index("#define GEVR_MODGUN_NAME"):gun.index("static const char *gevrModGunModelFor(")]
run("support_fit_native.c", tables + "\n" + "\n".join(
    function(source, name) for source, name in (
        (mesh, "void gevrModMeshPoint("),
        (gun, "static const char *gevrModGunModelFor("),
        (gun, "static s32 gevrModGunShownOk("),
        (gun, "static s32 gevrModGunFittedItem("),
        (gun, "s32 gevrModGunSupport("),
        (gun, "void gevrModGunModelSet("),
        (gun, "void gevrModGunModelReset("),
    )))
