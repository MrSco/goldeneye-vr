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
import time
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
    empty_placement(gun, view)
    speed_placement(view)
    automatic_placement(gun)
    throws_placement((ROOT / "src/game/gun.c").read_bytes().decode(), gun)


def automatic_placement(gun):
    """the firing switch asks gevrModFireAs for the item's branch"""
    fire = gun.index("if (handptr->weapon_action_state == GUN_ANIM_STATE_FIRE)")
    ask = gun.index("switch (gevrModFireAs(var_s1, GEVR_ITEM_AS(var_s1)))", fire)
    check(ask < gun.index("case ITEM_CAMERA:", ask) < gun.index("case ITEM_SKORPION:", ask) < ask + 4000,
          "the firing switch's branch is the mod's choice, else the item's")
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    check("s_modFireMode[item] == 1 ? ITEM_AK47 : ITEM_WPPK" in function(game, "s32 gevrModFireAs(s32 item, s32 as)")
          and "in->fireRate >= 0 ? (u8)in->fireRate : 0xFF" in function(game, "void gevrModGameSetStats("),
          "an automatic's branch or a pistol's; a rate of none put back as the game's 0xFF")


def throws(exe, temp):
    """setStats' throwSpeed, throwLift and throwGuide for a thrown item, read back, refused elsewhere, put back"""
    lua = r"""
mod.on("mission_start", function()
  local w = mod.weapon
  local g = w.stats("grenade")
  print("own", string.format("%.2f", g.throwSpeed), g.throwLift, g.throwGuide, w.stats("ak47").throwSpeed)
  local bad = 0
  for _, t in ipairs({ { throwSpeed = 0 }, { throwSpeed = 101 }, { throwLift = 51 }, { throwLift = -51 },
                       { throwGuide = 1 }, { throwSpeed = "x" } }) do
    if not pcall(w.setStats, "grenade", t) then bad = bad + 1 end
  end
  print("bad", bad, (pcall(w.setStats, "ak47", { throwSpeed = 10 })), (pcall(w.setStats, "ak47", { throwGuide = true })))
  assert(w.setStats("grenade", { throwSpeed = 30, throwLift = -2.5, throwGuide = true }))
  g = w.stats("grenade")
  print("now", g.throwSpeed, g.throwLift, g.throwGuide, g.throwGuideStyle.length, g.throwGuideStyle.fade)
  local looks = 0
  for _, t in ipairs({ { length = 5 }, { length = 501 }, { width = 0.4 }, { width = 31 }, { colour = { 256, 0, 0 } },
                       { colour = { 1, 2 } }, { colour = { 1, 2, 3.5 } }, { colour = "cyan" }, { fade = 1 } }) do
    if not pcall(w.setStats, "grenade", { throwGuide = t }) then looks = looks + 1 end
  end
  print("looks", looks, (pcall(w.setStats, "grenade", { throwGuide = "cyan" })))
  assert(w.setStats("grenade", { throwGuide = { length = 60, width = 1.5, colour = { 0, 255, 255 }, fade = true } }))
  local s = w.stats("grenade").throwGuideStyle
  print("look", w.stats("grenade").throwGuide, s.length, s.width, s.colour[1], s.colour[2], s.colour[3], s.fade)
end)
"""
    out = run(exe, temp, "throws", lua, "start", "prepare", 9, "begin", 9, "show3", 26, "title", "show3", 26)
    check(logs(out) == ["own 16.67 5.0 false nil", "bad 6 false false", "now 30.0 -2.5 true 100.0 false",
                        "looks 9 false", "look true 60.0 1.5 0 255 255 true"],
          "a grenade's throw, the refusals, an AK47's none; a guide's look, its refusals, read back: " + str(logs(out)))
    check(answers(out, "STATS3") == ["STATS3 30.00 -2.50 1 60.0 1.5 0 255 255 1",
                                     "STATS3 16.67 5.00 0 100.0 6.0 255 255 255 0"],
          "set, then the game's own at the mission's end: " + str(answers(out, "STATS3")))


def throws_placement(gun, fire):
    """the three throws launch through the shared helpers; the guide draws from them, without spread"""
    for name, speed in (("void generate_player_thrown_grenade(s32 hand)", "base_velocity = 16.666666f;"),
                        ("void generate_player_thrown_knife(s32 hand)", "base_velocity = 25.0f;"),
                        ("void generate_player_thrown_object(s32 hand)", "base_velocity = 6.6666665f;")):
        body = function(gun, name)
        check(speed in body and body.index("gevrThrowTrigger(hand, base_velocity, &base_speed_vec, &throw_speed_vec);")
              < body.index("gevrThrowAddMotion(&throw_speed_vec);")
              < body.index("throw_item_pos_related.m[3][0]"), name + ": launched through the shared helpers")
    base = function(gun, "f32 gevrThrowBaseSpeed(s32 item)")
    check("return 25.0f;" in base and "return 6.6666665f;" in base and "return 16.666666f;" in base,
          "the guide's base speeds are the throws' own")
    trigger = function(gun, "void gevrThrowTrigger(")
    check("f32 lift = 5.0f;" in trigger and "(aim->f[1] * speed) + lift" in trigger
          and "gevrModThrowStats(getCurrentPlayerWeaponId(hand), &speed, &lift);" in trigger,
          "the game's formula, a mod's speed and lift for the item")
    aim = function(fire, "static void gevrAimNoSpread(")
    check("RANDOM" not in aim and "randomGetNext" not in aim and "gevrStereoShotFromEye(hand, &crosspos, origin, dir)" in aim,
          "the guide's aim draws no random numbers")
    guide = function(fire, "Gfx *gevrThrowGuideDraw(Gfx *gdl, s32 hand, s32 item)")
    check(guide.index("gevrAimNoSpread(&origin, &aim, hand);") < guide.index("gevrThrowTrigger(hand, gevrThrowBaseSpeed(item), &aim, &v);")
          < guide.index("gevrThrowAddMotion(&v);") < guide.index("gdl = sub_GAME_7F061E18(gdl, &guide, 0);")
          and "gevrMotionThrowOwnsHand(hand)" in guide and "VrThrowGuide || gevrModThrowGuide(item)" in guide,
          "the guide: the throw's own calculation, drawn as a tracer, by the setting or a mod, not in a motion throw")
    check(guide.index("weapon_action_state != GUN_ANIM_STATE_IDLE") < guide.index("gevrAimNoSpread(&origin, &aim, hand);")
          and "weapon_action_state != GUN_ANIM_STATE_TRIGGER_PRESS" in guide,
          "only while an item's ready in the hand")
    check(guide.index("s_gevrBeamStyle = styled ? &style : NULL;") < guide.index("gdl = sub_GAME_7F061E18(gdl, &guide, 0);")
          < guide.index("s_gevrBeamStyle = NULL;") and "style.radius = width * 5.0f;" in guide,
          "a pack's look for this one beam only")
    beam = function(fire, "Gfx *sub_GAME_7F061E18(Gfx *gdl, BeamRecord *flash, s32 arg2)")
    check(beam.index("radius = 30.0f;") < beam.index("radius = s_gevrBeamStyle->radius;")
          < beam.index("vtx[3].v.tc[1] = image->height << 5;") < beam.index("vtx[2].v.cn[3] = 0;")
          < beam.index("gSPVertex("), "the beam's radius and colour, and its far end's fade, before it's drawn")
    render = function(fire, "void gunRenderFirstPersonGunModels(Gfx **gdlptr)")
    check(render.index("gdl = sub_GAME_7F061E18(gdl, &handptr->weapon_beam, 0);")
          < render.index("gdl = gevrThrowGuideDraw(gdl, handnum, item);"), "drawn with the hand, after its beam")
    settings = (ROOT / "port/src/gevr_settings.c").read_bytes().decode()
    ini = (ROOT / "port/vr/vr_settings.cpp").read_bytes().decode()
    check('TOGGLE(GEVR_SEC_WEAPONS, "Throw guide", VrThrowGuide,' in settings
          and 'iniPrintf(f, "ThrowGuide=%d\\n", VrThrowGuide ? 1 : 0);' in ini
          and 'strcmp(key, "ThrowGuide") == 0' in ini,
          "the base game's Throw guide: flat and VR, kept in the ini")


def speed_placement(view):
    """setSpeed where the game applies its own speed boost, both ways of moving"""
    boost = view.index("g_CurrentPlayer->speedforwards *= g_CurrentPlayer->speedboost;")
    scale = view.index("const f32 k = gevrModMoveScale();", boost)
    check(scale < boost + 300 and view.index("g_CurrentPlayer->speedsideways *= k;", scale) < scale + 200,
          "the mod's speed scale beside the game's speed boost, forward and sideways")
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    check("gevrModScriptRunning() && gevrModGameLocal() ? s_modMoveScale : 1.0f" in function(game, "f32 gevrModMoveScale(void)"),
          "only while a mod runs, and only the local player's")


def empty_placement(gun, view):
    """out_of_ammo asked where the game switches an empty gun away, after its own reasons not to"""
    switch = function(view, "void gevrAutoAdvanceHand(s32 hand)")
    check(switch.index("item == ITEM_TANKSHELLS") < switch.index("gevrGexMinesHold(hand)")
          < switch.index("if (gevrModApiOutOfAmmo(hand, item))") < switch.index("gevrCycleHandWeaponInternal(hand, 1, TRUE);\n    }"),
          "VR: asked after the tank's own switch and the hand's reasons to stay, before the switch")
    flat = gun.index("if (!gevrModApiOutOfAmmo(hand, var_s1))")
    check(flat < gun.index("autoadvance_on_deplete_all_ammo();", flat) < flat + 200, "flat: asked before the switch")


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


def armour_placement():
    """armour dropped from the mod API's own records, as the stage's armour; a box's or armour's model loaded first"""
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    drop = function(game, "int gevrModGameDrop(int item, int ammo, float armour, int from)")
    check(drop.index("modelLoad(crate->obj);") < drop.index("objInitWithModelDef((ObjectRecord *)crate")
          and drop.index("modelLoad(PROP_BODYARMOUR);") < drop.index("objInitWithModelDef((ObjectRecord *)vest"),
          "a model the stage never placed is loaded before its prop is made")
    check(drop.index("if (s_modArmour[i].prop == NULL) vest = &s_modArmour[i];") < drop.index("vest->type = PROPDEF_ARMOUR;")
          < drop.index("vest->obj = PROP_BODYARMOUR;") < drop.index("vest->amount = armour;")
          < drop.index("chrpropReparent(vest->prop, chr->prop);"),
          "a free record (no prop), made the stage's armour with the amount, on the chr to fall")
    slots = function(game, "static s32 runtimeSlot(ObjectRecord *obj, s32 *kind, s32 *slot)")
    check("*kind = GEVR_MODAPI_PICKUP_ARMOUR;" in slots, "a dropped armour has a runtime id and pickup_removed")
    api = (ROOT / "port/src/gevr_modapi.c").read_bytes().decode()
    check("gevrModGameDropsClear();" in function(api, "void gevrModApiPickupClear(void)"),
          "the pool's records free as a mission loads")
    obj = (ROOT / "src/game/propobj.c").read_bytes().decode()
    check("case PROPDEF_ARMOUR:" in function(obj, "bool objIsCollectable(PropDefHeaderRecord *obj)"),
          "the game collects armour by its type, so a record of the mod API's is taken as the stage's")


