/*
 * The mod API's areas (docs/mod-system.md, API v1): the game's events into
 * mods' scripts (gevr_modscript.c) and the mod.* tables, over the thin game
 * side in src/game/gevr_modgame.c. Each area is an adapter over a system the
 * game already has; nothing here plays the game.
 *
 * The weapons area (docs/mod-system.md, Where each hook sits):
 * - weapon_fire_attempt(hand, item) before a shot or throw spends anything
 *   (gunfire.c gunTickHandState, bondview2.c gevrMotionThrowTick); a
 *   handler returning false declines it: a gun clicks dry, a throw comes
 *   back. Not melee.
 * - reload_attempt(hand, item) before a reload takes rounds from the
 *   reserve; false declines it, and nothing is taken.
 * - weapon_fire(hand, item, loaded), reload_start(hand, item),
 *   reload_end(hand, item, loaded), weapon_drawn(hand, item).
 * - mod.weapon.held / loaded / setLoaded (hand), stats / setStats (item),
 *   within limits; the stats go back to the game's own at a mission's end
 *   and when a mod stops (gevrModApiReset). mod.items: names to ids.
 * All of it for this machine's human player only, never a copy or a bot.
 */
/* the C library's first: see gevr_modscript.c (MB_LEN_MAX) */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gevr_modapi.h"
#include "gevr_modattach_net.h"
#include "gevr_modscript.h"
#include "system.h"
#include "../external/lua/lua.h"
#include "../external/lua/lauxlib.h"

/* the network (port/src/net/net_core.h) */
extern bool netIsActive(void);
extern bool netIsHost(void);
extern bool netIsDedicated(void);
extern int netGetLocalSlot(void);
extern int netGetHostSlot(void);
extern int netSendModMessage(int kind, int to, const char *mod, const char *channel, const void *data, int len);
extern int netLobbySlotConnected(int slot);

/* ITEM_IDS from ITEM_UNARMED (0): mod.items, and a name for an item argument */
static const char *const s_itemNames[GEVR_MODAPI_ITEMS] = {
    "unarmed", "fist", "knife", "throwknife", "wppk", "wppksil", "tt33", "skorpion", "ak47", "uzi",
    "mp5k", "mp5ksil", "spectre", "m16", "fnp90", "shotgun", "autoshot", "sniperrifle", "ruger",
    "goldengun", "silverwppk", "goldwppk", "laser", "watchlaser", "grenadelaunch", "rocketlaunch",
    "grenade", "timedmine", "proximitymine", "remotemine", "trigger", "taser", "tankshells",
    "bombcase", "plastique", "flarepistol", "pitongun", "bungee", "doordecoder", "bombdefuser",
    "camera", "lockexploder", "doorexploder", "briefcase", "weaponcase", "safecrackercase",
    "keyanalysercase", "bug", "microcamera", "bugdetector", "explosivefloppy", "polarizedglasses",
    "darkglasses", "creditcard", "gaskeyring", "datathief", "watchidentifier", "watchcommunicator",
    "watchgeigercounter", "watchmagnetrepel", "watchmagnetattract", "goldeneyekey", "blackbox",
    "circuitboard", "clipboard", "stafflist", "dossierred", "plans", "spyfile", "blueprints", "map",
    "audiotape", "videotape", "dattape", "spooltape", "microfilm", "microcode", "lectre", "money",
    "goldbar", "heroin", "keycard", "keyyale", "keybolt", "suit_lf_hand", "joypad", "rocketround",
    "grenaderound", "token",
};

/* AMMOTYPE from AMMO_9MM (1): mod.ammo's names, and a name for an ammo argument
 * (9mm_2 is the Klobb's) */
static const char *const s_ammoNames[GEVR_MODAPI_AMMO_TYPES + 1] = {
    NULL, "9mm", "9mm_2", "rifle", "shotgun", "grenade", "rockets", "remotemine", "proxmine", "timedmine",
    "knife", "grenaderound", "magnum", "goldengun",
};

/* The body's anchors, the body slots' places (GEVR_BS_*): each named for
 * the hand whose side it's on, gun or off */
static const char *const s_anchorNames[GEVR_MODAPI_ANCHORS] = {
    "hip_gun", "hip_off", "back_gun", "back_off", "chest", "belt",
};

/* A guard's parts for mod.attach, in the game's hit part order (HIT_LEFT_FOOT 1
 * to HIT_CHEST 15, bondaicommands.h) */
static const char *const s_bodyPartNames[GEVR_MODAPI_GUARD_PARTS + 1] = {
    "left_foot", "left_leg", "left_thigh", "right_foot", "right_leg", "right_thigh", "pelvis", "head",
    "left_hand", "left_arm", "left_shoulder", "right_hand", "right_arm", "right_shoulder", "chest", NULL,
};

/* GEVR_MODALERT_*, GEVR_MODKILL_*: guard_alerted's how, guard_killed's */
static const char *const s_alertNames[] = { NULL, "saw", "heard", "shot", "alarm", "other" };
static const char *const s_killNames[] = { NULL, "shot", "explosion" };

/* GEVR_MODPICKUP_*: a pickup's kind in pickup_spawning and item_pickup */
static const char *const s_pickupKinds[] = { NULL, "weapon", "ammo", "crate", "armour", "key", "object" };

/* The weapon stats a mod changed, as the game had them: put back by gevrModApiReset */
static struct {
    int item;
    GevrModWeaponStats own;
} s_saved[GEVR_MODAPI_ITEMS];
static int s_savedCount;

/* ---- arguments ---- */

static const char *handName(int hand)
{
    return hand ? "left" : "right";
}

static int handArg(lua_State *L, int arg)
{
    if (lua_type(L, arg) == LUA_TSTRING) {
        const char *s = lua_tostring(L, arg);

        if (!strcmp(s, "right")) return 0;
        if (!strcmp(s, "left")) return 1;
    } else if (lua_isinteger(L, arg)) {
        const lua_Integer h = lua_tointeger(L, arg);

        if (h == 0 || h == 1) return (int)h;
    }
    return luaL_argerror(L, arg, "a hand is \"right\" or \"left\"");
}

static int itemArg(lua_State *L, int arg)
{
    lua_Integer item;

    if (lua_type(L, arg) == LUA_TSTRING) {
        const char *name = lua_tostring(L, arg);
        int i;

        for (i = 0; i < GEVR_MODAPI_ITEMS; i++) {
            if (!strcmp(name, s_itemNames[i])) return i;
        }
        return luaL_argerror(L, arg, "no item has that name (see mod.items)");
    }
    item = luaL_checkinteger(L, arg);
    if (item < 0 || item >= GEVR_MODAPI_ITEMS) {
        return luaL_argerror(L, arg, "no item has that id (see mod.items)");
    }
    return (int)item;
}

/* An item or ammo type at idx by name or id, from first to count - 1: -1
 * when it's neither */
static int lookup(lua_State *L, int idx, const char *const *names, int first, int count)
{
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char *s = lua_tostring(L, idx);
        int i;

        for (i = first; i < count; i++) {
            if (!strcmp(s, names[i])) return i;
        }
    } else if (lua_type(L, idx) == LUA_TNUMBER) {
        int whole;
        const lua_Integer v = lua_tointegerx(L, idx, &whole);   /* 3 or 3.0, as luaL_checkinteger takes */

        if (whole && v >= first && v < count) return (int)v;
    }
    return -1;
}

static int ammoArg(lua_State *L, int arg)
{
    const int type = lookup(L, arg, s_ammoNames, 1, GEVR_MODAPI_AMMO_TYPES + 1);

    if (type < 0) return luaL_argerror(L, arg, "an ammo type is a name or id from mod.ammo");
    return type;
}

static void needPlayer(lua_State *L, const char *call)
{
    if (!gevrModGamePlayerReady()) {
        luaL_error(L, "%s: no player in a stage now", call);
    }
}

/* ---- mod.weapon ---- */

static int l_held(lua_State *L)
{
    const int hand = handArg(L, 1);
    int item;

    needPlayer(L, "mod.weapon.held");
    item = gevrModGameHeld(hand);
    if (item < 0) lua_pushnil(L);
    else lua_pushinteger(L, item);
    return 1;
}

static int l_loaded(lua_State *L)
{
    const int hand = handArg(L, 1);

    needPlayer(L, "mod.weapon.loaded");
    lua_pushinteger(L, gevrModGameLoaded(hand));
    return 1;
}

/* setLoaded(hand, rounds): 0 to the held weapon's magazine; the count set */
static int l_setLoaded(lua_State *L)
{
    const int hand = handArg(L, 1);
    const lua_Integer want = luaL_checkinteger(L, 2);
    GevrModWeaponStats st;
    int item, rounds;

    needPlayer(L, "mod.weapon.setLoaded");
    item = gevrModGameHeld(hand);
    if (item < 0 || !gevrModGameStats(item, &st) || st.magazine <= 0) {
        return luaL_error(L, "mod.weapon.setLoaded: the %s hand's item holds no rounds", handName(hand));
    }
    rounds = want < 0 ? 0 : want > st.magazine ? st.magazine : (int)want;
    gevrModGameSetLoaded(hand, rounds);
    lua_pushinteger(L, rounds);
    return 1;
}

static int l_stats(lua_State *L)
{
    const int item = itemArg(L, 1);
    GevrModWeaponStats st;

    if (!gevrModGameStats(item, &st)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, st.magazine);
    lua_setfield(L, -2, "magazine");
    if (st.fireRate >= 0) {
        lua_pushinteger(L, st.fireRate);
        lua_setfield(L, -2, "fireRate");
    }
    lua_pushboolean(L, st.automatic);
    lua_setfield(L, -2, "automatic");
    lua_pushnumber(L, st.damage);
    lua_setfield(L, -2, "damage");
    lua_pushnumber(L, st.inaccuracy);
    lua_setfield(L, -2, "inaccuracy");
    lua_pushnumber(L, st.recoilBack);
    lua_setfield(L, -2, "recoilBack");
    lua_pushnumber(L, st.recoilUp);
    lua_setfield(L, -2, "recoilUp");
    lua_pushinteger(L, st.sound);
    lua_setfield(L, -2, "sound");
    lua_pushnumber(L, st.loudnessMin);
    lua_setfield(L, -2, "loudnessMin");
    lua_pushnumber(L, st.loudnessMax);
    lua_setfield(L, -2, "loudnessMax");
    if (st.throwSpeed > 0.0f) {
        lua_pushnumber(L, st.throwSpeed);
        lua_setfield(L, -2, "throwSpeed");
        lua_pushnumber(L, st.throwLift);
        lua_setfield(L, -2, "throwLift");
        lua_pushboolean(L, st.throwGuide);
        lua_setfield(L, -2, "throwGuide");
        lua_createtable(L, 0, 4);   /* its look */
        lua_pushnumber(L, st.guideLength);
        lua_setfield(L, -2, "length");
        lua_pushnumber(L, st.guideWidth);
        lua_setfield(L, -2, "width");
        lua_createtable(L, 3, 0);
        for (int c = 0; c < 3; c++) {
            lua_pushinteger(L, st.guideColour[c]);
            lua_rawseti(L, -2, c + 1);
        }
        lua_setfield(L, -2, "colour");
        lua_pushboolean(L, st.guideFade);
        lua_setfield(L, -2, "fade");
        lua_setfield(L, -2, "throwGuideStyle");
    }
    return 1;
}

/* a number field from 0 to most, kept when absent; a script error past it */
static void statNumber(lua_State *L, const char *key, double most, float *out)
{
    double v = 0.0;

    if (lua_getfield(L, 2, key) != LUA_TNIL) {
        if (!lua_isnumber(L, -1) || (v = lua_tonumber(L, -1)) < 0 || v > most) {
            char cap[48];   /* Lua's own formats have no %g */

            snprintf(cap, sizeof(cap), "%.3g", most);
            luaL_error(L, "mod.weapon.setStats: %s is 0 to %s", key, cap);
        }
        *out = (float)v;
    }
    lua_pop(L, 1);
}

/* the most a stat may be: ten times the weapon's own, or 10 (100 for
 * inaccuracy and loudness) where its own is less */
static double statMost(float own, double floor)
{
    const double ten = own * 10.0;

    return ten > floor ? ten : floor;
}

/* setStats(item, {magazine=, fireRate=, damage=}): any of them, within the
 * limits; for everyone holding that weapon, guards too, until the mission ends */
static int l_setStats(lua_State *L)
{
    const int item = itemArg(L, 1);
    GevrModWeaponStats cur, own, next;
    int i;

    luaL_checktype(L, 2, LUA_TTABLE);
    if (!gevrModGameStats(item, &cur)) {
        return luaL_error(L, "mod.weapon.setStats: %s has no weapon stats of its own", s_itemNames[item]);
    }
    for (i = 0; i < s_savedCount && s_saved[i].item != item; i++) {
    }
    own = i < s_savedCount ? s_saved[i].own : cur;
    next = cur;
    if (lua_getfield(L, 2, "magazine") != LUA_TNIL) {
        lua_Integer v;

        if (!lua_isinteger(L, -1) || (v = lua_tointeger(L, -1)) < 1 || v > GEVR_MODAPI_MAG_MAX) {
            return luaL_error(L, "mod.weapon.setStats: magazine is a whole number from 1 to %d", GEVR_MODAPI_MAG_MAX);
        }
        next.magazine = (int)v;
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "automatic") != LUA_TNIL) {
        if (!lua_isboolean(L, -1)) return luaL_error(L, "mod.weapon.setStats: automatic is true or false");
        next.automatic = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "fireRate") != LUA_TNIL) {
        lua_Integer v;

        if (!next.automatic) {
            return luaL_error(L, "mod.weapon.setStats: %s isn't automatic; it has no fireRate (automatic = true gives it one)",
                              s_itemNames[item]);
        }
        if (!lua_isinteger(L, -1) || (v = lua_tointeger(L, -1)) < GEVR_MODAPI_RATE_MIN || v > GEVR_MODAPI_RATE_MAX) {
            return luaL_error(L, "mod.weapon.setStats: fireRate is ticks between shots, %d to %d", GEVR_MODAPI_RATE_MIN,
                              GEVR_MODAPI_RATE_MAX);
        }
        next.fireRate = (int)v;
    }
    lua_pop(L, 1);
    if (next.automatic && next.fireRate < 0) {
        return luaL_error(L, "mod.weapon.setStats: %s has no fireRate of its own; an automatic needs one", s_itemNames[item]);
    }
    if (lua_getfield(L, 2, "damage") != LUA_TNIL) {
        const double cap = own.damage > 0 ? own.damage * GEVR_MODAPI_DAMAGE_SCALE : 0.0;
        double v;

        if (!lua_isnumber(L, -1) || (v = lua_tonumber(L, -1)) < 0 || v > cap) {
            char most[64];   /* Lua's own formats have no %g */

            snprintf(most, sizeof(most), "%.3g for %s (%g times its own)", cap, s_itemNames[item],
                     (double)GEVR_MODAPI_DAMAGE_SCALE);
            return luaL_error(L, "mod.weapon.setStats: damage is 0 to %s", most);
        }
        next.damage = (float)v;
    }
    lua_pop(L, 1);
    statNumber(L, "inaccuracy", statMost(own.inaccuracy, 100.0), &next.inaccuracy);
    statNumber(L, "recoilBack", statMost(own.recoilBack, 10.0), &next.recoilBack);
    statNumber(L, "recoilUp", statMost(own.recoilUp, 10.0), &next.recoilUp);
    statNumber(L, "loudnessMin", statMost(own.loudnessMax, 100.0), &next.loudnessMin);
    statNumber(L, "loudnessMax", statMost(own.loudnessMax, 100.0), &next.loudnessMax);
    if (next.loudnessMin > next.loudnessMax) {
        return luaL_error(L, "mod.weapon.setStats: loudnessMin is at most loudnessMax");
    }
    if (lua_getfield(L, 2, "sound") != LUA_TNIL) {
        lua_Integer v;

        if (!lua_isinteger(L, -1) || (v = lua_tointeger(L, -1)) < 0 || v >= gevrModGameSoundCount()) {
            return luaL_error(L, "mod.weapon.setStats: sound is a ROM sound effect's id, 0 to %d",
                              gevrModGameSoundCount() - 1);
        }
        next.sound = (int)v;
    }
    lua_pop(L, 1);
    {
        /* a thrown weapon or gadget's trigger throw: its speed, lift and guide */
        static const char *const throws[] = { "throwSpeed", "throwLift", "throwGuide" };
        int k;

        for (k = 0; k < 3; k++) {
            const int there = lua_getfield(L, 2, throws[k]) != LUA_TNIL;
            lua_Number v = 0;
            int ok = 0;

            if (there && cur.throwSpeed <= 0.0f) {
                return luaL_error(L, "mod.weapon.setStats: %s isn't thrown; it has no %s", s_itemNames[item], throws[k]);
            }
            if (there && k == 0) {
                v = lua_tonumberx(L, -1, &ok);
                if (!ok || !(v >= 1.0 && v <= GEVR_MODAPI_THROW_SPEED_MAX)) {
                    return luaL_error(L, "mod.weapon.setStats: throwSpeed is 1 to %d", (int)GEVR_MODAPI_THROW_SPEED_MAX);
                }
                next.throwSpeed = (float)v;
            } else if (there && k == 1) {
                v = lua_tonumberx(L, -1, &ok);
                if (!ok || !(v >= -GEVR_MODAPI_THROW_LIFT_MAX && v <= GEVR_MODAPI_THROW_LIFT_MAX)) {
                    return luaL_error(L, "mod.weapon.setStats: throwLift is -%d to %d", (int)GEVR_MODAPI_THROW_LIFT_MAX,
                                      (int)GEVR_MODAPI_THROW_LIFT_MAX);
                }
                next.throwLift = (float)v;
            } else if (there && lua_isboolean(L, -1)) {
                next.throwGuide = lua_toboolean(L, -1);
            } else if (there && lua_istable(L, -1)) {
                /* on, in a look of the pack's: what it leaves out is the game's tracer's */
                const int t = lua_gettop(L);

                next.throwGuide = 1;
                next.guideLength = GEVR_MODAPI_GUIDE_LENGTH;
                next.guideWidth = GEVR_MODAPI_GUIDE_WIDTH;
                next.guideColour[0] = next.guideColour[1] = next.guideColour[2] = 255;
                next.guideFade = 0;
                if (lua_getfield(L, t, "length") != LUA_TNIL) {
                    v = lua_tonumberx(L, -1, &ok);
                    if (!ok || !(v >= GEVR_MODAPI_GUIDE_LENGTH_MIN && v <= GEVR_MODAPI_GUIDE_LENGTH_MAX)) {
                        return luaL_error(L, "mod.weapon.setStats: throwGuide's length is %d to %d", (int)GEVR_MODAPI_GUIDE_LENGTH_MIN,
                                          (int)GEVR_MODAPI_GUIDE_LENGTH_MAX);
                    }
                    next.guideLength = (float)v;
                }
                lua_pop(L, 1);
                if (lua_getfield(L, t, "width") != LUA_TNIL) {
                    v = lua_tonumberx(L, -1, &ok);
                    if (!ok || !(v >= GEVR_MODAPI_GUIDE_WIDTH_MIN && v <= GEVR_MODAPI_GUIDE_WIDTH_MAX)) {
                        return luaL_error(L, "mod.weapon.setStats: throwGuide's width is 0.5 to %d", (int)GEVR_MODAPI_GUIDE_WIDTH_MAX);
                    }
                    next.guideWidth = (float)v;
                }
                lua_pop(L, 1);
                if (lua_getfield(L, t, "colour") != LUA_TNIL) {
                    int c;

                    if (!lua_istable(L, -1) || lua_rawlen(L, -1) != 3) {
                        return luaL_error(L, "mod.weapon.setStats: throwGuide's colour is { r, g, b }, 0 to 255 each");
                    }
                    for (c = 0; c < 3; c++) {
                        int whole;
                        lua_Integer n;

                        lua_rawgeti(L, -1, c + 1);
                        n = lua_tointegerx(L, -1, &whole);
                        lua_pop(L, 1);
                        if (!whole || n < 0 || n > 255) {
                            return luaL_error(L, "mod.weapon.setStats: throwGuide's colour is { r, g, b }, 0 to 255 each");
                        }
                        next.guideColour[c] = (int)n;
                    }
                }
                lua_pop(L, 1);
                if (lua_getfield(L, t, "fade") != LUA_TNIL) {
                    if (!lua_isboolean(L, -1)) return luaL_error(L, "mod.weapon.setStats: throwGuide's fade is true or false");
                    next.guideFade = lua_toboolean(L, -1);
                }
                lua_pop(L, 1);
            } else if (there) {
                return luaL_error(L, "mod.weapon.setStats: throwGuide is true, false or a look { length, width, colour, fade }");
            }
            lua_pop(L, 1);
        }
    }
    if (i == s_savedCount && s_savedCount < GEVR_MODAPI_ITEMS) {
        s_saved[s_savedCount].item = item;
        s_saved[s_savedCount].own = cur;
        s_savedCount++;
    }
    gevrModGameSetStats(item, &next);
    lua_pushboolean(L, 1);
    return 1;
}

