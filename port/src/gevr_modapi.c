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
 * - weapon_fire(hand, item, loaded), reload_start(hand, item),
 *   reload_end(hand, item, loaded), weapon_drawn(hand, item).
 * - mod.weapon.held / loaded / setLoaded (hand), stats / setStats (item),
 *   within limits; the stats go back to the game's own at a mission's end
 *   and when a mod stops (gevrModApiReset). mod.items: names to ids.
 * All of it for this machine's human player only, never a copy or a bot.
 */
/* the C library's first: see gevr_modscript.c (MB_LEN_MAX) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gevr_modapi.h"
#include "gevr_modscript.h"
#include "../external/lua/lua.h"
#include "../external/lua/lauxlib.h"

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
    lua_pushnumber(L, st.damage);
    lua_setfield(L, -2, "damage");
    return 1;
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
    if (lua_getfield(L, 2, "fireRate") != LUA_TNIL) {
        lua_Integer v;

        if (own.fireRate < 0) {
            return luaL_error(L, "mod.weapon.setStats: %s isn't automatic; it has no fireRate", s_itemNames[item]);
        }
        if (!lua_isinteger(L, -1) || (v = lua_tointeger(L, -1)) < GEVR_MODAPI_RATE_MIN || v > GEVR_MODAPI_RATE_MAX) {
            return luaL_error(L, "mod.weapon.setStats: fireRate is ticks between shots, %d to %d", GEVR_MODAPI_RATE_MIN,
                              GEVR_MODAPI_RATE_MAX);
        }
        next.fireRate = (int)v;
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 2, "damage") != LUA_TNIL) {
        const double cap = own.damage > 0 ? own.damage * GEVR_MODAPI_DAMAGE_SCALE : 0.0;
        double v;

        if (!lua_isnumber(L, -1) || (v = lua_tonumber(L, -1)) < 0 || v > cap) {
            return luaL_error(L, "mod.weapon.setStats: damage is 0 to %.3g for %s (%g times its own)", cap, s_itemNames[item],
                              (double)GEVR_MODAPI_DAMAGE_SCALE);
        }
        next.damage = (float)v;
    }
    lua_pop(L, 1);
    if (i == s_savedCount && s_savedCount < GEVR_MODAPI_ITEMS) {
        s_saved[s_savedCount].item = item;
        s_saved[s_savedCount].own = cur;
        s_savedCount++;
    }
    gevrModGameSetStats(item, &next);
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg s_weaponFuncs[] = {
    { "held", l_held },
    { "loaded", l_loaded },
    { "setLoaded", l_setLoaded },
    { "stats", l_stats },
    { "setStats", l_setStats },
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
}

void gevrModApiReset(void)
{
    int i;

    for (i = s_savedCount - 1; i >= 0; i--) {
        gevrModGameSetStats(s_saved[i].item, &s_saved[i].own);
    }
    s_savedCount = 0;
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
    handItem(a, hand, item);
    a[2].kind = GEVR_MODARG_INTEGER;
    a[2].n = gevrModGameLoaded(hand);
    gevrModScriptEvent("weapon_fire", a, 3, 0);
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
    handItem(a, hand, item);
    a[2].kind = GEVR_MODARG_INTEGER;
    a[2].n = gevrModGameLoaded(hand);
    gevrModScriptEvent("reload_end", a, 3, 0);
}

void gevrModApiDrawn(int hand, int item)
{
    GevrModArg a[2];

    if (!forLocal()) return;
    handItem(a, hand, item);
    gevrModScriptEvent("weapon_drawn", a, 2, 0);
}