def pickup_ids(exe, temp):
    """runtime pickups' ids never given twice in a mission; pickup_removed for one freed untaken, at the next
    frame in play and only then; a new mission's ids afresh"""
    lua = r"""
mod.on("pickup_removed", function(pickup) print("removed", pickup, math.type(pickup)) end)
mod.on("tick", function() print("tick") end)
"""
    play = ("start", "prepare", 9, "begin", 9)
    out = run(exe, temp, "pickupids", lua, *play,
              "slotid", 0, 3, "slotid", 0, 3, "slotid", 1, 3,
              "slotfreed", 0, 3, "slotid", 0, 3,
              "slotid", 1, 5, "slottaken", 1, 5, "slotfreed", 1, 5,
              "slotfreed", 0, 9, "slotid", 0, 64, "slotid", 9, 0, "slotid", -1, 0,
              "frame", "slotfreed", 1, 3, "frame", "frame",
              "prepare", 10, "begin", 10, "slotid", 0, 3)
    check(answers(out, "SLOTID") == ["SLOTID 65536", "SLOTID 65536", "SLOTID 65537", "SLOTID 65538", "SLOTID 65539",
                                     "SLOTID -1", "SLOTID -1", "SLOTID -1", "SLOTID 65536"],
          "a slot keeps its id until freed, then its next pickup a new one; none past the pools; afresh next mission: "
          + str(answers(out, "SLOTID")))
    check(logs(out) == ["removed 65536 integer", "tick", "removed 65537 integer", "tick", "tick"],
          "freed untaken: told at the next frame before tick; taken, or never named: not told: " + str(logs(out)))
    out = run(exe, temp, "pickupwait", lua, "start", "prepare", 9, "slotid", 0, 1, "slotfreed", 0, 1,
              "begin", 9, "slotid", 0, 2, "slotfreed", 0, 2)
    check(logs(out) == [], "none told outside play, nor before the next frame: " + str(logs(out)))
    many = []
    for slot in range(40):
        many += ["slotid", 0, slot, "slotid", 1, slot]
    for slot in range(40):
        many += ["slotfreed", 0, slot, "slotfreed", 1, slot]
    lua = r"""
local n = 0
mod.on("pickup_removed", function() n = n + 1 end)
mod.on("tick", function() print("count", n) end)
"""
    out = run(exe, temp, "pickupmany", lua, *play, *many, "frame", "frame")
    check(logs(out) == ["count 64", "count 64"] and "mods: 16 pickup_removed dropped: more than 64 in a frame" in out,
          "at most 64 a frame, the rest logged: " + str(logs(out)))
    script = (ROOT / "port/src/gevr_modscript.c").read_bytes().decode()
    frame = function(script, "void gevrModScriptFrame(int ticks)")
    check(frame.index("gevrModApiPickupFlush();") < frame.index('fire("tick", &a, 1, 0, 0);'), "told before tick")
    check(script.index("gevrModApiUiClear();    /* and its clicks") < script.index("gevrModApiPickupClear();"),
          "a mission's load clears the last one's ids")


def shared_drops(exe, temp):
    """drop's shared: the host's id from 0x40000000, sent; a player's refused; the host's drop made on a player's
    headset from the same chr with that id; the host deciding who takes it; a recycled one gone everywhere"""
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("mission_start", function()
  print("solo", mod.pickups.drop({ item = "ak47", from = "player", shared = true }),
        mod.pickups.drop({ item = "ak47", from = "player" }),
        (pcall(mod.pickups.drop, { item = "ak47", from = "player", shared = 1 })))
end)
"""
    out = run(exe, temp, "sharedsolo", lua, *play)
    check(logs(out) == ["solo 1073741824 65536 false"] and not answers(out, "NETSEND"),
          "solo: a shared id, nothing sent; shared is a boolean: " + str(logs(out)))

    lua = r"""
mod.on("mission_start", function()
  print("host", mod.pickups.drop({ item = "ak47", from = 1005, shared = true }),
        mod.pickups.drop({ armour = 0.5, from = "player", shared = true }),
        mod.pickups.drop({ ammo = "rifle", from = "player" }),
        mod.pickups.drop({ item = "ak47", from = "player", shared = true }),
        (pcall(mod.pickups.drop, { item = "ak47", from = 40000, shared = true })))
end)
mod.on("pickup_removed", function(p) print("removed", p) end)
"""
    # the host: another player's pickup held back, its own awarded at once and told; two claims for the armour in
    # one frame (the first wins, the second and a late claim for the gun get gone); a shared gun recycled
    out = run(exe, temp, "sharedhost", lua, "online", 1, 1, 0, 0, *play,
              "pickuptry", 0, 1, 0, "pickuptry", 0, 0, 1, "slottaken", 0, 0, "slotfreed", 0, 0, "pickuptry", 1, 0, 1,
              "netpickup", "tactical", 1, 3, "0900000001000040", "netpickup", "tactical", 3, 3, "0900000001000040",
              "netpickup", "tactical", 3, 3, "0900000000000040", "frame",
              "slotfreed", 0, 1, "frame", "slotid", 0, 1)
    check(logs(out) == ["host 1073741824 1073741825 65536 1073741826 false", "removed 1073741825", "removed 1073741826"],
          "the host: shared ids, a local one beside them; the armour awarded away and the recycled gun gone here: "
          + str(logs(out)))
    check(answers(out, "TRY") == ["TRY 1", "TRY 0", "TRY 0"],
          "another player's pickup of a shared one held back here; the host's own and a box not: "
          + str(answers(out, "TRY")))
    check(answers(out, "NETSEND") == ["NETSEND 2 255 tactical - 12 09 08 00 00 00 00 00 40 ed 03 00 00",
                                      "NETSEND 2 255 tactical - 12 09 00 00 00 01 00 00 40 ff ff 00 80",
                                      "NETSEND 2 255 tactical - 12 09 08 00 00 02 00 00 40 ff ff 00 00",
                                      "NETSEND 4 255 tactical - 8 09 00 00 00 00 00 00 40",
                                      "NETSEND 4 255 tactical - 8 09 01 00 00 01 00 00 40",
                                      "NETSEND 5 3 tactical - 8 09 00 00 00 01 00 00 40",
                                      "NETSEND 5 3 tactical - 8 09 00 00 00 00 00 00 40",
                                      "NETSEND 5 255 tactical - 8 09 00 00 00 02 00 00 40"],
          "sent: three drops (the box isn't shared); the gun awarded to the host's player (slot 0) as it walked over "
          "it; the armour to slot 1's claim; slot 3's claim for it, and its late one for the gun, answered gone; the "
          "recycled gun gone everywhere: " + str(answers(out, "NETSEND")))
    check(answers(out, "REMOVE") == ["REMOVE 2 0"] and answers(out, "SLOTID") == ["SLOTID 65537"],
          "the armour's copy freed here once; the recycled slot's next pickup a new id: "
          + str(answers(out, "REMOVE", "SLOTID")))

    lua = r"""
mod.on("mission_start", function()
  print("client", (pcall(mod.pickups.drop, { item = "ak47", from = "player", shared = true })))
end)
mod.on("pickup_removed", function(p) print("removed", p) end)
"""
    # a player (slot 2): the host's drops made here; its walk-over claims once (not again within half a second),
    # another's held back; the gun awarded to it taken through the game's pickup, the armour awarded to slot 3
    # removed; a shared gun recycled here sent gone; a gone for none, and a claim, ignored
    out = run(exe, temp, "sharedclient", lua, "online", 1, 0, 0, 2, *play,
              "netpickup", "tactical", 0, 2, "0908000000000040ffff0000",
              "netpickup", "tactical", 0, 2, "0708000009000040ffff0000",
              "netpickup", "tactical", 0, 2, "0908000003000040ffff00",
              "netpickup", "tactical", 0, 2, "0900000001000040ed030040",
              "netpickup", "tactical", 0, 2, "0908000002000040ffff0000",
              "frame",
              "slotid", 0, 0, "slotid", 2, 0, "slotid", 0, 1,
              "pickuptry", 0, 0, 1, "pickuptry", 0, 0, 1, "pickuptry", 2, 0, 0,
              "netpickup", "tactical", 0, 4, "0902000000000040", "netpickup", "tactical", 0, 4, "0903000001000040",
              "frame", "slotfreed", 0, 1,
              "netpickup", "tactical", 0, 5, "0900000007000040", "netpickup", "tactical", 0, 3, "0900000002000040",
              "frame")
    check(logs(out) == ["client false", "removed 1073741825", "removed 1073741826"],
          "a player's shared drop refused; the armour another won and the recycled gun removed here: "
          + str(logs(out)))
    check(answers(out, "DROP") == ["DROP 8 0 0.00 -16", "DROP 0 0 0.25 1005", "DROP 8 0 0.00 -16"],
          "the host's drops made here at the next frame, from the host's player (slot 0) and the guard; another "
          "mission's and a short one not: " + str(answers(out, "DROP")))
    check(answers(out, "SLOTID") == ["SLOTID 1073741824", "SLOTID 1073741825", "SLOTID 1073741826"],
          "bound to the host's ids: " + str(answers(out, "SLOTID")))
    check(answers(out, "TRY") == ["TRY 1", "TRY 1", "TRY 1"]
          and answers(out, "NETSEND") == ["NETSEND 3 0 tactical - 8 09 00 00 00 00 00 00 40",
                                          "NETSEND 5 255 tactical - 8 09 00 00 00 02 00 00 40"],
          "held back for the host: one claim to it; the recycled gun sent gone: "
          + str(answers(out, "TRY", "NETSEND")))
    check(answers(out, "GRANTED") == ["GRANTED 0 0 0"] and answers(out, "REMOVE") == ["REMOVE 2 0"],
          "the award taken through the game's own pickup, not held back; another's award removes the copy: "
          + str(answers(out, "GRANTED", "REMOVE")))
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    grant = function(game, "int gevrModGamePickupGrant(int kind, int slot)")
    check(grant.index("set_cur_player(localSlot());") < grant.index("op = propPickupByPlayer(prop, TRUE);")
          < grant.index("propExecuteTickOperation(prop, op);") < grant.index("set_cur_player(was);"),
          "an award taken by the game's own pickup, as the local player, as AI_BondCollectObject takes one")
    obj = (ROOT / "src/game/propobj.c").read_bytes().decode().replace("\r\n", "\n")
    pick = obj[obj.index("TICKOP propPickupByPlayer(PropRecord *prop, bool showstring)"):]
    check(pick.index("if (g_CurrentPlayer->bonddead || g_ClockTimer == 0)") < pick.index("if (gevrModPickupHold(prop))")
          < pick.index("switch (obj->type)"), "held back before the pickup changes anything")

def drops_placement():
    """weapon_dropped at the game's drop; a mod's drop through the game's own; ids as co-op numbers them"""
    obj = (ROOT / "src/game/propobj.c").read_bytes().decode().replace("\r\n", "\n")
    setdropped = function(obj, "void propobjSetDropped(PropRecord *prop, DROPTYPE droptype)")
    check(setdropped.index("gevrModWeaponDropped(prop);") < setdropped.index("sub_GAME_7F03FDA8(prop);"),
          "weapon_dropped as a held weapon is set to fall, still in its chr's hand")
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    ids = function(game, "static int pickupId(ObjectRecord *target)")
    check(ids.index("if (runtimeSlot(target, &kind, &slot)) return gevrModApiPickupSlotId(kind, slot);")
          < ids.index("sizepropdef(def)"),
          "a runtime pickup's id from the mod API's table, the stage's own by its setup index")
    slots = function(game, "static s32 runtimeSlot(ObjectRecord *obj, s32 *kind, s32 *slot)")
    check("sizeof(WeaponObjRecord) * MAX_WEAPON_SLOTS" in slots and "sizeof(AmmoCrateRecord) * MAX_AMMO_CRATES" in slots,
          "the game's two runtime pools")
    taken = function(game, "void gevrModPickupTaken(")
    check(taken.index("got.pickup = pickupId((ObjectRecord *)rec);") < taken.index("gevrModApiPickupSlotTaken(pool, slot);"),
          "item_pickup carries the pickup's id, and a runtime one is marked taken before it's freed")
    check("gevrModApiPickupSlotFreed(kind, slot);" in function(game, "void gevrModPickupFreed(ObjectRecord *obj)"),
          "a freed runtime pickup's id goes")
    free = function(obj, "void objFree(ObjectRecord* obj, s32 freeprop, s32 canregen)")
    check(free.index("gevrModPickupFreed(obj);") < free.index("if (obj->type == PROPDEF_AUTOGUN)"),
          "every free of an object asks first, while it's whole")
    drop = function(game, "int gevrModGameDrop(int item, int ammo, float armour, int from)")
    check(drop.index("s_modDropping = TRUE;") < drop.index("something_with_generating_object(chr, propid, (ITEM_IDS)item, 0x20000000")
          < drop.index("ammocrateAllocate()") < drop.index("chrpropReparent(crate->prop, chr->prop);")
          < drop.index("vest->type = PROPDEF_ARMOUR;")
          < drop.index("propobjSetDropped(prop, DROPTYPE_DEFAULT);") < drop.index("objDrop(prop);")
          < drop.index("s_modDropping = FALSE;"),
          "a mod's drop: drop_inventory's gun, a shot crate's box, then the game's fall; no weapon_dropped for it")
    check("if (s_modDropping" in function(game, "void gevrModWeaponDropped(PropRecord *prop)"),
          "a mod's own drop isn't told back to it")


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
    lua = 'mod.on("item_pickup", function(kind, id, rounds, pickup) print("took", kind, id, rounds, pickup) end)'
    out = run(exe, temp, "took", lua, "start", "prepare", 9, "took", "weapon", 8, "begin", 9, "took", "weapon", 8,
              "took", "ammo", 3, 10, "took", "crate", "3:20,4:5", "took", "armour", 0.5, "took", "key", "took", "object",
              "local", 0, "took", "weapon", 4)
    check(logs(out) == ["took weapon 8 nil 77", "took ammo 3 10 77", "took ammo 3 20 77", "took ammo 4 5 77",
                        "took armour 0.5 nil 77", "took key nil nil 77", "took object nil nil 77"],
          "item_pickup in play, a crate as its types, never a copy's or a bot's: " + str(logs(out)))

    # ---- mod.pickups.drop and weapon_dropped ----
    lua = r"""
mod.on("mission_start", function()
  local bad = 0
  local function try(t) if not pcall(mod.pickups.drop, t) then bad = bad + 1 end end
  try({ from = "player" })
  try({ item = "ak47", ammo = "rifle", from = "player" })
  try({ item = "watchlaser", from = "player" })
  try({ item = "ak47" })
  try({ item = "ak47", from = "guard" })
  try({ item = "ak47", from = -3 })
  try({ ammo = 99, from = "player" })
  try({ armour = 0, from = "player" })
  try({ armour = 1.5, from = "player" })
  try({ armour = "full", from = "player" })
  try({ armour = 0.5, ammo = "rifle", from = "player" })
  print("bad", bad)
  print("drops", mod.pickups.drop({ item = "ak47", from = "player" }), mod.pickups.drop({ ammo = "rifle", from = 1005 }),
        mod.pickups.drop({ item = 8, from = 999 }), mod.pickups.drop({ armour = 0.5, from = 1005 }))
end)
mod.on("weapon_dropped", function(pickup, item, from) print("dropped", pickup, item, from) end)
"""
    out = run(exe, temp, "drop", lua, "start", "prepare", 9, "dropped", 32770, 8, 1005, "begin", 9,
              "dropped", 32770, 8, 1005, "dropped", -1, 4, -1, "dropped", 5, 26, -2)
    check(logs(out) == ["bad 11", "drops 65536 65537 nil 65538", "dropped 32770 8 1005", "dropped nil 4 player",
                        "dropped 5 26 nil"],
          "drop's answers and refusals; weapon_dropped's id, item and who (a guard, a player, none), in play: "
          + str(logs(out)))
    check(answers(out, "DROP") == ["DROP 8 0 0.00 -1", "DROP 0 3 0.00 1005", "DROP 8 0 0.00 999", "DROP 0 0 0.50 1005"],
          "the game side: a gun from the player, a rifle box from a guard, none from a guard that isn't, half armour: "
          + str(answers(out, "DROP")))

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


