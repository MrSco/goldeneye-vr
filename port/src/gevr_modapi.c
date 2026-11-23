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

/* AMMOTYPE from AMMO_9MM (1): mod.ammo's names, and a name for an ammo argument
 * (9mm_2 is the Klobb's) */
static const char *const s_ammoNames[GEVR_MODAPI_AMMO_TYPES + 1] = {
    NULL, "9mm", "9mm_2", "rifle", "shotgun", "grenade", "rockets", "remotemine", "proxmine", "timedmine",
    "knife", "grenaderound", "magnum", "goldengun",
};

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
            char most[64];   /* Lua's own formats have no %g */

            snprintf(most, sizeof(most), "%.3g for %s (%g times its own)", cap, s_itemNames[item],
                     (double)GEVR_MODAPI_DAMAGE_SCALE);
            return luaL_error(L, "mod.weapon.setStats: damage is 0 to %s", most);
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

static void pickupNote(const char *kind, int nargs, int id, int rounds, float armour)
{
    GevrModArg a[3];

    a[0].kind = GEVR_MODARG_STRING;
    a[0].s = kind;
    if (armour >= 0) {
        a[1].kind = GEVR_MODARG_NUMBER;
        a[1].n = armour;
    } else {
        a[1].kind = GEVR_MODARG_INTEGER;
        a[1].n = id;
    }
    a[2].kind = GEVR_MODARG_INTEGER;
    a[2].n = rounds;
    gevrModScriptEvent("item_pickup", a, nargs, 0);
}

void gevrModApiItemPickup(const GevrModPickup *p)
{
    int t;

    if (!forLocal()) return;
    switch (p->kind) {
    case GEVR_MODPICKUP_WEAPON:
        pickupNote("weapon", 2, p->item, 0, -1);
        break;
    case GEVR_MODPICKUP_AMMO:
        pickupNote("ammo", 3, p->ammo, p->rounds, -1);
        break;
    case GEVR_MODPICKUP_CRATE:   /* as boxes: one for each type it held */
        for (t = 1; t <= GEVR_MODAPI_AMMO_TYPES; t++) {
            if (p->crate[t] > 0) pickupNote("ammo", 3, t, p->crate[t], -1);
        }
        break;
    case GEVR_MODPICKUP_ARMOUR:
        pickupNote("armour", 2, 0, 0, p->armour);
        break;
    case GEVR_MODPICKUP_KEY:
        pickupNote("key", 1, 0, 0, -1);
        break;
    case GEVR_MODPICKUP_OBJECT:
        pickupNote("object", 1, 0, 0, -1);
        break;
    }
}
