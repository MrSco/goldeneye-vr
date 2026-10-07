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
    struct { StandTile *current_tile_ptr; coord3d collision_position; } field_488;
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
static int last_reliable;
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
    last_reliable = reliable;
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

/* The tank's pose on every headset: the game's helpers it calls */
#define COOP_LOG(...) ((void)0)
#ifndef M_TAU_F
#define M_TAU_F 6.2831855f
#endif
#define TANK_UNKD0_SCALE 0.83f
static u64 now_us = 5000000;
static int local_spectator;
static StandTile *walk_fail_from;
static StandTile *walk_dest;
static int room_updates;
static int collision_updates;
static StandTile walked_tile;
u64 sysGetMicroseconds(void) { return now_us; }
bool netLocalIsSpectator(void) { return local_spectator; }
int gevrCoopActive(void) { return coop_active; }
void matrix_4x4_set_rotation_around_y(f32 angle, Mtxf *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0][0] = cosf(angle); m->m[0][2] = -sinf(angle);
    m->m[1][1] = 1.0f;
    m->m[2][0] = sinf(angle); m->m[2][2] = cosf(angle);
    m->m[3][3] = 1.0f;
}
void matrix_scalar_multiply(f32 scalar, f32 *matrix) { for (int i = 0; i < 12; i++) matrix[i] *= scalar; }
void matrix_4x4_copy(Mtxf *src, Mtxf *dst) { *dst = *src; }
s32 walkTilesBetweenPoints_NoCallback(StandTile **tile, f32 sx, f32 sz, f32 dx, f32 dz)
{
    (void)sx; (void)sz; (void)dx; (void)dz;
    if (*tile == walk_fail_from) return 0;
    *tile = walk_dest ? walk_dest : &walked_tile;
    return 1;
}
f32 stanGetPositionYValue(StandTile *tile, f32 x, f32 z) { (void)tile; (void)x; (void)z; return 7.0f; }
f32 chrpropBBOXGetYmin(ModelRoData_BoundingBoxRecord *box) { (void)box; return -10.0f; }
void setupUpdateObjectRoomPosition(ObjectRecord *obj) { (void)obj; room_updates++; }
void chrobjCollisionRelated(ObjectRecord *obj) { (void)obj; collision_updates++; }

void gevrCoopApplyTank(s32 slot, s32 driven, const f32 pos[3], f32 yaw, f32 turretyaw, f32 turretpitch, s32 firing);
/* INSERT_TANK_SEND */

/* INSERT_TANK_SYNC */

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

/* The driver's headset sends its tank; another headset poses the same tank there. */
static void tank_record(PropRecord *prop, TankRecord *tank, Model *model, ModelFileHeader *header,
                        ModelNode *switches, ModelNode *child, f32 x, f32 z)
{
    memset(prop, 0, sizeof(*prop));
    memset(tank, 0, sizeof(*tank));
    memset(model, 0, sizeof(*model));
    memset(header, 0, sizeof(*header));
    memset(switches, 0, sizeof(*switches));
    memset(child, 0, sizeof(*child));
    /* bondview2.c reads the bounding box as the decompiled driver code does */
    switches->Child = child;
    header->Switches = (ModelNode **)switches;
    model->obj = header;
    model->scale = 0.5f;
    tank->type = PROPDEF_TANK;
    tank->model = model;
    tank->prop = prop;
    prop->type = PROP_TYPE_OBJ;
    prop->obj = (ObjectRecord *)tank;
    prop->pos.x = x;
    prop->pos.z = z;
}

/* The last packet sent: this headset's tank pose, past the header */
static struct netbuf tank_packet(u8 *flags)
{
    struct netbuf b = open_last();
    assert(netbufReadU8(&b) == NET_MSG_COOP_TANK);
    assert(netbufReadU8(&b) == (u8)local_slot);
    assert(netbufReadLeft(&b) == NET_COOP_TANK_BYTES);
    *flags = netbufReadU8(&b);
    return b;
}

static void tank_pose(struct netbuf *w, u8 flags, f32 x, f32 y, f32 z, f32 yaw, f32 turretyaw, f32 turretpitch)
{
    netbufWriteU8(w, flags);
    netbufWriteF32(w, x); netbufWriteF32(w, y); netbufWriteF32(w, z);
    netbufWriteF32(w, yaw); netbufWriteF32(w, turretyaw); netbufWriteF32(w, turretpitch);
    rewind_written(w);
}