def pads_placement():
    """goTo and face on the AI's own commands, where the guards run, for a living guard and a pad"""
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    goto = function(game, "int gevrModGameGoTo(int id, int pad, int speed)")
    face = function(game, "int gevrModGameFace(int id, int pad)")
    for name, text in (("goTo", goto), ("face", face)):
        check("!gevrModGameRunsGuards() || chr == NULL || guardDying(chr) || !padOk(pad)" in text,
              name + ": where the guards run, a living guard, a pad the setup has")
    check("chrGoToPad(chr, pad, speed == 2 ? SPEED_SPRINT : speed == 1 ? SPEED_RUN : SPEED_WALK)" in goto,
          "goTo: chrGoToPad at the speed asked")
    check("check_set_actor_standing_still(chr, TARGET_PAD, pad)" in face, "face: as AI_TRYFacingTarget at a pad")
    hat = function(game, "int gevrModGameGiveHat(int id, int hat)")
    check("chr->handle_positiondata_hat != NULL" in hat and "hatCreateForChr(chr, PROP_HATFURRY + hat, 0)" in hat
          and "!gevrModGameRunsGuards()" in hat, "giveHat: as AI_TRYGiveMeHat, one hat a guard, where the guards run")
    pad = function(game, "int gevrModGamePad(int pad, GevrModPad *out)")
    check("if (!padOk(pad)) return 0;" in pad and "g_CurrentSetup.boundpads[pad - 10000]" in pad
          and "p->stan != NULL ? p->stan->room : -1" in pad, "a pad by id, bound pads from 10000, its tile's room")


