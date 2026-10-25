/* Production arena adaptation, native guard routing and floor routing without a ROM. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <memp.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_stage.h"
#include "gevr_arena.h"
#include "gevr_bot.h"
#include "bondhead.h"
#include "net_match.h"

struct stagesetup g_CurrentSetup;
static int mode;
int netHordeActive(void) { return mode == NET_MODE_HORDE; }
s32 gevrMpRules(void) { return mode == NET_MODE_DEATHMATCH; }
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
void sysFatalError(const char *fmt, ...) { fprintf(stderr, "fatal: %s\n", fmt); abort(); }
void osSyncPrintf(const char *fmt, ...) { (void)fmt; }
u32 randomGetNext(void) { return 42; }
s32 sizepropdef(PropDefHeaderRecord *p) { return gevrSetupPropWords(p->type); }
static void *allocations[128];
static int allocated;
void *mempAllocBytesInBank(u32 size, u8 bank) {
    assert(bank == MEMPOOL_STAGE && allocated < 128);
    void *p = calloc(1, size); assert(p); allocations[allocated++] = p; return p;
}
static void clear_allocations(void) { while (allocated) free(allocations[--allocated]); }

StandTile *standTileStart;
struct ArenaStanPrefix { s32 stanfile; StandTile *ptr_firstroom; };
static struct ArenaStanPrefix prefix;
struct ArenaStanPrefix *stan_prefix = &prefix;
u8 list_of_tilesizes[16] = { [3] = 0x40 };
void getTileMidPoint(StandTile *tile, coord3d *out) {
    *out = (coord3d){0};
    for (s32 i = 0; i < 3; i++) {
        out->x += tile->points[i].x / 3.0f;
        out->y += tile->points[i].y / 3.0f;
        out->z += tile->points[i].z / 3.0f;
    }
}
void getTileEdgePoints(StandTile *tile, s32 e, coord3d *a, coord3d *b) {
    s32 next = (e + 1) % 3;
    *a = (coord3d){.x = tile->points[e].x, .y = tile->points[e].y, .z = tile->points[e].z};
    *b = (coord3d){.x = tile->points[next].x, .y = tile->points[next].y, .z = tile->points[next].z};
}
/* stan.c returns the tile plane's height; these fixture tiles are flat. A
 * bare StandTile (no sides) has no point storage behind it. */
f32 stanGetPositionYValue(StandTile *tile, f32 x, f32 z) {
    (void)x; (void)z;
    return (tile->tail.half >> 12) & 15 ? tile->points[0].y : 0.0f;
}
/* loadobjectmodel.c's own walk */
ObjectRecord *setupGetPtrToCommandByIndex(s32 index) {
    PropDefHeaderRecord *p = g_CurrentSetup.propDefs;
    for (s32 i = 0; index >= 0 && p && p->type != PROPDEF_END; i++, p = (void *)((u32 *)p + sizepropdef(p)))
        if (i == index) return (ObjectRecord *)p;
    return NULL;
}
static s32 blocking, observed_closed_doors;
s32 stanTestLineUnobstructed(StandTile **t, f32 x, f32 z, f32 xx, f32 zz, s32 mask,
                             f32 ymax, f32 ymin, f32 a, f32 b) {
    (void)t;(void)x;(void)z;(void)xx;(void)zz;(void)ymax;(void)ymin;(void)a;(void)b;
    if (mask & CDTYPE_CLOSEDDOORS) observed_closed_doors++;
    return !(blocking && (mask & CDTYPE_CLOSEDDOORS));
}
s32 walkTilesBetweenPoints_NoCallback(StandTile **t, f32 x, f32 z, f32 xx, f32 zz) {
    (void)t;(void)x;(void)z;(void)xx;(void)zz;return 1;
}
StandTile *stanFindTileBelowPos(coord3d *p, u8 *rooms, f32 *height) {
    (void)p;(void)rooms;(void)height;return prefix.ptr_firstroom;
}
extern void init_path_table_links(void);

