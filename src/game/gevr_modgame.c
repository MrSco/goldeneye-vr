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

void gevrModGameSetLoaded(int hand, int rounds)
{
    s32 was;

    if (!gevrModGamePlayerReady()) return;
    localHand(hand, &was)->weapon_ammo_in_magazine = rounds;
    set_cur_player(was);
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
    return 1;
}

void gevrModGameSetStats(int item, const GevrModWeaponStats *in)
{
    WeaponStats *st;

    if (item < 0 || item >= ITEM_IDS_MAX) return;
    st = get_ptr_item_statistics((ITEM_IDS)item);
    if (!st || st == &default_weaponstats) return;
    st->MagSize = (s16)in->magazine;
    if (in->fireRate >= 0) st->AutomaticFiringRate = (u8)in->fireRate;
    st->DestructionAmount = in->damage;
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

void gevrModPickupScan(void)
{
    PropDefHeaderRecord *rec = g_CurrentSetup.propDefs;
    s32 i;

    memset(s_kept, 0, sizeof(s_kept));
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

void gevrModPickupTaken(PropDefHeaderRecord *rec)
{
    GevrModPickup got;
    s32 k;

    if (!gevrModScriptRunning() || !gevrModGameLocal()) return;
    memset(&got, 0, sizeof(got));
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
