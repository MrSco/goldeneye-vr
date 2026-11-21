"""Compile teleport locomotion's arithmetic (port/src/gevr_teleport.c) and run its checks,
then check the game side keeps the rules that make it fair (docs/teleport.md)."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="gevr-teleport-") as temp:
    exe = Path(temp) / "teleport.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Werror",
                    "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/teleport_native.c"), str(ROOT / "port/src/gevr_teleport.c"),
                    "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


def body(text, signature):
    """The function's text, from its signature to its closing brace at column 0."""
    start = text.index(signature)
    return text[start:text.index("\n}\n", start)]


game = (ROOT / "src/game/gevr_teleport.c").read_text(encoding="utf-8")
view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
inp = (ROOT / "port/src/input.c").read_text(encoding="utf-8")
pure = (ROOT / "port/src/gevr_teleport.c").read_text(encoding="utf-8")

# No angle functions from the C library: the game's own replace them (gevr_pull.h).
for text in (pure, game):
    assert not re.search(r"\b(atan2f?|acosf?|asinf?)\(", text)

# The jump is the walk's own test, step by step, and moves nothing while it asks.
walk = body(view, "f32 gevrTeleportWalk(")
assert "bondviewTryMoveToStan(&next, &stan)" in walk
assert "GEVR_TELEPORT_STEP" in walk and "#define GEVR_TELEPORT_STEP 20.0f" in view
assert "pl->field_488.collision_position = from;" in walk
assert "pl->field_488.current_tile_ptr = fromtile;" in walk
assert "pl->autocrouchpos = autocrouch;" in walk
assert "g_WorldTankProp = tank;" in walk
assert walk.index("s_gevrMoveProbe = TRUE;") < walk.index("bondviewTryMoveToStan") < walk.index("s_gevrMoveProbe = FALSE;")
# ... against everything the walk is stopped by (the probe adds no collision types of its own)
move = body(view, "s32 bondviewTryMoveToStan(")
assert "CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PLAYERS | CDTYPE_CHRS | CDTYPE_PATHBLOCKER" in move

# Every jump (the arc's and a hop) is walked and paid for at the moment it happens.
jump = body(game, "static void tpJump(void)")
assert jump.index("tpWalkTo(") < jump.index("gevrTeleportSpend(&s_tp, reached)") < jump.index("gevrTeleportPlace(")
assert game.count("gevrTeleportPlace(&") == 1
assert "gevrTeleportWalk(dx, dz, dist, end, endtile)" in body(game, "static s32 tpWalkTo(")
tick = body(game, "void gevrTeleportTick(void)")
assert "gevrTeleportRest(&s_tp, dt, gevrTeleportSpeedFactor())" in tick
assert "gevrTeleportBlinkStep(&s_tp, dt) && s_pending" in tick
# Only this headset's own player, on foot and in play.
takes = body(game, "s32 gevrTeleportTakesStick(void)")
for need in ("VrTeleport", "g_gevrStereo", "bonddead", "g_PlayerIsInTank", "gevrDamBoatAboard()",
             "gevrSpectating()", "gevrCoopLocalDowned()"):
    assert need in takes, need
for need in ("lvlGetControlsLockedFlag()", "watch_animation_state", "mpmenuon", "gevrScopeZoomStick()", "gevrWeaponPanelOpen"):
    assert need in tick, need
assert re.search(r"if \(!netIsActive\(\) \|\| get_cur_playernum\(\) == netGetLocalSlot\(\)\)\s*\{\s*"
                 r"extern void gevrTeleportTick\(void\);", view)

# With teleport on the stick walks nobody: no C buttons from it.
block = inp[inp.index("const bool teleport = g_gevrStereo && !zoomStick && gevrTeleportTakesStick() != 0;"):]
block = block[:block.index("\n            }\n")]
assert "if (!teleport) {" in block
for button in ("L_CBUTTONS", "R_CBUTTONS", "D_CBUTTONS", "U_CBUTTONS"):
    assert button in block

# The setting: off by default, saved, on the Comfort page of a headset only.
assert "int VrTeleport = 0;" in (ROOT / "port/vr/vr_settings_defaults.c").read_text(encoding="utf-8")
ini = (ROOT / "port/vr/vr_settings.cpp").read_bytes().decode("utf-8")
assert 'iniPrintf(f, "Teleport=%d\\n", VrTeleport ? 1 : 0);' in ini
assert 'strcmp(key, "Teleport") == 0) VrTeleport = ival != 0;' in ini
table = (ROOT / "port/src/gevr_settings.c").read_text(encoding="utf-8")
assert 'CHOICE_VR(GEVR_SEC_COMFORT, "Movement", VrTeleport, s_two, s_moveNames,' in table

# The blink reaches the eyes with no vignette set.
gl = (ROOT / "port/fast3d/gfx_opengl.cpp").read_bytes().decode("utf-8")
assert "(strength <= 0.001f && blink <= 0.001f)" in gl and "uBlink" in gl
pc = (ROOT / "port/fast3d/gfx_pc.cpp").read_bytes().decode("utf-8", errors="replace")
assert pc.count("gfx_opengl_draw_vignette(s_gevrLastVignette, gevrTeleportBlink());") == 2

print("teleport: game side ok")
