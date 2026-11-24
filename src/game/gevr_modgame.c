/*
 * The mod API's game side (port/include/gevr_modapi.h): the few reads and
 * writes port/src/gevr_modapi.c needs from the game, kept thin so that
 * file needs none of the game's headers (and test_modapi.py runs it with a
 * fake game).
 */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bondview.h"
#include "player.h"
#include <string.h>
#include "gun.h"
#include "bondinv.h"
#include "loadobjectmodel.h"
#include "gevr_modapi.h"
#include "gevr_modscript.h"
#include "gevr_bodyslots.h"
#include "chr.h"
#include "chrobjdata.h"
#include <bondaicommands.h>
#include "music.h"       /* g_musicSfxBufferPtr: the sound effects bank */
#include "snd.h"
#include "explosion.h"
#include "chrai.h"            /* g_WeaponSlots, g_AmmoCrates: the game's runtime pickups */
#include "loadobjectmodel.h"  /* sizepropdef */
#include "propobj.h"
#include "model.h"            /* modelSetScale */

extern bool netIsActive(void);       /* net_core.c */
extern int netGetLocalSlot(void);
extern WeaponStats default_weaponstats;   /* gunWeaponStats.inc.c: every item without a model shares it */
extern stagesetup g_CurrentSetup;         /* chrai.h */
extern f32 g_SoloAmmoMultiplier;          /* propobj.h */
extern PROP getPropForHeldItem(ITEM_IDS item);                  /* player.c */
extern s32 get_ammo_in_magazine(AmmoCrateRecord *crate);        /* propobj.c */
extern void gunRequestHandWeaponChange(enum GUNHAND hand, s32 nextWeapon, s32 cycleDirection);   /* gun.c */
extern int vr_haptics_ready(void);                                              /* port/vr, port/flat */
extern void vrHapticsGetRumble(int id, float *amp, float *dur, float *freq);
extern s32 trigger_haptic_vibration_freq_c(int hand_index, float amplitude, float duration, float frequency);
#define GEVR_ACTION_MOD 1008   /* port/vr/vr_haptics.h: the player's Mods row */
extern bool netIsHost(void);
extern s32 get_cur_playernum(void);
extern s32 gevrBodyChooseHead(s32 id, s32 head);                   /* chraction.c */
extern AIRecord *ailistFindById(s32 ID);                            /* chrai.c */
extern PathRecord *pathFindById(s32 ID);
extern PropRecord *chrSpawnAtPad(ChrRecord *self, s32 bodynum, s32 headnum, s32 padid, AIRecord *ailist, s32 flags);
extern bool if_actor_able_set_on_path(ChrRecord *self, struct PathRecord *path);   /* chraction.c */
extern PropRecord *chrGiveWeapon(ChrRecord *self, s32 PropID, ITEM_IDS ItemID, s32 flags);   /* propobj.c */
extern void objFreePermanently(ObjectRecord *obj, bool freeprop);
extern int gevrCoopLocalDowned(void);                              /* net_coop.c */
extern s32 s_gevrExplosionDamage;                                   /* explosion.c: the damage being dealt is a blast's */
extern s32 sub_GAME_7F053894(coord3d *pos, f32 low, f32 high);      /* propobj.c: a placed sound's volume, heard from here */
extern void gevrSndSpatialPlace(ALSoundState *state, const coord3d *pos);   /* gevr_sndpath.c */

/* a spawned guard's named behaviours are these global lists */
typedef char aiNamesAreTheGlobalLists[(int)GEVR_MODAI_STANDARD == (int)GAILIST_STANDARD_GUARD
                                      && (int)GEVR_MODAI_DEAF == (int)GAILIST_SIMPLE_GUARD_DEAF
                                      && (int)GEVR_MODAI_SIMPLE == (int)GAILIST_SIMPLE_GUARD
                                      && (int)GEVR_MODAI_CHASE == (int)GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND ? 1 : -1];

/* mod.items names every item from ITEM_UNARMED; mod.ammo every type a pickup carries */
typedef char itemNamesForEveryItem[GEVR_MODAPI_ITEMS == ITEM_IDS_MAX ? 1 : -1];
typedef char ammoNamesForEveryType[GEVR_MODAPI_AMMO_TYPES == AMMOTYPE_GLOBAL_MAX ? 1 : -1];

static s32 localSlot(void)
{
    return netIsActive() ? netGetLocalSlot() : 0;
}

int gevrModGameLocal(void)
{
    (void)sizeof(itemNamesForEveryItem);
    (void)sizeof(ammoNamesForEveryType);
    /* as gunfire.c's motion throws and lv.c's reload ask: offline, every tick is this player's */
    return !netIsActive() || get_cur_playernum() == netGetLocalSlot();
}

int gevrModGamePlayerReady(void)
{
    const s32 me = localSlot();

    return me >= 0 && me < getPlayerCount() && g_playerPointers[me] != NULL;
}

/* The local player's hand, whoever the game is ticking now */
static struct hand *localHand(int hand, s32 *was)
{
    *was = get_cur_playernum();
    set_cur_player(localSlot());
    return &g_CurrentPlayer->hands[hand ? GUNLEFT : GUNRIGHT];
}

int gevrModGameHeld(int hand)
{
    s32 was;
    int item;

    if (!gevrModGamePlayerReady()) return -1;
    localHand(hand, &was);
    item = getCurrentPlayerWeaponId(hand ? GUNLEFT : GUNRIGHT);
    set_cur_player(was);
    return item;
}

int gevrModGameLoaded(int hand)
{
    s32 was;
    int rounds;

    if (!gevrModGamePlayerReady()) return 0;
    rounds = localHand(hand, &was)->weapon_ammo_in_magazine;
    set_cur_player(was);
    return rounds;
}

/* bondview2.c: the per-hand VR scope's mod override, and the model set */
extern void gevrScopeModSet(s32 hand, s32 mode, s32 item, const f32 at[3], f32 radius, f32 zoomDeg);
extern int gevrFitLook(int hand);

void gevrModGameScope(int hand, int mode, int item, const float at[3], float radius, float zoomDeg)
{
    gevrScopeModSet(hand, mode, item, at, radius, zoomDeg);
}

extern s32 gevrModGunHeld(s32 hand);                                        /* gun.c */
extern void gevrModGunModelSet(s32 hand, s32 item, const char *name);
extern void gevrModGunModelReset(void);
#include "gevr_modassets.h"
#include "gevr_modmesh.h"

int gevrModGameLook(int hand)
{
    if (!gevrModGamePlayerReady() || hand < 0 || hand > 1) return 0;
    return gevrModGunHeld(hand) ? 3 : gevrFitLook(hand);
}

int gevrModGameGunModel(int hand, int item, const char *name)
{
    const GevrModMesh *mesh;
    if (name != NULL) {
        int native;
        if (!gevrModAssetHasModel(name)) return 0;
        native = gevrModMeshLoad(name, &mesh);
        if (native < 0) return 0;
    }
    gevrModGunModelSet(hand, item, name);
    return 1;
}

void gevrModGameGunModelReset(void)
{
    gevrModGunModelReset();
}

void gevrModGameSetLoaded(int hand, int rounds)
{
    s32 was;

    if (!gevrModGamePlayerReady()) return;
    localHand(hand, &was)->weapon_ammo_in_magazine = rounds;
    set_cur_player(was);
}

/* Automatic fire by the item, as gunfire.c's firing switch decides it, and a
 * mod's say per item (setStats' automatic): 0 the game's own, 1 automatic, 2
 * a shot a press. Put back with the stats (gevrModApiReset) */
static s8 s_modFireMode[ITEM_IDS_MAX];

static s32 nativeAutomatic(s32 item)
{
    switch (GEVR_ITEM_AS(item))
    {
        case ITEM_SKORPION: case ITEM_AK47: case ITEM_UZI: case ITEM_MP5K:
        case ITEM_MP5KSIL: case ITEM_SPECTRE: case ITEM_M16: case ITEM_FNP90:
            return 1;
        default:
            return 0;
    }
}

/* A thrown item's trigger throw (setStats' throwSpeed, throwLift and
 * throwGuide; gun.c gevrThrowTrigger, gunfire.c gevrThrowGuideDraw): a speed
 * of 0 and no lift set are the game's own. Put back with the stats */