def mission(exe, temp):
    """mod.mission's difficulty, folder and day"""
    lua = 'mod.on("mission_start", function() print("mission", mod.mission.difficulty(), mod.mission.folder(), mod.mission.day()) end)'
    before = int(time.time() // 86400)
    out = run(exe, temp, "mission", lua, "start", "prepare", 9, "begin", 9)
    after = int(time.time() // 86400)
    got = logs(out)
    check(len(got) == 1 and got[0].split()[:3] == ["mission", "00agent", "4"]
          and before <= int(got[0].split()[3]) <= after, "difficulty's name, the folder from 1, today's number: " + str(got))
    lua = r"""
mod.on("mission_start", function()
  local bad = 0
  for _, a in ipairs({ { 0, "agent" }, { 21, "agent" }, { 1, "hard" }, { 1, "agent", -5 }, { 1.5, "agent" },
                       { 1, "agent", 99999999 } }) do
    if not pcall(mod.mission.complete, a[1], a[2], a[3]) then bad = bad + 1 end
  end
  print("complete", bad, mod.mission.complete(1, "agent"), mod.mission.complete(20, "007", 300))
end)
"""
    out = run(exe, temp, "complete", lua, "start", "prepare", 9, "begin", 9)
    check(logs(out) == ["complete 6 true true"] and answers(out, "COMPLETE") == ["COMPLETE 0 0 0", "COMPLETE 19 3 300"],
          "complete: 6 refused; Dam at Agent with no time, Egypt at 007 in 300 s: " + str(logs(out)) + str(answers(out, "COMPLETE")))
    out = run(exe, temp, "completebase", lua, "campaign", 0, "start", "prepare", 9, "begin", 9)
    check(logs(out) == ["complete 6 false false"], "outside a mod campaign: false: " + str(logs(out)))
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    done = function(game, "int gevrModGameComplete(int mission, int difficulty, int seconds)")
    check(done.index("gevrModActive() == NULL") < done.index("fileUnlockStageInFolderAtDifficulty(selected_folder_num"),
          "the save is written only in a mod campaign (its own file), through the game's own unlock")
    check("selected_difficulty <= DIFFICULTY_007" in function(game, "int gevrModGameDifficulty(void)")
          and "selected_folder_num < 4" in function(game, "int gevrModGameFolder(void)"),
          "the game's own chosen difficulty and folder, none outside their ranges")


def pads(exe, temp):
    """mod.pads' counts and reads; mod.guards.goTo and face"""
    lua = r"""
mod.on("mission_start", function()
  local n, bound = mod.pads.count()
  print("count", n, bound)
  local p = mod.pads.get(2)
  print("pad", p.x, p.y, p.z, p.room, p.look[3])
  local b = mod.pads.get(10000)
  print("bound", b.x, b.room, mod.pads.get(3), mod.pads.get(-1))
  print("goto", mod.guards.goTo(5000, 1), mod.guards.goTo(5000, 10000, "sprint"), mod.guards.goTo(5000, 7, "run"),
        mod.guards.goTo(99, 1))
  print("face", mod.guards.face(5000, 2), mod.guards.face(99, 2))
  print("badspeed", (pcall(mod.guards.goTo, 5000, 1, "crawl")))
  print("hat", mod.guards.giveHat(5000, "helmet"), mod.guards.giveHat(99, "peaked"),
        (pcall(mod.guards.giveHat, 5000, "crown")))
end)
"""
    out = run(exe, temp, "pads", lua, "addguard", 5000, 2, 47, 1, 8, -1, 75, 100, 0, 0, 0, "start", "prepare", 9,
              "begin", 9)
    check(logs(out) == ["count 3 1", "pad 200.0 0.0 -20.0 7 1.0", "bound 999.0 nil nil nil",
                        "goto true true false false", "face true false", "badspeed false", "hat true false false"],
          "pads read; goTo and face answered: " + str(logs(out)))
    check(answers(out, "GOTO", "FACE", "HAT") == ["GOTO 5000 1 0", "GOTO 5000 10000 2", "GOTO 5000 7 1", "GOTO 99 1 0",
                                                 "FACE 5000 2", "FACE 99 2", "HAT 5000 5", "HAT 99 11"],
          "the game side: walk, sprint, run; face: " + str(answers(out, "GOTO", "FACE")))
    out = run(exe, temp, "padsclient", lua, "addguard", 5000, 2, 47, 1, 8, -1, 75, 100, 0, 0, 0, "runs", 0,
              "start", "prepare", 9, "begin", 9)
    check("goto false false false false" in logs(out) and "face false false" in logs(out)
          and "hat false false false" in logs(out) and not answers(out, "GOTO", "FACE", "HAT"),
          "a co-op client sends no guard anywhere, and hats none: " + str(logs(out)))


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


def effects_placement():
    """the cartridge's own: sndPlaySfx placed as the game places one; explosionCreateSmoke where something is"""
    game = (ROOT / "src/game/gevr_modgame.c").read_bytes().decode()
    play = function(game, "int gevrModGamePlaySound(")
    check("id > gevrModGameSoundCount()" in play and play.index("sndPlaySfx(") < play.index("sub_GAME_7F053894(&at,")
          < play.index("gevrSndSpatialPlace(state, &at);"), "a sound: an id the bank has, placed as chrobjSndCreatePostEvent does")
    smoke = function(game, "int gevrModGameSmoke(")
    check("padOk(which)" in smoke and "guardById(which)" in smoke and smoke.index("if (stan == NULL) return 0;")
          < smoke.index("explosionCreateSmoke("), "smoke: at a pad the stage has, a guard there is, a tile to stand on")


def effects(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("mission_start", function()
  local a, e = mod.audio, mod.effect
  assert(a.count() == 120)
  assert(a.play(5) and a.play(120, 0.5) and a.playAt(7, 10, 20, 30) and a.playAt(8, 1, 2, 3, 0.25))
  for _, bad in ipairs({ { 0 }, { 121 }, { 1.5 }, { "x" }, { 5, 1.5 }, { 5, -0.1 }, { 5, 0 / 0 } }) do
    assert(not pcall(a.play, table.unpack(bad)), "a bad sound or volume")
  end
  assert(not pcall(a.playAt, 5, 1, 2) and not pcall(a.playAt, 5, 1, 2, 0 / 0) and not pcall(a.playAt, 5, 1, 2, 3, 2))
  assert(e.play("smoke", "player") and e.play("puff", 7) and e.play("smoke_huge", { pad = 12 }))
  assert(e.play("smoke_dark", 99) == false, "no such guard")
  for _, bad in ipairs({ { "fire", "player" }, { "smoke" }, { "smoke", "enemy" }, { "smoke", {} },
                         { "smoke", { pad = -1 } }, { "smoke", { pad = 1.5 } } }) do
    assert(not pcall(e.play, table.unpack(bad)), "a bad effect or place")
  end
  assert(e.blood({ headBurst = 10, bodies = { 4, 2 } }) and e.blood(false))
  assert(e.blood({}))
  assert(not pcall(e.blood, { headBurst = 101 }))
  assert(not pcall(e.blood, { headBurst = 0/0 }))
  assert(not pcall(e.blood, { bodies = { -1 } }))
  assert(not pcall(e.blood, { bodies = { 256 } }))
  assert(not pcall(e.blood, { bodies = { 2.5 } }))
  print("effects ok")
end)
"""
    out = run(exe, temp, "effects", lua, *play)
    check(logs(out) == ["effects ok"], "mod.audio and mod.effect: " + out[-600:])
    check(answers(out, "BLOOD") == ["BLOOD 10 2 4", "BLOOD 0"],
          "pack blood validates the head-burst odds and the eligible body rows")
    check(answers(out, "SOUND", "SMOKE") == ["SOUND 5 -1.00 -", "SOUND 120 0.50 -", "SOUND 7 -1.00 10 20 30",
                                             "SOUND 8 0.25 1 2 3", "SMOKE 1 1 0", "SMOKE 7 2 7", "SMOKE 6 3 12"],
          "the sounds and smokes as the game gets them (the sound's own volume unless given): "
          + str(answers(out, "SOUND", "SMOKE")))


def net_placement():
    """the sender by its peer; the relay never back to the sender; peer_ready at the first stage-ready"""
    core = (ROOT / "port/src/net/net_core.c").read_bytes().decode()
    recv = function(core, "static void netReceiveMod(")
    check(recv.index("s_client_peers[slot_id] != peer") < recv.index("netBroadcastPacket(data, size, NET_CHAN_RELIABLE,")
          and "ENET_PACKET_FLAG_RELIABLE, peer);" in recv and "peer != s_server_peer" in recv,
          "a player's message: checked by its peer, relayed to the others; a host's from its own peer alone")
    ready = function(core, "static void netReceiveStageReady(")
    check(ready.index("s_lobby_state.slots[slot].loaded = 1;") < ready.index("if (!was_ready) gevrModApiPeerReady(slot);"),
          "peer_ready as a player first reaches the stage")
    check("case NET_MSG_MOD: netReceiveMod(peer, slot_id, data, size, &buf); break;" in core, "NET_MSG_MOD dispatched")


def net(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.net.on("pos", function(data, from) print("pos", #data, data:byte(3), data:sub(4), from) end)
mod.net.on("pos", function(data, from) print("pos again", from) end)
mod.net.on("chat", function(data, from) print("chat", data, from) end)
mod.on("peer_ready", function(slot) print("ready", slot) end)
mod.on("mission_start", function()
  local n = mod.net
  print("role", n.role(), n.slot())
  assert(n.send("pos", "ab\0cd") and n.send("chat", "hi", 1))
  assert(n.send("chat", "me", 2) == false, "not to oneself")
  for _, bad in ipairs({ { "", "x" }, { "a b", "x" }, { string.rep("c", 33), "x" }, { "pos", string.rep("x", 1025) },
                         { "pos", "x", 8 }, { "pos", "x", -1 }, { "pos", "x", 1.5 }, { "pos" } }) do
    assert(not pcall(n.send, table.unpack(bad)), "refused")
  end
  assert(not pcall(n.on, "pos") and not pcall(n.on, "a b", print))
  local sent = 0
  for i = 1, 40 do if n.send("chat", "x") then sent = sent + 1 end end
  print("rate", sent)
end)
"""
    out = run(exe, temp, "net", lua, "online", 1, 0, 0, 2, "start", "prepare", 9, "netrecv", "tactical", 0, "pos", "xy\\0zz",
              "netrecv", "other", 0, "pos", "q", "peerready", 3, "begin", 9, "netrecv", "tactical", 3, "chat", "yo",
              "netrecv", "tactical", -1, "chat", "srv", "peerready", 1, "frame")
    check(logs(out) == ["role client 2", "rate 28", "ready 1", "ready 3", "pos 5 0 zz 0", "pos again 0", "chat yo 3",
                        "chat srv nil"],
          "mod.net: role, slot, the rate, held messages handed over at the next frame, by channel and mod; peer_ready "
          "held too, as a player who loads before the mission starts is told as it does: " + str(logs(out)))
    check(answers(out, "NETSEND") == ["NETSEND 0 255 tactical pos 5 61 62 00 63 64", "NETSEND 0 1 tactical chat 2 68 69"]
          + ["NETSEND 0 255 tactical chat 1 78"] * 28,
          "what went out, to everyone or one: " + str(answers(out, "NETSEND"))[:300])

    lua = 'mod.on("mission_start", function() print(mod.net.role(), mod.net.slot(), mod.net.send("x", "y")) end)'
    out = run(exe, temp, "netsolo", lua, *play)
    check(logs(out) == ["solo 0 false"] and not answers(out, "NETSEND"), "solo: no one else, nothing sent")
    out = run(exe, temp, "nethost", lua, "online", 1, 1, 0, 0, *play)
    check(logs(out) == ["host 0 true"], "the host: " + str(logs(out)))
    out = run(exe, temp, "netserver", lua, "online", 1, 1, 1, -1, *play)
    check(logs(out) == ["server nil true"], "a dedicated server: no slot of its own: " + str(logs(out)))

    # the last mission's held messages go as the next loads; a flood past the inbox is dropped
    lua = """
mod.net.on("c", function(data) print("got", data) end)
mod.on("peer_ready", function(slot) print("ready", slot) end)
"""
    out = run(exe, temp, "netclear", lua, "online", 1, 1, 0, 0, "start", "prepare", 9, "netrecv", "tactical", 1, "c", "old",
              "peerready", 2, "prepare", 10, "netrecv", "tactical", 1, "c", "new", "peerready", 1, "begin", 10, "frame")
    check(logs(out) == ["ready 1", "got new"], "a mission loading drops the last one's: " + str(logs(out)))
    flood = []
    for k in range(70):
        flood += ["netrecv", "tactical", 0, "c", "m%d" % k]
    lua = """
local got, first, last = 0
mod.net.on("c", function(data) got = got + 1; first = first or data; last = data end)
mod.on("tick", function() print(got, first, last) end)
"""
    out = run(exe, temp, "netflood", lua, "online", 1, 0, 0, 2, *play, *flood, "frame")
    check(logs(out) == ["64 m0 m63"] and "6 message(s) dropped" in out,
          "64 held, the rest dropped and logged: " + str(logs(out)))

    # a mod stopped here is told to the others; one stopped elsewhere stops here, untold
    lua = """
mod.net.on("boom", function() error("bad message") end)
mod.on("tick", function() print("tick") end)
"""
    out = run(exe, temp, "netstop", lua, "online", 1, 0, 0, 2, *play, "netrecv", "tactical", 0, "boom", "x", "frame", "frame")
    check(answers(out, "NETSEND") == ["NETSEND 1 255 tactical - 0"] and logs(out) == [],
          "an error in play: the mod off here and told to everyone: " + out[-500:])
    lua = 'mod.on("tick", function() print("tick") end)'
    out = run(exe, temp, "netremote", lua, "online", 1, 0, 0, 2, *play, "frame", "netoff", "tactical", 1, "frame",
              "netoff", "other", 1)
    check(logs(out) == ["tick"] and not answers(out, "NETSEND")
          and any("stopped on another player's headset" in l for l in out.splitlines()),
          "stopped on another headset: off here, and not told again: " + out[-500:])
    out = run(exe, temp, "netsolostop", 'mod.on("tick", function() error("x") end)', *play, "frame")
    check(not answers(out, "NETSEND"), "solo: a stop told to no one")


def ui_placement():
    """The panel takes a page's ask in both its input passes; both screens draw the pages; the clicks go at the frame"""
    inp = (ROOT / "port/src/input.c").read_bytes().decode()
    check(inp.count("panelWas = s_gevrPanel;\n            gevrPanelTakeAsk(t, paused);") == 2,
          "mod.ui.open: the flat game's and the headset's panel passes both take the ask")
    take = function(inp, "static void gevrPanelTakeAsk(u32 t, int paused)")
    check(take.index("if (gevrIntroPlaying()) return;") < take.index("s_gevrPanelAsk = 0;") < take.index("s_gevrStartUntil"),
          "a page asked for in the intro waits for play, still asked, before the pause's Start")
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    intro = function(view, "s32 gevrIntroPlaying(void)")
    check("if (netIsActive())" in intro and "getPlayerCount() == 1" in intro
          and all(m in intro for m in ("CAMERAMODE_INTRO", "CAMERAMODE_FADESWIRL", "CAMERAMODE_SWIRL", "CAMERAMODE_MP")),
          "the intro: its fly-by, fade and swirl (solo's NONE before it), multiplayer's swirl; online nothing waits")
    skip = view.index("(buttons & ~oldbuttons & (CONT_A | B_BUTTON | Z_TRIG | START_BUTTON | CONT_R | CONT_L))")
    check(view.rindex("g_CameraMode == CAMERAMODE_INTRO", 0, skip) > skip - 4000,
          "the reason: Start skips the intro (if this moves, recheck gevrIntroPlaying)")
    menu = (ROOT / "port/vr/vr_pause_menu.cpp").read_bytes().decode()
    check("gevrModUiAttach(v.settings);" in menu and "gevrModUiAttach(view.settings);" in menu,
          "the solo panel and the online Settings tab both draw the mods' pages")
    host = (ROOT / "port/src/gevr_modscript.c").read_bytes().decode()
    frame = function(host, "void gevrModScriptFrame(int ticks)")
    check(frame.index("gevrModApiUiFlush();") < frame.index('fire("tick"'), "the screen's clicks reach the mod before its tick")
    load = function(host, "void gevrModScriptStageLoad(")
    check(load.index("gevrModApiUiClear();") < load.index("if (!mission) return;"), "a mission loading drops the last one's clicks")


UI_PAGE = """
mod.ui.page("loadout", { title = "Loadout", text = "Pick a sidearm.",
  rows = {
    { kind = "choice", id = "gun", label = "Sidearm", choices = { "PP7", "DD44" }, value = 2, tip = "Your pistol." },
    { kind = "toggle", id = "sil", label = "Silencer", value = true },
    { kind = "slider", id = "mags", label = "Magazines", min = 1, max = 6, value = 3, blocked = "Not in a mission" },
    { kind = "slider", id = "cash", label = "Cash", min = -50, max = 50 },
  },
  buttons = { { id = "ready", label = "Ready" }, { id = "buy", label = "Buy", blocked = true } } })
mod.ui.page("shop", { title = "Workshop" })
"""


def scope_placement():
    """The per-hand scope asks for the hand's (a mod's or the game's) everywhere it looked up the item's"""
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    check(view.count("gevrScopeFor(") == 2 and "return gevrScopeFor(item);\n}" in function(view, "static const struct GevrScope *gevrScopeForHand("),
          "the item's own table is read only through gevrScopeForHand")
    for name in ("s32 gevrScopeFitIndex(void)", "f32 gevrScopeMagnificationHand(s32 hand)", "s32 gevrGripSteadyOnCtrl(s32 ctrl)",
                 "static s32 gevrScopeBeginHand(s32 hand)"):
        check("gevrScopeForHand(" in function(view, name), name + ": the hand's scope")
    check(view.count("gevrScopeZoom(item)") == 1 and "gevrScopeZoom(item)" in function(view, "static f32 gevrScopeZoomHand("),
          "the zoom: a mod's own, else the item's")
    lens = function(view, "static void gevrScopeLensPlace(")
    check("gevrScopeInTable(sc) ? gevrScopeFitOf(" in lens and "gexDef->hasScope && gevrScopeInTable(sc)" in lens,
          "a mod's lens: no Gun fit index of the table's, and its own eyepiece over GoldenEye X's")
    check("sc != NULL && gevrScopeInTable(sc)" in function(view, "s32 gevrScopeFitIndex(void)"), "Gun fit fits the game's own only")
    api = (ROOT / "port/src/gevr_modapi.c").read_bytes().decode()
    drawn = function(api, "void gevrModApiDrawn(int hand, int item)")
    check(drawn.index("gevrModGameScope(hand, GEVR_MODSCOPE_GAME") < drawn.index('"weapon_drawn"'),
          "a draw gives the game's scope back before weapon_drawn")
    check("gevrModGameScope(0, GEVR_MODSCOPE_GAME" in function(api, "void gevrModApiReset(void)"), "a reset gives them back")


def scope(exe, temp):
    """mod.weapon.scope: a lens, none, the game's own; its limits; a draw gives the game's back; look"""
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("mission_start", function()
  local bad = 0
  local function try(t) if not pcall(mod.weapon.scope, "right", t) then bad = bad + 1 end end
  try({ radius = 16, zoom = 4 })
  try({ at = { 0, 80 }, radius = 16, zoom = 4 })
  try({ at = { 0, 80, 4001 }, radius = 16, zoom = 4 })
  try({ at = { 0, 80, "x" }, radius = 16, zoom = 4 })
  try({ at = { 0, 80, -44 }, radius = 0.5, zoom = 4 })
  try({ at = { 0, 80, -44 }, radius = 401, zoom = 4 })
  try({ at = { 0, 80, -44 }, radius = 16, zoom = 0.9 })
  try({ at = { 0, 80, -44 }, radius = 16, zoom = 26 })
  try({ at = { 0, 80, -44 }, radius = 16 })
  try(true)
  try(3)
  print("bad", bad)
  print("empty", mod.weapon.scope("left", { at = { 0, 0, 0 }, radius = 10, zoom = 2 }))
  print("back", mod.weapon.scope("right"))
end)
mod.on("weapon_drawn", function(hand, item)
  print("drawn", hand, item, mod.weapon.look(hand))
  if item == 8 then
    print("set", mod.weapon.scope(hand, { at = { 0, 80, -44 }, radius = 16, zoom = 4 }))
  elseif item == 4 then
    print("none", mod.weapon.scope(hand, false))
  end
end)
"""
    out = run(exe, temp, "scope", lua, *play, "look", 1, 2, "draw", 0, 8, "draw", 1, 4, "draw", 0, 5,
              "local", 0, "draw", 0, 8)
    check(logs(out) == ["bad 11", "empty false", "back true", "drawn right 8 original", "set true", "drawn left 4 bean",
                        "none true", "drawn right 5 original"],
          "scope's answers, refusals and look: " + str(logs(out)))
    scopes = answers(out, "SCOPE")
    check(scopes[-6:] == ["SCOPE 0 0 4", "SCOPE 0 0 8", "SCOPE 0 1 8 0.0 80.0 -44.0 16.0 16.4", "SCOPE 1 0 4",
                          "SCOPE 1 2 4", "SCOPE 0 0 5"],
          "nil the game's own; each draw the game's back, then the mod's (4x is 16.4 degrees) or none; a copy's draw "
          "touches nothing: " + str(scopes))
    check(scopes[:2] == ["SCOPE 0 0 0", "SCOPE 1 0 0"], "a mission's reset gives both hands the game's own: " + str(scopes))


def gunmodel_placement():
    """A hand's gun loads the mod's file in GoldenEye X's place; ob.c names it; the fits are the original's"""
    gun = (ROOT / "src/game/gun.c").read_bytes().decode()
    load = function(gun, "void used_to_load_1st_person_model_on_demand(GUNHAND hand)")
    check(load.index("gevrModGunReconcile(hand);") < load.index("item             = g_CurrentPlayer->field_2A44[hand];"),
          "each tick a hand showing another model than its choice loads again, before the load")
    regular = load[load.rindex("texInitPool(", 0, load.index("gevrModTried = gevrModGunModelFor(hand, item);")):]
    check(regular.index("gevrModAssetPendingName = packModel == 0 ? gevrModTried : NULL;") < regular.index("if (gevrModTried == NULL)")
          < regular.index("gevrGexGunPrepare(") < regular.index("load_object_fill_header(")
          < regular.index("gevrModUsed = gevrMesh != NULL || (gevrModTried != NULL && gevrModAssetPendingUsed);")
          < regular.index("gevrModAssetPendingName = NULL;"),
          "the regular gun's load: the mod's file named around it, GoldenEye X's only without one")
    check(load.count("gevrModAssetPendingName = packModel == 0 ? gevrModTried : NULL;") == 1, "only the regular gun's load (not the watch's or the suit's)")
    tail = load[load.index("g_CurrentPlayer->hand_item[hand]      = item;"):]
    check("s_gevrModGunShown[p][hand].owner = g_CurrentPlayer;" in tail and "gevrModTried != NULL ? gevrModTried" in tail,
          "what the hand shows: the name tried, so a broken file isn't tried each tick")
    reconcile = function(gun, "static void gevrModGunReconcile(s32 hand)")
    check("hand_invisible[hand] < 0" in reconcile and "lock_hand_model[hand]" in reconcile
          and "hand_invisible[hand] = -3;" in reconcile, "a reload only of a hand held as it is, as the club's replacement")
    for name in ("void gevrModGunModelSet(", "void gevrModGunModelReset(void)"):
        check("g_CurrentPlayer" not in function(gun, name), name + " is only the table: no player that may be gone")
    check("(netIsActive() && get_cur_playernum() != netGetLocalSlot())" in function(gun, "static const char *gevrModGunModelFor("),
          "the local human's hands only")
    ob = (ROOT / "src/game/ob.c").read_bytes().decode()
    check("gevrModAssetPendingName != NULL ? gevrModAssetPendingName : srcfile->filename" in ob
          and "gevrModAssetPendingUsed = converted != 0 && gevrModAssetPendingName != NULL;" in ob,
          "load_resource takes the hand's file's name, and says whether it loaded")
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    check("gevrBeanIsEnabled() && !gevrModGunHeld(hand) ? 2 : 0" in function(view, "int gevrFitLook(int hand)"),
          "a mod's model takes the original models' fits")
    api = (ROOT / "port/src/gevr_modapi.c").read_bytes().decode()
    check("gevrModGameGunModelReset();" in function(api, "void gevrModApiReset(void)"), "a reset gives the hands' own back")


def gunmodel(exe, temp):
    """mod.weapon.model: set, missing, cleared, refused; reset; look's "mod\""""
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("mission_start", function()
  local bad = 0
  local function try(...) if not pcall(mod.weapon.model, ...) then bad = bad + 1 end end
  try("right", "ak47", "")
  try("right", "ak47", "a/b")
  try("right", "ak47", string.rep("a", 64))
  try("right", "fist", "GtacZ")
  try("right", "watchlaser", "GtacZ")
  try("middle", "ak47", "GtacZ")
  try("right", "nothing", "GtacZ")
  print("bad", bad)
  print("set", mod.weapon.model("right", "ak47", "GtacAk74Z"))
  print("missing", mod.weapon.model("left", 8, "GnoneZ"))
  print("clear", mod.weapon.model("right", "ak47"))
  print("look", mod.weapon.look("left"))
end)
"""
    out = run(exe, temp, "gunmodel", lua, "start", "prepare", 9, "look", 1, 3, "begin", 9)
    check(logs(out) == ["bad 7", "set true", "missing false", "clear true", "look mod"],
          "model's answers and refusals; look's mod: " + str(logs(out)))
    check(answers(out, "GUNMODEL") == ["GUNMODEL reset", "GUNMODEL 0 8 GtacAk74Z", "GUNMODEL 1 8 GnoneZ", "GUNMODEL 0 8 -"],
          "the game side: a mission's reset, then each call: " + str(answers(out, "GUNMODEL")))

def gunmodel_fit(exe, temp):
    """A fitted body keeps the same affine rows as an attachment and requires a mesh."""
    lua = r"""
mod.on("mission_start", function()
  local frame = {{-1,0,0},{0,0,1},{0,1,0},{4,12,-8}}
  print("fit", mod.weapon.model("right", "ak47", "pack-body.bin", {frame=frame, muzzle={0, 3, 350}}))
  print("muzzle only", mod.weapon.model("left", "ak47", "pack-body.bin", {muzzle={4, 5, 6}}))
  print("support only", mod.weapon.model("left", "ak47", "pack-body.bin", {support={4, 5, 60}}))
  print("bad support", (pcall(mod.weapon.model, "right", "ak47", "pack-body.bin", {support={0, 0, 4001}})))
  print("bad muzzle", (pcall(mod.weapon.model, "right", "ak47", "pack-body.bin", {muzzle={0, 0, 4001}})))
  print("nan muzzle", (pcall(mod.weapon.model, "right", "ak47", "pack-body.bin", {muzzle={0/0, 0, 1}})))
  print("short muzzle", (pcall(mod.weapon.model, "right", "ak47", "pack-body.bin", {muzzle={0, 1}})))
  print("thrown muzzle", mod.weapon.model("right", "grenade", "pack-body.bin", {muzzle={0, 0, 100}}))
  print("legacy", mod.weapon.model("left", "ak47", "GtacAk74Z", {frame=frame}))
  print("clear with fit", (pcall(mod.weapon.model, "right", "ak47", nil, {frame=frame})))
  print("wrong shape", (pcall(mod.weapon.model, "right", "ak47", "pack-body.bin", {frame={{0,0,0}}})))
  print("unfitted", mod.weapon.model("right", "ak47", "pack-body.bin"))
end)
"""
    out = run(exe, temp, "gunmodel_fit", lua, "start", "prepare", 9, "begin", 9)
    check(logs(out) == ["fit true", "muzzle only true", "support only true", "bad support false", "bad muzzle false", "nan muzzle false", "short muzzle false",
                        "thrown muzzle false", "legacy false", "clear with fit false", "wrong shape false", "unfitted true"],
          "fitted gun model results: " + str(logs(out)))
    check(answers(out, "GUNFRAME") == ["GUNFRAME 0 -1.0 1.0 1.0 12.0 1.0", "GUNFRAME 1 -1.0 1.0 1.0 12.0 1.0"],
          "only well-formed fit requests reach the game with the authored frame: " + str(answers(out, "GUNFRAME")))
    check(answers(out, "GUNMUZZLE") == ["GUNMUZZLE 0 0.0 3.0 350.0", "GUNMUZZLE 1 4.0 5.0 6.0"],
          "only valid model-local muzzle points for a gun reach the game")
    check(answers(out, "GUNSUPPORT") == ["GUNSUPPORT 1 4.0 5.0 60.0"],
          "only valid model-local support points reach the game")


def pack_names(exe, temp):
    """The Lua binding forwards flat pack filenames and an omitted legacy header."""
    lua = r"""
mod.on("mission_start", function()
  print("held", mod.weapon.model("right", "ak47", "pack-body.bin"))
  print("attached", mod.attach.add({ model = "pack-part.bin", on = "gun", hand = "right" }))
  print("legacy", mod.attach.add({ model = "legacy.bin", on = "gun", hand = "right" }))
  print("header", mod.attach.add({ model = "legacy.bin", like = "PcartonZ", on = "gun", hand = "right" }))
  local ok = pcall(mod.attach.add, { model = "part/pack-part.bin", on = "gun", hand = "right" })
  print("nested", ok)
end)
"""
    out = run(exe, temp, "packnames", lua, "start", "prepare", 9, "begin", 9)
    check(logs(out) == ["held true", "attached 1", "legacy nil", "header 2", "nested false"],
          "flat .bin and hyphenated names, with and without a cartridge header: " + str(logs(out)))


def attach_placement():
    """Each attachment drawn at its anchor as drawn: the gun's root after the gun, the hands and body with the body"""
    gun = (ROOT / "src/game/gunfire.c").read_bytes().decode()
    draw = gun.index("subdraw(&renderdata, &handptr->weaponModel);")
    att = gun.index("renderdata.gdl = gevrModAttachDrawGun(renderdata.gdl, &renderdata, handnum, &handptr->weaponModel);")
    check(draw < gun.index("renderdata.gdl = gevrBeanMagDraw(renderdata.gdl, &renderdata, handnum);") < att
          < gun.index("gdl = renderdata.gdl;", att) < gun.index("VR_CULL_MIRROR_END", att),
          "the gun's after the gun and its own extras, before the mirrored left gun's culling ends")
    body = (ROOT / "src/game/gevr_bodyslots.c").read_bytes().decode()
    slots = function(body, "Gfx *gevrBodySlotsDraw(Gfx *gdl)")
    check(slots.index("gdl = gevrModAttachDrawBody(gdl);") < slots.index("if (!s_bodyLive || !s_bodyFrameValid || !VrBodySlotShow"),
          "the hands' and the body's before the body slots' own early return (with body slots off too)")
    frame = function(body, "s32 gevrBodySlotFrame(s32 slot, Mtxf *root)")
    view = (ROOT / "src/game/bondview2.c").read_bytes().decode()
    hand = function(view, "s32 gevrModAttachHandFrame(s32 hand, Mtxf *root)")
    for name, text in (("the body's", frame), ("a hand's", hand)):
        check("k = 0.085f * D_800364CC * gevrGunSizeNow(&setting, &online);" in text,
              name + " frame at a gun model's units and the player's gun size")
    check("!s_bodyFrameValid" in frame and "gevrGripAxesRaw(hand == GUNRIGHT ? 1 : 0" in hand,
          "frames only while kept: the torso's, the controller's raw grip")
    mine = (ROOT / "src/game/gevr_modattach.c").read_bytes().decode()
    load = function(mine, "static AttachFile *attachLoad(const char *model, const char *like)")
    check(load.index("gevrModAssetPendingName = model;") < load.index("load_object_fill_header(")
          < load.index("used = gevrModAssetPendingUsed;") < load.index("gevrModAssetPendingName = NULL;")
          < load.index("if (!used)"), "a model loads under its own name, and the cartridge's in its place is refused")
    one = function(mine, "static Gfx *attachDraw(")
    check(one.index("matrix_4x4_multiply(anchor, &a->offset, &pose);") < one.index("mtx[i] = pose;")
          < one.index("if (a->file->rigid)") < one.index("instcalcmatrices(&rd, &a->model);")
          < one.index("subdraw(&rd, &a->model);"),
          "laid in the anchor's frame: a prop file's nodes from there, an animated one's every matrix the pose")
    check("(node->Opcode & 0xff) == MODELNODE_OPCODE_GROUP" in function(mine, "static s32 attachHasJoints(")
          and "f->rigid = attachHasJoints(f->header.RootNode);" in load,
          "a model with joints only an animation poses is found as it loads")
    drawgun = function(mine, "Gfx *gevrModAttachDrawGun(")
    check("node = ((Mtxf *) gun->render_pos)[a->node];" in drawgun and "a->node < gun->obj->numMatrices" in drawgun
          and "attachLocal()" in drawgun, "the gun model's matrix it rides as drawn, none past the model's; local")
    drawbody = function(mine, "Gfx *gevrModAttachDrawBody(Gfx *gdl)")
    check("g_gevrExtraPass" in drawbody and "0x565F0000 | (u32) (a->on == GEVR_MODATTACH_HAND ? (a->where == GUNRIGHT ? 2 : 1) : 3)" in drawbody
          and drawbody.count("matrix_4x4_7F058C64();") == 1,
          "a hand's follows its controller between frames, the body's the body; bracketed as the body slots' guns")
    check("g_CurrentPlayer" not in function(mine, "void gevrModGameAttachReset(void)"), "the reset is only tables")
    api = (ROOT / "port/src/gevr_modapi.c").read_bytes().decode()
    check("gevrModGameAttachReset();" in function(api, "void gevrModApiReset(void)"), "a reset detaches everything")


def attach_loadout(exe, temp):
    """mod.attach at a measured loadout's size (#291) with no pack's files: two rifles of 14 parts (17 files
    between them), five magazines, a sheath and a scabbard; twice over for a second rig; then the limits"""
    lua = r"""
mod.on("mission_start", function()
  local function rifle(on, hand, finish, extra)
    local parts = { "PlBaseZ", finish }
    for i = 1, 9 do parts[#parts + 1] = "PlPart" .. i .. "Z" end
    for _, p in ipairs(extra) do parts[#parts + 1] = p end
    local got = 0
    for _, p in ipairs(parts) do
      if mod.attach.add({ model = p, like = "PcartonZ", on = on, hand = hand }) then got = got + 1 end
    end
    return got
  end
  local function rig()
    local got = rifle("gun", "right", "PlFinishAZ", { "PlRailZ", "PlSightZ", "PlMagZ" })
      + rifle("back_gun", nil, "PlFinishBZ", { "PlCanZ", "PlSightBZ", "PlMagZ" })
    for _, on in ipairs({ "chest", "chest", "belt", "hip_off", "hip_off" }) do
      if mod.attach.add({ model = on == "hip_off" and "PlPistolMagZ" or "PlMagZ", like = "PcartonZ", on = on }) then
        got = got + 1
      end
    end
    for _, m in ipairs({ "PlSheathZ", "PlScabbardZ" }) do
      if mod.attach.add({ model = m, like = "PcartonZ", on = "back_off" }) then got = got + 1 end
    end
    return got
  end
  print("rig", rig(), rig())
  local more = 0
  for i = 1, 60 do
    if mod.attach.add({ model = "PlMagZ", like = "PcartonZ", on = "belt" }) then more = more + 1 end
  end
  print("full", more, mod.attach.add({ model = "PlMagZ", like = "PcartonZ", on = "belt" }))
end)
"""
    out = run(exe, temp, "attachloadout", lua, "start", "prepare", 9, "begin", 9)
    check(logs(out) == ["rig 35 35", "full 58 nil"],
          "a rig of 35 twice (70 attached, 20 files), then up to 128 attached and none past: " + str(logs(out)))
    lua = r"""
mod.on("mission_start", function()
  local files = 0
  for i = 1, 64 do
    if mod.attach.add({ model = "PlFile" .. i .. "Z", like = "PcartonZ", on = "belt" }) then files = files + 1 end
  end
  print("files", files, mod.attach.add({ model = "PlFile65Z", like = "PcartonZ", on = "belt" }),
        mod.attach.add({ model = "PlFile1Z", like = "PcartonZ", on = "chest" }))
end)
"""
    out = run(exe, temp, "attachfiles", lua, "start", "prepare", 9, "begin", 9)
    check(logs(out) == ["files 64 nil 65"], "64 model files a mission, none past; a loaded one again: " + str(logs(out)))
    mine = (ROOT / "src/game/gevr_modattach.c").read_bytes().decode()
    one = function(mine, "static Gfx *attachDraw(")
    check(one.index("if (!gevrDynRoom(gdl, a->file->nodes * 8 + 64, h->numMatrices * (s32) sizeof(Mtxf) + 0x400))")
          < one.index("mtx = dynAllocate(h->numMatrices * sizeof(Mtxf));") and "return gdl;" in one,
          "an attachment without room left this frame is skipped before anything's allocated")
    dyn = (ROOT / "src/game/dyn.c").read_bytes().decode()
    room = function(dyn, "s32 gevrDynRoom(Gfx *gdl, s32 gfx, s32 bytes)")
    check("(end - pos) / sizeof(Gfx) < (uintptr_t)gfx" in room and "vtxEnd - vtx >= (uintptr_t)bytes" in room,
          "the room: the master list's commands after gdl and dynAllocate's bytes")


def attach_authored_frame(exe, temp):
    """An authored mirrored frame survives both placement calls with the node intact."""
    lua = r"""
mod.on("mission_start", function()
  local frame = {{0, 0, 2}, {0, 1, 0}, {1, 0, 0}, {9, 11, 13}}
  local id = assert(mod.attach.add{on="gun", hand="right", node=7, model="pack-part.bin", frame=frame})
  frame[4] = {-2, -3, -5}
  assert(mod.attach.place(id, {frame=frame}))
  local invalid = {
    {frame=frame, at={0, 0, 0}}, {frame=frame, turn={0, 0, 0}}, {frame=frame, scale=1},
    {frame={{1,0,0},{1,0,0},{0,0,1},{0,0,0}}},
    {frame={{1,0,0},{0,1,0},{0,0,1},{0,0,4001}}},
    {frame={{0/0,0,0},{0,1,0},{0,0,1},{0,0,0}}},
    {frame={{1,0,0},{0,1},{0,0,1},{0,0,0}}},
  }
  for _, placement in ipairs(invalid) do assert(not pcall(mod.attach.place, id, placement)) end
  mod.log("authored frame accepted; invalid frames rejected")
end)
"""
    out = run(exe, temp, "attach_authored_frame", lua, "start", "prepare", 9, "begin", 9)
    check("authored frame accepted; invalid frames rejected" in logs(out), "authored frame validation: " + out)
    check(answers(out, "ATTACH 1", "ATTACHPLACE") == [
          "ATTACH 1 0 0 7 pack-part.bin  |0 0 2 0 |0 1 0 0 |1 0 0 0 |9 11 13 1",
          "ATTACHPLACE 1 |0 0 2 0 |0 1 0 0 |1 0 0 0 |-2 -3 -5 1"],
          "authored and moved frames reach the game unchanged: " + str(answers(out, "ATTACH")))


def attach(exe, temp):
    """mod.attach: add on the gun, a hand and the body; the offset; refusals; show and remove, the owner's only"""
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("mission_start", function()
  local bad = 0
  local function try(t) if not pcall(mod.attach.add, t) then bad = bad + 1 end end
  try({ like = "Gak47Z", on = "gun", hand = "right" })
  try({ model = "GtacAcogZ", like = false, on = "gun", hand = "right" })
  try({ model = "a/b", like = "Gak47Z", on = "gun", hand = "right" })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "elbow" })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "gun" })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "hand", hand = "middle" })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "chest", at = { 1, 2 } })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "chest", at = { 1, 2, 4001 } })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "chest", turn = { 0, 0, 361 } })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "chest", scale = 0 })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "chest", scale = 101 })
  try("x")
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "hip_gun", node = 1 })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "gun", hand = "right", node = 256 })
  try({ model = "GtacAcogZ", like = "Gak47Z", on = "gun", hand = "right", node = 1.5 })
  print("bad", bad)
  local optic = mod.attach.add({ model = "GtacAcogZ", like = "Gak47Z", on = "gun", hand = "right", at = { 0, 90, -20 } })
  local mag = mod.attach.add({ model = "GtacMagZ", like = "PcartonZ", on = "hand", hand = "left", turn = { 0, 90, 0 }, scale = 2 })
  local sheath = mod.attach.add({ model = "GtacSheathZ", like = "GknifeZ", on = "hip_off", turn = { 90, 0, 0 } })
  print("ids", optic, mag, sheath)
  print("missing", mod.attach.add({ model = "GnoneZ", like = "Gak47Z", on = "belt" }))
  print("slide", mod.attach.add({ model = "GtacSlideZ", like = "PcartonZ", on = "gun", hand = "left", node = 3 }))
  print("place", mod.attach.place(optic, { at = { 0, 0, 10 } }), mod.attach.place(99, {}))
  print("badplace", (pcall(mod.attach.place, optic, { turn = { 0, 0, 400 } })))
  print("show", mod.attach.show(mag, false), mod.attach.show(99, true))
  print("remove", mod.attach.remove(sheath), mod.attach.remove(sheath))
