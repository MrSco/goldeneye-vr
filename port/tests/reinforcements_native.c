/* Production code runs against a small world with real character/AI types. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bondtypes.h"
#include "bondconstants.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/player.h"
#include "aicommands2.h"

#define SLOTS 64
static ChrRecord slots[SLOTS];
static PropRecord props[SLOTS];
static Model models[SLOTS];
ChrRecord *g_ChrSlots = slots, *g_ActiveChrs;
s32 g_NumChrSlots = SLOTS, g_ActiveChrsCount, g_ClockTimer;
struct player *g_CurrentPlayer;
int VrBodiesStay, VrFastReinforcements;
static bool network;
static s32 players = 1, free_slots = 64, next_slot = 1;

bool netIsActive(void) { return network; }
s32 getPlayerCount(void) { return players; }
s32 chrGetNumFree(void) { return free_slots; }
AIRecord *ailistFindById(s32 id) { (void)id; return NULL; }
PropRecord *chrGetEquippedWeaponProp(ChrRecord *chr, GUNHAND hand) { return NULL; }
PropRecord *chrGiveWeapon(ChrRecord *chr, s32 model, ITEM_IDS item, s32 flags) { return NULL; }
void propweaponSetDual(WeaponObjRecord *left, WeaponObjRecord *right) {}
PropRecord *hatCreateForChr(ChrRecord *chr, s32 model, u32 flags) { return NULL; }
static s32 chraiGoToLabel(void *list, s32 offset, u8 label) { return 1000 + label; }

static ChrRecord *make_chr(s32 slot, s32 id)
{
    ChrRecord *chr = &slots[slot];
    memset(chr, 0, sizeof(*chr));
    chr->chrnum = id;
    chr->model = &models[slot];
    chr->prop = &props[slot];
    props[slot].chr = chr;
    chr->fadealpha = 255;
    return chr;
}

PropRecord *chrSpawnAtChr(ChrRecord *self, s32 body, s32 head, s32 id, AIRecord *ai, s32 flags)
{
    if (free_slots < 3 || next_slot == SLOTS) return NULL;
    --free_slots;
    ChrRecord *chr = make_chr(next_slot, 5000 + next_slot);
    ++next_slot;
    return chr->prop;
}

/* INSERT_BODIES */
/* INSERT_LOOKUPS */

static s32 run_ai(ChrRecord *ChrEntityp, void *AiListp)
{
    s32 Offset = 0;
    switch (*(u8 *)AiListp) {
        /* INSERT_AI_EXISTENCE */
        /* INSERT_AI_CLONE */
    }
    return Offset;
}

static bool gone(ChrRecord *parent, u8 id)
{
    AiIFChrDoesNotExistRecord ai = { .cmd = AI_IFChrDoesNotExist, .CHR_NUM = id, .GOTOLABEL = 7 };
    return run_ai(parent, &ai) == 1007;
}

static ChrRecord *spawn(ChrRecord *parent)
{
    AiTRYCloningChrRecord ai = { .cmd = AI_TRYCloningChr, .CHR_NUM = (u8)CHR_SELF,
                               .AI_LIST_ID = 0, .GOTOLABEL = 7 };
    assert(run_ai(parent, &ai) == 1007);
    return &slots[next_slot - 1];
}

static ChrRecord *reset(int bodies, int fast)
{
    gevrBodiesReset();
    memset(slots, 0, sizeof(slots));
    VrBodiesStay = bodies;
    VrFastReinforcements = fast;
    network = FALSE; players = 1; free_slots = 63; next_slot = 1; g_ClockTimer = 0;
    ChrRecord *parent = make_chr(0, 5);
    parent->chrflags = CHRFLAG_CLONE;
    return parent;
}

static void die(ChrRecord *chr)
{
    chr->actiontype = ACT_DEAD;
    chr->act_init.padding[0] = -1;
    chrlvTickDead(chr);
}

static void tick(ChrRecord *chr, int elapsed)
{
    g_ClockTimer = elapsed;
    chrlvTickDead(chr);
    g_ClockTimer = 0;
}

static void require(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "REGRESSION: %s\n", message); exit(1); }
}