#define GAME_THROW_LIFT 5.0f   /* gun.c's throws' own */
extern s32 gevrIsThrowable(s32 item);        /* input.c */
extern f32 gevrThrowBaseSpeed(s32 item);     /* gun.c */
static f32 s_modThrowSpeed[ITEM_IDS_MAX], s_modThrowLift[ITEM_IDS_MAX];
static s8 s_modThrowLiftSet[ITEM_IDS_MAX], s_modThrowGuide[ITEM_IDS_MAX];
/* and its guide's look, when a mod gave one (else the game's tracer) */
static s8 s_modGuideStyled[ITEM_IDS_MAX], s_modGuideFade[ITEM_IDS_MAX];
static f32 s_modGuideLength[ITEM_IDS_MAX], s_modGuideWidth[ITEM_IDS_MAX];
static u8 s_modGuideColour[ITEM_IDS_MAX][3];

void gevrModThrowStats(s32 item, f32 *speed, f32 *lift)
{
    if (item < 0 || item >= ITEM_IDS_MAX) return;
    if (s_modThrowSpeed[item] > 0.0f) *speed = s_modThrowSpeed[item];
    if (s_modThrowLiftSet[item]) *lift = s_modThrowLift[item];
}

s32 gevrModThrowGuide(s32 item)
{
    return item >= 0 && item < ITEM_IDS_MAX && s_modThrowGuide[item];
}

/* gunfire.c gevrThrowGuideDraw: the look a mod gave the item's guide; 0 the game's tracer */
s32 gevrModThrowGuideLook(s32 item, f32 *length, f32 *width, u8 rgb[3], s32 *fade)
{
    if (item < 0 || item >= ITEM_IDS_MAX || !s_modThrowGuide[item] || !s_modGuideStyled[item]) return FALSE;
    *length = s_modGuideLength[item];
    *width = s_modGuideWidth[item];
    rgb[0] = s_modGuideColour[item][0];
    rgb[1] = s_modGuideColour[item][1];
    rgb[2] = s_modGuideColour[item][2];
    *fade = s_modGuideFade[item];
    return TRUE;
}

/* gunfire.c: the firing switch's case for the item: an automatic's (the
 * AK47's) or a shot a press (the PP7's) when a mod chose, else as */
s32 gevrModFireAs(s32 item, s32 as)
{
    if (item < 0 || item >= ITEM_IDS_MAX || s_modFireMode[item] == 0) return as;
    return s_modFireMode[item] == 1 ? ITEM_AK47 : ITEM_WPPK;
}

int gevrModGameStats(int item, GevrModWeaponStats *out)
{
    const WeaponStats *st;

    if (item < 0 || item >= ITEM_IDS_MAX) return 0;
    st = get_ptr_item_statistics((ITEM_IDS)item);
    if (!st || st == &default_weaponstats) return 0;
    out->magazine = st->MagSize;
    out->fireRate = st->AutomaticFiringRate == 0xFF ? -1 : (int)st->AutomaticFiringRate;
    out->damage = st->DestructionAmount;
    out->inaccuracy = st->Inaccuracy;
    out->recoilBack = st->RecoilBack;
    out->recoilUp = st->RecoilUp;
    out->sound = st->Sound;
    out->loudnessMin = st->LoudnessMin;
    out->loudnessMax = st->LoudnessMax;
    out->automatic = s_modFireMode[item] ? s_modFireMode[item] == 1 : nativeAutomatic(item);
    if (gevrIsThrowable(item))
    {
        out->throwSpeed = s_modThrowSpeed[item] > 0.0f ? s_modThrowSpeed[item] : gevrThrowBaseSpeed(item);
        out->throwLift = s_modThrowLiftSet[item] ? s_modThrowLift[item] : GAME_THROW_LIFT;
        out->throwGuide = s_modThrowGuide[item];
        out->guideLength = s_modGuideStyled[item] ? s_modGuideLength[item] : GEVR_MODAPI_GUIDE_LENGTH;
        out->guideWidth = s_modGuideStyled[item] ? s_modGuideWidth[item] : GEVR_MODAPI_GUIDE_WIDTH;
        out->guideColour[0] = s_modGuideStyled[item] ? s_modGuideColour[item][0] : 255;
        out->guideColour[1] = s_modGuideStyled[item] ? s_modGuideColour[item][1] : 255;
        out->guideColour[2] = s_modGuideStyled[item] ? s_modGuideColour[item][2] : 255;
        out->guideFade = s_modGuideStyled[item] ? s_modGuideFade[item] : 0;
    }
    else
    {
        out->throwSpeed = 0.0f;
        out->throwLift = 0.0f;
        out->throwGuide = 0;
        out->guideLength = 0.0f;
        out->guideWidth = 0.0f;
        out->guideColour[0] = out->guideColour[1] = out->guideColour[2] = 0;
        out->guideFade = 0;
    }
    return 1;
}

void gevrModGameSetStats(int item, const GevrModWeaponStats *in)
{
    WeaponStats *st;

    if (item < 0 || item >= ITEM_IDS_MAX) return;
    st = get_ptr_item_statistics((ITEM_IDS)item);
    if (!st || st == &default_weaponstats) return;
    st->MagSize = (s16)in->magazine;
    /* -1 is the game's own "none" (0xFF), so a gun made automatic gets it back */
    st->AutomaticFiringRate = in->fireRate >= 0 ? (u8)in->fireRate : 0xFF;
    s_modFireMode[item] = (in->automatic != 0) == (nativeAutomatic(item) != 0) ? 0 : in->automatic ? 1 : 2;
    if (gevrIsThrowable(item))
    {
        s_modThrowSpeed[item] = in->throwSpeed > 0.0f && in->throwSpeed != gevrThrowBaseSpeed(item) ? in->throwSpeed : 0.0f;
        s_modThrowLiftSet[item] = in->throwLift != GAME_THROW_LIFT;
        s_modThrowLift[item] = in->throwLift;
        s_modThrowGuide[item] = in->throwGuide != 0;
        s_modGuideLength[item] = in->guideLength;
        s_modGuideWidth[item] = in->guideWidth;
        s_modGuideColour[item][0] = (u8)in->guideColour[0];
        s_modGuideColour[item][1] = (u8)in->guideColour[1];
        s_modGuideColour[item][2] = (u8)in->guideColour[2];
        s_modGuideFade[item] = in->guideFade != 0;
        s_modGuideStyled[item] = in->guideLength != GEVR_MODAPI_GUIDE_LENGTH || in->guideWidth != GEVR_MODAPI_GUIDE_WIDTH
            || in->guideColour[0] != 255 || in->guideColour[1] != 255 || in->guideColour[2] != 255 || in->guideFade;
    }
    st->DestructionAmount = in->damage;
    st->Inaccuracy = in->inaccuracy;
    st->RecoilBack = in->recoilBack;
    st->RecoilUp = in->recoilUp;
    st->Sound = (u16)in->sound;
    st->LoudnessMin = in->loudnessMin;
    st->LoudnessMax = in->loudnessMax;
}

/* mod.player.setSpeed's scale (bondview2.c, the local player's walk) */
static f32 s_modMoveScale = 1.0f;

void gevrModGameSetSpeed(float scale)
{
    s_modMoveScale = scale;
}

f32 gevrModMoveScale(void)
{
    return gevrModScriptRunning() && gevrModGameLocal() ? s_modMoveScale : 1.0f;
}

/* ---- the items area ---- */

int gevrModGameSwappable(int item)
{
    s32 as;

    if (item <= ITEM_FIST || item >= ITEM_IDS_MAX) return 0;
    as = GEVR_ITEM_AS(item);   /* a hack's gun on a special item's slot counts as the gun it is */
    if (!((as >= ITEM_KNIFE && as <= ITEM_ROCKETLAUNCH && as != ITEM_WATCHLASER) || as == ITEM_GRENADE
          || as == ITEM_TIMEDMINE || as == ITEM_PROXIMITYMINE || as == ITEM_REMOTEMINE))
    {
        return 0;
    }
    return getPropForHeldItem((ITEM_IDS)item) >= 0;   /* a model to lie on the floor */
}

int gevrModGameHas(int item)
{
    s32 was;
    int has;

    if (!gevrModGamePlayerReady()) return 0;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    has = bondinvHasInvItem((ITEM_IDS)item) != 0;
    set_cur_player(was);
    return has;
}

int gevrModGameGive(int item)
{
    s32 was;
    int given;

    if (!gevrModGamePlayerReady()) return 0;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    given = !bondinvHasInvItem((ITEM_IDS)item);
    bondinvAddInvItem((ITEM_IDS)item);
    if (item == ITEM_REMOTEMINE)
    {
        bondinvAddInvItem(ITEM_TRIGGER);   /* as a pickup of them does: the detonator */
    }
    set_cur_player(was);
    return given;
}

