/* The bots' floor routes (src/game/gevr_botnav.c) over a synthetic strip of tiles. */
#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

StandTile *standTileStart;
u8 list_of_tilesizes[] = { 0x20,0x20,0x20,0x20, 0x28,0x30,0x38,0x40, 0x48,0x50,0x58,0x00 };
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
void getTileMidPoint(StandTile *tile, coord3d *out) {
    s32 n = (tile->tail.half >> 12) & 0xf;
    out->x = out->y = out->z = 0;
    for (s32 i = 0; i < n; i++) { out->x += tile->points[i].x; out->y += tile->points[i].y; out->z += tile->points[i].z; }
    out->x /= n; out->y /= n; out->z /= n;
}
void getTileEdgePoints(StandTile *tile, s32 i, coord3d *a, coord3d *b) {
    s32 n = (tile->tail.half >> 12) & 0xf;
    a->x = tile->points[i].x; a->y = tile->points[i].y; a->z = tile->points[i].z;
    i = (i + 1) % n;
    b->x = tile->points[i].x; b->y = tile->points[i].y; b->z = tile->points[i].z;
}
static f32 s_walk_limit = 1e9f;   /* a straight walk reaches this far ahead */
s32 walkTilesBetweenPoints_NoCallback(StandTile **tile, f32 sx, f32 sz, f32 dx, f32 dz) {
    (void)tile; (void)sx; (void)dx;
    return dz - sz <= s_walk_limit;
}

/* A live rectangular prop footprint on otherwise connected floor. Use the
 * production collision-category selector: Library glass has both the path
 * blocker and AI-see-through flags, so a sight query misses it. */
static ObjectRecord s_object;
static PropRecord s_prop;
static s32 s_block_enabled;
static f32 s_minx, s_maxx, s_minz, s_maxz, s_bottom, s_top;
s32 propDoorGetCdTypes(PropRecord *prop) { (void)prop; return CDTYPE_CLOSEDDOORS; }
/* INSERT_PRODUCTION_CD_SELECTOR */

static s32 slab(f32 start, f32 delta, f32 lo, f32 hi, f32 *enter, f32 *leave) {
    f32 a, b;
    if (delta == 0) return start >= lo && start <= hi;
    a = (lo - start) / delta; b = (hi - start) / delta;
    if (a > b) { f32 temp = a; a = b; b = temp; }
    if (a > *enter) *enter = a;
    if (b < *leave) *leave = b;
    return *enter <= *leave;
}

s32 stanTestLineUnobstructed(StandTile **tile, f32 sx, f32 sz, f32 dx, f32 dz,
                            s32 cdtypes, f32 height, f32 bottom, f32 endheight, f32 endbottom) {
    f32 enter = 0, leave = 1;
    coord3d mid;
    (void)endheight; (void)endbottom;
    if (!walkTilesBetweenPoints_NoCallback(tile, sx, sz, dx, dz)) return FALSE;
    getTileMidPoint(*tile, &mid);
    if (!s_block_enabled || !propIsOfCdType(&s_prop, cdtypes)
        || s_top <= mid.y + bottom || mid.y + height <= s_bottom) return TRUE;
    return !(slab(sx, dx - sx, s_minx, s_maxx, &enter, &leave)
             && slab(sz, dz - sz, s_minz, s_maxz, &enter, &leave));
}

static void glass(f32 minx, f32 maxx, f32 z) {
    memset(&s_object, 0, sizeof(s_object));
    memset(&s_prop, 0, sizeof(s_prop));
    s_object.type = PROPDEF_GLASS;
    s_object.flags = PROPFLAG_00000800 | PROPFLAG_04000000;
    s_prop.type = PROP_TYPE_OBJ; s_prop.obj = &s_object;
    s_minx = minx; s_maxx = maxx; s_minz = z - 1; s_maxz = z + 1;
    s_bottom = 0; s_top = 200;
    s_block_enabled = TRUE;
}
#include "../../src/game/gevr_botnav.c"
struct StanPrefixRecord *stan_prefix;

