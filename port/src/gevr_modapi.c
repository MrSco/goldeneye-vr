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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gevr_modapi.h"
#include "gevr_modscript.h"
#include "system.h"
#include "../external/lua/lua.h"
#include "../external/lua/lauxlib.h"

/* the network (port/src/net/net_core.h) */
extern bool netIsActive(void);
extern bool netIsHost(void);
extern bool netIsDedicated(void);
extern int netGetLocalSlot(void);
extern int netSendModMessage(int kind, int to, const char *mod, const char *channel, const void *data, int len);

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

static const luaL_Reg s_guardFuncs[] = {
    { "list", l_guardList },
    { "get", l_guardGet },
    { "spawn", l_guardSpawn },
    { "giveWeapon", l_guardGiveWeapon },
    { "patrol", l_guardPatrol },
    { "paths", l_guardPaths },
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

static const luaL_Reg s_playerFuncs[] = {
    { "health", l_playerHealth },
    { "setHealth", l_playerSetHealth },
    { "setArmour", l_playerSetArmour },
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

static const luaL_Reg s_effectFuncs[] = {
    { "play", l_effectPlay },
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
    luaL_setfuncs(L, s_handFuncs, 0);
    lua_setfield(L, -2, "hands");
    lua_newtable(L);
    luaL_setfuncs(L, s_guardFuncs, 0);
    lua_setfield(L, -2, "guards");
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

void gevrModApiNetFlush(void)
{
    int slot;

    for (slot = 0; s_readyHeld && slot < 32; slot++) {
        if (s_readyHeld & (1u << slot)) {
            GevrModArg a[1];

            s_readyHeld &= ~(1u << slot);
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
}

void gevrModApiNetClear(void)
{
    s_inStart = s_inCount = s_inDropped = 0;
    s_readyHeld = 0;
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
