/* Adapt the cartridge's campaign/MP setups for the full online map catalogue.
 * Campaign arenas keep their scenery and floor; MP-only Horde arenas gain the
 * native guard waypoint/group graph from their linked floor tiles. No ROM data
 * is bundled or changed on disk. All peers derive the same setup at stage load. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <memp.h>
#include <string.h>
#include "gevr_arena.h"
#include "player.h"
#include "stan.h"
#include "loadobjectmodel.h"
#include "net_game.h"
#include "net_horde.h"
#include "system.h"

struct ArenaStanPrefix { s32 stanfile; StandTile *ptr_firstroom; };
extern struct ArenaStanPrefix *stan_prefix;
extern u8 list_of_tilesizes[];
extern void getTileMidPoint(StandTile *tile, coord3d *out);
extern void getTileEdgePoints(StandTile *tile, s32 side, coord3d *a, coord3d *b);

static PropDefHeaderRecord *s_added_props;
static s32 s_campaign_deathmatch;

static void *arenaAlloc(size_t size)
{
    void *p = mempAllocBytesInBank((u32)((size + 15) & ~(size_t)15), MEMPOOL_STAGE);
    memset(p, 0, size);
    return p;
}

s32 gevrArenaCampaignDeathmatch(s32 stage)
{
    return gevrMpRules() && netHordeStageValid((unsigned)stage) && !netStageHasMpSetup((unsigned)stage);
}

static s32 arenaPadCount(void)
{
    s32 count = 0;
    if (g_CurrentSetup.pads) while (g_CurrentSetup.pads[count].plink) count++;
    return count;
}

static void arenaAddPad(s32 *out, s32 *count, s32 capacity, s32 pad, s32 pads)
{
    if (*count >= capacity || pad < 0 || pad >= pads || !g_CurrentSetup.pads[pad].stan) return;
    for (s32 i = 0; i < *count; i++) if (out[i] == pad) return;
    out[(*count)++] = pad;
}

s32 gevrArenaPads(s32 *out, s32 capacity)
{
    s32 count = 0, pads = arenaPadCount();
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
         p = (void *)((u32 *)p + sizepropdef(p))) {
        if (p->type == PROPDEF_GUARD) arenaAddPad(out, &count, capacity, ((GuardRecord *)p)->PadID, pads);
    }
    /* Keep guard pads first. Sample paths across the entire map, rather than
     * filling the bounded list with the first corridor of a large mission. */
    waypoint *ways = g_CurrentSetup.pathwaypoints;
    s32 nw = 0;
    if (ways) while (ways[nw].padID >= 0) nw++;
    s32 slots = capacity - count;
    if (slots > nw) slots = nw;
    for (s32 i = 0; i < slots; i++)
        arenaAddPad(out, &count, capacity, ways[(i * nw) / slots].padID, pads);
    if (!count) {
        for (s32 i = 0; i < pads; i++) arenaAddPad(out, &count, capacity, i, pads);
    }
    return count;
}

