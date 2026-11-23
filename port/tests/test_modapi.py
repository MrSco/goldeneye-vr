"""The mod API's weapons and items areas (docs/mod-system.md, plan step 4):
events from the gun code, mod.weapon's calls and limits, mod.items, the
stats put back; pickup_spawning's swaps and refusals, item_pickup,
mod.inventory and mod.ammo. Builds modapi_native.c around the production
port/src/gevr_modapi.c, gevr_modscript.c and port/external/lua over a fake
game, and checks where the hooks sit in the production gunfire.c,
bondview2.c, prop.c and propobj.c, and what gevr_modgame.c leaves alone.
Run: python port/tests/test_modapi.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_modscript import LUA  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("FAIL:", what)


def build(temp):
    cc = shutil.which("gcc") or "cc"
    sanitize = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    flags = ["-std=gnu11", "-O1", "-funsigned-char", "-fwrapv", "-fno-strict-aliasing", "-D_LANGUAGE_C=1", "-DGEVR=1"]
    objects = []
    for name in LUA:   # as the build's gevr_lua: without the game's include/
        obj = temp / (name + ".o")
        subprocess.run([cc, *flags, *sanitize, "-c", str(ROOT / "port/external/lua" / (name + ".c")), "-o", str(obj)],
                       check=True)
        objects.append(str(obj))
    exe = temp / ("modapi_test.exe" if os.name == "nt" else "modapi_test")
    subprocess.run([cc, *flags, *sanitize, "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/modapi_native.c"), str(ROOT / "port/src/gevr_modapi.c"),
                    str(ROOT / "port/src/gevr_modscript.c"), *objects, "-lm", "-o", str(exe)], check=True)
    return exe


def run(exe, temp, name, main_lua, *commands):
    d = temp / name
    (d / "mods/tactical/scripts").mkdir(parents=True, exist_ok=True)
    (d / "save").mkdir(exist_ok=True)
    (d / "mods/tactical/scripts/main.lua").write_bytes(main_lua.encode())
    out = subprocess.run([str(exe), *map(str, commands)], cwd=d, capture_output=True, text=True, timeout=300)
    assert out.returncode == 0, out.stderr
    return out.stdout


def logs(out):
    return [l.split(" ", 2)[2][len("mod tactical: "):] for l in out.splitlines()
            if l.startswith("LOG ") and l.split(" ", 2)[2].startswith("mod tactical: ")]


def answers(out, *prefixes):
    return [l for l in out.splitlines() if l.startswith(prefixes)]


def placement():
    """Each hook sits before what it guards, in the production gun code."""
    gun = (ROOT / "src/game/gunfire.c").read_bytes().decode()
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    shot = gun.index("if ((handptr->weapon_firing_status != 0) && gevrModApiFireAttempt(hand, var_s1))")
    spend = gun.index("handptr->weapon_ammo_in_magazine -= 1;\n                handptr->volley += 1;")
    check(shot < spend, "a shot is asked before its round is spent")
    decline = gun[shot:spend]
    check("GUN_ANIM_STATE_DRY_FIRE" in decline and "EMPTY_GUN_FIRE_SFX" in decline and "weapon_firing_status = 0" in decline,
          "a declined shot clicks dry, firing nothing")
    for state in ("GRENADE_RECOVER", "THROWKNIFE_RECOVER", "MINE_RECOVER"):
        commit = gun.index("handptr->weapon_action_state = GUN_ANIM_STATE_" + state + ";\n                handptr->field_890")
        ask = gun.rindex("else if (gevrModApiFireAttempt(hand, var_s1))", 0, commit)
        between = gun[ask:commit]
        check("weapon_ammo_in_magazine -= 1" in between and between.index("IDLE") < between.index("-= 1"),
              state + ": asked before the throw spends, declined back to idle")
    check(len(re.findall(r"gevrModApiFired\(hand, var_s1\)", gun)) == 4, "weapon_fire after each spend in gunfire.c")
    lower = gun.index("handptr->weapon_action_state = GUN_ANIM_STATE_RELOAD_LOWER;\n#ifdef GEVR\n            gevrModApiReloadStart")
    check(lower > 0, "reload_start as the native reload lowers")
    raise_ = gun.index("sub_GAME_7F0649D8(hand);\n            g_CurrentPlayer->trigger_released = 0;\n#ifdef GEVR\n            gevrModApiReloadEnd")
    check(raise_ > 0, "reload_end after the native reload's ammo move")
    check("g_gevrSwitchInstant[hand] = 0;\n            gevrModApiDrawn(hand, var_s1);" in gun, "weapon_drawn at a switch's end")
    throw = view.index("if (gevrModApiFireAttempt(hand, item))\n                {")
    commit = view.index("g_gevrMotionThrowActive[hand] = 1;\n                s_gevrMotionRecover[hand] = TRUE;\n                g_gevrMotionThrowVel[hand] = final_vel;")
    check(throw < commit and "return;" in view[throw:commit], "a motion throw is asked before it leaves the hand")
    drop = view.index("&& gevrModApiFireAttempt(hand, item))")
    check(drop < view.index("The grenade was cooked and gently dropped"), "a cooked grenade's drop is asked first")
    for name, text in (("gunfire.c", gun), ("bondview2.c", view)):
        # a call before the prototype is an implicit int declaration, an error
        # beside the header's void one in clang (every build but the tests')
        check(text.index('#include "gevr_modapi.h"') < text.index("gevrModApi"),
              name + " includes gevr_modapi.h before its first hook")
    check("gevrModApiReloadStart(hand, getCurrentPlayerWeaponId(hand));   /* reload_start" in view
          and "gevrModApiReloadEnd(hand, getCurrentPlayerWeaponId(hand));   /* reload_end: a magazine seated" in view,
          "a hand reload's magazine out and in")


def items_placement():
    """pickup_spawning as each record is about to be placed, after the scan; item_pickup before a freed pickup goes"""
    prop = (ROOT / "src/game/prop.c").read_bytes().decode().replace("\r\n", "\n")
    obj = (ROOT / "src/game/propobj.c").read_bytes().decode().replace("\r\n", "\n")
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    first = prop.index("phead = g_CurrentSetup.propDefs;\n            pdefIndex = 0;")
    scan = prop.index("gevrModPickupScan();")
    loop = prop.index("while ((i1 = phead->type) != PROPDEF_END)")
    second = prop.index("pdefIndex = 0;", loop)
    check(scan < first < loop and "case PROPDEF_" not in prop[scan:loop], "the scan runs before the stage's records are placed")
    check(prop.index("case PROPDEF_LINK:") > second and prop.index("case PROPDEF_SAFE_ITEM:") > second,
          "dual pairs and safes are resolved after the scan (it reads their indices, not pointers)")
    weapon = prop.index("gevrModPickupSpawn((PropDefHeaderRecord *)weapon, cmdindex)")
    check(prop.index("case ITEM_TOKEN:") < weapon
          < prop.index("weaponLoadProjectileModels(weapon->weaponnum);\n            domakedefaultobj(arg0")
          and "giveweapon = 0;" in prop[weapon:weapon + 200],
          "a weapon is asked after multiplayer's weapon set, before its models load and it's placed")
    case = prop[prop.index("case PROPDEF_ARMOUR:"):prop.index("case PROPDEF_TINTED_GLASS:")]
    check(case.index("pdef_ba->amount = pdef_ba->initialamount;") < case.index("gevrModPickupSpawn(phead, pdefIndex)) break;")
          < case.index("domakedefaultobj"), "armour is asked with its amount read, before it's placed")
    case = prop[prop.index("case PROPDEF_MAGAZINE:"):prop.index("case PROPDEF_AMMO:")]
    check(case.index("phead->type == PROPDEF_MAGAZINE && gevrModPickupSpawn(phead, pdefIndex)) break;")
          < case.index("domakedefaultobj"), "an ammo box is asked before it's placed, nothing else in its case")
    case = prop[prop.index("case PROPDEF_AMMO:"):prop.index("case PROPDEF_TANK:")]
    check(case.index("ammoqty = mpweapon->ammoamount;") < case.index("gevrModPickupSpawn(phead, pdefIndex)) break;")
          < case.index("modelLoad("), "a crate is asked after multiplayer's, before its rounds' models load")
    fn = obj[obj.index("// Perfect Dark propobj.c: s32 propPickupByPlayer"):]
    fn = fn[:fn.index("\n}\n")]
    taken = fn.index("gevrModPickupTaken(")
    check(fn.index("switch (obj->type)") < taken < fn.index("objFree(obj") and "if (op != TICKOP_NONE)" in fn[taken - 120:taken],
          "item_pickup after the game took it, before a freed pickup goes")
    scanfn = game[game.index("void gevrModPickupScan(void)"):game.index("int gevrModPickupSpawn(")]
    for kind in ("PROPDEF_TAG", "PROPDEF_RENAME", "PROPDEF_LINK", "PROPDEF_SAFE_ITEM"):
        check("case " + kind + ":" in scanfn, "a swap leaves alone what " + kind + " points at")
    spawnfn = game[game.index("int gevrModPickupSpawn("):]
    asked = spawnfn[:spawnfn.index("gevrModApiPickupSpawning(&p)")]
    check("kept(index)" in asked and "PROPFLAG_INSIDEANOTHEROBJ" in asked and "PROPFLAG_ASSIGNEDTOCHR" in asked
          and "gevrModGameSwappable(" in asked, "never asked: kept records, embedded ones, guards' guns, other items")


def items(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)

    # ---- pickup_spawning's swaps, as a Tactical-style mod uses them ----
    lua = """
