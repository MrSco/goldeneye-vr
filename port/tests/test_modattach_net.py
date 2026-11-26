"""mod.attach on the other players' headsets (#335, docs/mod-system.md "Network"):
port/src/gevr_modattach_net.c as it is, the owner's side driving a mirror of
what each other headset holds. A flip while both players are there reaches
the other; a player who joins gets the whole state (the host's ask, then that
player alone); a removal on a gun swap, a detach and a stop reach them; sends
go on change, at most GEVR_ATTNET_SENDS a second, packed; every malformed
message is refused whole. Then where it sits: NET_MOD_ATTACH in
net_protocol.h and net_core.c, the API's calls in gevr_modapi.c, and the
draws on the other player's copy in the game.
Run: python port/tests/test_modattach_net.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("FAIL:", what)


def function(source, signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth, i = 1, body + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(source[i], 0)
        i += 1
    return source[start:i]


def native():
    cc = os.environ.get("CC") or shutil.which("gcc") or "cc"
    sanitize = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    with tempfile.TemporaryDirectory(prefix="gevr-modattach-net-") as temp:
        exe = Path(temp) / ("modattach_net.exe" if os.name == "nt" else "modattach_net")
        subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Werror", *sanitize, "-I" + str(ROOT / "port/include"),
                        str(ROOT / "port/tests/modattach_net_native.c"), str(ROOT / "port/src/gevr_modattach_net.c"),
                        "-lm", "-o", str(exe)], check=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
        check(out.returncode == 0 and "modattach_net: ok" in out.stdout,
              "the bytes, the owner's sends and the mirror: " + out.stdout + out.stderr)


def placement():
    proto = (ROOT / "port/src/net/net_protocol.h").read_bytes().decode()
    check("NET_MOD_ATTACH = 6," in proto and "#define NET_MOD_ATTACH_BYTES 1024" in proto,
          "NET_MOD_ATTACH, a new kind of NET_MSG_MOD, and its limit")
    core = (ROOT / "port/src/net/net_core.c").read_bytes().decode()
    send = function(core, "int netSendModMessage(")
    check("if (kind == NET_MOD_ATTACH && (!data || len < 2 || len > NET_MOD_ATTACH_BYTES)) return 0;" in send
          and "kind >= NET_MOD_DROP && kind <= NET_MOD_GONE" in send,
          "sent with its length checked; a pickup's rules don't take it")
    recv = function(core, "static void netReceiveMod(")
    check(recv.index("} else if (kind == NET_MOD_ATTACH) {") < recv.index("if (netIsHost()) {")
          and "len < 2 || len > NET_MOD_ATTACH_BYTES || netbufReadLeft(buf) != len" in recv
          and recv.index("NET_MOD_HOST_RELAY_A_SECOND") < recv.index("gevrModNetAttach(mod,"),
          "read with its length checked before the host's relay and its rate, delivered after")
    api = (ROOT / "port/src/gevr_modapi.c").read_bytes().decode()
    add = function(api, "static int l_attachAdd(lua_State *L)")
    check(add.index("id = gevrModGameAttach(&a);") < add.index("attachTell(mod, id, &a);"),
          "told once attached, with the id the mod gets")
    tell = function(api, "static void attachTell(")
    check("!netIsActive() || a->on == GEVR_MODATTACH_GUARD" in tell, "online only; never a guard's")
    check("gevrAttachNetRemove(" in function(api, "static int l_attachRemove(")
          and "gevrAttachNetShow(" in function(api, "static int l_attachShow(")
          and "gevrAttachNetPlace(" in function(api, "static int l_attachPlace("),
          "remove, show and place told as they change")
    reset = function(api, "void gevrModApiReset(void)")
    check(reset.index("gevrModGameAttachReset();") < reset.index("gevrAttachNetReset(netIsActive() && gevrModScriptStage() >= 0);"),
          "a stop in a mission tells the others (a CLEAR)")
    got = function(api, "void gevrModNetAttach(")
    check("gevrAttachNetDecode(" in got and got.index("if (n < 0) {") < got.index("s_attachHeld++")
          and "(from == netGetHostSlot() || from < 0)" in got and "netIsDedicated()" in got,
          "checked whole before it's held; an ask only the host's; a dedicated server draws no one")
    apply = function(api, "static void attachApply(")
    check("stage != (gevrModScriptStage() & 0xFF) || !gevrModScriptOn(mod)" in apply,
          "applied only if it's this mission's and its mod runs here")
    frame = function(api, "static void attachNetFrame(void)")
    check("!netLobbySlotConnected(slot)" in frame and "gevrModGameAttachRemoteGone(slot, 0);" in frame,
          "a player who leaves takes theirs along")
    flush = function(api, "void gevrModApiNetFlush(void)")
    check(flush.index("attachJoined(slot);") < flush.index('gevrModScriptEvent("peer_ready"'), "the host's ask at peer_ready")
    game = (ROOT / "src/game/gevr_modattach.c").read_bytes().decode()
    gun = function(game, "Gfx *gevrModAttachDrawPlayerGun(")
    check("a->id < 0 && a->owner == slot" in gun and "node = ((Mtxf *) gun->render_pos)[a->node];" in gun
          and "attachDraw(gdl, templ, a, &node, NULL, TRUE)" in gun,
          "another player's on its gun: the held model's matrix as drawn, through the one attachment draw")
    body = function(game, "Gfx *gevrModAttachDrawPlayer(ChrRecord *chr")
    check("gevrAvatarAttachFrame(slot, a->on, a->where, a->body, unit, &frame)" in body and "fadealpha < 0xFF" in body,
          "another player's on a hand or the body: the frames its tracked pose gives")
    local = function(game, "Gfx *gevrModAttachDrawGun(")
    check("a->id > 0" in local and "a->id <= 0" in function(game, "Gfx *gevrModAttachDrawBody(Gfx *gdl)"),
          "this player's own draws leave another player's alone")
    one = function(game, "static Gfx *attachDraw(")
    check("if (world && !(templ->flags & 1)) return gdl;" in one and "D_80032310[0] / 65536.0f" in one,
          "a pack mesh in a body's passes: once, at the level's scale")
    add = function(game, "int gevrModGameAttach(const GevrModAttach *a)")
    check(add.index("if (s_attach[i].id == 0)") < add.index("if (s_attach[i].id < 0)"),
          "this player's own find a free place first, then set another player's aside")
    remote = function(game, "static AttachFile *attachLoadRemote(")
    check("unused <= ATT_REMOTE_FILES_FREE" in remote, "another player's never takes the last model files")
    held = (ROOT / "src/game/gevr_heldgun.c").read_bytes().decode()
    draw = function(held, "Gfx *gevrHeldGunDraw(")
    check(draw.index("subdraw(&rd, &inst->model);") < draw.index("gevrModAttachDrawPlayerGun(rd.gdl, &rd, chr, hand, &inst->model)")
          < draw.index("bondviewTransformManyPosToViewMatrix(inst->model.render_pos"),
          "drawn after the held gun, while its matrices are still floats")
    chr = (ROOT / "src/game/chr.c").read_bytes().decode()
    render = function(chr, "Gfx *chrRenderProp(PropRecord *prop, Gfx *gdl, s32 withalpha)")
    check(render.index("gevrModAttachDrawGuard(chr, &mrData, chrfadealpha, mrData.gdl);")
          < render.index("gevrModAttachDrawPlayer(chr, &mrData, chrfadealpha, mrData.gdl);")
          < render.index("bondviewTransformManyPosToViewMatrix(chr->model->render_pos, chr->model->obj->numMatrices);"),
          "a hand's and the body's after the body, in its passes")
    avatar = (ROOT / "port/src/gevr_avatar.c").read_bytes().decode()
    frame = function(avatar, "int gevrAvatarAttachFrame(")
    check("netGetAvatarPose(slot,&pose,weight)" in frame and "gevrBodyFrameBuild(&f,&torso,hp,fwd,head[1],head[0]);" in frame
          and "weight[where+1]<=0" in frame and "weight[0]<=0" in frame,
          "the tracked pose's grip or torso, none untracked")
    slots = (ROOT / "src/game/gevr_bodyslots.c").read_bytes().decode()
    at = function(slots, "s32 gevrBodySlotAt(s32 slot, f32 out[3])")
    check("gevrBodySlotOffsets(slot, VrBodySlotFit[slot], VrPlayerHeight, off);" in at
          and "gevrBodySlotSide(slot, VrLeftHandedMode)" in at,
          "a body anchor's centre from this player's own fit, as gevrBodySlotCentre lays it")


def main():
    native()
    placement()
    if FAILS:
        print("%d failure(s)" % len(FAILS))
        sys.exit(1)
    print("PASS: mod.attach on the other players: the message's bytes and refusals, sends on change at the rate, "
          "a flip, a join's whole state, a removal and a stop reaching the others; NET_MOD_ATTACH's place in the "
          "network and the API; the draws on the other player's gun, hands and body")


if __name__ == "__main__":
    main()