/* a number field of the scope's table within [lo, hi]: a script error otherwise */
static float scopeNumber(lua_State *L, int t, const char *key, int index, float lo, float hi, const char *what)
{
    lua_Number v;
    int ok;

    if (key) lua_getfield(L, t, key);
    else lua_rawgeti(L, t, index);
    v = lua_tonumberx(L, -1, &ok);
    lua_pop(L, 1);
    if (!ok || !(v >= lo && v <= hi)) luaL_error(L, "mod.weapon.scope: %s", what);
    return (float)v;
}

/*
 * scope(hand, t | false | nil): the game's per-hand VR scope on the held gun.
 *   t = { at = { x, y, z }, radius = r, zoom = m }: a lens at the eyepiece
 *     (x, y, z, r in the drawn model's own coordinates, as a model editor
 *     shows them; mod.weapon.look says which models), m times magnified;
 *   false: none, even on a gun the game scopes;
 *   nil: the game's own.
 * For the gun the hand holds now: a draw puts the game's own back, so
 * weapon_drawn is the place to set it. True when set; false with nothing in
 * the hand. It shows only in VR.
 */
static int l_scope(lua_State *L)
{
    const int hand = handArg(L, 1);
    float at[3] = { 0.0f, 0.0f, 0.0f }, radius = 0.0f, zoomDeg = 0.0f;
    int mode, item, i;

    needPlayer(L, "mod.weapon.scope");
    if (lua_isnoneornil(L, 2)) {
        mode = GEVR_MODSCOPE_GAME;
    } else if (lua_isboolean(L, 2) && !lua_toboolean(L, 2)) {
        mode = GEVR_MODSCOPE_NONE;
    } else {
        float zoom;

        luaL_argexpected(L, lua_istable(L, 2), 2, "a table { at, radius, zoom }, false or nil");
        if (lua_getfield(L, 2, "at") != LUA_TTABLE || lua_rawlen(L, -1) != 3) {
            luaL_error(L, "mod.weapon.scope: at is { x, y, z }, the eyepiece in the model's own units");
        }
        for (i = 0; i < 3; i++) {
            at[i] = scopeNumber(L, lua_gettop(L), NULL, i + 1, -GEVR_MODAPI_SCOPE_RANGE, GEVR_MODAPI_SCOPE_RANGE,
                                "at's x, y and z are -4000 to 4000");
        }
        lua_pop(L, 1);
        radius = scopeNumber(L, 2, "radius", 0, GEVR_MODAPI_SCOPE_RADIUS_MIN, GEVR_MODAPI_SCOPE_RADIUS_MAX,
                             "radius is 1 to 400, in the model's own units");
        zoom = scopeNumber(L, 2, "zoom", 0, GEVR_MODAPI_SCOPE_ZOOM_MIN, GEVR_MODAPI_SCOPE_ZOOM_MAX,
                           "zoom is a magnification, 1 to 25");
        /* the game's scopes speak in the angle shown, its normal view 60 degrees */
        zoomDeg = (float)(2.0 * atan(tan(30.0 * 3.14159265358979 / 180.0) / zoom) * 180.0 / 3.14159265358979);
        mode = GEVR_MODSCOPE_MOD;
    }
    item = gevrModGameHeld(hand);
    if (item <= 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    gevrModGameScope(hand, mode, item, at, radius, zoomDeg);
    lua_pushboolean(L, 1);
    return 1;
}

/* look(hand): the models the hand's gun is drawn with, for scope's at */
static int l_look(lua_State *L)
{
    static const char *const s_looks[] = { "original", "gex", "bean", "mod" };
    const int hand = handArg(L, 1);
    int look;

    needPlayer(L, "mod.weapon.look");
    look = gevrModGameLook(hand);
    lua_pushstring(L, s_looks[look >= 0 && look <= 3 ? look : 0]);
    return 1;
}

/* A pack model's name: a cartridge file's (letters, digits, _), 1 to 63 */
static int modelNameOk(const char *s, size_t n)
{
    size_t i;

    if (n < 1 || n > 63) return 0;
    if (n > 4 && !memcmp(s + n - 4, ".bin", 4)) n -= 4;
    for (i = 0; i < n; i++) {
        const char c = s[i];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/* Four affine rows in the same convention as attachOffset: the model's
 * local axes in anchor coordinates, followed by its origin in model units.
 * Do not decompose the supplied frame; nested mounts can include scale and
 * reflection. The model still follows the anchor's engine animation. */
static int authoredFrame(lua_State *L, int t, const char *call, float out[4][4])
{
    if (lua_getfield(L, t, "frame") == LUA_TNIL) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1) || lua_rawlen(L, -1) != 4) {
        return luaL_error(L, "%s: frame needs four 3-number rows (axes, then origin)", call);
    }
    for (int row = 0; row < 4; ++row) {
        const double limit = row == 3 ? GEVR_MODAPI_ATTACH_RANGE : GEVR_MODAPI_ATTACH_SCALE_MAX;
        lua_rawgeti(L, -1, row + 1);
        if (!lua_istable(L, -1) || lua_rawlen(L, -1) != 3) {
            return luaL_error(L, "%s: frame needs four 3-number rows (axes, then origin)", call);
        }
        for (int col = 0; col < 3; ++col) {
            if (lua_rawgeti(L, -1, col + 1) != LUA_TNUMBER) {
                return luaL_error(L, "%s: frame needs four 3-number rows (axes, then origin)", call);
            }
            const double n = lua_tonumber(L, -1);
            if (!(n >= -limit && n <= limit)) {
                return luaL_error(L, "%s: frame component outside its limit", call);
            }
            out[row][col] = (float)n;
            lua_pop(L, 1);
        }
        out[row][3] = row == 3;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    const double a = out[0][0], b = out[0][1], c = out[0][2];
    const double d = out[1][0], e = out[1][1], f = out[1][2];
    const double g = out[2][0], h = out[2][1], i = out[2][2];
    if (fabs(a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)) < 1e-8) {
        return luaL_error(L, "%s: frame needs independent axes", call);
    }
    return 1;
}

static int authoredGunPoint(lua_State *L, int t, const char *key, float out[3])
{
    if (lua_getfield(L, t, key) == LUA_TNIL) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1) || lua_rawlen(L, -1) != 3) {
        return luaL_error(L, "mod.weapon.model: %s needs three numbers", key);
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (lua_rawgeti(L, -1, axis + 1) != LUA_TNUMBER) {
            return luaL_error(L, "mod.weapon.model: %s needs three numbers", key);
        }
        const double value = lua_tonumber(L, -1);
        if (!(value >= -GEVR_MODAPI_ATTACH_RANGE && value <= GEVR_MODAPI_ATTACH_RANGE)) {
            return luaL_error(L, "mod.weapon.model: %s components are limited to +/-4000 model units", key);
        }
        out[axis] = (float)value;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 1;
}

/*
 * model(hand, item, file | nil [, { frame, muzzle, support }]): when the hand draws item, the mod's
 * assets/models/<file> in place of the item's cartridge model, built over
 * that model's header (its switch and texture counts, its skeleton); nil, the
 * item's own. A hand holding item now loads it at once. True when set; false
 * when the mod has no such file. Every hand's own come back at a mission's
 * start and when a mod stops.
 */
static int l_gunModel(lua_State *L)
{
    const int hand = handArg(L, 1);
    const int item = itemArg(L, 2);
    const char *name = NULL;

    if (!lua_isnoneornil(L, 3)) {
        size_t n;

        name = luaL_checklstring(L, 3, &n);
        if (!modelNameOk(name, n)) {
            return luaL_argerror(L, 3, "a flat model file name (assets/models/<file>)");
        }
    }
    if (!gevrModGameSwappable(item)) {
        return luaL_argerror(L, 2, "a gun or thrown weapon with a model in this cartridge");
    }
    float frame[4][4], muzzle[3], support[3];
    const float (*fit)[4] = NULL;
    const float *muzzlePoint = NULL, *supportPoint = NULL;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        if (name == NULL) return luaL_argerror(L, 4, "only with a pack mesh file");
        if (authoredFrame(L, 4, "mod.weapon.model", frame)) fit = frame;
        if (authoredGunPoint(L, 4, "muzzle", muzzle)) muzzlePoint = muzzle;
        if (authoredGunPoint(L, 4, "support", support)) supportPoint = support;
        if (fit == NULL && muzzlePoint == NULL && supportPoint == NULL)
            return luaL_argerror(L, 4, "a frame, muzzle or support point for a pack mesh");
    }
    needPlayer(L, "mod.weapon.model");
    lua_pushboolean(L, gevrModGameGunModel(hand, item, name, fit, muzzlePoint, supportPoint));
    return 1;
}

static const luaL_Reg s_weaponFuncs[] = {
    { "held", l_held },
    { "loaded", l_loaded },
    { "setLoaded", l_setLoaded },
    { "stats", l_stats },
    { "setStats", l_setStats },
    { "scope", l_scope },
    { "look", l_look },
    { "model", l_gunModel },
    { NULL, NULL },
};

/* ---- mod.inventory, mod.ammo ---- */

static int l_has(lua_State *L)
{
    const int item = itemArg(L, 1);

    needPlayer(L, "mod.inventory.has");
    lua_pushboolean(L, gevrModGameHas(item));
    return 1;
}

/* A gun or thrown weapon only: mission items and gadgets stay the level's */
static int swappableArg(lua_State *L, int arg, const char *call)
{
    const int item = itemArg(L, arg);

    if (!gevrModGameSwappable(item)) {
        luaL_error(L, "%s: %s isn't a gun or thrown weapon in this cartridge", call, s_itemNames[item]);
    }
    needPlayer(L, call);
    return item;
}

/* give(item): true when newly given; no rounds come with it (mod.ammo.set) */
static int l_give(lua_State *L)
{
    lua_pushboolean(L, gevrModGameGive(swappableArg(L, 1, "mod.inventory.give")));
    return 1;
}

/* take(item): true when it was there; a hand holding it goes unarmed */
static int l_take(lua_State *L)
{
    lua_pushboolean(L, gevrModGameTake(swappableArg(L, 1, "mod.inventory.take")));
    return 1;
}

static const luaL_Reg s_inventoryFuncs[] = {
    { "has", l_has },
    { "give", l_give },
    { "take", l_take },
    { NULL, NULL },
};

/* get(type): the spare rounds, not those loaded (mod.weapon.loaded) */
static int l_ammoGet(lua_State *L)
{
    const int type = ammoArg(L, 1);

    needPlayer(L, "mod.ammo.get");
    lua_pushinteger(L, gevrModGameAmmo(type));
    return 1;
}

/* set(type, rounds): 0 to the type's most; the count set */
static int l_ammoSet(lua_State *L)
{
    const int type = ammoArg(L, 1);
    const lua_Integer want = luaL_checkinteger(L, 2);
    const int most = gevrModGameAmmoMax(type);
    const int rounds = want < 0 ? 0 : want > most ? most : (int)want;

    needPlayer(L, "mod.ammo.set");
    gevrModGameSetAmmo(type, rounds);
    lua_pushinteger(L, rounds);
    return 1;
}

static int l_ammoMax(lua_State *L)
{
    lua_pushinteger(L, gevrModGameAmmoMax(ammoArg(L, 1)));
    return 1;
}

static const luaL_Reg s_ammoFuncs[] = {
    { "get", l_ammoGet },
    { "set", l_ammoSet },
    { "max", l_ammoMax },
    { NULL, NULL },
};

/* ---- mod.pickups ---- */

/*
 * Runtime pickups' ids (gevr_modapi.h): the game recycles its weapon and ammo
 * box slots (and frees the oldest dropped gun when the weapon slots are full),
 * so a slot's number names a different pickup over a mission. Each is given
 * the next id as it's first named (item_pickup, weapon_dropped, drop) and
 * keeps it until its slot is freed; the id is never given again this mission.
 * One freed that this headset's player didn't take is told as pickup_removed
 * at the next frame in play, outside whatever call freed it.
 */
static int s_pickupId[GEVR_MODAPI_PICKUP_KINDS][GEVR_MODAPI_PICKUP_SLOTS];
static unsigned char s_pickupTaken[GEVR_MODAPI_PICKUP_KINDS][GEVR_MODAPI_PICKUP_SLOTS];
static int s_pickupNext = GEVR_MODAPI_PICKUP_RUNTIME;
static int s_removed[GEVR_MODAPI_PICKUP_QUEUE], s_removedQueued, s_removedDropped;

/*
 * Shared pickups (drop's shared): the host's drop is given the next id from
 * GEVR_MODAPI_PICKUP_SHARED and sent (NET_MOD_DROP); each other headset drops
 * its own copy from the same chr and binds it that id. The host decides who
 * takes one: a player's pickup is held back and claimed (NET_MOD_CLAIM); the
 * host awards the first claim it has, its own pickup the same way, while it
 * still has its copy (NET_MOD_GRANT): the winner's game takes it, the others
 * free theirs (told there as pickup_removed). A claim for one the host no
 * longer has gets NET_MOD_GONE back, and a copy the game frees untaken (a
 * recycled slot) is sent as NET_MOD_GONE, so every copy goes. s_pickupMod:
 * the mod that dropped it, which the messages carry; empty for a pickup that
 * isn't shared.
 */
static int s_pickupBind;   /* the next id named: a shared drop's own */
static int s_sharedNext = GEVR_MODAPI_PICKUP_SHARED;
static char s_pickupMod[GEVR_MODAPI_PICKUP_KINDS][GEVR_MODAPI_PICKUP_SLOTS][64];
static unsigned long long s_claimAt[GEVR_MODAPI_PICKUP_KINDS][GEVR_MODAPI_PICKUP_SLOTS];
static int s_pickupRemoving;   /* a copy freed for a message: nothing to tell */
static int s_pickupGranting;   /* the host's award being taken: not held back */
typedef struct {
    int kind, stage, id, item, ammo, from, winner, sender;
    float armour;
    char mod[64];
} HeldPickup;
static HeldPickup s_heldPickup[GEVR_MODAPI_PICKUP_HELD];
static int s_heldPickups, s_heldPickupsDropped;

static int pickupSlotOk(int kind, int slot)
{
    return kind >= 0 && kind < GEVR_MODAPI_PICKUP_KINDS && slot >= 0 && slot < GEVR_MODAPI_PICKUP_SLOTS;
}