mod.on("pickup_spawning", function(p)
  print("ask", p.kind, p.index)
  if p.kind == "weapon" then
    if p.item == mod.items.goldengun then return false end
    if p.index % 2 == 0 then return { item = "ak47" } end
  elseif p.kind == "ammo" then
    p.ammo = mod.ammo.rifle
    return p
  elseif p.kind == "crate" then
    print("crate holds", p.crate[1], p.crate[3])
    return { crate = { rifle = 40.0, [mod.ammo.shotgun] = 5 } }
  elseif p.kind == "armour" then
    return { amount = 0.5 }
  end
end)
"""
    out = run(exe, temp, "swaps", lua, "start", "weapon", 1, 4, "prepare", 9, "weapon", 4, 4, "weapon", 5, 4,
              "weapon", 6, 19, "box", 7, 1, "crate", 8, "1:10,3:20", "armour", 9, 1.0)
    check(answers(out, "SPAWN") == ["SPAWN 1 weapon 4", "SPAWN 4 weapon 8", "SPAWN 5 weapon 4", "SPAWN 6 none",
                                    "SPAWN 7 ammo 3", "SPAWN 8 crate 3:40 4:5", "SPAWN 9 armour 0.50"],
          "swaps: a weapon, none, kept, a box's type, a crate's rounds, armour; nothing outside a mission: "
          + str(answers(out, "SPAWN")))
    check(logs(out) == ["ask weapon 4", "ask weapon 5", "ask weapon 6", "ask ammo 7", "ask crate 8", "crate holds 10 20",
                        "ask armour 9"], "asked: " + str(logs(out)))

    # ---- handlers in turn: each sees the last one's answer; none ends it ----
    lua = """
mod.on("pickup_spawning", function(p) return { item = mod.items.m16 } end)
mod.on("pickup_spawning", function(p)
  print("second sees", p.item)
  if p.index == 2 then return false end
end)
mod.on("pickup_spawning", function(p) print("third", p.index) end)
"""
    out = run(exe, temp, "chain", lua, "start", "prepare", 9, "weapon", 1, 4, "weapon", 2, 4)
    check(answers(out, "SPAWN") == ["SPAWN 1 weapon 13", "SPAWN 2 none"]
          and logs(out) == ["second sees 13", "third 1", "second sees 13"], "in turn: " + out[-500:])

    # ---- an answer the game can't take stops the mod; the pickup is the level's ----
    bad = [("weapon", 4, "true", "answer nil (keep it), false (none) or a weapon table"),
           ("weapon", 4, "5", "a weapon table"),
           ("weapon", 4, "{ item = 'keycard' }", "a gun or thrown weapon"),
           ("weapon", 4, "{ item = 'watchlaser' }", "a gun or thrown weapon"),
           ("weapon", 4, "{ item = 999 }", "a gun or thrown weapon"),
           ("weapon", 4, "{ kind = 'ammo', item = 8 }", "a weapon pickup stays a weapon"),
           ("box", 1, "{ ammo = 14 }", "a type from mod.ammo"),
           ("box", 1, "{ ammo = 'laser' }", "a type from mod.ammo"),
           ("crate", "3:20", "{ crate = 3 }", "a table of type = rounds"),
           ("crate", "3:20", "{ crate = { rifle = 401 } }", "0 to 400 of rifle"),
           ("crate", "3:20", "{ crate = { rifle = 1.5 } }", "0 to 400 of rifle"),
           ("crate", "3:20", "{ crate = { bfg = 1 } }", "types from mod.ammo"),
           ("crate", "3:20", "{ crate = { rifle = 1, [3] = 2 } }", "rifle is in the crate twice"),
           ("crate", "3:20", "{ crate = { rifle = 0, [3] = 2 } }", "rifle is in the crate twice"),
           ("crate", "3:20", "{ crate = { rifle = '40' } }", "0 to 400 of rifle"),
           ("armour", 1.0, "{ amount = 0 }", "more than 0, up to 1"),
           ("armour", 1.0, "{ amount = 1.5 }", "more than 0, up to 1"),
           ("armour", 1.0, "{ amount = 'full' }", "more than 0, up to 1")]
    for n, (kind, value, answer, why) in enumerate(bad):
        lua = 'mod.on("pickup_spawning", function(p) print("asked", p.index) return %s end)' % answer
        out = run(exe, temp, "bad%d" % n, lua, "start", "prepare", 9, kind, 1, value, "weapon", 2, 4)
        errors = [l for l in out.splitlines() if l.startswith("LOG ") and "mods: tactical: pickup_spawning: " in l]
        first = answers(out, "SPAWN")[:1]
        kept = {"weapon": "SPAWN 1 weapon 4", "box": "SPAWN 1 ammo 1", "crate": "SPAWN 1 crate 3:20",
                "armour": "SPAWN 1 armour 1.00"}[kind]
        check(len(errors) == 1 and why in errors[0] and first == [kept] and answers(out, "SPAWN")[1:] == ["SPAWN 2 weapon 4"]
              and logs(out) == ["asked 1"],
              "%s answered %s: refused (%s), the mod stopped, the pickups the level's: %s" % (kind, answer, why, out[-700:]))

    # ---- item_pickup: what the local player took ----
    lua = 'mod.on("item_pickup", function(kind, id, rounds) print("took", kind, id, rounds) end)'
    out = run(exe, temp, "took", lua, "start", "prepare", 9, "took", "weapon", 8, "begin", 9, "took", "weapon", 8,
              "took", "ammo", 3, 10, "took", "crate", "3:20,4:5", "took", "armour", 0.5, "took", "key", "took", "object",
              "local", 0, "took", "weapon", 4)
    check(logs(out) == ["took weapon 8 nil", "took ammo 3 10", "took ammo 3 20", "took ammo 4 5", "took armour 0.5 nil",
                        "took key nil nil", "took object nil nil"],
          "item_pickup in play, a crate as its types, never a copy's or a bot's: " + str(logs(out)))

    # ---- mod.inventory and mod.ammo ----
    lua = r"""
