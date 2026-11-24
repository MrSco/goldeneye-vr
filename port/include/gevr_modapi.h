/*
 * The mod API's areas (docs/mod-system.md, API v1 and "Where each hook
 * sits"): the game's events into mods' scripts (gevr_modscript.c) and the
 * mod.* tables, in port/src/gevr_modapi.c, over a thin game side in
 * src/game/gevr_modgame.c. The areas so far: weapons, items, hands, guards,
 * damage and the player, sound and smoke, the network, the UI.
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
/* out_of_ammo: a hand's gun and its reserve are empty and the game is about
 * to switch the hand to its next weapon; 1 when a mod keeps it in hand.
 * Kept, it isn't asked again until the gun fires, reloads or is drawn */
int gevrModApiOutOfAmmo(int hand, int item);
/* weapon_drawn: a switched-to weapon is up and usable (the hand's scope is
 * the game's own again first: mod.weapon.scope) */
void gevrModApiDrawn(int hand, int item);

/* mod.weapon.scope's limits: the eyepiece in the drawn model's own units
 * (a gun is about 800 long), its radius, and the magnification */
#define GEVR_MODAPI_SCOPE_RANGE 4000.0f
#define GEVR_MODAPI_SCOPE_RADIUS_MIN 1.0f
#define GEVR_MODAPI_SCOPE_RADIUS_MAX 400.0f
#define GEVR_MODAPI_SCOPE_ZOOM_MIN 1.0f
#define GEVR_MODAPI_SCOPE_ZOOM_MAX 25.0f
enum { GEVR_MODSCOPE_GAME, GEVR_MODSCOPE_MOD, GEVR_MODSCOPE_NONE };

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
    int pickup;         /* item_pickup: the pickup's id (gevrModGamePickupId), -1 none */
} GevrModPickup;

/* pickup_spawning: each mod's answer in turn into p, the same kind, or none */
void gevrModApiPickupSpawning(GevrModPickup *p);
/* item_pickup: the local human player took it */
void gevrModApiItemPickup(const GevrModPickup *p);
/* weapon_dropped: a weapon a chr held is dropped to the floor (a dead or
 * disarmed guard's gun, a player's): its pickup id, its item, and who held
 * it (a guard's id, GEVR_MODAPI_FROM_PLAYER, or GEVR_MODAPI_FROM_NONE) */
#define GEVR_MODAPI_FROM_PLAYER (-1)
#define GEVR_MODAPI_FROM_NONE (-2)
void gevrModApiWeaponDropped(int pickup, int item, int from);

/* A pickup's id: the setup record's index (pickup_spawning's), or one of the
 * game's runtime slots, as co-op's object messages number them (net_core.c
 * netObjectIndex): a weapon's 0x8000 + its slot, an ammo box's 0x9000 + its */
#define GEVR_MODAPI_PICKUP_WEAPONS 0x8000
#define GEVR_MODAPI_PICKUP_BOXES 0x9000
/* mod.pickups.drop: the game's own drop (drop_inventory's, a shot crate's box)
 * of a gun or thrown weapon (item, ammo 0) or an ammo box of a type (item 0,
 * ammo 1 to 13), from the local player (from GEVR_MODAPI_FROM_PLAYER) or a
 * guard (its id); the new pickup's id, or -1 (no such guard, no free slot) */
int gevrModGameDrop(int item, int ammo, int from);

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

/* ---- the hands area: the local human player's VR hands, the body's anchors, haptics ---- */

/* The body's anchors: the body slots' places (port/include/gevr_bodyslot.h
 * GEVR_BS_*, the same order), whether the slots option is on or not */
#define GEVR_MODAPI_ANCHORS 6
#define GEVR_MODAPI_HAPTIC_MAX_S 0.5f   /* one pulse at most */

/* ctrl is the body slots' (0 the off hand, 1 the gun hand); anchor a
 * GEVR_BS_* or -1 for none */
