/* Production floor traversal, scenery collision and spawn solver. Only loaded
 * model bounds and room membership are supplied by the fixture. */
#include <ultra64.h>
#include <bondtypes.h>
#include "game/stan.h"
#include "game/chrai.h"
#include "game/chr.h"
#include "aicommands2.h"
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include "gevr_stage.h"

float level_scale, inv_level_scale;
StandTile *standTileStart, *stanTileEnd, *stanSavedColl_tile;
s32 stanSavedColl_pointI;
static s32 gevrStanWalkReports;
static u32 gevrStanWalkLink;
static StandTile *gevrStanWalkLinked;
static int obstacle;
static float ox, oz, oradius;
static float obstacle_top = 180, obstacle_bottom = 0;
static int wall;
static float wx, wz, wradius;
PropRecord g_Props[6], *stanSavedColl_posData;
static ObjectRecord objects[2];
static s16 props[7] = {-1, -1, -1, -1, -1, -1, -1};
s16 *ptr_list_object_lookup_indices = props;
s32 D_800413BC;
coord2d stanSavedColl_pntA, stanSavedColl_pntB;
f32 stanSavedColl_someMin;
#define STAN_LOCUS_TILESTACK_MAX 55
#define LOG_NOTE 1
#define LOG_ERROR 3
void sysLogPrintf(s32 level, const char *fmt, ...) {}
struct player {
    PropRecord *prop;
    rect4f collision_bounds;
    s32 field_AC, bonddead, cameramode;
    f32 field_70;
};
static struct player players[4], *g_playerPointers[4], *g_CurrentPlayer;
static int viewer_room[4], viewer_occupied[4], online = 1;
static bool netIsActive(void) { return online; }
static bool netSlotOccupied(int slot) { return slot >= 0 && slot < 4 && viewer_occupied[slot]; }
static s32 getPlayerPointerIndex(PropRecord *prop) { return prop - g_Props - 2; }
static s32 getPlayerCount(void) { return 4; }
static f32 bondviewGetPlayerDuckingHeightRelated(struct player *player) { return 170; }
static s32 propDoorGetCdTypes(PropRecord *prop) { return CDTYPE_DOORS; }
/* INSERT_PLAYER_FILTER */
/* INSERT_PLAYER_BOUNDS */
/* INSERT_PLAYER_RULE */
static s32 gevrNetInsideDoorProp(PropRecord *p, rect4f *r, s32 n, f32 x, f32 z) { return 0; }
static s32 gevrInsideObjProp(PropRecord *p, rect4f *r, s32 n, f32 x, f32 z) { return 0; }
void roomGetProps(s32 *rooms) {
    int n = 0;
    if (obstacle) props[n++] = 0;
    if (wall) props[n++] = 1;
    for (int slot = 0; slot < 4; slot++) if (viewer_room[slot]) props[n++] = slot + 2;
    props[n] = -1;
}
void chraiGetCollisionBounds(PropRecord *p, rect4f **polygon, s32 *vertices, f32 *top, f32 *bottom)
{
    static rect4f rect;
    if (p->type == PROP_TYPE_VIEWER)
    {
        *polygon = NULL; *vertices = 0;
        bondviewGetPropHeightRelatedValues(p, polygon, vertices, top, bottom);
        return;
    }
    bool first = p == &g_Props[0];
    float x = first ? ox : wx, z = first ? oz : wz, r = first ? oradius : wradius;
    rect.points[0] = (coord2d){{x-r, z-r}};
    rect.points[1] = (coord2d){{x-r, z+r}};
    rect.points[2] = (coord2d){{x+r, z+r}};
    rect.points[3] = (coord2d){{x+r, z-r}};
    *polygon = &rect; *vertices = (p->obj->state & PROPSTATE_20) ? 0 : 4;
    *top = first ? obstacle_top : 180; *bottom = first ? obstacle_bottom : 0;
}
void chrSetMoving(ChrRecord *chr, s32 flag) {}
void bondviewUpdateGuardTankFlagsRelated(PropRecord *prop, s32 flag) {}
/* INSERT_TOGGLE */

int getRotationalDirectionBetween(f32, f32, f32, f32);
/* INSERT_INTERSECTION */
/* INSERT_POLYGON */

/* INSERT_GEOMETRY */
/* INSERT_GROUND */
/* stan.c stanFindGroundAtCyl's centre test over the fixture's tiles in a row
 * (it has no room bounds table): the highest floor under the point, below it. */
