/*
 * Bots' routes over the stage's floor tiles (the "stan" GoldenEye walks its
 * players and guards on). Multiplayer setups carry no path tables (the
 * guards' waypoints are the solo missions'), but every stage has its floor:
 * tiles linked edge to edge, as stan.c's own walks follow them
 * (stanFillin, sub_GAME_7F0B0914). Perfect Dark's simulants route over
 * waypoint groups (padhalllv.c navFindRoute); these route over the tiles
 * the same way, then walk the corners a straight walk cannot cut.
 *
 * The graph is built once a stage, the first time a bot asks: a node per
 * tile at its middle, an edge per linked side through that side's middle.
 */

#include <ultra64.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "gevr_bot.h"
#include "stan.h"

struct StanPrefixRecord {
    s32 stanfile;
    StandTile *ptr_firstroom;
};
extern struct StanPrefixRecord *stan_prefix;
extern StandTile *standTileStart;
extern u8 list_of_tilesizes[];
extern void getTileMidPoint(StandTile *tile, coord3d *out);
extern void getTileEdgePoints(StandTile *tile, s32 pointI, coord3d *currPntRtn, coord3d *nextPointRtn);
extern s32 walkTilesBetweenPoints_NoCallback(StandTile **tileStack, f32 start_x, f32 start_z, f32 dest_x, f32 dest_z);
extern void sysLogPrintf(s32 level, const char *fmt, ...);

#define NAV_MAX_EXPANDED 12000   /* one search's budget of tiles */
#define NAV_LADDER_COST 3.0f     /* special 3 (stan.c g_StanTileSpecialFlags) */

typedef struct NavTile {
    StandTile *tile;
    coord3d mid;
    s32 first;      /* the tile's edges in s_edges */
    s32 count;
} NavTile;

typedef struct NavEdge {
    s32 from;
    s32 to;
    coord3d portal; /* the middle of the shared side */
    f32 cost;
} NavEdge;

static NavTile *s_tiles;
static NavEdge *s_edges;
static s32 s_tilecount, s_edgecount;
static StandTile *s_builtfor;   /* the stage's first tile when built */

/* the A* state, reused between searches by stamp */
static f32 *s_cost;
static s32 *s_from;             /* the edge that reached the tile */
static s32 *s_area;             /* gevrBotNavMainArea: the glass-free area of each tile */
static s32 s_mainarea = -1;     /* the largest; -1 not worked out for this graph */
static u32 *s_seen;             /* the search that touched it */
static u8 *s_closed;
static u32 s_search;
typedef struct NavOpen { f32 f; s32 tile; } NavOpen;
static NavOpen *s_open;
static s32 s_opencount, s_opencap;

static s32 navPointCount(StandTile *tile)
{
    return (tile->tail.half >> 12) & 0xf;
}

static StandTile *navNextTile(StandTile *tile)
{
    return (StandTile *)((u8 *)tile + list_of_tilesizes[navPointCount(tile)]);
}