/* the runtime slot holding a pickup with that id now */
static int pickupSlotOf(int id, int *kind, int *slot)
{
    int k, s;

    for (k = 0; id != 0 && k < GEVR_MODAPI_PICKUP_KINDS; k++) {
        for (s = 0; s < GEVR_MODAPI_PICKUP_SLOTS; s++) {
            if (s_pickupId[k][s] == id) {
                *kind = k;
                *slot = s;
                return 1;
            }
        }
    }
    return 0;
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned get32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

int gevrModApiPickupSlotId(int kind, int slot)
{
    if (!pickupSlotOk(kind, slot)) return -1;
    if (s_pickupId[kind][slot] == 0) {
        if (s_pickupBind) {
            s_pickupId[kind][slot] = s_pickupBind;   /* a shared drop's: the host's id */
            s_pickupBind = 0;
        } else {
            if (s_pickupNext > GEVR_MODAPI_PICKUP_LAST) return -1;
            s_pickupId[kind][slot] = s_pickupNext++;
        }
        s_pickupTaken[kind][slot] = 0;
        s_pickupMod[kind][slot][0] = '\0';
    }
    return s_pickupId[kind][slot];
}

void gevrModApiPickupSlotTaken(int kind, int slot)
{
    if (pickupSlotOk(kind, slot) && s_pickupId[kind][slot] != 0) s_pickupTaken[kind][slot] = 1;
}

/* a shared pickup's claim, award or gone: its stage, the winner, its id */
static void sendPickup(int kind, int to, const char *mod, int winner, int id)
{
    unsigned char b[GEVR_MODAPI_NET_PICKUP_BYTES] = { 0 };

    b[0] = (unsigned char)gevrModScriptStage();
    b[1] = (unsigned char)winner;
    put32(b + 4, (unsigned)id);
    netSendModMessage(kind, to, mod, NULL, b, (int)sizeof(b));
}

/* this headset's copy freed for a message (another won it, or it's gone) */
static void removeCopy(int kind, int slot)
{
    s_pickupRemoving = 1;
    gevrModGamePickupRemove(kind, slot);
    s_pickupRemoving = 0;
}

int gevrModApiPickupTry(int kind, int slot, int local)
{
    unsigned long long now;
    int id;

    if (!pickupSlotOk(kind, slot) || (id = s_pickupId[kind][slot]) < GEVR_MODAPI_PICKUP_SHARED
        || !s_pickupMod[kind][slot][0] || !netIsActive() || s_pickupGranting) {
        return 0;   /* not shared, not a network game, or the award itself */
    }
    if (!local) return 1;   /* another player's: claimed from their headset */
    if (netIsHost()) {
        /* the host's own: awarded at once, as a claim would be, and told */
        sendPickup(GEVR_MODNET_GRANT, 0xFF, s_pickupMod[kind][slot], netGetLocalSlot(), id);
        return 0;
    }
    now = sysGetMicroseconds();
    if (s_claimAt[kind][slot] == 0 || now - s_claimAt[kind][slot] >= GEVR_MODAPI_CLAIM_US) {
        s_claimAt[kind][slot] = now ? now : 1;
        sendPickup(GEVR_MODNET_CLAIM, netGetHostSlot(), s_pickupMod[kind][slot], 0, id);
    }
    return 1;   /* held back for the host's award */
}

void gevrModApiPickupSlotFreed(int kind, int slot)
{
    int id;

    if (!pickupSlotOk(kind, slot) || (id = s_pickupId[kind][slot]) == 0) return;
    if (id >= GEVR_MODAPI_PICKUP_SHARED && s_pickupMod[kind][slot][0] && !s_pickupTaken[kind][slot]
        && !s_pickupRemoving && netIsActive() && gevrModGamePlayerReady()) {
        /* the game freed a shared copy untaken (its slot recycled, say): every copy goes */
        sendPickup(GEVR_MODNET_GONE, 0xFF, s_pickupMod[kind][slot], 0, id);
    }
    s_pickupId[kind][slot] = 0;
    s_pickupMod[kind][slot][0] = '\0';
    s_claimAt[kind][slot] = 0;
    if (!s_pickupTaken[kind][slot] && gevrModScriptRunning()) {
        if (s_removedQueued < GEVR_MODAPI_PICKUP_QUEUE) s_removed[s_removedQueued++] = id;
        else s_removedDropped++;
    }
    s_pickupTaken[kind][slot] = 0;
}

void gevrModNetPickup(const char *mod, int from, int kind, const void *data, int len)
{
    const unsigned char *b = (const unsigned char *)data;
    HeldPickup *h;

    if (!gevrModScriptRunning() || !mod || !b || strlen(mod) >= sizeof(h->mod) || kind < GEVR_MODNET_DROP
        || kind > GEVR_MODNET_GONE
        || len != (kind == GEVR_MODNET_DROP ? GEVR_MODAPI_NET_DROP_BYTES : GEVR_MODAPI_NET_PICKUP_BYTES)) {
        return;
    }
    if (s_heldPickups == GEVR_MODAPI_PICKUP_HELD) {
        s_heldPickupsDropped++;   /* logged as the frame applies them */
        return;
    }
    h = &s_heldPickup[s_heldPickups++];
    memset(h, 0, sizeof(*h));
    snprintf(h->mod, sizeof(h->mod), "%s", mod);
    h->kind = kind;
    h->sender = from;
    h->stage = b[0];
    h->winner = b[1];
    h->id = (int)get32(b + 4);
    if (kind == GEVR_MODNET_DROP) {
        const int raw = b[8] | b[9] << 8;
        const int who = raw >= 0x8000 ? raw - 0x10000 : raw;

        h->item = b[1];
        h->ammo = b[2];
        h->armour = (float)(b[10] | b[11] << 8) / 65535.0f;
        h->from = who >= 0 ? who : GEVR_MODAPI_FROM_SLOT0 - (-1 - who);   /* a guard, or a player's slot */
    }
}

/* the shared drops and takes that came, in order, if they're this mission's */
static void pickupApplyHeld(void)
{
    const int stage = gevrModScriptStage() & 0xFF;
    int q, n = s_heldPickups;
    static HeldPickup taken[GEVR_MODAPI_PICKUP_HELD];

    if (s_heldPickupsDropped) {
        sysLogPrintf(LOG_WARNING, "mods: %d shared pickup message(s) dropped: more than %d waited", s_heldPickupsDropped,
                     GEVR_MODAPI_PICKUP_HELD);
        s_heldPickupsDropped = 0;
    }
    memcpy(taken, s_heldPickup, sizeof(taken[0]) * (size_t)n);
    s_heldPickups = 0;
    for (q = 0; q < n; q++) {
        const HeldPickup *h = &taken[q];
        int kind, slot;

        if (h->stage != stage || h->id < GEVR_MODAPI_PICKUP_SHARED) continue;   /* another mission's */
        if (h->kind == GEVR_MODNET_DROP) {
            if (pickupSlotOf(h->id, &kind, &slot)
                || (h->item != 0) + (h->ammo != 0) + (h->armour > 0.0f) != 1
                || (h->item != 0 && !gevrModGameSwappable(h->item)) || h->ammo > GEVR_MODAPI_AMMO_TYPES) {
                continue;   /* here already, or not one a drop makes */
            }
            s_pickupBind = h->id;
            if (gevrModGameDrop(h->item, h->ammo, h->armour, h->from) >= 0 && pickupSlotOf(h->id, &kind, &slot)) {
                snprintf(s_pickupMod[kind][slot], sizeof(s_pickupMod[kind][slot]), "%s", h->mod);
            } else {
                sysLogPrintf(LOG_WARNING, "mods: %s: the host's shared pickup %d isn't here (no such guard or "
                             "player, or no free slot)", h->mod, h->id);
            }
            s_pickupBind = 0;
        } else if (h->kind == GEVR_MODNET_CLAIM) {
            /* the host: the first claim while its copy's here wins, as its own pickup would */
            if (!netIsHost() || h->sender < 0) continue;
            if (pickupSlotOf(h->id, &kind, &slot)) {
                sendPickup(GEVR_MODNET_GRANT, 0xFF, h->mod, h->sender, h->id);
                removeCopy(kind, slot);
            } else {
                sendPickup(GEVR_MODNET_GONE, h->sender, h->mod, 0, h->id);   /* late, or gone: nothing */
            }
        } else if (pickupSlotOf(h->id, &kind, &slot)) {
            if (h->kind == GEVR_MODNET_GRANT && h->winner == netGetLocalSlot()) {
                s_pickupGranting = 1;   /* this player's: taken through the game's own pickup */
                gevrModGamePickupGrant(kind, slot);
                s_pickupGranting = 0;
            } else {
                removeCopy(kind, slot);   /* another's, or gone: pickup_removed here */
            }
        }
    }
}

void gevrModApiPickupFlush(void)
{
    int q, n;
    int taken[GEVR_MODAPI_PICKUP_QUEUE];

    pickupApplyHeld();
    n = s_removedQueued;

    if (s_removedDropped) {
        sysLogPrintf(LOG_WARNING, "mods: %d pickup_removed dropped: more than %d in a frame", s_removedDropped,
                     GEVR_MODAPI_PICKUP_QUEUE);
        s_removedDropped = 0;
    }
    /* taken first: a handler's own calls can free more, told next frame */
    memcpy(taken, s_removed, sizeof(taken[0]) * (size_t)n);
    s_removedQueued = 0;
    for (q = 0; q < n; q++) {
        GevrModArg a[1];

        a[0].kind = GEVR_MODARG_INTEGER;
        a[0].n = taken[q];
        gevrModScriptEvent("pickup_removed", a, 1, 0);
    }
}

void gevrModApiPickupClear(void)
{
    memset(s_pickupId, 0, sizeof(s_pickupId));
    memset(s_pickupTaken, 0, sizeof(s_pickupTaken));
    s_pickupNext = GEVR_MODAPI_PICKUP_RUNTIME;
    s_removedQueued = 0;
    s_removedDropped = 0;
    s_pickupBind = 0;
    s_sharedNext = GEVR_MODAPI_PICKUP_SHARED;
    memset(s_pickupMod, 0, sizeof(s_pickupMod));
    s_heldPickups = 0;
    s_heldPickupsDropped = 0;
    memset(s_claimAt, 0, sizeof(s_claimAt));
    s_pickupRemoving = 0;
    s_pickupGranting = 0;
    gevrModGameDropsClear();   /* and the armour pool: the last stage's props are gone */
}

/*
 * drop{ item = gun, from = "player" | guard }, drop{ ammo = type, from = ... }
 * or drop{ armour = amount, from = ... }: the game's own drop of a gun or
 * thrown weapon (as a player's inventory is dropped, drop_inventory), of an
 * ammo box of that type (as a shot crate drops one), or of body armour (more
 * than 0, up to 1, as the stage's own), falling from the player or a guard (a
 * mod.guards id, a corpse too). The new pickup's id (as item_pickup and
 * weapon_dropped give it), or nil when there's no free slot or no such guard.
 * Placed on this headset only: in co-op, every headset that should have it
 * drops it. shared = true (the host, or solo): one pickup for every headset,
 * one id (from GEVR_MODAPI_PICKUP_SHARED), each other headset dropping its
 * copy from the same chr, and the host deciding who takes it.
 */
static int l_drop(lua_State *L)
{
    int item = 0, ammo = 0, from, id, shared = 0;
    float armour = 0.0f;

    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_getfield(L, 1, "item") != LUA_TNIL) item = itemArg(L, lua_gettop(L));
    lua_pop(L, 1);
    if (lua_getfield(L, 1, "ammo") != LUA_TNIL) ammo = ammoArg(L, lua_gettop(L));
    lua_pop(L, 1);
    if (lua_getfield(L, 1, "armour") != LUA_TNIL) {
        const lua_Number a = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : -1.0;

        if (!(a > 0.0 && a <= 1.0)) return luaL_error(L, "mod.pickups.drop: armour is more than 0, up to 1");
        armour = (float)a;
    }
    lua_pop(L, 1);
    if ((item != 0) + (ammo != 0) + (armour > 0.0f) != 1) {
        return luaL_error(L, "mod.pickups.drop: one of item, ammo or armour");
    }
    if (item != 0 && !gevrModGameSwappable(item)) {
        return luaL_error(L, "mod.pickups.drop: the item is a gun or thrown weapon with a model in this cartridge");
    }
    lua_getfield(L, 1, "from");
    if (lua_type(L, -1) == LUA_TSTRING && !strcmp(lua_tostring(L, -1), "player")) {
        from = GEVR_MODAPI_FROM_PLAYER;
    } else {
        int whole;
        const lua_Integer g = lua_tointegerx(L, -1, &whole);

        if (!whole || g < 0 || g > 0x7fffffff) return luaL_error(L, "mod.pickups.drop: from is \"player\" or a guard's id");
        from = (int)g;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "shared");
    if (!lua_isnil(L, -1) && !lua_isboolean(L, -1)) return luaL_error(L, "mod.pickups.drop: shared is true or false");
    shared = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (shared && netIsActive() && !netIsHost()) return luaL_error(L, "mod.pickups.drop: a shared drop is the host's");
    if (shared && from != GEVR_MODAPI_FROM_PLAYER && from > 32767) {
        return luaL_error(L, "mod.pickups.drop: a shared drop's guard is 0 to 32767");
    }
    needPlayer(L, "mod.pickups.drop");
    if (shared) {
        if (s_sharedNext == 0x7FFFFFFF) {
            lua_pushnil(L);   /* the mission's shared ids used up */
            return 1;
        }
        s_pickupBind = s_sharedNext++;
    }
    id = gevrModGameDrop(item, ammo, armour, from);
    s_pickupBind = 0;
    if (id >= 0 && shared) {
        int kind, slot;

        if (pickupSlotOf(id, &kind, &slot)) {
            snprintf(s_pickupMod[kind][slot], sizeof(s_pickupMod[kind][slot]), "%s", gevrModScriptIdOf(L));
        }
        if (netIsActive()) {
            unsigned char b[GEVR_MODAPI_NET_DROP_BYTES] = { 0 };
            const int who = from == GEVR_MODAPI_FROM_PLAYER ? -1 - netGetLocalSlot() : from;
            const unsigned amount = (unsigned)(armour * 65535.0f + 0.5f);

            b[0] = (unsigned char)gevrModScriptStage();
            b[1] = (unsigned char)item;
            b[2] = (unsigned char)ammo;
            put32(b + 4, (unsigned)id);
            b[8] = (unsigned char)(who & 0xFF);
            b[9] = (unsigned char)((who >> 8) & 0xFF);
            b[10] = (unsigned char)(amount & 0xFF);
            b[11] = (unsigned char)(amount >> 8);
            netSendModMessage(GEVR_MODNET_DROP, 0xFF, gevrModScriptIdOf(L), NULL, b, (int)sizeof(b));
        }
    }
    if (id < 0) lua_pushnil(L);
    else lua_pushinteger(L, id);
    return 1;
}

static const luaL_Reg s_pickupFuncs[] = {
    { "drop", l_drop },
    { NULL, NULL },
};

/* ---- mod.hands ---- */

/* position(hand): x, y, z in cm in the torso's frame, or nil */
static int l_handPosition(lua_State *L)
{
    const int hand = handArg(L, 1);
    float at[3];

    if (!gevrModGameHandAt(hand, at)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, at[0]);
    lua_pushnumber(L, at[1]);
    lua_pushnumber(L, at[2]);
    return 3;
}

/* anchor(name): x, y, z and its reach in cm, or nil */
static int l_handAnchor(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    float at[3], reach;
    int a;

    for (a = 0; a < GEVR_MODAPI_ANCHORS && strcmp(name, s_anchorNames[a]); a++) {
    }
    if (a == GEVR_MODAPI_ANCHORS) return luaL_argerror(L, 1, "no anchor has that name (see the spec's hands area)");
    if (!gevrModGameAnchorAt(a, at, &reach)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, at[0]);
    lua_pushnumber(L, at[1]);
    lua_pushnumber(L, at[2]);
    lua_pushnumber(L, reach);
    return 4;
}

/* haptic(hand, amplitude, seconds): true when a controller was pulsed */
static int l_haptic(lua_State *L)
{
    const int hand = handArg(L, 1);
    const lua_Number amp = luaL_checknumber(L, 2);
    const lua_Number secs = luaL_checknumber(L, 3);

    if (!(amp >= 0 && amp <= 1)) return luaL_argerror(L, 2, "an amplitude is 0 to 1");
    if (!(secs >= 0 && secs <= GEVR_MODAPI_HAPTIC_MAX_S)) {
        char most[48];   /* Lua's own formats have no %g */

        snprintf(most, sizeof(most), "a pulse lasts 0 to %g seconds", (double)GEVR_MODAPI_HAPTIC_MAX_S);
        return luaL_argerror(L, 3, most);
    }
    lua_pushboolean(L, gevrModGameHaptic(hand, (float)amp, (float)secs));
    return 1;
}

/* ---- mod.guards ---- */

#define GUARDS_LIST_MAX 512

/* list(): the guards' ids, living or dying */
static int l_guardList(lua_State *L)
{
    static int ids[GUARDS_LIST_MAX];
    const int n = gevrModGameGuards(ids, GUARDS_LIST_MAX);
    int i;

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++) {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static void setItem(lua_State *L, const char *field, int item)
{
    if (item >= 0) {
        lua_pushinteger(L, item);
        lua_setfield(L, -2, field);
    }
}

/* get(id): a table of what the game has for it, or nil */
static int l_guardGet(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1);
    GevrModGuardState g;

    if (id < -32768 || id > 32767 || !gevrModGameGuard((int)id, &g)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 12);
    lua_pushinteger(L, g.id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, g.body);
    lua_setfield(L, -2, "body");
    setItem(L, "head", g.head);
    lua_pushboolean(L, g.alive);
    lua_setfield(L, -2, "alive");
    lua_pushboolean(L, g.alerted);
    lua_setfield(L, -2, "alerted");
    setItem(L, "weapon", g.weapon[0]);
    setItem(L, "weaponLeft", g.weapon[1]);
    lua_pushnumber(L, g.health);
    lua_setfield(L, -2, "health");
    lua_pushnumber(L, g.maxHealth);
    lua_setfield(L, -2, "maxHealth");
    lua_pushnumber(L, g.pos[0]);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, g.pos[1]);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, g.pos[2]);
    lua_setfield(L, -2, "z");
    return 1;
}

/* A spawned guard's behaviour by name (GEVR_MODAI_*) */
static const struct {
    const char *name;
    int id;
} s_aiNames[] = {
    { "standard", GEVR_MODAI_STANDARD },
    { "simple", GEVR_MODAI_SIMPLE },
    { "deaf", GEVR_MODAI_DEAF },
    { "chase", GEVR_MODAI_CHASE },
};

