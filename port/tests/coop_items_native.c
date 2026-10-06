/* Production co-op grant, gadget-use and tank helpers, with the game's netbuf. */
#include "net_core.h"
#include "net/netbuf.h"
#include "aicommands2.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }

struct player {
    PropRecord *prop;
    s32 bonddead;
};
struct player *g_playerPointers[4];
u32 *ptr_last_tag_entry_type16;

static int cur_player;
static int local_slot;
static int is_host;
static int coop_active = 1;
static int net_active = 1;
static int puppets;
static int downed[4];
static int occupied[4];
static int spectator[4];
static int owned[4][ITEM_IDS_MAX];
static int adds;
static u8 last[64];
static unsigned last_len;
static int broadcasts;
static PropRecord *collision_prop;
static int collision_calls;
static int tank_solid = 1;
static int goto_calls;
static s32 goto_offset;
static ObjectRecord *tagged;
static AIRecord *AiListp;
static s32 Offset;

s32 get_cur_playernum(void) { return cur_player; }
void set_cur_player(s32 playernum) { cur_player = playernum; }
int netGetLocalSlot(void) { return local_slot; }
bool netIsHost(void) { return is_host; }
int netCoopActive(void) { return coop_active; }
bool netIsActive(void) { return net_active; }
int gevrCoopPuppets(void) { return puppets; }
int gevrCoopDowned(int player) { return player >= 0 && player < 4 && downed[player]; }
bool netSlotOccupied(int slot) { return slot >= 0 && slot < 4 && occupied[slot]; }
bool netSlotIsSpectator(int slot) { return slot >= 0 && slot < 4 && spectator[slot]; }
int bondinvAddInvItem(int item)
{
    assert(cur_player >= 0 && cur_player < 4);
    assert(item > ITEM_UNARMED && item < ITEM_IDS_MAX);
    if (owned[cur_player][item]) return FALSE;
    owned[cur_player][item] = TRUE;
    adds++;
    return TRUE;
}
void netCoopBroadcast(const unsigned char *data, unsigned int size, int reliable)
{
    assert(size <= sizeof(last));
    memcpy(last, data, size);
    last_len = size;
    broadcasts++;
    (void)reliable;
}
ObjectRecord *objFindByTagId(s32 tag)
{
    return tagged && tag == 7 ? tagged : NULL;
}
s32 chraiGoToLabel(AIRecord *list, s32 offset, u8 label)
{
    assert(list == AiListp);
    assert(offset == Offset);
    assert(label == 9);
    goto_calls++;
    return goto_offset;
}
static PropRecord *active_tail;
static int delist_calls;
static int reparent_calls;
static int projectile_frees;
PropRecord *chrpropGetActiveTail(void) { return active_tail; }
void chrpropDelist(PropRecord *prop) { delist_calls++; (void)prop; }
void chrpropReparent(PropRecord *child, PropRecord *host)
{
    reparent_calls++;
    child->parent = host;
    child->prev = host->child;
    if (host->child) host->child->next = child;
    child->next = NULL;
    host->child = child;
}
void projectileFree(struct Projectile *projectile) { projectile_frees++; (void)projectile; }
void sub_GAME_7F03D058(PropRecord *prop, bool enable)
{
    collision_calls++;
    collision_prop = prop;
    tank_solid = enable ? 1 : 0;
}

s32 g_PlayerIsInTank;
struct PropRecord *g_WorldTankProp;
struct PropRecord *g_PlayerTankProp;
f32 g_PlayerTankYOffset;
s32 g_BondCanEnterTank;

/* INSERT_NEAREST */

/* INSERT_GRANT */

/* INSERT_GADGET */

/* INSERT_MINE */

/* INSERT_TANK */

static void host_event(struct netbuf *b)
{
    u8 kind = netbufReadU8(b);
    switch (kind) {
        /* INSERT_GRANT_CASE */
        /* INSERT_GADGET_CASE */
        default:
            break;
    }
}

static void run_gadget_check(void)
{
    switch (AI_IFBondUsedGadgetOnObject) {
        /* INSERT_AI_CASE */
        default:
            assert(0);
            break;
    }
}