static void timing(void)
{
    const int choices[] = {0, 12, 24, 48};
    for (int i = 0; i < 4; ++i) {
        ChrRecord *parent = reset(choices[i], 0), *clone = spawn(parent);
        die(clone);
        require(!gone(parent, (u8)CHR_CLONE), "corpse signalled gone before original fade time");
        tick(clone, 0);
        require(!gone(parent, (u8)CHR_CLONE), "paused corpse advanced script removal");
        tick(clone, CHRLV_TICK_DEAD_CHECK - 1);
        require(!gone(parent, (u8)CHR_CLONE), "corpse signalled gone one tick early");
        tick(clone, 1);
        if (!choices[i]) {
            assert(clone->hidden & CHRHIDDEN_REMOVE);
            clone->model = NULL; /* engine cleanup after the production fade marks removal */
        } else {
            assert(clone->model && clone->fadealpha == 255 && gevrBodyKept(clone));
        }
        require(gone(parent, (u8)CHR_CLONE), "retained corpse blocked scripted progress after fade time");
    }
}

static void tracking(void)
{
    ChrRecord *parent = reset(48, 0);
    for (int generation = 0; generation < 8; ++generation) {
        require(gone(parent, (u8)CHR_CLONE), "replacement was blocked by an old corpse");
        ChrRecord *clone = spawn(parent);
        require(clone->chrnum == 10005, "replacement spawned without its tracked clone ID");
        require(!gone(parent, (u8)CHR_CLONE), "live replacement incorrectly allowed another reinforcement");
        die(clone);
        tick(clone, CHRLV_TICK_DEAD_CHECK);
    }
}

static void retention_changes(void)
{
    ChrRecord *parent = reset(12, 0), *clone = spawn(parent);
    die(clone); tick(clone, 20);
    VrBodiesStay = 0;
    tick(clone, 1);
    assert(!gevrBodyKept(clone) && clone->act_init.padding[0] == 21);
    assert(!gone(parent, (u8)CHR_CLONE));
    VrBodiesStay = 48;
    tick(clone, 1);
    assert(!gevrBodyKept(clone)); /* never recapture a released, fading corpse */
    parent = reset(48, 0); clone = spawn(parent); die(clone);
    tick(clone, CHRLV_TICK_DEAD_CHECK);
    VrBodiesStay = 0; tick(clone, 1);
    assert(gone(parent, (u8)CHR_CLONE)); /* changing the setting cannot resurrect its ID */
    parent = reset(48, 0); clone = spawn(parent); die(clone); tick(clone, 20);
    free_slots = 3; tick(clone, 1);
    assert(!gevrBodyKept(clone) && clone->act_init.padding[0] == 21);
    parent = reset(12, 0);
    for (int i = 0; i < 13; ++i) die(spawn(parent));
    assert(s_gevrBodyCount == 12 && !gevrBodyKept(&slots[1]));
    VrBodiesStay = 0; tick(&slots[13], 0);
    assert(s_gevrBodyCount == 0);
    gevrBodiesReset(); assert(!gevrBodyKept(&slots[13]));
}

static void fast_mode(void)
{
    const int choices[] = {0, 12, 24, 48};
    for (int i = 0; i < 4; ++i) {
        ChrRecord *parent = reset(choices[i], 1), *latest = NULL;
        for (int j = 0; j < 8; ++j) {
            assert(gone(parent, (u8)CHR_CLONE));
            latest = spawn(parent);
            assert(chrFindById(parent, (u8)CHR_CLONE) == latest);
            assert(!gone(parent, 5)); /* fixed-ID mission conditions are unchanged */
            for (int a = 1; a < next_slot; ++a)
                for (int b = a + 1; b < next_slot; ++b)
                    assert(slots[a].chrnum != slots[b].chrnum);
        }
        network = TRUE; assert(!gone(parent, (u8)CHR_CLONE));
        network = FALSE; players = 2; assert(!gone(parent, (u8)CHR_CLONE));
        players = 1; VrFastReinforcements = 0;
        assert(!gone(parent, (u8)CHR_CLONE) && chrFindById(parent, (u8)CHR_CLONE) == latest);
        assert(next_slot == 9); /* default permits one; fast permits eight living reinforcements */
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "timing")) { timing(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "tracking")) { tracking(); return 0; }
    timing(); tracking(); retention_changes(); fast_mode();
    printf("PASS: %d-tick fade parity, all body counts, replacement IDs, retention/slot changes, optional swarm and solo gate\n",
           CHRLV_TICK_DEAD_CHECK);
    return 0;
}