/* A whole number field of the table at t, lo to hi: 1 with it in *out, 0 when
 * the field is absent; past them, the caller's message as an error */
static int wholeField(lua_State *L, int t, const char *field, lua_Integer lo, lua_Integer hi, int *out,
                      const char *call, const char *what)
{
    int whole = 0;
    lua_Integer v = 0;
    const int type = lua_getfield(L, t, field);

    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return 0;
    }
    if (type == LUA_TNUMBER) v = lua_tointegerx(L, -1, &whole);
    lua_pop(L, 1);
    if (!whole || v < lo || v > hi) luaL_error(L, "%s: %s is %s", call, field, what);
    *out = (int)v;
    return 1;
}

/* A gun field (a name or id from mod.items), -1 absent */
static int gunField(lua_State *L, int t, const char *field, const char *call)
{
    int item = -1;

    if (lua_getfield(L, t, field) != LUA_TNIL) {
        item = lookup(L, -1, s_itemNames, 0, GEVR_MODAPI_ITEMS);
        if (item < 0 || !gevrModGameGuardGun(item)) luaL_error(L, "%s: %s is a gun (mod.items)", call, field);
    }
    lua_pop(L, 1);
    return item;
}

/* spawn{pad, body, head, ai, weapon, weaponLeft}: the new guard's id; false
 * where the guards don't run (a co-op client), outside a stage, or when the
 * game can't (no room at the pad, no free slot) */
static int l_guardSpawn(lua_State *L)
{
    static const char call[] = "mod.guards.spawn";
    GevrModGuardOrder o;

    luaL_checktype(L, 1, LUA_TTABLE);
    if (!wholeField(L, 1, "pad", 0, 0x7FFF, &o.pad, call, "a setup pad's number (10000 and up: a bound pad's)")) {
        return luaL_error(L, "%s: pad is a setup pad's number (10000 and up: a bound pad's)", call);
    }
    if (!wholeField(L, 1, "body", 0, 0x7FFF, &o.body, call, "one of the cartridge's bodies (not a head)")
        || !gevrModGameBodyOk(o.body)) {
        return luaL_error(L, "%s: body is one of the cartridge's bodies (not a head)", call);
    }
    o.head = -1;
    if (wholeField(L, 1, "head", 0, 0x7FFF, &o.head, call, "one of the cartridge's heads")) {
        if (gevrModGameBodyOk(o.body) == 2) return luaL_error(L, "%s: that body has its own head", call);
        if (!gevrModGameHeadOk(o.head)) return luaL_error(L, "%s: head is one of the cartridge's heads", call);
    }
    o.ai = GEVR_MODAI_STANDARD;
    if (lua_getfield(L, 1, "ai") == LUA_TSTRING) {
        const char *name = lua_tostring(L, -1);
        size_t i;

        for (i = 0; i < sizeof(s_aiNames) / sizeof(s_aiNames[0]) && strcmp(name, s_aiNames[i].name); i++) {
        }
        if (i == sizeof(s_aiNames) / sizeof(s_aiNames[0])) {
            return luaL_error(L, "%s: ai is \"standard\", \"simple\", \"deaf\", \"chase\" or a level's guard list id", call);
        }
        o.ai = s_aiNames[i].id;
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
        if (wholeField(L, 1, "ai", 0, 0x7FFF, &o.ai, call, "\"standard\", \"simple\", \"deaf\", \"chase\" or a level's guard list id")
            && !gevrModGameAiListOk(o.ai)) {
            return luaL_error(L, "%s: AI list %d isn't a guard's list in this stage", call, o.ai);
        }
    }
    o.weapon[0] = gunField(L, 1, "weapon", call);
    o.weapon[1] = gunField(L, 1, "weaponLeft", call);
    if (!gevrModGameRunsGuards()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    {
        const int id = gevrModGameSpawnGuard(&o);

        if (id < 0) lua_pushboolean(L, 0);
        else lua_pushinteger(L, id);
    }
    return 1;
}

/* giveWeapon(id, item, hand): true when the guard holds it */
static int l_guardGiveWeapon(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1);
    const int item = itemArg(L, 2);
    const int hand = lua_isnoneornil(L, 3) ? 0 : handArg(L, 3);

    if (!gevrModGameGuardGun(item)) {
        return luaL_error(L, "mod.guards.giveWeapon: %s isn't a gun a guard can hold", s_itemNames[item]);
    }
    lua_pushboolean(L, id >= -32768 && id <= 32767 && gevrModGameRunsGuards()
                           && gevrModGameGiveGuardWeapon((int)id, item, hand));
    return 1;
}

/* patrol(id, path): true when the guard set out along it */
static int l_guardPatrol(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1);
    const lua_Integer path = luaL_checkinteger(L, 2);

    lua_pushboolean(L, id >= -32768 && id <= 32767 && path >= 0 && path <= 255 && gevrModGameRunsGuards()
                           && gevrModGamePatrol((int)id, (int)path));
    return 1;
}