mod.on("mission_start", function()
  local inv, ammo = mod.inventory, mod.ammo
  assert(ammo["9mm"] == 1 and ammo["9mm_2"] == 2 and ammo.rifle == 3 and ammo.goldengun == 13)
  assert(not inv.has("ak47") and inv.has("wppk") and not pcall(inv.has, "bfg"))
  assert(inv.give("ak47") == true and inv.give(8) == false and inv.has("ak47"))
  assert(inv.give("remotemine") and inv.has("trigger"), "the detonator comes with remote mines")
  for _, no in ipairs({ "keycard", "watchlaser", "fist", "unarmed", "trigger", "token" }) do
    assert(not pcall(inv.give, no) and not pcall(inv.take, no), no .. " isn't a mod's to give or take")
  end
  assert(inv.take("wppk") == true and inv.take("wppk") == false and not inv.has("wppk"))
  assert(ammo.max("rifle") == 400 and ammo.max(1) == 800 and ammo.max("rockets") == 3)
  assert(ammo.set("rifle", 999) == 400 and ammo.get("rifle") == 400, "clamped to the most")
  assert(ammo.set(3, -5) == 0, "never below 0")
  assert(ammo.set("9mm", 37) == 37 and ammo.get(1) == 37)
  for _, no in ipairs({ "laser", 0, 14, 2.5 }) do
    assert(not pcall(ammo.get, no) and not pcall(ammo.set, no, 1) and not pcall(ammo.max, no))
  end
  assert(not pcall(ammo.set, "rifle", 1.5))
  print("items ok")
end)
"""
    out = run(exe, temp, "inventory", lua, "own", 4, *play, "inv", "ammois", 1, "ammois", 3)
    check(logs(out) == ["items ok"], "inventory and ammo: " + out[-700:])
    check(answers(out, "INV", "AMMO") == ["INV 8 29 30 held 1 0", "AMMO 1 37", "AMMO 3 0"],
          "given, taken (the gun hand to its fist), the rounds set: " + str(answers(out, "INV", "AMMO")))


def hands_placement():
    """hand_near at the game's own reach; the torso's frame kept for a mod; grips told on the tick"""
    body = (ROOT / "src/game/gevr_bodyslots.c").read_bytes().decode()
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    haptics_h = (ROOT / "port/vr/vr_haptics.h").read_bytes().decode()
    haptics = (ROOT / "port/vr/vr_haptics.cpp").read_bytes().decode()
    entered = body.index("        h->slot = slot;\n        h->looking = gevrBodyLookedAt(slot, FALSE);")
    near = body.index("gevrModApiHandNear(ctrl, slot);")
    check(entered < near < body.index("        return;", near) and body.index("gevrBodyBuzz(ctrl, GEVR_ACTION_SLOT_HOVER);") < near,
          "hand_near as a hand comes into a slot, with the reach's own buzz")
    check("(gevrHandReloadActive() || (g_gevrStereo && gevrModScriptRunning()))" in body,
          "the torso's frame kept in stereo play while a mod runs, the slots off")
    check("out[0] = VrLeftHandedMode ? -x : x;" in body, "x toward the gun hand's side, either handedness")
    check(body.index('#include "gevr_modapi.h"') < body.index("gevrModApi"), "gevr_bodyslots.c includes the header first")
    inp = view[view.index("void gevrGripGestureInput("):]
    check(inp.index("s_gevrModGripDown[ctrl] = 1;") < inp.index("    if (!held)\n"), "a press or a let go noted before the let go returns")
    tick = view[view.index("void gevrGripGestureTick(void)"):]
    tick = tick[:tick.index("\n}\n")]
    check(tick.index("gevrGripGestureTry(ctrl) ? 2 : 3;") < tick.index("gevrModApiGrip(ctrl, anchor, s_gevrGripGesture[ctrl] == 2)")
          and "get_cur_playernum() == netGetLocalSlot()" in tick[tick.index("s_gevrModGripDown[ctrl] ||"):],
          "a grip told once the game decided it, on the local player's tick")
    check("#define GEVR_ACTION_MOD 1008" in game and "#define GEVR_ACTION_MOD              1008" in haptics_h
          and '{ GEVR_ACTION_MOD,              "Mods"' in haptics, "a mod's pulses go through the player's Mods haptics row")


def hands(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)

    lua = """
mod.on("grip", function(hand, anchor, taken) print("grip", hand, anchor, taken) end)
mod.on("release", function(hand, anchor) print("release", hand, anchor) end)
mod.on("hand_near", function(hand, anchor) print("near", hand, anchor) end)
"""
    out = run(exe, temp, "grips", lua, "start", "prepare", 9, "grip", 1, 4, 1, "begin", 9, "grip", 1, 4, 1,
              "grip", 0, -1, 0, "release", 1, -1, "release", 0, 5, "near", 0, 5, "near", 1, 0, "local", 0,
              "grip", 1, 0, 1, "near", 1, 2)
    check(logs(out) == ["grip right chest true", "grip left nil false", "release right nil", "release left belt",
                        "near left belt", "near right hip_gun"],
          "grip, release and hand_near in play, for this machine's player: " + str(logs(out)))

    lua = r"""
mod.on("mission_start", function()
  local h = mod.hands
  local x, y, z = h.position("right")
  assert(x == 20 and y == -40 and z == 30, "the gun hand")
  assert(h.position(1) == nil, "untracked")
  local ax, ay, az, r = h.anchor("hip_gun")
  assert(ax == 18 and ay == -55 and az == 5 and r == 12)
  assert(select(4, h.anchor("belt")) == 10)
  assert(not pcall(h.anchor, "pocket") and not pcall(h.anchor))
  assert(h.haptic("left", 0.5, 0.1) == true and h.haptic("right", 0, 0) == true)
  for _, bad in ipairs({ { "right", 1.5, 0.1 }, { "right", -0.1, 0.1 }, { "right", 0.5, 0.6 }, { "right", 0.5, -1 },
                         { "middle", 0.5, 0.1 }, { "right", 0 / 0, 0.1 } }) do
    assert(not pcall(h.haptic, table.unpack(bad)), "past a limit")
  end
  print("hands ok")
end)
"""
    out = run(exe, temp, "hands", lua, "handat", 0, 20, -40, 30, "untracked", 1, *play)
    check(logs(out) == ["hands ok"] and answers(out, "HAPTIC") == ["HAPTIC 1 0.50 0.10", "HAPTIC 0 0.00 0.00"],
          "mod.hands: positions, anchors, pulses within limits: " + out[-700:])

    lua = r"""