/* As horde mode's trade (gevr_horde.c): the pair and the gun leave the
 * inventory, and a hand holding it goes empty (the gun hand to its fist) */
int gevrModGameTake(int item)
{
    InvItem *pair;
    s32 was;
    int had;

    if (!gevrModGamePlayerReady()) return 0;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    had = bondinvHasInvItem((ITEM_IDS)item) != 0;
    pair = bondinvGetDualWeapon((ITEM_IDS)item, (ITEM_IDS)item);
    if (pair)
    {
        bondinvRemoveItem(pair);
        had = 1;
    }
    bondinvRemoveItemByID((ITEM_IDS)item);
    if (getCurrentPlayerWeaponId(GUNLEFT) == item) gunRequestHandWeaponChange(GUNLEFT, ITEM_UNARMED, 1);
    if (getCurrentPlayerWeaponId(GUNRIGHT) == item) gunRequestHandWeaponChange(GUNRIGHT, ITEM_FIST, 1);
    set_cur_player(was);
    return had;
}

int gevrModGameAmmo(int type)
{
    s32 was;
    int rounds;

    if (!gevrModGamePlayerReady() || type <= AMMO_NONE || type > AMMOTYPE_GLOBAL_MAX) return 0;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    rounds = g_CurrentPlayer->ammoheldarr[type];
    set_cur_player(was);
    return rounds;
}

/* The spare rounds alone: not give_cur_player_ammo, which fills a held
 * single-use item's magazine instead */
void gevrModGameSetAmmo(int type, int rounds)
{
    s32 was;

    if (!gevrModGamePlayerReady() || type <= AMMO_NONE || type > AMMOTYPE_GLOBAL_MAX) return;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    g_CurrentPlayer->ammoheldarr[type] = rounds;
    set_cur_player(was);
}

int gevrModGameAmmoMax(int type)
{
    return type > AMMO_NONE && type <= AMMOTYPE_GLOBAL_MAX ? get_max_ammo_for_type(type) : 0;
}

/* The records a swap leaves alone, by setup index: the stage's tags (an
 * objective or the level's script finds its object by one), renames (a
 * mission item's own name), dual pairs and a safe's contents */
#define PICKUP_SCAN_MAX 16384
static u8 s_kept[PICKUP_SCAN_MAX / 8];

static void keep(s32 index)
{
    if (index >= 0 && index < PICKUP_SCAN_MAX) s_kept[index >> 3] |= (u8)(1u << (index & 7));
}

static int kept(s32 index)
{
    return index < 0 || index >= PICKUP_SCAN_MAX || ((s_kept[index >> 3] >> (index & 7)) & 1);
}

static void guardWeaponsReset(void);

void gevrModPickupScan(void)
{
    PropDefHeaderRecord *rec = g_CurrentSetup.propDefs;
    s32 i;

    memset(s_kept, 0, sizeof(s_kept));
    guardWeaponsReset();   /* a new stage's guards */
    if (!gevrModScriptRunning()) return;
    /* before proplvreset2's loop: the links still hold indices, not pointers */
    for (i = 0; rec && rec->type != PROPDEF_END; i++)
    {
        switch (rec->type)
        {
            case PROPDEF_TAG:       keep(i + ((TagObjectRecord *)rec)->OffsetToObj); break;
            case PROPDEF_RENAME:    keep(i + ((RenameObjectRecord *)rec)->TagID); break;
            case PROPDEF_LINK:      keep(i + ((LinkRecord *)rec)->Index1); keep(i + ((LinkRecord *)rec)->Index2); break;
            case PROPDEF_SAFE_ITEM: keep(i + ((SafeObjectRecord *)rec)->Index1); break;
            default: break;
        }
        rec = (PropDefHeaderRecord *)((u32 *)rec + sizepropdef(rec));
    }
}

/* An ammo box's look for a type it's changed to: the cartridge's own box or
 * round for it */
static const s16 s_boxLook[GEVR_MODAPI_AMMO_TYPES + 1] = {
    -1, PROP_WPPKMAG, PROP_SKORPIONMAG, PROP_AK47MAG, PROP_BOXCARTRIDGES, PROP_CHRGRENADE, PROP_CHRROCKET,
    PROP_CHRREMOTEMINE, PROP_CHRPROXIMITYMINE, PROP_CHRTIMEDMINE, PROP_CHRTHROWKNIFE, PROP_CHRGRENADEROUND,
    PROP_MAGNUMSHELLS, PROP_GOLDENSHELLS,
};

int gevrModPickupSpawn(PropDefHeaderRecord *rec, int index)
{
    ObjectRecord *obj = (ObjectRecord *)rec;
    GevrModPickup p;
    s32 k;

    if (!gevrModScriptRunning() || kept(index) || (obj->flags & PROPFLAG_INSIDEANOTHEROBJ)) return 0;
    memset(&p, 0, sizeof(p));
    p.index = index;
    switch (rec->type)
    {
        case PROPDEF_COLLECTABLE:
            if ((obj->flags & PROPFLAG_ASSIGNEDTOCHR) || !gevrModGameSwappable(((WeaponObjRecord *)rec)->weaponnum)) return 0;
            p.kind = GEVR_MODPICKUP_WEAPON;
            p.item = ((WeaponObjRecord *)rec)->weaponnum;
            break;
        case PROPDEF_MAGAZINE:
            p.ammo = ((AmmoCrateRecord *)rec)->ammoType;
            if (p.ammo <= AMMO_NONE || p.ammo > AMMOTYPE_GLOBAL_MAX) return 0;
            p.kind = GEVR_MODPICKUP_AMMO;
            break;
        case PROPDEF_AMMO:
            p.kind = GEVR_MODPICKUP_CRATE;
            for (k = 0; k < AMMOTYPE_GLOBAL_MAX; k++) p.crate[k + 1] = ((MultiAmmoCrateRecord *)rec)->slots[k].quantity;
            break;
        case PROPDEF_ARMOUR:
            p.kind = GEVR_MODPICKUP_ARMOUR;
            p.armour = ((BodyArmourRecord *)rec)->amount;
            break;
        default:
            return 0;
    }
    gevrModApiPickupSpawning(&p);
    if (p.none) return 1;
    switch (rec->type)
    {
        case PROPDEF_COLLECTABLE:
            if (p.item != ((WeaponObjRecord *)rec)->weaponnum)
            {
                ((WeaponObjRecord *)rec)->weaponnum = (s8)p.item;
                obj->obj = (s16)getPropForHeldItem((ITEM_IDS)p.item);
            }
            break;
        case PROPDEF_MAGAZINE:
            if (p.ammo != ((AmmoCrateRecord *)rec)->ammoType)
            {
                ((AmmoCrateRecord *)rec)->ammoType = (AMMOTYPE)p.ammo;
                obj->obj = s_boxLook[p.ammo];
            }
            break;
        case PROPDEF_AMMO:
            for (k = 0; k < AMMOTYPE_GLOBAL_MAX; k++) ((MultiAmmoCrateRecord *)rec)->slots[k].quantity = (u16)p.crate[k + 1];
            break;
        case PROPDEF_ARMOUR:
            ((BodyArmourRecord *)rec)->initialamount = p.armour;
            ((BodyArmourRecord *)rec)->amount = p.armour;
            break;
    }
    return 0;
}

typedef char pickupPoolsFit[MAX_WEAPON_SLOTS <= GEVR_MODAPI_PICKUP_SLOTS && MAX_AMMO_CRATES <= GEVR_MODAPI_PICKUP_SLOTS
                            && GEVR_MODAPI_ARMOUR_DROPS <= GEVR_MODAPI_PICKUP_SLOTS ? 1 : -1];

/* Body armour a mod drops (mod.pickups.drop): the game makes none while
 * playing, only the stage's own, so the records are the mod API's. A record
 * is free while it has no prop, as the game's weapon slots are (objFree
 * clears it when the armour's taken); none kept past a stage. */
static BodyArmourRecord s_modArmour[GEVR_MODAPI_ARMOUR_DROPS];

void gevrModGameDropsClear(void)
{
    memset(s_modArmour, 0, sizeof(s_modArmour));
}

