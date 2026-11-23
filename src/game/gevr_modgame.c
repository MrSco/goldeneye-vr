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
