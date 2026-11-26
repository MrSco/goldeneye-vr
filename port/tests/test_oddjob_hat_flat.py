"""Oddjob's hats in both hands on a flat screen: each hand moves out, VR never does."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
fire = (ROOT / "src/game/gunfire.c").read_text(encoding="utf-8")
hat = (ROOT / "port/src/gevr_oddjobhat.c").read_text(encoding="utf-8")

# gunfire.c: flat only, both hands on the throwing knife, outwards per hand
hook = fire.index("gevrHatDualSpread()")
gate = fire[fire.rindex("if (", 0, hook):hook + 40]
assert "!g_gevrStereo" in gate, gate
assert "item == ITEM_THROWKNIFE" in gate, gate
assert "get_item_in_hand_or_watch_menu(handnum == GUNRIGHT ? GUNLEFT : GUNRIGHT) == ITEM_THROWKNIFE" in gate, gate
assert "(handnum == GUNRIGHT ? 1.0f : -1.0f) * gevrHatDualSpread()" in gate, gate

# the spread: nothing while the hat is off or missing, the tune file's "flat" line, default 10
body = hat[hat.index("float gevrHatDualSpread(void)"):]
body = body[:body.index("\n}\n")]
assert "if (!VrOddjobHat || !gevrHatAvailable()) return 0.0f;" in body, body
assert 'sscanf(line, "flat %f", &s_tune.spread)' in hat
assert re.search(r"#define HAT_FLAT_SPREAD 10\.0f", hat)
print("oddjob hat flat spread: ok")