static void check_graph(void)
{
    _Alignas(16) u8 floor[0x200] = {0};
    standTileStart = (void *)floor;
    StandTile *t[3];
    for (int i = 0; i < 3; i++) {
        t[i] = (void *)(floor + 0x80 + i * 0x40);
        *(u32 *)t[i] = i + 1; t[i]->tail.half = 0x3012;
        t[i]->points[0].x = i * 1000;
        t[i]->points[1].x = i * 1000 + 400;
        t[i]->points[2].x = i * 1000 + 200;
        t[i]->points[2].z = 300;
    }
    /* A reciprocal connection and a disconnected island. */
    t[0]->points[0].link = 0xc0 >> 3;
    t[1]->points[0].link = 0x80 >> 3;
    prefix.ptr_firstroom = t[0];
    PadRecord pads[4] = {0};
    for (int i = 0; i < 3; i++) { pads[i].plink = "pad"; pads[i].stan = t[i]; getTileMidPoint(t[i], &pads[i].pos); }
    PropDefHeaderRecord end = {.type = PROPDEF_END};
    mode = NET_MODE_HORDE;
    for (int map = 0; map < netStageCount(); map++) {
        if (!netStageUsesMpSetup(netStage(map)->level_id, mode)) continue;
        g_CurrentSetup = (struct stagesetup){.pads = pads, .propDefs = &end};
        gevrArenaPrepareSetup(netStage(map)->level_id);
        assert(g_CurrentSetup.pads != pads && g_CurrentSetup.pathwaypoints[4].padID == -1);
        init_path_table_links(); /* validates reciprocal edges, groups and sentinels */
        waypoint *ways = g_CurrentSetup.pathwaypoints, *route[8];
        assert(waypointFindRoute(&ways[0], &ways[1], route, 8) >= 3);
        assert(route[0] == &ways[0] && route[1] == &ways[3]); /* via shared edge */
        assert(waypointFindRoute(&ways[0], &ways[2], route, 8) < 2);
        assert(!g_CurrentSetup.padnames && !g_CurrentSetup.boundpadnames);
        s32 candidates[128]; assert(gevrArenaPads(candidates, 128) == 4);
        gevrBotNavForget(); blocking = 0;
        assert(gevrBotNavCanReach(t[0], &pads[0].pos, t[1], &pads[1].pos));
        assert(!gevrBotNavCanReach(t[0], &pads[0].pos, t[2], &pads[2].pos));
        blocking = 1;
        assert(!gevrBotNavCanReach(t[0], &pads[0].pos, t[1], &pads[1].pos));
        GevrBotRoute bot;
        assert(gevrBotNavPlan(&bot, t[0], &pads[0].pos, t[1], &pads[1].pos)); /* bots can open doors */
        clear_allocations();
    }
    assert(observed_closed_doors > 0);
    gevrBotNavForget(); prefix.ptr_firstroom = NULL;
    puts("PASS: MP-only Horde guard graphs validate and route through shared edges; disconnected floors and closed doors rejected");
}

static void check_campaign(void)
{
    PadRecord pads[33] = {0}; waypoint ways[33] = {0}; StandTile tile = {0};
    for (int i = 0; i < 32; i++) {
        pads[i].plink = "pad"; pads[i].stan = &tile; pads[i].pos.x = i * 1000;
        pads[i].up.y = pads[i].look.z = 1; ways[i].padID = i;
    }
    ways[32].padID = -1;
    /* Raw packed command stream: never let a host struct add padding between commands. */
    u8 original[sizeof(GuardRecord) + sizeof(ObjectRecord) + sizeof(PropDefHeaderRecord)] = {0};
    GuardRecord guard = {.type = PROPDEF_GUARD, .PadID = 0}; memcpy(original, &guard, sizeof(guard));
    ObjectRecord scenery = {.type = PROPDEF_PROP, .pad = 1}; memcpy(original + sizeof(guard), &scenery, sizeof(scenery));
    ((PropDefHeaderRecord *)(original + sizeof(guard) + sizeof(scenery)))->type = PROPDEF_END;
    SetupIntroEmpty intro = {.type = INTROTYPE_END}; AIListRecord ai = {0};
    for (int m = 0; m < netCoopMissionCount(); m++) {
        unsigned stage = netCoopMission(m)->level_id;
        assert(!netStageUsesMpSetup(stage, NET_MODE_HORDE));
        assert(!netStageUsesMpSetup(stage, NET_MODE_COOP));
        mode = NET_MODE_COOP;
        g_CurrentSetup = (struct stagesetup){.pads = pads, .pathwaypoints = ways, .propDefs = (void *)original, .intro = (void *)&intro, .ailists = &ai};
        gevrArenaPrepareSetup(stage);
        assert(g_CurrentSetup.propDefs == (void *)original && g_CurrentSetup.intro == (void *)&intro && g_CurrentSetup.ailists == &ai);
        mode = NET_MODE_DEATHMATCH;
        if (netStageHasMpSetup(stage)) continue; /* existing MP setups are preserved */
        gevrArenaPrepareSetup(stage);
        assert(g_CurrentSetup.pathwaypoints == ways && !g_CurrentSetup.ailists);
        assert(!memcmp(g_CurrentSetup.propDefs, original, sizeof(original) - sizeof(PropDefHeaderRecord)));
        SetupIntroSpawn *starts = (void *)g_CurrentSetup.intro;
        for (int i = 0; i < 8; i++) {
            assert(starts[i].type == INTROTYPE_SPAWN && starts[i].index >= 0 && starts[i].index < 32);
            for (int j = 0; j < i; j++) assert(starts[i].index != starts[j].index);
        }
        int guns = 0, ammo = 0, armour = 0, token = 0, command = 0;
        for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p->type != PROPDEF_END;
             p = (void *)((u32 *)p + sizepropdef(p)), command++) {
            if (command == 0) { assert(gevrArenaSkipProp(p)); continue; }
            assert(!gevrArenaSkipProp(p));
            if (p->type == PROPDEF_COLLECTABLE) {
                WeaponObjRecord *gun = (void *)p;
                if (gun->weaponnum == ITEM_TOKEN) { token++; assert(gun->obj == PROP_CHRBRIEFCASE); }
                else { assert((u8)gun->weaponnum == 0xf0 + guns % 8); guns++; }
            }
            if (p->type == PROPDEF_AMMO) { assert(((u32 *)p)[0]); ammo++; }
            if (p->type == PROPDEF_ARMOUR) armour++;
        }
        assert(guns == 16 && ammo == 16 && armour == 2 && token == 1 && command == 37);
        clear_allocations();
    }
    /* Preserve the original favourites and voting indices. */
    assert(netStageIndexOf(34) == 0 && netStageIndexOf(22) == 11 && netStageIndexOf(41) == 12);
    puts("PASS: all campaign maps select the correct setup; campaign Deathmatch generates distinct spawns, selected-set pickups, armour and Flag Tag; co-op unchanged");
}