end)
"""
    out = run(exe, temp, "attach", lua, *play)
    check(logs(out) == ["bad 15", "ids 1 2 3", "missing nil", "slide 4", "place true false", "badplace false",
                        "show true false", "remove true false"],
          "attach's answers and refusals: " + str(logs(out)))
    check(answers(out, "ATTACH", "DETACH") == [
        "ATTACH reset",
        "ATTACH 1 0 0 0 GtacAcogZ Gak47Z |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 90 -20 1",
        "ATTACH 2 1 1 0 GtacMagZ PcartonZ |0 0 -2 0 |0 2 0 0 |2 0 0 0 |0 0 0 1",
        "ATTACH 3 2 1 0 GtacSheathZ GknifeZ |1 0 0 0 |0 0 1 0 |0 -1 0 0 |0 0 0 1",
        "ATTACH 0 2 5 0 GnoneZ Gak47Z |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 0 0 1",
        "ATTACH 4 0 1 3 GtacSlideZ PcartonZ |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 0 0 1",
        "ATTACHPLACE 1 |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 0 10 1",
        "ATTACHSHOW 2 0",
        "DETACH 3"],
        "the game side: on the gun, the left hand, the off hip, a gun model's node; turned about Y (X to -Z) and X "
        "(Y to Z), scaled; a model that won't load; placed again; show and remove once: "
        + str(answers(out, "ATTACH", "DETACH")))

    # on a guard's body part
    lua = r"""