/* bondview2.c gevrGripGestureTick: the grip went down (taken: one of the
 * game's grips used it: a slot, the reload belt, a pull, a use) or up */
void gevrModApiGrip(int ctrl, int anchor, int taken);
void gevrModApiRelease(int ctrl, int anchor);
/* gevr_bodyslots.c: a hand came into a body slot that has something for it */
void gevrModApiHandNear(int ctrl, int anchor);

/* ---- the guards area: the setup's guards as they load; alerts, deaths and
 * corpses on the machine that runs the guards (solo, or the co-op host) ---- */

enum { GEVR_MODALERT_SAW = 1, GEVR_MODALERT_HEARD, GEVR_MODALERT_SHOT, GEVR_MODALERT_ALARM, GEVR_MODALERT_OTHER };
enum { GEVR_MODKILL_SHOT = 1, GEVR_MODKILL_EXPLOSION };

typedef struct GevrModGuardSpawn {
    int id;          /* its chrnum, the setup's */
    int index;       /* the setup record's */
    int body, head;  /* head -1: the body has its own */
    int weapon;      /* its setup gun, -1 none; the answer's goes to weaponAssignToHome */
} GevrModGuardSpawn;

/* guard_spawning: each mod's answer in turn into g (body, head, weapon) */
void gevrModApiGuardSpawning(GevrModGuardSpawn *g);
/* guard_alerted(id, how, player): told once per guard */
void gevrModApiGuardAlerted(int id, int how, int player);
/* guard_killed(id, how, item, player): item for a shot, -1 else; player
 * the slot whose shot killed it, -1 unknown (an explosion) */
void gevrModApiGuardKilled(int id, int how, int item, int player);
/* guard_spawned(id): a guard the game spawned in play (its scripts, its
 * reinforcements); a mod's own spawn returns the id instead */
void gevrModApiGuardSpawned(int id);

/* The guards a mod may spawn run one of these global AI lists (bondaicommands.h
 * GAILIST_*; gevr_modgame.c checks the numbers), or a level's own guard list */
enum {
    GEVR_MODAI_STANDARD = 2,   /* GAILIST_STANDARD_GUARD: idles, clones or runs to attack */
    GEVR_MODAI_DEAF = 5,       /* GAILIST_SIMPLE_GUARD_DEAF: stands or patrols; sight only */
    GEVR_MODAI_SIMPLE = 7,     /* GAILIST_SIMPLE_GUARD: stands or patrols until it notices */
    GEVR_MODAI_CHASE = 13,     /* GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND */
};

/* A guard a mod spawns (mod.guards.spawn) */
typedef struct GevrModGuardOrder {
    int pad;            /* a setup pad, or 10000 + a bound pad */
    int body, head;     /* head -1: the game picks it (or the body has its own) */
    int ai;             /* an AI list id */
    int weapon[2];      /* the right and left hands' guns, -1 none */
} GevrModGuardOrder;
/* corpse_retiring(id, x, y, z): the body is about to go */
void gevrModApiCorpseRetiring(int id, const float pos[3]);

/* The game's hooks (src/game/gevr_modgame.c) */
struct GuardRecord;
struct WeaponObjRecord;
struct ChrRecord;
/* chraction.c expand_09_characters, before the model is made: guard_spawning */
void gevrModGuardSpawning(struct GuardRecord *rec, int index, int *body, int *head);
/* prop.c weaponAssignToHome, a guard's setup gun: the one its guard_spawning chose */
void gevrModGuardWeapon(struct WeaponObjRecord *weapon);
/* chr.c chrAllocate: a slot's new guard, told nothing yet */
void gevrModGuardFresh(struct ChrRecord *chr);
/* chraction.c, chrai.c: a guard turns on a player (GEVR_MODALERT_*); told once */
void gevrModGuardAlert(struct ChrRecord *chr, int how);
/* chraction.c: a guard's death begins (GEVR_MODKILL_*; item for a shot, else -1) */
void gevrModGuardKilled(struct ChrRecord *chr, int how, int item);
/* chraction.c handles_shot_actors: a player's shot hit it (the host applies a
 * client's there too, with the shooter current) */
