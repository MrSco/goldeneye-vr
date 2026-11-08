/* Adapt the cartridge's campaign/MP setups for the full online map catalogue.
 * Campaign arenas keep their scenery and floor; MP-only Horde arenas gain the
 * native guard waypoint/group graph from their linked floor tiles. No ROM data
 * is bundled or changed on disk. All peers derive the same setup at stage load. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <memp.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_arena.h"
#include "player.h"
#include "stan.h"
#include "loadobjectmodel.h"
#include "net_game.h"
#include "net_horde.h"
#include "system.h"
#include "boss.h"
#include "bondview.h"
#include "net_core.h"

struct ArenaStanPrefix { s32 stanfile; StandTile *ptr_firstroom; };
extern struct ArenaStanPrefix *stan_prefix;
extern u8 list_of_tilesizes[];
extern void getTileMidPoint(StandTile *tile, coord3d *out);
extern void getTileEdgePoints(StandTile *tile, s32 side, coord3d *a, coord3d *b);

static PropDefHeaderRecord *s_added_props;
static s32 s_campaign_deathmatch, s_solo_arena, s_stage, s_out_ticks;
static u32 s_level_keys;

static void *arenaAlloc(size_t size)
{
    void *p = mempAllocBytesInBank((u32)((size + 15) & ~(size_t)15), MEMPOOL_STAGE);
    memset(p, 0, size);
    return p;
}

s32 gevrArenaCampaignDeathmatch(s32 stage)
{
    return gevrMpRules() && netHordeStageValid((unsigned)stage) &&
        (!netStageHasMpSetup((unsigned)stage) || netRoundFullMap());
}

static s32 arenaPadCount(void)
{
    s32 count = 0;
    if (g_CurrentSetup.pads) while (g_CurrentSetup.pads[count].plink) count++;
    return count;
}

/* DAB geconvert.c flooredPads (5262): a pad stands on its floor, -30..+60
 * from the tile under it, in the setup's own units. Train's trackside pads
 * sit 345-370 above the ballast, behind the crates (Dab's netplay.md). Pads
 * and tiles here are in world units, the setup's over the level scale:
 * compared raw, Cradle's pads (29 setup units up, 123 world) all failed but
 * two, and Cradle (Full) started all eight players on one platform (#228). */
extern f32 level_scale;   /* stan.c */
static s32 arenaPadOnFloor(PadRecord *pad)
{
    f32 dy = (pad->pos.y - stanGetPositionYValue(pad->stan, pad->pos.x, pad->pos.z)) * level_scale;
    return dy >= -30.0f && dy <= 60.0f;
}

/* The rooms a player can stand in (user, 2026-10-24: Train's tracks run on
 * for a long way). Train: the carriages are rooms 1-51; 52 is the broken
 * carriage behind held door 347, the track and ballast 53-57. Spawns and
 * pickups stay inside (rc6: a spawn on the track, dead three seconds later),
 * and three seconds outside is a death of the player's own (gevrArenaTick). */
static const struct ArenaBounds { s16 stage; u8 first, last; } s_bounds[] = {
    { LEVELID_TRAIN, 1, 51 },
};

static const struct ArenaBounds *arenaBounds(void)
{
    for (u32 i = 0; s_solo_arena && i < sizeof(s_bounds) / sizeof(*s_bounds); i++)
        if (s_bounds[i].stage == s_stage) return &s_bounds[i];
    return NULL;
}

static s32 arenaBehindGlass(StandTile *tile);

static void arenaAddPad(s32 *out, s32 *count, s32 capacity, s32 pad, s32 pads)
{
    if (*count >= capacity || pad < 0 || pad >= pads || !g_CurrentSetup.pads[pad].stan) return;
    if (!arenaPadOnFloor(&g_CurrentSetup.pads[pad])) return;
    if (arenaBehindGlass(g_CurrentSetup.pads[pad].stan)) return;
    const struct ArenaBounds *bounds = arenaBounds();
    const u8 room = g_CurrentSetup.pads[pad].stan->room;
    if (bounds && (room < bounds->first || room > bounds->last)) return;
    for (s32 i = 0; i < *count; i++) if (out[i] == pad) return;
    out[(*count)++] = pad;
}