mod.on("mission_start", function()
  local h = mod.hands
  assert(h.position("right") == nil and h.anchor("chest") == nil and h.haptic("right", 0.5, 0.1) == false)
  print("flat ok")
end)
"""
    out = run(exe, temp, "flat", lua, "torso", 0, *play)
    check(logs(out) == ["flat ok"] and answers(out, "HAPTIC") == [], "without VR hands: nil and false: " + out[-500:])


TOLD_FIXTURE = r"""
/* gevr_modgame.c's guard bookkeeping over a stub guard table: told once,
 * fresh slots, the guard's own marks, dying guards, the host only */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef int s32;
typedef float f32;
enum { PROP_TYPE_CHR = 3, ACT_DIE = 4, ACT_DEAD = 5, ACT_STAND = 1 };
enum { GEVR_MODALERT_SAW = 1, GEVR_MODALERT_HEARD, GEVR_MODALERT_SHOT, GEVR_MODALERT_ALARM, GEVR_MODALERT_OTHER };
enum { GEVR_MODKILL_SHOT = 1, GEVR_MODKILL_EXPLOSION };
typedef struct { float x, y, z; } coord3d;
typedef struct PropRecord { int type; coord3d pos; } PropRecord;
typedef struct ChrRecord { short chrnum; int actiontype; int lastseetarget60, lastheartarget60; PropRecord *prop; } ChrRecord;
typedef signed char s8;
static ChrRecord slots[4];
static PropRecord props[4];
static ChrRecord *g_ChrSlots = slots;
static s32 g_NumChrSlots = 4;
static int running = 1, online, host, curPlayer;
static int gevrModScriptRunning(void) { return running; }
static int netIsActive(void) { return online; }
static int netIsHost(void) { return host; }
static int get_cur_playernum(void) { return curPlayer; }
static char told[512];
static void note(const char *fmt, int a, int b, int c)
{
    const size_t n = strlen(told);
    snprintf(told + n, sizeof(told) - n, fmt, a, b, c);
}
static void gevrModApiGuardAlerted(int id, int how, int player) { note("alerted %d %d %d;", id, how, player); }
static void gevrModApiGuardKilled(int id, int how, int item, int player)
{
    note("killed %d %d %d", id, how, item);
    note(" %d;", player, 0, 0);
}
static void gevrModApiCorpseRetiring(int id, const float pos[3]) { note("retiring %d %d %d;", id, (int)pos[0], (int)pos[2]); }
/* PRODUCTION */
int main(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        slots[i].chrnum = (short)(10 + i);
        slots[i].actiontype = ACT_STAND;
        slots[i].prop = &props[i];
        props[i].type = PROP_TYPE_CHR;
        props[i].pos.x = 100.0f * i;
        props[i].pos.z = -5.0f;
        gevrModGuardFresh(&slots[i]);
    }
    /* told once, however it's alerted after */
    curPlayer = 2;
    gevrModGuardAlert(&slots[0], GEVR_MODALERT_SAW);
    gevrModGuardAlert(&slots[0], GEVR_MODALERT_OTHER);
    gevrModGuardAlert(&slots[0], GEVR_MODALERT_HEARD);
    assert(!strcmp(told, "alerted 10 1 2;"));
    /* the game's own marks: a guard that saw or heard a player before */
    told[0] = 0;
    slots[1].lastheartarget60 = 77;
    gevrModGuardAlert(&slots[1], GEVR_MODALERT_SAW);
    assert(!told[0]);
    /* dying guards, retired bodies (chrnum -1), other props: nothing */
    slots[2].actiontype = ACT_DIE;
    gevrModGuardAlert(&slots[2], GEVR_MODALERT_HEARD);
    slots[2].actiontype = ACT_STAND;
    slots[2].chrnum = -1;
    gevrModGuardAlert(&slots[2], GEVR_MODALERT_HEARD);
    slots[2].chrnum = 12;
    props[2].type = 1;
    gevrModGuardAlert(&slots[2], GEVR_MODALERT_HEARD);
    props[2].type = PROP_TYPE_CHR;
    assert(!told[0]);
    /* a co-op client runs no guards: nothing; its host does */
    online = 1; host = 0;
    gevrModGuardAlert(&slots[2], GEVR_MODALERT_ALARM);
    gevrModGuardKilled(&slots[2], GEVR_MODKILL_SHOT, 8);
    assert(!told[0]);
    host = 1;
    gevrModGuardAlert(&slots[2], GEVR_MODALERT_ALARM);
    assert(!strcmp(told, "alerted 12 4 2;"));
    online = host = 0;
    /* killed and retiring once each; a slot's new guard starts over */
    told[0] = 0;
    gevrModGuardKilled(&slots[3], GEVR_MODKILL_EXPLOSION, -1);
    gevrModGuardKilled(&slots[3], GEVR_MODKILL_SHOT, 8);
    gevrModGuardRetiring(&slots[3]);
    gevrModGuardRetiring(&slots[3]);
    assert(!strcmp(told, "killed 13 2 -1 -1;retiring 13 300 -5;"));
    /* a shot's death names the player whose shot hit it last; a guard's own shot, or none, names no one */
    told[0] = 0;
    gevrModGuardShotBy(&slots[1], 3);
    gevrModGuardShotBy(&slots[1], 2);
    gevrModGuardKilled(&slots[1], GEVR_MODKILL_SHOT, 8);
    assert(!strcmp(told, "killed 11 1 8 2;"));
    told[0] = 0;
    gevrModGuardFresh(&slots[1]);
    gevrModGuardShotBy(&slots[1], 1);
    gevrModGuardShotBy(&slots[1], -1);
    gevrModGuardKilled(&slots[1], GEVR_MODKILL_SHOT, 4);
    assert(!strcmp(told, "killed 11 1 4 -1;"));
    told[0] = 0;
    gevrModGuardFresh(&slots[1]);
    gevrModGuardShotBy(&slots[1], 0);
    gevrModGuardKilled(&slots[1], GEVR_MODKILL_EXPLOSION, -1);
    assert(!strcmp(told, "killed 11 2 -1 -1;"));
    told[0] = 0;
    slots[0].chrnum = 5001;
    gevrModGuardFresh(&slots[0]);
    gevrModGuardAlert(&slots[0], GEVR_MODALERT_SHOT);
    assert(!strcmp(told, "alerted 5001 3 2;"));
    /* outside the slots: never told */
    told[0] = 0;
    {
        ChrRecord stray = slots[1];

        stray.lastheartarget60 = 0;
        gevrModGuardAlert(&stray, GEVR_MODALERT_SAW);
        gevrModGuardKilled(&stray, GEVR_MODKILL_SHOT, 4);
    }
    assert(!told[0]);
    /* no mod running: nothing */
    running = 0;
    gevrModGuardKilled(&slots[1], GEVR_MODKILL_SHOT, 4);
    assert(!told[0]);
    puts("ok");
    return 0;
}
"""


def function(source, signature):
    """A function's definition (not a prototype) from its signature to its closing brace"""
    start = source.index(signature)
    while source[source.index(")", start) + 1:].lstrip()[:1] != "{":
        start = source.index(signature, start + 1)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def guards_told(temp):
    """gevr_modgame.c's own bookkeeping: alerted, killed and retiring once per guard, on the machine running guards"""
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    start = game.index("/* What each slot's guard has been told")
    told = game[start:game.index("/* A stage's guard:", start)]
    production = "\n".join([function(game, "static int runsGuards(void)"), told])
    source = temp / "told.c"
    source.write_bytes(TOLD_FIXTURE.replace("/* PRODUCTION */", production).encode())
    exe = temp / ("told.exe" if os.name == "nt" else "told")
    cc = shutil.which("gcc") or "cc"
    sanitize = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([cc, "-std=gnu11", "-O1", "-Wall", "-Werror", "-Wno-unused-function", *sanitize, str(source), "-o", str(exe)],
                   check=True)
    out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    check(out.returncode == 0 and out.stdout.strip() == "ok", "guards told once, on the host: " + out.stdout + out.stderr)