/* a runtime pickup's record (GEVR_MODAPI_PICKUP_*), or NULL */
static ObjectRecord *slotObject(int kind, int slot)
{
    if (kind == GEVR_MODAPI_PICKUP_WEAPON && slot >= 0 && slot < MAX_WEAPON_SLOTS)
    {
        return (ObjectRecord *)&g_WeaponSlots[slot];
    }
    if (kind == GEVR_MODAPI_PICKUP_BOX && slot >= 0 && slot < MAX_AMMO_CRATES)
    {
        return (ObjectRecord *)&g_AmmoCrates[slot];
    }
    if (kind == GEVR_MODAPI_PICKUP_ARMOUR && slot >= 0 && slot < GEVR_MODAPI_ARMOUR_DROPS)
    {
        return (ObjectRecord *)&s_modArmour[slot];
    }
    return NULL;
}

/* A shared pickup another player won, or gone (gevr_modapi.c): this
 * headset's copy freed as the game frees a recycled one, so objFree tells
 * pickup_removed */
int gevrModGamePickupRemove(int kind, int slot)
{
    ObjectRecord *obj = slotObject(kind, slot);

    if (obj == NULL || obj->prop == NULL)
    {
        return 0;
    }
    objFreePermanently(obj, TRUE);
    return 1;
}

/* A shared pickup the host awarded this headset's player: the game's own
 * pickup of it, as the local player, then its tick operation (as
 * AI_BondCollectObject takes one), so item_pickup comes from the game */
int gevrModGamePickupGrant(int kind, int slot)
{
    ObjectRecord *obj = slotObject(kind, slot);
    PropRecord *prop;
    s32 was;
    TICKOP op;

    if (obj == NULL || (prop = obj->prop) == NULL || !gevrModGamePlayerReady())
    {
        return 0;
    }
    was = get_cur_playernum();
    set_cur_player(localSlot());
    op = propPickupByPlayer(prop, TRUE);
    propExecuteTickOperation(prop, op);
    set_cur_player(was);
    return op != TICKOP_NONE;
}

/* propobj.c propPickupByPlayer, before anything changes: a shared pickup in
 * a network game goes through the host (gevr_modapi.c gevrModApiPickupTry);
 * 1 holds this one back */
s32 gevrModPickupHold(PropRecord *prop)
{
    s32 kind, slot;

    if (prop == NULL || prop->obj == NULL || !runtimeSlot(prop->obj, &kind, &slot))
    {
        return FALSE;
    }
    return gevrModApiPickupTry(kind, slot, gevrModGameLocal());
}

/* A pickup the game made while playing: its pool (GEVR_MODAPI_PICKUP_*) and
 * slot, or FALSE for the stage's own */
static s32 runtimeSlot(ObjectRecord *obj, s32 *kind, s32 *slot)
{
    const uintptr_t at = (uintptr_t)obj;
    const uintptr_t weapons = (uintptr_t)&g_WeaponSlots[0], crates = (uintptr_t)&g_AmmoCrates[0];
    const uintptr_t vests = (uintptr_t)&s_modArmour[0];

    (void)sizeof(pickupPoolsFit);
    if (at >= weapons && at < weapons + sizeof(WeaponObjRecord) * MAX_WEAPON_SLOTS
        && (at - weapons) % sizeof(WeaponObjRecord) == 0)
    {
        *kind = GEVR_MODAPI_PICKUP_WEAPON;
        *slot = (s32)((at - weapons) / sizeof(WeaponObjRecord));
        return TRUE;
    }
    if (at >= crates && at < crates + sizeof(AmmoCrateRecord) * MAX_AMMO_CRATES
        && (at - crates) % sizeof(AmmoCrateRecord) == 0)
    {
        *kind = GEVR_MODAPI_PICKUP_BOX;
        *slot = (s32)((at - crates) / sizeof(AmmoCrateRecord));
        return TRUE;
    }
    if (at >= vests && at < vests + sizeof(s_modArmour) && (at - vests) % sizeof(BodyArmourRecord) == 0)
    {
        *kind = GEVR_MODAPI_PICKUP_ARMOUR;
        *slot = (s32)((at - vests) / sizeof(BodyArmourRecord));
        return TRUE;
    }
    return FALSE;
}

/* A pickup's id (gevr_modapi.h): the setup record's index for the stage's
 * own; a runtime one's from the mod API's table, never reused in a mission
 * (the game's slots are) */
static int pickupId(ObjectRecord *target)
{
    PropDefHeaderRecord *def = g_CurrentSetup.propDefs;
    s32 kind, slot, index;

    if (target == NULL) return -1;
    if (runtimeSlot(target, &kind, &slot)) return gevrModApiPickupSlotId(kind, slot);
    for (index = 0; def != NULL && def->type != PROPDEF_END && index < GEVR_MODAPI_PICKUP_RUNTIME; index++)
    {
        if ((ObjectRecord *)def == target) return index;
        def += sizepropdef(def);
    }
    return -1;
}

/* propobj.c objFree: an object freed; a runtime pickup's id goes with it,
 * and pickup_removed is told unless this headset's player took it */
void gevrModPickupFreed(ObjectRecord *obj)
{
    s32 kind, slot;

    if (obj != NULL && runtimeSlot(obj, &kind, &slot)) gevrModApiPickupSlotFreed(kind, slot);
}

void gevrModPickupTaken(PropDefHeaderRecord *rec)
{
    GevrModPickup got;
    s32 k, pool, slot;

    if (!gevrModScriptRunning() || !gevrModGameLocal()) return;
    memset(&got, 0, sizeof(got));
    got.pickup = pickupId((ObjectRecord *)rec);
    if (runtimeSlot((ObjectRecord *)rec, &pool, &slot)) gevrModApiPickupSlotTaken(pool, slot);   /* freed next: not removed */
    switch (rec->type)
    {
        case PROPDEF_COLLECTABLE:
            got.kind = GEVR_MODPICKUP_WEAPON;
            got.item = ((WeaponObjRecord *)rec)->weaponnum;
            break;
        case PROPDEF_MAGAZINE:   /* what propPickupByPlayer gives */
            got.ammo = ((AmmoCrateRecord *)rec)->ammoType;
            if (got.ammo <= AMMO_NONE || got.ammo > AMMOTYPE_GLOBAL_MAX) return;
            got.kind = GEVR_MODPICKUP_AMMO;
            got.rounds = get_ammo_in_magazine((AmmoCrateRecord *)rec);
            break;
        case PROPDEF_AMMO:
            got.kind = GEVR_MODPICKUP_CRATE;
            for (k = 0; k < AMMOTYPE_GLOBAL_MAX; k++)
            {
                s32 type = k + 1 == AMMO_9MM_2 ? AMMO_9MM : k + 1;   /* as the crate gives them */
                s32 rounds = ((MultiAmmoCrateRecord *)rec)->slots[k].quantity;

                if (gevrSoloRules()) rounds *= g_SoloAmmoMultiplier;
                got.crate[type] += rounds;
            }
            break;
        case PROPDEF_ARMOUR:
            got.kind = GEVR_MODPICKUP_ARMOUR;
            got.armour = ((BodyArmourRecord *)rec)->amount;
            break;
        case PROPDEF_KEY:
            got.kind = GEVR_MODPICKUP_KEY;
            break;
        default:
            got.kind = GEVR_MODPICKUP_OBJECT;
            break;
    }
    gevrModApiItemPickup(&got);
}

/* ---- the hands area ---- */

/* hand 0 right (the gun hand), 1 left: the body slots' ctrl 1 and 0; the
 * local player's view, whoever the game is ticking (a mod's tick runs
 * after every player's) */
int gevrModGameHandAt(int hand, float out[3])
{
    s32 was;
    int ok;

    if (!gevrModGamePlayerReady()) return 0;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    ok = gevrBodyLocalHand(hand ? 0 : 1, out);
    set_cur_player(was);
    return ok;
}

int gevrModGameAnchorAt(int anchor, float out[3], float *radius)
{
    return gevrBodyLocalSlot(anchor, out, radius);
}

/* As the body slots' own buzz (gevr_bodyslots.c gevrBodyBuzz): the logical
 * controller, the left-handed swap done in trigger_haptic_vibration. The
 * player's Mods row scales the strength and caps the length. */
int gevrModGameHaptic(int hand, float amplitude, float seconds)
{
    float scale, most, freq;

    if (!vr_haptics_ready()) return 0;
    vrHapticsGetRumble(GEVR_ACTION_MOD, &scale, &most, &freq);
    if (scale <= 0.0f || most <= 0.0f || amplitude <= 0.0f || seconds <= 0.0f) return 1;   /* off: nothing to feel */
    trigger_haptic_vibration_freq_c(hand ? 0 : 1, amplitude * scale, seconds < most ? seconds : most, freq);
    return 1;
}

