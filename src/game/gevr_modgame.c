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
#include "gun.h"
#include "gevr_modapi.h"

extern bool netIsActive(void);       /* net_core.c */
extern int netGetLocalSlot(void);
extern WeaponStats default_weaponstats;   /* gunWeaponStats.inc.c: every item without a model shares it */

/* mod.items names every item from ITEM_UNARMED */
typedef char itemNamesForEveryItem[GEVR_MODAPI_ITEMS == ITEM_IDS_MAX ? 1 : -1];

static s32 localSlot(void)
{
    return netIsActive() ? netGetLocalSlot() : 0;
}

int gevrModGameLocal(void)
{
    (void)sizeof(itemNamesForEveryItem);
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
