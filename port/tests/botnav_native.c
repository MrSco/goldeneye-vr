/* The bots' floor routes (src/game/gevr_botnav.c) over a synthetic strip of tiles. */
#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
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
#include "../../src/game/gevr_botnav.c"
struct StanPrefixRecord *stan_prefix;

/* count tiles in a row along +z, 100 units each, linked side to side */
static u8 s_floor[0x80 + 0x28 * 128 + 8];
static struct StanPrefixRecord s_prefix;
static StandTile *stripTile(s32 k) { return (StandTile *)(s_floor + 0x80 + 0x28 * k); }
static void strip(s32 count) {
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

EXPORT int test_botnav(void) {
    GevrBotRoute route;
    coord3d from = { 50, 0, 50 }, goal = { 50, 0, 950 }, aim;

    /* ten tiles: nine sides, then the goal, in walking order */
    strip(10);
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(9), &goal));
    CHECK(s_tilecount == 10 && s_edgecount == 18);
    CHECK(route.count == 10 && route.next == 0 && route.goaltile == stripTile(9));
    CHECK(route.points[0].x == 50 && route.points[0].z == 100);
    CHECK(route.points[8].z == 900 && route.points[9].z == 950);
    /* the follower cuts ahead as far as a straight walk reaches, four points at most */
    CHECK(gevrBotNavNext(&route, &from, stripTile(0), &aim) && route.next == 4 && aim.z == 500);
    s_walk_limit = 260;
    route.next = 0;
    CHECK(gevrBotNavNext(&route, &from, stripTile(0), &aim) && route.next == 2 && aim.z == 300);
    s_walk_limit = 1e9f;
    /* a reached point falls behind; past the last there is no more */
    coord3d there = { 50, 0, 945 };
    route.next = 9;
    CHECK(!gevrBotNavNext(&route, &there, stripTile(9), &aim));
    /* the same tile: the goal alone */
    CHECK(gevrBotNavPlan(&route, stripTile(3), &from, stripTile(3), &goal) && route.count == 1 && route.points[0].z == 950);
    /* longer than a route holds: its first part, from the start */
    strip(100);
    goal.z = 9950;
    CHECK(gevrBotNavPlan(&route, stripTile(0), &from, stripTile(99), &goal));
    CHECK(route.count == GEVR_BOT_ROUTE_POINTS - 1 && route.points[0].z == 100 && route.points[GEVR_BOT_ROUTE_POINTS - 2].z == 100 * (GEVR_BOT_ROUTE_POINTS - 1));
    /* unlinked: no route */
    stripTile(50)->points[0].link = 0; stripTile(49)->points[2].link = 0;
    s_builtfor = NULL;
    CHECK(!gevrBotNavPlan(&route, stripTile(0), &from, stripTile(99), &goal) && route.count == 0);
    navFree();
    return 0;
}
