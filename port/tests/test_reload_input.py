"""Compile actual VR button mapping and B/Y activation, including manual eject."""
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


inputs = (ROOT / "port/src/input.c").read_text()
view = (ROOT / "src/game/bondview2.c").read_text()
level = (ROOT / "src/game/lv.c").read_text()
vr = (ROOT / "port/vr/vr_input.cpp").read_text()
gun = (ROOT / "src/game/gun.c").read_text()
fixture = (ROOT / "port/tests/reload_input_native.cpp").read_text()
fixture = fixture.replace("/* INSERT_BUTTONS */", function(vr, 'extern "C" bool get_button_state('))
fixture = fixture.replace("/* INSERT_BRIDGE */", "\n".join(function(inputs, s) for s in (
    "int gevrVrReloadPressedMask(", "int gevrVrReloadHeldMask(", "int gevrVrTakeReloadMask(")))
fixture = fixture.replace("/* INSERT_RELOAD */", function(gun, "void attempt_reload_item_in_hand("))
fixture = fixture.replace("/* INSERT_POLL */", function(inputs[inputs.rindex("if (idx == localSlot) {"):], "if (idx == localSlot) {"))
marker = view.index("/* OR of B/Y loses a second press")
fixture = fixture.replace("/* INSERT_MOVEMENT */", function(view[marker:], "if (!netIsActive() || get_cur_playernum() == netGetLocalSlot()) {"))
start = level.rfind("#ifdef GEVR", 0, level.index("/* Drain even when a door/tank/menu consumes"))
end = level.index("propsTickPlayer();", start)
fixture = fixture.replace("/* INSERT_ACTION */", level[start:end])
with tempfile.TemporaryDirectory(prefix="gevr-reload-buttons-") as temp:
    source, exe = Path(temp) / "test.cpp", Path(temp) / "test.exe"
    source.write_text(fixture)
    subprocess.run([shutil.which("g++") or "g++", "-std=c++20", "-O2", "-Wall", "-Werror",
                    "-I" + str(ROOT / "port/include"), str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