/* Dab's Mod GoldenEye Arenas rules (geconvert.c): the level's own keys open
 * the doors they open, locks stay only on fall-away doors, safes are kept and
 * spawns stand on their floor. */
static void check_rules(void)
{
    static const u8 types[] = { PROPDEF_KEY, PROPDEF_DOOR, PROPDEF_DOOR, PROPDEF_DOOR, PROPDEF_DOOR,
                                PROPDEF_LOCK_DOOR, PROPDEF_LOCK_DOOR, PROPDEF_SAFE, PROPDEF_END };
    _Alignas(8) u8 stream[4096] = {0};
    size_t at[9], used = 0;
    for (int i = 0; i < 9; i++) {
        at[i] = used;
        ((PropDefHeaderRecord *)(stream + used))->type = types[i];
        used += (size_t)gevrSetupPropWords(types[i]) * 4;
        assert(used < sizeof(stream));
    }
    ((KeyRecord *)(stream + at[0]))->keyflags = 0x1;
    ((DoorRecord *)(stream + at[1]))->keyflags = 0x1;     /* the level's key opens it */
    ((DoorRecord *)(stream + at[2]))->keyflags = 0x80;    /* no key of the level: Runway's chemical plant */
    ((DoorRecord *)(stream + at[3]))->doorType = DOORTYPE_FALLAWAY; /* Surface's vent hatch */
    ((LockDoorRecord *)(stream + at[5]))->Index1 = 3 - 5; /* on the fall-away door */
    ((LockDoorRecord *)(stream + at[6]))->Index1 = 4 - 6; /* on an ordinary door */
    PadRecord pads[3] = {0}; StandTile tile = {0};
    for (int i = 0; i < 2; i++) { pads[i].plink = "pad"; pads[i].stan = &tile; pads[i].pos.x = i * 1000; }
    pads[1].pos.y = 345; /* Train's trackside pads, behind the crates */
    mode = NET_MODE_DEATHMATCH;
    for (int map = 0; map < netStageCount(); map++) {
        unsigned stage = netStage(map)->level_id;
        if (netStageHasMpSetup(stage)) continue;
        g_CurrentSetup = (struct stagesetup){.pads = pads, .propDefs = (void *)stream};
        gevrArenaPrepareSetup(stage);
        assert(gevrArenaDoorKeyOpens(0x1) && !gevrArenaDoorKeyOpens(0x80) && !gevrArenaDoorKeyOpens(0x81));
        assert(!gevrArenaDoorKeyOpens(0));
        PropDefHeaderRecord *p = g_CurrentSetup.propDefs;
        for (int i = 0; i < 8; i++, p = (void *)((u32 *)p + sizepropdef(p))) {
            assert(p->type == types[i]);
            const s32 skipped = gevrArenaSkipProp(p);
            assert(skipped == (i == 0 || i == 6));
        }
        SetupIntroSpawn *starts = (void *)g_CurrentSetup.intro;
        for (int i = 0; i < 8; i++) assert(starts[i].index == 0);
        clear_allocations();
    }
    puts("PASS: arena key rule, fall-away locks, safes and floor-standing spawns (Dab's Mod)");
}

int main(void) { setvbuf(stdout, NULL, _IONBF, 0); check_graph(); check_campaign(); check_rules(); }
