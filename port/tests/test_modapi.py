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
    reload_placement(gun, view)


def reload_placement(gun, view):
    """reload_attempt before each reload that takes rounds from the reserve,
    and a reload_start before each hand reload's reload_end."""
    def body(text, signature):
        start = text.index(signature)
        return text[start:text.index("\n}\n", start)]

    native = gun.index("&& !gevrModApiReloadAttempt(hand, var_s1, get_ammo_in_hands_weapon(hand))")
    lower = gun.index("handptr->weapon_action_state = GUN_ANIM_STATE_RELOAD_LOWER;", native)
    check(native < lower < gun.index("gevrModApiReloadStart(hand, var_s1)", lower)
          and gun.rindex("if (handptr->weapon_action_state == GUN_ANIM_STATE_RELOAD_START)", 0, native) > native - 600,
          "the native reload is asked as it starts, before it lowers and reload_start")
    auto = gun.index("&& !gevrModApiReloadDeclined(hand, var_s1, get_ammo_in_hands_weapon(hand)))")
    check(auto < gun.index("handptr->weapon_action_state = GUN_ANIM_STATE_RELOAD_START;", auto) < auto + 300,
          "an empty gun's automatic reload waits while a mod's answer holds")
    grab = body(view, "s32 gevrReloadGrabBelt(s32 ctrl)")
    check(grab.index("gevrModApiReloadAttempt(") < grab.index("gevrGexMagIn(hand, \"gripped at the belt\", -1);")
          and "return TRUE;   /* a mod declined it" in grab, "the gun hand's squeeze at the belt is asked first")
    belt = view.index("gevrGexAtBelt(gunHand, handAt) && get_ammo_in_hands_weapon(gunHand) > 0")
    fresh = view.index("s_gevrGexHeldRounds = -1;   /* a fresh one */", belt)
    check(view.index("&& !gevrModApiReloadAttempt(gunHand, getCurrentPlayerWeaponId(gunHand)", belt) < fresh < belt + 600,
          "a fresh magazine taken at the belt is asked first")
    touch = view.index("gevrGexMagIn(hand, \"loaded at the belt\", -1);")
    check(view.rindex("|| !gevrModApiReloadAttempt(hand, getCurrentPlayerWeaponId(hand), get_ammo_in_hands_weapon(hand)))", 0, touch)
          > touch - 300, "a dual-wielded gun's touch at the belt is asked first")
    rounds = body(view, "static void gevrGexRoundTick(")
    check(rounds.index("&& !gevrModApiReloadAttempt(gunHand, roundItem,") < rounds.index("const s32 rounds = gevrReloadPickup(gunHand);"),
          "a round taken for a gun loaded a round at a time is asked first")
    seat = body(view, "static void gevrGexMagIn(")
    check(seat.index("if (!s_gevrGexReloadOpen[hand])") < seat.index("gevrModApiReloadStart(hand,")
          < seat.index("sub_GAME_7F0649D8(hand);") < seat.index("gevrModApiReloadEnd(hand,"),
          "a magazine in without a start sends one before its rounds move")
    out = body(view, "static void gevrGexMagOut(")
    check(out.index("gevrModApiReloadStart(") < out.index("s_gevrGexReloadOpen[hand] = TRUE;"),
          "a magazine out opens the reload")
    check("s_gevrGexReloadOpen[hand] = FALSE;   /* a gun drawn" in body(view, "s32 gevrReloadDraw(s32 hand)"),
          "a drawn gun closes the last one's reload")


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


def main():
    placement()
    items_placement()
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

        # ---- reload_attempt: a reload declined takes nothing; the automatic one waits ----
        lua = """
local spare = 0
mod.on("reload_attempt", function(hand, item)
  print("reload_attempt", hand, item, spare)
  return spare > 0
end)
mod.on("reload_start", function(hand, item) print("reload_start", hand, item) end)
mod.on("weapon_fire", function() spare = spare + 1 end)
"""
        out = run(exe, temp, "reloadask", lua, *play,
                  "hold", 0, 4, 0, "reload", 0, 7, "loadedis", 0,
                  "autoreload", 0, 30, "autoreload", 0, 30, "autoreload", 0, 24,
                  "hold", 0, 4, 2, "fire", 0, "reload", 0, 7, "loadedis", 0, "autoreload", 0, 24,
                  "local", 0, "reload", 0, 5, "loadedis", 0)
        check(logs(out) == ["reload_attempt right 4 0", "reload_attempt right 4 0",
                            "reload_attempt right 4 1", "reload_start right 4", "reload_attempt right 4 1"],
              "reload_attempt's events: " + str(logs(out)))
        check(answers(out, "RELOAD", "LOADED", "AUTO") == ["RELOAD DECLINED 0", "LOADED 0", "AUTO HELD", "AUTO HELD",
                                                          "AUTO DECLINED", "LOADED 7", "AUTO RELOADS", "LOADED 5"],
              "declined, nothing moves and the automatic reload waits for a new item or reserve; a copy's isn't asked: "
              + str(answers(out, "RELOAD", "LOADED", "AUTO")))

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

    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        sys.exit(1)
    print("PASS: mods' weapons area: fire attempts declined or not, weapon_fire, reloads asked and declined, draws, the local player only, "
          "mod.weapon's calls and limits, mod.items, the stats put back; the hooks' places in the gun code. "
          "Items area: pickup_spawning's swaps, turns and refusals, item_pickup, mod.inventory, mod.ammo; "
          "the hooks' places in the stage's setup and the pickup")


if __name__ == "__main__":
    main()
