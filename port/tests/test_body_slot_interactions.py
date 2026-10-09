"""Run production grip arbitration, stick capture and label display-list checks."""
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


view = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
body = (ROOT / "src/game/gevr_bodyslots.c").read_text(encoding="utf-8")
gun = (ROOT / "src/game/gunfire.c").read_text(encoding="utf-8")
inputs = (ROOT / "port/src/input.c").read_text(encoding="utf-8")

# Keep the actual motion-throw ownership/arming/release code. Projectile flight
# and velocity sampling are outside this regression; count accepted releases.
motion = function(view, "void gevrMotionThrowTick(")
motion = motion[:motion.index("    f32 at[3], right[3], up[3], back[3];")]
motion += "if (release && s_gevrThrowWindup[hand]) { throws++; s_gevrThrowWindup[hand] = 0; }\n}"
production = "\n".join([
    function(body, "int gevrBodySlotGrip("),
    function(body, "void gevrBodySlotGripLetGo("),
    function(body, "int gevrBodySlotHoldsGrip("),
    function(body, "int gevrBodySlotStick("),
    function(view, "void gevrGripGestureInput("),
    function(view, "s32 gevrGripGestureTaken(s32 ctrl)\n{"),
    function(view, "void gevrGripGestureTick("),
    motion,
])
poll = function(inputs[inputs.index("static bool gripWas[2];"):], "for (int c = 0; c < 2; c++)")
stick_poll = function(inputs[inputs.index("// Body slots: a hand in a slot steps"):], "for (int c = 0; c < 2; c++)")
production += "\nstatic void poll(void) { " + poll + " }"
production += "\nstatic void sticks(void) { " + stick_poll + " }"
fixtures = {
    "body_slot_interactions_native.c": production,
    "body_slot_labels_native.c": "\n".join(function(gun, signature) for signature in (
        "static Gfx *gevrDrawViewTagMode(", "Gfx *gevrDrawViewTag(", "Gfx *gevrDrawViewTagOverlay(")),
}
with tempfile.TemporaryDirectory(prefix="gevr-slot-interactions-") as temp:
    for name, production in fixtures.items():
        source, exe = Path(temp) / name, Path(temp) / (name + ".exe")
        fixture = (ROOT / "port/tests" / name).read_text(encoding="utf-8")
        source.write_text(fixture.replace("/* PRODUCTION */", production), encoding="utf-8")
        subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Werror",
                        "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-Wno-unused-parameter",
                        "-I" + str(ROOT / "port/include"), str(source),
                        str(ROOT / "port/src/gevr_bodyslot.c"), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