static void run_tank_sync(void)
{
    PropRecord prop, other, decoy;
    TankRecord tank, othertank, decoytank;
    Model model, othermodel, decoymodel;
    ModelFileHeader header, otherheader, decoyheader;
    ModelNode switches, child, oswitches, ochild, dswitches, dchild;
    struct player rider;
    StandTile start, ridertile;
    struct netbuf b;
    u8 raw[64];
    u8 flags;
    int before;

    gevrCoopTankReset();
    tank_record(&prop, &tank, &model, &header, &switches, &child, 100.0f, 200.0f);
    coop_active = 1;
    net_active = 1;
    local_slot = 1;
    cur_player = 1;
    is_host = 0;
    puppets = 1;
    local_spectator = 0;

    /* Driving: the pose goes out unreliably, at most 20 times a second. */
    g_PlayerIsInTank = 1;
    g_PlayerTankProp = &prop;
    prop.pos.x = 150.0f; prop.pos.y = 40.0f; prop.pos.z = 260.0f;
    tank.tank_orientation_angle = 1.25f;
    tank.turret_orientation_angle = 0.5f;
    tank.turret_vertical_angle = 0.125f;
    tank.is_firing_tank = 1;
    before = broadcasts;
    gevrCoopSendTank();
    assert(broadcasts == before + 1 && !last_reliable);
    b = tank_packet(&flags);
    assert(flags == (NET_COOP_TANK_DRIVEN | NET_COOP_TANK_FIRING));
    assert(netbufReadF32(&b) == 150.0f && netbufReadF32(&b) == 40.0f && netbufReadF32(&b) == 260.0f);
    assert(netbufReadF32(&b) == 1.25f && netbufReadF32(&b) == 0.5f && netbufReadF32(&b) == 0.125f);
    now_us += 10000;
    gevrCoopSendTank();
    assert(broadcasts == before + 1);
    now_us += 50000;
    gevrCoopSendTank();
    assert(broadcasts == before + 2);

    /* Another headset's player ticking here sends nothing. */
    cur_player = 2;
    now_us += 60000;
    gevrCoopSendTank();
    assert(broadcasts == before + 2);
    cur_player = 1;

    /* Out of the tank: one reliable last pose, even inside the 50 ms, then quiet. */
    g_PlayerIsInTank = 0;
    g_PlayerTankProp = NULL;
    now_us += 1000;
    gevrCoopSendTank();
    assert(broadcasts == before + 3 && last_reliable);
    b = tank_packet(&flags);
    assert(flags == 0);
    now_us += 100000;
    gevrCoopSendTank();
    assert(broadcasts == before + 3);

    /* Not co-op, or a spectator: nothing. */
    g_PlayerIsInTank = 1;
    g_PlayerTankProp = &prop;
    coop_active = 0;
    now_us += 100000;
    gevrCoopSendTank();
    coop_active = 1;
    gevrCoopTankReset();
    local_spectator = 1;
    now_us += 100000;
    gevrCoopSendTank();
    local_spectator = 0;
    assert(broadcasts == before + 3);

    /* Another headset: its own copy of the tank, parked at the start, and a far decoy. */
    gevrCoopTankReset();
    g_PlayerIsInTank = 0;
    g_PlayerTankProp = NULL;
    tank_record(&other, &othertank, &othermodel, &otherheader, &oswitches, &ochild, 100.0f, 200.0f);
    tank_record(&decoy, &decoytank, &decoymodel, &decoyheader, &dswitches, &dchild, 5000.0f, 5000.0f);
    decoy.prev = &other;
    active_tail = &decoy;
    other.stan = &start;
    g_PlayerIsInTank = 1;
    g_PlayerTankProp = &prop;
    now_us += 100000;
    gevrCoopSendTank();          /* the driver's packet, from slot 1 */
    g_PlayerIsInTank = 0;
    g_PlayerTankProp = NULL;
    local_slot = 0;
    room_updates = collision_updates = 0;
    b = open_last();
    assert(netbufReadU8(&b) == NET_MSG_COOP_TANK);
    assert(netbufReadU8(&b) == 1);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 150.0f && other.pos.y == 40.0f && other.pos.z == 260.0f);
    assert(othertank.runtime_pos.x == 150.0f && othertank.runtime_pos.z == 260.0f);
    assert(othertank.tank_orientation_angle == 1.25f);
    assert(othertank.turret_orientation_angle == 0.5f);
    assert(othertank.turret_vertical_angle == 0.125f);
    assert(othertank.is_firing_tank == 1);
    assert(fabsf(othertank.mtx.m[0][0] - cosf(M_TAU_F - 1.25f) * 0.5f) < 1e-5f);
    assert(fabsf(othertank.mtx.m[2][0] - sinf(M_TAU_F - 1.25f) * 0.5f) < 1e-5f);
    assert(other.stan == &walked_tile && othertank.stan_y == 7.0f);
    /* the ground it rides on, as the driver's MoveBond smooths it */
    assert(fabsf(othertank.unkD0 * (1.0f - TANK_UNKD0_SCALE) - (-10.0f * 0.5f) + 4.0f - 40.0f) < 1e-3f);
    assert(room_updates == 1 && collision_updates == 1);
    assert(decoy.pos.x == 5000.0f && decoytank.tank_orientation_angle == 0.0f);
    /* a teammate drives it: this headset's player cannot get in */
    assert(gevrCoopTankTaken(&other));
    assert(!gevrCoopTankTaken(&decoy));
    now_us += 2000000;
    assert(!gevrCoopTankTaken(&other));

    /* A walk that does not get there: walked from the rider's tile instead. */
    memset(&rider, 0, sizeof(rider));
    rider.field_488.current_tile_ptr = &start;
    g_playerPointers[1] = &rider;
    walk_fail_from = &walked_tile;
    walk_dest = &ridertile;
    b = written(raw, sizeof(raw));
    tank_pose(&b, NET_COOP_TANK_DRIVEN, 160.0f, 40.0f, 270.0f, 1.25f, 0.5f, 0.125f);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 160.0f && other.stan == &ridertile);
    /* ... and if that fails too, the tank's own tile stands. */
    walk_fail_from = &ridertile;
    rider.field_488.current_tile_ptr = &ridertile;
    b = written(raw, sizeof(raw));
    tank_pose(&b, NET_COOP_TANK_DRIVEN, 165.0f, 40.0f, 275.0f, 1.25f, 0.5f, 0.125f);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 165.0f && other.stan == &ridertile);
    walk_fail_from = NULL;
    walk_dest = NULL;
    g_playerPointers[1] = NULL;

    /* The parked pose frees it, and the gun flash goes out. */
    b = written(raw, sizeof(raw));
    tank_pose(&b, 0, 180.0f, 41.0f, 300.0f, 2.0f, 0.25f, 0.0f);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 180.0f && othertank.tank_orientation_angle == 2.0f);
    assert(othertank.is_firing_tank == 0);
    assert(!gevrCoopTankTaken(&other));

    /* Not a number, a short packet, or this headset's own slot: ignored. */
    b = written(raw, sizeof(raw));
    tank_pose(&b, NET_COOP_TANK_DRIVEN, NAN, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 180.0f);
    b = written(raw, sizeof(raw));
    netbufWriteU8(&b, NET_COOP_TANK_DRIVEN);
    netbufWriteF32(&b, 1.0f);
    rewind_written(&b);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 180.0f);
    b = written(raw, sizeof(raw));
    tank_pose(&b, NET_COOP_TANK_DRIVEN, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    coopReceiveTank(0, &b);
    assert(other.pos.x == 180.0f);

    /* This headset's player drives it: its own pose stands. */
    g_PlayerIsInTank = 1;
    g_PlayerTankProp = &other;
    b = written(raw, sizeof(raw));
    tank_pose(&b, NET_COOP_TANK_DRIVEN, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    coopReceiveTank(1, &b);
    assert(other.pos.x == 180.0f);
    g_PlayerIsInTank = 0;
    g_PlayerTankProp = NULL;

    /* A new stage forgets the old tank. */
    gevrCoopTankReset();
    assert(!gevrCoopTankTaken(&other));
    active_tail = NULL;
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
    run_tank_sync();
    return 0;
}