def guards_placement():
    """guard_spawning before the model; the gun at weaponAssignToHome; alerts once from each way in; deaths; corpses"""
    act = (ROOT / "src/game/chraction.c").read_bytes().decode().replace("\r\n", "\n")
    ai = (ROOT / "src/game/chrai.c").read_bytes().decode().replace("\r\n", "\n")
    chr_ = (ROOT / "src/game/chr.c").read_bytes().decode()
    prop = (ROOT / "src/game/prop.c").read_bytes().decode()
    expand = function(act, "void expand_09_characters(")
    check(expand.index("cmfr->hasHead == 0") < expand.index("gevrModGuardSpawning(arg1, arg2, &bodyid, &headid);")
          < expand.index("retrieve_header_for_body_and_head(bodyid, headid"),
          "guard_spawning after the game's own body and head, before the model is made")
    assign = function(prop, "void weaponAssignToHome(")
    check(assign.index("chr = chrFindByLiteralId(weapon->pad);") < assign.index("gevrModGuardWeapon(weapon);")
          < assign.index("cheatIsActive(CHEAT_ENEMY_ROCKETS)"), "a guard's chosen gun before the rockets cheat")
    sight = function(act, "bool chrCheckTargetInSight(")
    check(sight.index("gevrModGuardAlert(self, GEVR_MODALERT_SAW)") < sight.rindex("chrlvSetTargetToPlayer(self);"),
          "saw: told before the sight sets the target")
    argh = function(act, "void chrlvTickArgh(")
    check(argh.index("GEVR_MODALERT_SHOT") < argh.index("chrlvSetTargetToPlayer(self);"), "shot: told as the hit's reaction ends")
    for name, how in (("void chrlvSetTargetToPlayer(", "GEVR_MODALERT_OTHER"),
                      ("void chrlvAlertGuardToPlayerPosition(", "GEVR_MODALERT_HEARD")):
        fn = function(act, name)
        check(fn.index(how) < fn.index("temp_v0 = getCurrentPlayerProp();"), how + ": told before the setter's marks")
    alarm = ai[ai.rindex("case AI_AlarmOn:"):ai.rindex("case AI_AlarmOff:")]   # the interpreter's, not the size table's
    check(alarm.index("GEVR_MODALERT_ALARM") < alarm.index("alarmActivate();"), "alarm: the guard whose list raised it")
    hit = function(act, "void triggered_on_shot_hit(")
    check(hit.index("increment_num_kills_display_text_in_MP();") < hit.index("gevrModGuardKilled(self, GEVR_MODKILL_SHOT, item);"),
          "guard_killed for a shot's death")
    blast = function(act, "s32 chrlvExplosionDamage(")
    check(blast.index("increment_num_kills_display_text_in_MP();") < blast.index("GEVR_MODKILL_EXPLOSION"),
          "guard_killed for an explosion's")
    dead = function(act, "void chrlvTickDead(")
    check(dead.index("gevrModGuardRetiring(self);") < dead.index("self->hidden |= CHRHIDDEN_REMOVE;"),
          "corpse_retiring before the body is marked for removal")
    keep = function(act, "s32 gevrBodyRetireForAi(")
    check(keep.index("gevrModGuardRetiring(chr);") < keep.index("chr->chrnum = -1;"),
          "a kept body's corpse_retiring before its id retires")
    alloc = chr_[chr_.index("var_s0->lastheartarget60 = 0;"):]
    check(alloc.index("gevrModGuardFresh(var_s0);") < 120, "a slot's new guard starts untold")
    for name, text in (("chraction.c", act), ("chrai.c", ai), ("chr.c", chr_), ("prop.c", prop)):
        check(text.index('#include "gevr_modapi.h"') < text.index("gevrMod"), name + " includes gevr_modapi.h before its first hook")


def guards(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)

    # ---- guard_spawning: body, head and gun, as the setup loads ----
    lua = """
mod.on("guard_spawning", function(g)
  print("ask", g.id, g.index, g.body, g.head, g.weapon)
  if g.id == 1 then return { body = 3, head = 50, weapon = "ak47" } end
  if g.id == 2 then return { body = 5 } end
  if g.id == 3 then g.weapon = mod.items.rocketlaunch return g end
end)
"""
    out = run(exe, temp, "spawning", lua, "start", "spawning", 9, 0, 1, 47, 4, "prepare", 9,
              "spawning", 1, 10, 0, 47, 4, "spawning", 2, 11, 1, 47, 8, "spawning", 3, 12, 6, -1, 13,
              "spawning", 4, 13, 2, 48, -1)
    check(answers(out, "GUARD") == ["GUARD 9 body 1 head 47 weapon 4", "GUARD 1 body 3 head 50 weapon 8",
                                    "GUARD 2 body 5 head -1 weapon 8", "GUARD 3 body 6 head -1 weapon 25",
                                    "GUARD 4 body 2 head 48 weapon -1"],
          "guard_spawning: body, head, gun; a body's own head; nothing outside a mission: " + str(answers(out, "GUARD")))
    check(logs(out) == ["ask 1 10 0 47 4", "ask 2 11 1 47 8", "ask 3 12 6 nil 13", "ask 4 13 2 48 nil"],
          "asked: " + str(logs(out)))

    bad = [("{ body = 46 }", "not a head"), ("{ body = 45 }", "not a head"), ("{ body = 99 }", "not a head"),
           ("{ body = 1.5 }", "not a head"), ("{ head = 3 }", "one of the cartridge's heads"),
           ("{ head = 88 }", "one of the cartridge's heads"), ("{ body = 5, head = 50 }", "has its own head"),
           ("{ weapon = 'grenade' }", "a gun"), ("{ weapon = 'watchlaser' }", "a gun"), ("{ weapon = 'knife' }", "a gun"),
           ("false", "answer nil (keep it) or a table"), ("7", "answer nil (keep it) or a table")]
    for n, (answer, why) in enumerate(bad):
        lua = 'mod.on("guard_spawning", function(g) print("asked", g.id) return %s end)' % answer
        out = run(exe, temp, "badguard%d" % n, lua, "start", "prepare", 9, "spawning", 1, 10, 0, 47, 4, "spawning", 2, 11, 0, 47, 4)
        errors = [l for l in out.splitlines() if l.startswith("LOG ") and "mods: tactical: guard_spawning: " in l]
        check(len(errors) == 1 and why in errors[0] and answers(out, "GUARD") == ["GUARD 1 body 0 head 47 weapon 4",
                                                                                 "GUARD 2 body 0 head 47 weapon 4"]
              and logs(out) == ["asked 1"],
              "guard_spawning answered %s: refused (%s), the mod stopped, the setup's guard: %s" % (answer, why, out[-600:]))
    lua = 'mod.on("guard_spawning", function(g) return { weapon = "ak47" } end)'
    out = run(exe, temp, "nogun", lua, "start", "prepare", 9, "spawning", 1, 10, 0, 47, -1)
    check(any("this guard has no gun in the setup" in l for l in out.splitlines()), "a gun only replaces a setup gun")

    # ---- the events, and mod.guards' reads ----
    lua = r"""
mod.on("guard_alerted", function(id, how, player) print("alerted", id, how, player) end)
mod.on("guard_killed", function(id, how, item, player) print("killed", id, how, item, player) end)
mod.on("corpse_retiring", function(id, x, y, z) print("corpse", id, x, y, z) end)
mod.on("mission_start", function()
  local ids = mod.guards.list()
  assert(#ids == 2 and ids[1] == 7 and ids[2] == 8)
  local g = mod.guards.get(7)
  assert(g.id == 7 and g.body == 2 and g.head == 47 and g.alive and not g.alerted and g.weapon == 8 and g.weaponLeft == nil)
  assert(g.health == 75 and g.maxHealth == 100 and g.x == 10 and g.y == 20 and g.z == 30)
  local d = mod.guards.get(8)
  assert(d.head == nil and not d.alive and d.alerted and d.weapon == nil and d.weaponLeft == 4)
  assert(mod.guards.get(99) == nil and mod.guards.get(-70000) == nil and not pcall(mod.guards.get, "seven"))
  print("guards ok")
end)
"""
    out = run(exe, temp, "guardevents", lua, "addguard", 7, 2, 47, 1, 8, -1, 75, 100, 10, 20, 30,
              "addguard", 8, 6, -1, 0, -1, 4, 0, 100, 0, 0, 0, "start", "prepare", 9, "alerted", 7, 1, 0, "begin", 9,
              "alerted", 7, 1, 0, "alerted", 8, 2, 1, "alerted", 9, 3, -1, "alerted", 9, 4, 2, "alerted", 9, 5, 0,
              "killed", 7, 1, 8, 1, "killed", 8, 2, -1, -1, "retiring", 7, 1.5, 2, -3)
    check(logs(out) == ["guards ok", "alerted 7 saw 0", "alerted 8 heard 1", "alerted 9 shot nil", "alerted 9 alarm 2",
                        "alerted 9 other 0", "killed 7 shot 8 1", "killed 8 explosion nil nil", "corpse 7 1.5 2.0 -3.0"],
          "guard_alerted, guard_killed, corpse_retiring in play; mod.guards.list and get: " + str(logs(out)))


