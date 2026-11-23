"""The mod API's weapons area (docs/mod-system.md, plan step 4): events from
the gun code, mod.weapon's calls and limits, mod.items, and the stats put
back. Builds modapi_native.c around the production port/src/gevr_modapi.c,
gevr_modscript.c and port/external/lua over a fake game, and checks where
the hooks sit in the production gunfire.c and bondview2.c.
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


def main():
    placement()
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

    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        sys.exit(1)
    print("PASS: mods' weapons area: fire attempts declined or not, weapon_fire, reloads, draws, the local player only, "
          "mod.weapon's calls and limits, mod.items, the stats put back; the hooks' places in the gun code")


if __name__ == "__main__":
    main()