mod.on("mission_start", function()
  local bad = 0
  local function try(t) if not pcall(mod.attach.add, t) then bad = bad + 1 end end
  try({ model = "PvestZ", like = "PcartonZ", on = "guard", part = "chest" })
  try({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = 5000 })
  try({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = 5000, part = "tail" })
  try({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = -1, part = "chest" })
  try({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = 1.5, part = "chest" })
  try({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = 5000, part = "chest", node = 2 })
  print("bad", bad)
  local vest = mod.attach.add({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = 5000, part = "chest",
                                at = { 0, 0, 30 } })
  local pouch = mod.attach.add({ model = "PpouchZ", like = "PcartonZ", on = "guard", guard = 5000, part = "left_foot" })
  print("guard", vest, pouch, mod.attach.add({ model = "PvestZ", like = "PcartonZ", on = "guard", guard = 99,
                                                part = "head" }))
end)
mod.on("tick", function() print("tick", mod.attach.show(1, false), mod.attach.remove(2)) end)
"""
    out = run(exe, temp, "attachguard", lua, *play, "attachgone", 1, "frame")
    check(logs(out) == ["bad 6", "guard 1 2 nil", "tick false true"],
          "a guard's part: refusals, ids, none for a guard that isn't there; one gone with its body is no "
          "mod's: " + str(logs(out)))
    check(answers(out, "ATTACH")[1:4] == ["ATTACH 1 3 5000 15 PvestZ PcartonZ |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 0 30 1",
                                         "ATTACH 2 3 5000 1 PpouchZ PcartonZ |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 0 0 1",
                                         "ATTACH 0 3 99 8 PvestZ PcartonZ |1 0 0 0 |0 1 0 0 |0 0 1 0 |0 0 0 1"],
          "the game side: on guard 5000's chest (15) and left foot (1), the game's hit parts: "
          + str(answers(out, "ATTACH")))
    mine = (ROOT / "src/game/gevr_modattach.c").read_bytes().decode()
    part = function(mine, "static s32 attachPartMtx(ModelFileHeader *h, s32 part)")
    check("MODELNODE_OPCODE_BBOX" in part and "BoundingBox.ModelNumber == part" in part
          and "modelFindNodeMtxIndex(node, 0)" in part, "a part's matrix: its bounding box's node, as hits name it")
    guard = function(mine, "Gfx *gevrModAttachDrawGuard(")
    check("a->chr != chr" in guard and "node = ((Mtxf *) chr->model->render_pos)[a->partMtx];" in guard
          and "objdata.PropType = fadealpha < 0xFF ? 5 : 9;" in guard and "chr->prop->type != PROP_TYPE_CHR" in guard,
          "drawn on that guard's record at its part's matrix as drawn, as stuck objects are; never a player's body")
    gone = function(mine, "void gevrModAttachChrGone(ChrRecord *chr)")
    check("gevrModApiAttachGone(s_attach[i].id);" in gone, "gone with the body, and no mod's")
    chr = (ROOT / "src/game/chr.c").read_bytes().decode()
    render = function(chr, "Gfx *chrRenderProp(PropRecord *prop, Gfx *gdl, s32 withalpha)")
    check(render.index("drawjointlist(&mrData, chr->field_20);")
          < render.index("mrData.gdl = gevrChrEmbeddedRender(chr, &mrData, chrfadealpha, withalpha);")
          < render.index("mrData.gdl = gevrModAttachDrawGuard(chr, &mrData, chrfadealpha, mrData.gdl);")
          < render.index("bondviewTransformManyPosToViewMatrix(chr->model->render_pos, chr->model->obj->numMatrices);"),
          "after the body and what's stuck to it, while its matrices are still floats")
    cleanup = function(chr, "void chrpropCleanupForRemoval(PropRecord *prop)")
    check(cleanup.index("gevrModAttachChrGone(chr);") < cleanup.index("chr->model = NULL;"),
          "a body's removal takes its attachments")

    # another mod's attachment isn't this one's to hide or remove
    lua = r"""
