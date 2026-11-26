"""Oddjob's hats in both hands on a flat screen: each hand moves out, VR never does."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
fire = (ROOT / "src/game/gunfire.c").read_text(encoding="utf-8")
hat = (ROOT / "port/src/gevr_oddjobhat.c").read_text(encoding="utf-8")

# gunfire.c: flat only, both hands on the throwing knife, outwards per hand, eased
hook = fire.index("? gevrHatDualSpread() : 0.0f;")
block = fire[fire.rindex("static f32 spread[MAX_PLAYER_COUNT][2];", 0, hook):]
block = block[:block.index("gunofs.x +=")]
want = block[block.index("const f32 want"):block.index("? gevrHatDualSpread()")]
assert "!g_gevrStereo" in want, want
assert "item == ITEM_THROWKNIFE" in want, want
assert "get_item_in_hand_or_watch_menu(handnum == GUNRIGHT ? GUNLEFT : GUNRIGHT) == ITEM_THROWKNIFE" in want, want
assert "if (g_gevrStereo) *cur = 0.0f;" in block, block
assert "step = 0.7f * g_GlobalTimerDelta" in block, block
assert "gunofs.x += (handnum == GUNRIGHT ? 1.0f : -1.0f) * *cur;" in fire

# the spread: nothing unless the held knife draws as the hat; the tune file's "flat" line, default 10
body = hat[hat.index("float gevrHatDualSpread(void)"):]
body = body[:body.index("\n}\n")]
assert "if (!VrOddjobHat || !s_hat.ok) return 0.0f;" in body, body
assert "s_models[r].kind == HAT_HELD && s_models[r].header" in body, body
assert 'sscanf(line, "flat %f", &s_tune.spread)' in hat
assert re.search(r"#define HAT_FLAT_SPREAD 10\.0f", hat)
print("oddjob hat flat spread: ok")