s32 gevrArenaPads(s32 *out, s32 capacity)
{
    s32 count = 0, pads = arenaPadCount();
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
         p = (void *)((u32 *)p + sizepropdef(p))) {
        if (p->type == PROPDEF_GUARD) {
            GuardRecord guard; memcpy(&guard, p, sizeof(guard)); /* word-aligned stream */
            arenaAddPad(out, &count, capacity, guard.PadID, pads);
        }
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
    /* Deathmatch's eight starts want eight places: a level with fewer guard and
     * path pads than that tops up from its other floor pads (Horde: only none) */
    const s32 want = s_campaign_deathmatch ? 8 : 1;
    for (s32 i = 0; count < want && i < pads; i++) arenaAddPad(out, &count, capacity, i, pads);
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

/* Floor that only glass leads to (user, 2026-11-07: an Archives (Full) start
 * in the courtyard the mission escapes to, behind the ground floor's windows,
 * in only once someone inside shoots the glass). The stan runs on under a
 * pane, so the floor graph is cut where a link crosses an upright pane that
 * reaches down to it, and whatever that cuts off from the rest of its floor
 * takes no starts or pickups. Floors the stan never joined (lifts) are left as
 * they are. gevrBotNavMainArea asks the same of the live collision, but the
 * starts are picked here, before any pane is placed, so this reads the panes'
 * bound pads instead. Replayed on the decomp's setups (2026-11-07), it moves a start
 * off Archives' courtyard (rooms 47/55/64, pad 172), Caverns' rooms 40-42
 * (pad 78), Dam's 106/109 (pad 17) and Aztec's 62-64 (pad 116); nothing on
 * the other campaign levels. */
struct ArenaPane { f32 ax, az, bx, bz, lo, hi; };
static StandTile **s_glassed;   /* the tiles behind glass, by address */
static s32 s_nglassed;

static s32 arenaRoot(s32 *parent, s32 i)
{
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

static void arenaJoin(s32 *parent, s32 a, s32 b)
{
    a = arenaRoot(parent, a); b = arenaRoot(parent, b);
    if (a != b) parent[a > b ? a : b] = a < b ? a : b;
}

static s32 arenaGlassPane(PropDefHeaderRecord *p, struct ArenaPane *out)
{
    if (p->type != PROPDEF_GLASS && p->type != PROPDEF_TINTED_GLASS) return FALSE;
    ObjectRecord obj; memcpy(&obj, p, sizeof(obj));
    if (obj.pad < 10000 || !g_CurrentSetup.boundpads) return FALSE; /* sized by its bound pad */
    BoundPadRecord *bp = g_CurrentSetup.boundpads;
    for (s32 i = 10000; i < obj.pad; i++, bp++) if (!bp->plink) return FALSE;
    if (!bp->plink) return FALSE;
    /* chrprop.c's bound-pad frame: x along up x look, y along up, z along look */
    const coord3d side = { .x = bp->up.y * bp->look.z - bp->look.y * bp->up.z,
                           .y = bp->up.z * bp->look.x - bp->look.z * bp->up.x,
                           .z = bp->up.x * bp->look.y - bp->look.x * bp->up.y };
    const coord3d *axis[3] = { &side, &bp->up, &bp->look };
    const f32 lo[3] = { bp->bbox.xmin, bp->bbox.ymin, bp->bbox.zmin };
    const f32 hi[3] = { bp->bbox.xmax, bp->bbox.ymax, bp->bbox.zmax };
    s32 thin = 0;
    for (s32 a = 1; a < 3; a++) if (hi[a] - lo[a] < hi[thin] - lo[thin]) thin = a;
    if (axis[thin]->y > .5f || axis[thin]->y < -.5f) return FALSE; /* a skylight or a glass floor */
    coord3d c[8];
    for (s32 k = 0; k < 8; k++) {
        const f32 u = (k & 1 ? hi : lo)[0], v = (k & 2 ? hi : lo)[1], w = (k & 4 ? hi : lo)[2];
        c[k].x = bp->pos.x + u * side.x + v * bp->up.x + w * bp->look.x;
        c[k].y = bp->pos.y + u * side.y + v * bp->up.y + w * bp->look.y;
        c[k].z = bp->pos.z + u * side.z + v * bp->up.z + w * bp->look.z;
    }
    f32 best = -1;
    out->lo = out->hi = c[0].y;
    for (s32 k = 0; k < 8; k++) {
        if (c[k].y < out->lo) out->lo = c[k].y;
        if (c[k].y > out->hi) out->hi = c[k].y;
        for (s32 m = k + 1; m < 8; m++) {
            const f32 dx = c[k].x - c[m].x, dz = c[k].z - c[m].z;
            if (dx * dx + dz * dz <= best) continue;
            best = dx * dx + dz * dz;
            out->ax = c[k].x; out->az = c[k].z; out->bx = c[m].x; out->bz = c[m].z;
        }
    }
    return TRUE;
}

static f32 arenaTurn(f32 ax, f32 az, f32 bx, f32 bz, f32 cx, f32 cz)
{
    return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
}

static s32 arenaPaneCuts(const struct ArenaPane *pane, const coord3d *a, const coord3d *b)
{
    const f32 floor = (a->y + b->y) * .5f;
    /* the pane comes down to the floor (within its own height: Archives'
     * windows stand 10-25 units over theirs) and the floor is not above it */
    if (floor > pane->hi || pane->lo - floor > pane->hi - pane->lo) return FALSE;
    return arenaTurn(a->x, a->z, b->x, b->z, pane->ax, pane->az) * arenaTurn(a->x, a->z, b->x, b->z, pane->bx, pane->bz) < 0
        && arenaTurn(pane->ax, pane->az, pane->bx, pane->bz, a->x, a->z) * arenaTurn(pane->ax, pane->az, pane->bx, pane->bz, b->x, b->z) < 0;
}

static void arenaGlassFloors(void)
{
    s_glassed = NULL; s_nglassed = 0;
    if (!stan_prefix || !stan_prefix->ptr_firstroom) return;
    s32 npanes = 0;
    struct ArenaPane scratch;
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
         p = (void *)((u32 *)p + sizepropdef(p))) npanes += arenaGlassPane(p, &scratch);
    if (!npanes) return;
    struct ArenaPane *panes = malloc(npanes * sizeof(*panes));
    if (!panes) return;
    npanes = 0;
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
         p = (void *)((u32 *)p + sizepropdef(p))) npanes += arenaGlassPane(p, &panes[npanes]);
    s32 nt = 0;
    for (StandTile *t = stan_prefix->ptr_firstroom; *(u32 *)t;
         t = (void *)((u8 *)t + list_of_tilesizes[arenaSides(t)])) nt++;
    /* working space off the stage pool (Dam: 2755 tiles); only the result stays */
    StandTile **tiles = nt ? malloc(nt * sizeof(*tiles)) : NULL;
    s32 *work = nt ? malloc(nt * 4 * sizeof(*work)) : NULL;
    coord3d *mid = nt ? malloc(nt * sizeof(*mid)) : NULL;
    if (!tiles || !work || !mid) { free(panes); free(tiles); free(work); free(mid); return; }
    s32 *whole = work, *cut = work + nt, *size = work + 2 * nt, *best = work + 3 * nt;
    s32 ti = 0;
    for (StandTile *t = stan_prefix->ptr_firstroom; *(u32 *)t;
         t = (void *)((u8 *)t + list_of_tilesizes[arenaSides(t)])) {
        getTileMidPoint(t, &mid[ti]);
        whole[ti] = cut[ti] = ti; size[ti] = 0; best[ti] = -1;
        tiles[ti++] = t;
    }
    s32 cuts = 0;
    for (s32 i = 0; i < nt; i++) for (s32 e = 0; e < arenaSides(tiles[i]); e++) {
        const s32 j = arenaTileIndex(tiles, nt, arenaLink(tiles[i], e));
        if (j <= i || !arenaReverseLink(tiles[i], tiles[j])) continue;
        arenaJoin(whole, i, j);
        s32 glassed = FALSE;
        for (s32 k = 0; k < npanes && !glassed; k++) glassed = arenaPaneCuts(&panes[k], &mid[i], &mid[j]);
        if (glassed) cuts++; else arenaJoin(cut, i, j);
    }
    /* each floor keeps its largest piece once the glass is in */
    for (s32 i = 0; i < nt; i++) size[arenaRoot(cut, i)]++;
    for (s32 i = 0; i < nt; i++) {
        const s32 w = arenaRoot(whole, i), c = arenaRoot(cut, i);
        if (best[w] < 0 || size[c] > size[best[w]]) best[w] = c;
    }
    s32 off = 0;
    for (s32 i = 0; i < nt; i++) off += arenaRoot(cut, i) != best[arenaRoot(whole, i)];
    if (off) {
        s_glassed = arenaAlloc(off * sizeof(*s_glassed));
        for (s32 i = 0; i < nt; i++)
            if (arenaRoot(cut, i) != best[arenaRoot(whole, i)]) s_glassed[s_nglassed++] = tiles[i];
    }
    sysLogPrintf(LOG_NOTE, "arena: %d panes, %d floor links under glass, %d of %d tiles behind it", npanes, cuts, off, nt);
    free(panes); free(tiles); free(work); free(mid);
}