/* paths(): the stage's patrol paths' ids */
static int l_guardPaths(lua_State *L)
{
    int ids[256];
    const int n = gevrModGamePaths(ids, 256);
    int i;

    lua_createtable(L, n, 0);
    for (i = 0; i < n; i++) {
        lua_pushinteger(L, ids[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* goTo(id, pad[, speed]): the guard walks ("walk", the default), runs or
 * sprints to the pad by the game's own route (chrGoToPad); true when it set
 * off. Where the guards run (solo, the co-op host) */
static int l_guardGoTo(lua_State *L)
{
    static const char *const speeds[] = { "walk", "run", "sprint", NULL };
    const lua_Integer id = luaL_checkinteger(L, 1);
    const lua_Integer pad = luaL_checkinteger(L, 2);
    const int speed = luaL_checkoption(L, 3, "walk", speeds);

    lua_pushboolean(L, id >= -32768 && id <= 32767 && pad >= 0 && pad <= 0x7fffffff && gevrModGameRunsGuards()
                           && gevrModGameGoTo((int)id, (int)pad, speed));
    return 1;
}

/* face(id, pad): the guard stands and turns to face the pad (as
 * AI_TRYFacingTarget); true when it did */
static int l_guardFace(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1);
    const lua_Integer pad = luaL_checkinteger(L, 2);

    lua_pushboolean(L, id >= -32768 && id <= 32767 && pad >= 0 && pad <= 0x7fffffff && gevrModGameRunsGuards()
                           && gevrModGameFace((int)id, (int)pad));
    return 1;
}

/* giveHat(id, hat): one of the cartridge's hats on a living guard that has
 * none, as the game's own scripts give one (AI_TRYGiveMeHat); true when
 * it's on. A pack's model in the hat's place (assets/models) changes its look */
static int l_guardGiveHat(lua_State *L)
{
    static const char *const hats[GEVR_MODAPI_HATS + 1] = {
        "fur", "furbrown", "furblack", "sidecap", "sidecapbrown", "helmet", "helmetgrey", "moon",
        "beret", "beretblue", "beretred", "peaked", NULL,
    };
    const lua_Integer id = luaL_checkinteger(L, 1);
    const int hat = luaL_checkoption(L, 2, NULL, hats);

    lua_pushboolean(L, id >= -32768 && id <= 32767 && gevrModGameRunsGuards() && gevrModGameGiveHat((int)id, hat));
    return 1;
}

static const luaL_Reg s_guardFuncs[] = {
    { "list", l_guardList },
    { "get", l_guardGet },
    { "spawn", l_guardSpawn },
    { "giveWeapon", l_guardGiveWeapon },
    { "patrol", l_guardPatrol },
    { "paths", l_guardPaths },
    { "goTo", l_guardGoTo },
    { "face", l_guardFace },
    { "giveHat", l_guardGiveHat },
    { NULL, NULL },
};

/* ---- mod.pads ---- */

/* count(): the stage's pads (ids 0 up) and its bound pads (ids 10000 up) */
static int l_padCount(lua_State *L)
{
    int bound = 0;
    const int n = gevrModGamePadCount(&bound);

    lua_pushinteger(L, n);
    lua_pushinteger(L, bound);
    return 2;
}

/* get(id): { x, y, z, room, look = { x, y, z } } in world units, or nil */
static int l_padGet(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1);
    GevrModPad p;
    int i;

    if (id < 0 || id > 0x7fffffff || !gevrModGamePad((int)id, &p)) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 5);
    lua_pushnumber(L, p.pos[0]);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, p.pos[1]);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, p.pos[2]);
    lua_setfield(L, -2, "z");
    if (p.room >= 0) {
        lua_pushinteger(L, p.room);
        lua_setfield(L, -2, "room");
    }
    lua_createtable(L, 3, 0);
    for (i = 0; i < 3; i++) {
        lua_pushnumber(L, p.look[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "look");
    return 1;
}

/* ---- mod.mission ---- */

/* difficulty(): "agent", "secretagent", "00agent" or "007"; nil without one
 * (multiplayer) */
static int l_missionDifficulty(lua_State *L)
{
    static const char *const names[] = { "agent", "secretagent", "00agent", "007" };
    const int d = gevrModGameDifficulty();

    if (d < 0 || d > 3) lua_pushnil(L);
    else lua_pushstring(L, names[d]);
    return 1;
}

/* folder(): the save folder the run plays from, 1 to 4 as the folder screen
 * shows them, or nil */
static int l_missionFolder(lua_State *L)
{
    const int f = gevrModGameFolder();

    if (f < 0 || f > 3) lua_pushnil(L);
    else lua_pushinteger(L, f + 1);
    return 1;
}

/* day(): today's number, whole days since 1 January 1970 (UTC), for daily
 * challenges; each headset reads its own clock */
static int l_missionDay(lua_State *L)
{
    const time_t now = time(NULL);

    lua_pushinteger(L, now > 0 ? (lua_Integer)(now / 86400) : 0);
    return 1;
}

/*
 * complete(mission, difficulty[, seconds]): the mission (1 Dam to 20 Egypt, the
 * folder screen's order) marked complete at that difficulty, and those below
 * it, in the run's save folder, with its time in seconds if given. Only in a
 * mod campaign, which saves to its own file, never the base game's; true when
 * written, false outside one or with no folder chosen.
 */
static int l_missionComplete(lua_State *L)
{
    static const char *const names[] = { "agent", "secretagent", "00agent", "007", NULL };
    const lua_Integer mission = luaL_checkinteger(L, 1);
    const int difficulty = luaL_checkoption(L, 2, NULL, names);
    lua_Integer seconds = 0;

    if (mission < 1 || mission > GEVR_MODAPI_MISSIONS) return luaL_argerror(L, 1, "a mission is 1 (Dam) to 20 (Egypt)");
    if (!lua_isnoneornil(L, 3)) {
        seconds = luaL_checkinteger(L, 3);
        if (seconds < 1 || seconds >= GEVR_MODAPI_NO_TIME) return luaL_argerror(L, 3, "a time is 1 to 99999998 seconds");
    }
    lua_pushboolean(L, gevrModGameComplete((int)mission - 1, difficulty, (int)seconds));
    return 1;
}

static const luaL_Reg s_missionFuncs[] = {
    { "difficulty", l_missionDifficulty },
    { "folder", l_missionFolder },
    { "day", l_missionDay },
    { "complete", l_missionComplete },
    { NULL, NULL },
};

static const luaL_Reg s_padFuncs[] = {
    { "count", l_padCount },
    { "get", l_padGet },
    { NULL, NULL },
};

/* ---- mod.player ---- */

/* health(): this machine's player's health and armour, 0 to 1, or nil */
static int l_playerHealth(lua_State *L)
{
    float health, armour;

    if (!gevrModGamePlayerHealth(&health, &armour)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, health);
    lua_pushnumber(L, armour);
    return 2;
}

/* setHealth(x): more than 0, up to 1; false when dead, down or not in a stage */
static int l_playerSetHealth(lua_State *L)
{
    const lua_Number v = luaL_checknumber(L, 1);

    if (!(v > 0 && v <= 1)) return luaL_argerror(L, 1, "health is more than 0, up to 1 (a full bar)");
    lua_pushboolean(L, gevrModGameSetPlayerHealth((float)v));
    return 1;
}

/* setArmour(x): 0 to 1 */
static int l_playerSetArmour(lua_State *L)
{
    const lua_Number v = luaL_checknumber(L, 1);

    if (!(v >= 0 && v <= 1)) return luaL_argerror(L, 1, "armour is 0 to 1 (a full vest)");
    lua_pushboolean(L, gevrModGameSetPlayerArmour((float)v));
    return 1;
}

/* setSpeed(scale): the local player's walking and strafing, times scale
 * (0.25 to 2; 1 the game's own), until the mission ends or the mod stops */
static int l_playerSetSpeed(lua_State *L)
{
    const lua_Number v = luaL_checknumber(L, 1);

    if (!(v >= GEVR_MODAPI_SPEED_MIN && v <= GEVR_MODAPI_SPEED_MAX)) {
        return luaL_argerror(L, 1, "a speed is 0.25 to 2 times the game's own");
    }
    gevrModGameSetSpeed((float)v);
    return 0;
}

static const luaL_Reg s_playerFuncs[] = {
    { "health", l_playerHealth },
    { "setHealth", l_playerSetHealth },
    { "setArmour", l_playerSetArmour },
    { "setSpeed", l_playerSetSpeed },
    { NULL, NULL },
};

/* ---- mod.audio, mod.effect: the cartridge's own, on this headset ---- */

/* A sound id from 1 to the bank's count */
static int soundArg(lua_State *L, int arg)
{
    const lua_Integer id = luaL_checkinteger(L, arg);

    if (id < 1 || id > gevrModGameSoundCount()) {
        return luaL_argerror(L, arg, "a sound is an id from 1 to mod.audio.count()");
    }
    return (int)id;
}

/* An optional volume, 0 to 1; -1 when not given (the sound's own) */
static float volumeArg(lua_State *L, int arg)
{
    lua_Number v;

    if (lua_isnoneornil(L, arg)) return -1.0f;
    v = luaL_checknumber(L, arg);
    if (!(v >= 0 && v <= 1)) return (float)luaL_argerror(L, arg, "a volume is 0 to 1");
    return (float)v;
}

/* count(): the sound effects bank's ids, 1 to it */
static int l_audioCount(lua_State *L)
{
    lua_pushinteger(L, gevrModGameSoundCount());
    return 1;
}

/* play(id, volume): heard as the game's own are, not from a place */
static int l_audioPlay(lua_State *L)
{
    const int id = soundArg(L, 1);
    const float volume = volumeArg(L, 2);

    lua_pushboolean(L, gevrModGamePlaySound(id, volume, NULL));
    return 1;
}

/* playAt(id, x, y, z, volume): heard from there (world units) */
static int l_audioPlayAt(lua_State *L)
{
    const int id = soundArg(L, 1);
    float pos[3];
    int i;
    float volume;

    for (i = 0; i < 3; i++) {
        const lua_Number v = luaL_checknumber(L, 2 + i);

        if (!(v >= -1e7 && v <= 1e7)) return luaL_argerror(L, 2 + i, "a place is in world units");
        pos[i] = (float)v;
    }
    volume = volumeArg(L, 5);
    lua_pushboolean(L, gevrModGamePlaySound(id, volume, pos));
    return 1;
}

static const luaL_Reg s_audioFuncs[] = {
    { "count", l_audioCount },
    { "play", l_audioPlay },
    { "playAt", l_audioPlayAt },
    { NULL, NULL },
};

/* The smoke a mod may raise: g_SmokeTypes' rows by name */
static const struct {
    const char *name;
    int type;
} s_effects[] = {
    { "puff", 7 }, { "smoke_small", 2 }, { "smoke", 1 }, { "smoke_light", 3 },
    { "smoke_dark", 4 }, { "smoke_large", 5 }, { "smoke_huge", 6 },
};

/* play(name, at): at is "player", a guard's id, or { pad = n } */
static int l_effectPlay(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    size_t i;
    int at, which = 0;

    for (i = 0; i < sizeof(s_effects) / sizeof(s_effects[0]) && strcmp(name, s_effects[i].name); i++) {
    }
    if (i == sizeof(s_effects) / sizeof(s_effects[0])) {
        return luaL_argerror(L, 1, "an effect is \"puff\", \"smoke_small\", \"smoke\", \"smoke_light\", \"smoke_dark\", "
                                   "\"smoke_large\" or \"smoke_huge\"");
    }
    if (lua_type(L, 2) == LUA_TSTRING && !strcmp(lua_tostring(L, 2), "player")) {
        at = GEVR_MODAT_PLAYER;
    } else if (lua_type(L, 2) == LUA_TNUMBER) {
        const lua_Integer id = luaL_checkinteger(L, 2);

        if (id < -32768 || id > 32767) {
            lua_pushboolean(L, 0);
            return 1;
        }
        at = GEVR_MODAT_GUARD;
        which = (int)id;
    } else if (lua_istable(L, 2)) {
        if (!wholeField(L, 2, "pad", 0, 0x7FFF, &which, "mod.effect.play", "a setup pad's number (10000 and up: a bound pad's)")) {
            return luaL_argerror(L, 2, "a place is \"player\", a guard's id or { pad = n }");
        }
        at = GEVR_MODAT_PAD;
    } else {
        return luaL_argerror(L, 2, "a place is \"player\", a guard's id or { pad = n }");
    }
    lua_pushboolean(L, gevrModGameSmoke(s_effects[i].type, at, which));
    return 1;
}

/* blood({ headBurst = percent, bodies = { body rows } }) or blood(false). */
static int l_effectBlood(lua_State *L)
{
    if (lua_isboolean(L, 1) && !lua_toboolean(L, 1)) {
        lua_pushboolean(L, gevrModGameBlood(NULL));
        return 1;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    GevrModBloodConfig config = {0};
    wholeField(L, 1, "headBurst", 0, 100, &config.headBurst, "mod.effect.blood", "a percentage from 0 to 100");
    lua_getfield(L, 1, "bodies");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TTABLE);
        const lua_Unsigned count = lua_rawlen(L, -1);
        if (count > GEVR_MOD_BLOOD_BODIES) return luaL_argerror(L, 1, "too many body rows");
        for (lua_Unsigned i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, (lua_Integer)i);
            int valid;
            const lua_Integer body = lua_tointegerx(L, -1, &valid);
            if (!valid || body < 0 || body >= GEVR_MOD_BLOOD_BODIES)
                return luaL_argerror(L, 1, "bodies must contain integer body rows from 0 to 255");
            config.bodies[body] = 1;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_pushboolean(L, gevrModGameBlood(&config));
    return 1;
}

static const luaL_Reg s_effectFuncs[] = {
    { "play", l_effectPlay },
    { "blood", l_effectBlood },
    { NULL, NULL },
};

/* ---- mod.net ---- */

/* A channel: 1 to GEVR_MODAPI_NET_CHANNEL printable characters, no spaces */
static const char *channelArg(lua_State *L, int arg)
{
    size_t n, i;
    const char *c = luaL_checklstring(L, arg, &n);

    for (i = 0; i < n && c[i] > 0x20 && c[i] < 0x7f; i++) {
    }
    if (n == 0 || n > GEVR_MODAPI_NET_CHANNEL || i != n) {
        luaL_argerror(L, arg, "a channel is 1 to 32 printable characters, no spaces");
    }
    return c;
}

/* What each mod sent this second (sendAllowed); four, as many mods as the
 * script host runs (gevr_modscript.c MAX_MODS) */
static struct {
    char id[64];
    unsigned long long since;
    int sends, bytes;
} s_sent[4];

static int sendAllowed(const char *id, int len)
{
    const unsigned long long now = sysGetMicroseconds();
    int k, unused = -1;

    for (k = 0; k < 4 && strcmp(s_sent[k].id, id) != 0; k++) {
        if (unused < 0 && !s_sent[k].id[0]) unused = k;
    }
    if (k == 4) {
        if (unused < 0) return 0;
        k = unused;
        snprintf(s_sent[k].id, sizeof(s_sent[k].id), "%s", id);
        s_sent[k].since = now;
    }
    if (now - s_sent[k].since >= 1000000ull) {
        s_sent[k].since = now;
        s_sent[k].sends = 0;
        s_sent[k].bytes = 0;
    }
    if (s_sent[k].sends + 1 > GEVR_MODAPI_NET_SENDS || s_sent[k].bytes + len > GEVR_MODAPI_NET_BYTES) return 0;
    s_sent[k].sends++;
    s_sent[k].bytes += len;
    return 1;
}

/* send(channel, data, to): to everyone else, or the player in slot to; false
 * in solo, outside a game, past the rate (30 a second, 16 KB) or to no one */
static int l_netSend(lua_State *L)
{
    const char *channel = channelArg(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    int to = 0xFF;

    if (!lua_isnoneornil(L, 3)) {
        const lua_Integer t = luaL_checkinteger(L, 3);

        if (t < 0 || t > 7) return luaL_argerror(L, 3, "to is a player's slot, 0 to 7");
        to = (int)t;
    }
    if (len > GEVR_MODAPI_NET_PAYLOAD) return luaL_argerror(L, 2, "a message is at most 1024 bytes");
    lua_pushboolean(L, netIsActive() && to != netGetLocalSlot() && sendAllowed(gevrModScriptIdOf(L), (int)len)
                           && netSendModMessage(GEVR_MODNET_DATA, to, gevrModScriptIdOf(L), channel, data, (int)len));
    return 1;
}

/* A handler for one channel: net_message's arguments (channel, data, from)
 * to fn(data, from) when the channel is its own */
static int netFilter(lua_State *L)
{
    if (lua_rawequal(L, 1, lua_upvalueindex(1))) {
        lua_pushvalue(L, lua_upvalueindex(2));
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
    }
    return 0;
}

/* on(channel, fn): fn(data, from) for each message on the channel; from is
 * the sender's slot, nil a dedicated server */
static int l_netOn(lua_State *L)
{
    size_t n;

    channelArg(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushcclosure(L, netFilter, 2);
    /* into the host's own lists, as mod.on keeps them: an event mods reach only through here */
    lua_getfield(L, LUA_REGISTRYINDEX, "gevr.on");
    if (lua_getfield(L, -1, "net_message") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "net_message");
    }
    n = (size_t)lua_rawlen(L, -1);
    lua_pushvalue(L, -3);
    lua_rawseti(L, -2, (lua_Integer)n + 1);
    return 0;
}

/* role(): "solo", "host", "client" or "server" (a dedicated server) */
static int l_netRole(lua_State *L)
{
    lua_pushstring(L, !netIsActive() ? "solo" : netIsDedicated() ? "server" : netIsHost() ? "host" : "client");
    return 1;
}

/* slot(): this machine's player's slot (0 in solo), nil on a dedicated server */
static int l_netSlot(lua_State *L)
{
    const int slot = netIsActive() ? netGetLocalSlot() : 0;

    if (slot < 0) lua_pushnil(L);
    else lua_pushinteger(L, slot);
    return 1;
}

static const luaL_Reg s_netFuncs[] = {
    { "send", l_netSend },
    { "on", l_netOn },
    { "role", l_netRole },
    { "slot", l_netSlot },
    { NULL, NULL },
};

/* ---- mod.ui ---- */

/* input.c: the in-game screen (port/vr/vr_pause_menu.cpp) */
extern int gevrPanelAvailable(void);   /* this build has it (not the dedicated server) */
extern void gevrPanelOpenAsk(void);    /* open it as the hold does (a solo mission pauses) */
extern void gevrPanelCloseAsk(void);   /* close it as Resume does */

static GevrModUiPage s_pages[GEVR_MODAPI_UI_PAGES];
static int s_pageCount;
static int s_uiAsk = -1;   /* the s_pages entry mod.ui.open asked for */

/* Text: printable ASCII, spaces too (a name: no spaces), 1 to max characters */
static int uiTextOk(const char *s, size_t n, size_t max, int name)
{
    size_t i;

    if (n == 0 || n > max) return 0;
    for (i = 0; i < n; i++) {
        if (s[i] < (name ? 0x21 : 0x20) || s[i] > 0x7e) return 0;
    }
    return 1;
}

/* A text field of the table at t into out: 1 when there, 0 when absent and
 * not needed; anything else the caller's message as an error */
static int uiField(lua_State *L, int t, const char *field, char *out, size_t max, int name, int needed, const char *what)
{
    size_t n = 0;
    const char *s;
    const int type = lua_getfield(L, t, field);

    out[0] = '\0';
    if (type == LUA_TNIL && !needed) {
        lua_pop(L, 1);
        return 0;
    }
    s = type == LUA_TSTRING ? lua_tolstring(L, -1, &n) : NULL;
    if (!s || !uiTextOk(s, n, max, name)) luaL_error(L, "mod.ui.page: %s is %s", field, what);
    memcpy(out, s, n);
    out[n] = '\0';
    lua_pop(L, 1);
    return 1;
}

/* blocked: absent, false, or the reason (a string) the row or button can't be used now */
static void uiBlocked(lua_State *L, int t, char *out)
{
    const int type = lua_getfield(L, t, "blocked");
    const int on = lua_toboolean(L, -1);

    lua_pop(L, 1);
    out[0] = '\0';
    if (type == LUA_TNIL || (type == LUA_TBOOLEAN && !on)) return;
    if (type == LUA_TBOOLEAN) {   /* true: no reason given */
        snprintf(out, GEVR_MODAPI_UI_REASON + 1, "Not now");
        return;
    }
    uiField(L, t, "blocked", out, GEVR_MODAPI_UI_REASON, 0, 1, "false, or the reason it can't be used now (1 to 32 characters)");
}

static void uiRow(lua_State *L, int t, GevrModUiRow *r, int index)
{
    char kind[16];
    int k;

    memset(r, 0, sizeof(*r));
    if (!lua_istable(L, t)) luaL_error(L, "mod.ui.page: rows[%d] is a table", index);
    uiField(L, t, "kind", kind, sizeof(kind) - 1, 1, 1, "\"toggle\", \"choice\" or \"slider\"");
    r->kind = !strcmp(kind, "toggle") ? GEVR_MODUI_TOGGLE : !strcmp(kind, "choice") ? GEVR_MODUI_CHOICE
            : !strcmp(kind, "slider") ? GEVR_MODUI_SLIDER : 0;
    if (!r->kind) luaL_error(L, "mod.ui.page: kind is \"toggle\", \"choice\" or \"slider\"");
    uiField(L, t, "id", r->id, GEVR_MODAPI_UI_ID, 1, 1, "1 to 32 printable characters, no spaces");
    uiField(L, t, "label", r->label, GEVR_MODAPI_UI_LABEL, 0, 1, "1 to 24 printable characters");
    uiField(L, t, "tip", r->tip, GEVR_MODAPI_UI_TEXT, 0, 0, "1 to 160 printable characters");
    uiBlocked(L, t, r->blocked);
    if (r->kind == GEVR_MODUI_TOGGLE) {
        const int type = lua_getfield(L, t, "value");

        if (type != LUA_TNIL && type != LUA_TBOOLEAN) luaL_error(L, "mod.ui.page: a toggle's value is true or false");
        r->value = lua_toboolean(L, -1);
        lua_pop(L, 1);
    } else if (r->kind == GEVR_MODUI_CHOICE) {
        if (lua_getfield(L, t, "choices") != LUA_TTABLE) luaL_error(L, "mod.ui.page: a choice has choices, 2 to 12 names");
        r->count = (int)lua_rawlen(L, -1);
        if (r->count < 2 || r->count > GEVR_MODAPI_UI_CHOICES) luaL_error(L, "mod.ui.page: a choice has choices, 2 to 12 names");
        for (k = 0; k < r->count; k++) {
            size_t n = 0;
            const char *s = lua_rawgeti(L, -1, k + 1) == LUA_TSTRING ? lua_tolstring(L, -1, &n) : NULL;

            if (!s || !uiTextOk(s, n, GEVR_MODAPI_UI_LABEL, 0)) {
                luaL_error(L, "mod.ui.page: each choice is 1 to 24 printable characters");
            }
            memcpy(r->choices[k], s, n);
            r->choices[k][n] = '\0';
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        if (wholeField(L, t, "value", 1, r->count, &r->value, "mod.ui.page", "a choice's number, 1 to its count")) r->value--;
    } else {
        if (!wholeField(L, t, "min", -GEVR_MODAPI_UI_RANGE, GEVR_MODAPI_UI_RANGE, &r->min, "mod.ui.page",
                        "a whole number, -100000 to 100000")
            || !wholeField(L, t, "max", -GEVR_MODAPI_UI_RANGE, GEVR_MODAPI_UI_RANGE, &r->max, "mod.ui.page",
                           "a whole number, -100000 to 100000")) {
            luaL_error(L, "mod.ui.page: a slider has min and max");
        }
        if (r->max <= r->min || r->max - r->min > GEVR_MODAPI_UI_STEPS) {
            luaL_error(L, "mod.ui.page: a slider's max is above its min, at most 1000 steps");
        }
        r->value = r->min;
        wholeField(L, t, "value", r->min, r->max, &r->value, "mod.ui.page", "a whole number from min to max");
    }
}

/* page(id, {title, text, rows, buttons}): a sub-tab under the in-game
 * screen's Mods/Fun, made or replaced (true), or false when the four pages
 * are taken */
static int l_uiPage(lua_State *L)
{
    const char *mod = gevrModScriptIdOf(L);
    GevrModUiPage p;
    size_t n;
    const char *id = luaL_checklstring(L, 1, &n);
    int i, k, at = -1;

    if (!uiTextOk(id, n, GEVR_MODAPI_UI_ID, 1)) return luaL_argerror(L, 1, "a page's id is 1 to 32 printable characters, no spaces");
    luaL_checktype(L, 2, LUA_TTABLE);
    memset(&p, 0, sizeof(p));
    snprintf(p.mod, sizeof(p.mod), "%s", mod);
    memcpy(p.id, id, n);
    uiField(L, 2, "title", p.title, GEVR_MODAPI_UI_TITLE, 0, 1, "1 to 12 printable characters");
    uiField(L, 2, "text", p.text, GEVR_MODAPI_UI_TEXT, 0, 0, "1 to 160 printable characters");
    if (lua_getfield(L, 2, "rows") != LUA_TNIL) {
        if (!lua_istable(L, -1) || lua_rawlen(L, -1) > GEVR_MODAPI_UI_ROWS) luaL_error(L, "mod.ui.page: rows is a list of at most 8");
        p.rows = (int)lua_rawlen(L, -1);
        for (i = 0; i < p.rows; i++) {
            lua_rawgeti(L, -1, i + 1);
            uiRow(L, lua_gettop(L), &p.row[i], i + 1);
            lua_pop(L, 1);
            for (k = 0; k < i; k++) {
                if (!strcmp(p.row[k].id, p.row[i].id)) luaL_error(L, "mod.ui.page: two rows have the id %s", p.row[i].id);
            }
        }
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "buttons") != LUA_TNIL) {
        if (!lua_istable(L, -1) || lua_rawlen(L, -1) > GEVR_MODAPI_UI_BUTTONS) {
            luaL_error(L, "mod.ui.page: buttons is a list of at most 4");
        }
        p.buttons = (int)lua_rawlen(L, -1);
        for (i = 0; i < p.buttons; i++) {
            const int t = lua_gettop(L) + 1;

            if (lua_rawgeti(L, -1, i + 1) != LUA_TTABLE) luaL_error(L, "mod.ui.page: buttons[%d] is a table", i + 1);
            uiField(L, t, "id", p.button[i].id, GEVR_MODAPI_UI_ID, 1, 1, "1 to 32 printable characters, no spaces");
            uiField(L, t, "label", p.button[i].label, GEVR_MODAPI_UI_BUTTON, 0, 1, "1 to 14 printable characters");
            uiBlocked(L, t, p.button[i].blocked);
            lua_pop(L, 1);
            for (k = 0; k < i; k++) {
                if (!strcmp(p.button[k].id, p.button[i].id)) luaL_error(L, "mod.ui.page: two buttons have the id %s", p.button[i].id);
            }
        }
    }
    lua_pop(L, 1);
    for (i = 0; i < s_pageCount; i++) {
        if (!strcmp(s_pages[i].mod, mod) && !strcmp(s_pages[i].id, p.id)) at = i;
    }
    if (at < 0) {
        /* every mod's together, so no mod can know: false, not an error */
        if (s_pageCount == GEVR_MODAPI_UI_PAGES) {
            lua_pushboolean(L, 0);
            return 1;
        }
        at = s_pageCount++;
    }
    s_pages[at] = p;
    lua_pushboolean(L, 1);
    return 1;
}

static int ownPage(lua_State *L, int arg)
{
    const char *mod = gevrModScriptIdOf(L);
    const char *id = luaL_checkstring(L, arg);
    int i;

    for (i = 0; i < s_pageCount; i++) {
        if (!strcmp(s_pages[i].mod, mod) && !strcmp(s_pages[i].id, id)) return i;
    }
    return -1;
}

/* remove(id): true when the mod had that page */
static int l_uiRemove(lua_State *L)
{
    const int at = ownPage(L, 1);

    if (at >= 0) {
        memmove(&s_pages[at], &s_pages[at + 1], sizeof(s_pages[0]) * (size_t)(s_pageCount - at - 1));
        s_pageCount--;
        s_uiAsk = s_uiAsk == at ? -1 : s_uiAsk > at ? s_uiAsk - 1 : s_uiAsk;
    }
    lua_pushboolean(L, at >= 0);
    return 1;
}

/* open(id): the in-game screen on that page (a solo mission pauses, as the
 * hold's does); false where there's no screen (the dedicated server) */
static int l_uiOpen(lua_State *L)
{
    const int at = ownPage(L, 1);

    if (at < 0) return luaL_argerror(L, 1, "no page of this mod's has that id (mod.ui.page)");
    if (!gevrPanelAvailable() || netIsDedicated()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    s_uiAsk = at;
    gevrPanelOpenAsk();
    lua_pushboolean(L, 1);
    return 1;
}

/* close(): the in-game screen closed, as Resume closes it */
static int l_uiClose(lua_State *L)
{
    (void)L;
    s_uiAsk = -1;
    if (gevrPanelAvailable()) gevrPanelCloseAsk();
    return 0;
}

/* ---- mod.attach ---- */

/* which mod attached each (remove and show answer only to it), as ids come back */
static struct { int id; char mod[64]; } s_attachOwners[GEVR_MODAPI_ATTACH_MAX];

_Static_assert(GEVR_MODATTACH_GUN == GEVR_ATTNET_GUN && GEVR_MODATTACH_HAND == GEVR_ATTNET_HAND
               && GEVR_MODATTACH_BODY == GEVR_ATTNET_BODYSLOT && GEVR_MODAPI_ATTACH_MAX == GEVR_ATTNET_OWNED
               && GEVR_MODAPI_ANCHORS == GEVR_ATTNET_ANCHORS && GEVR_MODAPI_NET_PAYLOAD == GEVR_ATTNET_BYTES,
               "mod.attach's limits as the network's");

static void attachNetForget(void);

/* Online, what this player wears is told to the others (#335,
 * gevr_modattach_net.c): on the gun, a hand or the body, never a guard's
 * (each headset's mod puts the same on its own guards) */
static void attachTell(const char *mod, int id, const GevrModAttach *a)
{
    GevrAttachRecord r;
    int i, j;

    if (!netIsActive() || a->on == GEVR_MODATTACH_GUARD) return;
    memset(&r, 0, sizeof(r));
    r.op = GEVR_ATTNET_SET;
    r.id = id;
    r.on = a->on;
    r.where = a->where;
    r.node = a->on == GEVR_MODATTACH_GUN ? a->node : 0;
    r.shown = 1;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) r.frame[i][j] = a->offset[i][j];
    }
    if (a->on == GEVR_MODATTACH_BODY && !gevrModGameAttachBodyAt(a->where, r.body)) memset(r.body, 0, sizeof(r.body));
    snprintf(r.model, sizeof(r.model), "%s", a->model);
    snprintf(r.like, sizeof(r.like), "%s", a->like);
    gevrAttachNetSet(mod, &r);
}

static void attachName(lua_State *L, const char *key, char *out, size_t cap, int optional)
{
    size_t n;
    const char *s;

    const int type = lua_getfield(L, 1, key);
    if (optional && type == LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    if (type != LUA_TSTRING) luaL_error(L, "mod.attach.add: %s is a model file's name", key);
    s = lua_tolstring(L, -1, &n);
    if (!modelNameOk(s, n)) luaL_error(L, "mod.attach.add: %s requires a flat model file name", key);
    snprintf(out, cap, "%s", s);
    lua_pop(L, 1);
}

/* an optional { a, b, c } field of table t within [lo, hi] */
static void attachTriple(lua_State *L, int t, const char *call, const char *key, float out[3], float lo, float hi,
                         const char *what)
{
    int i;

    if (lua_getfield(L, t, key) == LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1) || lua_rawlen(L, -1) != 3) luaL_error(L, "%s: %s", call, what);
    for (i = 0; i < 3; i++) {
        int ok;
        lua_Number v;

        lua_rawgeti(L, -1, i + 1);
        v = lua_tonumberx(L, -1, &ok);
        lua_pop(L, 1);
        if (!ok || !(v >= lo && v <= hi)) luaL_error(L, "%s: %s", call, what);
        out[i] = (float)v;
    }
    lua_pop(L, 1);
}

/* The offset as the game lays a model in a frame (Mtxf rows: the model's X, Y
 * and Z axes, then its place): turned about the anchor's X, then Y, then Z,
 * scaled, at at */
static void attachOffset(float out[4][4], const float at[3], const float turn[3], float scale)
{
    const double r = 3.14159265358979 / 180.0;
    const double cx = cos(turn[0] * r), sx = sin(turn[0] * r), cy = cos(turn[1] * r), sy = sin(turn[1] * r);
    const double cz = cos(turn[2] * r), sz = sin(turn[2] * r);
    /* M = Rz Ry Rx: an axis e_i goes to M's column i */
    const double m[3][3] = {
        { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx },
        { sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx },
        { -sy, cy * sx, cy * cx },
    };
    int i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) out[i][j] = (float)(m[j][i] * scale);
        out[i][3] = 0.0f;
        out[3][i] = at[i];
    }
    out[3][3] = 1.0f;
}