def guards2_placement():
    """the shooter at every hit; guard_spawned at the game's spawns; the calls on the game's own commands"""
    act = (ROOT / "src/game/chraction.c").read_bytes().decode().replace("\r\n", "\n")
    ai = (ROOT / "src/game/chrai.c").read_bytes().decode().replace("\r\n", "\n")
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    shot = function(act, "bool handles_shot_actors(")
    check(shot.index("gevrCoopGuardProvoked(self, get_cur_playernum());")
          < shot.index("gevrModGuardShotBy(self, isPlayer ? get_cur_playernum() : -1);")
          < shot.index("PropRecord *myprop = self->prop;"),
          "every hit records its player (none for a guard's), after a client's report returns")
    for case, call in (("case AI_TRYSpawningChrAtPad:", "chrSpawnAtPad("), ("case AI_TRYSpawningChrNextToChr:", "chrSpawnAtChr(")):
        block = ai[ai.rindex(case):]
        block = block[:block.index("break;")]
        check(block.index("PropRecord *spawned = " + call) < block.index("gevrModGuardSpawned(spawned->chr);"),
              case + " tells guard_spawned of the guard it spawned")
    clone = ai[ai.rindex("case AI_TRYCloningChr:"):]
    clone = clone[:clone.index("case AI_TextPrintBottom:")]
    check(clone.index("clone->chrnum = chrnum;") < clone.index("gevrModGuardSpawned(clone);") < clone.index("pass = TRUE;")
          and clone.index("chrGiveWeapon(clone, srcweaponL->obj") < clone.index("gevrModGuardSpawned(clone);"),
          "a clone's guard_spawned once it's armed and its id settled")
    spawn = function(game, "int gevrModGameSpawnGuard(")
    check("padOk(o->pad)" in spawn and "gevrModGameAiListOk(o->ai)" in spawn and "chrSpawnAtPad(NULL," in spawn
          and spawn.index("chrSpawnAtPad(") < spawn.index("armGuard("), "spawn: a checked pad and list, the game's spawn, then guns")
    give = function(game, "int gevrModGameGiveGuardWeapon(")
    check(give.index("old->dualweapon->dualweapon = NULL;") < give.index("objFreePermanently(") < give.index("armGuard("),
          "giveWeapon: the held gun's pair unlinked, the gun freed, then the new one")
    check("if_actor_able_set_on_path(chr, found)" in function(game, "int gevrModGamePatrol("), "patrol: AI_StartPatrol's call")
    check("isBGAIListID(id)" in function(game, "int gevrModGameAiListOk("), "a level script's list isn't a guard's")


