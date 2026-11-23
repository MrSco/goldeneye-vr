/*
 * The mod API's areas (docs/mod-system.md, API v1 and "Where each hook
 * sits"): the game's events into mods' scripts (gevr_modscript.c) and the
 * mod.* tables, in port/src/gevr_modapi.c, over a thin game side in
 * src/game/gevr_modgame.c. The first area: weapons.
 */
#ifndef GEVR_MODAPI_H
#define GEVR_MODAPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* ITEM_IDS from ITEM_UNARMED (0) to the last before ITEM_IDS_MAX: mod.items'
 * names (gevr_modgame.c checks the count against the game's enum) */
#define GEVR_MODAPI_ITEMS 89

struct lua_State;
/* gevr_modscript.c: the areas' tables into the mod table on the stack's top */
void gevrModApiOpen(struct lua_State *L);
/* gevr_modscript.c: what the mods changed back to the game's own (a mission's
 * end, a mod stopped or paused) */
void gevrModApiReset(void);

/* Weapon stats limits (mod.weapon.setStats): per weapon type, shared by
 * everyone holding it, guards too; back to the game's own at a mission's
 * end and when a mod stops */
#define GEVR_MODAPI_MAG_MAX 255
#define GEVR_MODAPI_RATE_MIN 1        /* ticks between an automatic's shots */
#define GEVR_MODAPI_RATE_MAX 60
#define GEVR_MODAPI_DAMAGE_SCALE 10.0f   /* at most this times the weapon's own */

/* ---- the weapons area: gunfire.c and bondview2.c, the local human player's hands ---- */

/* weapon_fire_attempt before a shot or throw spends anything: 1 when a mod
 * declines it (the caller then clicks dry, or brings the hand back) */
int gevrModApiFireAttempt(int hand, int item);
/* weapon_fire: the round spent (loaded is after it) */
void gevrModApiFired(int hand, int item);
/* reload_attempt before a reload takes rounds from the reserve (the native
 * reload as it starts, a hand reload's fresh magazine or round): 1 when a mod
 * declines it. reserve: the hand's spare rounds now, kept with the answer */
int gevrModApiReloadAttempt(int hand, int item, int reserve);
/* gunfire.c's automatic reload of an empty gun, which asks every tick: 1 while
 * the hand's last reload_attempt declined this item at this reserve */
int gevrModApiReloadDeclined(int hand, int item, int reserve);
/* reload_start: a reload begins (the native one lowers the gun; a hand
 * reload's magazine leaves it, or a magazine goes in without that) */
void gevrModApiReloadStart(int hand, int item);
/* reload_end: rounds are in (the native reload's ammo move; a magazine seated) */
void gevrModApiReloadEnd(int hand, int item);
/* weapon_drawn: a switched-to weapon is up and usable */
void gevrModApiDrawn(int hand, int item);

/* ---- the game's side (src/game/gevr_modgame.c), for gevr_modapi.c ---- */

typedef struct GevrModWeaponStats {
    int magazine;     /* MagSize */
    int fireRate;     /* AutomaticFiringRate, ticks between shots; -1: not automatic */
    float damage;     /* DestructionAmount */
} GevrModWeaponStats;

/* The current player is this machine's human (not a copy, not a bot) */
int gevrModGameLocal(void);
/* This machine's human player is in a stage */
int gevrModGamePlayerReady(void);
/* The local human player's hand (0 right, 1 left): the item, its loaded rounds */
int gevrModGameHeld(int hand);
int gevrModGameLoaded(int hand);
void gevrModGameSetLoaded(int hand, int rounds);
/* An item's own weapon stats row: 0 when it has none (it shares the default) */
int gevrModGameStats(int item, GevrModWeaponStats *out);
void gevrModGameSetStats(int item, const GevrModWeaponStats *in);

#ifdef __cplusplus
}
#endif

#endif