static s32 arenaBehindGlass(StandTile *tile)
{
    return s_nglassed && arenaTileIndex(s_glassed, s_nglassed, tile) >= 0;
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
    sysLogPrintf(LOG_NOTE, "arena: %d spawn candidates, starts at pads %d %d %d %d %d %d %d %d", count,
                 picks[0], picks[1 % np], picks[2 % np], picks[3 % np], picks[4 % np], picks[5 % np], picks[6 % np],
                 picks[7 % np]);
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
    /* The stream is only word-aligned and these records hold pointers: each is
     * built aligned and copied in (PR #224; UBSan over the decomp's setups). */
    for (s32 i = 0; i < 16; i++) {
        WeaponObjRecord gun = {0};
        arenaObject((ObjectRecord *)&gun, PROPDEF_COLLECTABLE, PROP_CHRWPPK, picks[i % np]);
        gun.weaponnum = (s8)(0xf0 + i % 8); /* retail selected-weapon-set slots */
        memcpy(cursor, &gun, sizeof(gun)); cursor += sizeof(gun);
        MultiAmmoCrateRecord ammo = {0};
        arenaObject((ObjectRecord *)&ammo, PROPDEF_AMMO, PROP_AMMO_CRATE5, picks[(i + 16) % np]);
        for (s32 a = 0; a < AMMOTYPE_GLOBAL_MAX; a++) ammo.slots[a].modelnum = 0xffff;
        memcpy(cursor, &ammo, sizeof(ammo)); cursor += sizeof(ammo);
    }
    for (s32 i = 0; i < 2; i++) {
        BodyArmourRecord armour = {0};
        arenaObject((ObjectRecord *)&armour, PROPDEF_ARMOUR, PROP_BODYARMOUR, picks[(i + 24) % np]);
        /* prop.c converts the cartridge's fixed-point word at load. */
        u32 amount = 65535; memcpy(&armour.initialamount, &amount, sizeof(amount));
        memcpy(cursor, &armour, sizeof(armour)); cursor += sizeof(armour);
    }
    WeaponObjRecord token = {0};
    arenaObject((ObjectRecord *)&token, PROPDEF_COLLECTABLE, PROP_CHRBRIEFCASE, picks[(np - 1)]);
    token.weaponnum = ITEM_TOKEN; /* weaponAssignToHome loads it only for Flag Tag */
    memcpy(cursor, &token, sizeof(token)); cursor += sizeof(token);
    PropDefHeaderRecord last = {0};
    last.type = PROPDEF_END;
    memcpy(cursor, &last, sizeof(last));
    g_CurrentSetup.propDefs = (void *)defs;
}

