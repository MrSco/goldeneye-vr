"""A pack gun's moving parts, against the game's own code with stub callers.

- gun.c's GoldenEye slide (sub_GAME_7F05E808, sub_GAME_7F05E83C) and the pack
  action beside it (gevr_modaction.h, gevrModGunActionTick and its share): the
  same shots and ticks give the same travel, on a carrier with a stroke and on
  one without; the rack's pull; hold open; another gun; the groups' offsets.
- Where the game calls them: the shot, the tick, the body's draw and a gun
  attachment's draw.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    """The definition (not a declaration) that starts with signature."""
    start = source.index(signature)
    while source[source.index(")", start) + 1:].lstrip()[0] != "{":
        start = source.index(signature, start + 1)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def check(ok, what):
    if not ok:
        raise SystemExit("FAIL: " + what)


gun = (ROOT / "src/game/gun.c").read_bytes().decode().replace("\r\n", "\n")
tables = gun[gun.index("#define GEVR_MODGUN_NAME"):gun.index("static const char *gevrModGunModelFor(")]
action = gun[gun.index("static GevrModAction s_gevrModAction"):gun.index("static GevrModAction *gevrModGunAction(")]
production = "\n".join([tables, action] + [function(gun, name) for name in (
    "static const char *gevrModGunModelFor(",
    "static s32 gevrModGunShownOk(",
    "static s32 gevrModGunFittedItem(",
    "static GevrModAction *gevrModGunAction(",
    "static const GevrModActionTiming *gevrModGunTiming(",
    "void gevrModGunActionShot(",
    "static void gevrModGunActionTick(",
    "static void gevrModGunCopyActionTick(",
    "f32 gevrModGunActionShare(",
    "const GevrModGunParts *gevrModGunParts(",
    "void gevrModGunModelSet(",
    "void gevrModGunModelReset(",
    "void sub_GAME_7F05E808(",
    "void sub_GAME_7F05E83C(",
)])
fixture = (ROOT / "port/tests/pack_action_native.c").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="gevr-pack-action-") as temp:
    source, exe = Path(temp) / "pack_action_native.c", Path(temp) / "pack_action_native.exe"
    source.write_text(fixture.replace("/* PRODUCTION */", production), encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Werror", "-Wno-unused-function",
                    "-DGEVR=1", "-I", str(ROOT / "port/include"), str(source), "-o", str(exe), "-lm"], check=True)
    subprocess.run([str(exe)], check=True)

# The game's own calls: the shot opens it where GoldenEye's slide opens, each
# tick steps it before the hand's model is reconciled, and both draws use it.
check("gevrModGunActionShot(hand);" in function(gun, "void sub_GAME_7F05E808("),
      "a shot opens the pack action where it opens GoldenEye's slide")
load = function(gun, "void used_to_load_1st_person_model_on_demand(")
check(load.index("gevrModGunActionTick(hand);") < load.index("gevrModGunReconcile(hand);"),
      "the action steps each tick, with the hand's model choice")
fire = (ROOT / "src/game/gunfire.c").read_bytes().decode()
check("gevrModMeshDrawParts(renderdata.gdl" in fire and "gevrModGunParts(handnum, partOffset)" in fire,
      "the held body draws its groups where the action is")
attach = (ROOT / "src/game/gevr_modattach.c").read_bytes().decode().replace("\r\n", "\n")
drawGun = function(attach, "Gfx *gevrModAttachDrawGun(")
check("attachActionShare(a, hand)" in drawGun and "offset.m[3][k] += share * a->travel[k];" in drawGun,
      "a gun part moves along its travel in its anchor's frame")
share = function(attach, "static f32 attachActionShare(")
check("return gevrModGunActionShare(hand, a->handle);" in share and "&a->timing" in share,
      "a part follows the body's action, or its own with its own timing")
partTick = function(attach, "void gevrModAttachActionTick(")
check("gevrModActionShot(&a->action, item);" in partTick and "gevrModActionTicks(&a->timing, interval, &open, &close);" in partTick
      and "attachLocal()" in partTick, "a timed part steps its own action on the same shot and interval")
check("gevrModAttachActionTick(hand, held," in function(gun, "static void gevrModGunActionTick("),
      "the hand's tick steps its timed parts")
game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
check("st->AutomaticFiringRate" in function(game, "s32 gevrModGameShotInterval(") and "nativeAutomatic(item)" in function(game, "s32 gevrModGameShotInterval("),
      "the interval is an automatic's own firing rate, as gunfire.c fires it")
check("a->on != GEVR_MODATTACH_GUN" in function(attach, "int gevrModGameAttachAction("),
      "only a part on a gun takes an action")
view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
check("gevrRackShare(&s_gevrRack[hand])" in function(view, "f32 gevrRackPullShare("), "the pull is the hand rack's own")
print("PASS: pack moving parts on GoldenEye's slide rule, the rack, hold open, offsets and their call sites")