mod.on("mission_start", function()
  print("other", mod.attach.show(1, false), mod.attach.remove(1), mod.attach.place(1, {}))
end)
"""
    out = run(exe, temp, "attachother", lua, *play)
    check(logs(out) == ["other false false false"] and not answers(out, "DETACH", "ATTACHSHOW", "ATTACHPLACE"),
          "an id this mod didn't attach: false, nothing touched")


def ui(exe, temp):
    play = ("start", "prepare", 9, "begin", 9)
    lua = r"""
mod.on("mission_start", function()
""" + UI_PAGE + r"""
end)
mod.on("ui_click", function(page, b) print("click", page, b) end)
mod.on("ui_change", function(page, row, v) print("change", page, row, v, math.type(v)) end)
"""
    out = run(exe, temp, "ui", lua, "start", "prepare", 9, "ui", "begin", 9, "ui", "uiset", 0, 0, 0, "uiset", 0, 1, 0,
              "uiset", 0, 2, 5, "uiclick", 0, 0, "uiclick", 0, 1, "uiset", 0, 1, 0, "uiset", 0, 3, 7, "uiset", 0, 3, 20,
              "uiset", 0, 3, 99, "uiset", 0, 0, 5, "uiclick", 1, 0, "frame", "ui")
    pages = answers(out, "UIPAGE", "UIROW", "UIBUTTON", "UIASKED")
    shown = ["UIPAGE 0 tactical loadout Loadout|Pick a sidearm.",
             "UIROW 0 0 2 gun Sidearm|1|0|0|PP7,DD44||Your pistol.",
             "UIROW 0 1 1 sil Silencer|1|0|0|||",
             "UIROW 0 2 3 mags Magazines|3|1|6||Not in a mission|",
             "UIROW 0 3 3 cash Cash|-50|-50|50|||",
             "UIBUTTON 0 0 ready Ready|",
             "UIBUTTON 0 1 buy Buy|Not now",
             "UIPAGE 1 tactical shop Workshop|",
             "UIASKED -1 of 2"]
    check(pages[:10] == ["UIASKED -1 of 0"] + shown, "the pages as the screen gets them: " + str(pages[:10]))
    check(pages[10:] == [l.replace("Sidearm|1|", "Sidearm|0|").replace("Silencer|1|", "Silencer|0|")
                         .replace("Cash|-50|", "Cash|50|") for l in shown],
          "the screen's changes kept at once (a blocked row's not; a slider held to its ends): " + str(pages[10:]))
    check(logs(out) == ["change loadout gun 1 integer", "change loadout sil false nil", "click loadout ready",
                        "change loadout cash 50 integer"],
          "ui_change and ui_click at the next frame: a choice's number from 1, a toggle's boolean, a slider's last "
          "value once; a blocked button and an unchanged value send nothing: " + str(logs(out)))

    # the limits: each a script error, the page left as it was
    lua = r"""
local bad = 0
local function try(id, t) if not pcall(mod.ui.page, id, t) then bad = bad + 1 end end
local function row(r) return { title = "T", rows = { r } } end
mod.on("mission_start", function()
  try("a b", { title = "T" }); try("", { title = "T" }); try(string.rep("x", 33), { title = "T" })
  try("p", {}); try("p", { title = string.rep("t", 13) }); try("p", { title = "T", text = string.rep("t", 161) })
  try("p", { title = "T", text = "tab\there" })
  local nine = {}
  for i = 1, 9 do nine[i] = { kind = "toggle", id = "r" .. i, label = "R" } end
  try("p", { title = "T", rows = nine })
  try("p", row({ kind = "bogus", id = "r", label = "R" })); try("p", row({ kind = "toggle", label = "R" }))
  try("p", row({ kind = "toggle", id = "r", label = string.rep("l", 25) }))
  try("p", { title = "T", rows = { { kind = "toggle", id = "r", label = "R" }, { kind = "toggle", id = "r", label = "S" } } })
  try("p", row({ kind = "toggle", id = "r", label = "R", value = "yes" }))
  try("p", row({ kind = "choice", id = "r", label = "R", choices = { "one" } }))
  local many = {}
  for i = 1, 13 do many[i] = "c" .. i end
  try("p", row({ kind = "choice", id = "r", label = "R", choices = many }))
  try("p", row({ kind = "choice", id = "r", label = "R", choices = { "a", string.rep("c", 25) } }))
  try("p", row({ kind = "choice", id = "r", label = "R", choices = { "a", "b" }, value = 3 }))
  try("p", row({ kind = "slider", id = "r", label = "R", min = 1 }))
  try("p", row({ kind = "slider", id = "r", label = "R", min = 5, max = 5 }))
  try("p", row({ kind = "slider", id = "r", label = "R", min = 0, max = 1001 }))
  try("p", row({ kind = "slider", id = "r", label = "R", min = -100001, max = 0 }))
  try("p", row({ kind = "slider", id = "r", label = "R", min = 0, max = 10, value = 11 }))
  try("p", row({ kind = "slider", id = "r", label = "R", min = 0, max = 10, value = 1.5 }))
  try("p", row({ kind = "toggle", id = "r", label = "R", blocked = string.rep("b", 33) }))
  try("p", { title = "T", buttons = { { id = "a", label = "A" }, { id = "b", label = "B" }, { id = "c", label = "C" },
                                     { id = "d", label = "D" }, { id = "e", label = "E" } } })
  try("p", { title = "T", buttons = { { id = "a", label = string.rep("b", 15) } } })
  try("p", { title = "T", buttons = { { id = "a", label = "A" }, { id = "a", label = "B" } } })
  print("bad", bad)
  local made = 0
  for i = 1, 4 do if mod.ui.page("p" .. i, { title = "P" .. i }) == true then made = made + 1 end end
  print("fifth", made, mod.ui.page("p5", { title = "P5" }))
  print("again", mod.ui.page("p2", { title = "Again" }))
  print("remove", mod.ui.remove("p1"), mod.ui.remove("p1"), (pcall(mod.ui.open, "p1")))
end)
"""
    out = run(exe, temp, "uibad", lua, *play, "ui")
    check(logs(out) == ["bad 27", "fifth 4 false", "again true", "remove true false false"],
          "27 pages refused; 4 at most, every mod's together, a fifth false; a page replaced in its place; remove: "
          + str(logs(out)))
    check(answers(out, "UIPAGE") == ["UIPAGE 0 tactical p2 Again|", "UIPAGE 1 tactical p3 P3|", "UIPAGE 2 tactical p4 P4|"],
          "the pages left, in their order: " + str(answers(out, "UIPAGE")))

    # open: the screen asked for, the page's tab up once shown; close; false with no screen
    lua = r"""