/* DAB geconvert.c setupKeyFlags (4753): the keys the level's own setup
 * holds. A door those open is one the player gets through once the key is
 * found, and an arena has no keys, so it opens for everyone; a door no key of
 * the level opens stays shut, as GoldenEye keeps it (Runway's chemical plant
 * doors, Depot's upper doors, most of Train's). */
static void arenaLevelKeys(void)
{
    s_level_keys = 0;
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
         p = (void *)((u32 *)p + sizepropdef(p)))
        if (p->type == PROPDEF_KEY) {
            KeyRecord key; memcpy(&key, p, sizeof(key));
            s_level_keys |= key.keyflags;
        }
}

/* The locks the mission's scripts take off doors (chrai.c AI_DoorUnsetLock),
 * by the door's setup record: an arena runs no scripts, so these open from the
 * start as the keys' do. #226: Facility's bottling room, unlocked with the
 * door decoder (lock 8, tag 0x26), stayed shut, a start inside. Control's six
 * and Train's carriages are the others; doors the levels hold shut keep their
 * holds (s_door_holds). */
extern s32 chraiitemsize(u8 *AIList, s32 offset);   /* chrai.c */
static struct ArenaUnlock { s32 record; u32 bits; } s_unlocks[48];
static s32 s_nunlocks;

