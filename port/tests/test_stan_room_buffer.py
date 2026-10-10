"""A line or volume check whose walk fills all twenty rooms keeps its stack.

Crash report fb1c9718 (v0.4.16): a host's bot planned a path, the line test
noted twenty room changes and wrote its -1 terminator one word past its
twenty-word room buffer, into the stack canary ("stack corruption detected").
Runs the production stanTestLineUnobstructed and stanTestVolume with walkers
that report a full buffer, with array bounds trapped (the stack protector
alone misses it here: the stray word lands on another local, not the canary).
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
stan = (ROOT / 'src/game/stan.c').read_text(encoding='utf-8')
production = '\n'.join(block(stan, signature) for signature in (
    's32 stanTestLineUnobstructed(StandTile **pTile,',
    's32 stanTestVolume(StandTile **arg0,'))
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <ultra64.h>
#include "bondtypes.h"
#include "game/stan.h"
#include "game/chrai.h"
#include "game/chr.h"
#include "game/stanintersection.h"

StandTile *stanSavedColl_tile;
s32 stanSavedColl_pointI;
struct coord2d stanSavedColl_pntA, stanSavedColl_pntB;
f32 stanSavedColl_someMin;
PropRecord *stanSavedColl_posData;
f32 inv_level_scale = 1.0f;
s32 D_800413BC;
static s16 s_noProps[1] = { -1 };
s16 *ptr_list_object_lookup_indices = s_noProps;
PropRecord g_Props[1];
static s32 s_lists;

/* a walk across a stair of two rooms: a new entry at every change, up to the buffer */
static s32 fillRooms(s32 *rooms, s32 *count, s32 max)
{
    s32 i;
    for (i = 0; i < max; i++) rooms[i] = i & 1;
    *count = max;
    return max;
}
s32 sub_GAME_7F0B0C24(StandTile **tile, f32 sx, f32 sz, f32 dx, f32 dz, s32 *rooms, s32 *count, s32 max)
{
    fillRooms(rooms, count, max);
    return 1;
}
s32 sub_GAME_7F0B21B0(StandTile **tile, f32 x, f32 z, f32 radius, s32 *rooms, s32 *count, s32 max)
{
    fillRooms(rooms, count, max);
    return -1;
}
void roomGetProps(s32 *rooms)
{
    s32 i;
    for (i = 0; i < 20; i++) assert(rooms[i] == (i & 1));
    assert(rooms[20] == -1);
    s_lists++;
}
s32 propIsOfCdType(PropRecord *prop, s32 cdtypes) { return 0; }
void chraiGetCollisionBounds(PropRecord *prop, struct rect4f **polygon, s32 *edges, f32 *a, f32 *b) {}
static s32 gevrNetInsidePlayerProp(struct PropRecord *prop, f32 x, f32 z, const char *where) { return 0; }
static s32 gevrNetInsideDoorProp(PropRecord *prop, rect4f *polygon, s32 edges, f32 x, f32 z) { return 0; }
static s32 gevrInsideObjProp(PropRecord *prop, rect4f *polygon, s32 edges, f32 x, f32 z) { return 0; }
f32 calculateSegmentIntersectionFraction(coord2d *a, coord2d *b, coord2d *c, coord2d *d) { return 1.0f; }
bool doSegmentsIntersect(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f, f32 g, f32 h) { return 0; }
f32 stanGetPositionYValue(StandTile *tile, f32 x, f32 z) { return 0.0f; }
s32 walkTilesBetweenPoints_NoCallback(StandTile **tile, f32 sx, f32 sz, f32 dx, f32 dz) { return 1; }
f32 stanGetSignedPointLineDistance(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) { return 0.0f; }
f32 distBetweenPoints2d(f32 a, f32 b, f32 c, f32 d) { return 0.0f; }
bool stanPointProjectsOntoEdge(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) { return 0; }

/* PRODUCTION */

int main(void)
{
    StandTile floor, *tile = &floor;

    assert(stanTestLineUnobstructed(&tile, 0, 0, 100, 0, CDTYPE_PATHBLOCKER, 150.0f, 30.0f, 0.0f, 1.0f) == 1);
    tile = &floor;
    assert(stanTestVolume(&tile, 0, 0, 30.0f, CDTYPE_PATHBLOCKER, 0.0f, 0.0f) == -2);
    assert(s_lists == 2);
    puts("PASS: a full room walk keeps its terminator inside the buffer");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-stan-rooms-') as temp:
    source, exe = Path(temp) / 'test.c', Path(temp) / 'test.exe'
    source.write_text(fixture.replace('/* PRODUCTION */', production), encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'cc', '-std=gnu11', '-O0', '-fsanitize=bounds', '-fsanitize-undefined-trap-on-error',
                    '-D_LANGUAGE_C', '-DGEVR', '-DVERSION_US',
                    '-I' + str(ROOT), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src'),
                    '-I' + str(ROOT / 'port/include'), str(source), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
