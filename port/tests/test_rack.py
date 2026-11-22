"""Compile hand reload's rack (port/include/gevr_rack.h) and run its checks, then check the
game side uses it as docs/hand-reload-rack.md says. The whole reload with the rack in it is
port/tests/test_hand_reload.py."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="gevr-rack-") as temp:
    exe = Path(temp) / "rack.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Werror",
                    "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/rack_native.c"), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")

# The guns: the pistols, the submachine guns and the KF7, and no others.
items = set(re.findall(r"case (ITEM_\w+):", function(view, "static s32 gevrRackItem(")))
assert items == {"ITEM_WPPK", "ITEM_WPPKSIL", "ITEM_SILVERWPPK", "ITEM_GOLDWPPK", "ITEM_TT33",
                 "ITEM_SKORPION", "ITEM_UZI", "ITEM_MP5K", "ITEM_MP5KSIL", "ITEM_SPECTRE", "ITEM_AK47",
                 "ITEM_M16", "ITEM_FNP90", "ITEM_SNIPERRIFLE", "ITEM_AUTOSHOT", "ITEM_SHOTGUN"}, items
# ... and only the pump shotgun for every shot.
assert re.findall(r"ITEM_\w+", function(view, "static s32 gevrRackEveryShot(")) == ["ITEM_SHOTGUN"]
assert "gevrRackEveryShot(i)" in function(view, "void gevrHandReloadTick(")

# Only a magazine pushed in by the off hand waits for the rack: the belt's touch and the gun
# hand's squeeze there (dual wielding's reloads) go through gevrGexMagIn alone.
tick = function(view, "void gevrHandReloadTick(")
assert tick.count("gevrRackHold(") == 1
pushed = tick.index('gevrGexMagIn(gunHand, "pushed in", s_gevrGexHeldRounds);')
assert pushed < tick.index("gevrRackHold(") < tick.index("case GEVR_GEXMAG_OUT:")
assert "gevrRackHold(" not in function(view, "static void gevrGexMagIn(")
assert "gevrRackChamber(&s_gevrRack[hand]" in function(view, "static void gevrGexMagIn(")
# The waiting rounds are counted as the player's, and leave with the magazine.
assert "s_gevrRack[hand].pending" in function(view, "s32 gevrReloadReservedRounds(")
assert "s_gevrRack[hand].pending > 0" in function(view, "s32 gevrReloadCarriesRounds(")
assert "gevrRackGiveBack(" in function(view, "static void gevrGexMagOut(")
assert "gevrRackGiveBack(" in function(view, "s32 gevrReloadStow(")
# The gun is drawn back with the pull.
assert "gevrRackNudge(&s_gevrRack[handnum])" in function(view, "s32 gevrStereoGunMatrix(s32 handnum, Mtxf *out)\n{")

# The pump shotgun's shells wait as a magazine's rounds do; the rack is one routine for both.
shells = function(view, "static void gevrGexRoundTick(")
assert "gevrRackAdd(&s_gevrRack[gunHand], s_gevrGexHeldRounds)" in shells and "gevrRackRun(gunHand, grip, fresh)" in shells
assert "gevrRackRun(gunHand, grip, !s_gripWas[gunHand]);" in tick
# Its place is Gun fit's, saved per model set and gun, and undone with the rest.
ini = (ROOT / "port/vr/vr_settings.cpp").read_bytes().decode("utf-8")
assert 'iniPrintf(f, "%sRack%s=%.2f %.2f %.2f\\n", kFitLookPrefix[m], w.name, t[0], t[1], t[2]);' in ini
assert 'strncmp(fit, "Rack", 4) == 0' in ini and "VrRackFit[look][w.item][i] = t[i];" in ini
inp = (ROOT / "port/src/input.c").read_text(encoding="utf-8")
assert "memcpy(VrRackFit, s_gunFitSaved.rack, sizeof(VrRackFit));" in inp
assert "memcpy(s_gunFitSaved.rack, VrRackFit, sizeof(VrRackFit));" in inp
assert "if (gevrReloadFitting && lg && !rackGrip) gevrRackFitSet();" in inp

# The label has a line and a colour for each of its states.
gun = (ROOT / "src/game/gun.c").read_bytes().decode("utf-8", errors="replace")
label = function(gun, "static Gfx *gevrReloadLabelOn(Gfx *gdl, s32 hand, const Mtxf *gun)\n{")
header = (ROOT / "port/include/gevr_hand_reload.h").read_text(encoding="utf-8")
count = len(set(re.findall(r"GEVR_RELOAD_LABEL_\w+", re.search(
    r"enum \{\s*GEVR_RELOAD_LABEL_NONE.*?GEVR_RELOAD_LABEL_COUNT", header, re.S).group()))) - 1
text = re.search(r"text\[GEVR_RELOAD_LABEL_COUNT\] = \{(.*?)\};", label, re.S).group(1)
panel = re.search(r"panel\[GEVR_RELOAD_LABEL_COUNT\] = \{(.*?)\};", label, re.S).group(1)
assert len(re.findall(r'NULL|"[^"]*"', text)) == count == len(re.findall(r"0x[0-9A-Fa-f]+|\b0\b", panel)), (count, text, panel)
assert '"RACK THE SLIDE", "PULL THE BOLT", "PUMP IT"' in text

print("rack: game side ok")