static void arenaScriptUnlocks(void)
{
    s_nunlocks = 0;
    for (AIListRecord *list = g_CurrentSetup.ailists; list && list->ailist; list++) {
        u8 *ai = (u8 *)list->ailist;
        for (s32 at = 0, steps = 0; ai[at] != AI_EndList && steps < 8192; steps++) {
            if (ai[at] == AI_DoorUnsetLock && s_nunlocks < (s32)(sizeof(s_unlocks) / sizeof(*s_unlocks))) {
                /* the door its tag names: the tag's record plus its offset */
                s32 index = 0;
                for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
                     p = (void *)((u32 *)p + sizepropdef(p)), index++) {
                    if (p->type != PROPDEF_TAG) continue;
                    TagObjectRecord tag; memcpy(&tag, p, sizeof(tag));
                    if (tag.ID != ai[at + 1]) continue;
                    s_unlocks[s_nunlocks].record = index + tag.OffsetToObj;
                    s_unlocks[s_nunlocks++].bits = ai[at + 2];
                    break;
                }
            }
            at += chraiitemsize(ai, at);
        }
    }
}

s32 gevrArenaDoorKeyOpens(s32 record, u32 keyflags)
{
    u32 opened = s_level_keys;
    for (s32 i = 0; i < s_nunlocks; i++)
        if (s_unlocks[i].record == record) opened |= s_unlocks[i].bits;
    return keyflags && (keyflags & opened) == keyflags;
}

/* DAB geconvert.c lockOnFallawayCarried (4785): a fall-away door free of
 * locks falls away at once, so its locks stay (Surface's vent hatches, Train's
 * floor panel); a lock on any other door is left out, as Dab's arenas do. */