/* ---- the guards area ---- */

/* Guard events come from where the guards run: solo, or the co-op host
 * (a client's guards are the host's copies, net_coop.c) */
static int runsGuards(void)
{
    return !netIsActive() || netIsHost();
}

/* The body table's rows (the cartridge's, ended by a row with no header) */
static int bodyRows(void)
{
    int n = 0;

    while (c_item_entries != NULL && c_item_entries[n].header != NULL && n < 0x7FFF) n++;
    return n;
}

int gevrModGameBodyOk(int body)
{
    if (body < 0 || body >= bodyRows() || body == BODY_Left_Suit_Hand_Floating_Arm) return 0;
    if (body >= HEAD_START && body != BODY_Natalya_Jungle_Fatigues) return 0;   /* the heads' rows */
    return c_item_entries[body].hasHead ? 2 : 1;
}

int gevrModGameHeadOk(int head)
{
    return head >= HEAD_START && head < BODY_Natalya_Jungle_Fatigues && head < bodyRows();
}

int gevrModGameGuardGun(int item)
{
    s32 as;

    if (item <= ITEM_FIST || item >= ITEM_IDS_MAX) return 0;
    as = GEVR_ITEM_AS(item);
    return as >= ITEM_WPPK && as <= ITEM_ROCKETLAUNCH && as != ITEM_WATCHLASER && getPropForHeldItem((ITEM_IDS)item) >= 0;
}

/* guard_spawning's guns, by chrnum, for this stage's weaponAssignToHome */
#define GUARD_WEAPONS_MAX 512
static struct { s16 id; s8 item; } s_guardWeapons[GUARD_WEAPONS_MAX];
static int s_guardWeaponCount;

static void guardWeaponsReset(void)
{
    s_guardWeaponCount = 0;
}

/* The guard's setup gun: its first PROPFLAG_ASSIGNEDTOCHR record (the pad
 * holds the chrnum), -1 none */
static int setupGun(s32 chrnum)
{
    PropDefHeaderRecord *rec = g_CurrentSetup.propDefs;

    for (; rec && rec->type != PROPDEF_END; rec = (PropDefHeaderRecord *)((u32 *)rec + sizepropdef(rec)))
    {
        WeaponObjRecord *w = (WeaponObjRecord *)rec;

        if (rec->type == PROPDEF_COLLECTABLE && (w->flags & PROPFLAG_ASSIGNEDTOCHR) && w->pad == chrnum)
        {
            return w->weaponnum;
        }
    }
    return -1;
}

void gevrModGuardSpawning(GuardRecord *rec, int index, int *body, int *head)
{
    GevrModGuardSpawn g;

    if (!gevrModScriptRunning()) return;
    g.id = rec->chrnum;
    g.index = index;
    g.body = *body;
    g.head = *head;
    g.weapon = setupGun(rec->chrnum);
    gevrModApiGuardSpawning(&g);
    if (g.body != *body || g.head != *head)
    {
        *body = g.body;
        *head = g.head;
        if (c_item_entries[*body].hasHead) *head = -1;
        else if (*head < 0) *head = gevrBodyChooseHead(*body, rec->HeadID);   /* as the game picks one */
    }
    if (g.weapon >= 0 && g.weapon != setupGun(rec->chrnum) && s_guardWeaponCount < GUARD_WEAPONS_MAX)
    {
        s_guardWeapons[s_guardWeaponCount].id = (s16)rec->chrnum;
        s_guardWeapons[s_guardWeaponCount].item = (s8)g.weapon;
        s_guardWeaponCount++;
    }
}

void gevrModGuardWeapon(WeaponObjRecord *weapon)
{
    int i;

    for (i = s_guardWeaponCount - 1; i >= 0; i--)
    {
        if (s_guardWeapons[i].id == weapon->pad)
        {
            weapon->weaponnum = s_guardWeapons[i].item;
            weapon->obj = (s16)getPropForHeldItem((ITEM_IDS)weapon->weaponnum);
            return;
        }
    }
}

/* What each slot's guard has been told: alerted, killed, retiring */
#define GUARD_SLOTS_MAX 1024
enum { TOLD_ALERT = 1, TOLD_KILLED = 2, TOLD_RETIRING = 4 };
static u8 s_told[GUARD_SLOTS_MAX];
static s8 s_shooter[GUARD_SLOTS_MAX];   /* the player whose shot hit it last, -1 none */

static u8 *toldOf(ChrRecord *chr)
{
    const uintptr_t at = (uintptr_t)chr, first = (uintptr_t)g_ChrSlots;
    size_t i;

    if (g_ChrSlots == NULL || at < first || at >= first + (size_t)g_NumChrSlots * sizeof(ChrRecord)) return NULL;
    i = (at - first) / sizeof(ChrRecord);
    return i < GUARD_SLOTS_MAX ? &s_told[i] : NULL;
}

void gevrModGuardFresh(ChrRecord *chr)
{
    u8 *told = toldOf(chr);

    if (told)
    {
        *told = 0;
        s_shooter[told - s_told] = -1;
    }
}

void gevrModGuardShotBy(ChrRecord *chr, int player)
{
    u8 *told = toldOf(chr);

    if (told) s_shooter[told - s_told] = (s8)(player >= 0 && player < 4 ? player : -1);
}

static int guardAlerted(ChrRecord *chr)
{
    const u8 *told = toldOf(chr);

    /* the game's own marks too: a guard that saw or heard a player before a
     * co-op host change, or before the mod started */
    return (told && (*told & TOLD_ALERT)) || chr->lastseetarget60 != 0 || chr->lastheartarget60 != 0;
}

static int guardDying(const ChrRecord *chr)
{
    return chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD;
}

void gevrModGuardAlert(ChrRecord *chr, int how)
{
    u8 *told;

    if (!gevrModScriptRunning() || !runsGuards() || chr == NULL || chr->prop == NULL || chr->prop->type != PROP_TYPE_CHR
        || guardDying(chr) || chr->chrnum < 0 || guardAlerted(chr))
    {
        return;
    }
    told = toldOf(chr);
    if (told == NULL) return;   /* past the slots tracked: never told twice */
    *told |= TOLD_ALERT;
    /* the guard ticks with its target player current (net_coop.c gevrCoopGuardTarget) */
    gevrModApiGuardAlerted(chr->chrnum, how, get_cur_playernum());
}

void gevrModGuardKilled(ChrRecord *chr, int how, int item)
{
    u8 *told;

    if (!gevrModScriptRunning() || !runsGuards() || chr == NULL || chr->chrnum < 0) return;
    told = toldOf(chr);
    if (told == NULL || (*told & TOLD_KILLED)) return;
    *told |= TOLD_KILLED;
    /* a shot's death: the player whose shot hit it last (an explosion's owner isn't known here) */
    gevrModApiGuardKilled(chr->chrnum, how, item, how == GEVR_MODKILL_SHOT ? s_shooter[told - s_told] : -1);
}

void gevrModGuardRetiring(ChrRecord *chr)
{
    u8 *told;
    float pos[3];

    if (!gevrModScriptRunning() || !runsGuards() || chr == NULL || chr->prop == NULL || chr->chrnum < 0) return;
    told = toldOf(chr);
    if (told == NULL || (*told & TOLD_RETIRING)) return;
    *told |= TOLD_RETIRING;
    pos[0] = chr->prop->pos.x;
    pos[1] = chr->prop->pos.y;
    pos[2] = chr->prop->pos.z;
    gevrModApiCorpseRetiring(chr->chrnum, pos);
}

/* A stage's guard: a character with a model and an id, not a retired body */
static int isGuard(const ChrRecord *chr)
{
    return chr->model != NULL && chr->prop != NULL && chr->prop->type == PROP_TYPE_CHR && chr->chrnum >= 0;
}

int gevrModGameGuards(int *ids, int max)
{
    s32 i;
    int n = 0;

    for (i = 0; g_ChrSlots != NULL && i < g_NumChrSlots && n < max; i++)
    {
        if (isGuard(&g_ChrSlots[i])) ids[n++] = g_ChrSlots[i].chrnum;
    }
    return n;
}

static int heldItem(const ChrRecord *chr, s32 hand)
{
    const PropRecord *prop = chr->weapons_held[hand];

    return prop != NULL && prop->weapon != NULL ? prop->weapon->weaponnum : -1;
}