void gevrModGuardShotBy(struct ChrRecord *chr, int player);
/* chrai.c: a guard the game spawned (AI_TRYSpawningChrAtPad, NextToChr, a clone) */
void gevrModGuardSpawned(struct ChrRecord *chr);
/* chraction.c chrlvTickDead: the body is marked for removal */
void gevrModGuardRetiring(struct ChrRecord *chr);

/* ---- the damage and player area: damage to guards (where they run) and to
 * this machine's player (on its own headset); co-op's downed and revived ---- */

enum { GEVR_MODTARGET_GUARD = 1, GEVR_MODTARGET_PLAYER };
enum { GEVR_MODHOW_SHOT = 1, GEVR_MODHOW_EXPLOSION, GEVR_MODHOW_GAS, GEVR_MODHOW_HIT };
enum { GEVR_MODPART_HEAD = 1, GEVR_MODPART_BODY, GEVR_MODPART_LIMB, GEVR_MODPART_GUN, GEVR_MODPART_HAT };
#define GEVR_MODAPI_DAMAGE_MAX 1000.0f   /* before_damage's answer: 0 to this */

typedef struct GevrModDamage {
    int target;       /* GEVR_MODTARGET_* */
    int id;           /* a guard's chrnum, a player's slot */
    float amount;     /* the game's units: a guard's as its health; a player's, 1 a full bar */
    int how;          /* GEVR_MODHOW_* */
    int part;         /* a guard's shot: GEVR_MODPART_*, 0 none */
    int item;         /* a shot's gun, -1 */
    int by;           /* the player slot behind it, -1 none known */
    float health;     /* damage_applied: what's left (a guard's in its units; a player's 0 to 1) */
    float armour;     /* damage_applied, a player's: 0 to 1 */
    int cancelled;    /* before_damage's answer: none at all */
} GevrModDamage;

/* before_damage: each mod's answer in turn into d (amount, cancelled) */
void gevrModApiBeforeDamage(GevrModDamage *d);
/* damage_applied: what it did */
void gevrModApiDamageApplied(const GevrModDamage *d);
/* player_downed / player_revived (co-op), player_died */
void gevrModApiPlayerDowned(int slot, int down);
void gevrModApiPlayerDied(int slot);

/* The game's hooks (src/game/gevr_modgame.c) */
/* chraction.c handles_shot_actors, chrlvExplosionDamage: before a guard's
 * damage; 1 when a mod cancelled it, else *amount is the mod's (or the game's) */
int gevrModGuardBeforeDamage(struct ChrRecord *chr, float *amount, int how, int hitpart, int item, int player);
void gevrModGuardDamaged(struct ChrRecord *chr, float amount, int how, int hitpart, int item, int player);
/* bondview2.c record_damage_kills, for the current player: before (1 cancelled)
 * and after; by the game's attacker (-1 none), affectsArmour 0 for gas */
int gevrModPlayerBeforeDamage(float *amount, int by, int affectsArmour);
void gevrModPlayerDamaged(float amount, int by, int affectsArmour, float healthBefore, float armourBefore);
/* bondview2.c bondviewKillCurrentPlayer: the current player died */
void gevrModPlayerDied(void);

/* ---- the assets area, part 1: the cartridge's own sound effects and smoke,
 * on this headset (a mod tells the others over mod.net) ---- */

enum { GEVR_MODAT_PLAYER = 1, GEVR_MODAT_GUARD, GEVR_MODAT_PAD };
#define GEVR_MODAPI_SMOKE_TYPES 10   /* explosion.c g_SmokeTypes' rows past the empty first */
#define GEVR_MODAPI_SOUND_NEAR 5000.0f   /* a placed sound: full within, as chrobjSndCreatePostEventDefault */
#define GEVR_MODAPI_SOUND_FAR 6000.0f