static s32 arenaLockOnFallaway(PropDefHeaderRecord *prop)
{
    s32 index = 0;
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p != prop; index++) {
        if (p->type == PROPDEF_END) return FALSE;
        p = (void *)((u32 *)p + sizepropdef(p));
    }
    LockDoorRecord lock; memcpy(&lock, prop, sizeof(lock));
    void *target = setupGetPtrToCommandByIndex(index + lock.Index1);
    if (!target || ((PropDefHeaderRecord *)target)->type != PROPDEF_DOOR) return FALSE;
    DoorRecord door; memcpy(&door, target, sizeof(door));
    return door.doorType == DOORTYPE_FALLAWAY;
}

s32 gevrArenaSkipProp(PropDefHeaderRecord *prop)
{
    if (!s_campaign_deathmatch || !s_added_props || (uintptr_t)prop >= (uintptr_t)s_added_props) return FALSE;
    switch (prop->type) {
    case PROPDEF_DOOR: case PROPDEF_DOOR_SCALE: case PROPDEF_PROP:
    case PROPDEF_GLASS: case PROPDEF_TINTED_GLASS: return FALSE;
    /* DAB g_Carry (geconvert.c 4604): a safe is scenery; its door hangs on it
     * (Depot's floating safe door) */
    case PROPDEF_SAFE: return FALSE;
    case PROPDEF_LOCK_DOOR: return !arenaLockOnFallaway(prop);
    default: return TRUE; /* campaign actors, inventory, objectives and script links */
    }
}

/* What the mission's scripts did and an arena does not run (user, 2026-10-24,
 * from a v0.5.14 Deathmatch test). Records are the solo setup's command
 * indices (goldeneye-decomp Usetup*Z.c). Doors only an outro or a wall
 * console opens, onto space the level never built or a dead end, stay shut;
 * Dam's truck gates, worked from consoles in the mission, stand open. A
 * one-way door into a room with no other way out works both ways (user,
 * 2026-10-25, rc6: locked in Train's last carriage); in Deathmatch every
 * one-way door does (2026-10-27: the "(Full)" maps add Facility's four and
 * Bunker II's fourteen to Bunker I's, Silo's, Train's and Egypt's). */
enum { ARENA_SHUT, ARENA_OPEN, ARENA_BOTH_WAYS };
static const struct ArenaDoorHold { s16 stage, record; u8 hold; } s_door_holds[] = {
    { LEVELID_DAM, 267, ARENA_OPEN }, { LEVELID_DAM, 268, ARENA_OPEN }, /* truck gates, an airlock pair */
    { LEVELID_SURFACE2, 78, ARENA_SHUT },                      /* outro: tunnel to locked blast doors */
    { LEVELID_BUNKER1, 116, ARENA_SHUT },                      /* outro */
    { LEVELID_SILO, 116, ARENA_SHUT }, { LEVELID_SILO, 117, ARENA_SHUT },       /* outro lift */
    { LEVELID_DEPOT, 184, ARENA_SHUT }, { LEVELID_DEPOT, 185, ARENA_SHUT },     /* outro: train's outer doors */
    { LEVELID_JUNGLE, 565, ARENA_SHUT }, { LEVELID_JUNGLE, 566, ARENA_SHUT },   /* outro lift */
    { LEVELID_CONTROL, 174, ARENA_SHUT }, { LEVELID_CONTROL, 175, ARENA_SHUT }, /* outro lift */
    { LEVELID_TRAIN, 346, ARENA_BOTH_WAYS },                   /* into the last carriage (Xenia, Trevelyan) */
    { LEVELID_TRAIN, 347, ARENA_SHUT },                        /* on to the broken carriage, past roller 356 */
};

