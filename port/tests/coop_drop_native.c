/* Production co-op hand sync and drop lifecycle; only world services are stubbed. */
#include "net_core.h"  /* Match the production network file's bool/include order. */
#include "game/chr.h"
#include "game/objecthandler.h"
#include "game/model.h"
#include "game/matrixmath.h"
#include "game/stan.h"
#include "random.h"
#include <string.h>
#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static Projectile projectiles[8];
static int allocations, freed, given, last_give_flags, room_updates, firing_stops;
PropRecord *g_ActivePropsHead, *g_ActivePropsTail;
static PropRecord player;
static Mtxf view;
static WeaponObjRecord replacement;
static PropRecord replacement_prop;
static Model replacement_model;

void objDetach(PropRecord *prop);
void chrpropReparent(PropRecord *prop, PropRecord *parent);
void chrpropDelist(PropRecord *prop);
void propobjSetDropped(PropRecord *prop, DROPTYPE droptype);
s32 objDrop(PropRecord *prop);

Projectile *projectileAllocate(void) {
    if (allocations == 8) return NULL;
    return &projectiles[allocations++];
}
void embedmentFree(Embedment *embedment) { (void)embedment; }
u32 randomGetNext(void) { return 0x80000000; }
PropRecord *getCurrentPlayerProp(void) { return &player; }
Mtxf *currentPlayerGetViewToWorldMtxf(void) { return &view; }
Mtxf *getsubmatrix(Model *model) { return (Mtxf *)model->render_pos; }
f32 getsubroty(Model *model) { (void)model; return 0; }
f32 objGetWidth(ObjectRecord *obj) { (void)obj; return 1; }
void chrSetFiring(ChrRecord *chr, s32 hand, s32 firing) {
    (void)chr; (void)hand; if (!firing) firing_stops++;
}
void sub_GAME_7F03D058(PropRecord *prop, bool unset) { (void)prop; (void)unset; }
void sub_GAME_7F0402B4(PropRecord *prop, rgba_u8 *color) {
    (void)prop; memset(color, 255, sizeof(*color));
}
void setupUpdateObjectRoomPosition(ObjectRecord *obj) { (void)obj; room_updates++; }
s32 stanTestLineUnobstructed(StandTile **tile, f32 x, f32 z, f32 dx, f32 dz,
                            int types, f32 height, f32 a, f32 b, f32 c) {
    (void)tile; (void)x; (void)z; (void)dx; (void)dz;
    (void)types; (void)height; (void)a; (void)b; (void)c; return 1;
}
s32 stanTestVolume(StandTile **tile, f32 x, f32 z, f32 radius, s32 types, f32 a, f32 b) {
    (void)tile; (void)x; (void)z; (void)radius; (void)types; (void)a; (void)b; return -1;
}
void sub_GAME_7F057C14(coord3d *speed, Mtxf *rotation) {
    *speed = (coord3d){1, 2, 3}; matrix_4x4_set_identity(rotation);
}
void matrix_4x4_set_rotation_around_xyz(coord3d *angles, Mtxf *matrix) {
    (void)angles; matrix_4x4_set_identity(matrix);
}
void objFreePermanently(ObjectRecord *obj, s32 freeprop) {
    (void)freeprop;
    if (obj->prop->parent) objDetach(obj->prop);
    else if (obj->prop->flags & PROPFLAG_ENABLED) chrpropDelist(obj->prop);
    obj->prop->flags &= ~PROPFLAG_ENABLED;
    freed++;
}
enum PROP getPropForHeldItem(ITEM_IDS item) { (void)item; return PROP_CHRKALASH; }
PropRecord *chrGiveWeapon(ChrRecord *chr, s32 model, ITEM_IDS item, s32 flags) {
    (void)model; given++; last_give_flags = flags;
    replacement.weaponnum = item; replacement.prop = &replacement_prop;
    replacement.model = &replacement_model;
    replacement_prop.type = PROP_TYPE_WEAPON; replacement_prop.weapon = &replacement;
    chrpropReparent(&replacement_prop, chr->prop);
    chr->weapons_held[(flags & PROPFLAG_WEAPON_LEFTHANDED) ? GUNLEFT : GUNRIGHT] = &replacement_prop;
    return &replacement_prop;
}