/* count tiles in a row along +z, 100 units each, linked side to side */
static u8 s_floor[0x80 + 0x28 * 128 + 8];
static struct StanPrefixRecord s_prefix;
static StandTile *stripTile(s32 k) { return (StandTile *)(s_floor + 0x80 + 0x28 * k); }
static void strip(s32 count) {
    s_block_enabled = FALSE;
    memset(s_floor, 0, sizeof(s_floor));
    standTileStart = (StandTile *)s_floor;
    for (s32 k = 0; k < count; k++) {
        StandTile *t = stripTile(k);
        s16 z0 = (s16)(100 * k), z1 = (s16)(100 * k + 100);
        t->idhi = 1; t->room = 1; t->tail.half = 4 << 12;
        t->points[0].x = 0;   t->points[0].z = z0;
        t->points[1].x = 100; t->points[1].z = z0;
        t->points[2].x = 100; t->points[2].z = z1;
        t->points[3].x = 0;   t->points[3].z = z1;
        if (k > 0) t->points[0].link = (u16)(((u8 *)stripTile(k - 1) - s_floor) >> 3);
        if (k + 1 < count) t->points[2].link = (u16)(((u8 *)stripTile(k + 1) - s_floor) >> 3);
    }
    s_prefix.ptr_firstroom = stripTile(0);
    stan_prefix = &s_prefix;
    s_builtfor = NULL;
}

/* A 3x3 room: glass across the direct route, an opening to its right. */
static void room(void) {
    strip(9);
    for (s32 z = 0; z < 3; z++) for (s32 x = 0; x < 3; x++) {
        s32 k = z * 3 + x;
        StandTile *t = stripTile(k);
        for (s32 p = 0; p < 4; p++) {
            t->points[p].x = (p == 1 || p == 2 ? 100 : 0) + x * 100;
            t->points[p].z = (p >= 2 ? 100 : 0) + z * 100;
            t->points[p].link = 0;
        }
        if (z > 0) t->points[0].link = (u16)(((u8 *)stripTile(k - 3) - s_floor) >> 3);
        if (x < 2) t->points[1].link = (u16)(((u8 *)stripTile(k + 1) - s_floor) >> 3);
        if (z < 2) t->points[2].link = (u16)(((u8 *)stripTile(k + 3) - s_floor) >> 3);
        if (x > 0) t->points[3].link = (u16)(((u8 *)stripTile(k - 1) - s_floor) >> 3);
    }
}

EXPORT int test_botnav(void) {
    GevrBotRoute route;
    coord3d from = { 50, 0, 50 }, goal = { 50, 0, 950 }, aim;

    /* ten tiles: nine sides with middles between them, ending at the goal */
    strip(10);
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(9), &goal));
    CHECK(s_tilecount == 10 && s_edgecount == 18);
    CHECK(route.count == 18 && route.next == 0 && route.goaltile == stripTile(9));
    CHECK(route.points[0].x == 50 && route.points[0].z == 100);
    CHECK(route.points[16].z == 900 && route.points[17].z == 950);
    /* the follower cuts ahead as far as a straight walk reaches, four points at most */
    CHECK(gevrBotNavNext(&route, &from, stripTile(0), &aim) && route.next == 4 && aim.z == 300);
    s_walk_limit = 160;
    route.next = 0;
    CHECK(gevrBotNavNext(&route, &from, stripTile(0), &aim) && route.next == 2 && aim.z == 200);
    s_walk_limit = 1e9f;
    /* a reached point falls behind; past the last there is no more */
    coord3d there = { 50, 0, 945 };
    route.next = 17;
    CHECK(!gevrBotNavNext(&route, &there, stripTile(9), &aim));
    /* the same tile: the goal alone */
    coord3d samefrom = { 50, 0, 350 }, samegoal = { 50, 0, 370 };
    CHECK(gevrBotNavPlan(&route, stripTile(3), &samefrom, stripTile(3), &samegoal) && route.count == 1 && route.points[0].z == 370);
    /* longer than a route holds: its first part, from the start */
    strip(100);
    goal.z = 9950;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(99), &goal));
    CHECK(route.count == GEVR_BOT_ROUTE_POINTS && route.points[0].z == 100 && route.points[GEVR_BOT_ROUTE_POINTS - 1].z == 100 * (GEVR_BOT_ROUTE_POINTS / 2) + 50);
    /* unlinked: no route */
    stripTile(50)->points[0].link = 0; stripTile(49)->points[2].link = 0;
    s_builtfor = NULL;
    CHECK(!gevrBotNavPlan(&route, stripTile(0), &from, stripTile(99), &goal) && route.count == 0);
    navFree();
    return 0;
}