/* Tiles lie in the file in address order: a binary search finds one */
static s32 navIndexOf(StandTile *tile)
{
    s32 lo = 0;
    s32 hi = s_tilecount - 1;

    while (lo <= hi)
    {
        s32 mid = (lo + hi) / 2;

        if (s_tiles[mid].tile == tile) return mid;
        if ((uintptr_t)s_tiles[mid].tile < (uintptr_t)tile) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static f32 navDist(const coord3d *a, const coord3d *b)
{
    f32 dx = a->x - b->x;
    f32 dy = a->y - b->y;
    f32 dz = a->z - b->z;

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* Glass and other path blockers sit on connected floor tiles. They can be
 * transparent to sight while still stopping a player's body (Library's
 * panes). Check the standing body's height above the floor, without the
 * AI-opaque sight filter. Doors remain traversable: the bot opens them.
 * Query live collision so broken glass becomes traversable on the next plan. */
static s32 navWalkClear(StandTile *tile, const coord3d *from, const coord3d *to)
{
    return tile != NULL && stanTestLineUnobstructed(&tile, from->x, from->z, to->x, to->z,
                                                   CDTYPE_PATHBLOCKER, 150.0f, 30.0f, 0.0f, 1.0f);
}

/*
 * A search leg: across one tile, between its middle (or the bot, or the goal)
 * and a side it is linked through. The link already says the floor goes on,
 * so only a prop (glass) blocks it. The line test's own floor walk is 2D and
 * fails on tiles with next to no area seen from above (stair risers, slopes'
 * thin strips): Bunker II's stairs read as walls, and a bot spawned on its
 * first start pad had no route anywhere and stood there all match
 * (dedicated server P0, 2026-10-28). A shortcut over several tiles
 * (gevrBotNavNext) still takes the walls into account.
 */
static s32 s_navglass;   /* a route through glass: the links alone (gevrBotNavPlan) */

static s32 navLegClear(StandTile *tile, const coord3d *from, const coord3d *to)
{
    if (tile == NULL)
    {
        return FALSE;
    }
    return s_navglass || navWalkClear(tile, from, to) || stanSavedColl_posData == NULL;
}

static void navFree(void)
{
    free(s_tiles); free(s_edges); free(s_cost); free(s_from); free(s_seen); free(s_closed); free(s_open); free(s_area);
    s_tiles = NULL; s_edges = NULL; s_cost = NULL; s_from = NULL; s_seen = NULL; s_closed = NULL; s_open = NULL; s_area = NULL;
    s_tilecount = s_edgecount = s_opencap = 0;
    s_mainarea = -1;
    s_builtfor = NULL;
}

void gevrBotNavForget(void)
{
    s_builtfor = NULL;
}

static s32 navBuild(void)
{
    StandTile *first;
    StandTile *tile;
    s32 tiles = 0;
    s32 sides = 0;
    s32 i;

    if (stan_prefix == NULL || stan_prefix->ptr_firstroom == NULL)
    {
        return FALSE;
    }
    first = stan_prefix->ptr_firstroom;
    if (first == s_builtfor && s_tiles != NULL)
    {
        return TRUE;
    }
    navFree();
    for (tile = first; *(u32 *)tile; tile = navNextTile(tile))
    {
        tiles++;
        sides += navPointCount(tile);
    }
    if (tiles == 0)
    {
        return FALSE;
    }
    s_tiles = calloc(tiles, sizeof(*s_tiles));
    s_edges = calloc(sides ? sides : 1, sizeof(*s_edges));
    s_cost = calloc(tiles, sizeof(*s_cost));
    s_from = calloc(tiles, sizeof(*s_from));
    s_seen = calloc(tiles, sizeof(*s_seen));
    s_closed = calloc(tiles, sizeof(*s_closed));
    s_opencap = sides + tiles + 16;
    s_open = calloc(s_opencap, sizeof(*s_open));
    if (!s_tiles || !s_edges || !s_cost || !s_from || !s_seen || !s_closed || !s_open)
    {
        navFree();
        return FALSE;
    }
    s_tilecount = tiles;
    for (i = 0, tile = first; i < tiles; i++, tile = navNextTile(tile))
    {
        s_tiles[i].tile = tile;
        getTileMidPoint(tile, &s_tiles[i].mid);
    }
    s_edgecount = 0;
    for (i = 0; i < tiles; i++)
    {
        s32 n = navPointCount(s_tiles[i].tile);
        s32 e;

        s_tiles[i].first = s_edgecount;
        for (e = 0; e < n; e++)
        {
            u16 link = s_tiles[i].tile->points[e].link;
            StandTile *other;
            s32 to;
            coord3d a;
            coord3d b;
            NavEdge *edge;

            if ((link >> 4) == 0)
            {
                continue;
            }
            other = (StandTile *)((u8 *)standTileStart + ((u32)link << 3));
            to = navIndexOf(other);
            if (to < 0 || to == i)
            {
                continue;
            }
            getTileEdgePoints(s_tiles[i].tile, e, &a, &b);
            edge = &s_edges[s_edgecount++];
            edge->from = i;
            edge->to = to;
            edge->portal.x = (a.x + b.x) * 0.5f;
            edge->portal.y = (a.y + b.y) * 0.5f;
            edge->portal.z = (a.z + b.z) * 0.5f;
            edge->cost = navDist(&s_tiles[i].mid, &edge->portal) + navDist(&edge->portal, &s_tiles[to].mid);
            if (s_tiles[to].tile->mid.headerMid.special == 3 || s_tiles[i].tile->mid.headerMid.special == 3)
            {
                edge->cost *= NAV_LADDER_COST;
            }
        }
        s_tiles[i].count = s_edgecount - s_tiles[i].first;
    }
    s_builtfor = first;
    s_search = 0;
    sysLogPrintf(1, "bots: floor graph, %d tiles, %d links", s_tilecount, s_edgecount);
    return TRUE;
}

static void navPush(f32 f, s32 tile)
{
    s32 i;

    if (s_opencount >= s_opencap)
    {
        return;
    }
    i = s_opencount++;
    while (i > 0 && s_open[(i - 1) / 2].f > f)
    {
        s_open[i] = s_open[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    s_open[i].f = f;
    s_open[i].tile = tile;
}

static NavOpen navPop(void)
{
    NavOpen top = s_open[0];
    NavOpen last = s_open[--s_opencount];
    s32 i = 0;

    for (;;)
    {
        s32 c = i * 2 + 1;

        if (c >= s_opencount) break;
        if (c + 1 < s_opencount && s_open[c + 1].f < s_open[c].f) c++;
        if (s_open[c].f >= last.f) break;
        s_open[i] = s_open[c];
        i = c;
    }
    if (s_opencount > 0)
    {
        s_open[i] = last;
    }
    return top;
}

static s32 navPlan(GevrBotRoute *route, StandTile *fromtile, const coord3d *from,
                   StandTile *goaltile, const coord3d *goal);

/*
 * A route around glass first. When glass is the only way (Caverns' first
 * start pad looks out through two panes and has no other way out), a route
 * through it: the bot shoots the pane in its way (gevr_bot.c), as a player
 * there has to.
 */
s32 gevrBotNavPlan(GevrBotRoute *route, StandTile *fromtile, const coord3d *from,
                   StandTile *goaltile, const coord3d *goal)
{
    s32 found;

    route->glass = FALSE;
    if (navPlan(route, fromtile, from, goaltile, goal))
    {
        return TRUE;
    }
    s_navglass = TRUE;
    found = navPlan(route, fromtile, from, goaltile, goal);
    s_navglass = FALSE;
    route->glass = found;
    return found;
}

static s32 navPlan(GevrBotRoute *route, StandTile *fromtile, const coord3d *from,
                   StandTile *goaltile, const coord3d *goal)
{
    s32 start;
    s32 end;
    s32 expanded = 0;
    s32 found = FALSE;
    s32 count;
    s32 keep;
    s32 j;
    s32 t;

    route->count = 0;
    route->next = 0;
    route->goaltile = goaltile;
    if (fromtile == NULL || goaltile == NULL || !navBuild())
    {
        return FALSE;
    }
    start = navIndexOf(fromtile);
    end = navIndexOf(goaltile);
    if (start < 0 || end < 0)
    {
        return FALSE;
    }
    if (++s_search == 0)
    {
        memset(s_seen, 0, s_tilecount * sizeof(*s_seen));
        s_search = 1;
    }
    s_opencount = 0;
    s_seen[start] = s_search;
    s_closed[start] = FALSE;
    s_cost[start] = 0.0f;
    s_from[start] = -1;
    navPush(navDist(from, goal), start);
    while (s_opencount > 0 && expanded < NAV_MAX_EXPANDED)
    {
        NavOpen open = navPop();
        s32 i = open.tile;
        s32 e;

        if (s_closed[i]) continue;
        s_closed[i] = TRUE;
        expanded++;
        if (i == end)
        {
            if (i == start && !navLegClear(fromtile, from, goal))
            {
                break;
            }
            found = TRUE;
            break;
        }
        for (e = s_tiles[i].first; e < s_tiles[i].first + s_tiles[i].count; e++)
        {
            s32 to = s_edges[e].to;
            f32 cost = s_cost[i] + s_edges[e].cost;
            const coord3d *entry = i == start ? from : &s_tiles[i].mid;
            const coord3d *exit = to == end ? goal : &s_tiles[to].mid;

            if (s_seen[to] == s_search && (s_closed[to] || cost >= s_cost[to]))
            {
                continue;
            }
            if (!navLegClear(s_tiles[i].tile, entry, &s_edges[e].portal)
                || !navLegClear(s_tiles[to].tile, &s_edges[e].portal, exit))
            {
                continue;
            }

            if (s_seen[to] != s_search)
            {
                s_seen[to] = s_search;
                s_closed[to] = FALSE;
            }
            s_cost[to] = cost;
            s_from[to] = e;
            navPush(cost + navDist(&s_tiles[to].mid, goal), to);
        }
    }
    if (!found)
    {
        return FALSE;
    }
    /* Keep the tile middles as well as the sides: the search checked a path
     * via those middles. A direct line between two portals in one tile may
     * cut through glass even when the two legs via its middle are clear. */
    count = 0;
    for (t = end; s_from[t] >= 0; t = s_edges[s_from[t]].from)
    {
        count++;
    }
    keep = count < GEVR_BOT_ROUTE_POINTS / 2 ? count : GEVR_BOT_ROUTE_POINTS / 2;
    for (j = 0, t = end; s_from[t] >= 0; j++, t = s_edges[s_from[t]].from)
    {
        if (count - 1 - j < keep)
        {
            s32 point = (count - 1 - j) * 2;

            route->points[point] = s_edges[s_from[t]].portal;
            route->points[point + 1] = t == end ? *goal : s_tiles[t].mid;
        }
    }
    if (count > keep)
    {
        /* longer than a route holds: walk its first part, then plan again */
        route->count = keep * 2;
        return TRUE;
    }
    if (count == 0)
    {
        route->points[0] = *goal;
        route->count = 1;
    }
    else
    {
        route->count = count * 2;
    }
    return TRUE;
}

static f32 navFlatDist(const coord3d *a, const coord3d *b)
{
    f32 dx = a->x - b->x;
    f32 dz = a->z - b->z;

    return sqrtf(dx * dx + dz * dz);
}

s32 gevrBotNavNext(GevrBotRoute *route, const coord3d *pos, StandTile *tile, coord3d *aim)
{
    s32 k;
    s32 best;

    if (route->count <= 0)
    {
        return FALSE;
    }
    /* The legs as the search checked them (navLegClear): glass, not the 2D
     * floor walk, which fails on stairs; a route through glass ignores it. */
    s_navglass = route->glass;
    /* Nearby points are reached only on this side of any glass. */
    while (route->next < route->count && navFlatDist(pos, &route->points[route->next]) < 40.0f
           && navLegClear(tile, pos, &route->points[route->next]))
    {
        /* Near a corner, finish approaching this point if skipping it would
         * put glass across the next leg. Replan if it is actually reached
         * and the next leg has become blocked. */
        if (route->next + 1 < route->count && navFlatDist(pos, &route->points[route->next]) > 1.0f
            && !navLegClear(tile, pos, &route->points[route->next + 1]))
        {
            break;
        }
        route->next++;
    }
    if (route->next >= route->count)
    {
        s_navglass = FALSE;
        return FALSE;
    }
    /* An obstacle added since planning, or a bot displaced off its route:
     * let the caller replan instead of walking into the same pane forever. */
    if (!navLegClear(tile, pos, &route->points[route->next]))
    {
        s_navglass = FALSE;
        route->count = 0;
        return FALSE;
    }
    s_navglass = FALSE;
    /* Cut corners only when both floor and path blockers allow the shortcut. */
    best = route->next;
    for (k = route->next + 1; tile != NULL && k < route->count && k <= route->next + 4; k++)
    {
        if (!navWalkClear(tile, pos, &route->points[k]))
        {
            break;
        }
        best = k;
    }
    route->next = best;
    *aim = route->points[best];
    return TRUE;
}

/*
 * Whether a tile is in the stage's main floor area: the largest stretch of
 * linked floor a player walks without passing glass. Worked out once a graph,
 * at the first question, before any pane is broken, so it is the same on
 * every headset. The online start pads added at ammo spots use it (bondview_r.c
 * gevrAddOnlineStartPads): Stack's and Library's farthest ammo crate sat in a
 * four-tile case walled with glass, and the player who started there was shut
 * in (dedicated server P0, 2026-10-28).
 */
s32 gevrBotNavMainArea(StandTile *tile)
{
    s32 index;

    if (tile == NULL || !navBuild() || (index = navIndexOf(tile)) < 0)
    {
        return FALSE;
    }
    if (s_mainarea < 0)
    {
        s32 *stack = malloc(s_tilecount * sizeof(*stack));
        s32 areas = 0;
        s32 best = 0;
        s32 i;

        if (s_area == NULL)
        {
            s_area = malloc(s_tilecount * sizeof(*s_area));
        }
        if (stack == NULL || s_area == NULL)
        {
            free(stack);
            return TRUE;   /* no memory to tell: let every spot through, as before */
        }
        for (i = 0; i < s_tilecount; i++)
        {
            s_area[i] = -1;
        }
        for (i = 0; i < s_tilecount; i++)
        {
            s32 top = 0;
            s32 size = 0;

            if (s_area[i] >= 0)
            {
                continue;
            }
            s_area[i] = areas;
            stack[top++] = i;
            while (top > 0)
            {
                s32 t = stack[--top];
                s32 e;

                size++;
                for (e = s_tiles[t].first; e < s_tiles[t].first + s_tiles[t].count; e++)
                {
                    s32 to = s_edges[e].to;

                    if (s_area[to] >= 0
                        || !navLegClear(s_tiles[t].tile, &s_tiles[t].mid, &s_edges[e].portal)
                        || !navLegClear(s_tiles[to].tile, &s_edges[e].portal, &s_tiles[to].mid))
                    {
                        continue;
                    }
                    s_area[to] = areas;
                    stack[top++] = to;
                }
            }
            if (size > best)
            {
                best = size;
                s_mainarea = areas;
            }
            areas++;
        }
        free(stack);
        sysLogPrintf(1, "bots: %d floor areas between glass; the main one %d of %d tiles", areas, best, s_tilecount);
    }
    return s_area[index] == s_mainarea;
}

StandTile *gevrBotNavTileAt(const coord3d *pos, StandTile *near)
{
    StandTile *walk = near;
    coord3d mid;

    if (near == NULL)
    {
        return NULL;
    }
    getTileMidPoint(near, &mid);
    if (walkTilesBetweenPoints_NoCallback(&walk, mid.x, mid.z, pos->x, pos->z))
    {
        return walk;
    }
    return near;
}