static struct netbuf open_last(void)
{
    struct netbuf b;
    netbufStartReadData(&b, last, last_len);
    assert(netbufReadU32(&b) == GEVR_NET_MAGIC);
    assert(netbufReadU16(&b) == GEVR_NET_VERSION);
    return b;
}

static struct netbuf written(u8 *raw, u32 cap)
{
    struct netbuf b = { .data = raw, .size = cap };
    netbufStartWrite(&b);
    return b;
}

static void rewind_written(struct netbuf *b)
{
    netbufStartReadData(b, b->data, b->wp);
}

static void reset_party(void)
{
    memset(owned, 0, sizeof(owned));
    adds = 0;
    broadcasts = 0;
    last_len = 0;
    cur_player = 3;
}

int main(void)
{
    struct player players[4];
    PropRecord props[4];
    PropRecord tank;
    coord3d query;
    struct netbuf packet;
    u8 raw[16];
    TagObjectRecord tag;
    ObjectRecord *obj;
    PropRecord *objprop;
    int before;

    memset(players, 0, sizeof(players));
    memset(props, 0, sizeof(props));
    memset(&tank, 0, sizeof(tank));
    for (int i = 0; i < 4; i++) {
        props[i].pos.x = (f32)(i * 100);
        players[i].prop = &props[i];
        g_playerPointers[i] = &players[i];
        occupied[i] = 1;
    }
    /* A hole and a spectator: the grant still reaches the later player. */
    g_playerPointers[1] = NULL;
    occupied[1] = 0;
    spectator[3] = 1;
    local_slot = 0;
    is_host = 1;
    puppets = 0;
    coop_active = 1;

    assert(gevrCoopSharedGadget(ITEM_DOORDECODER));
    assert(gevrCoopSharedGadget(ITEM_DATATHIEF));
    assert(gevrCoopSharedGadget(ITEM_BOMBDEFUSER));
    assert(gevrCoopSharedGadget(ITEM_EXPLOSIVEFLOPPY));
    assert(gevrCoopSharedGadget(ITEM_DATTAPE));
    assert(!gevrCoopSharedGadget(ITEM_WPPK));
    assert(!gevrCoopSharedGadget(ITEM_GOLDENGUN));
    assert(!gevrCoopSharedGadget(ITEM_TANKSHELLS));
    assert(!gevrCoopSharedGadget(ITEM_KEYYALE));
    assert(!gevrCoopSharedGadget(ITEM_GOLDENEYEKEY));
    assert(!gevrCoopSharedGadget(ITEM_TOKEN));

    reset_party();
    gevrCoopGrantItem(ITEM_DOORDECODER);
    assert(owned[0][ITEM_DOORDECODER] && owned[2][ITEM_DOORDECODER]);
    assert(!owned[3][ITEM_DOORDECODER]);
    assert(adds == 2);
    assert(cur_player == 3);
    packet = open_last();
    assert(netbufReadU8(&packet) == NET_MSG_COOP_GRANT);
    assert(netbufReadU8(&packet) == 0);
    assert(netbufReadS32(&packet) == ITEM_DOORDECODER);
    assert(!netbufReadLeft(&packet));
    before = adds;
    gevrCoopGrantItem(ITEM_DOORDECODER);
    assert(adds == before);

    reset_party();
    gevrCoopGrantItem(ITEM_WPPK);
    gevrCoopGrantItem(ITEM_KEYYALE);
    gevrCoopGrantItem(ITEM_TOKEN);
    assert(adds == 0 && broadcasts == 0);

    /* A client grant is an event. The host's copy of that event reaches every slot. */
    reset_party();
    is_host = 0;
    puppets = 1;
    gevrCoopGrantItem(ITEM_DATTAPE);
    assert(owned[0][ITEM_DATTAPE] && owned[2][ITEM_DATTAPE] && adds == 2);
    packet = open_last();
    assert(netbufReadU8(&packet) == NET_MSG_COOP_EVENT);
    assert(netbufReadU8(&packet) == (u8)local_slot);
    assert(netbufReadU8(&packet) == NET_COOP_EVENT_GRANT);
    assert(netbufReadS32(&packet) == ITEM_DATTAPE);

    reset_party();
    is_host = 1;
    puppets = 0;
    packet = written(raw, sizeof(raw));
    netbufWriteU8(&packet, NET_COOP_EVENT_GRANT);
    netbufWriteS32(&packet, ITEM_BOMBDEFUSER);
    rewind_written(&packet);
    host_event(&packet);
    assert(owned[0][ITEM_BOMBDEFUSER] && owned[2][ITEM_BOMBDEFUSER]);
    assert(!owned[3][ITEM_BOMBDEFUSER]);
    packet = open_last();
    assert(netbufReadU8(&packet) == NET_MSG_COOP_GRANT);
    assert(netbufReadU8(&packet) == (u8)local_slot);
    assert(netbufReadS32(&packet) == ITEM_BOMBDEFUSER);

    /* The host's grant packet, on a client: every local slot, and no echo. */
    reset_party();
    is_host = 0;
    puppets = 1;
    before = broadcasts;
    packet = written(raw, sizeof(raw));
    netbufWriteS32(&packet, ITEM_DOORDECODER);
    rewind_written(&packet);
    coopReceiveGrant(&packet);
    assert(owned[0][ITEM_DOORDECODER] && owned[2][ITEM_DOORDECODER]);
    assert(broadcasts == before);
    before = adds;
    packet = written(raw, sizeof(raw));
    netbufWriteS32(&packet, ITEM_DOORDECODER);
    rewind_written(&packet);
    coopReceiveGrant(&packet);
    assert(adds == before);
    packet = written(raw, sizeof(raw));
    netbufWriteS32(&packet, ITEM_WPPK);
    rewind_written(&packet);
    coopReceiveGrant(&packet);
    assert(adds == before);

    query.x = 80.0f;
    query.y = 0.0f;
    query.z = 0.0f;
    g_playerPointers[1] = &players[1];
    occupied[1] = 1;
    spectator[1] = 0;
    assert(gevrCoopNearestPlayer(&query) == 1);
    query.x = 20.0f;
    assert(gevrCoopNearestPlayer(&query) == 0);
    players[1].bonddead = TRUE;
    query.x = 80.0f;
    assert(gevrCoopNearestPlayer(&query) == 0);
    players[1].bonddead = FALSE;
    spectator[1] = 1;
    assert(gevrCoopNearestPlayer(&query) == 0);
    spectator[1] = 0;
    downed[1] = 1;
    assert(gevrCoopNearestPlayer(&query) == 0);
    downed[1] = 0;
    props[1].pos.x = 10.0f;
    props[1].pos.y = 100.0f;
    query.x = 10.0f;
    assert(gevrCoopNearestPlayer(&query) == 0);
    props[1].pos.y = 0.0f;
    assert(gevrCoopNearestPlayer(&query) == 1);
    local_slot = 2;
    assert(gevrCoopNearestPlayer(NULL) == 2);
    local_slot = -1;
    assert(gevrCoopNearestPlayer(NULL) == 0);
    local_slot = 0;

    obj = calloc(1, sizeof(*obj));
    objprop = calloc(1, sizeof(*objprop));
    assert(obj && objprop);
    obj->prop = objprop;
    tagged = obj;
    memset(&tag, 0, sizeof(tag));
    tag.ID = 7;
    tag.TaggedObject = obj;
    ptr_last_tag_entry_type16 = (u32 *)&tag;

    puppets = 1;
    cur_player = 0;
    broadcasts = 0;
    gevrCoopReportGadgetUse(obj);
    packet = open_last();
    assert(netbufReadU8(&packet) == NET_MSG_COOP_EVENT);
    assert(netbufReadU8(&packet) == (u8)local_slot);
    assert(netbufReadU8(&packet) == NET_COOP_EVENT_GADGET);
    assert(netbufReadS32(&packet) == 7);

    before = broadcasts;
    puppets = 0;
    gevrCoopReportGadgetUse(obj);
    cur_player = 1;
    puppets = 1;
    gevrCoopReportGadgetUse(obj);
    tag.TaggedObject = NULL;
    cur_player = 0;
    gevrCoopReportGadgetUse(obj);
    assert(broadcasts == before);
    tag.TaggedObject = obj;

    is_host = 1;
    puppets = 0;
    packet = written(raw, sizeof(raw));
    netbufWriteU8(&packet, NET_COOP_EVENT_GADGET);
    netbufWriteS32(&packet, 7);
    rewind_written(&packet);
    host_event(&packet);
    assert(obj->state & PROPSTATE_ACTIVATED);

    AiListp = (AIRecord *)(u8[]){ AI_IFBondUsedGadgetOnObject, 7, 9 };
    obj->state = 0;
    Offset = 0;
    goto_calls = 0;
    run_gadget_check();
    assert(goto_calls == 0);
    assert(Offset == (s32)sizeof(AiIFBondUsedGadgetOnObjectRecord));

    coopApplyGadgetUse(7);
    assert(obj->state & PROPSTATE_ACTIVATED);
    Offset = 0;
    goto_offset = 42;
    run_gadget_check();
    assert(goto_calls == 1);
    assert(Offset == 42);
    assert((obj->state & PROPSTATE_ACTIVATED) == 0);
    run_gadget_check();
    assert(goto_calls == 1);
    assert(Offset == 42 + (s32)sizeof(AiIFBondUsedGadgetOnObjectRecord));

    /* Player A is this headset. Player B's tick must not take the deck. */
    memset(&tank, 0, sizeof(tank));
    net_active = 1;
    local_slot = 0;
    cur_player = 0;
    g_WorldTankProp = &tank;
    g_PlayerTankProp = &tank;
    g_PlayerTankYOffset = 48.0f;
    g_BondCanEnterTank = 1;
    g_PlayerIsInTank = 0;
    tank_solid = 1;
    {
        GevrTankGuard guard;
        gevrCoopPushTank(&guard);
        assert(!guard.active);
        assert(g_WorldTankProp == &tank && g_BondCanEnterTank == 1);
        gevrCoopPopTank(&guard);

        cur_player = 1;
        gevrCoopPushTank(&guard);
        assert(guard.active);
        assert(g_WorldTankProp == NULL);
        assert(g_PlayerTankProp == NULL);
        assert(g_PlayerTankYOffset == 0.0f);
        assert(g_BondCanEnterTank == 0);
        assert(g_PlayerIsInTank == 0);
        if (g_WorldTankProp != NULL && g_PlayerTankProp != NULL) gevrCoopReleaseTank();
        assert(tank_solid == 1);
        assert(collision_calls == 0);
        gevrCoopPopTank(&guard);
        assert(g_WorldTankProp == &tank);
        assert(g_PlayerTankProp == &tank);
        assert(g_PlayerTankYOffset == 48.0f);
        assert(g_BondCanEnterTank == 1);
        assert(tank_solid == 1);
    }

    cur_player = 0;
    tank_solid = 0;
    collision_calls = 0;
    gevrCoopReleaseTank();
    assert(g_WorldTankProp == NULL);
    assert(g_PlayerTankProp == NULL);
    assert(g_PlayerTankYOffset == 0.0f);
    assert(g_BondCanEnterTank == 0);
    assert(tank_solid == 1);
    assert(collision_prop == &tank);
    collision_calls = 0;
    gevrCoopReleaseTank();
    assert(collision_calls == 0);

    net_active = 0;
    cur_player = 1;
    g_WorldTankProp = &tank;
    g_PlayerTankYOffset = 12.0f;
    g_BondCanEnterTank = 1;
    {
        GevrTankGuard guard;
        gevrCoopPushTank(&guard);
        assert(!guard.active);
        assert(g_WorldTankProp == &tank);
        assert(g_PlayerTankYOffset == 12.0f);
        assert(g_BondCanEnterTank == 1);
    }

    /* Surface 2: a settled remote mine that is not on the helicopter fails.
     * One still in the air does not. The host places the thrower's mine. */
    {
        WeaponObjRecord *mine = calloc(1, sizeof(*mine));
        PropRecord *mineprop = calloc(1, sizeof(*mineprop));
        struct Projectile flight;
        int before_tx;
        assert(mine && mineprop);
        mine->type = PROPDEF_COLLECTABLE;
        mine->weaponnum = ITEM_REMOTEMINE;
        mine->runtime_bitflags = RUNTIMEBITFLAG_HASPROJECTILE | RUNTIME_OWNER_BITS(1);
        mine->projectile = &flight;
        mine->prop = mineprop;
        mineprop->type = PROP_TYPE_WEAPON;
        mineprop->weapon = mine;
        active_tail = mineprop;
        objprop->type = PROP_TYPE_OBJ;
        objprop->obj = obj;
        objprop->child = NULL;

        assert(gevrCoopThrownMissionItem(ITEM_REMOTEMINE));
        assert(gevrCoopThrownMissionItem(ITEM_TIMEDMINE));
        assert(gevrCoopThrownMissionItem(ITEM_BUG));
        assert(!gevrCoopThrownMissionItem(ITEM_WPPK));
        assert(!gevrCoopThrownMissionItem(ITEM_GRENADE));

        is_host = 1;
        puppets = 0;
        coop_active = 1;
        local_slot = 0;
        assert(gevrCoopDeferRemoteMineSettle(mine));
        mine->runtime_bitflags = RUNTIMEBITFLAG_HASPROJECTILE | RUNTIME_OWNER_BITS(0);
        assert(!gevrCoopDeferRemoteMineSettle(mine));
        mine->runtime_bitflags = RUNTIMEBITFLAG_HASPROJECTILE | RUNTIME_OWNER_BITS(1);
        is_host = 0;
        assert(!gevrCoopDeferRemoteMineSettle(mine));
        is_host = 1;
        coop_active = 0;
        assert(!gevrCoopDeferRemoteMineSettle(mine));
        coop_active = 1;
        mine->weaponnum = ITEM_WPPK;
        assert(!gevrCoopDeferRemoteMineSettle(mine));
        mine->weaponnum = ITEM_REMOTEMINE;

        /* In the air: not landed, not on the helicopter. */
        assert(objprop->child == NULL);
        assert(mine->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE);

        puppets = 1;
        is_host = 0;
        cur_player = 0;
        local_slot = 0;
        before_tx = broadcasts;
        gevrCoopReportMineSettled(mine, objprop);
        packet = open_last();
        assert(netbufReadU8(&packet) == NET_MSG_COOP_EVENT);
        assert(netbufReadU8(&packet) == (u8)local_slot);
        assert(netbufReadU8(&packet) == NET_COOP_EVENT_MINE);
        assert(netbufReadS32(&packet) == ITEM_REMOTEMINE);
        assert(netbufReadS32(&packet) == 7);
        gevrCoopReportMineSettled(mine, NULL);
        packet = open_last();
        assert(netbufReadU8(&packet) == NET_MSG_COOP_EVENT);
        assert(netbufReadU8(&packet) == (u8)local_slot);
        assert(netbufReadU8(&packet) == NET_COOP_EVENT_MINE);
        assert(netbufReadS32(&packet) == ITEM_REMOTEMINE);
        assert(netbufReadS32(&packet) == -1);
        before_tx = broadcasts;
        puppets = 0;
        gevrCoopReportMineSettled(mine, objprop);
        cur_player = 1;
        puppets = 1;
        gevrCoopReportMineSettled(mine, objprop);
        mine->weaponnum = ITEM_WPPK;
        cur_player = 0;
        gevrCoopReportMineSettled(mine, objprop);
        mine->weaponnum = ITEM_REMOTEMINE;
        assert(broadcasts == before_tx);

        is_host = 1;
        puppets = 0;
        delist_calls = 0;
        reparent_calls = 0;
        projectile_frees = 0;
        coopApplyMineSettled(1, ITEM_REMOTEMINE, 7);
        assert(mineprop->parent == objprop);
        assert(objprop->child == mineprop);
        assert((mine->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0);
        assert(mine->projectile == NULL);
        assert(delist_calls == 1 && reparent_calls == 1 && projectile_frees == 1);

        mine->runtime_bitflags = RUNTIMEBITFLAG_HASPROJECTILE | RUNTIME_OWNER_BITS(1);
        mine->projectile = &flight;
        mineprop->parent = NULL;
        objprop->child = NULL;
        delist_calls = 0;
        reparent_calls = 0;
        coopApplyMineSettled(1, ITEM_REMOTEMINE, -1);
        assert(mineprop->parent == NULL);
        assert(objprop->child == NULL);
        assert((mine->runtime_bitflags & RUNTIMEBITFLAG_HASPROJECTILE) == 0);
        assert(delist_calls == 0 && reparent_calls == 0);
        /* Settled and not on the helicopter: the list fails the objective. */
        assert(active_tail == mineprop);

        free(mine);
        free(mineprop);
    }

    free(obj);
    free(objprop);
    return 0;
}
