/* Production floor traversal and spawn solver; object collision is supplied
 * by a fixture rectangle. ROM audits cover floor geometry, not loaded models. */
#include <ultra64.h>
#include <bondtypes.h>
#include "game/stan.h"
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
#define STAN_LOCUS_TILESTACK_MAX 55
#define LOG_NOTE 1
#define LOG_ERROR 3
void sysLogPrintf(s32 level, const char *fmt, ...) {}

/* INSERT_GEOMETRY */
/* INSERT_AI_SIZE */
s32 auditCollectedOpcode(void) { return AI_IFBondCollectedObject; }

s32 stanTestLineUnobstructed(StandTile **tile, f32 x, f32 z, f32 dx, f32 dz,
        int types, f32 a, f32 b, f32 c, f32 d)
{
    if (obstacle && fabsf(dx - ox) < oradius && fabsf(dz - oz) < oradius) return FALSE;
    return walkTilesBetweenPoints_NoCallback(tile, x, z, dx, dz);
}
s32 stanTestVolume(StandTile **tile, f32 x, f32 z, f32 radius, s32 types, f32 a, f32 b)
{
    if (obstacle && fabsf(x - ox) < oradius + radius && fabsf(z - oz) < oradius + radius) return 1;
    return sub_GAME_7F0B1DDC(tile, x, z, radius, NULL, NULL, NULL, NULL);
}

/* INSERT_SOLVER */
/* INSERT_START */
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
}