/* ---- the network (plan step 5): mod.net between the players' machines ---- */

#define GEVR_MODAPI_NET_PAYLOAD 1024      /* a message's bytes (net_protocol.h NET_MOD_PAYLOAD_MAX) */
#define GEVR_MODAPI_NET_CHANNEL 32        /* a channel's name (NET_MOD_CHANNEL_MAX) */
#define GEVR_MODAPI_NET_INBOX 64          /* messages held until play */
#define GEVR_MODAPI_NET_SENDS 30          /* a mod's sends a second */
#define GEVR_MODAPI_NET_BYTES 16384       /* and their bytes */
enum { GEVR_MODNET_DATA = 0, GEVR_MODNET_OFF = 1 };   /* net_protocol.h NET_MOD_* */

/* net_core.c: a mod's message arrived (from: the sender's slot, -1 a
 * dedicated server); held, and handed to the mod at the next frame in play */
void gevrModNetReceive(const char *mod, int from, const char *channel, const void *data, int len);
/* net_core.c: the mod stopped on another player's headset */
void gevrModNetStopped(const char *mod, int from);
/* net_core.c netReceiveStageReady, the host: a player is in the stage;
 * peer_ready at the next frame in play */
void gevrModApiPeerReady(int slot);
/* gevr_modscript.c: a mod stopped here (an error, over budget): the others too */
void gevrModApiStopped(const char *id);
/* gevr_modscript.c: each frame in play, the held messages to their mods; as a
 * mission loads, the last one's go */
void gevrModApiNetFlush(void);
void gevrModApiNetClear(void);

/* ---- the UI: mods' pages on the in-game screen (mod.ui) ---- */

/* A page is a sub-tab under the in-game screen's Mods/Fun tab (port/vr/
 * gevr_modui_ui.h): a line of text, rows drawn as the settings' are, and a
 * row of buttons. The limits keep the fullest page on the screen without
 * scrolling (port/tests/ingame_fit_native.cpp). */
#define GEVR_MODAPI_UI_PAGES 4       /* every mod's together */
#define GEVR_MODAPI_UI_ROWS 8
#define GEVR_MODAPI_UI_BUTTONS 4
#define GEVR_MODAPI_UI_CHOICES 12
#define GEVR_MODAPI_UI_ID 32         /* a page's, row's or button's id */
#define GEVR_MODAPI_UI_TITLE 12      /* a page's tab */
#define GEVR_MODAPI_UI_LABEL 24      /* a row's label, a choice's name */
#define GEVR_MODAPI_UI_BUTTON 14     /* a button's label */
#define GEVR_MODAPI_UI_TEXT 160      /* the text above the rows, a tip */
#define GEVR_MODAPI_UI_REASON 32     /* why a row or button can't be used now */
#define GEVR_MODAPI_UI_RANGE 100000  /* a slider's ends: -this to this */
#define GEVR_MODAPI_UI_STEPS 1000    /* and at most this many steps between them */
#define GEVR_MODAPI_UI_QUEUE 32      /* clicks and changes waiting for the mod's next frame */
enum { GEVR_MODUI_TOGGLE = 1, GEVR_MODUI_CHOICE, GEVR_MODUI_SLIDER };

