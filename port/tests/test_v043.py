"""Compile production ammo, input/action routing, and wrist rendering in native harnesses."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
gun = (root / "src/game/gunfire.c").read_text(encoding="utf-8")
state = (root / "src/game/gun.c").read_text(encoding="utf-8")
view = (root / "src/game/bondview2.c").read_text(encoding="utf-8")
level = (root / "src/game/lv.c").read_text(encoding="utf-8")
input_source = (root / "port/src/input.c").read_text(encoding="utf-8")
buttons = (root / "port/vr/vr_input.cpp").read_text(encoding="utf-8")
glass = (root / "src/game/glass2.c").read_text(encoding="utf-8")
sync = (root / "port/src/net/net_player_sync.c").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


poll_start = input_source.index("    if (idx == localSlot) {\n        const unsigned held")
movement_start = view.index("#ifdef GEVR\n    /* OR of B/Y")
action_start = level.index("#ifdef GEVR\n            /* Drain even")
fixtures = {
    "mp_presentation": {
        "POSE": function(sync, "static void netSyncCopyOrientation("),
        "OWNER": function(view, "static s32 gevrPlayerModelTickOwner("),
        "RESPAWN": function(view, "static s32 gevrOnlineRespawnReady("),
        "HEADING": next(line.strip() for line in view.splitlines() if "setsubroty(chr->model," in line),
    },
    "ammo_handedness": {"AMMO": function(gun, "Gfx *generate_ammo_total_microcode(")},
    "reload_input": {
        "BUTTONS": function(buttons, 'extern "C" bool get_button_state('),
        "BRIDGE": "\n".join(function(input_source, signature) for signature in (
            "int gevrVrReloadPressedMask(", "int gevrVrReloadHeldMask(", "int gevrVrTakeReloadMask(")),
        "RELOAD": function(state, "void attempt_reload_item_in_hand("),
        "POLL": function(input_source[poll_start:], "    if (idx == localSlot)"),
        "MOVEMENT": view[movement_start:view.index("    g_CurrentPlayer->field_D0 = 0;", movement_start)],
        "ACTION": level[action_start:level.index("            propsTickPlayer();", action_start)],
    },
    "watch_status": {
        "GAUGES": function(glass, "void hudMakeDamageSegments("),
        "GAUGE_DL": function(glass, "Gfx *buildGaugeBarDL("),
        "FIT": function(view, "static void gevrWatchStatusFit("),
        "RENDER": function(view, "static Gfx *gevrRenderWatchStatus(Gfx *gdl, const Mtxf *wrist)\n{"),
    },
}
compiler = shutil.which("g++") or "g++"
with tempfile.TemporaryDirectory(prefix="gevr-v043-") as temp:
    for name, parts in fixtures.items():
        fixture = (root / f"port/tests/{name}_native.cpp").read_text(encoding="utf-8")
        for marker, production in parts.items():
            fixture = fixture.replace(f"/* INSERT_{marker} */", production)
        source, exe = Path(temp) / f"{name}.cpp", Path(temp) / f"{name}.exe"
        source.write_text(fixture, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-O2", "-I" + str(root / "port/include"),
                        "-I" + str(root / "port/vr"), str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
