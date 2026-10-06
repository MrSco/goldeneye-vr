"""Co-op mission gadgets, gadget-on-object use, and tank globals."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
coop = (ROOT / "port/src/net/net_coop.c").read_text(encoding="utf-8")
action = (ROOT / "src/game/chraction.c").read_text(encoding="utf-8")
ai = (ROOT / "src/game/chrai.c").read_text(encoding="utf-8")
propobj = (ROOT / "src/game/propobj.c").read_text(encoding="utf-8")
gunfire = (ROOT / "src/game/gunfire.c").read_text(encoding="utf-8")
bondview = (ROOT / "src/game/bondview2.c").read_text(encoding="utf-8")
player = (ROOT / "src/game/player.c").read_text(encoding="utf-8")
gun = (ROOT / "src/game/gun.c").read_text(encoding="utf-8")

tick = block(action, "void chrlvAllChrTick(void)")
assert "gevrCoopNearestPlayer(" in tick
assert "set_cur_player(gevrTarget)" in tick
assert "set_cur_player(gevrPrev)" in tick
assert "gevrCoopGrantItem(wep->weaponnum)" in propobj
assert "gevrCoopReportGadgetUse(temp_v0_8->obj)" in gunfire
collision = block(bondview, "void bondviewCalcUpdatePlayerCollision(")
assert "gevrCoopReleaseTank()" in collision
move = block(bondview, "void MoveBond(")
frozen = block(bondview, "void bondviewFrozenMoveBond(")
assert "gevrCoopPushTank(&gevrTank)" in move and "gevrCoopPopTank(&gevrTank)" in move
assert frozen.count("gevrCoopPopTank(&gevrTank)") == 2
held = block(player, "void sub_GAME_7F09B398(")
assert "gevrCoopThrownMissionItem(wepid)" in held
thrown = block(gun, "void generate_player_thrown_object(")
assert thrown.count("gevrSoloRules()") == 4
assert "gevrCoopDeferRemoteMineSettle" in propobj
assert "gevrCoopReportMineSettled" in propobj
assert "case NET_COOP_EVENT_MINE:" in coop
assert "coopApplyMineSettled(slot, item, tag)" in coop

tank_start = bondview.index("typedef struct GevrTankGuard")
tank_end = bondview.index("\n#endif", bondview.index("void gevrCoopReleaseTank(void)", tank_start))
parts = {
    "NEAREST": "\n\n".join((
        block(coop, "static int coopPlayerTargetable("),
        block(coop, "s32 gevrCoopNearestPlayer("),
    )),
    "GRANT": "\n\n".join((
        block(coop, "int gevrCoopSharedGadget("),
        block(coop, "static void coopGrantLocal("),
        block(coop, "static void coopHeader(struct netbuf *b, u8 type)\n{"),
        block(coop, "static void coopSendEvent(u8 kind, s32 a, s32 b2, int nargs)\n{"),
        block(coop, "static void coopBroadcastGrant("),
        block(coop, "void gevrCoopGrantItem("),
        block(coop, "static void coopReceiveGrant("),
    )),
    "GADGET": "\n\n".join((
        block(coop, "static s32 coopTagOf("),
        block(coop, "void gevrCoopReportGadgetUse("),
        block(coop, "static void coopApplyGadgetUse("),
    )),
    "TANK": bondview[tank_start:tank_end],
    "GRANT_CASE": block(coop, "case NET_COOP_EVENT_GRANT:"),
    "GADGET_CASE": block(coop, "case NET_COOP_EVENT_GADGET:"),
    "AI_CASE": block(ai, "case AI_IFBondUsedGadgetOnObject:\n                {"),
    "MINE": "\n\n".join((
        block(coop, "int gevrCoopThrownMissionItem("),
        block(coop, "int gevrCoopDeferRemoteMineSettle("),
        block(coop, "void gevrCoopReportMineSettled("),
        block(coop, "static void coopApplyMineSettled("),
    )),
}
fixture = (ROOT / "port/tests/coop_items_native.c").read_text(encoding="utf-8")
for marker, code in parts.items():
    fixture = fixture.replace("/* INSERT_" + marker + " */", code)

with tempfile.TemporaryDirectory(prefix="gevr-coop-items-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test"
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
            "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / path) for path in
             (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    result = subprocess.run(args + [str(source), str(ROOT / "port/src/net/netbuf.c"), "-o", str(exe)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
print("Co-op gadget grant, gadget use, tank isolation, and Surface 2 mine checks passed")