/* table t's at, turn and scale (each optional) as the offset in the anchor's frame */
static void attachPlacement(lua_State *L, int t, const char *call, float offset[4][4])
{
    float at[3] = { 0.0f, 0.0f, 0.0f }, turn[3] = { 0.0f, 0.0f, 0.0f }, scale = 1.0f;

    if (authoredFrame(L, t, call, offset)) {
        const char *const keys[] = { "at", "turn", "scale" };
        for (int n = 0; n < 3; ++n) {
            if (lua_getfield(L, t, keys[n]) != LUA_TNIL) {
                luaL_error(L, "%s: frame cannot be combined with %s", call, keys[n]);
            }
            lua_pop(L, 1);
        }
        return;
    }
    attachTriple(L, t, call, "at", at, -GEVR_MODAPI_ATTACH_RANGE, GEVR_MODAPI_ATTACH_RANGE, "at is { x, y, z }, -4000 to 4000");
    attachTriple(L, t, call, "turn", turn, -360.0f, 360.0f, "turn is { x, y, z } in degrees, -360 to 360");
    if (lua_getfield(L, t, "scale") != LUA_TNIL) {
        int ok;
        const lua_Number v = lua_tonumberx(L, -1, &ok);

        if (!ok || !(v >= GEVR_MODAPI_ATTACH_SCALE_MIN && v <= GEVR_MODAPI_ATTACH_SCALE_MAX)) {
            luaL_error(L, "%s: scale is 0.01 to 100", call);
        }
        scale = (float)v;
    }
    lua_pop(L, 1);
    attachOffset(offset, at, turn, scale);
}

/*
 * add(t): t = { model, like, on, hand, guard, part, at, turn, scale }. model
 * is the pack's assets/models/<model>, built over the cartridge's model file
 * like (a G gun or P prop file: its header). on is "gun" or "hand" (with hand
 * "right" or "left"), a body anchor (hip_gun, hip_off, back_gun, back_off,
 * chest, belt), or "guard" (with guard, its id, and part, one of the game's
 * hit parts: chest, head, pelvis...). at (model units), turn (degrees about
 * the anchor's X, Y, Z) and scale place it in the anchor's frame. Its id, or
 * nil when the model won't load, there's no such guard or part, or all are
 * attached already.
 */
static int l_attachAdd(lua_State *L)
{
    const char *mod = gevrModScriptIdOf(L);
    GevrModAttach a;
    const char *on;
    int i, id;

    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&a, 0, sizeof(a));
    attachName(L, "model", a.model, sizeof(a.model), 0);
    attachName(L, "like", a.like, sizeof(a.like), 1);
    if (lua_getfield(L, 1, "on") != LUA_TSTRING) {
        luaL_error(L, "mod.attach.add: on is \"gun\", \"hand\", a body anchor or \"guard\"");
    }
    on = lua_tostring(L, -1);
    if (!strcmp(on, "guard")) {
        int whole;
        lua_Integer g;

        if (lua_getfield(L, 1, "node") != LUA_TNIL) luaL_error(L, "mod.attach.add: node is for the gun only");
        lua_pop(L, 1);
        lua_getfield(L, 1, "guard");
        g = lua_tointegerx(L, -1, &whole);
        if (!whole || g < 0 || g > 32767) luaL_error(L, "mod.attach.add: guard is a guard's id");
        lua_pop(L, 1);
        if (lua_getfield(L, 1, "part") != LUA_TSTRING) luaL_error(L, "mod.attach.add: part is a guard's body part (chest, head...)");
        for (i = 0; i < GEVR_MODAPI_GUARD_PARTS && strcmp(lua_tostring(L, -1), s_bodyPartNames[i]); i++) {
        }
        if (i == GEVR_MODAPI_GUARD_PARTS) luaL_error(L, "mod.attach.add: no part %s (chest, head, pelvis ...)", lua_tostring(L, -1));
        lua_pop(L, 1);
        a.on = GEVR_MODATTACH_GUARD;
        a.where = (int)g;
        a.node = i + 1;   /* HIT_LEFT_FOOT 1 ... */
    } else if (!strcmp(on, "gun") || !strcmp(on, "hand")) {
        a.on = !strcmp(on, "gun") ? GEVR_MODATTACH_GUN : GEVR_MODATTACH_HAND;
        lua_getfield(L, 1, "hand");
        if (lua_type(L, -1) == LUA_TSTRING && !strcmp(lua_tostring(L, -1), "right")) a.where = 0;
        else if (lua_type(L, -1) == LUA_TSTRING && !strcmp(lua_tostring(L, -1), "left")) a.where = 1;
        else luaL_error(L, "mod.attach.add: hand is \"right\" or \"left\" for the gun or a hand");
        lua_pop(L, 1);
    } else {
        if (lua_getfield(L, 1, "node") != LUA_TNIL) luaL_error(L, "mod.attach.add: node is for the gun only");
        lua_pop(L, 1);
        for (i = 0; i < GEVR_MODAPI_ANCHORS && strcmp(on, s_anchorNames[i]); i++) {
        }
        if (i == GEVR_MODAPI_ANCHORS) luaL_error(L, "mod.attach.add: no anchor %s (gun, hand, %s ...)", on, s_anchorNames[0]);
        a.on = GEVR_MODATTACH_BODY;
        a.where = i;
    }
    lua_pop(L, 1);
    if (a.on == GEVR_MODATTACH_GUN) {
        if (lua_getfield(L, 1, "node") != LUA_TNIL) {
            int whole;
            const lua_Integer n = lua_tointegerx(L, -1, &whole);

            if (!whole || n < 0 || n >= GEVR_MODAPI_ATTACH_NODES) {
                luaL_error(L, "mod.attach.add: node is a gun model's matrix, 0 to 255");
            }
            a.node = (int)n;
        }
        lua_pop(L, 1);
    }
    attachPlacement(L, 1, "mod.attach.add", a.offset);
    needPlayer(L, "mod.attach.add");
    id = gevrModGameAttach(&a);
    if (id <= 0) {
        lua_pushnil(L);
        return 1;
    }
    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++) {
        if (s_attachOwners[i].id == 0) {
            s_attachOwners[i].id = id;
            snprintf(s_attachOwners[i].mod, sizeof(s_attachOwners[i].mod), "%s", mod);
            break;
        }
    }
    attachTell(mod, id, &a);
    lua_pushinteger(L, id);
    return 1;
}

void gevrModApiAttachGone(int id)
{
    int i;

    for (i = 0; id > 0 && i < GEVR_MODAPI_ATTACH_MAX; i++) {
        if (s_attachOwners[i].id == id) s_attachOwners[i].id = 0;
    }
}

/* this mod's attachment's place in s_attachOwners, or -1 */
static int attachOwned(lua_State *L, lua_Integer id)
{
    const char *mod = gevrModScriptIdOf(L);
    int i;

    for (i = 0; id > 0 && i < GEVR_MODAPI_ATTACH_MAX; i++) {
        if (s_attachOwners[i].id == id && !strcmp(s_attachOwners[i].mod, mod)) return i;
    }
    return -1;
}

/* remove(id): true when this mod had it */
static int l_attachRemove(lua_State *L)
{
    const int at = attachOwned(L, luaL_checkinteger(L, 1));

    if (at >= 0) {
        gevrModGameDetach(s_attachOwners[at].id);
        gevrAttachNetRemove(s_attachOwners[at].id);   /* and the others are told */
        s_attachOwners[at].id = 0;
    }
    lua_pushboolean(L, at >= 0);
    return 1;
}

/* show(id, shown): hides or shows it, kept attached; true when this mod has it */
static int l_attachShow(lua_State *L)
{
    const int at = attachOwned(L, luaL_checkinteger(L, 1));

    luaL_checktype(L, 2, LUA_TBOOLEAN);
    if (at >= 0) {
        gevrModGameAttachShow(s_attachOwners[at].id, lua_toboolean(L, 2));
        gevrAttachNetShow(s_attachOwners[at].id, lua_toboolean(L, 2));
    }
    lua_pushboolean(L, at >= 0);
    return 1;
}

/* place(id, { at, turn, scale }): moves it in its anchor's frame (a slide
 * locked back, say), from the next frame; true when this mod has it */
static int l_attachPlace(lua_State *L)
{
    const int at = attachOwned(L, luaL_checkinteger(L, 1));
    float offset[4][4];

    luaL_checktype(L, 2, LUA_TTABLE);
    attachPlacement(L, 2, "mod.attach.place", offset);
    if (at >= 0) {
        float frame[4][3];
        int i;

        gevrModGameAttachPlace(s_attachOwners[at].id, (const float (*)[4])offset);
        for (i = 0; i < 4; i++) memcpy(frame[i], offset[i], sizeof(frame[i]));
        gevrAttachNetPlace(s_attachOwners[at].id, (const float (*)[3])frame);
    }
    lua_pushboolean(L, at >= 0);
    return 1;
}

static const luaL_Reg s_attachFuncs[] = {
    { "add", l_attachAdd },
    { "remove", l_attachRemove },
    { "show", l_attachShow },
    { "place", l_attachPlace },
    { NULL, NULL },
};

static const luaL_Reg s_uiFuncs[] = {
    { "page", l_uiPage },
    { "remove", l_uiRemove },
    { "open", l_uiOpen },
    { "close", l_uiClose },
    { NULL, NULL },
};

static const luaL_Reg s_handFuncs[] = {
    { "position", l_handPosition },
    { "anchor", l_handAnchor },
    { "haptic", l_haptic },
    { NULL, NULL },
};

void gevrModApiOpen(lua_State *L)
{
    int i;

    lua_newtable(L);
    luaL_setfuncs(L, s_weaponFuncs, 0);
    lua_setfield(L, -2, "weapon");
    lua_createtable(L, 0, GEVR_MODAPI_ITEMS);
    for (i = 0; i < GEVR_MODAPI_ITEMS; i++) {
        lua_pushinteger(L, i);
        lua_setfield(L, -2, s_itemNames[i]);
    }
    lua_setfield(L, -2, "items");
    lua_newtable(L);
    luaL_setfuncs(L, s_inventoryFuncs, 0);
    lua_setfield(L, -2, "inventory");
    lua_newtable(L);   /* the calls, and the types' names to ids, as mod.items */
    luaL_setfuncs(L, s_ammoFuncs, 0);
    for (i = 1; i <= GEVR_MODAPI_AMMO_TYPES; i++) {
        lua_pushinteger(L, i);
        lua_setfield(L, -2, s_ammoNames[i]);
    }
    lua_setfield(L, -2, "ammo");
    lua_newtable(L);
    luaL_setfuncs(L, s_pickupFuncs, 0);
    lua_setfield(L, -2, "pickups");
    lua_newtable(L);
    luaL_setfuncs(L, s_handFuncs, 0);
    lua_setfield(L, -2, "hands");
    lua_newtable(L);
    luaL_setfuncs(L, s_guardFuncs, 0);
    lua_setfield(L, -2, "guards");
    lua_newtable(L);
    luaL_setfuncs(L, s_padFuncs, 0);
    lua_setfield(L, -2, "pads");
    lua_newtable(L);
    luaL_setfuncs(L, s_missionFuncs, 0);
    lua_setfield(L, -2, "mission");
    lua_newtable(L);
    luaL_setfuncs(L, s_playerFuncs, 0);
    lua_setfield(L, -2, "player");
    lua_newtable(L);
    luaL_setfuncs(L, s_audioFuncs, 0);
    lua_setfield(L, -2, "audio");
    lua_newtable(L);
    luaL_setfuncs(L, s_effectFuncs, 0);
    lua_setfield(L, -2, "effect");
    lua_newtable(L);
    luaL_setfuncs(L, s_netFuncs, 0);
    lua_setfield(L, -2, "net");
    lua_newtable(L);
    luaL_setfuncs(L, s_uiFuncs, 0);
    lua_setfield(L, -2, "ui");
    lua_newtable(L);
    luaL_setfuncs(L, s_attachFuncs, 0);
    lua_setfield(L, -2, "attach");
}

static void weaponMemosClear(int hand);

void gevrModApiReset(void)
{
    int i;

    weaponMemosClear(0);   /* a declined reload or a kept empty gun: asked afresh */
    weaponMemosClear(1);
    for (i = s_savedCount - 1; i >= 0; i--) {
        gevrModGameSetStats(s_saved[i].item, &s_saved[i].own);
    }
    s_savedCount = 0;
    gevrModGameScope(0, GEVR_MODSCOPE_GAME, 0, NULL, 0.0f, 0.0f);   /* the game's own scopes again */
    gevrModGameScope(1, GEVR_MODSCOPE_GAME, 0, NULL, 0.0f, 0.0f);
    gevrModGameGunModelReset();   /* and the hands' own models */
    gevrModGameAttachReset();     /* and nothing attached, this player's nor the others' */
    gevrModGameBlood(NULL);
    gevrModGameSetSpeed(1.0f);    /* and the player's own speed */
    memset(s_attachOwners, 0, sizeof(s_attachOwners));
    /* online, a stop in a mission is told (every headset stops with it: the
     * others clear theirs too); a mission's end isn't (each clears its own as
     * the next loads) */
    gevrAttachNetReset(netIsActive() && gevrModScriptStage() >= 0);
    attachNetForget();
}

/* ---- the weapons area's events ---- */

static int forLocal(void)
{
    return gevrModScriptRunning() && gevrModGameLocal();
}

static void handItem(GevrModArg *a, int hand, int item)
{
    a[0].kind = GEVR_MODARG_STRING;
    a[0].s = handName(hand);
    a[1].kind = GEVR_MODARG_INTEGER;
    a[1].n = item;
}

int gevrModApiFireAttempt(int hand, int item)
{
    GevrModArg a[2];

    if (!forLocal()) return 0;
    handItem(a, hand, item);
    return gevrModScriptEvent("weapon_fire_attempt", a, 2, 1);
}

void gevrModApiFired(int hand, int item)
{
    GevrModArg a[3];

    if (!forLocal()) return;
    weaponMemosClear(hand);   /* it had rounds: empty again, it's asked again */
    handItem(a, hand, item);
    a[2].kind = GEVR_MODARG_INTEGER;
    a[2].n = gevrModGameLoaded(hand);
    gevrModScriptEvent("weapon_fire", a, 3, 0);
}

/* reload_attempt's last answer per hand: the item and the reserve it declined
 * at, -1 none (gunfire.c's automatic reload isn't asked again meanwhile) */
static int s_reloadDeclined[2] = { -1, -1 }, s_reloadDeclinedReserve[2];

int gevrModApiReloadAttempt(int hand, int item, int reserve)
{
    GevrModArg a[2];
    int declined;

    if (!forLocal() || hand < 0 || hand > 1) return 0;
    handItem(a, hand, item);
    declined = gevrModScriptEvent("reload_attempt", a, 2, 1);
    s_reloadDeclined[hand] = declined ? item : -1;
    s_reloadDeclinedReserve[hand] = reserve;
    return declined;
}

/* out_of_ammo's answer per hand: the item kept in hand, -1 none. The game asks
 * each tick the trigger is up (gunfire.c), so a kept gun stays kept until it
 * has rounds (a shot, a reload), is drawn, or the mission or the mod ends */
static int s_emptyKept[2] = { -1, -1 };

static void weaponMemosClear(int hand)
{
    if (hand < 0 || hand > 1) return;
    s_reloadDeclined[hand] = -1;
    s_emptyKept[hand] = -1;
}

int gevrModApiOutOfAmmo(int hand, int item)
{
    GevrModArg a[2];
    int kept;

    if (!forLocal() || hand < 0 || hand > 1) return 0;
    if (s_emptyKept[hand] == item) return 1;
    handItem(a, hand, item);
    kept = gevrModScriptEvent("out_of_ammo", a, 2, 1);
    s_emptyKept[hand] = kept ? item : -1;
    return kept;
}