def guards2(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("guard_spawned", function(id) print("spawned", id) end)
mod.on("mission_start", function()
  local g = mod.guards
  local a = g.spawn({ pad = 12, body = 2, head = 50, weapon = "ak47", ai = "simple" })
  local b = g.spawn({ pad = 10003, body = 5, weaponLeft = mod.items.wppk, ai = 1025 })
  local c = g.spawn({ pad = 13, body = 1 })
  assert(a == 5000 and b == 5001 and c == 5002, tostring(a) .. tostring(b) .. tostring(c))
  assert(g.spawn({ pad = 999, body = 1 }) == false, "no room there")
  for _, bad in ipairs({ {}, { body = 1 }, { pad = 1 }, { pad = -1, body = 1 }, { pad = 1.5, body = 1 },
                         { pad = 1, body = 46 }, { pad = 1, body = 5, head = 50 }, { pad = 1, body = 1, head = 3 },
                         { pad = 1, body = 1, ai = "alarm" }, { pad = 1, body = 1, ai = 3 }, { pad = 1, body = 1, ai = 4096 },
                         { pad = 1, body = 1, weapon = "grenade" }, { pad = 1, body = 1, weaponLeft = "keycard" } }) do
    assert(not pcall(g.spawn, bad), "refused")
  end
  assert(not pcall(g.spawn, "pad 1"))
  assert(g.get(a).weapon == 8 and g.get(b).weaponLeft == 4 and g.get(c).weapon == nil)
  assert(g.giveWeapon(c, "m16") == true and g.get(c).weapon == 13)
  assert(g.giveWeapon(c, mod.items.wppk, "left") == true and g.get(c).weaponLeft == 4)
  assert(g.giveWeapon(77, "m16") == false, "no such guard")
  assert(not pcall(g.giveWeapon, c, "grenade") and not pcall(g.giveWeapon, c, "m16", "middle"))
  assert(g.patrol(a, 3) == true and g.patrol(a, 9) == false and g.patrol(77, 3) == false and g.patrol(a, 300) == false)
  local paths = g.paths()
  assert(#paths == 2 and paths[1] == 3 and paths[2] == 4)
  print("calls ok")
end)
"""
    out = run(exe, temp, "guards2", lua, "gspawned", 40, *play, "gspawned", 41, "runs", 0, "gspawned", 42)
    check(logs(out) == ["calls ok", "spawned 41", "spawned 42"],
          "spawn, giveWeapon, patrol, paths; guard_spawned in play: " + str(logs(out)))
    check(answers(out, "SPAWNGUARD") == ["SPAWNGUARD pad 12 body 2 head 50 ai 7 weapons 8 -1",
                                         "SPAWNGUARD pad 10003 body 5 head -1 ai 1025 weapons -1 4",
                                         "SPAWNGUARD pad 13 body 1 head -1 ai 2 weapons -1 -1"],
          "the orders as the game gets them (the standard guard by default): " + str(answers(out, "SPAWNGUARD")))
    lua = r"""
mod.on("mission_start", function()
  local g = mod.guards
  assert(g.spawn({ pad = 12, body = 2 }) == false and g.giveWeapon(7, "m16") == false and g.patrol(7, 3) == false)
  assert(not pcall(g.spawn, { pad = 12, body = 46 }), "a bad order is still the mod's error")
  print("client ok")
end)
"""
    out = run(exe, temp, "guards2client", lua, "addguard", 7, 2, 47, 1, 8, -1, 75, 100, 0, 0, 0, "runs", 0, *play)
    check(logs(out) == ["client ok"] and not answers(out, "SPAWNGUARD", "GIVEGUARD"),
          "where the guards don't run (a co-op client): false, nothing done: " + out[-500:])


def damage_placement():
    """before_damage before the game's own damage, damage_applied after it; deaths, downs"""
    act = (ROOT / "src/game/chraction.c").read_bytes().decode().replace("\r\n", "\n")
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    coop = (ROOT / "port/src/net/net_coop.c").read_bytes().decode()
    shot = function(act, "bool handles_shot_actors(")
    check(shot.index("gevrHordeInstaKill(self)") < shot.index("gevrModGuardBeforeDamage(self, &damageToCause, GEVR_MODHOW_SHOT")
          < shot.index("self->damage += damageToCause;") < shot.index("gevrModGuardDamaged(self, damageToCause, GEVR_MODHOW_SHOT")
          < shot.index("play_sound_for_shot_actor(self);\n\n            if (chrlvAttackAnimationRelated7F026F30"),
          "a guard's shot: asked after its damage is reckoned, before it's taken; told after")
    blast = function(act, "s32 chrlvExplosionDamage(")
    check(blast.index("gevrCoopPuppets()") < blast.index("gevrModGuardBeforeDamage(self, &damage, GEVR_MODHOW_EXPLOSION")
          < blast.index("self->numarghs += 1;") < blast.index("self->damage = self->maxdamage;")
          < blast.index("gevrModGuardDamaged(self, damage, GEVR_MODHOW_EXPLOSION"),
          "a blast: asked before it touches the guard (declined, it never hit), told once it's dying")
    hurt = function(view, "void record_damage_kills(")
    check(hurt.index("gevrCoopDowned(get_cur_playernum())) return;") < hurt.index("gevrModPlayerBeforeDamage(&damage_amount, playerid,")
          < hurt.index("playerid = get_cur_playernum();") < hurt.index("f32 damage_dealt = g_playerPerm->handicap * damage_amount;"),
          "the player's damage: asked after co-op's checks, before the attacker's -1 becomes the player and the handicap")
    check(hurt.rindex("gevrModPlayerDamaged(damage_amount, gevrModBy,") > hurt.index("bondviewKillCurrentPlayer();"),
          "damage_applied once the damage is done")
    kill = function(view, "void bondviewKillCurrentPlayer(")
    check(kill.index("g_CurrentPlayer->bonddead == FALSE") < kill.index("gevrModPlayerDied();"), "player_died once per death")
    check(function(coop, "static void coopSetLocalDowned(").index("gevrModApiPlayerDowned(me, down);")
          > function(coop, "static void coopSetLocalDowned(").index("s_downed[me] == down) return;")
          and "gevrModApiPlayerDowned(slot, down);" in function(coop, "static void coopAnnounceDowned("),
          "downed and revived: this headset's player when it changes, the others' as they're announced")


def damage(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("before_damage", function(d)
  print("before", d.target, d.id, d.amount, d.how, d.part, d.item, d.by)
  if d.target == "guard" and d.part == "head" then return d.amount * 2 end
  if d.how == "gas" then return false end
  if d.target == "player" then return { amount = 0.25 } end
end)
mod.on("before_damage", function(d) print("second", d.amount) end)
mod.on("damage_applied", function(d) print("applied", d.target, d.id, d.amount, d.health, d.armour) end)
mod.on("player_downed", function(slot) print("downed", slot) end)
mod.on("player_revived", function(slot) print("revived", slot) end)
mod.on("player_died", function(slot) print("died", slot) end)
"""
    out = run(exe, temp, "damage", lua, "hurt", 1, 7, 1, 1, 1, 8, 0, *play,
              "hurt", 1, 7, 1.5, 1, 1, 8, 0, "hurt", 1, 9, 2, 2, 0, -1, -1, "hurt", 2, 0, 0.5, 4, 0, -1, -1,
              "hurt", 2, 1, 0.1, 3, 0, -1, -1, "downed", 2, 1, "downed", 2, 0, "died", 0)
    check(answers(out, "DAMAGE") == ["DAMAGE 1.00", "DAMAGE 3.00", "DAMAGE 2.00", "DAMAGE 0.25", "DAMAGE cancelled"],
          "a changed amount, one kept, a table's, a cancel; nothing asked outside play: " + str(answers(out, "DAMAGE")))
    check(logs(out) == ["before guard 7 1.5 shot head 8 0", "second 3.0", "applied guard 7 3.0 0.0 nil",
                        "before guard 9 2.0 explosion nil nil nil", "second 2.0", "applied guard 9 2.0 1.0 nil",
                        "before player 0 0.5 hit nil nil nil", "second 0.25", "applied player 0 0.25 2.75 0.25",
                        "before player 1 0.10000000149012 gas nil nil nil",
                        "downed 2", "revived 2", "died 0"],
          "before_damage in turn, false ends it; damage_applied; down, up, died: " + str(logs(out)))

    bad = ["-1", "1001", "0/0", "'lots'", "true", "{ amount = -2 }", "{ amount = 'x' }"]
    for n, answer in enumerate(bad):
        lua = 'mod.on("before_damage", function(d) print("asked", d.id) return %s end)' % answer
        out = run(exe, temp, "baddamage%d" % n, lua, *play, "hurt", 1, 7, 1, 1, 2, 8, 0, "hurt", 1, 8, 1, 1, 2, 8, 0)
        errors = [l for l in out.splitlines() if l.startswith("LOG ") and "mods: tactical: before_damage: " in l]
        check(len(errors) == 1 and answers(out, "DAMAGE") == ["DAMAGE 1.00", "DAMAGE 1.00"] and logs(out) == ["asked 7"],
              "before_damage answered %s: refused, the mod stopped, the game's damage: %s" % (answer, out[-500:]))

    lua = r"""
mod.on("mission_start", function()
  local p = mod.player
  local h, a = p.health()
  assert(h == 0.5 and a == 0.25)
  assert(p.setHealth(1) and p.setArmour(0) and p.setArmour(1))
  h, a = p.health()
  assert(h == 1 and a == 1)
  for _, bad in ipairs({ 0, -0.1, 1.01, 0 / 0 }) do assert(not pcall(p.setHealth, bad)) end
  for _, bad in ipairs({ -0.1, 1.5, 0 / 0 }) do assert(not pcall(p.setArmour, bad)) end
  assert(not pcall(p.setHealth, "full"))
  print("player ok")
end)
"""
    out = run(exe, temp, "player", lua, "vitals", 0.5, 0.25, 0, 0, *play)
    check(logs(out) == ["player ok"], "mod.player: health and armour, set within limits: " + out[-500:])
    lua = r"""
mod.on("mission_start", function()
  assert(mod.player.setHealth(1) == false and mod.player.setArmour(1) == false, "down: nothing set")
  print("down ok")
end)
"""
    out = run(exe, temp, "playerdown", lua, "vitals", 0.1, 0, 0, 1, *play)
    check(logs(out) == ["down ok"], "a downed player's health isn't set: " + out[-400:])


def main():
    placement()
    items_placement()
    hands_placement()
    guards_placement()
    guards2_placement()
    damage_placement()
    with tempfile.TemporaryDirectory(prefix="gevr-modapi-") as temp:
        temp = Path(temp)
        exe = build(temp)
        play = ("start", "prepare", 9, "begin", 9)

        # ---- the events, as a Tactical-style mod uses them: no magazine, no shot ----
        lua = """
local mags = { 7, 0 }
mod.on("weapon_fire_attempt", function(hand, item)
  print("attempt", hand, item, mod.weapon.loaded(hand))
  return mod.weapon.loaded(hand) > 0
end)
mod.on("weapon_fire", function(hand, item, loaded) print("fire", hand, item, loaded) end)
mod.on("reload_start", function(hand, item) print("reload_start", hand, item) end)
mod.on("reload_end", function(hand, item, loaded)
  print("reload_end", hand, item, loaded)
  print("set", mod.weapon.setLoaded(hand, 3))
end)
mod.on("weapon_drawn", function(hand, item) print("drawn", hand, item, mod.weapon.held(hand)) end)
"""
        out = run(exe, temp, "events", lua, *play, "hold", 0, 4, 1, "fire", 0, "fire", 0, "reload", 0, 7,
                  "loadedis", 0, "draw", 1, 8, "local", 0, "fire", 0, "reload", 0, 5, "draw", 1, 4)
        check(logs(out) == ["attempt right 4 1", "fire right 4 0", "attempt right 4 0", "reload_start right 4",
                            "reload_end right 4 7", "set 3", "drawn left 8 8"],
              "events: " + str(logs(out)))
        check(answers(out, "FIRED", "DECLINED", "LOADED") == ["FIRED 0", "DECLINED 0", "LOADED 3", "FIRED 2"],
              "a declined shot spends nothing; setLoaded in reload_end; a copy or bot's shot isn't asked: "
              + str(answers(out, "FIRED", "DECLINED", "LOADED")))
        out = run(exe, temp, "outside", lua, "start", "fire", 0, "prepare", 9, "fire", 0)
        check(logs(out) == [] and answers(out, "FIRED") == ["FIRED 6", "FIRED 5"], "nothing asked outside play")

        # ---- mod.weapon's calls and their limits; mod.items ----
        lua = r"""
assert(mod.items.ak47 == 8 and mod.items.wppk == 4 and mod.items.unarmed == 0 and mod.items.token == 88)
mod.on("mission_start", function()
  local w = mod.weapon
  assert(w.held("right") == 4 and w.held(0) == 4 and w.held("left") == 0)
  assert(not pcall(w.held, "middle") and not pcall(w.held, 2))
  assert(w.setLoaded("right", 99) == 7 and w.loaded("right") == 7, "clamped to the magazine")
  assert(w.setLoaded("right", -3) == 0, "never below 0")
  assert(not pcall(w.setLoaded, "left", 1), "unarmed holds no rounds")
  local s = w.stats("ak47")
  assert(s.magazine == 30 and s.fireRate == 3 and math.abs(s.damage - 1.5) < 1e-6)
  assert(w.stats("wppk").fireRate == nil, "not automatic")
  assert(w.stats("trigger") == nil, "no stats of its own")
  assert(not pcall(w.stats, "bfg") and not pcall(w.stats, 999))
  assert(w.setStats("ak47", { magazine = 45, fireRate = 2, damage = 3.0 }))
  assert(w.stats(8).magazine == 45 and w.stats(8).fireRate == 2)
  for _, bad in ipairs({ { magazine = 0 }, { magazine = 256 }, { magazine = 1.5 }, { fireRate = 0 },
                         { fireRate = 61 }, { damage = -1 }, { damage = 15.01 } }) do
    assert(not pcall(w.setStats, "ak47", bad), "a value past its limit")
  end
  local ok, err = pcall(w.setStats, "ak47", { damage = 16 })
  assert(not ok and err:find("damage is 0 to 15 for ak47 (10 times its own)", 1, true), err)
  assert(w.setStats("ak47", { damage = 15 }), "ten times its own")
  assert(not pcall(w.setStats, "wppk", { fireRate = 2 }), "no fireRate for a single shot")
  assert(not pcall(w.setStats, "trigger", { magazine = 2 }), "no stats of its own")
  assert(w.setStats("wppk", { magazine = 12 }))
  print("calls ok")
end)
"""
        out = run(exe, temp, "calls", lua, *play, "show", 8, "show", 4, "title", "show", 8, "show", 4)
        check(logs(out) == ["calls ok"], "calls: " + out[-600:])
        check(answers(out, "STATS") == ["STATS 8 45 2 15.00", "STATS 4 12 -1 1.00", "STATS 8 30 3 1.50", "STATS 4 7 -1 1.00"],
              "stats changed, then the game's own again at the mission's end: " + str(answers(out, "STATS")))

        # ---- an error puts the stats back at once ----
        lua = """
mod.on("mission_start", function() mod.weapon.setStats("ak47", { magazine = 99 }) end)
mod.on("weapon_fire_attempt", function() error("jam") end)
"""
        out = run(exe, temp, "error", lua, *play, "show", 8, "fire", 0, "show", 8, "fire", 0)
        check(answers(out, "STATS", "FIRED", "DECLINED") == ["STATS 8 99 3 1.50", "FIRED 6", "STATS 8 30 3 1.50", "FIRED 5"],
              "an error: the shot happens, the mod stops, its stats go back: " + str(answers(out, "STATS", "FIRED", "DECLINED")))

        items(exe, temp)
        hands(exe, temp)
        guards(exe, temp)
        guards2(exe, temp)
        damage(exe, temp)
        guards_told(temp)

    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        sys.exit(1)
    print("PASS: mods' weapons area: fire attempts declined or not, weapon_fire, reloads, draws, the local player only, "
          "mod.weapon's calls and limits, mod.items, the stats put back; the hooks' places in the gun code. "
          "Items area: pickup_spawning's swaps, turns and refusals, item_pickup, mod.inventory, mod.ammo; "
          "the hooks' places in the stage's setup and the pickup. Hands area: grip, release, hand_near, "
          "mod.hands' positions, anchors and pulses; the hooks' places. Guards area: guard_spawning's body, head and gun, "
          "refusals, alerts, kills and corpses told once on the host, mod.guards' reads; the hooks' places. Guards, part 2: "
          "spawn, giveWeapon, patrol, paths, guard_spawned, guard_killed's player. Damage and player: before_damage's "
          "answers and refusals, damage_applied, downed, revived, died, mod.player's health and armour; the hooks' places")


if __name__ == "__main__":
    main()