/* INSERT_PRODUCTION_DROP_FUNCTIONS */

static void tickDrops(ChrRecord *chr) {
    PropRecord *prop = chr->prop;
    /* INSERT_PRODUCTION_DROP_TICK */
}
static void syncHands(ChrRecord *chr, u8 right, u8 left) {
    NetChrState state = {0}, *st = &state;
    st->actiontype = chr->actiontype; st->weapon[0] = right; st->weapon[1] = left;
    /* INSERT_PRODUCTION_DYING */
    /* INSERT_PRODUCTION_SYNC_HANDS */
}

typedef struct {
    ChrRecord chr;
    PropRecord guard, guns[2];
    WeaponObjRecord weapons[2];
    Model models[2];
    ModelNode attachment;
    Mtxf held[2];
    StandTile tile;
} Guard;

static void reset(void) {
    memset(projectiles, 0, sizeof(projectiles));
    memset(&replacement, 0, sizeof(replacement));
    memset(&replacement_prop, 0, sizeof(replacement_prop));
    allocations = freed = given = room_updates = firing_stops = 0;
    g_ActivePropsHead = g_ActivePropsTail = NULL;
    matrix_4x4_set_identity(&view);
}
static void initGuard(Guard *g, int hands, int onscreen, int action) {
    memset(g, 0, sizeof(*g));
    g->guard.type = PROP_TYPE_CHR; g->guard.chr = &g->chr;
    g->guard.pos = (coord3d){100, 20, 300}; g->guard.stan = &g->tile;
    g->chr.prop = &g->guard; g->chr.actiontype = action;
    for (int hand = 0; hand < 2; hand++) if (hands & (1 << hand)) {
        PropRecord *prop = &g->guns[hand]; WeaponObjRecord *weapon = &g->weapons[hand];
        prop->type = PROP_TYPE_WEAPON; prop->weapon = weapon;
        if (onscreen) prop->flags = PROPFLAG_ONSCREEN;
        weapon->type = PROPDEF_COLLECTABLE; weapon->weaponnum = ITEM_AK47;
        weapon->prop = prop; weapon->model = &g->models[hand];
        weapon->runtime_bitflags = RUNTIMEBITFLAG_HASOWNER;
        weapon->model->scale = .75f; weapon->model->render_pos = (RenderPosView *)&g->held[hand];
        weapon->model->attachedto_objinst = &g->attachment;
        matrix_4x4_set_identity(&g->held[hand]);
        coord3d pos = {101 + hand, 25, 302};
        matrix_4x4_set_position(&pos, &g->held[hand]);
        chrpropReparent(prop, &g->guard); g->chr.weapons_held[hand] = prop;
    }
}
static int activeCount(void) {
    int count = 0;
    for (PropRecord *p = g_ActivePropsHead; p && count < 9; p = p->next) count++;
    return count;
}