EXPORT int test_botnav_glass(void) {
    GevrBotRoute route;
    coord3d from = { 50, 0, 50 }, goal = { 50, 0, 250 }, aim;
    s32 detour;

    room(); glass(0, 200, 100);
    CHECK(propIsOfCdType(&s_prop, CDTYPE_PATHBLOCKER));
    CHECK(!propIsOfCdType(&s_prop, CDTYPE_PATHBLOCKER | CDTYPE_AIOPAQUE));
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(6), &goal));
    detour = FALSE;
    for (s32 i = 0; i < route.count; i++) if (route.points[i].x > 200) detour = TRUE;
    CHECK(detour);
    CHECK(gevrBotNavNext(&route, &from, stripTile(0), &aim));
    StandTile *walk = stripTile(0);
    CHECK(stanTestLineUnobstructed(&walk, from.x, from.z, aim.x, aim.z, CDTYPE_PATHBLOCKER, 150, 30, 0, 1));
    CHECK(aim.z < 100 || aim.x > 200); /* crosses only through the opening */

    /* Follow the detour in strides, including within-stride corner checks. */
    coord3d pos = from;
    s32 steps = 0;
    route.next = 0;
    while (gevrBotNavNext(&route, &pos, stripTile(0), &aim) && steps++ < 100) {
        f32 distance = navFlatDist(&pos, &aim);
        f32 fraction = distance > 20 ? 20 / distance : 1;
        pos.x += (aim.x - pos.x) * fraction;
        pos.z += (aim.z - pos.z) * fraction;
    }
    CHECK(steps < 100 && route.count > 0 && navFlatDist(&pos, &goal) < 40);

    /* Two clear legs via a middle need that middle in the output route:
     * directly joining their portals would cross an offset pane. */
    room(); glass(60, 70, 115);
    s_minz = 110; s_maxz = 120;
    stripTile(0)->points[1].link = 0; /* turn through tile 3, then tile 4 */
    s_builtfor = NULL;
    goal = (coord3d){ 150, 0, 150 };
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(4), &goal));
    CHECK(route.count == 4 && route.points[1].x == 50 && route.points[1].z == 150);
    walk = stripTile(3);
    CHECK(!stanTestLineUnobstructed(&walk, route.points[0].x, route.points[0].z,
                                   route.points[2].x, route.points[2].z, CDTYPE_PATHBLOCKER, 150, 30, 0, 1));
    pos = from;
    for (s32 i = 0; i < route.count; i++) {
        walk = stripTile(i / 2);
        CHECK(stanTestLineUnobstructed(&walk, pos.x, pos.z, route.points[i].x, route.points[i].z,
                                      CDTYPE_PATHBLOCKER, 150, 30, 0, 1));
        pos = route.points[i];
    }
    goal = (coord3d){ 50, 0, 250 };
    room(); glass(0, 200, 100);

    /* Breaking the pane opens the shorter path without rebuilding the graph. */
    s_block_enabled = FALSE;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(6), &goal));
    CHECK(route.count == 4 && route.points[0].x == 50 && route.points[0].z == 100);

    /* An existing route is invalidated when its next point becomes blocked. */
    glass(0, 200, 80);
    CHECK(!gevrBotNavNext(&route, &from, stripTile(0), &aim) && route.count == 0);

    /* A point within one stride but across glass is not treated as reached. */
    route.count = 2; route.next = 0;
    route.points[0] = (coord3d){ 50, 0, 85 }; route.points[1] = goal;
    CHECK(!gevrBotNavNext(&route, &from, stripTile(0), &aim) && route.next == 0);

    /* Glass cutting the first/last tile also blocks the actual endpoints. */
    strip(3); glass(0, 100, 80);
    CHECK(!gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));
    glass(0, 100, 220);
    CHECK(!gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));
    goal.z = 90; glass(0, 100, 80);
    CHECK(!gevrBotNavPlan(&route, stripTile(0), &from, stripTile(0), &goal));

    /* Floor-relative body height: overhead/below-floor glass does not block. */
    goal.z = 250; glass(0, 100, 100);
    s_bottom = 200; s_top = 400;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));
    s_bottom = -200; s_top = 0;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));

    /* A different storey's glass is ignored; glass on this floor blocks. */
    for (s32 k = 0; k < 3; k++) for (s32 p = 0; p < 4; p++) stripTile(k)->points[p].y = 300;
    s_builtfor = NULL;
    from.y = goal.y = 300;
    s_bottom = 0; s_top = 200;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));
    s_bottom = 300; s_top = 500;
    CHECK(!gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));

    /* Doors keep their floor routes; bots open them in gevr_bot.c. */
    s_prop.type = PROP_TYPE_DOOR;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(2), &goal));
    CHECK(gevrBotNavNext(&route, &from, stripTile(0), &aim));
    s_block_enabled = FALSE;
    navFree();
    return 0;
}