int gevrModGameGuard(int id, GevrModGuardState *out)
{
    s32 i;

    for (i = 0; g_ChrSlots != NULL && i < g_NumChrSlots; i++)
    {
        ChrRecord *chr = &g_ChrSlots[i];

        if (!isGuard(chr) || chr->chrnum != id) continue;
        out->id = chr->chrnum;
        out->body = chr->bodynum;
        out->head = chr->bodynum >= 0 && c_item_entries[chr->bodynum].hasHead ? -1 : chr->headnum;
        out->alive = !guardDying(chr);
        out->alerted = guardAlerted(chr);
        out->weapon[0] = heldItem(chr, GUNRIGHT);
        out->weapon[1] = heldItem(chr, GUNLEFT);
        out->maxHealth = chr->maxdamage;
        out->health = chr->maxdamage - chr->damage > 0.0f ? chr->maxdamage - chr->damage : 0.0f;
        out->pos[0] = chr->prop->pos.x;
        out->pos[1] = chr->prop->pos.y;
        out->pos[2] = chr->prop->pos.z;
        return 1;
    }
    return 0;
}

/* ---- guards, part 2: what a mod may change, where the guards run ---- */

void gevrModGuardSpawned(ChrRecord *chr)
{
    if (!gevrModScriptRunning() || !runsGuards() || chr == NULL || chr->chrnum < 0) return;
    gevrModApiGuardSpawned(chr->chrnum);
}

int gevrModGameRunsGuards(void)
{
    return runsGuards() && gevrModGamePlayerReady();
}

int gevrModGameAiListOk(int id)
{
    if (id < 0 || isBGAIListID(id)) return 0;   /* a level script's, not a guard's */
    if (isGlobalAIListID(id))
    {
        /* not the subroutines (they return to a caller), the alarm raiser (it runs
         * to a preset pad a spawned guard has none of), nor ending the level */
        return id == GEVR_MODAI_STANDARD || id == GEVR_MODAI_SIMPLE || id == GEVR_MODAI_DEAF || id == GEVR_MODAI_CHASE;
    }
    return ailistFindById(id) != NULL;
}

/* A pad the setup has: its pads, or 10000 and up its bound pads (both end
 * with no link, as expand_09_characters counts them) */
static int padOk(int pad)
{
    s32 n = 0;

    if (pad >= 10000)
    {
        while (g_CurrentSetup.boundpads != NULL && g_CurrentSetup.boundpads[n].plink != NULL) n++;
        return pad - 10000 < n;
    }
    while (g_CurrentSetup.pads != NULL && g_CurrentSetup.pads[n].plink != NULL) n++;
    return pad >= 0 && pad < n;
}

static ChrRecord *guardById(int id)
{
    s32 i;

    for (i = 0; g_ChrSlots != NULL && i < g_NumChrSlots; i++)
    {
        if (isGuard(&g_ChrSlots[i]) && g_ChrSlots[i].chrnum == id) return &g_ChrSlots[i];
    }
    return NULL;
}

/* gevr_modattach.c: a guard's record by id (alive or dead), for mod.attach */
ChrRecord *gevrModGameGuardChr(int id)
{
    return guardById(id);
}

/* ---- the pickups a mod drops, and the weapons the game drops ---- */

extern AmmoCrateRecord *ammocrateAllocate(void);   /* propobj.c: a runtime ammo box's slot */
extern ObjectRecord blank_07_object;               /* propobj.c: a runtime box's blank record */

static s32 s_modDropping;   /* a mod's own drop: its call has the id, so no weapon_dropped */

/* who a chr is, for weapon_dropped and a drop's from */
static int fromChr(ChrRecord *chr)
{
    s32 i;

    if (chr == NULL) return GEVR_MODAPI_FROM_NONE;
    for (i = 0; i < getPlayerCount(); i++)
    {
        if (g_playerPointers[i] != NULL && g_playerPointers[i]->prop != NULL && g_playerPointers[i]->prop->chr == chr)
        {
            return GEVR_MODAPI_FROM_PLAYER;
        }
    }
    return isGuard(chr) ? chr->chrnum : GEVR_MODAPI_FROM_NONE;
}

/* propobj.c propobjSetDropped: a weapon a chr held, dropped */
void gevrModWeaponDropped(PropRecord *prop)
{
    ObjectRecord *obj;

    if (s_modDropping || !gevrModScriptRunning() || prop == NULL || prop->parent == NULL
        || prop->parent->type != PROP_TYPE_CHR || (obj = prop->obj) == NULL || obj->type != PROPDEF_COLLECTABLE)
    {
        return;
    }
    gevrModApiWeaponDropped(pickupId(obj), ((WeaponObjRecord *)obj)->weaponnum, fromChr(prop->parent->chr));
}

int gevrModGameDrop(int item, int ammo, float armour, int from)
{
    ChrRecord *chr;
    PropRecord *prop = NULL;
    s32 id = -1;

    if (!gevrModGamePlayerReady()) return -1;
    if (from == GEVR_MODAPI_FROM_PLAYER || from <= GEVR_MODAPI_FROM_SLOT0)
    {
        /* this headset's player, or a player's slot (the host's, for its shared drop) */
        const s32 slot = from == GEVR_MODAPI_FROM_PLAYER ? localSlot() : GEVR_MODAPI_FROM_SLOT0 - from;
        struct player *who = slot >= 0 && slot < getPlayerCount() ? g_playerPointers[slot] : NULL;

        chr = who != NULL && who->prop != NULL ? who->prop->chr : NULL;
    }
    else
    {
        chr = guardById(from);
    }
    if (chr == NULL || chr->prop == NULL) return -1;
    s_modDropping = TRUE;
    if (item > 0)
    {
        /* as drop_inventory drops a player's guns: made in the chr's hand, then let fall */
        const s32 propid = getPropForHeldItem((ITEM_IDS)item);

        if (propid >= 0)
        {
            prop = something_with_generating_object(chr, propid, (ITEM_IDS)item, 0x20000000, NULL, NULL);
        }
    }
    else if (ammo >= 1 && ammo <= GEVR_MODAPI_AMMO_TYPES && s_boxLook[ammo] >= 0)
    {
        /* as a shot crate drops a box of one type (objDamage): a runtime box on the chr, then let fall */
        AmmoCrateRecord *crate = ammocrateAllocate();

        if (crate != NULL)
        {
            *(ObjectRecord *)crate = blank_07_object;
            crate->obj = s_boxLook[ammo];
            crate->ammoType = ammo;
            modelLoad(crate->obj);   /* a box the stage never placed: its file, as a hat's is loaded (hatCreateForChr) */
            if (objInitWithModelDef((ObjectRecord *)crate, PitemZ_entries[crate->obj].header) != NULL)
            {
                modelSetScale(crate->model, crate->model->scale);
                chrpropReparent(crate->prop, chr->prop);
                prop = crate->prop;
            }
        }
    }
    else if (armour > 0.0f && armour <= 1.0f)
    {
        /* as the stage's own armour (PROPDEF_ARMOUR, the body armour model), in a
         * record of the mod API's pool, then let fall as the box is: taken
         * by the game's own armour pickup, worn down by shots as the stage's */
        BodyArmourRecord *vest = NULL;
        s32 i;

        for (i = 0; i < GEVR_MODAPI_ARMOUR_DROPS && vest == NULL; i++)
        {
            if (s_modArmour[i].prop == NULL) vest = &s_modArmour[i];
        }
        if (vest != NULL)
        {
            *(ObjectRecord *)vest = blank_07_object;
            vest->type = PROPDEF_ARMOUR;
            vest->obj = PROP_BODYARMOUR;
            vest->initialamount = armour;
            vest->amount = armour;
            modelLoad(PROP_BODYARMOUR);
            if (objInitWithModelDef((ObjectRecord *)vest, PitemZ_entries[PROP_BODYARMOUR].header) != NULL)
            {
                modelSetScale(vest->model, vest->model->scale);
                chrpropReparent(vest->prop, chr->prop);
                prop = vest->prop;
            }
            else
            {
                vest->prop = NULL;   /* free again */
            }
        }
    }
    if (prop != NULL && prop->obj != NULL)
    {
        id = pickupId(prop->obj);
        propobjSetDropped(prop, DROPTYPE_DEFAULT);
        objDrop(prop);
    }
    s_modDropping = FALSE;
    return id;
}

/* As the game's own scripts give a spawned guard its guns (AI_TRYGiveMeItem) */
static int armGuard(ChrRecord *chr, int item, int hand)
{
    const s32 g = hand ? GUNLEFT : GUNRIGHT;

    return chrGiveWeapon(chr, getPropForHeldItem((ITEM_IDS)item), (ITEM_IDS)item, hand ? 0x10000000 : 0) != NULL
        && chr->weapons_held[g] != NULL;
}

