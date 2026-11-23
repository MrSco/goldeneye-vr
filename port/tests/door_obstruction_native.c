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

static int check(const char *name, s32 expected)
{
    const s32 result = sub_GAME_7F0448A8(&g_Props[0]);
    if (result != expected) {
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
    failures += check("clear door", 1);
    roomProps[1] = 1;

    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_DIE;
    failures += check("dying guard clears door", 1);
    guard.actiontype = ACT_DEAD;
    failures += check("dead guard clears door", 1);
    failures += !!guard.hidden;

    guard.actiontype = ACT_STAND;
    failures += check("living guard blocks door", 0);
    failures += !(guard.hidden & CHRHIDDEN_OFFSCREEN_PATROL);
    g_Props[1] = (PropRecord){.type = PROP_TYPE_VIEWER};
    failures += check("player blocks door", 0);
    g_Props[1] = (PropRecord){.type = PROP_TYPE_OBJ, .obj = &crate};
    failures += check("crate blocks door", 0);

    g_Props[0].type = PROP_TYPE_OBJ;
    g_Props[1] = (PropRecord){.type = PROP_TYPE_CHR, .chr = &guard};
    guard.actiontype = ACT_DEAD;
    failures += check("corpse still blocks non-door objects", 0);
    if (!failures) puts("PASS: door obstruction");
    return failures != 0;
}
