/*
 * The mod API's areas (docs/mod-system.md, API v1 and "Where each hook
 * sits"): the game's events into mods' scripts (gevr_modscript.c) and the
 * mod.* tables, in port/src/gevr_modapi.c, over a thin game side in
 * src/game/gevr_modgame.c. The areas so far: weapons, items.
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

/* ---- the items area: the stage's pickups as it loads, the local human player's pickups and inventory ---- */

/* AMMO_9MM (1) to AMMO_GGUN (13), the types a pickup carries: mod.ammo's
 * names and an ammo crate's slots (gevr_modgame.c checks the count) */
#define GEVR_MODAPI_AMMO_TYPES 13
#define GEVR_MODAPI_ARMOUR_MAX 1.0f   /* a full vest */

enum {
    GEVR_MODPICKUP_WEAPON = 1,   /* a gun or a thrown weapon */
    GEVR_MODPICKUP_AMMO,         /* a box of one type */
    GEVR_MODPICKUP_CRATE,        /* a crate of several */
    GEVR_MODPICKUP_ARMOUR,
    GEVR_MODPICKUP_KEY,          /* item_pickup only */
    GEVR_MODPICKUP_OBJECT,       /* item_pickup only: anything else a player takes */
};

typedef struct GevrModPickup {
    int kind;
    int index;          /* the setup record's, the same on every headset */
    int item;           /* WEAPON (item_pickup: any collectable with an item) */
    int ammo;           /* AMMO: its type */
    int rounds;         /* AMMO, item_pickup: what it carried */
    int crate[GEVR_MODAPI_AMMO_TYPES + 1];   /* CRATE: rounds by type, [1] to [13] */
    float armour;       /* ARMOUR: 0 to GEVR_MODAPI_ARMOUR_MAX */
    int none;           /* pickup_spawning's answer: no pickup here */
} GevrModPickup;

/* pickup_spawning: each mod's answer in turn into p, the same kind, or none */
void gevrModApiPickupSpawning(GevrModPickup *p);
/* item_pickup: the local human player took it */
void gevrModApiItemPickup(const GevrModPickup *p);

/* The game's hooks (src/game/gevr_modgame.c) */
struct PropDefHeaderRecord;
/* prop.c proplvreset2, before the stage's records are placed: those a swap
 * must leave alone (a tag, a rename, a dual pair or a safe points at them) */
void gevrModPickupScan(void);
/* prop.c, as a pickup's record is about to be placed: pickup_spawning; 1
 * when a mod said none (the record isn't placed) */
int gevrModPickupSpawn(struct PropDefHeaderRecord *rec, int index);
/* propobj.c propPickupByPlayer, the pickup taken and not yet freed: item_pickup */
void gevrModPickupTaken(struct PropDefHeaderRecord *rec);

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
/* A gun or thrown weapon with a model in this cartridge: what a swap or
 * mod.inventory.give/take may use */
int gevrModGameSwappable(int item);
/* The local human player's inventory: has; give (1 newly, 0 already had);
 * take (1 taken, a hand holding it goes unarmed) */
int gevrModGameHas(int item);
int gevrModGameGive(int item);
int gevrModGameTake(int item);
/* The local human player's spare rounds of a type (not those loaded), and
 * the most the type holds */
int gevrModGameAmmo(int type);
void gevrModGameSetAmmo(int type, int rounds);
int gevrModGameAmmoMax(int type);

#ifdef __cplusplus
}
#endif

#endif
