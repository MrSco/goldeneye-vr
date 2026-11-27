"""A pack gun's own hand (mod.weapon.hands), against the game's own code with stub callers.

- gun.c's glove table and lookups, and gevr_modgame.c's check: which glove a
  hand draws in each pose and with whose fit, only on the pack body it was
  given for; the trigger; the support glove only while that gun is held
  two-handed; refusals; a cleared hand, another gun and a new mission.
- Where the game draws them: the glove in the stock hand's place, the support
  glove with the gun, and the off hand's own arm left out meanwhile.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    """The definition (not a declaration) that starts with signature."""
    def closes(at):
        depth, i = 0, source.index("(", at)
        while True:
            depth += (source[i] == "(") - (source[i] == ")")
            if depth == 0:
                return i
            i += 1
    start = source.index(signature)
    while source[closes(start) + 1:].lstrip()[0] != "{":
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
game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode().replace("\r\n", "\n")
mesh = (ROOT / "src/game/gevr_modmesh.c").read_bytes().decode().replace("\r\n", "\n")
tables = gun[gun.index("#define GEVR_MODGUN_NAME"):gun.index("static const char *gevrModGunModelFor(")]
production = "\n".join([tables] + [function(gun, name) for name in (
    "static const char *gevrModGunModelFor(",
    "static s32 gevrModGunShownOk(",
    "static s32 gevrModGunFittedItem(",
    "void gevrModGunHandsSet(",
    "const GevrModMesh *gevrModGunHand(",
    "s32 gevrModGunSupportGloveShown(",
    "s32 gevrModGunTriggerHeld(",
    "s32 gevrModGunGrip(",
    "void gevrModGunModelSet(",
    "void gevrModGunModelReset(",
)] + [function(game, "int gevrModGameGunHands("), function(mesh, "void gevrModMeshPoint(")])
fixture = (ROOT / "port/tests/pack_hands_native.c").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="gevr-pack-hands-") as temp:
    source, exe = Path(temp) / "pack_hands_native.c", Path(temp) / "pack_hands_native.exe"
    source.write_text(fixture.replace("/* PRODUCTION */", production), encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Werror", "-Wno-unused-function",
                    "-DGEVR=1", "-I", str(ROOT / "port/include"), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

fire = (ROOT / "src/game/gunfire.c").read_bytes().decode().replace("\r\n", "\n")
body = fire[fire.index("const GevrModMesh *mesh = gevrModGunMesh(handnum);"):]
body = body[:body.index("subdraw(&renderdata, &handptr->weaponModel);")]
check(body.index("gevrModGunTriggerHeld(handnum)") < body.index("GEVR_MODHAND_READY")
      < body.index("modelDrawGunHand(&renderdata") and "else\n" in body[body.index("GEVR_MODHAND_READY"):body.index("modelDrawGunHand(&renderdata")],
      "the trigger's glove, else ready, else the stock hand and sleeve, on the pack body")
check(body.index("gevrModGunSupportGloveShown(handnum)") < body.index("GEVR_MODHAND_SUPPORT"),
      "the support glove with the gun while the other hand holds it")
arm = fire[fire.index("if (gevrStereoTwoHandGrip() && !gevrGexSupportHandShown(gevrStereoTwoHandGun())"):]
arm = arm[:arm.index("gevrRenderBeanGripArm")]
check("!gevrModGunSupportGloveShown(gevrStereoTwoHandGun())" in arm, "and the off hand's own arm not drawn meanwhile")
view = (ROOT / "src/game/bondview2.c").read_bytes().decode().replace("\r\n", "\n")
matrix = function(view, "s32 gevrStereoGunMatrix(")
check("out->m[3][i] = pos[i] + ((VrLeftHandedMode ? -off[0] : off[0]) * right[i] + off[1] * up[i]\n"
      "                                 + (GEVR_GRIP_TO_ORIGIN_CM + off[2]) * back[i]) * cm * size;" in matrix
      and "out->m[0][i] = (VrLeftHandedMode ? right[i] : -right[i]) * k;" in matrix,
      "the root and axes the fixture checks the grip through are bondview2's own")
check(matrix.index("gevrGunOff(handnum, off);") < matrix.index("gevrModGunGrip(handnum, &grip)")
      < matrix.index("gevrModGripOff(grip.f, player, VrGunOffDefault, GEVR_VIEWMODEL_CM, GEVR_GRIP_TO_ORIGIN_CM, off);")
      < matrix.index("gevrRackNudge(&s_gevrRack[handnum])") and "s_gevrGunOffOverride == NULL && gevrModGunGrip" in matrix,
      "the grip takes the player's fit as trim, before the rack's nudge, never over a fit override")
check("#define GEVR_VIEWMODEL_CM 0.85f" in view and "#define GEVR_GRIP_TO_ORIGIN_CM 12.0f" in view,
      "the fixture's view and grip constants are bondview2's")
defaults = (ROOT / "port/vr/vr_settings_defaults.c").read_bytes().decode()
own = [float(v) for v in re.findall(r"float VrGunOff[XYZ] = (-?[\d.]+)f;", defaults)]
kept = [float(v) for v in re.search(r"VrGunOffDefault\[3\] = \{ (-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f \}", defaults).groups()]
check(own == kept == [2.74, 1.94, -12.35], "the kept default fit is the shipped one, as the fixture uses")
print("PASS: pack gloves per pose and fit, the trigger, two-handed support, the grip point, refusals, resets and the draws")