EXPORT int test_coop_guard_drops(void) {
    for (int hands = 1; hands <= 3; hands++) for (int onscreen = 0; onscreen <= 1; onscreen++)
    for (int dead = 0; dead <= 1; dead++) {
        Guard g; reset(); initGuard(&g, hands, onscreen, dead ? ACT_DEAD : ACT_DIE);
        int count = !!(hands & 1) + !!(hands & 2);
        syncHands(&g.chr, 0, 0);
        CHECK(g.chr.hidden & CHRHIDDEN_DROP_HELD_ITEMS);
        tickDrops(&g.chr);
        CHECK(!g.guard.child && !(g.chr.hidden & CHRHIDDEN_DROP_HELD_ITEMS));
        CHECK(allocations == count && activeCount() == count && room_updates == count);
        CHECK(firing_stops == count && freed == 0 && given == 0);
        for (int hand = 0; hand < 2; hand++) if (hands & (1 << hand)) {
            PropRecord *prop = &g.guns[hand]; WeaponObjRecord *weapon = &g.weapons[hand];
            CHECK(!g.chr.weapons_held[hand] && !prop->parent && prop->stan == &g.tile);
            CHECK(prop->type == PROP_TYPE_WEAPON && (prop->flags & PROPFLAG_ENABLED));
            CHECK(!(weapon->runtime_bitflags & RUNTIMEBITFLAG_HASOWNER));
            CHECK(!weapon->model->attachedto_objinst && weapon->projectile);
            CHECK(weapon->projectile->droptype == DROPTYPE_DEFAULT);
            CHECK(weapon->projectile->flags & PROJECTILEFLAG_AIRBORNE);
            CHECK(weapon->projectile->ownerprop == &g.guard);
            CHECK(prop->pos.x == (onscreen ? 101 + hand : 100));
            CHECK(prop->pos.y == (onscreen ? 25 : 20));
            CHECK(prop->pos.z == (onscreen ? 302 : 300));
            CHECK(!memcmp(&weapon->runtime_pos, &prop->pos, sizeof(prop->pos)));
        }
        syncHands(&g.chr, 0, 0); tickDrops(&g.chr);
        CHECK(allocations == count && activeCount() == count && room_updates == count);
        for (int hand = 0; hand < 2; hand++) if (hands & (1 << hand))
            objFreePermanently((ObjectRecord *)&g.weapons[hand], 1);  /* Local collection removes the prop. */
        syncHands(&g.chr, 0, 0); tickDrops(&g.chr);
        CHECK(activeCount() == 0 && allocations == count && given == 0);
    }
    return 0;
}

EXPORT int test_coop_guard_hand_changes(void) {
    for (int hand = 0; hand < 2; hand++) {
        Guard g; reset(); initGuard(&g, 1 << hand, 0, ACT_STAND);
        g.weapons[hand].flags |= PROPFLAG_AIUNDROPPABLE;
        syncHands(&g.chr, hand == GUNRIGHT ? ITEM_AK47 : 0, hand == GUNLEFT ? ITEM_AK47 : 0);
        CHECK(g.chr.weapons_held[hand] == &g.guns[hand] && allocations == 0 && freed == 0 && given == 0);
        syncHands(&g.chr, 0, 0); tickDrops(&g.chr);
        CHECK(!g.chr.weapons_held[hand] && !g.guard.child && freed == 1);
        CHECK(allocations == 0 && activeCount() == 0 && !(g.chr.hidden & CHRHIDDEN_DROP_HELD_ITEMS));
        reset(); initGuard(&g, 1 << hand, 0, ACT_STAND);
        syncHands(&g.chr, hand == GUNRIGHT ? ITEM_TT33 : 0, hand == GUNLEFT ? ITEM_TT33 : 0);
        CHECK(freed == 1 && given == 1 && allocations == 0);
        CHECK(coopWeaponOf(&g.chr, hand) == ITEM_TT33 && g.guard.child == &replacement_prop);
        CHECK(last_give_flags == (hand == GUNLEFT ? PROPFLAG_WEAPON_LEFTHANDED : 0));
    }
    return 0;
}

EXPORT int test_coop_guard_pickups_are_local(void) {
    Guard host, client; reset();
    initGuard(&host, 1, 0, ACT_DIE); initGuard(&client, 1, 0, ACT_DIE);
    propobjSetDropped(&host.guns[GUNRIGHT], DROPTYPE_DEFAULT);
    host.chr.hidden |= CHRHIDDEN_DROP_HELD_ITEMS; tickDrops(&host.chr);
    syncHands(&client.chr, 0, 0); tickDrops(&client.chr);
    CHECK(activeCount() == 2 && !host.guns[0].parent && !client.guns[0].parent);
    objFreePermanently((ObjectRecord *)&host.weapons[0], 1);
    syncHands(&client.chr, 0, 0); tickDrops(&client.chr);
    CHECK(activeCount() == 1 && (client.guns[0].flags & PROPFLAG_ENABLED));
    CHECK(allocations == 2 && given == 0);
    objFreePermanently((ObjectRecord *)&client.weapons[0], 1);
    syncHands(&client.chr, 0, 0); tickDrops(&client.chr);
    CHECK(activeCount() == 0 && allocations == 2 && given == 0);
    return 0;
}