int gevrModApiReloadDeclined(int hand, int item, int reserve)
{
    return forLocal() && hand >= 0 && hand <= 1 && s_reloadDeclined[hand] == item
        && s_reloadDeclinedReserve[hand] == reserve;
}

void gevrModApiReloadStart(int hand, int item)
{
    GevrModArg a[2];

    if (!forLocal()) return;
    handItem(a, hand, item);
    gevrModScriptEvent("reload_start", a, 2, 0);
}

void gevrModApiReloadEnd(int hand, int item)
{
    GevrModArg a[3];

    if (!forLocal()) return;
    s_emptyKept[hand ? 1 : 0] = -1;   /* rounds in: empty again, it's asked again */
    handItem(a, hand, item);
    a[2].kind = GEVR_MODARG_INTEGER;
    a[2].n = gevrModGameLoaded(hand);
    gevrModScriptEvent("reload_end", a, 3, 0);
}

void gevrModApiDrawn(int hand, int item)
{
    GevrModArg a[2];

    if (!forLocal()) return;
    weaponMemosClear(hand);   /* another gun, or this one again: asked afresh */
    gevrModGameScope(hand, GEVR_MODSCOPE_GAME, item, NULL, 0.0f, 0.0f);   /* a mod sets this gun's in weapon_drawn */
    handItem(a, hand, item);
    gevrModScriptEvent("weapon_drawn", a, 2, 0);
}

/* ---- the items area's events ---- */

static int pushPickup(lua_State *L, void *ud)
{
    const GevrModPickup *p = (const GevrModPickup *)ud;
    int t;

    lua_createtable(L, 0, 3);
    lua_pushstring(L, s_pickupKinds[p->kind]);
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, p->index);
    lua_setfield(L, -2, "index");
    if (p->kind == GEVR_MODPICKUP_WEAPON) {
        lua_pushinteger(L, p->item);
        lua_setfield(L, -2, "item");
    } else if (p->kind == GEVR_MODPICKUP_AMMO) {
        lua_pushinteger(L, p->ammo);
        lua_setfield(L, -2, "ammo");
    } else if (p->kind == GEVR_MODPICKUP_CRATE) {
        lua_newtable(L);
        for (t = 1; t <= GEVR_MODAPI_AMMO_TYPES; t++) {
            if (p->crate[t] > 0) {
                lua_pushinteger(L, p->crate[t]);
                lua_rawseti(L, -2, t);
            }
        }
        lua_setfield(L, -2, "crate");
    } else {
        lua_pushnumber(L, p->armour);
        lua_setfield(L, -2, "amount");
    }
    return 1;
}

/* A handler's answer: nil keeps the pickup, false leaves none, a table of the
 * same kind changes what it holds (a field left out keeps its own). Anything
 * else is the mod's error. */
static int answerPickup(lua_State *L, void *ud)
{
    GevrModPickup *p = (GevrModPickup *)ud;
    GevrModPickup next = *p;
    const char *kind = s_pickupKinds[p->kind];
    const int ans = lua_gettop(L);

    if (lua_isnil(L, ans)) return 0;
    if (lua_type(L, ans) == LUA_TBOOLEAN && !lua_toboolean(L, ans)) {
        p->none = 1;
        return 1;   /* gone: no one else is asked */
    }
    if (!lua_istable(L, ans)) {
        return luaL_error(L, "pickup_spawning: answer nil (keep it), false (none) or a %s table", kind);
    }
    if (lua_getfield(L, ans, "kind") != LUA_TNIL && (lua_type(L, -1) != LUA_TSTRING || strcmp(lua_tostring(L, -1), kind))) {
        return luaL_error(L, "pickup_spawning: a %s pickup stays a %s", kind, kind);
    }
    lua_settop(L, ans);
    if (p->kind == GEVR_MODPICKUP_WEAPON) {
        if (lua_getfield(L, ans, "item") != LUA_TNIL) {
            next.item = lookup(L, -1, s_itemNames, 0, GEVR_MODAPI_ITEMS);
            if (next.item < 0 || !gevrModGameSwappable(next.item)) {
                return luaL_error(L, "pickup_spawning: a weapon's item is a gun or thrown weapon (mod.items)");
            }
        }
    } else if (p->kind == GEVR_MODPICKUP_AMMO) {
        if (lua_getfield(L, ans, "ammo") != LUA_TNIL) {
            next.ammo = lookup(L, -1, s_ammoNames, 1, GEVR_MODAPI_AMMO_TYPES + 1);
            if (next.ammo < 0) return luaL_error(L, "pickup_spawning: a box's ammo is a type from mod.ammo");
        }
    } else if (p->kind == GEVR_MODPICKUP_CRATE) {
        const int t = lua_getfield(L, ans, "crate");

        if (t != LUA_TNIL) {
            const int crate = lua_gettop(L);

            unsigned seen = 0;

            if (t != LUA_TTABLE) return luaL_error(L, "pickup_spawning: a crate's crate is a table of type = rounds");
            memset(next.crate, 0, sizeof(next.crate));
            lua_pushnil(L);
            while (lua_next(L, crate)) {
                const int type = lookup(L, -2, s_ammoNames, 1, GEVR_MODAPI_AMMO_TYPES + 1);
                const int most = type > 0 ? gevrModGameAmmoMax(type) : 0;
                int whole = 0;
                lua_Integer v = 0;

                if (type < 0) return luaL_error(L, "pickup_spawning: a crate holds types from mod.ammo");
                if (lua_type(L, -1) == LUA_TNUMBER) v = lua_tointegerx(L, -1, &whole);
                if (!whole || v < 0 || v > most) {
                    return luaL_error(L, "pickup_spawning: a crate holds 0 to %d of %s", most, s_ammoNames[type]);
                }
                if (seen & (1u << type)) return luaL_error(L, "pickup_spawning: %s is in the crate twice", s_ammoNames[type]);
                seen |= 1u << type;
                next.crate[type] = (int)v;
                lua_pop(L, 1);
            }
        }
    } else if (lua_getfield(L, ans, "amount") != LUA_TNIL) {
        double v;

        if (!lua_isnumber(L, -1) || (v = lua_tonumber(L, -1)) <= 0 || v > GEVR_MODAPI_ARMOUR_MAX) {
            char most[32];   /* Lua's own formats have no %g */

            snprintf(most, sizeof(most), "%g", (double)GEVR_MODAPI_ARMOUR_MAX);
            return luaL_error(L, "pickup_spawning: armour's amount is more than 0, up to %s (a full vest)", most);
        }
        next.armour = (float)v;
    }
    lua_settop(L, ans);
    *p = next;
    return 0;
}

void gevrModApiPickupSpawning(GevrModPickup *p)
{
    if (!gevrModScriptRunning() || p->kind < GEVR_MODPICKUP_WEAPON || p->kind > GEVR_MODPICKUP_ARMOUR) return;
    gevrModScriptAsk("pickup_spawning", pushPickup, answerPickup, p, 0);
}

/* item_pickup(kind, id, rounds, pickup): nargs of id and rounds given, the
 * rest nil; the pickup's id always fourth (nil when it has none) */
static void pickupNote(const char *kind, int nargs, int id, int rounds, float armour, int pickup)
{
    GevrModArg a[4];

    a[0].kind = GEVR_MODARG_STRING;
    a[0].s = kind;
    a[1].kind = a[2].kind = GEVR_MODARG_NIL;
    if (nargs >= 2 && armour >= 0) {
        a[1].kind = GEVR_MODARG_NUMBER;
        a[1].n = armour;
    } else if (nargs >= 2) {
        a[1].kind = GEVR_MODARG_INTEGER;
        a[1].n = id;
    }
    if (nargs >= 3) {
        a[2].kind = GEVR_MODARG_INTEGER;
        a[2].n = rounds;
    }
    a[3].kind = pickup >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
    a[3].n = pickup;
    gevrModScriptEvent("item_pickup", a, 4, 0);
}

void gevrModApiItemPickup(const GevrModPickup *p)
{
    int t;

    if (!forLocal()) return;
    switch (p->kind) {
    case GEVR_MODPICKUP_WEAPON:
        pickupNote("weapon", 2, p->item, 0, -1, p->pickup);
        break;
    case GEVR_MODPICKUP_AMMO:
        pickupNote("ammo", 3, p->ammo, p->rounds, -1, p->pickup);
        break;
    case GEVR_MODPICKUP_CRATE:   /* as boxes: one for each type it held, each with the crate's id */
        for (t = 1; t <= GEVR_MODAPI_AMMO_TYPES; t++) {
            if (p->crate[t] > 0) pickupNote("ammo", 3, t, p->crate[t], -1, p->pickup);
        }
        break;
    case GEVR_MODPICKUP_ARMOUR:
        pickupNote("armour", 2, 0, 0, p->armour, p->pickup);
        break;
    case GEVR_MODPICKUP_KEY:
        pickupNote("key", 1, 0, 0, -1, p->pickup);
        break;
    case GEVR_MODPICKUP_OBJECT:
        pickupNote("object", 1, 0, 0, -1, p->pickup);
        break;
    }
}

void gevrModApiWeaponDropped(int pickup, int item, int from)
{
    GevrModArg a[3];

    if (!gevrModScriptRunning()) return;
    a[0].kind = pickup >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
    a[0].n = pickup;
    a[1].kind = GEVR_MODARG_INTEGER;
    a[1].n = item;
    if (from == GEVR_MODAPI_FROM_PLAYER) {
        a[2].kind = GEVR_MODARG_STRING;
        a[2].s = "player";
    } else {
        a[2].kind = from >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
        a[2].n = from;
    }
    gevrModScriptEvent("weapon_dropped", a, 3, 0);
}

/* ---- the hands area's events ---- */

/* hand (the body slots' ctrl: 1 the gun hand, "right"), then the anchor or nil */
static int handAnchor(GevrModArg *a, int ctrl, int anchor)
{
    a[0].kind = GEVR_MODARG_STRING;
    a[0].s = handName(ctrl ? 0 : 1);
    if (anchor >= 0 && anchor < GEVR_MODAPI_ANCHORS) {
        a[1].kind = GEVR_MODARG_STRING;
        a[1].s = s_anchorNames[anchor];
    } else {
        a[1].kind = GEVR_MODARG_NIL;
    }
    return 2;
}

void gevrModApiGrip(int ctrl, int anchor, int taken)
{
    GevrModArg a[3];

    if (!forLocal()) return;
    handAnchor(a, ctrl, anchor);
    a[2].kind = GEVR_MODARG_BOOL;
    a[2].n = taken != 0;
    gevrModScriptEvent("grip", a, 3, 0);
}

void gevrModApiRelease(int ctrl, int anchor)
{
    GevrModArg a[2];

    if (!forLocal()) return;
    gevrModScriptEvent("release", a, handAnchor(a, ctrl, anchor), 0);
}

void gevrModApiHandNear(int ctrl, int anchor)
{
    GevrModArg a[2];

    if (!forLocal() || anchor < 0) return;
    gevrModScriptEvent("hand_near", a, handAnchor(a, ctrl, anchor), 0);
}

/* ---- the guards area's events ---- */

static int pushGuard(lua_State *L, void *ud)
{
    const GevrModGuardSpawn *g = (const GevrModGuardSpawn *)ud;

    lua_createtable(L, 0, 5);
    lua_pushinteger(L, g->id);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, g->index);
    lua_setfield(L, -2, "index");
    lua_pushinteger(L, g->body);
    lua_setfield(L, -2, "body");
    setItem(L, "head", g->head);
    setItem(L, "weapon", g->weapon);
    return 1;
}

/* A handler's answer: nil keeps the guard as the setup has it; a table
 * changes its body, head or gun (a field left out keeps its own). A body
 * with its own head takes no other; a gun replaces a setup gun only. */
static int answerGuard(lua_State *L, void *ud)
{
    GevrModGuardSpawn *g = (GevrModGuardSpawn *)ud;
    GevrModGuardSpawn next = *g;
    const int ans = lua_gettop(L);
    int own;

    if (lua_isnil(L, ans)) return 0;
    if (!lua_istable(L, ans)) return luaL_error(L, "guard_spawning: answer nil (keep it) or a table of body, head, weapon");
    if (lua_getfield(L, ans, "body") != LUA_TNIL) {
        int whole = 0;
        const lua_Integer v = lua_type(L, -1) == LUA_TNUMBER ? lua_tointegerx(L, -1, &whole) : -1;

        if (!whole || v < 0 || v > 0x7FFF || !gevrModGameBodyOk((int)v)) {
            return luaL_error(L, "guard_spawning: body is one of the cartridge's bodies (not a head)");
        }
        next.body = (int)v;
    }
    own = gevrModGameBodyOk(next.body) == 2;
    if (lua_getfield(L, ans, "head") != LUA_TNIL) {
        int whole = 0;
        const lua_Integer v = lua_type(L, -1) == LUA_TNUMBER ? lua_tointegerx(L, -1, &whole) : -1;

        if (own) return luaL_error(L, "guard_spawning: that body has its own head");
        if (!whole || v < 0 || v > 0x7FFF || !gevrModGameHeadOk((int)v)) {
            return luaL_error(L, "guard_spawning: head is one of the cartridge's heads");
        }
        next.head = (int)v;
    } else if (own) {
        next.head = -1;
    }
    if (lua_getfield(L, ans, "weapon") != LUA_TNIL) {
        const int item = lookup(L, -1, s_itemNames, 0, GEVR_MODAPI_ITEMS);

        if (g->weapon < 0) return luaL_error(L, "guard_spawning: this guard has no gun in the setup to change");
        if (item < 0 || !gevrModGameGuardGun(item)) {
            return luaL_error(L, "guard_spawning: a guard's weapon is a gun (mod.items)");
        }
        next.weapon = item;
    }
    lua_settop(L, ans);
    *g = next;
    return 0;
}

void gevrModApiGuardSpawning(GevrModGuardSpawn *g)
{
    if (!gevrModScriptRunning()) return;
    gevrModScriptAsk("guard_spawning", pushGuard, answerGuard, g, 0);
}

void gevrModApiGuardAlerted(int id, int how, int player)
{
    GevrModArg a[3];

    if (!gevrModScriptRunning() || how < GEVR_MODALERT_SAW || how > GEVR_MODALERT_OTHER) return;
    a[0].kind = GEVR_MODARG_INTEGER;
    a[0].n = id;
    a[1].kind = GEVR_MODARG_STRING;
    a[1].s = s_alertNames[how];
    a[2].kind = player >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
    a[2].n = player;
    gevrModScriptEvent("guard_alerted", a, 3, 0);
}

void gevrModApiGuardKilled(int id, int how, int item, int player)
{
    GevrModArg a[4];

    if (!gevrModScriptRunning() || how < GEVR_MODKILL_SHOT || how > GEVR_MODKILL_EXPLOSION) return;
    a[0].kind = GEVR_MODARG_INTEGER;
    a[0].n = id;
    a[1].kind = GEVR_MODARG_STRING;
    a[1].s = s_killNames[how];
    a[2].kind = item >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
    a[2].n = item;
    a[3].kind = player >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
    a[3].n = player;
    gevrModScriptEvent("guard_killed", a, 4, 0);
}

void gevrModApiGuardSpawned(int id)
{
    GevrModArg a[1];

    if (!gevrModScriptRunning()) return;
    a[0].kind = GEVR_MODARG_INTEGER;
    a[0].n = id;
    gevrModScriptEvent("guard_spawned", a, 1, 0);
}

void gevrModApiCorpseRetiring(int id, const float pos[3])
{
    GevrModArg a[4];
    int i;

    if (!gevrModScriptRunning()) return;
    a[0].kind = GEVR_MODARG_INTEGER;
    a[0].n = id;
    for (i = 0; i < 3; i++) {
        a[i + 1].kind = GEVR_MODARG_NUMBER;
        a[i + 1].n = pos[i];
    }
    gevrModScriptEvent("corpse_retiring", a, 4, 0);
}

/* ---- the damage and player area ---- */

static const char *const s_targetNames[] = { NULL, "guard", "player" };
static const char *const s_howNames[] = { NULL, "shot", "explosion", "gas", "hit" };
static const char *const s_partNames[] = { NULL, "head", "body", "limb", "gun", "hat" };

static void pushDamageFields(lua_State *L, const GevrModDamage *d)
{
    lua_pushstring(L, s_targetNames[d->target]);
    lua_setfield(L, -2, "target");
    lua_pushinteger(L, d->id);
    lua_setfield(L, -2, "id");
    lua_pushnumber(L, d->amount);
    lua_setfield(L, -2, "amount");
    lua_pushstring(L, s_howNames[d->how]);
    lua_setfield(L, -2, "how");
    if (d->part >= GEVR_MODPART_HEAD && d->part <= GEVR_MODPART_HAT) {
        lua_pushstring(L, s_partNames[d->part]);
        lua_setfield(L, -2, "part");
    }
    setItem(L, "item", d->item);
    if (d->by >= 0) {
        lua_pushinteger(L, d->by);
        lua_setfield(L, -2, "by");
    }
}

static int pushBefore(lua_State *L, void *ud)
{
    lua_createtable(L, 0, 7);
    pushDamageFields(L, (const GevrModDamage *)ud);
    return 1;
}

static int pushApplied(lua_State *L, void *ud)
{
    const GevrModDamage *d = (const GevrModDamage *)ud;

    lua_createtable(L, 0, 9);
    pushDamageFields(L, d);
    lua_pushnumber(L, d->health);
    lua_setfield(L, -2, "health");
    if (d->target == GEVR_MODTARGET_PLAYER) {
        lua_pushnumber(L, d->armour);
        lua_setfield(L, -2, "armour");
    }
    return 1;
}