/* Called before setupDoor, whose doorInit opens a KEEPOPEN door at once. */
void gevrArenaDoorRules(s32 stage, s32 record, DoorRecord *door)
{
    if (!s_solo_arena) return;
    if (s_campaign_deathmatch) door->flags2 &= ~(PROPFLAG2_08000000 | PROPFLAG2_10000000);
    for (u32 i = 0; i < sizeof(s_door_holds) / sizeof(*s_door_holds); i++) {
        if (s_door_holds[i].stage != stage || s_door_holds[i].record != record) continue;
        if (s_door_holds[i].hold == ARENA_BOTH_WAYS) {
            door->flags2 &= ~(PROPFLAG2_08000000 | PROPFLAG2_10000000); /* one-way locks, front and back */
            continue;
        }
        door->flags |= PROPFLAG_CANNOT_ACTIVATE;
        if (s_door_holds[i].hold == ARENA_OPEN) {
            door->flags |= PROPFLAG_DOOR_KEEPOPEN;
            door->flags2 &= ~PROPFLAG2_40000000; /* the airlock: opening one shuts its partner */
        } else {
            door->flags &= ~PROPFLAG_DOOR_KEEPOPEN;
        }
    }
}

#define ARENA_OUT_TICKS 180

void gevrArenaTick(s32 ticks)
{
    if (!s_campaign_deathmatch || ticks <= 0 || bossGetStageNum() != s_stage) return;
    const struct ArenaBounds *bounds = arenaBounds();
    const s32 me = netIsActive() ? netGetLocalSlot() : 0;
    struct player *pl = bounds && me >= 0 && me < getPlayerCount() ? g_playerPointers[me] : NULL;
    if (!pl || !pl->prop || !pl->prop->stan || pl->bonddead || netPlayerIsSpectator(me)) {
        s_out_ticks = 0;
        return;
    }
    const u8 room = pl->prop->stan->room;
    if (room >= bounds->first && room <= bounds->last) {
        s_out_ticks = 0;
        return;
    }
    if ((s_out_ticks += ticks) < ARENA_OUT_TICKS) return;
    s_out_ticks = 0;
    sysLogPrintf(LOG_NOTE, "arena: player %d out of bounds in room %d", me, room);
    const s32 prev = get_cur_playernum();
    set_cur_player(me);
    record_damage_kills(100.0f, 0, 0, -1, TRUE); /* no attacker: counted as the player's own */
    set_cur_player(prev);
}

/* The level's gas-releasing objects (Facility's bottling tanks and gas barrels)
 * are scenery in an arena. Dropped with the mission's other actors, the tanks
 * left their footprints solid and nothing drawn there (#226: a Facility (Full)
 * start among them, the player shut in); kept as they are, one shot would gas
 * the map. As plain props, records of the same 32 words, they stand, block and
 * break like any other. */
static void arenaGasAsScenery(void)
{
    for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p && p->type != PROPDEF_END;
         p = (void *)((u32 *)p + sizepropdef(p)))
        if (p->type == PROPDEF_GAS_RELEASING) p->type = PROPDEF_PROP;
}

void gevrArenaPrepareSetup(s32 stage)
{
    s_added_props = NULL;
    s_campaign_deathmatch = gevrArenaCampaignDeathmatch(stage);
    s_solo_arena = s_campaign_deathmatch || (netHordeActive() && !netStageUsesMpSetup((unsigned)stage, NET_MODE_HORDE));
    s_stage = stage;
    s_out_ticks = 0;
    s_level_keys = 0;
    s_nunlocks = 0;
    s_glassed = NULL; s_nglassed = 0;
    if (!netHordeActive() && !s_campaign_deathmatch) return;
    arenaGlassFloors();     /* before the starts and Horde's pads take floor pads */
    if (s_solo_arena) arenaGasAsScenery();
    arenaLevelKeys();
    arenaScriptUnlocks();   /* before the scripts go, below */
    if (netHordeActive()) arenaBuildGuardPaths();
    if (s_campaign_deathmatch) arenaDeathmatchSetup();
    /* Global guard AI lists remain available; mission/background scripts cannot
     * start a cutscene, countdown or mission completion in an arena. */
    g_CurrentSetup.ailists = NULL;
}