/* AI_TRYSpawningChrAtPad's spawn (chrSpawnAtPad, which tells co-op's others
 * as it does for the host's scripts), then the guns. Not in the player's
 * view: the game refuses a spawn there, as its own scripts usually ask. */
int gevrModGameSpawnGuard(const GevrModGuardOrder *o)
{
    PropRecord *prop;
    AIRecord *ai;
    s32 h;

    if (!gevrModGameRunsGuards() || !padOk(o->pad) || !gevrModGameBodyOk(o->body) || !gevrModGameAiListOk(o->ai)) return -1;
    if (o->head >= 0 && (gevrModGameBodyOk(o->body) == 2 || !gevrModGameHeadOk(o->head))) return -1;
    ai = ailistFindById(o->ai);
    prop = ai != NULL ? chrSpawnAtPad(NULL, o->body, o->head, o->pad, ai, 0) : NULL;
    if (prop == NULL || prop->chr == NULL) return -1;
    for (h = 0; h < 2; h++)
    {
        if (o->weapon[h] >= 0 && gevrModGameGuardGun(o->weapon[h])) armGuard(prop->chr, o->weapon[h], h);
    }
    return prop->chr->chrnum;
}

int gevrModGameGiveGuardWeapon(int id, int item, int hand)
{
    ChrRecord *chr = guardById(id);
    const s32 g = hand ? GUNLEFT : GUNRIGHT;

    if (!gevrModGameRunsGuards() || chr == NULL || guardDying(chr) || !gevrModGameGuardGun(item)) return 0;
    if (chr->weapons_held[g] != NULL)
    {
        /* the gun it held goes, as co-op's copies lose theirs (net_coop.c
         * coopSyncHand); a dual pair's other gun no longer points at it */
        WeaponObjRecord *old = chr->weapons_held[g]->weapon;

        if (old != NULL && old->dualweapon != NULL)
        {
            old->dualweapon->dualweapon = NULL;
            old->dualweapon = NULL;
        }
        if (chr->weapons_held[g]->obj != NULL) objFreePermanently(chr->weapons_held[g]->obj, TRUE);
        if (chr->weapons_held[g] != NULL) return 0;   /* still held: leave it */
    }
    return armGuard(chr, item, hand);
}

int gevrModGamePatrol(int id, int path)
{
    ChrRecord *chr = guardById(id);
    PathRecord *found;

    if (!gevrModGameRunsGuards() || chr == NULL || guardDying(chr) || g_CurrentSetup.patrolpaths == NULL) return 0;
    found = pathFindById(path);
    return found != NULL && if_actor_able_set_on_path(chr, found);
}

int gevrModGamePaths(int *ids, int max)
{
    s32 i;
    int n = 0;

    for (i = 0; g_CurrentSetup.patrolpaths != NULL && g_CurrentSetup.patrolpaths[i].waypoints != NULL && n < max; i++)
    {
        ids[n++] = g_CurrentSetup.patrolpaths[i].ID;
    }
    return n;
}

/* ---- the stage's pads, and guards sent to them ---- */

extern bool chrGoToPad(ChrRecord *self, s32 padid, SPEED speed);                                 /* chraction.c */
extern bool check_set_actor_standing_still(ChrRecord *self, s32 faceentitytype, s32 faceentityid);

int gevrModGamePadCount(int *bound)
{
    s32 n = 0, b = 0;

    while (g_CurrentSetup.pads != NULL && g_CurrentSetup.pads[n].plink != NULL) n++;
    while (g_CurrentSetup.boundpads != NULL && g_CurrentSetup.boundpads[b].plink != NULL) b++;
    if (bound != NULL) *bound = b;
    return n;
}

int gevrModGamePad(int pad, GevrModPad *out)
{
    PadRecord *p;

    if (!padOk(pad)) return 0;
    /* a bound pad begins as a pad does (chrGoToPad reads it so) */
    p = pad >= 10000 ? (PadRecord *)&g_CurrentSetup.boundpads[pad - 10000] : &g_CurrentSetup.pads[pad];
    out->pos[0] = p->pos.x;
    out->pos[1] = p->pos.y;
    out->pos[2] = p->pos.z;
    out->look[0] = p->look.x;
    out->look[1] = p->look.y;
    out->look[2] = p->look.z;
    out->room = p->stan != NULL ? p->stan->room : -1;
    return 1;
}

int gevrModGameGoTo(int id, int pad, int speed)
{
    ChrRecord *chr = guardById(id);

    if (!gevrModGameRunsGuards() || chr == NULL || guardDying(chr) || !padOk(pad)) return 0;
    return chrGoToPad(chr, pad, speed == 2 ? SPEED_SPRINT : speed == 1 ? SPEED_RUN : SPEED_WALK);
}

extern s32 selected_folder_num;          /* front.c: the folder the run plays from */
extern DIFFICULTY selected_difficulty;   /* front.c */

int gevrModGameDifficulty(void)
{
    return selected_difficulty >= DIFFICULTY_AGENT && selected_difficulty <= DIFFICULTY_007 ? (int)selected_difficulty : -1;
}

int gevrModGameFolder(void)
{
    return selected_folder_num >= 0 && selected_folder_num < 4 ? selected_folder_num : -1;
}

#include "gevr_modpack.h"   /* gevrModActive: the run's mod campaign */
extern void fileUnlockStageInFolderAtDifficulty(s32 foldernum, LEVEL_SOLO_SEQUENCE stage, DIFFICULTY difficulty, s32 newtime);
typedef char missionsAreTwenty[SP_LEVEL_MAX == GEVR_MODAPI_MISSIONS ? 1 : -1];

int gevrModGameComplete(int mission, int difficulty, int seconds)
{
    (void)sizeof(missionsAreTwenty);
    /* a mod campaign's save is its own file (libultra.c osEepromSetPath): the base game's is never written */
    if (gevrModActive() == NULL || gevrModGameFolder() < 0 || mission < SP_LEVEL_DAM || mission >= SP_LEVEL_MAX
        || difficulty < DIFFICULTY_AGENT || difficulty > DIFFICULTY_007)
    {
        return 0;
    }
    fileUnlockStageInFolderAtDifficulty(selected_folder_num, (LEVEL_SOLO_SEQUENCE)mission, (DIFFICULTY)difficulty,
                                        seconds > 0 ? seconds : GEVR_MODAPI_NO_TIME);
    return 1;
}

extern PropRecord *hatCreateForChr(ChrRecord *chr, s32 modelnum, u32 flags);   /* propobj.c */

/* the hats in mod.guards.giveHat's order */
typedef char hatsAreTwelve[PROP_HATPEAKED - PROP_HATFURRY + 1 == GEVR_MODAPI_HATS ? 1 : -1];

int gevrModGameGiveHat(int id, int hat)
{
    ChrRecord *chr = guardById(id);

    (void)sizeof(hatsAreTwelve);
    if (!gevrModGameRunsGuards() || chr == NULL || guardDying(chr) || hat < 0 || hat >= GEVR_MODAPI_HATS
        || chr->prop == NULL || chr->model == NULL || chr->handle_positiondata_hat != NULL)
    {
        return 0;   /* one hat a guard: the game's own never stacks them */
    }
    return hatCreateForChr(chr, PROP_HATFURRY + hat, 0) != NULL;
}

int gevrModGameFace(int id, int pad)
{
    ChrRecord *chr = guardById(id);

    if (!gevrModGameRunsGuards() || chr == NULL || guardDying(chr) || !padOk(pad)) return 0;
    return check_set_actor_standing_still(chr, TARGET_PAD, pad);
}

/* ---- the damage and player area ---- */

static int partOf(int hitpart)
{
    switch (hitpart)
    {
        case HIT_HEAD: return GEVR_MODPART_HEAD;
        case HIT_CHEST:
        case HIT_PELVIS: return GEVR_MODPART_BODY;
        case HIT_GUN: return GEVR_MODPART_GUN;
        case HIT_HAT: return GEVR_MODPART_HAT;
        case HIT_LEFT_FOOT: case HIT_LEFT_LEG: case HIT_LEFT_THIGH:
        case HIT_RIGHT_FOOT: case HIT_RIGHT_LEG: case HIT_RIGHT_THIGH:
        case HIT_LEFT_HAND: case HIT_LEFT_ARM: case HIT_LEFT_SHOULDER:
        case HIT_RIGHT_HAND: case HIT_RIGHT_ARM: case HIT_RIGHT_SHOULDER: return GEVR_MODPART_LIMB;
        default: return 0;
    }
}