typedef struct GevrModUiRow {
    int kind;                        /* GEVR_MODUI_* */
    char id[GEVR_MODAPI_UI_ID + 1];
    char label[GEVR_MODAPI_UI_LABEL + 1];
    char tip[GEVR_MODAPI_UI_TEXT + 1];          /* "" none */
    char blocked[GEVR_MODAPI_UI_REASON + 1];    /* "" usable; else greyed, with this reason */
    int value;                       /* toggle 0 or 1; choice 0 to count - 1; slider min to max */
    int min, max;                    /* a slider's */
    int count;                       /* a choice's */
    char choices[GEVR_MODAPI_UI_CHOICES][GEVR_MODAPI_UI_LABEL + 1];
} GevrModUiRow;
typedef struct GevrModUiButton {
    char id[GEVR_MODAPI_UI_ID + 1];
    char label[GEVR_MODAPI_UI_BUTTON + 1];
    char blocked[GEVR_MODAPI_UI_REASON + 1];    /* "" usable */
} GevrModUiButton;
typedef struct GevrModUiPage {
    char mod[64];                    /* the mod that made it: its events go there alone */
    char id[GEVR_MODAPI_UI_ID + 1];
    char title[GEVR_MODAPI_UI_TITLE + 1];
    char text[GEVR_MODAPI_UI_TEXT + 1];
    int rows, buttons;
    GevrModUiRow row[GEVR_MODAPI_UI_ROWS];
    GevrModUiButton button[GEVR_MODAPI_UI_BUTTONS];
} GevrModUiPage;

/* The in-game screen's side (port/vr/gevr_modui_ui.h, vr_pause_menu.cpp).
 * Pages shown now: their mods run in a mission (a stopped mod's hide). i is
 * 0 to gevrModUiCount() - 1, as they're shown this frame. */
int gevrModUiCount(void);
const GevrModUiPage *gevrModUiPage(int i);
/* A row changed: kept at once, and ui_change to its mod at the next frame */
void gevrModUiSet(int i, int row, int value);
/* A button pressed: ui_click to its mod at the next frame */
void gevrModUiClick(int i, int button);
/* The page mod.ui.open asked for, as shown now (-1 none): asked until its
 * tab is up (gevrModUiOpened) or the screen closes */
int gevrModUiAsked(void);
void gevrModUiOpened(void);
/* gevr_modscript.c: each frame in play, the clicks and changes to their mods;
 * as a mission loads, the last one's go (the pages stay) */
void gevrModApiUiFlush(void);
void gevrModApiUiClear(void);

/* ---- the game's side (src/game/gevr_modgame.c), for gevr_modapi.c ---- */

