#include <stdio.h>
#include "bondtypes.h"
#include "bondconstants.h"
#include "game/chrai.h"
#include "game/chraction.h"
#include "game/propobj.h"

PropRecord g_Props[2];
static s16 roomProps[] = {0, 1, -1};
s16 *ptr_list_object_lookup_indices = roomProps;
static struct rect4f overlap[4];

void chraiGetCollisionBounds(PropRecord *prop, struct rect4f **polygon, s32 *edges, f32 *top, f32 *bottom)
{
    (void)prop;
    *polygon = overlap;
    *edges = 4;
    *top = 100.f;
    *bottom = 0.f;
}

void chraiGetPropRoomIds(PropRecord *prop, s32 *roomids)
{
    (void)prop;
    roomids[0] = -1;
}

void roomGetProps(s32 *roomids) { (void)roomids; }

void chrpropGetCollisionBounds(PropRecord *prop, f32 *radius, f32 *top, f32 *bottom)
{
    (void)prop;
    *radius = 24.f;
    *top = 100.f;
    *bottom = 0.f;
}

f32 sub_GAME_7F03CFE8(PropRecord *prop) { (void)prop; return 0.f; }

static ChrRecord *doorHit;
static int doorHits;
void gevrBodyDoorHit(ChrRecord *chr) { doorHit = chr; doorHits++; }

s32 chrpropTestPointInPolygon(coord3d *point, struct rect4f *polygon, s32 edges)
{
    (void)point;
    (void)polygon;
    (void)edges;
    return 1;
}

s32 chrobjTestPointPolygonCollision(coord3d *point, f32 radius, struct rect4f *polygon, s32 edges)
{
    (void)point;
    (void)radius;
    (void)polygon;
    (void)edges;
    return 1;
}

s32 chrobjTestPolygonsTouchingOrOverlap2D(struct rect4f *a, s32 na, struct rect4f *b, s32 nb)
{
    (void)a;
    (void)na;
    (void)b;
    (void)nb;
    return 1;
}

/* INSERT_PRODUCTION_CODE */

static int check(const char *name, s32 expected, int hits)
{
    doorHit = NULL;
    doorHits = 0;
    const s32 result = sub_GAME_7F0448A8(&g_Props[0]);
    if (result != expected || doorHits != hits
        || (hits && doorHit != g_Props[1].chr)) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    ChrRecord guard = {0};
    ObjectRecord crate = {.type = PROPDEF_PROP};
    int failures = 0;
    g_Props[0].type = PROP_TYPE_DOOR;
    roomProps[1] = -1;
    failures += check("clear door", 1, 0);
    roomProps[1] = 1;

    /* A falling guard blocks as in the original; a body on the floor also
     * blocks, but the door touching it starts its fade. */
    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_DIE;
    failures += check("dying guard blocks door without fading", 0, 0);
    guard.actiontype = ACT_DEAD;
    failures += check("dead guard blocks door and starts fading", 0, 1);

    guard.actiontype = ACT_STAND;
    guard.hidden = 0;
    failures += check("living guard blocks door", 0, 0);
    failures += !(guard.hidden & CHRHIDDEN_OFFSCREEN_PATROL);
    g_Props[1] = (PropRecord){.type = PROP_TYPE_VIEWER};
    failures += check("player blocks door", 0, 0);
    g_Props[1] = (PropRecord){.type = PROP_TYPE_OBJ, .obj = &crate};
    failures += check("crate blocks door", 0, 0);

    g_Props[0].type = PROP_TYPE_OBJ;
    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_DEAD;
    failures += check("corpse blocks non-door objects without fading", 0, 0);
    if (!failures) puts("PASS: door obstruction");
    return failures != 0;
}
