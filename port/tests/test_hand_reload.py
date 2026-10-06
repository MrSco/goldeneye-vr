"""Compile production hand-reload gestures and exercise calibrated belt entry."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-ref", help="Read game sources from a Git ref to check regressions")
args = parser.parse_args()


def read(path):
    if args.source_ref:
        return subprocess.check_output(
            ["git", "show", f"{args.source_ref}:{path}"], cwd=ROOT, text=True, encoding="utf-8")
    return (ROOT / path).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


view = read("src/game/bondview2.c")
constants = read("src/bondconstants.h")
defaults = read("port/vr/vr_settings_defaults.c")
matrix = read("src/game/matrixmath.c")
definitions = [re.search(r"typedef enum " + name + r"\b.*?\}\s*" + name + r";",
                         constants, re.S).group() for name in ("GUNHAND", "ITEM_IDS")]
for first in ("GEVR_GT_HIPDROP", "GEVR_RT_MAGRADIUS", "GEVR_GEXMAG_IN"):
    definitions.append(re.search(r"enum \{ " + first + r"\b.*?\};", view, re.S).group())
for name in ("s_gevrGestureTune", "s_gevrReloadTune"):
    definitions.append(re.search(r"static f32 " + name + r"\[.*?\};", view, re.S).group())
definitions.append(re.search(r"float VrReloadBelt\[3\] = [^;]+;", defaults).group())
for name in ("VrReloadGrab", "VrGexGunOff", "VrGexForeHold", "VrGexPp7Grab", "VrGexPp7Support", "VrGexPp7GunOff",
             "VrGexKf7MagOff", "VrGexPp7MagOff", "VrGexKf7WellOff", "VrGexPp7WellOff"):
    definitions.append(re.search(r"float " + name + r"\[[^;]+;", defaults).group())
for name in ("s_gevrGexMagItem", "s_gevrPistolGripOwner"):
    definitions.append(re.search(r"static s32 " + name + r"[^;]+;", view).group())
for name in ("GEVR_UNITS_PER_METRE", "GEVR_RELOAD_BELT_EXIT_CM", "GEVR_VIEWMODEL_CM", "GEVR_GRIP_TO_ORIGIN_CM"):
    match = re.search(r"^#define " + name + r"\s+[^\n]+", view, re.M)
    if match:
        definitions.append(match.group())
definitions.extend(re.findall(r"^#define GEVR_CHOP_[^\n]+", view, re.M))
for name in ("s_gevrClubButt", "s_gevrChopSwing", "s_gevrBeltMeleeTaken"):
    match = re.search(r"^static (?:const )?(?:f32|s32) " + name + r"\[[23]\][^;]*;", view, re.M)
    if match:
        definitions.append(match.group())

production = [function(matrix, signature) for signature in (
    "void matrix_4x4_rotate_vector(", "void mtx4RotateVecInPlace(")]
production.append(function(read("src/game/gun.c"), "void attempt_reload_item_in_hand("))
production.append(function(read("src/game/gunfire.c"), "void sub_GAME_7F0649D8(enum GUNHAND hand)\n{").replace(
    "sub_GAME_7F0649D8", "testTopUp", 1))
production.extend(function(view, signature) for signature in (
    "static void gevrWorldToLocal(",
    "static s32 gevrHandOnBody(", "static s32 gevrHipZone(",
    "static s32 gevrReloadGun(", "static s32 gevrReloadMagazineFed(",
    "s32 gevrManualReloadOn(", "static s32 gevrGexByHand(",
    "static void gevrGexHeldDropped(", "static void gevrGexMagOut(",
    "static void gevrGexMagIn(",
    "static f32 gevrBeltDist2(", "static s32 gevrGexAtBelt(",
    "static s32 gevrGexPistolPoint(s32 support, f32 out[3])\n{", "static void gevrReloadMagPoint(",
    "static s32 gevrGexPistolSupportAllowed(void)\n{", "static f32 gevrGexReloadDistance(",
    "void gevrGexReloadReset(",
    "s32 gevrGexClaimsOffHand(void)\n{", "void gevrGexDropMagazine(",
    "static void gevrHandReloadFire(", "void gevrHandReloadTick("))
for signature in ("static s32 gevrReloadNeedsAmmo(", "static s32 gevrReloadBeltReach("):
    if signature in view:
        production.insert(-2, function(view, signature))
production.extend(function(view, signature) for signature in (
    "s32 gevrReloadHoldsHand(s32 ctrl)\n{", "void gevrHandChopTick(", "s32 gevrHandChopSwinging("))
production.append(function(view,"s32 gevrReloadFitAvailable(void)\n{"))
fixture = (ROOT / "port/tests/hand_reload_native.c").read_text(encoding="utf-8")
fixture = fixture.replace("/* DEFINITIONS */", "\n".join(definitions))
fixture = fixture.replace("/* PRODUCTION */", "\n".join(production))
with tempfile.TemporaryDirectory(prefix="gevr-hand-reload-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
    source.write_text(fixture, encoding="utf-8")
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-D_LANGUAGE_C",
                    "-I"+str(ROOT), "-I"+str(ROOT/"include"), "-I"+str(ROOT/"src"), "-I"+str(ROOT/"port/include"),
                    str(source), str(ROOT/"port/src/gevr_gexweapon.c"),
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