static void guardDamage(GevrModDamage *d, const ChrRecord *chr, float amount, int how, int hitpart, int item, int player)
{
    memset(d, 0, sizeof(*d));
    d->target = GEVR_MODTARGET_GUARD;
    d->id = chr->chrnum;
    d->amount = amount;
    d->how = how;
    d->part = how == GEVR_MODHOW_SHOT ? partOf(hitpart) : 0;
    d->item = item >= 0 && item < ITEM_IDS_MAX ? item : -1;
    d->by = player >= 0 && player < 4 ? player : -1;
}

/* Where the guards run (solo, the co-op host): a client's guards are the host's copies */
int gevrModGuardBeforeDamage(ChrRecord *chr, float *amount, int how, int hitpart, int item, int player)
{
    GevrModDamage d;

    if (!gevrModScriptRunning() || !runsGuards() || chr == NULL || chr->chrnum < 0) return 0;
    guardDamage(&d, chr, *amount, how, hitpart, item, player);
    gevrModApiBeforeDamage(&d);
    if (d.cancelled) return 1;
    *amount = d.amount;
    return 0;
}

void gevrModGuardDamaged(ChrRecord *chr, float amount, int how, int hitpart, int item, int player)
{
    GevrModDamage d;

    if (!gevrModScriptRunning() || !runsGuards() || chr == NULL || chr->chrnum < 0) return;
    guardDamage(&d, chr, amount, how, hitpart, item, player);
    d.health = chr->maxdamage - chr->damage > 0.0f ? chr->maxdamage - chr->damage : 0.0f;
    gevrModApiDamageApplied(&d);
}

/* What hurt the player: a blast (explosion.c says so), gas (no armour), else a hit */
static int playerHow(int affectsArmour)
{
    return s_gevrExplosionDamage ? GEVR_MODHOW_EXPLOSION : !affectsArmour ? GEVR_MODHOW_GAS : GEVR_MODHOW_HIT;
}

static void playerDamage(GevrModDamage *d, float amount, int by, int affectsArmour)
{
    memset(d, 0, sizeof(*d));
    d->target = GEVR_MODTARGET_PLAYER;
    d->id = get_cur_playernum();
    d->amount = amount;
    d->how = playerHow(affectsArmour);
    d->item = -1;
    d->by = by >= 0 && by < 4 ? by : -1;
}

/* record_damage_kills runs on the hurt player's own headset; the current player is that player */
int gevrModPlayerBeforeDamage(float *amount, int by, int affectsArmour)
{
    GevrModDamage d;

    if (!gevrModScriptRunning() || g_CurrentPlayer == NULL || !gevrModGameLocal()) return 0;
    playerDamage(&d, *amount, by, affectsArmour);
    gevrModApiBeforeDamage(&d);
    if (d.cancelled) return 1;
    *amount = d.amount;
    return 0;
}

void gevrModPlayerDamaged(float amount, int by, int affectsArmour, float healthBefore, float armourBefore)
{
    GevrModDamage d;

    if (!gevrModScriptRunning() || g_CurrentPlayer == NULL || !gevrModGameLocal()) return;
    /* nothing taken (invincible, the watch, the last hit's flash): nothing to tell */
    if (g_CurrentPlayer->bondhealth == healthBefore && g_CurrentPlayer->bondarmour == armourBefore) return;
    playerDamage(&d, amount, by, affectsArmour);
    d.health = g_CurrentPlayer->bondhealth > 0.0f ? g_CurrentPlayer->bondhealth : 0.0f;
    d.armour = g_CurrentPlayer->bondarmour > 0.0f ? g_CurrentPlayer->bondarmour : 0.0f;
    gevrModApiDamageApplied(&d);
}

void gevrModPlayerDied(void)
{
    if (gevrModScriptRunning() && g_CurrentPlayer != NULL && gevrModGameLocal()) gevrModApiPlayerDied(get_cur_playernum());
}

int gevrModGamePlayerHealth(float *health, float *armour)
{
    s32 was;

    if (!gevrModGamePlayerReady()) return 0;
    was = get_cur_playernum();
    set_cur_player(localSlot());
    *health = g_CurrentPlayer->bondhealth > 0.0f ? g_CurrentPlayer->bondhealth : 0.0f;
    *armour = g_CurrentPlayer->bondarmour > 0.0f ? g_CurrentPlayer->bondarmour : 0.0f;
    set_cur_player(was);
    return 1;
}

/* the local player, alive and up: what to set, else NULL (the caller restores was) */
static struct player *livingLocal(s32 *was)
{
    *was = get_cur_playernum();
    set_cur_player(localSlot());
    if (g_CurrentPlayer->bonddead || gevrCoopLocalDowned()) return NULL;
    return g_CurrentPlayer;
}

int gevrModGameSetPlayerHealth(float health)
{
    struct player *pl;
    s32 was;

    if (!gevrModGamePlayerReady() || !(health > 0.0f && health <= 1.0f)) return 0;
    pl = livingLocal(&was);
    if (pl) pl->bondhealth = health;
    set_cur_player(was);
    return pl != NULL;
}

int gevrModGameSetPlayerArmour(float armour)
{
    struct player *pl;
    s32 was;

    if (!gevrModGamePlayerReady() || !(armour >= 0.0f && armour <= 1.0f)) return 0;
    pl = livingLocal(&was);
    if (pl) pl->bondarmour = armour;
    set_cur_player(was);
    return pl != NULL;
}

/* ---- the assets area, part 1: sound effects and smoke ---- */

int gevrModGameSoundCount(void)
{
    const ALBank *bank = g_musicSfxBufferPtr;

    return bank != NULL && bank->instCount > 0 && bank->instArray[0] != NULL ? bank->instArray[0]->soundCount : 0;
}

/* sndPlaySfx, as the game plays one; placed, as chrobjSndCreatePostEvent
 * places it (its distance volume, scaled; the port's spatial place), heard
 * by this headset's player */
int gevrModGamePlaySound(int id, float volume, const float *pos)
{
    ALSoundState *state;

    if (!gevrModGamePlayerReady() || id < 1 || id > gevrModGameSoundCount()) return 0;
    state = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, (s16)id, NULL);
    if (state == NULL) return 0;
    if (pos != NULL)
    {
        coord3d at;
        s32 was = get_cur_playernum();

        at.x = pos[0];
        at.y = pos[1];
        at.z = pos[2];
        set_cur_player(localSlot());
        sndCreatePostEvent(state, 8, (s32)(sub_GAME_7F053894(&at, GEVR_MODAPI_SOUND_NEAR, GEVR_MODAPI_SOUND_FAR)
                                           * (volume >= 0.0f ? volume : 1.0f)));
        gevrSndSpatialPlace(state, &at);
        set_cur_player(was);
    }
    else if (volume >= 0.0f)
    {
        sndCreatePostEvent(state, 8, (s32)(0x7FFF * volume));
    }
    return 1;
}

int gevrModGameSmoke(int type, int at, int which)
{
    PropRecord *prop = NULL;
    StandTile *stan = NULL;
    coord3d pos;
    u8 rooms[2];
    u8 *roomList = rooms;

    if (!gevrModGamePlayerReady() || type < 1 || type > GEVR_MODAPI_SMOKE_TYPES) return 0;
    if (at == GEVR_MODAT_PLAYER)
    {
        prop = g_playerPointers[localSlot()]->prop;
    }
    else if (at == GEVR_MODAT_GUARD)
    {
        ChrRecord *chr = guardById(which);

        prop = chr != NULL ? chr->prop : NULL;
        if (prop == NULL) return 0;
    }
    else if (at == GEVR_MODAT_PAD && padOk(which))
    {
        const PadRecord *pad = which >= 10000 ? (const PadRecord *)&g_CurrentSetup.boundpads[which - 10000]
                                              : &g_CurrentSetup.pads[which];

        pos = pad->pos;
        stan = pad->stan;
        rooms[0] = stan != NULL ? stan->room : 0xFF;
        rooms[1] = 0xFF;
    }
    else
    {
        return 0;
    }
    if (prop != NULL)
    {
        pos = prop->pos;
        stan = prop->stan;
        roomList = prop->rooms;
    }
    if (stan == NULL) return 0;
    explosionCreateSmoke(&pos, stan, (s16)type, roomList, 0);   /* dropped when its 20 smokes are all in use */
    return 1;
}
