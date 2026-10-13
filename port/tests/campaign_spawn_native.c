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
PropRecord g_Props[2], *stanSavedColl_posData;
static ObjectRecord objects[2];
static s16 props[3] = {-1, -1, -1};
s16 *ptr_list_object_lookup_indices = props;
s32 D_800413BC;
coord2d stanSavedColl_pntA, stanSavedColl_pntB;
f32 stanSavedColl_someMin;
#define STAN_LOCUS_TILESTACK_MAX 55
#define LOG_NOTE 1
#define LOG_ERROR 3
void sysLogPrintf(s32 level, const char *fmt, ...) {}
static s32 gevrNetInsidePlayerProp(PropRecord *p, f32 x, f32 z, const char *s) { return 0; }
static s32 gevrNetInsideDoorProp(PropRecord *p, rect4f *r, s32 n, f32 x, f32 z) { return 0; }
static s32 gevrInsideObjProp(PropRecord *p, rect4f *r, s32 n, f32 x, f32 z) { return 0; }
void roomGetProps(s32 *rooms) {
    int n = 0;
    if (obstacle) props[n++] = 0;
    if (wall) props[n++] = 1;
    props[n] = -1;
}
s32 propIsOfCdType(PropRecord *p, s32 types) { return (types & (p == &g_Props[0] ? CDTYPE_OBJS : CDTYPE_DOORS)) != 0; }
void chraiGetCollisionBounds(PropRecord *p, rect4f **polygon, s32 *vertices, f32 *top, f32 *bottom)
{
    static rect4f rect;
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