static u8 fixtureTileSizes[] = {0x20,0x20,0x20,0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x00};
static StandTile *fixtureFirstTile;
StandTile *stanFindGroundAtCyl(coord3d *pos, f32 radius, u8 *rooms, StandTile *prefer, f32 *groundy, s32 *incentre)
{
    StandTile *best = NULL;
    f32 besty = -3.4028235e38f;
    f32 sx = pos->x * level_scale, sz = pos->z * level_scale;
    (void)radius; (void)rooms; (void)prefer;
    for (StandTile *tile = fixtureFirstTile; tile && tile < stanTileEnd && *(u32 *)tile != 0;
         tile = (StandTile *)((u8 *)tile + fixtureTileSizes[(tile->tail.half >> 12) & 0xf]))
    {
        s32 n = (tile->tail.half >> 12) & 0xf;
        if (n < 3 || stanTileHasZeroArea(tile) || !gevrStanPointInTile(tile, n, sx, sz)) continue;
        f32 y = gevrStanFloorAt(tile, n, pos->x, pos->z);
        if (y < pos->y && y > besty) { besty = y; best = tile; }
    }
    if (groundy) *groundy = best ? besty : -3.4028235e38f;
    if (incentre) *incentre = best != NULL;
    return best;
}
/* gevrSpawnSafeSpot's pads in the room: the fixture has none */
stagesetup g_CurrentSetup;
/* INSERT_AI_SIZE */
s32 auditCollectedOpcode(void) { return AI_IFBondCollectedObject; }

/* INSERT_SOLVER */
/* INSERT_START */
#ifdef AUDIT_SPAWN_PATH
s32 auditPath(coord3d *origin, coord3d *candidate, StandTile **tile)
{
    return gevrCoopSpawnPath(origin, candidate, tile);
}
#endif
void auditStart(coord3d *pos, StandTile **stan, coord3d *look, s32 slot)
{
    PadRecord pad = {0};
    pad.pos = *pos; pad.look = *look; pad.stan = *stan;
    gevrCoopStartSpot(pos, stan, &pad, slot);
}

/* Converted stan tiles stay at their original relative offsets. */
void auditGeometry(void *buffer, float scale, size_t size)
{
    struct gevrStanPrefix *prefix = buffer;
    level_scale = scale;
    inv_level_scale = 1.0f / scale;
    standTileStart = (StandTile *)((u8 *)prefix->firstroom - 128);
    fixtureFirstTile = prefix->firstroom;
    stanTileEnd = (StandTile *)((u8 *)buffer + size - 8);
}
void auditObstacle(int on, float x, float z, float radius)
{
    obstacle = on; ox = x; oz = z; oradius = radius;
    g_Props[0].type = PROP_TYPE_OBJ;
    g_Props[0].obj = &objects[0];
    objects[0].state = 0;
}
void auditObstacleHeight(float bottom, float top)
{
    obstacle_bottom = bottom; obstacle_top = top;
}
void auditWall(int on, float x, float z, float radius)
{
    wall = on; wx = x; wz = z; wradius = radius;
    g_Props[1].type = PROP_TYPE_DOOR;
    g_Props[1].obj = &objects[1];
    objects[1].state = 0;
}
void auditViewer(s32 slot, s32 in_room, s32 occupied, s32 poison)
{
    struct player *player = &players[slot];
    PropRecord *prop = &g_Props[slot + 2];
    viewer_room[slot] = in_room;
    viewer_occupied[slot] = occupied;
    g_playerPointers[slot] = player;
    player->prop = prop; prop->type = PROP_TYPE_VIEWER;
    player->field_AC = 1; player->cameramode = 0; player->bonddead = 0; player->field_70 = 0;
    /* A stale footprint whose edge lies inside the player's volume. */
    player->collision_bounds.points[0] = (coord2d){{0,-100}};
    player->collision_bounds.points[1] = (coord2d){{0,100}};
    player->collision_bounds.points[2] = (coord2d){{100,100}};
    player->collision_bounds.points[3] = (coord2d){{100,-100}};
    if (poison) memset(&player->collision_bounds, 0xd7, sizeof(player->collision_bounds));
    g_CurrentPlayer = &players[0];
    players[0].prop = &g_Props[2];
}
s32 auditViewerBounds(s32 slot)
{
    rect4f *polygon = NULL;
    s32 edges = -1;
    f32 top = 0, bottom = 0;
    bondviewGetPropHeightRelatedValues(&g_Props[slot + 2], &polygon, &edges, &top, &bottom);
    return edges;
}
void auditViewerState(s32 slot, s32 enabled, s32 cinema, s32 dead)
{
    players[slot].field_AC = enabled;
    players[slot].cameramode = cinema;
    players[slot].bonddead = dead;
}
s32 auditMoveVolume(coord3d *pos, StandTile *tile)
{
    return stanTestVolume(&tile, pos->x, pos->z, 30, CDTYPE_PLAYERS | CDTYPE_OBJS | CDTYPE_DOORS, 180, 30);
}
s32 auditMoveLine(coord3d *from, coord3d *to, StandTile *tile)
{
    return stanTestLineUnobstructed(&tile, from->x, from->z, to->x, to->z,
        CDTYPE_PLAYERS | CDTYPE_OBJS | CDTYPE_DOORS, 180, 30, 0, 1);
}
void auditOnline(s32 active) { online = active; }