/* An amount a handler gave: a number from 0 to GEVR_MODAPI_DAMAGE_MAX */
static float damageAmount(lua_State *L, int idx)
{
    const lua_Number v = lua_tonumber(L, idx);

    if (lua_type(L, idx) != LUA_TNUMBER || !(v >= 0 && v <= GEVR_MODAPI_DAMAGE_MAX)) {
        char most[48];   /* Lua's own formats have no %g */

        snprintf(most, sizeof(most), "%g", (double)GEVR_MODAPI_DAMAGE_MAX);
        luaL_error(L, "before_damage: an amount is a number from 0 to %s", most);
    }
    return (float)v;
}

/* nil keeps the damage, false cancels it (no one else is asked), a number
 * or a table's amount changes it */
static int answerDamage(lua_State *L, void *ud)
{
    GevrModDamage *d = (GevrModDamage *)ud;
    const int ans = lua_gettop(L);

    if (lua_isnil(L, ans)) return 0;
    if (lua_type(L, ans) == LUA_TBOOLEAN && !lua_toboolean(L, ans)) {
        d->cancelled = 1;
        d->amount = 0.0f;
        return 1;
    }
    if (lua_type(L, ans) == LUA_TNUMBER) {
        d->amount = damageAmount(L, ans);
        return 0;
    }
    if (!lua_istable(L, ans)) {
        return luaL_error(L, "before_damage: answer nil (keep it), false (none), an amount or a table of amount");
    }
    if (lua_getfield(L, ans, "amount") != LUA_TNIL) d->amount = damageAmount(L, -1);
    lua_settop(L, ans);
    return 0;
}

static int answerNothing(lua_State *L, void *ud)
{
    (void)L;
    (void)ud;
    return 0;
}

void gevrModApiBeforeDamage(GevrModDamage *d)
{
    if (!gevrModScriptRunning() || d->target < GEVR_MODTARGET_GUARD || d->target > GEVR_MODTARGET_PLAYER
        || d->how < GEVR_MODHOW_SHOT || d->how > GEVR_MODHOW_HIT) {
        return;
    }
    gevrModScriptAsk("before_damage", pushBefore, answerDamage, d, 1);
}

void gevrModApiDamageApplied(const GevrModDamage *d)
{
    if (!gevrModScriptRunning() || d->target < GEVR_MODTARGET_GUARD || d->target > GEVR_MODTARGET_PLAYER
        || d->how < GEVR_MODHOW_SHOT || d->how > GEVR_MODHOW_HIT) {
        return;
    }
    gevrModScriptAsk("damage_applied", pushApplied, answerNothing, (void *)d, 1);
}

static void slotEvent(const char *event, int slot)
{
    GevrModArg a[1];

    if (!gevrModScriptRunning() || slot < 0) return;
    a[0].kind = GEVR_MODARG_INTEGER;
    a[0].n = slot;
    gevrModScriptEvent(event, a, 1, 0);
}

void gevrModApiPlayerDowned(int slot, int down)
{
    slotEvent(down ? "player_downed" : "player_revived", slot);
}

void gevrModApiPlayerDied(int slot)
{
    slotEvent("player_died", slot);
}

/* ---- the network's events ---- */

/* Messages held until the next frame in play (one that came while the
 * mission loaded waits for it) */
static struct {
    char mod[64], channel[GEVR_MODAPI_NET_CHANNEL + 1];
    int from, len;
    unsigned char data[GEVR_MODAPI_NET_PAYLOAD];
} s_inbox[GEVR_MODAPI_NET_INBOX];
static int s_inStart, s_inCount, s_inDropped;
static unsigned s_readyHeld;   /* peer_ready's slots, held as the messages are (a bit each) */

void gevrModNetReceive(const char *mod, int from, const char *channel, const void *data, int len)
{
    int at;

    if (!gevrModScriptRunning() || !mod || !channel || len < 0 || len > GEVR_MODAPI_NET_PAYLOAD
        || strlen(mod) >= sizeof(s_inbox[0].mod) || strlen(channel) > GEVR_MODAPI_NET_CHANNEL) {
        return;
    }
    if (s_inCount == GEVR_MODAPI_NET_INBOX) {
        s_inDropped++;   /* logged as the frame flushes */
        return;
    }
    at = (s_inStart + s_inCount++) % GEVR_MODAPI_NET_INBOX;
    snprintf(s_inbox[at].mod, sizeof(s_inbox[at].mod), "%s", mod);
    snprintf(s_inbox[at].channel, sizeof(s_inbox[at].channel), "%s", channel);
    s_inbox[at].from = from;
    s_inbox[at].len = len;
    if (len > 0) memcpy(s_inbox[at].data, data, (size_t)len);
}

/*
 * The others' attachments (#335): their messages held as mod.net's are, and
 * applied at the next frame in play if they're this mission's. A player who
 * reaches the stage later is sent what everyone wears: the host answers for
 * itself and asks the others who wear something (s_rigs, as it heard them)
 * to send theirs to that player. A player who leaves takes theirs along.
 */
#define ATTACH_HELD 32
static struct {
    char mod[64];
    int from, len;
    unsigned char data[GEVR_ATTNET_BYTES];
} s_attachIn[ATTACH_HELD];
static int s_attachHeld, s_attachDropped, s_attachBad;
static unsigned s_rigs;      /* players heard wearing something this mission (the host's asks) */
static unsigned s_rigsDrawn; /* players whose attachments are placed here (gone when they leave) */
static char s_rigMod[64];    /* a mod heard attaching, to ask in its name */

static void attachNetForget(void)
{
    s_attachHeld = s_attachDropped = 0;
    s_rigs = s_rigsDrawn = 0;
}

void gevrModNetAttach(const char *mod, int from, const void *data, int len)
{
    static GevrAttachRecord r[GEVR_ATTNET_RECORDS];
    int n, i, keep = 0;

    if (!gevrModScriptRunning() || mod == NULL || strlen(mod) >= sizeof(s_attachIn[0].mod)) return;
    n = gevrAttachNetDecode((const unsigned char *)data, len, NULL, r);
    if (n < 0) {
        if (!s_attachBad) {
            sysLogPrintf(LOG_WARNING, "mods: %s: a malformed attachments message from player %d dropped", mod, from);
        }
        s_attachBad = 1;   /* once a mission */
        return;
    }
    for (i = 0; i < n; i++) {
        if (r[i].op == GEVR_ATTNET_ASK) {
            /* the host's alone: send what this player wears to that one */
            if ((from == netGetHostSlot() || from < 0) && r[i].slot != netGetLocalSlot()) gevrAttachNetAnswer(r[i].slot);
            continue;
        }
        if (from < 0 || from >= GEVR_ATTNET_SLOTS) continue;   /* a dedicated server wears nothing */
        keep = 1;
        if (r[i].op == GEVR_ATTNET_SET) {
            s_rigs |= 1u << from;
            snprintf(s_rigMod, sizeof(s_rigMod), "%s", mod);
        } else if (r[i].op == GEVR_ATTNET_CLEAR) {
            s_rigs &= ~(1u << from);
        }
    }
    if (!keep || netIsDedicated()) return;   /* the server draws no one */
    if (s_attachHeld == ATTACH_HELD) {
        s_attachDropped++;   /* logged as the frame applies them */
        return;
    }
    snprintf(s_attachIn[s_attachHeld].mod, sizeof(s_attachIn[0].mod), "%s", mod);
    s_attachIn[s_attachHeld].from = from;
    s_attachIn[s_attachHeld].len = len;
    memcpy(s_attachIn[s_attachHeld].data, data, (size_t)len);
    s_attachHeld++;
}

/* a held message's records to the game, if it's this mission's and its mod runs here */
static void attachApply(const char *mod, int from, const unsigned char *data, int len)
{
    static GevrAttachRecord r[GEVR_ATTNET_RECORDS];
    int n, i, j, stage;

    n = gevrAttachNetDecode(data, len, &stage, r);
    if (n < 0 || stage != (gevrModScriptStage() & 0xFF) || !gevrModScriptOn(mod)) return;
    for (i = 0; i < n; i++) {
        if (r[i].op == GEVR_ATTNET_SET) {
            GevrModAttach a;

            memset(&a, 0, sizeof(a));
            snprintf(a.model, sizeof(a.model), "%s", r[i].model);
            snprintf(a.like, sizeof(a.like), "%s", r[i].like);
            a.on = r[i].on;
            a.where = r[i].where;
            a.node = r[i].node;
            for (j = 0; j < 4; j++) {
                memcpy(a.offset[j], r[i].frame[j], sizeof(r[i].frame[j]));
                a.offset[j][3] = j == 3 ? 1.0f : 0.0f;
            }
            if (gevrModGameAttachRemote(from, r[i].id, &a, r[i].shown, r[i].body)) s_rigsDrawn |= 1u << from;
        } else if (r[i].op == GEVR_ATTNET_REMOVE) {
            gevrModGameAttachRemoteGone(from, r[i].id);
        } else if (r[i].op == GEVR_ATTNET_CLEAR) {
            gevrModGameAttachRemoteGone(from, 0);
        }
    }
}

static int attachSend(int to, const char *mod, const void *data, int len)
{
    return netSendModMessage(GEVR_MODNET_ATTACH, to, mod, NULL, data, len);
}

/* each frame in play: the others' held messages, who left, what this player's changed */
static void attachNetFrame(void)
{
    int i, slot;

    if (s_attachDropped) {
        sysLogPrintf(LOG_WARNING, "mods: %d attachments message(s) dropped: more than %d waited", s_attachDropped,
                     ATTACH_HELD);
        s_attachDropped = 0;
    }
    for (i = 0; i < s_attachHeld; i++) attachApply(s_attachIn[i].mod, s_attachIn[i].from, s_attachIn[i].data, s_attachIn[i].len);
    s_attachHeld = 0;
    for (slot = 0; s_rigsDrawn && slot < GEVR_ATTNET_SLOTS; slot++) {
        if ((s_rigsDrawn & (1u << slot)) && !netLobbySlotConnected(slot)) {
            gevrModGameAttachRemoteGone(slot, 0);   /* the player left */
            s_rigsDrawn &= ~(1u << slot);
            s_rigs &= ~(1u << slot);
        }
    }
    if (netIsActive() && gevrAttachNetWorn()) {
        gevrAttachNetRefreshBody(gevrModGameAttachBodyAt);
    }
    if (netIsActive()) gevrAttachNetPoll(sysGetMicroseconds(), gevrModScriptStage() & 0xFF, attachSend);
}

/* the host: a player reached the stage; what everyone wears goes to it */
static void attachJoined(int slot)
{
    if (!netIsActive() || slot < 0 || slot >= GEVR_ATTNET_SLOTS) return;
    gevrAttachNetAnswer(slot);   /* nothing when this player wears nothing */
    if (s_rigs & ~(1u << slot)) gevrAttachNetAskFor(s_rigMod, slot);
}

void gevrModApiNetFlush(void)
{
    int slot;

    for (slot = 0; s_readyHeld && slot < 32; slot++) {
        if (s_readyHeld & (1u << slot)) {
            GevrModArg a[1];

            s_readyHeld &= ~(1u << slot);
            attachJoined(slot);
            a[0].kind = GEVR_MODARG_INTEGER;
            a[0].n = slot;
            gevrModScriptEvent("peer_ready", a, 1, 0);
        }
    }
    if (s_inDropped) {
        sysLogPrintf(LOG_WARNING, "mods: %d message(s) dropped: more than %d waited", s_inDropped, GEVR_MODAPI_NET_INBOX);
        s_inDropped = 0;
    }
    while (s_inCount > 0) {
        const int at = s_inStart;
        GevrModArg a[3];

        s_inStart = (s_inStart + 1) % GEVR_MODAPI_NET_INBOX;
        s_inCount--;
        a[0].kind = GEVR_MODARG_STRING;
        a[0].s = s_inbox[at].channel;
        a[1].kind = GEVR_MODARG_BYTES;
        a[1].s = (const char *)s_inbox[at].data;
        a[1].n = s_inbox[at].len;
        a[2].kind = s_inbox[at].from >= 0 ? GEVR_MODARG_INTEGER : GEVR_MODARG_NIL;
        a[2].n = s_inbox[at].from;
        gevrModScriptEventFor(s_inbox[at].mod, "net_message", a, 3);
    }
    attachNetFrame();
}

void gevrModApiNetClear(void)
{
    s_inStart = s_inCount = s_inDropped = 0;
    s_readyHeld = 0;
    attachNetForget();
    s_attachBad = 0;
}

void gevrModNetStopped(const char *mod, int from)
{
    gevrModScriptStopRemote(mod, from);
}

void gevrModApiStopped(const char *id)
{
    if (netIsActive()) netSendModMessage(GEVR_MODNET_OFF, 0xFF, id, NULL, NULL, 0);
}

/* A player reached the stage (the host's netReceiveStageReady): told at the
 * next frame in play, so one who loads before the host's mission starts
 * is told as it does */
void gevrModApiPeerReady(int slot)
{
    if (!gevrModScriptRunning() || slot < 0 || slot >= 32) return;
    s_readyHeld |= 1u << slot;
}

/* ---- the UI's side: the in-game screen ---- */

/* The clicks and changes, for the mods' next frame */
static struct {
    char mod[64], page[GEVR_MODAPI_UI_ID + 1], what[GEVR_MODAPI_UI_ID + 1];
    int kind;    /* 0 a click, else the row's GEVR_MODUI_* */
    int value;
} s_uiQueue[GEVR_MODAPI_UI_QUEUE];
static int s_uiQueued, s_uiDropped;

/* The s_pages entry shown i-th now, -1 none */
static int uiShown(int i)
{
    int k;

    for (k = 0; k < s_pageCount && i >= 0; k++) {
        if (gevrModScriptOn(s_pages[k].mod) && i-- == 0) return k;
    }
    return -1;
}

int gevrModUiCount(void)
{
    int k, n = 0;

    for (k = 0; k < s_pageCount; k++) n += gevrModScriptOn(s_pages[k].mod) != 0;
    return n;
}

const GevrModUiPage *gevrModUiPage(int i)
{
    const int k = uiShown(i);

    return k >= 0 ? &s_pages[k] : NULL;
}

static void uiQueue(const GevrModUiPage *p, const char *what, int kind, int value)
{
    int q;

    /* a slider dragged: its last value, once */
    for (q = 0; kind && q < s_uiQueued; q++) {
        if (s_uiQueue[q].kind == kind && !strcmp(s_uiQueue[q].mod, p->mod) && !strcmp(s_uiQueue[q].page, p->id)
            && !strcmp(s_uiQueue[q].what, what)) {
            s_uiQueue[q].value = value;
            return;
        }
    }
    if (s_uiQueued == GEVR_MODAPI_UI_QUEUE) {
        s_uiDropped++;
        return;
    }
    q = s_uiQueued++;
    snprintf(s_uiQueue[q].mod, sizeof(s_uiQueue[q].mod), "%s", p->mod);
    snprintf(s_uiQueue[q].page, sizeof(s_uiQueue[q].page), "%s", p->id);
    snprintf(s_uiQueue[q].what, sizeof(s_uiQueue[q].what), "%s", what);
    s_uiQueue[q].kind = kind;
    s_uiQueue[q].value = value;
}

void gevrModUiSet(int i, int row, int value)
{
    const int k = uiShown(i);
    GevrModUiRow *r;

    if (k < 0 || row < 0 || row >= s_pages[k].rows) return;
    r = &s_pages[k].row[row];
    if (r->blocked[0]) return;
    if (r->kind == GEVR_MODUI_TOGGLE) value = value != 0;
    else if (r->kind == GEVR_MODUI_CHOICE && (value < 0 || value >= r->count)) return;
    else if (r->kind == GEVR_MODUI_SLIDER) value = value < r->min ? r->min : value > r->max ? r->max : value;
    if (value == r->value) return;
    r->value = value;
    uiQueue(&s_pages[k], r->id, r->kind, value);
}

void gevrModUiClick(int i, int button)
{
    const int k = uiShown(i);

    if (k < 0 || button < 0 || button >= s_pages[k].buttons || s_pages[k].button[button].blocked[0]) return;
    uiQueue(&s_pages[k], s_pages[k].button[button].id, 0, 0);
}

int gevrModUiAsked(void)
{
    int i;

    if (s_uiAsk < 0 || s_uiAsk >= s_pageCount || !gevrModScriptOn(s_pages[s_uiAsk].mod)) return -1;
    for (i = 0; uiShown(i) >= 0; i++) {
        if (uiShown(i) == s_uiAsk) return i;
    }
    return -1;
}

void gevrModUiOpened(void)
{
    s_uiAsk = -1;
}

void gevrModApiUiFlush(void)
{
    int q, n = s_uiQueued;
    static struct {
        char mod[64], page[GEVR_MODAPI_UI_ID + 1], what[GEVR_MODAPI_UI_ID + 1];
        int kind, value;
    } taken[GEVR_MODAPI_UI_QUEUE];

    if (s_uiDropped) {
        sysLogPrintf(LOG_WARNING, "mods: %d click(s) or change(s) dropped: more than %d waited", s_uiDropped,
                     GEVR_MODAPI_UI_QUEUE);
        s_uiDropped = 0;
    }
    /* taken first: a handler can change the pages, and the screen queues more only as it draws */
    memcpy(taken, s_uiQueue, sizeof(s_uiQueue[0]) * (size_t)n);
    s_uiQueued = 0;
    for (q = 0; q < n; q++) {
        GevrModArg a[3];

        a[0].kind = GEVR_MODARG_STRING;
        a[0].s = taken[q].page;
        a[1].kind = GEVR_MODARG_STRING;
        a[1].s = taken[q].what;
        if (!taken[q].kind) {
            gevrModScriptEventFor(taken[q].mod, "ui_click", a, 2);
            continue;
        }
        a[2].kind = taken[q].kind == GEVR_MODUI_TOGGLE ? GEVR_MODARG_BOOL : GEVR_MODARG_INTEGER;
        a[2].n = taken[q].kind == GEVR_MODUI_CHOICE ? taken[q].value + 1 : taken[q].value;   /* a choice's number from 1 */
        gevrModScriptEventFor(taken[q].mod, "ui_change", a, 3);
    }
}

void gevrModApiUiClear(void)
{
    s_uiQueued = s_uiDropped = 0;
    s_uiAsk = -1;
}