static s32 arenaTileIndex(StandTile **tiles, s32 count, StandTile *tile)
{
    s32 lo = 0, hi = count - 1;
    while (lo <= hi) {
        s32 mid = (lo + hi) / 2;
        if (tiles[mid] == tile) return mid;
        if ((uintptr_t)tiles[mid] < (uintptr_t)tile) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static s32 arenaSides(StandTile *tile) { return (tile->tail.half >> 12) & 15; }
static StandTile *arenaLink(StandTile *tile, s32 side)
{
    u16 link = tile->points[side].link;
    return (link >> 4) ? (StandTile *)((u8 *)standTileStart + ((u32)link << 3)) : NULL;
}

static s32 arenaReverseLink(StandTile *a, StandTile *b)
{
    for (s32 e = 0; e < arenaSides(b); e++) if (arenaLink(b, e) == a) return TRUE;
    return FALSE;
}

/* A tile centre -> shared edge -> tile centre path stays on the floor even
 * when a direct line between the centres cuts outside two joined polygons.
 * One waypoint per group lets the game's unchanged guard router use this
 * graph, including stairs, without inventing long straight links through walls. */
static void arenaBuildGuardPaths(void)
{
    if (g_CurrentSetup.pathwaypoints && g_CurrentSetup.pathwaypoints->padID >= 0) return;
    if (!stan_prefix || !stan_prefix->ptr_firstroom) return;
    s32 nt = 0, ns = 0;
    for (StandTile *t = stan_prefix->ptr_firstroom; *(u32 *)t;
         t = (void *)((u8 *)t + list_of_tilesizes[arenaSides(t)])) {
        nt++; ns += arenaSides(t);
    }
    if (!nt) return;
    StandTile **tiles = arenaAlloc(nt * sizeof(*tiles));
    s32 ti = 0;
    for (StandTile *t = stan_prefix->ptr_firstroom; *(u32 *)t;
         t = (void *)((u8 *)t + list_of_tilesizes[arenaSides(t)])) tiles[ti++] = t;
    /* A tile has a neighbour slot per side plus the -1 end; a malformed stan
     * linking one tile more often than it has sides must not overrun it. */
    s32 *counts = arenaAlloc((nt + ns) * sizeof(*counts));
    s32 edges = 0;
    for (s32 i = 0; i < nt; i++) for (s32 e = 0; e < arenaSides(tiles[i]); e++) {
        s32 j = arenaTileIndex(tiles, nt, arenaLink(tiles[i], e));
        if (j > i && arenaReverseLink(tiles[i], tiles[j])
            && counts[i] < arenaSides(tiles[i]) && counts[j] < arenaSides(tiles[j])) {
            counts[i]++; counts[j]++; edges++;
        }
    }
    memset(counts, 0, nt * sizeof(*counts));
    s32 oldpads = arenaPadCount(), nodes = nt + edges;
    /* Pads >= 10000 are the engine's bound-pad encoding. */
    if (oldpads + nodes >= 10000) sysFatalError("arena: guard path exceeds ordinary pad IDs");
    PadRecord *pads = arenaAlloc((oldpads + nodes + 1) * sizeof(*pads));
    if (oldpads) memcpy(pads, g_CurrentSetup.pads, oldpads * sizeof(*pads));
    waypoint *ways = arenaAlloc((nodes + 1) * sizeof(*ways));
    waygroup *groups = arenaAlloc((nodes + 1) * sizeof(*groups));
    s32 *members = arenaAlloc(nodes * 2 * sizeof(*members));
    s32 *links = arenaAlloc((ns + nt + edges * 3) * sizeof(*links));
    s32 offset = 0;
    for (s32 i = 0; i < nodes; i++) {
        ways[i].padID = oldpads + i;
        ways[i].groupNum = i;
        ways[i].neighbours = &links[offset];
        offset += i < nt ? arenaSides(tiles[i]) + 1 : 3;
        groups[i].neighbours = ways[i].neighbours;
        groups[i].waypoints = &members[i * 2];
        members[i * 2] = i; members[i * 2 + 1] = -1;
        pads[oldpads + i].plink = "arena";
        pads[oldpads + i].up.y = 1;
        pads[oldpads + i].look.z = 1;
        if (i < nt) {
            getTileMidPoint(tiles[i], &pads[oldpads + i].pos);
            pads[oldpads + i].stan = tiles[i];
        }
    }
    s32 next = nt;
    for (s32 i = 0; i < nt; i++) for (s32 e = 0; e < arenaSides(tiles[i]); e++) {
        s32 j = arenaTileIndex(tiles, nt, arenaLink(tiles[i], e));
        if (j <= i || !arenaReverseLink(tiles[i], tiles[j])
            || counts[i] >= arenaSides(tiles[i]) || counts[j] >= arenaSides(tiles[j])) continue;
        coord3d a, b;
        getTileEdgePoints(tiles[i], e, &a, &b);
        PadRecord *pad = &pads[oldpads + next];
        pad->pos = (coord3d){ .x = (a.x + b.x) * .5f, .y = (a.y + b.y) * .5f, .z = (a.z + b.z) * .5f };
        pad->stan = tiles[i];
        ways[i].neighbours[counts[i]++] = next;
        ways[j].neighbours[counts[j]++] = next;
        ways[next].neighbours[counts[next]++] = i;
        ways[next].neighbours[counts[next]++] = j;
        next++;
    }
    for (s32 i = 0; i < nodes; i++) ways[i].neighbours[counts[i]] = -1;
    ways[nodes].padID = -1;
    g_CurrentSetup.pads = pads;
    g_CurrentSetup.pathwaypoints = ways;
    g_CurrentSetup.waypointgroups = groups;
    /* The ROM's optional names do not cover the appended pads/groups. */
    g_CurrentSetup.padnames = NULL; g_CurrentSetup.boundpadnames = NULL;
    sysLogPrintf(LOG_NOTE, "arena: built guard paths, %d floor tiles, %d links", nt, edges);
}

static void arenaObject(ObjectRecord *obj, s32 type, s32 model, s32 pad)
{
    obj->type = type; obj->extrascale = 256; obj->obj = model; obj->pad = pad;
    obj->maxdamage = 1000;
}

static void arenaDeathmatchSetup(void)
{
    s32 candidates[128], count = gevrArenaPads(candidates, 128);
    if (!count) sysFatalError("arena: campaign has no spawn pads");
    /* Farthest-point sampling spreads spawns and equipment over the mission. */
    s32 picks[32], np = count < 32 ? count : 32;
    picks[0] = candidates[0];
    for (s32 n = 1; n < np; n++) {
        f32 best = -1;
        s32 chosen = candidates[0];
        for (s32 i = 0; i < count; i++) {
            f32 near = 1e30f;
            for (s32 k = 0; k < n; k++) {
                coord3d *a = &g_CurrentSetup.pads[candidates[i]].pos, *b = &g_CurrentSetup.pads[picks[k]].pos;
                f32 dx = a->x-b->x, dy = a->y-b->y, dz = a->z-b->z, d = dx*dx+dy*dy+dz*dz;
                if (d < near) near = d;
            }
            if (near > best) { best = near; chosen = candidates[i]; }
        }
        picks[n] = chosen;
    }
    struct ArenaIntro { SetupIntroSpawn spawn[8]; struct SetupIntroItem item; SetupIntroEmpty end; };
    struct ArenaIntro *intro = arenaAlloc(sizeof(*intro));
    for (s32 i = 0; i < 8; i++) {
        intro->spawn[i].type = INTROTYPE_SPAWN; intro->spawn[i].index = picks[i % np];
    }
    intro->item.type = INTROTYPE_ITEM; intro->item.item_right = ITEM_FIST;
    intro->end.type = INTROTYPE_END;
    g_CurrentSetup.intro = (void *)intro;

    PropDefHeaderRecord *end = g_CurrentSetup.propDefs;
    while (end && end->type != PROPDEF_END) end = (void *)((u32 *)end + sizepropdef(end));
    size_t original = end ? (u8 *)end - (u8 *)g_CurrentSetup.propDefs : 0;
    size_t extra = 16 * (sizeof(WeaponObjRecord) + sizeof(MultiAmmoCrateRecord))
        + 2 * sizeof(BodyArmourRecord) + sizeof(WeaponObjRecord) + sizeof(PropDefHeaderRecord);
    u8 *defs = arenaAlloc(original + extra);
    if (original) memcpy(defs, g_CurrentSetup.propDefs, original);
    u8 *cursor = defs + original;
    s_added_props = (void *)cursor;
    for (s32 i = 0; i < 16; i++) {
        WeaponObjRecord *gun = (void *)cursor; cursor += sizeof(*gun);
        arenaObject((ObjectRecord *)gun, PROPDEF_COLLECTABLE, PROP_CHRWPPK, picks[i % np]);
        gun->weaponnum = (s8)(0xf0 + i % 8); /* retail selected-weapon-set slots */
        MultiAmmoCrateRecord *ammo = (void *)cursor; cursor += sizeof(*ammo);
        arenaObject((ObjectRecord *)ammo, PROPDEF_AMMO, PROP_AMMO_CRATE5, picks[(i + 16) % np]);
        for (s32 a = 0; a < AMMOTYPE_GLOBAL_MAX; a++) ammo->slots[a].modelnum = 0xffff;
    }
    for (s32 i = 0; i < 2; i++) {
        BodyArmourRecord *armour = (void *)cursor; cursor += sizeof(*armour);
        arenaObject((ObjectRecord *)armour, PROPDEF_ARMOUR, PROP_BODYARMOUR, picks[(i + 24) % np]);
        /* prop.c converts the cartridge's fixed-point word at load. */
        u32 amount = 65535; memcpy(&armour->initialamount, &amount, sizeof(amount));
    }
    WeaponObjRecord *token = (void *)cursor; cursor += sizeof(*token);
    arenaObject((ObjectRecord *)token, PROPDEF_COLLECTABLE, PROP_CHRBRIEFCASE, picks[(np - 1)]);
    token->weaponnum = ITEM_TOKEN; /* weaponAssignToHome loads it only for Flag Tag */
    ((PropDefHeaderRecord *)cursor)->type = PROPDEF_END;
    g_CurrentSetup.propDefs = (void *)defs;
}

s32 gevrArenaSkipProp(PropDefHeaderRecord *prop)
{
    if (!s_campaign_deathmatch || !s_added_props || (uintptr_t)prop >= (uintptr_t)s_added_props) return FALSE;
    switch (prop->type) {
    case PROPDEF_DOOR: case PROPDEF_DOOR_SCALE: case PROPDEF_PROP:
    case PROPDEF_GLASS: case PROPDEF_TINTED_GLASS: return FALSE;
    default: return TRUE; /* campaign actors, inventory, objectives and script links */
    }
}

void gevrArenaPrepareSetup(s32 stage)
{
    s_added_props = NULL;
    s_campaign_deathmatch = gevrArenaCampaignDeathmatch(stage);
    if (!netHordeActive() && !s_campaign_deathmatch) return;
    if (netHordeActive()) arenaBuildGuardPaths();
    if (s_campaign_deathmatch) arenaDeathmatchSetup();
    /* Global guard AI lists remain available; mission/background scripts cannot
     * start a cutscene, countdown or mission completion in an arena. */
    g_CurrentSetup.ailists = NULL;
}