mod.on("mission_start", function()
  mod.ui.page("a", { title = "A" }); mod.ui.page("b", { title = "B" })
  print("open", mod.ui.open("b"))
end)
mod.on("tick", function() mod.ui.close() end)
"""
    out = run(exe, temp, "uiopen", lua, *play, "ui", "uiopened", "ui", "frame", "ui")
    check(answers(out, "PANEL", "UIASKED") == ["PANEL open", "UIASKED 1 of 2", "UIASKED -1 of 2", "PANEL close", "UIASKED -1 of 2"]
          and logs(out) == ["open true"], "mod.ui.open asks for the screen on its page until its tab is up; close: "
          + str(answers(out, "PANEL", "UIASKED")))
    out = run(exe, temp, "uiclose", lua, *play, "frame", "ui")
    check(answers(out, "UIASKED") == ["UIASKED -1 of 2"], "a close drops the ask")
    out = run(exe, temp, "uinopanel", lua, "panel", 0, *play, "ui")
    check(logs(out) == ["open false"] and not answers(out, "PANEL open"), "no screen in this build: false")
    out = run(exe, temp, "uiserver", lua, "online", 1, 1, 1, -1, *play, "ui")
    check(logs(out) == ["open false"] and answers(out, "UIASKED") == ["UIASKED -1 of 2"], "a dedicated server: false")

    # a stopped mod's pages hide, and come back with it in the next mission; a mission's loading drops the clicks
    lua = r"""
mod.on("mission_start", function(stage)
  if stage == 9 then mod.ui.page("p", { title = "P", buttons = { { id = "go", label = "Go" } } }) end
end)
mod.on("ui_click", function(page, b) print("click", page, b) end)
mod.on("tick", function() if bad then error("stopped") end end)
mod.on("mission_prepare", function(stage) bad = stage == 9 end)
mod.ui.page("early", { title = "Early" })
bad = true
"""
    out = run(exe, temp, "uistop", lua, "start", "prepare", 9, "begin", 9, "ui", "frame", "ui", "uiclick", 0, 0,
              "prepare", 10, "begin", 10, "ui", "uiclick", 1, 0, "prepare", 11, "begin", 11, "frame", "ui")
    check(answers(out, "UIASKED") == ["UIASKED -1 of 2", "UIASKED -1 of 0", "UIASKED -1 of 2", "UIASKED -1 of 2"]
          and logs(out) == [],
          "a stopped mod's pages hide, its clicks go nowhere; next mission they're back; a click before a mission "
          "loads is dropped: " + str(answers(out, "UIASKED")) + str(logs(out)))


def main():
    placement()
    items_placement()
    drops_placement()
    armour_placement()
    hands_placement()
    guards_placement()
    guards2_placement()
    pads_placement()
    damage_placement()
    effects_placement()
    net_placement()
    ui_placement()
    scope_placement()
    gunmodel_placement()
    attach_placement()
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

        # ---- out_of_ammo: an empty gun kept in hand until it has rounds or is drawn ----
        lua = """
local keep = true
mod.on("out_of_ammo", function(hand, item) print("out_of_ammo", hand, item); return not keep end)
"""
        out = run(exe, temp, "empty", lua, *play, "hold", 0, 8, 0, "empty", 0, "empty", 0, "fire", 0, "empty", 0,
                  "reload", 0, 5, "empty", 0, "draw", 0, 8, "empty", 0, "local", 0, "empty", 0)
        check(logs(out) == ["out_of_ammo right 8", "out_of_ammo right 8", "out_of_ammo right 8", "out_of_ammo right 8"],
              "asked once while kept, again after a shot, a reload and a draw: " + str(logs(out)))
        check(answers(out, "KEPT", "SWITCH") == ["KEPT", "KEPT", "KEPT", "KEPT", "KEPT", "SWITCH"],
              "kept each time; a copy's switch isn't asked: " + str(answers(out, "KEPT", "SWITCH")))
        out = run(exe, temp, "emptyno", 'mod.on("out_of_ammo", function() end)', *play, "hold", 0, 8, 0, "empty", 0)
        check(answers(out, "KEPT", "SWITCH") == ["SWITCH"], "a handler returning nothing lets the game switch")

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

        # ---- the handling stats, and the player's speed ----
        lua = r"""
mod.on("mission_start", function()
  local w = mod.weapon
  local s = w.stats("ak47")
  print("own", s.inaccuracy, s.recoilBack, s.sound, s.loudnessMax)
  assert(w.setStats("ak47", { inaccuracy = 2.5, recoilBack = 4, recoilUp = 1, sound = 12, loudnessMin = 1, loudnessMax = 5 }))
  local bad = 0
  for _, t in ipairs({ { inaccuracy = 101 }, { recoilUp = -1 }, { sound = 120 }, { sound = 1.5 }, { loudnessMin = 6 },
                       { loudnessMax = "x" } }) do
    if not pcall(w.setStats, "ak47", t) then bad = bad + 1 end
  end
  print("bad", bad, w.stats("ak47").sound)
  print("speed", (pcall(mod.player.setSpeed, 0.1)), (pcall(mod.player.setSpeed, 2.5)))
  mod.player.setSpeed(0.8)
  print("auto", w.stats("wppk").automatic, w.stats("ak47").automatic, (pcall(w.setStats, "wppk", { automatic = true })),
        (pcall(w.setStats, "wppk", { automatic = 1 })))
  assert(w.setStats("wppk", { automatic = true, fireRate = 2 }))
  assert(w.setStats("ak47", { automatic = false }))
  print("now", w.stats("wppk").automatic, w.stats("wppk").fireRate, w.stats("ak47").automatic,
        (pcall(w.setStats, "ak47", { fireRate = 4 })))
end)
"""
        out = run(exe, temp, "handling", lua, *play, "show2", 8, "show2", 4, "title", "show2", 8, "show2", 4)
        check(logs(out) == ["own 0.0 0.0 0 0.0", "bad 6 12", "speed false false", "auto false true false false",
                            "now true 2 false false"], "handling: " + str(logs(out)))
        check(answers(out, "STATS2") == ["STATS2 2.50 4.00 1.00 12 1.00 5.00 0 3", "STATS2 0.00 0.00 0.00 0 0.00 0.00 1 2",
                                         "STATS2 0.00 0.00 0.00 0 0.00 0.00 1 3", "STATS2 0.00 0.00 0.00 0 0.00 0.00 0 -1"],
              "accuracy, recoil, sound and loudness set; the PP7 automatic at 2 ticks, the KF7 a shot a press; then the "
          "game's own again at the mission's end: "
              + str(answers(out, "STATS2")))
        check(answers(out, "SPEED")[-3:] == ["SPEED 1.00", "SPEED 0.80", "SPEED 1.00"],
              "the speed: the game's own at the mission's start, the mod's, the game's own after: "
              + str(answers(out, "SPEED")))

        # ---- an error puts the stats back at once ----
        lua = """
mod.on("mission_start", function() mod.weapon.setStats("ak47", { magazine = 99 }) end)
mod.on("weapon_fire_attempt", function() error("jam") end)
"""
        out = run(exe, temp, "error", lua, *play, "show", 8, "fire", 0, "show", 8, "fire", 0)
        check(answers(out, "STATS", "FIRED", "DECLINED") == ["STATS 8 99 3 1.50", "FIRED 6", "STATS 8 30 3 1.50", "FIRED 5"],
              "an error: the shot happens, the mod stops, its stats go back: " + str(answers(out, "STATS", "FIRED", "DECLINED")))

        items(exe, temp)
        pickup_ids(exe, temp)
        throws(exe, temp)
        shared_drops(exe, temp)
        hands(exe, temp)
        guards(exe, temp)
        guards2(exe, temp)
        pads(exe, temp)
        mission(exe, temp)
        damage(exe, temp)
        effects(exe, temp)
        net(exe, temp)
        ui(exe, temp)
        scope(exe, temp)
        gunmodel(exe, temp)
        gunmodel_fit(exe, temp)
        attach(exe, temp)
        attach_authored_frame(exe, temp)
        pack_names(exe, temp)
        attach_loadout(exe, temp)
        guards_told(temp)

    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        sys.exit(1)
    print("PASS: mods' weapons area: fire attempts declined or not, weapon_fire, reloads asked and declined, an "
          "empty gun kept in hand, draws, the local player only, "
          "mod.weapon's calls and limits, mod.items, the stats put back; the hooks' places in the gun code. "
          "Items area: pickup_spawning's swaps, turns and refusals, item_pickup with its pickup id, ids never reused and pickup_removed, mod.pickups.drop (armour too, and shared, the host deciding who takes one), "
          "weapon_dropped, mod.inventory, mod.ammo; "
          "the hooks' places in the stage's setup and the pickup. Hands area: grip, release, hand_near, "
          "mod.hands' positions, anchors and pulses; the hooks' places. Guards area: guard_spawning's body, head and gun, "
          "refusals, alerts, kills and corpses told once on the host, mod.guards' reads; the hooks' places. Guards, part 2: "
          "spawn, giveWeapon, patrol, paths, goTo, face, giveHat, mod.pads, mod.mission (and complete in a mod campaign's own save), guard_spawned, guard_killed's player. Damage and player: before_damage's "
          "answers and refusals, damage_applied, downed, revived, died, mod.player's health and armour; the hooks' places. "
          "Sound and smoke: mod.audio's ids, volumes and places, mod.effect's smokes and places. Network: mod.net's sends, "
          "limits and rate, held messages by channel and mod, peer_ready, a stop told to the others and taken from them. "
          "UI: mod.ui's pages and limits, the screen's clicks and changes, open and close, a stopped mod's pages hidden. "
          "Scopes: mod.weapon.scope's lens, none and the game's own, its limits, a draw's reset; mod.weapon.look; "
          "the hand's scope wherever the game read the item's. Gun models: mod.weapon.model's choices, refusals "
          "and reset; the hand's load, load_resource's name and the fits. Attachments: mod.attach on the gun, a hand "
          "and the body or a gun model's node or a guard's body part, its offset, refusals, place, show and remove (the owner's only), one gone with its guard's body; the "
          "draws, frames and load; a measured loadout's size twice over, the limits, and a frame without room skipping one")


if __name__ == "__main__":
    main()