typedef struct GevrModWeaponStats {
    int magazine;     /* MagSize */
    int fireRate;     /* AutomaticFiringRate, ticks between shots; -1: not automatic */
    float damage;     /* DestructionAmount */
    float inaccuracy; /* Inaccuracy: the KF7's 10, the sniper rifle's 0 */
    float recoilBack; /* RecoilBack: how far the hands pull back at a shot */
    float recoilUp;   /* RecoilUp: how far the gun kicks up */
    int sound;        /* Sound: the shot's ROM sound effect (mod.audio's ids) */
    float loudnessMin;   /* LoudnessMin, LoudnessMax: how far guards hear a shot */
    float loudnessMax;
    int automatic;    /* fired automatically while the trigger's held (gunfire.c's firing switch) */
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
/* The hand's VR scope (bondview2.c gevrScopeModSet): GEVR_MODSCOPE_GAME the
 * game's own scoped guns, _MOD a mod's while the hand holds item (at: the
 * eyepiece's centre in the drawn model's own coordinates, radius in them,
 * zoomDeg the angle the lens shows), _NONE none even on a scoped gun */
void gevrModGameScope(int hand, int mode, int item, const float at[3], float radius, float zoomDeg);
/* Which models the hand's gun is drawn with: 0 the cartridge's, 1 GoldenEye
 * X's, 2 Bean's (bondview2.c gevrFitLook), 3 a mod's (mod.weapon.model) */
int gevrModGameLook(int hand);
/* A mod's model for the gun a hand draws (gun.c gevrModGunModelSet): the
 * run's mod's assets/models/<name> for item in that hand, or name NULL the
 * item's own. 0 when the run's mod has no such file (nothing changes) */
int gevrModGameGunModel(int hand, int item, const char *name);
/* every hand's own models back (a mod stopped, a new mission) */
void gevrModGameGunModelReset(void);

/* mod.attach (src/game/gevr_modattach.c): a pack's model on the local
 * player's gun, a hand or the body, placed each rendered frame */
#define GEVR_MODAPI_ATTACH_MAX 16      /* attached at once, every mod's together */
#define GEVR_MODAPI_ATTACH_MODELS 8    /* model files loaded in a mission */
#define GEVR_MODAPI_ATTACH_RANGE 4000.0f   /* at: model units (a gun is about 800 long) */
#define GEVR_MODAPI_ATTACH_SCALE_MIN 0.01f
#define GEVR_MODAPI_ATTACH_SCALE_MAX 100.0f
#define GEVR_MODAPI_ATTACH_NODES 256       /* node: a gun model's matrix, 0 to 255 */
enum { GEVR_MODATTACH_GUN, GEVR_MODATTACH_HAND, GEVR_MODATTACH_BODY };
typedef struct GevrModAttach {
    char model[64];        /* assets/models/<model> */
    char like[64];         /* the cartridge model file it's built over (a G gun or P prop) */
    int on;                /* GEVR_MODATTACH_* */
    int where;             /* the hand (0 right, 1 left), or the body slot (the anchors' order) */
    int node;              /* on the gun: the matrix of its model it rides (0 the root) */
    float offset[4][4];    /* in the anchor's frame, its units: rows the model's axes, then its place */
} GevrModAttach;
/* attach: its id from 1; 0 when the model won't load (no file, no such like,
 * a file the like's header doesn't fit); -1 with all attached */
int gevrModGameAttach(const GevrModAttach *a);
int gevrModGameDetach(int id);
int gevrModGameAttachShow(int id, int shown);
int gevrModGameAttachPlace(int id, const float offset[4][4]);   /* a new offset, from the next frame */
void gevrModGameAttachReset(void);   /* none attached (a new mission, a mod stopped) */
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
/* The torso's frame (gevr_bodyslots.c, this tick's), cm: x toward the gun
 * hand's side, y up, z ahead, from the eye's place upright over the torso.
 * A hand (0 right, the gun hand; 1 left) or an anchor's centre and reach;
 * 0 when there's no frame (no VR hands in play) or the hand isn't tracked */
int gevrModGameHandAt(int hand, float out[3]);
int gevrModGameAnchorAt(int anchor, float out[3], float *radius);
/* A pulse on a hand's controller, scaled by the player's Mods haptics
 * setting; 0 when there are no VR haptics */
int gevrModGameHaptic(int hand, float amplitude, float seconds);
/* A guard as the game has it now (on a co-op client, the host's copy as
 * last sent). health: what's left of maxHealth; pos: world units */
typedef struct GevrModGuardState {
    int id, body, head;   /* head -1: the body's own */
    int alive, alerted;
    int weapon[2];        /* right and left hands' items, -1 none */
    float health, maxHealth;
    float pos[3];
} GevrModGuardState;
/* The guards in the stage (living or dying): their ids, at most max; how many */
int gevrModGameGuards(int *ids, int max);
int gevrModGameGuard(int id, GevrModGuardState *out);
/* guard_spawning's limits: a body (1 takes a head, 2 has its own), a
 * head, a gun a guard can hold (with a model in this cartridge) */
int gevrModGameBodyOk(int body);
int gevrModGameHeadOk(int head);
int gevrModGameGuardGun(int item);
/* The calls that change a guard work where the guards run (solo, the co-op
 * host) and in a stage; elsewhere these return 0 or -1 */
int gevrModGameRunsGuards(void);
/* An AI list a spawned guard may run: one there is, a guard's (not a level
 * script's), not a subroutine */
int gevrModGameAiListOk(int id);
/* The guard's id, or -1 (a bad pad, no room for it there, no free slot) */
int gevrModGameSpawnGuard(const GevrModGuardOrder *o);
/* A gun into a living guard's hand (0 right, 1 left), the one it held gone */
int gevrModGameGiveGuardWeapon(int id, int item, int hand);
/* The guard along the stage's patrol path, as AI_StartPatrol */
int gevrModGamePatrol(int id, int path);
/* The stage's patrol paths' ids, at most max; how many */
int gevrModGamePaths(int *ids, int max);
/* The setup's pads: how many (ids 0 up), and its bound pads (10000 up) */
int gevrModGamePadCount(int *bound);
typedef struct GevrModPad {
    float pos[3];    /* world units, as mod.guards.get's */
    float look[3];   /* the way it faces */
    int room;        /* its tile's room, -1 none */
} GevrModPad;
/* A pad by id: 1 when the setup has it */
int gevrModGamePad(int pad, GevrModPad *out);
/* The guard to a pad (chrGoToPad, as AI_WalkToPad, AI_RunToPad and
 * AI_SprintToPad: speed 0 walk, 1 run, 2 sprint), or turned to face one
 * (check_set_actor_standing_still with TARGET_PAD, as AI_TRYFacingTarget):
 * 1 when the game took it */
int gevrModGameGoTo(int id, int pad, int speed);
int gevrModGameFace(int id, int pad);
/* A hat on a guard that has none, as AI_TRYGiveMeHat (hatCreateForChr):
 * hat 0 to GEVR_MODAPI_HATS - 1, the cartridge's hats in mod.guards.giveHat's
 * names' order (PROP_HATFURRY to PROP_HATPEAKED); 1 when it's on */
#define GEVR_MODAPI_HATS 12
int gevrModGameGiveHat(int id, int hat);

/* mod.mission: the difficulty chosen (0 Agent to 3 007; -1 none, as in
 * multiplayer) and the save folder the run plays from (0 to 3, -1 none) */
int gevrModGameDifficulty(void);
int gevrModGameFolder(void);
/* mod.mission.complete: mission (0 Dam to 19 Egypt, the folder screen's
 * order) marked complete at a difficulty (and those below it) in the run's
 * save folder, through the game's own fileUnlockStageInFolderAtDifficulty;
 * seconds its time, or 0 for none. Only in a mod campaign, whose save is its
 * own file (eeprom-mod-<id>.bin), never the base game's. 1 when written */
#define GEVR_MODAPI_MISSIONS 20
#define GEVR_MODAPI_NO_TIME 99999999   /* the game's own "no time" (cheat.c's unlocks) */
int gevrModGameComplete(int mission, int difficulty, int seconds);
/* This machine's player's health and armour, 0 to 1; 0 when no player is in a stage */
int gevrModGamePlayerHealth(float *health, float *armour);
/* Set them: health more than 0 to 1, armour 0 to 1; 0 when the player is
 * dead, down (co-op) or not in a stage */
int gevrModGameSetPlayerHealth(float health);
int gevrModGameSetPlayerArmour(float armour);
/* The sound effects bank's count: ids 1 to it (sndPlaySfx's, 1-based) */
int gevrModGameSoundCount(void);
/* mod.player.setSpeed: the local player's walking and strafing speed, times
 * scale (1 the game's own), until the mission ends or the mod stops */
#define GEVR_MODAPI_SPEED_MIN 0.25f
#define GEVR_MODAPI_SPEED_MAX 2.0f
void gevrModGameSetSpeed(float scale);
/* Play one in a stage: volume 0 to 1 sets it, -1 keeps its own; pos (world
 * units) places it, heard from there; 0 when it couldn't */
int gevrModGamePlaySound(int id, float volume, const float *pos);
/* Smoke (a g_SmokeTypes row, 1 to GEVR_MODAPI_SMOKE_TYPES) where this
 * headset's player, a guard or a pad is; 0 when there's no such place */
int gevrModGameSmoke(int type, int at, int which);

#ifdef __cplusplus
}
#endif

#endif
